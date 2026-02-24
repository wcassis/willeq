#ifndef EQT_GRAPHICS_FRAME_BUDGET_GOVERNOR_H
#define EQT_GRAPHICS_FRAME_BUDGET_GOVERNOR_H

#include <chrono>
#include <cstdint>

namespace EQT {
namespace Graphics {

// Budget state machine: controls loading aggressiveness based on frame time history
enum class BudgetState : uint8_t {
    Green,   // avg < 80% target: full budget, all task types allowed
    Yellow,  // avg 80-100%: capped 2ms budget, only Light/Trivial/Critical tasks
    Red      // avg > 100%: no loading except Critical (player entity, player region)
};

// Weight classes for load tasks — determines which BudgetState allows them
enum class LoadTaskWeight : uint8_t {
    Critical,  // Always allowed (player entity, player's BSP region)
    Heavy,     // Green only, >= 5ms remaining (entity loads, region builds)
    Light,     // Green or Yellow, >= 2ms remaining (doors, objects, cached entities)
    Trivial    // Green or Yellow, >= 0.5ms remaining (collision, VBO uploads)
};

// Tracks rolling frame times and enforces a Green/Yellow/Red state machine
// that throttles loading to guarantee minimum FPS.
class FrameBudgetGovernor {
public:
    explicit FrameBudgetGovernor(float targetFps = 30.0f);

    // Call at the start of each frame
    void beginFrame();

    // Call at the end of each frame (records frame time into ring buffer)
    void endFrame();

    // Policy check: can a task of this weight class start now?
    bool canStartTask(LoadTaskWeight weight) const;

    // Real-time budget check: enough time left in this frame?
    // safetyMs is subtracted from remaining budget (default 2ms)
    bool hasTimeBudget(float safetyMs = 2.0f) const;

    // How much budget (ms) remains in the current frame?
    float getRemainingBudgetMs() const;

    // Target frame time in ms (e.g. 33.3 for 30fps)
    float getTargetFrameTimeMs() const { return targetFrameTimeMs_; }

    // Current state
    BudgetState getState() const { return forcedState_ >= 0 ? static_cast<BudgetState>(forcedState_) : state_; }
    const char* getStateName() const;

    // Rolling average frame time (ms) over the ring buffer window
    float getAverageFrameTimeMs() const;

    // Ratio of average frame time to target (1.0 = exactly at target)
    float getBudgetRatio() const;

    // Runtime target FPS adjustment
    void setTargetFps(float fps);
    float getTargetFps() const { return targetFps_; }

    // Reset history — reinitialize ring buffer with target frame time.
    // Call after loading screen (long frames poison the rolling average).
    void resetHistory();

    // Request deferred reset — the actual reset happens at the START of the
    // next frame (in beginFrame), so the current poisoned frame is discarded.
    // Use this when resetting mid-frame (e.g. after deferred init) where
    // endFrame() would immediately re-poison the buffer.
    void requestReset() { resetPending_ = true; }

    // Debug overrides
    void forceState(BudgetState state) { forcedState_ = static_cast<int8_t>(state); }
    void clearForcedState() { forcedState_ = -1; }
    bool isForced() const { return forcedState_ >= 0; }

private:
    void updateState();

    float targetFps_;
    float targetFrameTimeMs_;

    // 30-frame ring buffer for frame times
    static constexpr int kRingSize = 30;
    float frameTimes_[kRingSize] = {};
    int ringHead_ = 0;
    int ringCount_ = 0;

    // State machine
    BudgetState state_ = BudgetState::Green;
    int consecutiveGoodFrames_ = 0;
    static constexpr int kUpgradeThreshold = 10;  // 10 good frames to upgrade one step

    // Per-frame timing
    std::chrono::steady_clock::time_point frameStart_;

    // Deferred reset flag — beginFrame() checks and performs reset
    bool resetPending_ = false;

    // Debug override (-1 = auto, 0/1/2 = forced Green/Yellow/Red)
    int8_t forcedState_ = -1;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_FRAME_BUDGET_GOVERNOR_H
