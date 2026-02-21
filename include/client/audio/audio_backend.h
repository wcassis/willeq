#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

namespace EQT {
namespace Audio {

class AudioMixer;

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

// Miniaudio-based hardware output
class MiniaudioBackend : public AudioBackend {
public:
    MiniaudioBackend();
    ~MiniaudioBackend() override;

    bool init(AudioMixer* mixer, uint32_t sampleRate) override;
    void start() override;
    void stop() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
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
