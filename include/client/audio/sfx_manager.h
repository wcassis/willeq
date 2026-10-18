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

// B1074 (#394): the owner token of a LOOPING channel. A looped channel never
// ends by itself, so its owner (a zone emitter) must stop it; the handle alone
// is not proof of ownership -- AudioManager::stopAllSounds() can free the
// channel behind the owner's back and the mixer can hand the same index to
// another sound. Every loop call below acts only while the channel is still
// active, looping and reading `samples` (the buffer the loop was started on).
struct SfxLoopHandle {
    int handle = -1;                  // mixer channel, -1 = none
    const float* samples = nullptr;   // ownership check (MixChannel::samples)
    bool spatial = false;             // volume/pan follow the listener
    float baseVolume = 1.0f;          // non-spatial loops: volume before gain
    bool valid() const { return handle >= 0; }
};

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
                    float maxDist = 500.0f, float fullVolDist = 50.0f,
                    bool loop = false);

    // B1074 (#394): looping playback for zone ambient emitters. The loop
    // never ends by itself: the owner keeps the returned token and calls
    // stopLoop() (leave range, day/night variant switch, zone unload). The
    // in-use pin keeps the buffer resident for exactly that long.
    SfxLoopHandle playSpatialLoop(const SoundBuffer* buffer, const glm::vec3& pos,
                                  float maxDist, float fullVolDist);
    SfxLoopHandle playLoop(const SoundBuffer* buffer, float volume);
    // True while the token's channel is still active, looping and on its
    // buffer (false once anything else freed or reused the channel).
    bool isLoopActive(const SfxLoopHandle& loop) const;
    // Re-derive a spatial loop's volume/pan from the CURRENT listener (no
    // maxDist cut-off: an owner fading out past its radius is still heard)
    // and scale it by gain (the owner's fade envelope, 0..1). A non-spatial
    // loop gets baseVolume * gain.
    void updateLoop(const SfxLoopHandle& loop, float gain);
    // Stop the token's channel if it is still the token's; no-op otherwise.
    void stopLoop(const SfxLoopHandle& loop);

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
    // Distance attenuation + stereo pan of pos for the current listener.
    void spatialVolumePan(const glm::vec3& pos, float fullVolDist,
                          float& volume, float& pan) const;

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
