#ifdef WITH_AUDIO

#include "client/audio/midi_player.h"
#include "client/audio/midi_voice_probe.h"
#include "common/logging.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>

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
    // T248: atomic because the game thread reads it (getPlaybackTimeMs, the
    // zone-music resume position) while render() advances it. Relaxed on
    // both sides -- it is a progress indicator, not a synchroniser.
    std::atomic<double> midiTimeMs{0.0};   // Current playback position in ms
};

namespace {

// WILLEQ T131: TinySoundFont's own header documents two preconditions for
// calling tsf_render*() from a different thread than the one issuing
// tsf_note_on/tsf_channel_set_*() (tsf.h, "Thread safety" comment,
// ~line 141-162):
//   1. tsf_set_max_voices() after loading, so tsf_note_on() never
//      reallocates the voice array on the render thread.
//   2. Pre-create every MIDI channel that will ever be used, so
//      tsf_channel_set_*() never mallocs/reallocs f->channels on the
//      render thread.
// T130's runtime interposer proved this codebase satisfied neither: the
// steady-state audio callback (MidiPlayer::render()'s MIDI-event pump)
// reached tsf_note_on/tsf_channel_init directly. This is the ONE place
// that precondition is established -- every MidiPlayer construction path
// (single-soundfont init, dual-soundfont init, and the GM-slot reload)
// calls this immediately after tsf_set_output(), so a future fifth
// construction path can't silently skip it (T115 was a member added
// directly beneath a comment warning about exactly that pattern).
//
// MidiPlayer::kMaxVoices (declared in the header) is the single source of
// truth for this value -- see the Review for the measured peak-concurrent-
// voice count it was chosen against, the headroom, and the byte cost.

void configureTsfForRealtimeCallback(tsf* font) {
    if (!font) return;

    if (!tsf_set_max_voices(font, MidiPlayer::kMaxVoices)) {
        LOG_ERROR(MOD_AUDIO,
                  "MidiPlayer: tsf_set_max_voices({}) failed (allocation failure at init) "
                  "-- the audio callback may still allocate under tsf_note_on",
                  MidiPlayer::kMaxVoices);
    }

    // Pre-create all 16 MIDI channels (0-15; channel 9 is General MIDI's
    // percussion channel per the flag_mididrums convention this codebase's
    // own render() dispatch already uses at TML_PROGRAM_CHANGE) via the
    // public API that triggers tsf_channel_init(). Preset 0 is a
    // placeholder -- every TML_PROGRAM_CHANGE event overwrites it before
    // any note sounds; this call exists only to force channel-array
    // allocation to happen here, at init/reload, instead of on the
    // callback thread the first time a channel is actually used.
    for (int channel = 0; channel < 16; ++channel) {
        tsf_channel_set_presetnumber(font, channel, 0, channel == 9 ? 1 : 0);
    }
}

}  // namespace

MidiPlayer::MidiPlayer()
    : impl_(std::make_unique<Impl>()) {
}

MidiPlayer::~MidiPlayer() {
    stop();
    if (impl_->midiMessages) {
        tml_free(impl_->midiMessages);
        impl_->midiMessages = nullptr;
    }
    // T496 (board B817): `soundFont` is an ALIAS of gmSoundFont/customSoundFont
    // only on the dual-font path -- `init(path, rate)` (the single-font
    // overload MusicPlayer's fallback uses) loads into `soundFont` and leaves
    // both of the others null, so clearing the pointer before the two closes
    // below leaked the whole font. MEASURED: 24,160,112 bytes for
    // synthus2.sf2, retained for the life of the process
    // (tests/test_music_player_soundfont_lifetime.cpp). Close it when it owns
    // itself; the alias case still goes through gm/custom exactly as before,
    // so there is no double-free.
    tsf* ownSoundFont = (impl_->soundFont != impl_->gmSoundFont &&
                         impl_->soundFont != impl_->customSoundFont)
                            ? impl_->soundFont
                            : nullptr;
    impl_->soundFont = nullptr;
    if (ownSoundFont) {
        tsf_close(ownSoundFont);
    }
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
    configureTsfForRealtimeCallback(impl_->soundFont);
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
    configureTsfForRealtimeCallback(impl_->gmSoundFont);
    impl_->soundFont = impl_->gmSoundFont;
    activeSoundFont_ = SoundFontType::GM;

    LOG_INFO(MOD_AUDIO, "MidiPlayer: loaded GM SoundFont: {} ({}Hz)", gmPath, sampleRate_);

    // T504: the custom SoundFont (optional) is only REMEMBERED here. It is
    // loaded by the first selectSoundFont(Custom) -- see the header.
    customSoundFontPath_ = customPath;

    initialized_ = true;
    LOG_INFO(MOD_AUDIO, "MidiPlayer initialized with dual SoundFonts ({}Hz, custom font deferred: {})",
             sampleRate_, customPath.empty() ? "none" : customPath);
    return true;
}

bool MidiPlayer::isCustomSoundFontResident() const {
    return impl_->customSoundFont != nullptr;
}

void MidiPlayer::selectSoundFont(SoundFontType type) {
    if (type == activeSoundFont_) return;

    if (type == SoundFontType::Custom && customSoundFontPath_.empty()) {
        LOG_WARN(MOD_AUDIO, "MidiPlayer: no custom SoundFont configured, staying on GM");
        return;
    }

    // T248: stop() waits on the render barrier, so no render pass is inside the
    // synth while the pointer is swapped and an instance is loaded or closed
    // below; a later pass sees playing_==false and does not dereference
    // impl_->soundFont until play() publishes it again.
    stop();

    if (type == SoundFontType::Custom) {
        // T504: load on demand. A failure leaves the player on GM, exactly as
        // a failed load at init() used to.
        tsf* custom = tsf_load_filename(customSoundFontPath_.c_str());
        if (!custom) {
            LOG_WARN(MOD_AUDIO, "MidiPlayer: failed to load custom SoundFont: {} (staying on GM)",
                     customSoundFontPath_);
            return;
        }
        tsf_set_output(custom, TSF_STEREO_INTERLEAVED, sampleRate_, 0.0f);
        configureTsfForRealtimeCallback(custom);
        impl_->customSoundFont = custom;
        impl_->soundFont = custom;
        LOG_INFO(MOD_AUDIO, "MidiPlayer: loaded custom SoundFont on demand: {}", customSoundFontPath_);
    } else {
        impl_->soundFont = impl_->gmSoundFont;
        // T504: nothing renders from the custom font any more -- give it back.
        if (impl_->customSoundFont) {
            tsf_close(impl_->customSoundFont);
            impl_->customSoundFont = nullptr;
            LOG_INFO(MOD_AUDIO, "MidiPlayer: released custom SoundFont");
        }
    }
    activeSoundFont_ = type;

    LOG_INFO(MOD_AUDIO, "MidiPlayer: switched to {} SoundFont",
             type == SoundFontType::Custom ? "custom" : "GM");
}

// WILLEQ T131: thread requirement, established by reading every call site
// (grepped the whole tree -- the sole caller today is
// AudioManager::initialize(), and it calls this BEFORE creating the audio
// backend, i.e. before the audio callback thread exists at all; see
// AudioManager::initialize()'s ordering in audio_manager.cpp). That makes
// today's one call site safe by construction, not by locking: there is no
// concurrent renderer to race. But this method is public API on a class
// instance the audio thread reads via impl_->soundFont (a raw pointer, no
// atomic, no mutex) once playback starts, and it frees the old tsf instance
// (tsf_close(oldGm) below) after swapping the pointer. A FUTURE caller that
// invokes this while the backend is running (e.g. a "/soundfont reload"
// command added later) WOULD race the audio thread on impl_->soundFont and
// risk a use-after-free on the old instance -- that is a real hazard this
// unit did not need to fix because it does not exist today, not one that
// has been made safe. See this unit's Review for the STOP-condition
// analysis. Do not add a runtime-reachable caller of this method without
// adding synchronization first.
// T248 addendum: the stop() below now waits on the render barrier, so a
// render pass cannot be inside the synth when the pointer is swapped and
// the old instance closed; a later pass sees playing_==false and does not
// dereference impl_->soundFont. The swap itself is still a plain pointer
// write (published by play()'s seq_cst playing_=true), so the T131 caveat
// about raw-pointer visibility stands for any caller that does not go
// through play() afterwards.
// T359 (board B015): the precondition above is now ENFORCED, not just
// documented. Refuse before loading anything -- a caller that gets here with
// the audio thread up has a bug, and half-doing the work would only make the
// race harder to see.
bool MidiPlayer::loadSoundFont(const std::string& path) {
    if (!initialized_) return false;

    if (audioThreadRunning_.load(std::memory_order_seq_cst)) {
        LOG_ERROR(MOD_AUDIO,
                  "MidiPlayer::loadSoundFont refused ({}): the audio backend is running. "
                  "The GM SoundFont swap is not synchronised against the audio callback "
                  "thread and would risk a use-after-free on the instance it replaces; "
                  "it may only run before the backend is created.",
                  path);
        return false;
    }

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
    configureTsfForRealtimeCallback(impl_->gmSoundFont);

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
    impl_->midiTimeMs.store(0.0, std::memory_order_relaxed);
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
    // T248: the whole body runs between renderBarrier_.enter()/exit(); stop()
    // waits on that barrier after flipping playing_. The playing_ check MUST
    // come after enter() -- the fence in enter() pairs with the fence in
    // waitQuiescent() so that a render pass either observes playing_==false
    // here and never touches the synth, or is one stop() will wait for.
    RenderBarrier::Scope inRender(renderBarrier_);

    if (!playing_.load() || !impl_->soundFont || !impl_->currentMessage) {
        std::memset(buffer, 0, frame_count * 2 * sizeof(float));
        return;
    }

    float vol = volume_.load();
    double timeMs = impl_->midiTimeMs.load(std::memory_order_relaxed);
    int samplesRemaining = frame_count;
    float* outPtr = buffer;

    while (samplesRemaining > 0) {
        // Process MIDI events up to current time
        while (impl_->currentMessage && impl_->currentMessage->time <= timeMs) {
            tml_message* msg = impl_->currentMessage;

            switch (msg->type) {
                case TML_PROGRAM_CHANGE:
                    tsf_channel_set_presetnumber(impl_->soundFont, msg->channel, msg->program, (msg->channel == 9));
                    break;
                case TML_NOTE_ON:
                    // T131: tsf_channel_note_on returns 0 when tsf_note_on
                    // couldn't find or steal a voice -- with max_voices set,
                    // that means the note was silently dropped, not that TSF
                    // tried to allocate. Surface it so a too-low cap is
                    // visible instead of an inaudible, unmeasured drop.
                    if (!tsf_channel_note_on(impl_->soundFont, msg->channel, msg->key, msg->velocity / 127.0f)) {
                        MidiVoiceProbe::recordNoteOnFailure();
                    }
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

        // T131: sample peak concurrent voices right after this chunk's
        // note-on events have been applied (the natural local-peak point),
        // once per iteration of this loop rather than once per frame.
        // tsf_active_voice_count() only iterates the pre-allocated,
        // fixed-size voice array (no allocation) -- see MidiVoiceProbe's
        // own header comment for why this is cheap enough for the hot path.
        MidiVoiceProbe::observe(tsf_active_voice_count(impl_->soundFont));

        // If no more messages and not looping, render remaining and stop
        if (!impl_->currentMessage) {
            if (looping_) {
                // Loop: reset to beginning
                impl_->currentMessage = impl_->midiMessages;
                timeMs = 0.0;
                impl_->midiTimeMs.store(timeMs, std::memory_order_relaxed);
                tsf_reset(impl_->soundFont);
                continue;
            }
        }

        // Calculate how many samples to render before next event
        int samplesToRender = samplesRemaining;
        if (impl_->currentMessage) {
            double msToNext = impl_->currentMessage->time - timeMs;
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
        timeMs += (samplesToRender * 1000.0) / sampleRate_;
        impl_->midiTimeMs.store(timeMs, std::memory_order_relaxed);
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

// T248: the teardown side of the handshake (see render_barrier.h). Before
// this unit stop() flipped playing_ and immediately called tsf_reset() --
// while the audio thread could still be inside render() above, past its
// playing_ check, in tsf_render_float()/tsf_channel_note_*() on the same
// voice array (TSAN: 139 reports incl. heap-use-after-free from the
// tml_free() in play() and a SEGV, in this unit's driver;
// review/T248/tsan_before_*). Now: publish the stop, wait for any in-flight
// render pass to leave (bounded by the remainder of ONE pass), then reset.
// A render pass that starts after waitQuiescent() returns sees
// playing_==false and never dereferences the synth or the message list,
// so play()/seekTo()/selectSoundFont()/loadSoundFont()/~MidiPlayer may
// mutate freely after calling this. No lock is taken on the render side.
void MidiPlayer::stop() {
    playing_.store(false);
    if (!renderBarrier_.waitQuiescent()) {
        LOG_WARN(MOD_AUDIO,
                 "MidiPlayer::stop: a render pass was still in flight after the "
                 "barrier timeout -- audio thread wedged? Resetting the synth anyway.");
    }
    if (impl_->soundFont) {
        tsf_reset(impl_->soundFont);
    }
    impl_->currentMessage = nullptr;
    impl_->midiTimeMs.store(0.0, std::memory_order_relaxed);
}

double MidiPlayer::getPlaybackTimeMs() const {
    return impl_->midiTimeMs.load(std::memory_order_relaxed);
}

// T248: seekTo() resets the synth and walks the message list, so it must
// not overlap a render pass. Its one production caller is play() (between
// stop()'s barrier and playing_=true, so `wasPlaying` is false there and
// the handshake below is skipped). A direct caller while playing gets the
// same handshake stop() uses -- pause, wait for the in-flight pass to
// leave, seek, resume -- rather than a documented "don't"; the driver in
// tests/test_midi_stop_race.cpp seeks mid-play on purpose.
void MidiPlayer::seekTo(double timeMs) {
    if (!initialized_ || !impl_->soundFont || !impl_->midiMessages) return;

    const bool wasPlaying = playing_.exchange(false);
    if (wasPlaying && !renderBarrier_.waitQuiescent()) {
        LOG_WARN(MOD_AUDIO,
                 "MidiPlayer::seekTo: a render pass was still in flight after the "
                 "barrier timeout -- audio thread wedged? Seeking anyway.");
    }

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
    impl_->midiTimeMs.store(0.0, std::memory_order_relaxed);

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

    impl_->midiTimeMs.store(timeMs, std::memory_order_relaxed);
    if (wasPlaying) {
        playing_.store(true);  // seq_cst: publishes the seek to the next render pass
    }
    LOG_DEBUG(MOD_AUDIO, "MidiPlayer: seeked to {:.0f}ms", timeMs);
}

void MidiPlayer::setVolume(float vol) {
    volume_.store(std::clamp(vol, 0.0f, 1.0f));
}

// T131: struct tsf is a complete type in this TU (TSF_IMPLEMENTATION is
// defined above), so these read tsf's own bookkeeping directly rather than
// duplicating it. See the header comment for why these exist.
int MidiPlayer::getConfiguredMaxVoices() const {
    if (!impl_->soundFont) return 0;
    return impl_->soundFont->maxVoiceNum;
}

int MidiPlayer::getPreallocatedChannelCount() const {
    if (!impl_->soundFont || !impl_->soundFont->channels) return 0;
    return impl_->soundFont->channels->channelNum;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
