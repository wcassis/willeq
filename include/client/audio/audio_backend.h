#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

#include "client/audio/audio_device_rate.h"  // B1069: kDefaultDeviceSampleRate

namespace EQT {
namespace Audio {

class AudioMixer;
struct PcmTap;  // B1069 diagnostic (WILLEQ_AUDIO_DUMP), audio_backend.cpp

// Callback for RDP audio streaming (s16 interleaved stereo)
using AudioOutputCallback = std::function<void(const int16_t* samples, size_t count,
                                                uint32_t sampleRate, uint8_t channels)>;

// Abstract audio output backend
class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    virtual bool init(AudioMixer* mixer, uint32_t sampleRate) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

// B1069: renders an AudioMixer at its own rate (mixRate) and hands out frames
// at the device rate, through ONE linear ma_resampler (interleaved stereo
// f32). When the two rates are equal it is a straight pass-through and the
// mixer renders into the caller's buffer. render() is real-time safe: every
// buffer is allocated in init(), a pull larger than the scratch is split
// into chunks, and nothing locks or logs. Device-free, so the frame-count
// arithmetic is pinned by tests/test_audio_device_rate.cpp without opening
// an audio device.
class DeviceRateAdapter {
public:
    DeviceRateAdapter();
    ~DeviceRateAdapter();
    DeviceRateAdapter(const DeviceRateAdapter&) = delete;
    DeviceRateAdapter& operator=(const DeviceRateAdapter&) = delete;

    bool init(AudioMixer* mixer, uint32_t mixRate, uint32_t deviceRate);
    // output: interleaved stereo float, frameCount * 2 floats at deviceRate.
    void render(float* output, uint32_t frameCount);

    bool isPassthrough() const;
    // B1069 diagnostic: also record every mixer frame rendered (pre-resampler).
    void setMixTap(PcmTap* tap);
    // "off", "sinc48" (PolyphaseResampler, the default) or "linear" (the
    // ma_resampler fallback for a rate pair with too many phases).
    const char* resamplerName() const;
    uint32_t mixRate() const;
    uint32_t deviceRate() const;
    // Total mixer frames rendered so far (relaxed counter; tests and the
    // init log read it, never the audio thread).
    uint64_t mixerFramesPulled() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Miniaudio-based hardware output
class MiniaudioBackend : public AudioBackend {
public:
    // Opaque; public only so the miniaudio data callback (a free function
    // in audio_backend.cpp) can name it as its pUserData type.
    struct Impl;

    MiniaudioBackend();
    ~MiniaudioBackend() override;

    // sampleRate is the MIXER's rate; the device is opened at
    // deviceSampleRate() (default kDefaultDeviceSampleRate).
    bool init(AudioMixer* mixer, uint32_t sampleRate) override;
    void start() override;
    void stop() override;

    // B1069: call before init(). 0 = backend's native rate.
    void setDeviceSampleRate(uint32_t hz) { requestedDeviceRate_ = hz; }
    uint32_t requestedDeviceSampleRate() const { return requestedDeviceRate_; }
    // B1069: the playback DEVICE sample format; call before init().
    void setDeviceFormat(DeviceFormat f) { requestedFormat_ = f; }
    // The format the callback delivers in (ma_device.playback.format), as
    // the config spelling ("s16", ...); valid after init().
    const char* configuredFormatName() const;
    // Test seam: open miniaudio's null backend instead of the default
    // backend list, so a test can open a "device" on a host with none.
    void setUseNullBackend(bool on) { useNullBackend_ = on; }

    // B1069: what the device actually runs at, valid after init().
    // configuredSampleRate() is the rate frames are handed to miniaudio at
    // (ma_device.sampleRate); internal* is the hardware side miniaudio
    // converts to (ma_device.playback.internal*).
    uint32_t configuredSampleRate() const;
    uint32_t internalSampleRate() const;
    bool isResampling() const;

private:
    std::unique_ptr<Impl> impl_;
    uint32_t requestedDeviceRate_ = kDefaultDeviceSampleRate;
    bool useNullBackend_ = false;
    DeviceFormat requestedFormat_ = kDefaultDeviceFormat;
};

// RDP loopback backend — renders PCM and delivers via callback
class RDPAudioBackend : public AudioBackend {
public:
    RDPAudioBackend();
    ~RDPAudioBackend() override;

    bool init(AudioMixer* mixer, uint32_t sampleRate) override;
    void start() override;
    void stop() override;

    void setOutputCallback(AudioOutputCallback callback);

private:
    void pumpThread();

    AudioMixer* mixer_ = nullptr;
    uint32_t sampleRate_ = 22050;
    AudioOutputCallback outputCallback_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
