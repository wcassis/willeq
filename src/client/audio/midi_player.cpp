#ifdef WITH_AUDIO

#include "client/audio/midi_player.h"
#include "common/logging.h"

#include <cstring>
#include <algorithm>

// TinySoundFont + TinyMidiLoader implementations — only in this .cpp
#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

namespace EQT {
namespace Audio {

struct MidiPlayer::Impl {
    tsf* soundFont = nullptr;
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
    if (impl_->soundFont) {
        tsf_close(impl_->soundFont);
        impl_->soundFont = nullptr;
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

bool MidiPlayer::loadSoundFont(const std::string& path) {
    if (!initialized_) return false;

    // TSF doesn't support multiple SoundFonts natively; replace the current one
    tsf* newSf = tsf_load_filename(path.c_str());
    if (!newSf) {
        LOG_WARN(MOD_AUDIO, "MidiPlayer: failed to load additional SoundFont: {}", path);
        return false;
    }

    // Stop playback, swap
    stop();
    tsf_close(impl_->soundFont);
    impl_->soundFont = newSf;
    tsf_set_output(impl_->soundFont, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);

    LOG_INFO(MOD_AUDIO, "MidiPlayer: loaded SoundFont: {}", path);
    return true;
}

bool MidiPlayer::play(const uint8_t* midiData, size_t size, bool loop) {
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
    playing_ = true;

    // Reset synth state
    tsf_reset(impl_->soundFont);

    LOG_DEBUG(MOD_AUDIO, "MidiPlayer: playing MIDI ({} bytes, loop={})", size, loop);
    return true;
}

bool MidiPlayer::play(const std::vector<uint8_t>& midiData, bool loop) {
    return play(midiData.data(), midiData.size(), loop);
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

void MidiPlayer::setVolume(float vol) {
    volume_.store(std::clamp(vol, 0.0f, 1.0f));
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
