#include "client/graphics/frame_budget_governor.h"
#include "common/logging.h"
#include <algorithm>

namespace EQT {
namespace Graphics {

FrameBudgetGovernor::FrameBudgetGovernor(float targetFps)
    : targetFps_(targetFps)
    , targetFrameTimeMs_(1000.0f / targetFps)
    , frameStart_(std::chrono::steady_clock::now())
{
    // Initialize ring buffer with target frame time (assume steady state)
    for (int i = 0; i < kRingSize; ++i) {
        frameTimes_[i] = targetFrameTimeMs_;
    }
}

void FrameBudgetGovernor::beginFrame() {
    if (resetPending_) {
        resetHistory();
        resetPending_ = false;
    }
    frameStart_ = std::chrono::steady_clock::now();
}

void FrameBudgetGovernor::endFrame(float endSceneUs, bool hasPendingWork) {
    auto now = std::chrono::steady_clock::now();
    float frameMs = std::chrono::duration_cast<std::chrono::microseconds>(
        now - frameStart_).count() / 1000.0f;

    // Record full wall-clock frame time (including endScene/VSYNC wait).
    // On the shared-bus ARM SoC, progressive loading (VBO/texture uploads)
    // competes for memory bandwidth with the GPU's tile renderer. If the
    // governor ignores endScene time, it stays GREEN and keeps loading every
    // frame, saturating the bus and causing eglSwapBuffers to block 70ms+.
    // Tracking full frame time provides natural backpressure: governor goes
    // Yellow/Red when frames exceed budget, halting loads, letting the GPU
    // drain, and endScene drops back to normal. The 30-frame ring buffer
    // already smooths the double-buffer vsync oscillation (7ms/33ms averages
    // to 20ms correctly).
    frameTimes_[ringHead_] = frameMs;
    ringHead_ = (ringHead_ + 1) % kRingSize;
    if (ringCount_ < kRingSize) ringCount_++;

    updateState();

    // Stall watchdog: if the governor is stuck non-Green with pending work,
    // a transient spike (particle init, GC, kernel scheduling) may have
    // poisoned the ring buffer. After kStallThreshold consecutive stall frames,
    // reset history to re-anchor the EMA to current reality.
    if (forcedState_ < 0 && state_ != BudgetState::Green && hasPendingWork) {
        consecutiveStallFrames_++;
        if (consecutiveStallFrames_ >= kStallThreshold) {
            LOG_INFO(MOD_GRAPHICS, "Governor watchdog: {} consecutive non-Green frames with "
                     "pending work (avg {:.1f}ms, frame {:.1f}ms) — resetting history",
                     consecutiveStallFrames_, getAverageFrameTimeMs(), frameMs);
            resetHistory();
            consecutiveStallFrames_ = 0;
        }
    } else {
        consecutiveStallFrames_ = 0;
    }
}

void FrameBudgetGovernor::updateState() {
    if (forcedState_ >= 0) return;  // Forced — don't auto-update

    float avg = getAverageFrameTimeMs();
    float ratio = avg / targetFrameTimeMs_;

    BudgetState newState;
    if (ratio > 1.0f) {
        newState = BudgetState::Red;
    } else if (ratio > 0.8f) {
        newState = BudgetState::Yellow;
    } else {
        newState = BudgetState::Green;
    }

    // Downgrade: immediate
    if (newState > state_) {
        state_ = newState;
        consecutiveGoodFrames_ = 0;
        return;
    }

    // Upgrade: hysteresis — require consecutive good frames
    if (newState < state_) {
        consecutiveGoodFrames_++;
        if (consecutiveGoodFrames_ >= kUpgradeThreshold) {
            // Upgrade one step at a time
            if (state_ == BudgetState::Red) {
                state_ = BudgetState::Yellow;
            } else if (state_ == BudgetState::Yellow) {
                state_ = BudgetState::Green;
            }
            consecutiveGoodFrames_ = 0;
        }
    } else {
        // Same state — reset counter
        consecutiveGoodFrames_ = 0;
    }
}

bool FrameBudgetGovernor::canStartTask(LoadTaskWeight weight) const {
    BudgetState effectiveState = getState();

    switch (weight) {
        case LoadTaskWeight::Critical:
            // Always allowed
            return true;

        case LoadTaskWeight::Heavy:
            // Green only, with >= 5ms remaining
            return effectiveState == BudgetState::Green && getRemainingBudgetMs() >= 5.0f;

        case LoadTaskWeight::Light:
            // Green or Yellow, with >= 2ms remaining
            return effectiveState != BudgetState::Red && getRemainingBudgetMs() >= 2.0f;

        case LoadTaskWeight::Trivial:
            // Green or Yellow, with >= 0.5ms remaining
            return effectiveState != BudgetState::Red && getRemainingBudgetMs() >= 0.5f;
    }
    return false;
}

bool FrameBudgetGovernor::hasTimeBudget(float safetyMs) const {
    return getRemainingBudgetMs() >= safetyMs;
}

float FrameBudgetGovernor::getRemainingBudgetMs() const {
    float elapsedUs = static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - frameStart_).count());
    return targetFrameTimeMs_ - (elapsedUs / 1000.0f);
}

const char* FrameBudgetGovernor::getStateName() const {
    switch (getState()) {
        case BudgetState::Green:  return "Green";
        case BudgetState::Yellow: return "Yellow";
        case BudgetState::Red:    return "Red";
    }
    return "Unknown";
}

float FrameBudgetGovernor::getAverageFrameTimeMs() const {
    if (ringCount_ == 0) return targetFrameTimeMs_;
    float sum = 0;
    for (int i = 0; i < ringCount_; ++i) {
        sum += frameTimes_[i];
    }
    return sum / static_cast<float>(ringCount_);
}

float FrameBudgetGovernor::getBudgetRatio() const {
    return getAverageFrameTimeMs() / targetFrameTimeMs_;
}

void FrameBudgetGovernor::resetHistory() {
    for (int i = 0; i < kRingSize; ++i) {
        frameTimes_[i] = targetFrameTimeMs_;
    }
    ringHead_ = 0;
    ringCount_ = 0;
    state_ = BudgetState::Green;
    consecutiveGoodFrames_ = 0;
}

void FrameBudgetGovernor::setTargetFps(float fps) {
    fps = std::max(10.0f, std::min(fps, 120.0f));
    targetFps_ = fps;
    targetFrameTimeMs_ = 1000.0f / fps;
}

} // namespace Graphics
} // namespace EQT
