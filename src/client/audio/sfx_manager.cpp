#ifdef WITH_AUDIO

#include "client/audio/sfx_manager.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/sound_buffer.h"

// dr_wav implementation — only in this .cpp
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Audio {

SfxManager::SfxManager() = default;
SfxManager::~SfxManager() = default;

void SfxManager::setMixer(AudioMixer* mixer) {
    mixer_ = mixer;
}

// B1015 (#336): the channel stores a RAW pointer into the buffer's vector
// (MixChannel::samples); the audio thread reads through it until the channel
// goes inactive. The buffer is the one cache's (the AudioManager's
// SoundBufferCache), and its in-use pin -- which asks THIS class (isInUse()),
// which entries a live channel points into -- is what keeps a playing PCM
// resident. play() resolves the buffer through the one cache before
// allocating the channel: by the time a caller invokes it the buffer is
// resident (every path loads through SoundBufferCache::getOrLoad first), so
// a miss here means the sound does not exist, and eviction cannot race the
// allocation (both are game-thread calls, the cache never evicts an entry
// an active channel names).
int SfxManager::play(const SoundBuffer* buffer, float volume, float pan,
                     bool loop) {
    if (!mixer_) return -1;
    if (!buffer || !buffer->isValid()) {
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
    ch->samples = buffer->getSamples();
    ch->sampleRate = buffer->getSampleRate();
    ch->channels = buffer->getChannels();
    ch->totalFrames = buffer->getFrameCount();
    ch->position = 0;
    ch->looping = loop;
    ch->active.store(true);

    return handle;
}

int SfxManager::playSpatial(const SoundBuffer* buffer,
                            const glm::vec3& pos,
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

    int handle = play(buffer, volume, pan, false);

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

bool SfxManager::isInUse(const SoundBuffer& buffer) const {
    if (!mixer_ || !buffer.isValid()) return false;
    const float* base = buffer.getSamples();
    for (int handle = 0; handle < AudioMixer::TOTAL_CHANNELS; ++handle) {
        MixChannel* ch = mixer_->getChannel(handle);
        if (ch && ch->active.load() && ch->samples == base) return true;
    }
    return false;
}

void SfxManager::updateListener(const glm::vec3& pos, const glm::vec3& forward) {
    listenerPos_ = pos;
    listenerForward_ = forward;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
