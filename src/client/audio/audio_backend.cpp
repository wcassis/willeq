#ifdef WITH_AUDIO

#include "client/audio/audio_backend.h"
#include "client/audio/audio_callback_probe.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/polyphase_resampler.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"

#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>

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

// ─── PcmTap (B1069 diagnostic) ──────────────────────────────────────────────

// WILLEQ_AUDIO_DUMP=<file.wav> [WILLEQ_AUDIO_DUMP_SECONDS=<n>, default 30]
// records exactly what the callback hands miniaudio (device rate + format)
// to <file.wav>, and the mixer's own output (f32 at the mixer rate, before
// the resampler) to <file.wav>.mix.wav, so the two can be compared offline
// for the same input. Off unless the variable is set. The buffers are
// allocated in init(); the callback only memcpy()s into them (no I/O, no
// allocation); the files are written in stop().
struct PcmTap {
    std::vector<uint8_t> buf;
    size_t pos = 0;  // audio thread writes; read after the device is stopped
    void reserve(size_t bytes) { buf.assign(bytes, 0); pos = 0; }
    void append(const void* data, size_t bytes) {
        if (buf.empty() || pos >= buf.size()) return;
        const size_t n = std::min(bytes, buf.size() - pos);
        std::memcpy(buf.data() + pos, data, n);
        pos += n;
    }
    // formatTag 1 = PCM, 3 = IEEE float.
    bool writeWav(const std::string& path, uint16_t formatTag, uint16_t bits,
                  uint32_t rate, uint16_t channels) const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        auto w32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto w16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        const uint32_t dataBytes = static_cast<uint32_t>(pos);
        const uint16_t blockAlign = static_cast<uint16_t>(channels * (bits / 8));
        std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes);
        std::fwrite("WAVEfmt ", 1, 8, f); w32(16);
        w16(formatTag); w16(channels); w32(rate); w32(rate * blockAlign);
        w16(blockAlign); w16(bits);
        std::fwrite("data", 1, 4, f); w32(dataBytes);
        std::fwrite(buf.data(), 1, pos, f);
        std::fclose(f);
        return true;
    }
};

// ─── DeviceRateAdapter (B1069) ──────────────────────────────────────────────

// Largest device-rate pull rendered in one resampler pass. A callback asking
// for more is split into chunks, so the scratch below never grows on the
// audio thread.
static constexpr uint32_t kAdapterChunkFrames = 2048;

struct DeviceRateAdapter::Impl {
    AudioMixer* mixer = nullptr;
    uint32_t mixRate = 0;
    uint32_t deviceRate = 0;
    bool passthrough = true;
    bool resamplerInitialized = false;
    ma_resampler resampler;        // fallback only (a rate pair too odd for the polyphase table)
    bool polyphase = false;
    PolyphaseResampler poly;       // B1069 acceptance 2: the default path
    std::vector<float> mixScratch;  // mixer-rate frames, interleaved stereo
    std::atomic<uint64_t> mixerFrames{0};
    PcmTap* mixTap = nullptr;       // B1069 diagnostic (WILLEQ_AUDIO_DUMP)

    ~Impl() {
        if (resamplerInitialized) ma_resampler_uninit(&resampler, nullptr);
    }
};

DeviceRateAdapter::DeviceRateAdapter() : impl_(std::make_unique<Impl>()) {}
DeviceRateAdapter::~DeviceRateAdapter() = default;

bool DeviceRateAdapter::init(AudioMixer* mixer, uint32_t mixRate, uint32_t deviceRate) {
    if (impl_->resamplerInitialized) {
        ma_resampler_uninit(&impl_->resampler, nullptr);
        impl_->resamplerInitialized = false;
    }
    impl_->mixer = mixer;
    impl_->mixRate = mixRate;
    impl_->deviceRate = deviceRate;
    impl_->mixerFrames.store(0, std::memory_order_relaxed);
    impl_->passthrough = (mixRate == deviceRate) || mixRate == 0 || deviceRate == 0;
    impl_->polyphase = false;
    if (impl_->passthrough) {
        impl_->mixScratch.clear();
        return mixRate == deviceRate;
    }

    // B1069 acceptance 2: band-limited polyphase sinc (polyphase_resampler.h
    // says why the linear resampler was audibly distorted).
    if (impl_->poly.init(mixRate, deviceRate, kAdapterChunkFrames)) {
        impl_->polyphase = true;
        impl_->mixScratch.clear();
        return true;
    }
    LOG_WARN(MOD_AUDIO, "DeviceRateAdapter: {} -> {} Hz needs more than {} phases; "
             "falling back to linear resampling", mixRate, deviceRate,
             PolyphaseResampler::kMaxPhases);

    ma_resampler_config rc = ma_resampler_config_init(
        ma_format_f32, 2, mixRate, deviceRate, ma_resample_algorithm_linear);
    if (ma_resampler_init(&rc, nullptr, &impl_->resampler) != MA_SUCCESS) {
        LOG_ERROR(MOD_AUDIO, "DeviceRateAdapter: ma_resampler_init {} -> {} Hz failed",
                  mixRate, deviceRate);
        impl_->passthrough = true;
        return false;
    }
    impl_->resamplerInitialized = true;

    // The most mixer frames one chunk can need: ask the resampler itself,
    // plus a margin for its fractional state at any later point.
    ma_uint64 need = 0;
    ma_resampler_get_required_input_frame_count(&impl_->resampler, kAdapterChunkFrames, &need);
    const size_t maxIn = static_cast<size_t>(need) + 8;
    impl_->mixScratch.assign(maxIn * 2, 0.0f);
    return true;
}

void DeviceRateAdapter::render(float* output, uint32_t frameCount) {
    if (!impl_->mixer) {
        std::memset(output, 0, static_cast<size_t>(frameCount) * 2 * sizeof(float));
        return;
    }
    if (impl_->passthrough) {
        impl_->mixer->render(output, static_cast<int>(frameCount));
        impl_->mixerFrames.fetch_add(frameCount, std::memory_order_relaxed);
        if (impl_->mixTap) impl_->mixTap->append(output, static_cast<size_t>(frameCount) * 2 * sizeof(float));
        return;
    }

    if (impl_->polyphase) {
        uint32_t done = 0;
        while (done < frameCount) {
            const uint32_t chunk = std::min(frameCount - done, kAdapterChunkFrames);
            const uint32_t need = impl_->poly.inputFramesFor(chunk);
            float* in = impl_->poly.inputBuffer();
            if (need > 0) {
                impl_->mixer->render(in, static_cast<int>(need));
                if (impl_->mixTap) impl_->mixTap->append(in, static_cast<size_t>(need) * 2 * sizeof(float));
            }
            impl_->poly.process(output + static_cast<size_t>(done) * 2, chunk);
            impl_->mixerFrames.fetch_add(need, std::memory_order_relaxed);
            done += chunk;
        }
        return;
    }

    const size_t maxIn = impl_->mixScratch.size() / 2;
    uint32_t done = 0;
    while (done < frameCount) {
        const uint32_t chunk = std::min(frameCount - done, kAdapterChunkFrames);
        ma_uint64 need = 0;
        ma_resampler_get_required_input_frame_count(&impl_->resampler, chunk, &need);
        if (need > maxIn) need = maxIn;  // cannot happen (sized in init); never overrun
        if (need > 0) {
            impl_->mixer->render(impl_->mixScratch.data(), static_cast<int>(need));
            if (impl_->mixTap) {
                impl_->mixTap->append(impl_->mixScratch.data(),
                                      static_cast<size_t>(need) * 2 * sizeof(float));
            }
        }
        ma_uint64 inFrames = need;
        ma_uint64 outFrames = chunk;
        float* dst = output + static_cast<size_t>(done) * 2;
        ma_resampler_process_pcm_frames(&impl_->resampler, impl_->mixScratch.data(), &inFrames,
                                        dst, &outFrames);
        impl_->mixerFrames.fetch_add(inFrames, std::memory_order_relaxed);
        if (outFrames < chunk) {
            // The resampler produced less than it said it would: pad with
            // silence rather than hand the device stale memory.
            std::memset(dst + outFrames * 2, 0,
                        static_cast<size_t>(chunk - outFrames) * 2 * sizeof(float));
        }
        done += chunk;
    }
}

bool DeviceRateAdapter::isPassthrough() const { return impl_->passthrough; }
void DeviceRateAdapter::setMixTap(PcmTap* tap) { impl_->mixTap = tap; }
const char* DeviceRateAdapter::resamplerName() const {
    return impl_->passthrough ? "off" : (impl_->polyphase ? "sinc48" : "linear");
}
uint32_t DeviceRateAdapter::mixRate() const { return impl_->mixRate; }
uint32_t DeviceRateAdapter::deviceRate() const { return impl_->deviceRate; }
uint64_t DeviceRateAdapter::mixerFramesPulled() const {
    return impl_->mixerFrames.load(std::memory_order_relaxed);
}

// ─── MiniaudioBackend ───────────────────────────────────────────────────────

// B1069: short, space-free names for the init log (ma_get_format_name()'s
// "16-bit Signed Integer" does not grep as one token). "s24" is miniaudio's
// tightly packed 24-bit; how it maps an ALSA S24_LE (24-in-32) device is
// miniaudio's call, so read this field, not the ALSA name, off the log.
static const char* shortFormatName(ma_format f) {
    switch (f) {
        case ma_format_u8:  return "u8";
        case ma_format_s16: return "s16";
        case ma_format_s24: return "s24";
        case ma_format_s32: return "s32";
        case ma_format_f32: return "f32";
        default:            return "unknown";
    }
}

struct MiniaudioBackend::Impl {
    ma_context context;
    bool contextInitialized = false;
    ma_device device;
    bool deviceInitialized = false;
    AudioMixer* mixer = nullptr;
    DeviceRateAdapter adapter;
    uint32_t mixRate = 0;
    uint32_t requestedRate = 0;
    DeviceFormat requestedFormat = kDefaultDeviceFormat;
    // B1069: the callback's output format (ma_device.playback.format). Not
    // f32 -> the adapter renders f32 into f32Scratch (allocated in init) and
    // ma_pcm_convert writes the device's format, chunk by chunk.
    ma_format outFormat = ma_format_f32;
    std::vector<float> f32Scratch;
    // B1069 diagnostic taps (PcmTap above); empty unless WILLEQ_AUDIO_DUMP.
    std::string dumpPath;
    PcmTap deviceTap;
    PcmTap mixTap;
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

// T248 (T128 board item): route miniaudio's own diagnostics into the willeq
// log. Before this nothing was registered on the device's ma_log, so its
// ALSA underrun-recovery messages ("[ALSA] Failed to recover device after
// underrun" etc.) had no path into the log file and an underrun was
// undetectable after the fact. miniaudio posts from whichever thread hit
// the condition -- on the underrun path that is the worker thread, already
// off the real-time path (it is inside snd_pcm_recover), so the LOG_* here
// is accepted for those error paths only; nothing in the steady-state
// callback posts a log.
static void miniaudioLogCallback(void* /*pUserData*/, ma_uint32 level, const char* pMessage) {
    std::string msg = pMessage ? pMessage : "";
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
    switch (level) {
        case MA_LOG_LEVEL_ERROR:   LOG_ERROR(MOD_AUDIO, "miniaudio: {}", msg); break;
        case MA_LOG_LEVEL_WARNING: LOG_WARN(MOD_AUDIO, "miniaudio: {}", msg); break;
        case MA_LOG_LEVEL_INFO:    LOG_INFO(MOD_AUDIO, "miniaudio: {}", msg); break;
        default:                   LOG_DEBUG(MOD_AUDIO, "miniaudio: {}", msg); break;
    }
}

static void miniaudioDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
#ifdef WILLEQ_T130_AUDIO_TID_PROBE
    t130WriteAudioTidOnce();
#endif
    AudioCallbackProbe::onCallback(t130MonotonicNs());
    // B1069: frames are delivered at the device's configured rate and
    // format; the adapter renders the mixer at its own rate and resamples
    // once, and a non-f32 device format is converted once here.
    auto* impl = static_cast<MiniaudioBackend::Impl*>(pDevice->pUserData);
    if (!impl) return;
    const ma_uint32 bpf = ma_get_bytes_per_frame(impl->outFormat, 2);
    if (impl->outFormat == ma_format_f32 || impl->f32Scratch.empty()) {
        impl->adapter.render(static_cast<float*>(pOutput), frameCount);
        impl->deviceTap.append(pOutput, static_cast<size_t>(frameCount) * bpf);
        return;
    }
    const ma_uint32 chunkMax = static_cast<ma_uint32>(impl->f32Scratch.size() / 2);
    auto* dst = static_cast<uint8_t*>(pOutput);
    ma_uint32 done = 0;
    while (done < frameCount) {
        const ma_uint32 n = std::min(frameCount - done, chunkMax);
        impl->adapter.render(impl->f32Scratch.data(), n);
        ma_pcm_convert(dst + static_cast<size_t>(done) * bpf, impl->outFormat,
                       impl->f32Scratch.data(), ma_format_f32,
                       static_cast<ma_uint64>(n) * 2, ma_dither_mode_none);
        done += n;
    }
    impl->deviceTap.append(pOutput, static_cast<size_t>(frameCount) * bpf);
}

MiniaudioBackend::MiniaudioBackend()
    : impl_(std::make_unique<Impl>()) {
}

MiniaudioBackend::~MiniaudioBackend() {
    stop();
}

// The mixer-rate period the T130 probe and the latency were tuned on
// (512 frames at 22050 Hz, ~23.2 ms). B1069 keeps that DURATION at any device
// rate rather than the frame count, so 48000 Hz gets ~1115-frame periods and
// the callback cadence (and the probe's "late" threshold) is unchanged.
static constexpr uint32_t kMixerPeriodFrames = 512;

bool MiniaudioBackend::init(AudioMixer* mixer, uint32_t sampleRate) {
    impl_->mixer = mixer;
    impl_->mixRate = sampleRate;
    impl_->requestedRate = requestedDeviceRate_;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    // B1069: the DEVICE sample format (default s16; HDMI sinks that drop
    // 24-bit, e.g. the XU4's monitor, play it). Native = ma_format_unknown,
    // which lets miniaudio take the device's own format.
    impl_->requestedFormat = requestedFormat_;
    switch (requestedFormat_) {
        case DeviceFormat::S16:    config.playback.format = ma_format_s16; break;
        case DeviceFormat::F32:    config.playback.format = ma_format_f32; break;
        case DeviceFormat::S24:    config.playback.format = ma_format_s24; break;
        case DeviceFormat::S32:    config.playback.format = ma_format_s32; break;
        case DeviceFormat::Native: config.playback.format = ma_format_unknown; break;
    }
    config.playback.channels = 2;
    // B1069 (#389): the DEVICE rate, not the mixer's. 0 = native.
    config.sampleRate        = requestedDeviceRate_;
    config.dataCallback      = miniaudioDataCallback;
    config.pUserData         = impl_.get();
    if (requestedDeviceRate_ > 0 && sampleRate > 0) {
        config.periodSizeInFrames = static_cast<ma_uint32>(
            (static_cast<uint64_t>(kMixerPeriodFrames) * requestedDeviceRate_ + sampleRate / 2) / sampleRate);
    } else if (sampleRate > 0) {
        config.periodSizeInMilliseconds = std::max<ma_uint32>(1, (kMixerPeriodFrames * 1000u) / sampleRate);
    } else {
        config.periodSizeInFrames = kMixerPeriodFrames;
    }

    ma_context* ctx = nullptr;
    if (useNullBackend_) {
        ma_backend backends[] = { ma_backend_null };
        if (ma_context_init(backends, 1, nullptr, &impl_->context) != MA_SUCCESS) {
            LOG_ERROR(MOD_AUDIO, "Failed to initialize miniaudio null context");
            return false;
        }
        impl_->contextInitialized = true;
        ctx = &impl_->context;
    }

    ma_result result = ma_device_init(ctx, &config, &impl_->device);
    if (result != MA_SUCCESS) {
        LOG_ERROR(MOD_AUDIO, "Failed to initialize miniaudio device: {}", static_cast<int>(result));
        if (impl_->contextInitialized) {
            ma_context_uninit(&impl_->context);
            impl_->contextInitialized = false;
        }
        return false;
    }

    // B1069: ma_device.sampleRate is the rate the callback must deliver at
    // (the requested one, or the device's native one when 0 was asked).
    impl_->adapter.init(mixer, sampleRate, impl_->device.sampleRate);
    impl_->outFormat = impl_->device.playback.format;
    // B1069 diagnostic: WILLEQ_AUDIO_DUMP (see PcmTap).
    if (const char* dump = std::getenv("WILLEQ_AUDIO_DUMP"); dump && *dump) {
        double secs = 30.0;
        if (const char* s = std::getenv("WILLEQ_AUDIO_DUMP_SECONDS")) secs = std::max(0.1, std::atof(s));
        impl_->dumpPath = dump;
        impl_->deviceTap.reserve(static_cast<size_t>(secs * impl_->device.sampleRate) *
                                 ma_get_bytes_per_frame(impl_->outFormat, 2));
        impl_->mixTap.reserve(static_cast<size_t>(secs * sampleRate) * 2 * sizeof(float));
        impl_->adapter.setMixTap(&impl_->mixTap);
        LOG_INFO(MOD_AUDIO, "Audio dump armed: {} ({} s, device + .mix.wav)", impl_->dumpPath, secs);
    }
    if (impl_->outFormat != ma_format_f32) {
        impl_->f32Scratch.assign(static_cast<size_t>(kAdapterChunkFrames) * 2, 0.0f);
    } else {
        impl_->f32Scratch.clear();
    }

    // T130: configure the callback-timing probe's expected period so it can
    // classify "late" callbacks. sampleRate is validated >0 by the caller
    // chain (AudioManager); the period is kMixerPeriodFrames of mixer time
    // whatever the device rate (see above).
    if (sampleRate > 0) {
        int64_t periodNs = (static_cast<int64_t>(kMixerPeriodFrames) * 1000000000LL) /
                            static_cast<int64_t>(sampleRate);
        AudioCallbackProbe::configure(periodNs);
    }

    // T248: see miniaudioLogCallback above.
    ma_log_register_callback(ma_device_get_log(&impl_->device),
                             ma_log_callback_init(miniaudioLogCallback, nullptr));

    impl_->deviceInitialized = true;
    LOG_INFO(MOD_AUDIO, "Miniaudio backend initialized ({}Hz mixer, {}Hz device, stereo)",
             sampleRate, impl_->device.sampleRate);
    return true;
}

uint32_t MiniaudioBackend::configuredSampleRate() const {
    return impl_->deviceInitialized ? impl_->device.sampleRate : 0;
}
uint32_t MiniaudioBackend::internalSampleRate() const {
    return impl_->deviceInitialized ? impl_->device.playback.internalSampleRate : 0;
}
const char* MiniaudioBackend::configuredFormatName() const {
    return impl_->deviceInitialized ? shortFormatName(impl_->device.playback.format) : "none";
}
bool MiniaudioBackend::isResampling() const {
    return impl_->deviceInitialized && !impl_->adapter.isPassthrough();
}

void MiniaudioBackend::start() {
    if (impl_->deviceInitialized) {
        ma_result result = ma_device_start(&impl_->device);
        if (result != MA_SUCCESS) {
            LOG_ERROR(MOD_AUDIO, "Failed to start miniaudio device: {}", static_cast<int>(result));
            return;
        }
        // B1069 (#389): the one line that says what the hardware is really
        // doing. The XU4 case was "client asked 22050, ALSA ran 32000, the
        // HDMI monitor dropped 32 kHz" and nothing in the log said 32000;
        // then "48000 but S24_LE, still silent". format= is the NEGOTIATED
        // hardware format (playback.internalFormat); requested_format= the
        // knob; client_format= what the callback writes (miniaudio converts
        // client -> hardware when they differ).
        const ma_device& d = impl_->device;
        LOG_INFO(MOD_AUDIO,
                 "Audio device: requested={}Hz configured={}Hz native={}Hz format={} "
                 "requested_format={} client_format={} "
                 "channels={} period={} backend={} device=\"{}\" mixer={}Hz resample={}",
                 impl_->requestedRate, d.sampleRate, d.playback.internalSampleRate,
                 shortFormatName(d.playback.internalFormat),
                 deviceFormatName(impl_->requestedFormat), shortFormatName(d.playback.format),
                 d.playback.internalChannels,
                 d.playback.internalPeriodSizeInFrames,
                 d.pContext ? ma_get_backend_name(d.pContext->backend) : "?",
                 d.playback.name, impl_->mixRate,
                 impl_->adapter.isPassthrough() ? "off" : impl_->adapter.resamplerName());
    }
}

void MiniaudioBackend::stop() {
    if (impl_->deviceInitialized) {
        const ma_format fmt = impl_->outFormat;
        const uint32_t devRate = impl_->device.sampleRate;
        ma_device_uninit(&impl_->device);
        impl_->deviceInitialized = false;
        if (!impl_->dumpPath.empty()) {
            const uint16_t bits = static_cast<uint16_t>(ma_get_bytes_per_sample(fmt) * 8);
            const bool okDev = impl_->deviceTap.writeWav(impl_->dumpPath,
                                                         fmt == ma_format_f32 ? 3 : 1, bits, devRate, 2);
            const bool okMix = impl_->mixTap.writeWav(impl_->dumpPath + ".mix.wav", 3, 32,
                                                      impl_->mixRate, 2);
            LOG_INFO(MOD_AUDIO, "Audio dump written: {} ({} B, {}) + .mix.wav ({} B): {}",
                     impl_->dumpPath, impl_->deviceTap.pos, shortFormatName(fmt),
                     impl_->mixTap.pos, (okDev && okMix) ? "ok" : "FAILED");
            impl_->dumpPath.clear();
            impl_->adapter.setMixTap(nullptr);
        }
    }
    if (impl_->contextInitialized) {
        ma_context_uninit(&impl_->context);
        impl_->contextInitialized = false;
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
    eqt::memory::MemoryScope memScope(eqt::memory::Category::Audio);  // T491
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
