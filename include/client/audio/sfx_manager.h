#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <glm/glm.hpp>

namespace EQT {
namespace Audio {

class AudioMixer;
class SoundBuffer;

// WAV playback and spatial positioning via AudioMixer channels.
//
// B1015 (#336): this class keeps NO cache. Before it (since T412) it held its
// own filename-keyed LRU with its own cap BESIDE the AudioManager's buffer
// cache -- two LRUs, two caps, two different victims. B989 (#308) removed
// the COPY between them but left the second LRU, which is where the defect
// lived: the two caches evicted different entries, so the buffer cache could
// evict a sound the sfx cache still held (the next play re-decoded it -- two
// decoded copies resident), and when every older sfx entry was playing the
// sfx LRU evicted the entry it had just preloaded, so the play returned -1
// and the requested sound silently never played.
//
// The ONE decoded-PCM cache of SFX PCM is the AudioManager's
// `SoundBufferCache` (the old bufferCache_, with in-use pinning added): one
// population under the preset's one soundCache cap, and the entry a play
// just inserted is never the entry it evicts. This class is the channel
// side: it allocates and frees mixer channels for decoded buffers it does
// NOT own.
//
// The pin, stated plainly: the channel holds a raw `float*`
// (MixChannel::samples = the buffer's vector data) that the audio thread
// reads until the channel goes inactive. The shared_ptr is NOT what keeps a
// playing channel safe -- nothing here holds a shared_ptr. What keeps it
// safe is the SoundBufferCache's in-use pin: it asks this class (isInUse():
// does an ACTIVE channel point into that buffer?) before evicting, and
// refuses any entry an active channel names.
class SfxManager {
public:
    SfxManager();
    ~SfxManager();

    // Not copyable
    SfxManager(const SfxManager&) = delete;
    SfxManager& operator=(const SfxManager&) = delete;

    // Set mixer reference
    void setMixer(AudioMixer* mixer);

    // Play a sound whose decoded buffer is already resident in the one
    // cache (the AudioManager loads it -- every play goes through
    // SoundBufferCache::getOrLoad first, so a null here means the sound
    // does not exist, not "not cached"). Allocates a mixer channel over the
    // buffer's PCM (the channel stores a raw pointer into the buffer's
    // vector; the cache's in-use pin keeps that PCM resident for the life of
    // the channel). Returns a channel handle (>= 0) or -1 on failure.
    int play(const SoundBuffer* buffer,
             float volume = 1.0f, float pan = 0.0f, bool loop = false);

    // Play with 3D spatial positioning (computes distance volume/pan and
    // calls play()).
    int playSpatial(const SoundBuffer* buffer, const glm::vec3& pos,
                    float maxDist = 500.0f, float fullVolDist = 50.0f);

    // Stop a playing sound by channel handle
    void stopChannel(int handle);

    // B1015 (#336): the in-use pin for the ONE cache. True when an ACTIVE
    // mixer channel points into `buffer`'s PCM (the channel stores
    // buffer->getSamples() raw -- the audio thread reads through it until the
    // channel goes inactive). The SoundBufferCache asks this before evicting,
    // and never evicts a buffer it names. (It is the old SfxManager cache's
    // isInUseLocked() -- same check, moved next to the only cache left.)
    bool isInUse(const SoundBuffer& buffer) const;

    // Update listener position for spatial sounds
    void updateListener(const glm::vec3& pos, const glm::vec3& forward);

private:
    AudioMixer* mixer_ = nullptr;

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
