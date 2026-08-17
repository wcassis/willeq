#pragma once

// T131: lock-free, allocation-free peak-concurrent-voice and dropped-note
// telemetry for TinySoundFont, sampled from the real-time audio callback
// thread (MidiPlayer::render). Mirrors audio_callback_probe.h's pattern
// (T130) exactly, for the same reason: this is a hot-path counter, so it
// has to be as cheap and as safe as the thing it's watching.
//
// Context: T131 pre-allocates every tsf instance's voice array
// (tsf_set_max_voices) and MIDI channel array (pre-creating all 16 channels)
// at init so the callback-thread allocation T130 found doesn't happen in
// steady state. The max_voices choice is a real tradeoff -- too low silently
// drops notes (tsf_note_on/tsf_channel_note_on return 0 when no voice is
// free), too high wastes memory on a 512MB target -- so it has to be chosen
// from a measured peak, not guessed. This probe supplies that measurement
// (peakVoicesSeen, sampled via the allocation-free tsf_active_voice_count())
// and answers "what would surface it if the cap is ever hit" on an ongoing
// basis (noteOnFailures), the same way AudioCallbackProbe answers "is the
// callback stalling" on an ongoing basis rather than only during a one-off
// investigation.
//
// Reporting happens elsewhere (renderer_diagnostics.cpp's /pmem "[Audio
// Voices]" line, sourced via AudioManager -> eq_commands.cpp on the game
// thread) -- never from the audio thread itself.

#include <atomic>
#include <cstdint>

namespace EQT {
namespace Audio {

struct MidiVoiceProbe {
    // Header-only (C++17 inline static data members), shared across every
    // TU that includes this header, same as AudioCallbackProbe.
    static inline std::atomic<int> peakVoicesSeen{0};
    static inline std::atomic<uint64_t> noteOnFailures{0};

    // Called from MidiPlayer::render(), on the audio thread, once per
    // render() invocation (not once per rendered sample -- a fixed, small,
    // bounded cost per callback). `activeVoices` is the caller's own
    // allocation-free tsf_active_voice_count() result; this function only
    // does a relaxed load + a CAS retry loop to track the max, no syscalls,
    // no allocation, no logging.
    static void observe(int activeVoices) {
        int prev = peakVoicesSeen.load(std::memory_order_relaxed);
        while (activeVoices > prev &&
               !peakVoicesSeen.compare_exchange_weak(prev, activeVoices, std::memory_order_relaxed)) {
        }
    }

    // Called from MidiPlayer::render(), on the audio thread, whenever
    // tsf_channel_note_on()/tsf_note_on() returns 0 (voice allocation
    // failed -- with max_voices set, this means every pre-allocated voice
    // is busy and the note was silently dropped, not that TSF tried and
    // failed to grow the array; see tsf_note_on's `if (f->maxVoiceNum)`
    // steal-a-releasing-voice branch, which itself never allocates).
    static void recordNoteOnFailure() {
        noteOnFailures.fetch_add(1, std::memory_order_relaxed);
    }

    static void reset() {
        peakVoicesSeen.store(0, std::memory_order_relaxed);
        noteOnFailures.store(0, std::memory_order_relaxed);
    }
};

}  // namespace Audio
}  // namespace EQT
