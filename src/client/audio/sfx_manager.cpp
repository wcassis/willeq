#ifdef WITH_AUDIO

#include "client/audio/sfx_manager.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/sound_buffer.h"
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

bool SfxManager::preload(const std::string& filename, std::shared_ptr<SoundBuffer> buffer) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    if (cache_.count(filename)) {
        return true;  // Already cached
    }
    if (!buffer || !buffer->isValid()) {
        return false;
    }

    // B989: no copy. The buffer is the AudioManager's -- the ledger already
    // charged its PCM to the owner that decoded it (au.sound_buffers) -- so
    // cacheSizeBytes_ counts the entry's SHARE, not the PCM. (It is the
    // buffer's whole PCM: the cache is one owner of it; the buffer cache is
    // the other. The cap bounds how many references THIS cache may hold.)
    cacheSizeBytes_ += buffer->getMemorySize();
    cache_[filename] = std::move(buffer);
    touchLruLocked(filename);

    // T412 (board B292): the ceiling AudioManager::setMemoryConstraints() hands
    // us is now actually enforced.
    enforceCacheBudgetLocked();

    return true;
}

int SfxManager::play(const std::string& filename, float volume, float pan, bool loop) {
    if (!mixer_) return -1;

    const SoundBuffer* buffer = getWavData(filename);
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

const SoundBuffer* SfxManager::getWavData(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(filename);
    if (it != cache_.end()) {
        // This IS the use that makes a sound recently used: play() reaches the
        // cache only through here (T412, board B292).
        touchLruLocked(filename);
        return it->second.get();
    }
    return nullptr;
}

size_t SfxManager::getCacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cacheSizeBytes_;
}

size_t SfxManager::getCacheMaxBytes() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cacheMaxBytes_;
}

size_t SfxManager::getCachedSoundCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return cache_.size();
}

size_t SfxManager::getEvictionCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return evictionCount_;
}

void SfxManager::setCacheMaxBytes(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cacheMaxBytes_ = maxBytes;
    // Applying a lower ceiling at runtime must take effect now, not at the next
    // preload -- setMemoryConstraints() is called precisely because memory is
    // already tight.
    enforceCacheBudgetLocked();
}

// ---------------------------------------------------------------------------
// T412 (board B292): LRU eviction. cacheMaxBytes_ had no reader at all before
// this; AudioManager::setMemoryConstraints() passed the configured sound-cache
// budget straight through to a member nothing compared against.
//
// The policy mirrors AudioManager::loadSound()'s own sound-buffer LRU (the
// sibling cache one layer up): most-recently-used at the front, evict from the
// back while over budget, and never evict down to an empty cache -- a caller
// that just asked for a sound is better served by keeping it than by honouring
// a ceiling smaller than a single WAV.
//
// The extra rule this cache needs is the in-use check. AudioMixer channels do
// not own their samples: play() stores wav->samples.data() into
// MixChannel::samples and the AUDIO thread reads through it until the channel
// goes inactive, so evicting a playing entry frees memory out from under the
// audio callback. isInUseLocked() refuses those. The check and the erase happen
// under cacheMutex_ while no new channel can appear, because preload(),
// setCacheMaxBytes() and play() are all game-thread calls; the audio thread only
// ever reads an already-published channel.
// ---------------------------------------------------------------------------

void SfxManager::touchLruLocked(const std::string& filename) const {
    lruOrder_.remove(filename);
    lruOrder_.push_front(filename);
}

bool SfxManager::isInUseLocked(const SoundBuffer& buffer) const {
    if (!mixer_ || !buffer.isValid()) return false;
    const float* base = buffer.getSamples();
    for (int handle = 0; handle < AudioMixer::TOTAL_CHANNELS; ++handle) {
        MixChannel* ch = mixer_->getChannel(handle);
        if (ch && ch->active.load() && ch->samples == base) return true;
    }
    return false;
}

void SfxManager::enforceCacheBudgetLocked() {
    if (cacheMaxBytes_ == 0) return;  // unlimited

    while (cacheSizeBytes_ > cacheMaxBytes_ && lruOrder_.size() > 1) {
        bool evicted = false;
        for (auto it = lruOrder_.end(); it != lruOrder_.begin(); ) {
            --it;
            auto cacheIt = cache_.find(*it);
            if (cacheIt == cache_.end()) {
                // Stale LRU entry (cache cleared out from under it) -- drop it.
                lruOrder_.erase(it);
                evicted = true;
                break;
            }
            if (isInUseLocked(*cacheIt->second)) continue;

            LOG_DEBUG(MOD_AUDIO, "SFX cache: evicting '{}' ({} bytes, {} -> {} of {})",
                      *it, cacheIt->second->getMemorySize(), cacheSizeBytes_,
                      cacheSizeBytes_ - cacheIt->second->getMemorySize(), cacheMaxBytes_);
            cacheSizeBytes_ -= cacheIt->second->getMemorySize();
            cache_.erase(cacheIt);  // drops one reference; the PCM stays alive
                                    // while the buffer cache (or a channel) holds it
            lruOrder_.erase(it);
            ++evictionCount_;
            evicted = true;
            break;
        }
        if (!evicted) {
            // Everything left is playing. Over budget is better than a
            // use-after-free; the next preload tries again.
            LOG_WARN(MOD_AUDIO,
                     "SFX cache over budget ({} > {} bytes) -- every cached sound is playing",
                     cacheSizeBytes_, cacheMaxBytes_);
            break;
        }
    }
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
