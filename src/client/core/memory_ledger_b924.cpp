// B924 (#243): the measurement device for the post-ready gpu_driver growth
// hold. A separate TU from memory_ledger.cpp (which is at its line ceiling):
//   * the rendered-frame counter the [memledger] line prints as `frames=`,
//     so an idle point can be correlated with FRAMES rendered (does
//     gpu_driver grow with frames or with wall time?);
//   * the WILLEQ_FPS_CAP=<fps> render-loop pace, for a capped-vs-uncapped
//     A/B hold;
//   * the WILLEQ_IDLE_MEMMAP=1 switch that makes pollSteadyState() print the
//     full [memmap] census at every idle point.
// All three are measurement devices, off by default and never used by any
// gate. The per-frame cost is one relaxed atomic add.
#include "client/core/memory_ledger.h"

#include <atomic>
#include <cstdlib>
#include <thread>
#include <chrono>

namespace {
std::atomic<uint64_t> g_renderedFrames{0};
}  // namespace

namespace eqt::memory {

void noteRenderedFrame() {
    g_renderedFrames.fetch_add(1, std::memory_order_relaxed);
    // WILLEQ_FPS_CAP=<fps>: pace the render loop to at most <fps> frames/s.
    // The frames-vs-time A/B; read once, off by default.
    static const int capFps = [] {
        const char* v = std::getenv("WILLEQ_FPS_CAP");
        return v ? std::atoi(v) : 0;
    }();
    if (capFps <= 0) return;
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::chrono::steady_clock::duration interval =
        std::chrono::microseconds(1000000 / capFps);
    const std::chrono::steady_clock::time_point next = last + interval;
    if (now < next) std::this_thread::sleep_until(next);
    last = std::chrono::steady_clock::now();
}

uint64_t renderedFrames() {
    return g_renderedFrames.load(std::memory_order_relaxed);
}

bool idleMemmapEnabled() {
    static const bool on = [] {
        const char* v = std::getenv("WILLEQ_IDLE_MEMMAP");
        return v && v[0] == '1' && !v[1];
    }();
    return on;
}

}  // namespace eqt::memory
