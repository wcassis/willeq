#ifdef WITH_AUDIO

#include "client/audio/sfx_manager.h"
#include "client/audio/audio_mixer.h"
#include "common/logging.h"

#include <cmath>
#include <algorithm>

// dr_wav implementation — only in this .cpp
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

namespace EQT {
namespace Audio {

SfxManager::SfxManager() = default;
SfxManager::~SfxManager() = default;

void SfxManager::setMixer(AudioMixer* mixer) {
    mixer_ = mixer;
}

bool SfxManager::preload(const std::string& filename, const float* samples,
                          size_t frameCount, uint32_t sampleRate, uint8_t channels) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    if (cache_.count(filename)) {
        return true;  // Already cached
    }

    WavData wav;
    wav.sampleRate = sampleRate;
    wav.channels = channels;
    wav.totalFrames = frameCount;

    size_t totalSamples = frameCount * channels;
    wav.samples.assign(samples, samples + totalSamples);
    wav.memoryBytes = totalSamples * sizeof(float);

    cacheSizeBytes_ += wav.memoryBytes;
    cache_[filename] = std::move(wav);

    return true;
}

int SfxManager::play(const std::string& filename, float volume, float pan, bool loop) {
    if (!mixer_) return -1;

    const WavData* wav = getWavData(filename);
    if (!wav || wav->samples.empty()) {
        return -1;
    }

    int handle = mixer_->allocSfxChannel();
    if (handle < 0) {
        return -1;
    }

    MixChannel* ch = mixer_->getChannel(handle);
    if (!ch) return -1;

    ch->type = MixChannelType::SFX;
    ch->volume = volume;
    ch->pan = pan;
    ch->samples = wav->samples.data();
    ch->sampleRate = wav->sampleRate;
    ch->channels = wav->channels;
    ch->totalFrames = wav->totalFrames;
    ch->position = 0;
    ch->looping = loop;
    ch->active.store(true);

    return handle;
}

int SfxManager::playSpatial(const std::string& filename, const glm::vec3& pos,
                             float maxDist, float fullVolDist) {
    // Calculate distance-based volume and pan
    float dx = pos.x - listenerPos_.x;
    float dy = pos.y - listenerPos_.y;
    float dz = pos.z - listenerPos_.z;
    float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (distance > maxDist) {
        return -1;  // Too far, don't play
    }

    // Inverse distance attenuation
    float volume;
    if (distance <= fullVolDist) {
        volume = 1.0f;
    } else {
        volume = fullVolDist / (fullVolDist + (distance - fullVolDist));
    }

    // Simple stereo panning based on listener direction
    // Project sound direction onto listener's left-right axis
    float pan = 0.0f;
    float dirLen = std::sqrt(dx * dx + dy * dy);
    if (dirLen > 0.001f) {
        // Cross product of forward and sound direction gives left-right
        float rightComponent = listenerForward_.x * (dy / dirLen) - listenerForward_.y * (dx / dirLen);
        pan = std::clamp(rightComponent, -1.0f, 1.0f);
    }

    int handle = play(filename, volume, pan, false);

    if (handle >= 0) {
        spatialChannels_.push_back({handle, pos, maxDist, fullVolDist});
    }

    return handle;
}

void SfxManager::stopChannel(int handle) {
    if (!mixer_) return;
    mixer_->freeChannel(handle);

    // Remove from spatial tracking
    spatialChannels_.erase(
        std::remove_if(spatialChannels_.begin(), spatialChannels_.end(),
                       [handle](const SpatialInfo& s) { return s.handle == handle; }),
        spatialChannels_.end());
}

void SfxManager::updateListener(const glm::vec3& pos, const glm::vec3& forward) {
    listenerPos_ = pos;
    listenerForward_ = forward;
}

const WavData* SfxManager::getWavData(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(filename);
    if (it != cache_.end()) {
        return &it->second;
    }
    return nullptr;
}

size_t SfxManager::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cacheSizeBytes_;
}

void SfxManager::setCacheMaxBytes(size_t maxBytes) {
    cacheMaxBytes_ = maxBytes;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
