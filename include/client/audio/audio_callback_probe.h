#pragma once

// T130: lock-free, allocation-free timing probe for the real-time audio
// callback (miniaudioDataCallback -> AudioMixer::render -> ...).
//
// Context: T130 established that the steady-state callback DOES allocate
// (TinySoundFont's tsf_channel_init/tsf_note_on/tsf_reset, reached from
// MidiPlayer::render's MIDI event pump -- see refactor/T130_audio_cluster.md
// Review). Since miniaudio itself exposes no underrun/xrun notification to
// the application (corrects T128's "wiring omission" framing -- there is
// nothing to wire), any signal for "is the callback being stalled by a lock
// (e.g. the M_ARENA_MAX=1 single malloc arena) the LoadingThread can hold
// during zone load" has to be built. This is that signal.
//
// Method: record wall-clock deltas between consecutive callback invocations
// and count how many exceed 1.5x the configured expected period (the
// period miniaudio was configured with, periodSizeInFrames / sampleRate).
// Same constraints T122's probe used: lock-free (all atomics, relaxed
// ordering -- exact interleaving across threads doesn't matter, only
// magnitude), allocation-free, no logging/blocking-syscalls in the hot
// path. The one "syscall" used, clock_gettime(CLOCK_MONOTONIC), is
// vDSO-resolved on Linux (no kernel entry, no context switch) -- the
// standard technique for RT-audio timing instrumentation, distinct from a
// blocking syscall like write().
//
// Reporting happens elsewhere (renderer_diagnostics.cpp's /pmem "[Audio
// Callback]" line, sourced via AudioMixer -> AudioManager -> eq_commands.cpp
// on the game thread) -- never from the audio thread itself.

#include <atomic>
#include <cstdint>

namespace EQT {
namespace Audio {

struct AudioCallbackProbe {
    // Header-only (C++17 inline static data members) -- no separate .cpp
    // needed, and every TU that includes this header shares the same
    // single instance per ODR.
    static inline std::atomic<uint64_t> callbackCount{0};
    static inline std::atomic<int64_t> lastTimestampNs{0};
    static inline std::atomic<int64_t> expectedPeriodNs{0};
    static inline std::atomic<uint64_t> lateCallbackCount{0};
    static inline std::atomic<int64_t> worstDeltaNs{0};

    // Called once, from MiniaudioBackend::init(), with the configured
    // period in nanoseconds (periodSizeInFrames * 1e9 / sampleRate).
    static void configure(int64_t periodNs) {
        expectedPeriodNs.store(periodNs, std::memory_order_relaxed);
        lastTimestampNs.store(0, std::memory_order_relaxed);
        callbackCount.store(0, std::memory_order_relaxed);
        lateCallbackCount.store(0, std::memory_order_relaxed);
        worstDeltaNs.store(0, std::memory_order_relaxed);
    }

    // Called at the top of miniaudioDataCallback, on the real-time audio
    // thread. Lock-free, allocation-free.
    static void onCallback(int64_t nowNs) {
        callbackCount.fetch_add(1, std::memory_order_relaxed);
        int64_t prev = lastTimestampNs.exchange(nowNs, std::memory_order_relaxed);
        if (prev == 0) return;  // first callback since configure(); no delta yet
        int64_t delta = nowNs - prev;
        if (delta < 0) return;  // clock oddity; ignore rather than corrupt worst/late

        int64_t expected = expectedPeriodNs.load(std::memory_order_relaxed);
        if (expected > 0 && delta > expected + expected / 2) {
            lateCallbackCount.fetch_add(1, std::memory_order_relaxed);
        }

        int64_t worst = worstDeltaNs.load(std::memory_order_relaxed);
        while (delta > worst &&
               !worstDeltaNs.compare_exchange_weak(worst, delta, std::memory_order_relaxed)) {
        }
    }
};

}  // namespace Audio
}  // namespace EQT
