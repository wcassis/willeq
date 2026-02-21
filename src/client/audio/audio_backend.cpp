#ifdef WITH_AUDIO

#include "client/audio/audio_backend.h"
#include "client/audio/audio_mixer.h"
#include "common/logging.h"

#include <vector>
#include <cstring>
#include <chrono>

// miniaudio implementation — only in this .cpp
#define MA_IMPLEMENTATION
#define MA_NO_ENCODING       // We don't need encoding
#define MA_NO_GENERATION     // We don't need waveform generation
#include "miniaudio.h"

namespace EQT {
namespace Audio {

// ─── MiniaudioBackend ───────────────────────────────────────────────────────

struct MiniaudioBackend::Impl {
    ma_device device;
    bool deviceInitialized = false;
    AudioMixer* mixer = nullptr;
};

static void miniaudioDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
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
    }

    LOG_DEBUG(MOD_AUDIO, "RDP audio pump thread stopped");
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
