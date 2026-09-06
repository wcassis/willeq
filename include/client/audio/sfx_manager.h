#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <glm/glm.hpp>

namespace EQT {
namespace Audio {

class AudioMixer;

// Decoded WAV data stored as float PCM
struct WavData {
    std::vector<float> samples;  // Interleaved if stereo
    uint32_t sampleRate = 0;
    uint8_t channels = 0;
    size_t totalFrames = 0;
    size_t memoryBytes = 0;      // Approximate memory usage
};

// WAV loading and spatial playback via AudioMixer channels
class SfxManager {
public:
    SfxManager();
    ~SfxManager();

    // Not copyable
    SfxManager(const SfxManager&) = delete;
    SfxManager& operator=(const SfxManager&) = delete;

    // Set mixer reference
    void setMixer(AudioMixer* mixer);

    // Load a WAV file into cache (returns true if loaded or already cached)
    bool preload(const std::string& filename, const float* samples,
                 size_t frameCount, uint32_t sampleRate, uint8_t channels);

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

    // Get cached WAV data (for SoundBuffer compatibility)
    const WavData* getWavData(const std::string& filename) const;

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
    // its vector -- play() hands the channel wav->samples.data() directly, so
    // freeing a playing entry is a use-after-free on the audio thread.
    bool isInUseLocked(const WavData& wav) const;

    // Move filename to the most-recently-used end.
    void touchLruLocked(const std::string& filename) const;

    AudioMixer* mixer_ = nullptr;

    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, WavData> cache_;
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
