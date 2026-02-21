#ifdef WITH_AUDIO

#include "client/audio/sound_buffer.h"
#include "common/logging.h"

#include <cstring>

// dr_wav is implemented in sfx_manager.cpp, only declare here
#include "dr_wav.h"

namespace EQT {
namespace Audio {

SoundBuffer::SoundBuffer() = default;

SoundBuffer::~SoundBuffer() {
    cleanup();
}

SoundBuffer::SoundBuffer(SoundBuffer&& other) noexcept
    : samples_(std::move(other.samples_))
    , frameCount_(other.frameCount_)
    , sampleRate_(other.sampleRate_)
    , channels_(other.channels_)
    , duration_(other.duration_)
    , memorySize_(other.memorySize_)
{
    other.frameCount_ = 0;
    other.sampleRate_ = 0;
    other.channels_ = 0;
    other.duration_ = 0.0f;
    other.memorySize_ = 0;
}

SoundBuffer& SoundBuffer::operator=(SoundBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();

        samples_ = std::move(other.samples_);
        frameCount_ = other.frameCount_;
        sampleRate_ = other.sampleRate_;
        channels_ = other.channels_;
        duration_ = other.duration_;
        memorySize_ = other.memorySize_;

        other.frameCount_ = 0;
        other.sampleRate_ = 0;
        other.channels_ = 0;
        other.duration_ = 0.0f;
        other.memorySize_ = 0;
    }
    return *this;
}

bool SoundBuffer::loadFromFile(const std::string& filepath) {
    cleanup();

    unsigned int wavChannels = 0;
    unsigned int wavSampleRate = 0;
    drwav_uint64 wavFrameCount = 0;

    float* samples = drwav_open_file_and_read_pcm_frames_f32(
        filepath.c_str(), &wavChannels, &wavSampleRate, &wavFrameCount, nullptr);
    if (!samples) {
        LOG_DEBUG(MOD_AUDIO, "Failed to open audio file: {}", filepath);
        return false;
    }

    if (wavChannels < 1 || wavChannels > 2) {
        LOG_WARN(MOD_AUDIO, "Unsupported channel count {} in: {}", wavChannels, filepath);
        drwav_free(samples, nullptr);
        return false;
    }

    size_t totalSamples = static_cast<size_t>(wavFrameCount * wavChannels);
    samples_.assign(samples, samples + totalSamples);
    drwav_free(samples, nullptr);

    frameCount_ = static_cast<size_t>(wavFrameCount);
    sampleRate_ = wavSampleRate;
    channels_ = static_cast<uint8_t>(wavChannels);
    duration_ = static_cast<float>(wavFrameCount) / static_cast<float>(wavSampleRate);
    memorySize_ = totalSamples * sizeof(float);

    LOG_DEBUG(MOD_AUDIO, "Loaded sound: {} ({}Hz, {}ch, {:.2f}s)",
              filepath, sampleRate_, channels_, duration_);
    return true;
}

bool SoundBuffer::loadFromMemory(const void* data, size_t size) {
    cleanup();

    unsigned int wavChannels = 0;
    unsigned int wavSampleRate = 0;
    drwav_uint64 wavFrameCount = 0;

    float* samples = drwav_open_memory_and_read_pcm_frames_f32(
        data, size, &wavChannels, &wavSampleRate, &wavFrameCount, nullptr);
    if (!samples) {
        LOG_DEBUG(MOD_AUDIO, "Failed to open audio from memory");
        return false;
    }

    if (wavChannels < 1 || wavChannels > 2) {
        LOG_WARN(MOD_AUDIO, "Unsupported channel count {} in memory WAV", wavChannels);
        drwav_free(samples, nullptr);
        return false;
    }

    size_t totalSamples = static_cast<size_t>(wavFrameCount * wavChannels);
    samples_.assign(samples, samples + totalSamples);
    drwav_free(samples, nullptr);

    frameCount_ = static_cast<size_t>(wavFrameCount);
    sampleRate_ = wavSampleRate;
    channels_ = static_cast<uint8_t>(wavChannels);
    duration_ = static_cast<float>(wavFrameCount) / static_cast<float>(wavSampleRate);
    memorySize_ = totalSamples * sizeof(float);

    return true;
}

bool SoundBuffer::loadFromPCM(const int16_t* samples, size_t sampleCount,
                               uint32_t sampleRate, uint8_t channels) {
    cleanup();

    if (!samples || sampleCount == 0 || channels < 1 || channels > 2) {
        return false;
    }

    sampleRate_ = sampleRate;
    channels_ = channels;

    frameCount_ = sampleCount / channels;
    duration_ = static_cast<float>(frameCount_) / static_cast<float>(sampleRate);

    // Convert int16_t -> float
    samples_.resize(sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) {
        samples_[i] = static_cast<float>(samples[i]) / 32768.0f;
    }

    memorySize_ = sampleCount * sizeof(float);
    return true;
}

void SoundBuffer::cleanup() {
    samples_.clear();
    frameCount_ = 0;
    sampleRate_ = 0;
    channels_ = 0;
    duration_ = 0.0f;
    memorySize_ = 0;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
