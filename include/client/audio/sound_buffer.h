#pragma once

#ifdef WITH_AUDIO

#include <AL/al.h>
#include <string>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Audio {

class SoundBuffer {
public:
    SoundBuffer();
    ~SoundBuffer();

    // Prevent copying (OpenAL buffer ownership)
    SoundBuffer(const SoundBuffer&) = delete;
    SoundBuffer& operator=(const SoundBuffer&) = delete;

    // Move semantics
    SoundBuffer(SoundBuffer&& other) noexcept;
    SoundBuffer& operator=(SoundBuffer&& other) noexcept;

    // Load from WAV file
    bool loadFromFile(const std::string& filepath);

    // Load from memory (WAV data)
    bool loadFromMemory(const void* data, size_t size);

    // Load from raw PCM data
    bool loadFromPCM(const int16_t* samples, size_t sampleCount,
                     uint32_t sampleRate, uint8_t channels);

    // Accessors
    ALuint getBuffer() const { return buffer_; }
    bool isValid() const { return buffer_ != 0; }
    uint32_t getSampleRate() const { return sampleRate_; }
    uint8_t getChannels() const { return channels_; }
    float getDuration() const { return duration_; }

private:
    void cleanup();

private:
    ALuint buffer_ = 0;
    uint32_t sampleRate_ = 0;
    uint8_t channels_ = 0;
    float duration_ = 0.0f;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
