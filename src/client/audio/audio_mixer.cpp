#ifdef WITH_AUDIO

#include "client/audio/audio_mixer.h"
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
    // Zero the output buffer
    std::memset(output, 0, frame_count * 2 * sizeof(float));

    float master = masterVolume_.load();
    float musicVol = musicVolume_.load();
    float sfxVol = sfxVolume_.load();

    for (auto& ch : channels_) {
        if (!ch.active.load(std::memory_order_relaxed)) {
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

        for (int i = 0; i < frame_count; ++i) {
            if (ch.position >= ch.totalFrames) {
                if (ch.looping) {
                    ch.position = 0;
                } else {
                    ch.active.store(false, std::memory_order_relaxed);
                    break;
                }
            }

            float sL, sR;
            if (ch.channels == 2) {
                sL = ch.samples[ch.position * 2];
                sR = ch.samples[ch.position * 2 + 1];
            } else {
                // Mono: duplicate to both channels
                sL = sR = ch.samples[ch.position];
            }

            output[i * 2]     += sL * gainL;
            output[i * 2 + 1] += sR * gainR;

            ch.position++;
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
    ch.position = 0;
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
