#ifdef WITH_AUDIO

#include "client/audio/audio_mixer.h"
#include "common/logging.h"
#include <cstring>
#include <algorithm>

namespace EQT {
namespace Audio {

AudioMixer::AudioMixer() {
    // All channels start inactive
    for (auto& ch : channels_) {
        ch.active.store(false);
    }
    // Pre-allocate render callback buffer (4096 frames stereo)
    renderBuf_.resize(4096 * 2);
}

AudioMixer::~AudioMixer() = default;

void AudioMixer::render(float* output, int frame_count) {
    // T248: the whole pass runs between renderBarrier_.enter()/exit() so
    // freeChannel()/waitForRenderQuiescent() can wait for it. The per-channel
    // active loads below MUST come after enter() (see render_barrier.h).
    RenderBarrier::Scope inRender(renderBarrier_);

    // Zero the output buffer
    std::memset(output, 0, frame_count * 2 * sizeof(float));

    float master = masterVolume_.load();
    float musicVol = musicVolume_.load();
    float sfxVol = sfxVolume_.load();

    for (auto& ch : channels_) {
        // acquire (T248): pairs with the producer's active.store(true) after
        // it filled in the channel's fields, so those writes are visible here.
        if (!ch.active.load(std::memory_order_acquire)) {
            continue;
        }

        // Determine group volume
        float groupVol;
        switch (ch.type) {
            case MixChannelType::Music:  groupVol = musicVol; break;
            case MixChannelType::SFX:    groupVol = sfxVol; break;
            case MixChannelType::Ambient: groupVol = sfxVol; break;
            default: groupVol = 1.0f; break;
        }
        float vol = master * groupVol * ch.volume;
        if (vol <= 0.0f) {
            continue;
        }

        // Pan: compute left/right gains (constant-power approximation)
        float panR = (ch.pan + 1.0f) * 0.5f;  // 0..1
        float panL = 1.0f - panR;
        float gainL = vol * panL;
        float gainR = vol * panR;

        // Render callback path (MIDI synthesis, streaming)
        if (ch.renderCallback) {
            size_t needed = static_cast<size_t>(frame_count) * 2;
            if (renderBuf_.size() < needed) {
                renderBuf_.resize(needed);
            }
            ch.renderCallback(ch.renderUserData, renderBuf_.data(), frame_count);
            for (int i = 0; i < frame_count; ++i) {
                output[i * 2]     += renderBuf_[i * 2]     * gainL;
                output[i * 2 + 1] += renderBuf_[i * 2 + 1] * gainR;
            }
            continue;
        }

        // PCM sample path
        if (!ch.samples || ch.totalFrames == 0) {
            continue;
        }

        // T404 (board B250): SOURCE frames advanced per OUTPUT frame. The
        // device runs at SAMPLE_RATE; an asset recorded at a different rate
        // must be resampled or it plays at the wrong pitch and duration. This
        // loop used to do ch.position++ unconditionally and never look at
        // ch.sampleRate at all, so every 11,025 Hz effect in the shipped
        // snd*.pfs archives came out at exactly k = 2.00 (T401 measured it
        // live on BuyItem/LevDn/SpelCast). Linear interpolation between the
        // two bracketing source frames; step is 1.0 exactly for a device-rate
        // asset, so those channels are bit-identical to the pre-T404 path.
        const double step = (ch.sampleRate > 0)
            ? static_cast<double>(ch.sampleRate) / static_cast<double>(SAMPLE_RATE)
            : 1.0;

        for (int i = 0; i < frame_count; ++i) {
            if (ch.position >= static_cast<double>(ch.totalFrames)) {
                if (ch.looping) {
                    // Keep the fractional remainder so a looped ambient does
                    // not accumulate a phase error every wrap.
                    ch.position -= static_cast<double>(ch.totalFrames);
                    if (ch.position < 0.0 || ch.position >= static_cast<double>(ch.totalFrames)) {
                        ch.position = 0.0;
                    }
                } else {
                    // release (T248): the allocator's seq_cst load of active
                    // then orders our reads of this channel before its
                    // resetChannel() writes.
                    ch.active.store(false, std::memory_order_release);
                    break;
                }
            }

            const size_t idx0 = static_cast<size_t>(ch.position);
            const float frac = static_cast<float>(ch.position - static_cast<double>(idx0));
            // The last source frame has no successor: hold it rather than
            // reading one past the buffer.
            const size_t idx1 = (idx0 + 1 < ch.totalFrames)
                ? idx0 + 1
                : (ch.looping ? 0 : idx0);

            float sL, sR;
            if (ch.channels == 2) {
                const float a0 = ch.samples[idx0 * 2];
                const float a1 = ch.samples[idx0 * 2 + 1];
                const float b0 = ch.samples[idx1 * 2];
                const float b1 = ch.samples[idx1 * 2 + 1];
                sL = a0 + (b0 - a0) * frac;
                sR = a1 + (b1 - a1) * frac;
            } else {
                // Mono: duplicate to both channels
                const float a = ch.samples[idx0];
                const float b = ch.samples[idx1];
                sL = sR = a + (b - a) * frac;
            }

            output[i * 2]     += sL * gainL;
            output[i * 2 + 1] += sR * gainR;

            ch.position += step;
        }
    }

    // Clamp output to [-1, 1]
    for (int i = 0; i < frame_count * 2; ++i) {
        output[i] = std::clamp(output[i], -1.0f, 1.0f);
    }
}

// Helper to reset a channel without copy-assigning (std::atomic is non-copyable)
static void resetChannel(MixChannel& ch, MixChannelType type) {
    ch.type = type;
    ch.volume = 1.0f;
    ch.pan = 0.0f;
    ch.samples = nullptr;
    ch.sampleRate = AudioMixer::SAMPLE_RATE;
    ch.channels = 1;
    ch.totalFrames = 0;
    ch.position = 0.0;
    ch.looping = false;
    ch.renderCallback = nullptr;
    ch.renderUserData = nullptr;
    // active stays false — caller sets it after filling in data
}

int AudioMixer::allocSfxChannel() {
    std::lock_guard<std::mutex> lock(allocMutex_);
    for (int i = 0; i < MAX_SFX_CHANNELS; ++i) {
        if (!channels_[i].active.load()) {
            resetChannel(channels_[i], MixChannelType::SFX);
            return i;
        }
    }
    return -1;
}

int AudioMixer::allocMusicChannel() {
    std::lock_guard<std::mutex> lock(allocMutex_);
    for (int i = MAX_SFX_CHANNELS; i < TOTAL_CHANNELS; ++i) {
        if (!channels_[i].active.load()) {
            resetChannel(channels_[i], MixChannelType::Music);
            return i;
        }
    }
    return -1;
}

void AudioMixer::freeChannel(int handle) {
    if (handle < 0 || handle >= TOTAL_CHANNELS) {
        return;
    }
    channels_[handle].active.store(false);
    // T248: before this, freeChannel() returned while render() could still
    // be inside this channel's renderCallback (e.g. MusicPlayer ->
    // MidiPlayer::render), and the caller went on to reuse the channel /
    // reset the synth behind it. Wait for any in-flight pass to leave.
    waitForRenderQuiescent();
}

void AudioMixer::stopAll() {
    // Deactivate every active channel, THEN wait for the render thread to
    // leave render() (the same T248 handshake freeChannel() does, once).
    // On return nothing in the audio thread still reads any channel's
    // samples, so the caller may free a SoundBuffer a channel pointed at.
    for (auto& ch : channels_) {
        ch.active.store(false);
    }
    waitForRenderQuiescent();
}

bool AudioMixer::waitForRenderQuiescent() {
    if (renderBarrier_.waitQuiescent()) {
        return true;
    }
    LOG_WARN(MOD_AUDIO,
             "AudioMixer: a render pass was still in flight after the barrier "
             "timeout -- audio thread wedged? Proceeding without it.");
    return false;
}

MixChannel* AudioMixer::getChannel(int handle) {
    if (handle < 0 || handle >= TOTAL_CHANNELS) {
        return nullptr;
    }
    return &channels_[handle];
}

void AudioMixer::setMasterVolume(float vol) {
    masterVolume_.store(std::clamp(vol, 0.0f, 1.0f));
}

void AudioMixer::setMusicVolume(float vol) {
    musicVolume_.store(std::clamp(vol, 0.0f, 1.0f));
}

void AudioMixer::setSfxVolume(float vol) {
    sfxVolume_.store(std::clamp(vol, 0.0f, 1.0f));
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
