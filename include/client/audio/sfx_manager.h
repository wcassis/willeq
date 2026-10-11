#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <glm/glm.hpp>

namespace EQT {
namespace Audio {

class AudioMixer;
class SoundBuffer;

// WAV playback and spatial positioning via AudioMixer channels.
//
// B989 (#308): the cache holds a shared_ptr to the SAME SoundBuffer the
// AudioManager's buffer cache already holds. There is exactly ONE decoded
// float-PCM copy per sound -- the buffer's vector -- and the ledger reports it
// once, to the owner that decoded it (the `au.sound_buffers` row, charged in
// AudioManager::loadSound()). Before B989 preload() COPIED the samples into a
// per-entry WavData, so every played WAV was held twice (the `au.sfx_cache`
// row) with each cache capped separately at the preset's soundCache. The
// shared_ptr also makes the LRU eviction safe the way a copy never was:
// evicting a cache entry only drops a reference, and the buffer stays alive
// while an active mixer channel reads its samples.
class SfxManager {
public:
    SfxManager();
    ~SfxManager();

    // Not copyable
    SfxManager(const SfxManager&) = delete;
    SfxManager& operator=(const SfxManager&) = delete;

    // Set mixer reference
    void setMixer(AudioMixer* mixer);

    // Share a decoded sound in the cache (a no-op when the filename is
    // already there). The buffer is owned by whoever decoded it -- the
    // AudioManager's buffer cache -- and the entry only adds a reference.
    // Returns true if the filename is cached afterwards.
    bool preload(const std::string& filename, std::shared_ptr<SoundBuffer> buffer);

    // Play a cached sound
    // Returns channel handle (>= 0) or -1 on failure
    int play(const std::string& filename, float volume = 1.0f,
             float pan = 0.0f, bool loop = false);

    // Play with 3D spatial positioning
    int playSpatial(const std::string& filename, const glm::vec3& pos,
                    float maxDist = 500.0f, float fullVolDist = 50.0f);

    // Stop a playing sound by channel handle
    void stopChannel(int handle);

    // Update listener position for spatial sounds
    void updateListener(const glm::vec3& pos, const glm::vec3& forward);

    // Get the cached decoded buffer (the shared_ptr the entry holds -- it is
    // the AudioManager's buffer, NOT a copy). Null when not cached.
    const SoundBuffer* getWavData(const std::string& filename) const;

    // Cache management
    size_t getCacheSize() const;

    // Set the cache ceiling in bytes; 0 means unlimited. Applying a ceiling
    // evicts immediately, so lowering it at runtime takes effect at once.
    // T412 (board B292): before this the budget was stored and nothing ever
    // compared cacheSizeBytes_ against it, so the SFX cache grew without bound
    // on a memory-constrained target.
    void setCacheMaxBytes(size_t maxBytes);
    size_t getCacheMaxBytes() const;

    // Number of WAVs currently cached, and how many have been evicted since
    // construction. Both exist for the budget tests and the debug HUD.
    size_t getCachedSoundCount() const;
    size_t getEvictionCount() const;

private:
    // --- T412 (board B292): LRU budget enforcement. All three require
    // cacheMutex_ to be held by the caller. ---

    // Evict least-recently-used entries until cacheSizeBytes_ <= cacheMaxBytes_,
    // skipping anything a mixer channel is still playing, and always keeping at
    // least one entry (the sibling rule in AudioManager::loadSound()).
    void enforceCacheBudgetLocked();

    // A WAV is "in use" when an ACTIVE mixer channel's sample pointer aliases
    // its vector -- play() hands the channel buffer->getSamples() directly, so
    // the last reference going away while a channel plays is a use-after-free
    // on the audio thread. isInUseLocked() refuses to evict those.
    bool isInUseLocked(const SoundBuffer& buffer) const;

    // Move filename to the most-recently-used end.
    void touchLruLocked(const std::string& filename) const;

    AudioMixer* mixer_ = nullptr;

    mutable std::mutex cacheMutex_;
    // B989: the decoded buffer itself is the AudioManager's (one copy,
    // `au.sound_buffers`); the entry is a reference counted by this cache and
    // the buffer cache, and the LRU drops references, not PCM.
    std::unordered_map<std::string, std::shared_ptr<SoundBuffer>> cache_;
    // Most-recently-used at the front. Mutable because getWavData() is const
    // and IS the read that makes a sound recently used (play() goes through it).
    mutable std::list<std::string> lruOrder_;
    size_t cacheMaxBytes_ = 0;  // 0 = unlimited
    size_t cacheSizeBytes_ = 0;
    size_t evictionCount_ = 0;

    // Listener state for spatial audio
    glm::vec3 listenerPos_{0.0f};
    glm::vec3 listenerForward_{0.0f, 0.0f, -1.0f};

    // Spatial channel tracking
    struct SpatialInfo {
        int handle;
        glm::vec3 pos;
        float maxDist;
        float fullVolDist;
    };
    std::vector<SpatialInfo> spatialChannels_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
