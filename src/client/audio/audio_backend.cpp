#ifdef WITH_AUDIO

#include "client/audio/audio_backend.h"
#include "client/audio/audio_callback_probe.h"
#include "client/audio/audio_mixer.h"
#include "common/logging.h"

#include <vector>
#include <cstring>
#include <chrono>
#include <ctime>

// miniaudio implementation — only in this .cpp
#define MA_IMPLEMENTATION
#define MA_NO_ENCODING       // We don't need encoding
#define MA_NO_GENERATION     // We don't need waveform generation
#include "miniaudio.h"

#ifdef WILLEQ_T130_AUDIO_TID_PROBE
// T130 diagnostic only — NOT part of a normal build (opt-in compile define,
// unset by default). Writes the audio callback thread's tid to a fixed path
// exactly once, so an out-of-process LD_PRELOAD allocator interposer
// (review/T130/audio_alloc_interposer.c) can attribute allocations to this
// specific thread. Allocation-free (direct write() syscall, stack-only
// snprintf) and gated by a std::atomic so it costs one relaxed load per
// callback after the first.
#include <atomic>
#include <cstdio>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
static std::atomic<bool> g_t130TidWritten{false};
static void t130WriteAudioTidOnce() {
    bool expected = false;
    if (!g_t130TidWritten.compare_exchange_strong(expected, true)) return;
    long tid = static_cast<long>(syscall(SYS_gettid));
    const char* path = std::getenv("WILLEQ_T130_TID_FILE");
    if (!path) path = "/tmp/willeq_t130_audio_tid";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%ld\n", tid);
        if (n > 0) { ssize_t w = write(fd, buf, static_cast<size_t>(n)); (void)w; }
        close(fd);
    }
}
#endif  // WILLEQ_T130_AUDIO_TID_PROBE

namespace EQT {
namespace Audio {

// ─── MiniaudioBackend ───────────────────────────────────────────────────────

struct MiniaudioBackend::Impl {
    ma_device device;
    bool deviceInitialized = false;
    AudioMixer* mixer = nullptr;
};

// T130: monotonic timestamp for AudioCallbackProbe. clock_gettime(CLOCK_MONOTONIC)
// is vDSO-resolved on Linux -- no kernel entry, no context switch -- so this
// is compatible with the real-time callback's no-syscalls constraint (same
// reasoning T122's own probe used for its timestamps).
static inline int64_t t130MonotonicNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static void miniaudioDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
#ifdef WILLEQ_T130_AUDIO_TID_PROBE
    t130WriteAudioTidOnce();
#endif
    AudioCallbackProbe::onCallback(t130MonotonicNs());
    auto* mixer = static_cast<AudioMixer*>(pDevice->pUserData);
    if (mixer) {
        mixer->render(static_cast<float*>(pOutput), static_cast<int>(frameCount));
    }
}

MiniaudioBackend::MiniaudioBackend()
    : impl_(std::make_unique<Impl>()) {
}

MiniaudioBackend::~MiniaudioBackend() {
    stop();
}

bool MiniaudioBackend::init(AudioMixer* mixer, uint32_t sampleRate) {
    impl_->mixer = mixer;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = sampleRate;
    config.dataCallback      = miniaudioDataCallback;
    config.pUserData         = mixer;
    config.periodSizeInFrames = 512;

    ma_result result = ma_device_init(nullptr, &config, &impl_->device);
    if (result != MA_SUCCESS) {
        LOG_ERROR(MOD_AUDIO, "Failed to initialize miniaudio device: {}", static_cast<int>(result));
        return false;
    }

    // T130: configure the callback-timing probe's expected period so it can
    // classify "late" callbacks. sampleRate is validated >0 by the caller
    // chain (AudioManager); config.periodSizeInFrames is the literal 512 set
    // above.
    if (sampleRate > 0) {
        int64_t periodNs = (static_cast<int64_t>(config.periodSizeInFrames) * 1000000000LL) /
                            static_cast<int64_t>(sampleRate);
        AudioCallbackProbe::configure(periodNs);
    }

    impl_->deviceInitialized = true;
    LOG_INFO(MOD_AUDIO, "Miniaudio backend initialized ({}Hz, stereo)", sampleRate);
    return true;
}

void MiniaudioBackend::start() {
    if (impl_->deviceInitialized) {
        ma_result result = ma_device_start(&impl_->device);
        if (result != MA_SUCCESS) {
            LOG_ERROR(MOD_AUDIO, "Failed to start miniaudio device: {}", static_cast<int>(result));
        }
    }
}

void MiniaudioBackend::stop() {
    if (impl_->deviceInitialized) {
        ma_device_uninit(&impl_->device);
        impl_->deviceInitialized = false;
    }
}

// ─── RDPAudioBackend ────────────────────────────────────────────────────────

RDPAudioBackend::RDPAudioBackend() = default;

RDPAudioBackend::~RDPAudioBackend() {
    stop();
}

bool RDPAudioBackend::init(AudioMixer* mixer, uint32_t sampleRate) {
    mixer_ = mixer;
    sampleRate_ = sampleRate;
    return true;
}

void RDPAudioBackend::start() {
    if (running_.load()) return;
    running_ = true;
    thread_ = std::thread(&RDPAudioBackend::pumpThread, this);
}

void RDPAudioBackend::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void RDPAudioBackend::setOutputCallback(AudioOutputCallback callback) {
    outputCallback_ = std::move(callback);
}

void RDPAudioBackend::pumpThread() {
    static constexpr size_t BUFFER_FRAMES = 1024;
    std::vector<float> floatBuf(BUFFER_FRAMES * 2);
    std::vector<int16_t> s16Buf(BUFFER_FRAMES * 2);

    LOG_DEBUG(MOD_AUDIO, "RDP audio pump thread started");

    while (running_.load()) {
        if (mixer_) {
            mixer_->render(floatBuf.data(), BUFFER_FRAMES);
        } else {
            std::memset(floatBuf.data(), 0, floatBuf.size() * sizeof(float));
        }

        // Convert float -> s16
        for (size_t i = 0; i < floatBuf.size(); ++i) {
            float s = floatBuf[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            s16Buf[i] = static_cast<int16_t>(s * 32767.0f);
        }

        if (outputCallback_) {
            outputCallback_(s16Buf.data(), BUFFER_FRAMES, sampleRate_, 2);
        }

        // Sleep to maintain sample rate: 1024 frames / sampleRate seconds
        auto sleepUs = (BUFFER_FRAMES * 1000000ULL) / sampleRate_;
        std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        FlushThreadLog();
    }

    LOG_DEBUG(MOD_AUDIO, "RDP audio pump thread stopped");
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
