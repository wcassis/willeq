#ifdef WITH_AUDIO

#include "client/audio/sound_buffer.h"
#include "common/logging.h"

#include <sndfile.h>
#include <vector>
#include <cstring>

namespace EQT {
namespace Audio {

SoundBuffer::SoundBuffer() = default;

SoundBuffer::~SoundBuffer() {
    cleanup();
}

SoundBuffer::SoundBuffer(SoundBuffer&& other) noexcept
    : buffer_(other.buffer_)
    , sampleRate_(other.sampleRate_)
    , channels_(other.channels_)
    , duration_(other.duration_)
{
    other.buffer_ = 0;
    other.sampleRate_ = 0;
    other.channels_ = 0;
    other.duration_ = 0.0f;
}

SoundBuffer& SoundBuffer::operator=(SoundBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();

        buffer_ = other.buffer_;
        sampleRate_ = other.sampleRate_;
        channels_ = other.channels_;
        duration_ = other.duration_;

        other.buffer_ = 0;
        other.sampleRate_ = 0;
        other.channels_ = 0;
        other.duration_ = 0.0f;
    }
    return *this;
}

bool SoundBuffer::loadFromFile(const std::string& filepath) {
    cleanup();

    SF_INFO sfInfo;
    std::memset(&sfInfo, 0, sizeof(sfInfo));

    SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        LOG_DEBUG(MOD_AUDIO, "Failed to open audio file: {} - {}", filepath, sf_strerror(nullptr));
        return false;
    }

    // Validate format
    if (sfInfo.channels < 1 || sfInfo.channels > 2) {
        LOG_WARN(MOD_AUDIO, "Unsupported channel count {} in: {}", sfInfo.channels, filepath);
        sf_close(file);
        return false;
    }

    // Read all samples
    std::vector<int16_t> samples(sfInfo.frames * sfInfo.channels);
    sf_count_t framesRead = sf_readf_short(file, samples.data(), sfInfo.frames);
    sf_close(file);

    if (framesRead != sfInfo.frames) {
        LOG_WARN(MOD_AUDIO, "Incomplete read from: {} ({}/{})", filepath, framesRead, sfInfo.frames);
    }

    // Store format info
    sampleRate_ = sfInfo.samplerate;
    channels_ = sfInfo.channels;
    duration_ = static_cast<float>(sfInfo.frames) / static_cast<float>(sfInfo.samplerate);

    // Determine OpenAL format
    ALenum format = (channels_ == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    // Create OpenAL buffer
    alGenBuffers(1, &buffer_);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "Failed to create OpenAL buffer: {}", alGetString(error));
        return false;
    }

    // Upload data
    alBufferData(buffer_, format, samples.data(),
                 samples.size() * sizeof(int16_t), sampleRate_);
    error = alGetError();
    if (error != AL_NO_ERROR) {
        LOG_ERROR(MOD_AUDIO, "Failed to fill OpenAL buffer: {}", alGetString(error));
        alDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        return false;
    }

    LOG_DEBUG(MOD_AUDIO, "Loaded sound: {} ({}Hz, {}ch, {:.2f}s)",
              filepath, sampleRate_, channels_, duration_);
    return true;
}

bool SoundBuffer::loadFromMemory(const void* data, size_t size) {
    cleanup();

    // Use libsndfile's virtual I/O for memory reading
    SF_VIRTUAL_IO vio;
    struct MemoryData {
        const uint8_t* data;
        size_t size;
        size_t offset;
    } memData = { static_cast<const uint8_t*>(data), size, 0 };

    vio.get_filelen = [](void* user_data) -> sf_count_t {
        return static_cast<MemoryData*>(user_data)->size;
    };

    vio.seek = [](sf_count_t offset, int whence, void* user_data) -> sf_count_t {
        auto* md = static_cast<MemoryData*>(user_data);
        switch (whence) {
            case SEEK_SET: md->offset = offset; break;
            case SEEK_CUR: md->offset += offset; break;
            case SEEK_END: md->offset = md->size + offset; break;
        }
        return md->offset;
    };

    vio.read = [](void* ptr, sf_count_t count, void* user_data) -> sf_count_t {
        auto* md = static_cast<MemoryData*>(user_data);
        sf_count_t available = md->size - md->offset;
        sf_count_t toRead = std::min(count, available);
        std::memcpy(ptr, md->data + md->offset, toRead);
        md->offset += toRead;
        return toRead;
    };

    vio.write = [](const void*, sf_count_t, void*) -> sf_count_t {
        return 0;  // Read-only
    };

    vio.tell = [](void* user_data) -> sf_count_t {
        return static_cast<MemoryData*>(user_data)->offset;
    };

    SF_INFO sfInfo;
    std::memset(&sfInfo, 0, sizeof(sfInfo));

    SNDFILE* file = sf_open_virtual(&vio, SFM_READ, &sfInfo, &memData);
    if (!file) {
        LOG_DEBUG(MOD_AUDIO, "Failed to open audio from memory: {}", sf_strerror(nullptr));
        return false;
    }

    // Read samples
    std::vector<int16_t> samples(sfInfo.frames * sfInfo.channels);
    sf_readf_short(file, samples.data(), sfInfo.frames);
    sf_close(file);

    return loadFromPCM(samples.data(), sfInfo.frames * sfInfo.channels,
                       sfInfo.samplerate, sfInfo.channels);
}

bool SoundBuffer::loadFromPCM(const int16_t* samples, size_t sampleCount,
                               uint32_t sampleRate, uint8_t channels) {
    cleanup();

    if (!samples || sampleCount == 0 || channels < 1 || channels > 2) {
        return false;
    }

    sampleRate_ = sampleRate;
    channels_ = channels;

    size_t frameCount = sampleCount / channels;
    duration_ = static_cast<float>(frameCount) / static_cast<float>(sampleRate);

    ALenum format = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    alGenBuffers(1, &buffer_);
    if (alGetError() != AL_NO_ERROR) {
        return false;
    }

    alBufferData(buffer_, format, samples, sampleCount * sizeof(int16_t), sampleRate);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buffer_);
        buffer_ = 0;
        return false;
    }

    return true;
}

void SoundBuffer::cleanup() {
    if (buffer_ != 0) {
        alDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
    sampleRate_ = 0;
    channels_ = 0;
    duration_ = 0.0f;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
