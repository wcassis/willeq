#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

#include "client/audio/render_barrier.h"

namespace EQT {
namespace Audio {

enum class SoundFontType { GM, Custom };

// TSF + TML MIDI synthesis.
// This is a NEW class — not the existing MusicPlayer.
class MidiPlayer {
public:
    MidiPlayer();
    ~MidiPlayer();

    // Not copyable
    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;

    // Initialize with a single SoundFont file
    bool init(const std::string& soundFontPath, uint32_t sampleRate = 22050);

    // Initialize with dual SoundFonts (GM + custom orchestral).
    //
    // T504 (load-step audit): the custom font is NOT loaded here. synthus2.sf2
    // is 12.0 MB on disk and TSF holds it as float samples -- MEASURED +22.9 MiB
    // RssAnon at process start, in every session, although only the tracks
    // AudioManager::getSoundFontTypeForFile() names (8 Velious + 23 Luclin
    // zones) ever render from it. init() records the path; the first
    // selectSoundFont(Custom) loads it and selectSoundFont(GM) frees it, so the
    // font is resident exactly while a track that needs it is selected.
    bool init(const std::string& gmPath, const std::string& customPath, uint32_t sampleRate);

    // T504: true while the custom font's samples are in memory. Test hook for
    // the lazy-load contract above; not a playback control.
    bool isCustomSoundFontResident() const;

    // B990 (issue #309): release the custom font if resident. The mirror of
    // selectSoundFont(Custom)'s on-demand load: a song that STOPS must give
    // its 23 MiB of float samples back (selectSoundFont(GM) is the other,
    // switch-driven half -- the production stop path reaches THIS one instead
    // of a switch, because nothing selects GM when the music simply ends).
    //
    // Thread contract (the T248 render barrier, render_barrier.h): like every
    // other mutating path it calls stop() first -- playing_=false plus the
    // render-barrier wait, so no render pass can still read impl_->soundFont
    // while the pointer is moved and the instance closed, and a later pass
    // sees playing_==false. stop() zeroes the playback position, so the
    // immediate MusicPlayer::stop() path saves it beforehand and restores it
    // after (zone-music resume reads it via getPlaybackTimeMs()); the fade
    // path needs no restore because it stops the song for good.
    void releaseCustomSoundFont();

    // Replace the GM SoundFont slot.
    //
    // PRECONDITION (T359, board B015): the audio backend must NOT be running.
    // The swap writes impl_->soundFont, a raw pointer the audio callback thread
    // reads with no atomic and no mutex, and then frees the instance it
    // replaced -- a use-after-free waiting for a second caller. The one
    // production call site (AudioManager::initialize) runs BEFORE the backend
    // is created, which is what makes it safe; that was recorded only in a
    // comment, so the precondition is now ENFORCED: called with the audio
    // thread up, this logs an error and returns false without touching
    // anything. Synchronising the swap instead would mean locking inside the
    // audio callback, which is not acceptable on the realtime thread.
    bool loadSoundFont(const std::string& path);

    // T359 (B015): AudioManager tells the player when the backend that drives
    // render() from the audio callback thread starts and stops. Nothing else
    // may call this; it is the precondition flag loadSoundFont() checks, not a
    // playback control.
    void setAudioThreadRunning(bool running) {
        audioThreadRunning_.store(running, std::memory_order_seq_cst);
    }
    bool isAudioThreadRunning() const {
        return audioThreadRunning_.load(std::memory_order_seq_cst);
    }

    // Switch between loaded SoundFonts
    void selectSoundFont(SoundFontType type);
    SoundFontType getActiveSoundFont() const { return activeSoundFont_; }

    // Play MIDI data from memory
    // trackIndex: ignored by TML (tml plays all tracks). Caller should provide
    // single-track MIDI data from XmiDecoder if a specific track is wanted.
    // startTimeMs: if > 0, seeks to this position before starting playback
    //              (avoids audible glitch vs calling seekTo() after play)
    bool play(const uint8_t* midiData, size_t size, bool loop = true, double startTimeMs = 0.0);
    bool play(const std::vector<uint8_t>& midiData, bool loop = true, double startTimeMs = 0.0);

    // Render audio into buffer (called from mixer, audio thread)
    // buffer: interleaved stereo float, frame_count * 2 floats
    void render(float* buffer, int frame_count);

    // Controls.
    // stop() may be called from any thread while render() is live on the
    // audio thread: it flips playing_, then waits on renderBarrier_ until no
    // render pass can still be reading the synth (T248 -- the one-frame
    // barrier; see render_barrier.h). Only after that does it touch TSF.
    // Every other mutating path (play, seekTo-from-play, selectSoundFont,
    // loadSoundFont, the destructor) goes through stop() first and inherits
    // that guarantee.
    void stop();
    void setVolume(float vol);
    float getVolume() const { return volume_.load(); }
    bool isPlaying() const { return playing_.load(); }

    // Playback position
    double getPlaybackTimeMs() const;
    void seekTo(double timeMs);

    bool isInitialized() const { return initialized_; }

    // T131: single source of truth for the max_voices value every
    // construction path configures via tsf_set_max_voices — see the Review
    // in refactor/T131_tsf_preallocate.md for the measured peak this was
    // chosen against and the headroom/byte-cost accounting.
    static constexpr int kMaxVoices = 256;

    // T131 test/diagnostic accessors: whether the callback-thread-allocation
    // preconditions (tsf_set_max_voices + all 16 MIDI channels pre-created)
    // have been applied to the currently ACTIVE SoundFont instance
    // (impl_->soundFont). Production code never needs these; they exist so
    // test_midi_player_prealloc.cpp can assert the invariant — "every
    // construction path applies the same setup" — directly, since that is
    // exactly the kind of thing this codebase has silently regressed before
    // (T115: a member added directly beneath a comment warning about the
    // previous three construction paths).
    // Returns 0 if there is no active SoundFont.
    int getConfiguredMaxVoices() const;
    // Returns the number of MIDI channels tsf has allocated for the active
    // SoundFont (0 if none allocated yet / no active SoundFont).
    int getPreallocatedChannelCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // T248: render() holds this for its whole body; stop() waits on it.
    RenderBarrier renderBarrier_;

    bool initialized_ = false;
    // T359 (B015): set by AudioManager around the backend's lifetime; read by
    // loadSoundFont() as its precondition. Atomic because the two writers
    // (initialize/shutdown) and the reader are not guaranteed to be one thread.
    std::atomic<bool> audioThreadRunning_{false};
    std::atomic<bool> playing_{false};
    std::atomic<float> volume_{1.0f};
    bool looping_ = false;
    uint32_t sampleRate_ = 22050;
    SoundFontType activeSoundFont_ = SoundFontType::GM;
    std::string customSoundFontPath_;  // T504: loaded on first selectSoundFont(Custom)
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
