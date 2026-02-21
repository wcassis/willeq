#pragma once

#ifdef WITH_AUDIO

#include <string>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Audio {

class SoundBuffer {
public:
    SoundBuffer();
    ~SoundBuffer();

    // Prevent copying (owns sample data)
    SoundBuffer(const SoundBuffer&) = delete;
    SoundBuffer& operator=(const SoundBuffer&) = delete;

    // Move semantics
    SoundBuffer(SoundBuffer&& other) noexcept;
    SoundBuffer& operator=(SoundBuffer&& other) noexcept;

    // Load from WAV file
    bool loadFromFile(const std::string& filepath);

    // Load from memory (WAV data)
    bool loadFromMemory(const void* data, size_t size);

    // Load from raw PCM data (int16_t input, converted to float internally)
    bool loadFromPCM(const int16_t* samples, size_t sampleCount,
                     uint32_t sampleRate, uint8_t channels);

    // Accessors
    const float* getSamples() const { return samples_.data(); }
    size_t getFrameCount() const { return frameCount_; }
    bool isValid() const { return !samples_.empty(); }
    uint32_t getSampleRate() const { return sampleRate_; }
    uint8_t getChannels() const { return channels_; }
    float getDuration() const { return duration_; }
    size_t getMemorySize() const { return memorySize_; }

private:
    void cleanup();

private:
    std::vector<float> samples_;    // Interleaved float PCM
    size_t frameCount_ = 0;
    uint32_t sampleRate_ = 0;
    uint8_t channels_ = 0;
    float duration_ = 0.0f;
    size_t memorySize_ = 0;  // Size of decoded audio data in bytes
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
