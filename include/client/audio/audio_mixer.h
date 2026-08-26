#pragma once

#ifdef WITH_AUDIO

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "client/audio/render_barrier.h"

namespace EQT {
namespace Audio {

// Channel types for volume grouping
enum class MixChannelType : uint8_t {
    Music,
    SFX,
    Ambient
};

// A single channel in the software mixer
struct MixChannel {
    MixChannelType type = MixChannelType::SFX;
    float volume = 1.0f;       // Per-channel volume (0.0 - 1.0)
    float pan = 0.0f;          // -1.0 (left) to 1.0 (right), 0.0 = center
    const float* samples = nullptr;  // Source sample data (interleaved stereo or mono)
    uint32_t sampleRate = 22050;
    uint8_t channels = 1;      // 1 = mono, 2 = stereo
    size_t totalFrames = 0;    // Total frames in sample data
    size_t position = 0;       // Current playback position in frames
    bool looping = false;
    std::atomic<bool> active{false};  // Lock-free read in render()

    // Real-time render callback (e.g., MIDI synthesis).
    // If set, called instead of reading from samples array.
    // Must fill buffer with interleaved stereo float samples (frameCount * 2 floats).
    using RenderFn = void(*)(void* userData, float* buffer, int frameCount);
    RenderFn renderCallback = nullptr;
    void* renderUserData = nullptr;
};

// Software stereo PCM mixer — the convergence point for all audio.
// render() is called from the audio thread callback and must be lock-free.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    // Not copyable
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // Render mixed audio into output buffer (called from audio thread)
    // output: interleaved stereo float buffer, frame_count * 2 floats
    void render(float* output, int frame_count);

    // Channel allocation (called from main thread, mutex-protected)
    int allocSfxChannel();
    int allocMusicChannel();  // Returns one of the 2 music channels
    // Deactivates the channel and then waits (T248, RenderBarrier) until no
    // render() pass can still be reading it: on return the callback will
    // not call this channel's renderCallback or touch its fields again, so
    // the caller may free/reset what they point at. Bounded wait; a timeout
    // is logged. Callable from the render thread (returns at once).
    void freeChannel(int handle);

    // T248: the teardown side of the render handshake, exposed for callers
    // that mutate channel state without freeing the channel. Publish the
    // change (an atomic store) first, then call this; returns false only if
    // a render pass stayed in flight past the barrier timeout.
    bool waitForRenderQuiescent();

    // Get channel for direct manipulation (must check active flag)
    MixChannel* getChannel(int handle);

    // Volume controls
    void setMasterVolume(float vol);
    void setMusicVolume(float vol);
    void setSfxVolume(float vol);
    float getMasterVolume() const { return masterVolume_.load(); }
    float getMusicVolume() const { return musicVolume_.load(); }
    float getSfxVolume() const { return sfxVolume_.load(); }

    static constexpr uint32_t SAMPLE_RATE = 22050;
    static constexpr int MAX_SFX_CHANNELS = 16;
    static constexpr int NUM_MUSIC_CHANNELS = 2;
    static constexpr int TOTAL_CHANNELS = MAX_SFX_CHANNELS + NUM_MUSIC_CHANNELS;

private:
    MixChannel channels_[TOTAL_CHANNELS];
    std::mutex allocMutex_;

    std::atomic<float> masterVolume_{1.0f};
    std::atomic<float> musicVolume_{0.7f};
    std::atomic<float> sfxVolume_{1.0f};

    // Temp buffer for render callbacks (pre-allocated to avoid alloc on audio thread)
    std::vector<float> renderBuf_;

    // T248: render() holds this for its whole body; freeChannel() and
    // waitForRenderQuiescent() wait on it.
    RenderBarrier renderBarrier_;
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
