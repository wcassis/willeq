#ifdef WITH_AUDIO

#include "client/audio/midi_player.h"
#include "common/logging.h"

#include <cstring>
#include <cmath>
#include <algorithm>

// TinySoundFont + TinyMidiLoader implementations — only in this .cpp
#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

namespace EQT {
namespace Audio {

struct MidiPlayer::Impl {
    tsf* soundFont = nullptr;              // Active pointer (points to gmSoundFont or customSoundFont)
    tsf* gmSoundFont = nullptr;            // GM SoundFont (synthusr/1mgm)
    tsf* customSoundFont = nullptr;        // Custom SoundFont (synthus2)
    tml_message* midiMessages = nullptr;   // Loaded MIDI
    tml_message* currentMessage = nullptr;  // Playback cursor
    double midiTimeMs = 0.0;               // Current playback position in ms
};

MidiPlayer::MidiPlayer()
    : impl_(std::make_unique<Impl>()) {
}

MidiPlayer::~MidiPlayer() {
    stop();
    if (impl_->midiMessages) {
        tml_free(impl_->midiMessages);
        impl_->midiMessages = nullptr;
    }
    // Clear active pointer first to avoid double-free
    impl_->soundFont = nullptr;
    if (impl_->gmSoundFont) {
        tsf_close(impl_->gmSoundFont);
        impl_->gmSoundFont = nullptr;
    }
    if (impl_->customSoundFont) {
        tsf_close(impl_->customSoundFont);
        impl_->customSoundFont = nullptr;
    }
}

bool MidiPlayer::init(const std::string& soundFontPath, uint32_t sampleRate) {
    if (initialized_) return true;

    sampleRate_ = sampleRate;

    impl_->soundFont = tsf_load_filename(soundFontPath.c_str());
    if (!impl_->soundFont) {
        LOG_ERROR(MOD_AUDIO, "MidiPlayer: failed to load SoundFont: {}", soundFontPath);
        return false;
    }

    tsf_set_output(impl_->soundFont, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);
    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "MidiPlayer initialized with SoundFont: {} ({}Hz)", soundFontPath, sampleRate_);
    return true;
}

bool MidiPlayer::init(const std::string& gmPath, const std::string& customPath, uint32_t sampleRate) {
    if (initialized_) return true;

    sampleRate_ = sampleRate;

    // Load GM SoundFont
    impl_->gmSoundFont = tsf_load_filename(gmPath.c_str());
    if (!impl_->gmSoundFont) {
        LOG_ERROR(MOD_AUDIO, "MidiPlayer: failed to load GM SoundFont: {}", gmPath);
        return false;
    }
    tsf_set_output(impl_->gmSoundFont, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);
    impl_->soundFont = impl_->gmSoundFont;
    activeSoundFont_ = SoundFontType::GM;

    LOG_INFO(MOD_AUDIO, "MidiPlayer: loaded GM SoundFont: {} ({}Hz)", gmPath, sampleRate_);

    // Load custom SoundFont (optional)
    if (!customPath.empty()) {
        impl_->customSoundFont = tsf_load_filename(customPath.c_str());
        if (impl_->customSoundFont) {
            tsf_set_output(impl_->customSoundFont, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);
            LOG_INFO(MOD_AUDIO, "MidiPlayer: loaded custom SoundFont: {}", customPath);
        } else {
            LOG_WARN(MOD_AUDIO, "MidiPlayer: failed to load custom SoundFont: {} (continuing with GM only)", customPath);
        }
    }

    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "MidiPlayer initialized with dual SoundFonts ({}Hz)", sampleRate_);
    return true;
}

void MidiPlayer::selectSoundFont(SoundFontType type) {
    if (type == activeSoundFont_) return;

    if (type == SoundFontType::Custom && !impl_->customSoundFont) {
        LOG_WARN(MOD_AUDIO, "MidiPlayer: custom SoundFont not loaded, staying on GM");
        return;
    }

    stop();

    if (type == SoundFontType::Custom) {
        impl_->soundFont = impl_->customSoundFont;
    } else {
        impl_->soundFont = impl_->gmSoundFont;
    }
    activeSoundFont_ = type;

    LOG_INFO(MOD_AUDIO, "MidiPlayer: switched to {} SoundFont",
             type == SoundFontType::Custom ? "custom" : "GM");
}

bool MidiPlayer::loadSoundFont(const std::string& path) {
    if (!initialized_) return false;

    tsf* newSf = tsf_load_filename(path.c_str());
    if (!newSf) {
        LOG_WARN(MOD_AUDIO, "MidiPlayer: failed to load SoundFont: {}", path);
        return false;
    }

    stop();

    // Replace the GM slot
    tsf* oldGm = impl_->gmSoundFont;
    impl_->gmSoundFont = newSf;
    tsf_set_output(impl_->gmSoundFont, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);

    // If active pointer was pointing at the old GM instance, update it
    if (impl_->soundFont == oldGm || activeSoundFont_ == SoundFontType::GM) {
        impl_->soundFont = impl_->gmSoundFont;
        activeSoundFont_ = SoundFontType::GM;
    }

    if (oldGm) {
        tsf_close(oldGm);
    }

    LOG_INFO(MOD_AUDIO, "MidiPlayer: replaced GM SoundFont: {}", path);
    return true;
}

bool MidiPlayer::play(const uint8_t* midiData, size_t size, bool loop, double startTimeMs) {
    if (!initialized_ || !impl_->soundFont) return false;

    stop();

    // Free previous MIDI
    if (impl_->midiMessages) {
        tml_free(impl_->midiMessages);
        impl_->midiMessages = nullptr;
    }

    impl_->midiMessages = tml_load_memory(midiData, static_cast<int>(size));
    if (!impl_->midiMessages) {
        LOG_ERROR(MOD_AUDIO, "MidiPlayer: failed to parse MIDI data ({} bytes)", size);
        return false;
    }

    impl_->currentMessage = impl_->midiMessages;
    impl_->midiTimeMs = 0.0;
    looping_ = loop;

    // Reset synth state
    tsf_reset(impl_->soundFont);

    // Seek before setting playing_ to avoid rendering from position 0
    if (startTimeMs > 0.0) {
        seekTo(startTimeMs);
    }

    playing_ = true;

    LOG_DEBUG(MOD_AUDIO, "MidiPlayer: playing MIDI ({} bytes, loop={}, startMs={:.0f})", size, loop, startTimeMs);
    return true;
}

bool MidiPlayer::play(const std::vector<uint8_t>& midiData, bool loop, double startTimeMs) {
    return play(midiData.data(), midiData.size(), loop, startTimeMs);
}

void MidiPlayer::render(float* buffer, int frame_count) {
    if (!playing_.load() || !impl_->soundFont || !impl_->currentMessage) {
        std::memset(buffer, 0, frame_count * 2 * sizeof(float));
        return;
    }

    float vol = volume_.load();
    int samplesRemaining = frame_count;
    float* outPtr = buffer;

    while (samplesRemaining > 0) {
        // Process MIDI events up to current time
        while (impl_->currentMessage && impl_->currentMessage->time <= impl_->midiTimeMs) {
            tml_message* msg = impl_->currentMessage;

            switch (msg->type) {
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(impl_->soundFont, msg->channel, msg->program, (msg->channel == 9));
                    break;
                case TML_NOTE_ON:
                    tsf_channel_note_on(impl_->soundFont, msg->channel, msg->key, msg->velocity / 127.0f);
                    break;
                case TML_NOTE_OFF:
                    tsf_channel_note_off(impl_->soundFont, msg->channel, msg->key);
                    break;
                case TML_PITCH_BEND:
                    tsf_channel_set_pitchwheel(impl_->soundFont, msg->channel, msg->pitch_bend);
                    break;
                case TML_CONTROL_CHANGE:
                    tsf_channel_midi_control(impl_->soundFont, msg->channel, msg->control, msg->control_value);
                    break;
            }

            impl_->currentMessage = impl_->currentMessage->next;
        }

        // If no more messages and not looping, render remaining and stop
        if (!impl_->currentMessage) {
            if (looping_) {
                // Loop: reset to beginning
                impl_->currentMessage = impl_->midiMessages;
                impl_->midiTimeMs = 0.0;
                tsf_reset(impl_->soundFont);
                continue;
            }
        }

        // Calculate how many samples to render before next event
        int samplesToRender = samplesRemaining;
        if (impl_->currentMessage) {
            double msToNext = impl_->currentMessage->time - impl_->midiTimeMs;
            int framesToNext = static_cast<int>((msToNext * sampleRate_) / 1000.0);
            if (framesToNext < samplesToRender && framesToNext > 0) {
                samplesToRender = framesToNext;
            }
        }

        if (samplesToRender <= 0) {
            samplesToRender = 1;  // Process at least 1 frame to advance time
        }

        // Render audio
        tsf_render_float(impl_->soundFont, outPtr, samplesToRender, 0);

        // Apply volume
        for (int i = 0; i < samplesToRender * 2; ++i) {
            outPtr[i] *= vol;
        }

        // Advance time
        impl_->midiTimeMs += (samplesToRender * 1000.0) / sampleRate_;
        outPtr += samplesToRender * 2;
        samplesRemaining -= samplesToRender;

        // Check if we've finished all events and synth is silent
        if (!impl_->currentMessage && !looping_) {
            // Fill remaining with silence
            if (samplesRemaining > 0) {
                tsf_render_float(impl_->soundFont, outPtr, samplesRemaining, 0);
                for (int i = 0; i < samplesRemaining * 2; ++i) {
                    outPtr[i] *= vol;
                }
            }
            playing_ = false;
            break;
        }
    }
}

void MidiPlayer::stop() {
    playing_ = false;
    if (impl_->soundFont) {
        tsf_reset(impl_->soundFont);
    }
    impl_->currentMessage = nullptr;
    impl_->midiTimeMs = 0.0;
}

double MidiPlayer::getPlaybackTimeMs() const {
    return impl_->midiTimeMs;
}

void MidiPlayer::seekTo(double timeMs) {
    if (!initialized_ || !impl_->soundFont || !impl_->midiMessages) return;

    // Handle looping: wrap seek position to MIDI duration
    if (looping_) {
        tml_message* last = impl_->midiMessages;
        double totalDuration = 0.0;
        while (last) {
            totalDuration = last->time;
            if (!last->next) break;
            last = last->next;
        }
        if (totalDuration > 0.0 && timeMs > totalDuration) {
            timeMs = std::fmod(timeMs, totalDuration);
        }
    }

    // Reset synth and replay channel state events up to target time.
    // Only process program changes, control changes, and pitch bends to
    // restore correct instrument/controller state. Skip note-on/note-off
    // to avoid a burst of accumulated voices on the first rendered frame.
    tsf_reset(impl_->soundFont);
    impl_->currentMessage = impl_->midiMessages;
    impl_->midiTimeMs = 0.0;

    while (impl_->currentMessage && impl_->currentMessage->time <= timeMs) {
        tml_message* msg = impl_->currentMessage;
        switch (msg->type) {
            case TML_PROGRAM_CHANGE:
                tsf_channel_set_presetnumber(impl_->soundFont, msg->channel, msg->program, (msg->channel == 9));
                break;
            case TML_PITCH_BEND:
                tsf_channel_set_pitchwheel(impl_->soundFont, msg->channel, msg->pitch_bend);
                break;
            case TML_CONTROL_CHANGE:
                tsf_channel_midi_control(impl_->soundFont, msg->channel, msg->control, msg->control_value);
                break;
            default:
                break;
        }
        impl_->currentMessage = impl_->currentMessage->next;
    }

    impl_->midiTimeMs = timeMs;
    LOG_DEBUG(MOD_AUDIO, "MidiPlayer: seeked to {:.0f}ms", timeMs);
}

void MidiPlayer::setVolume(float vol) {
    volume_.store(std::clamp(vol, 0.0f, 1.0f));
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
