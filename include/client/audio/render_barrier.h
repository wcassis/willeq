#pragma once

#ifdef WITH_AUDIO

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace EQT {
namespace Audio {

// T248: the handshake between a real-time render callback and the thread
// that tears down what the callback reads.
//
// Problem it solves: the audio thread (miniaudio's worker, or the RDP pump)
// runs render code that reads shared state -- TinySoundFont voices, a mix
// channel's fields -- with no lock (a lock inside the callback is forbidden:
// T130/T131). The game thread wants to reset/free that state (MidiPlayer::
// stop -> tsf_reset/tml_free, AudioMixer::freeChannel -> the channel is
// reused). Flipping a "stop" flag alone is not enough: the callback may
// already be past the flag check and still inside the render.
//
// Protocol (the render side never blocks, never allocates, never locks):
//   render thread:   enter(); <read shared state>; exit();
//   teardown thread: <publish "stop" (an atomic store the render side checks
//                    right after enter())>; waitQuiescent(); <mutate freely>
//
// enter() bumps an epoch counter to ODD and issues a seq_cst fence;
// exit() bumps it back to EVEN with release semantics. waitQuiescent()
// issues a seq_cst fence, then reads the epoch:
//   - EVEN: no render is in progress. The two seq_cst fences form the
//     classic store-buffering pair, so any render that starts later is
//     guaranteed to observe the teardown thread's "stop" store (it cannot
//     both start before our fence and miss the store). Return at once.
//   - ODD:  a render is in flight and may be reading the state. Poll until
//     the epoch changes (the render exited; its release-exit / our acquire-
//     load pair orders every read it made before our subsequent writes).
// That is the "one-frame barrier": the wait is bounded by the remainder of
// ONE render pass (a 512-frame period at 22050 Hz is ~23 ms of audio and
// far less of CPU), and it costs the callback two relaxed RMWs + one fence.
//
// Re-entrancy: a teardown issued FROM the render thread itself (e.g.
// MusicPlayer's fade-out completion calls MidiPlayer::stop() inside the
// mixer callback) would wait for its own exit forever, so waitQuiescent()
// returns immediately when called on the thread that last entered.
//
// Bounded: waitQuiescent() gives up after `timeout` and returns false. That
// is a "the audio thread is wedged" situation, not a normal one -- callers
// log it and proceed rather than hanging shutdown.
class RenderBarrier {
public:
    // Render side. Call at the top of the callback, before reading anything
    // the teardown side may mutate (including the "stop" flag itself).
    void enter() noexcept {
        renderThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
        epoch_.fetch_add(1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    // Render side. Call after the last read of shared state.
    void exit() noexcept {
        epoch_.fetch_add(1, std::memory_order_release);
    }

    // RAII pair for enter()/exit().
    class Scope {
    public:
        explicit Scope(RenderBarrier& b) noexcept : b_(b) { b_.enter(); }
        ~Scope() { b_.exit(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        RenderBarrier& b_;
    };

    // Teardown side. Publish the stop condition BEFORE calling this. Returns
    // true once no render pass can still be reading the pre-stop state;
    // false if a render pass stayed in flight for longer than `timeout`.
    bool waitQuiescent(std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) noexcept {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const uint64_t seen = epoch_.load(std::memory_order_acquire);
        if ((seen & 1u) == 0) {
            return true;  // idle -- and any later enter() sees our stores
        }
        if (renderThread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
            return true;  // we ARE the render thread (re-entrant teardown)
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        int spins = 0;
        while (epoch_.load(std::memory_order_acquire) == seen) {
            if (++spins < 256) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                if (std::chrono::steady_clock::now() >= deadline) {
                    return epoch_.load(std::memory_order_acquire) != seen;
                }
            }
        }
        return true;
    }

    // True while a render pass is between enter() and exit(). Diagnostic
    // only (tests); racy by nature.
    bool inRender() const noexcept {
        return (epoch_.load(std::memory_order_acquire) & 1u) != 0;
    }

private:
    std::atomic<uint64_t> epoch_{0};
    std::atomic<std::thread::id> renderThread_{};
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
