#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
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
    void setCacheMaxBytes(size_t maxBytes);

private:
    AudioMixer* mixer_ = nullptr;

    mutable std::mutex cacheMutex_;
    std::unordered_map<std::string, WavData> cache_;
    size_t cacheMaxBytes_ = 0;  // 0 = unlimited
    size_t cacheSizeBytes_ = 0;

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
