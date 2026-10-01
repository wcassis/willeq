// B973 (GitLab #291): post-load entity build scheduling. See the header for
// the rule and refactor/B973/README.md for the trace that motivated it.
#include "client/graphics/entity_build_scheduler.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>

namespace EQT {
namespace Graphics {

const char* entityBuildUnitKindName(EntityBuildUnitKind kind) {
    switch (kind) {
        case EntityBuildUnitKind::Upload: return "upload";
        case EntityBuildUnitKind::Create: return "create";
        case EntityBuildUnitKind::Finalize: return "finalize";
        case EntityBuildUnitKind::Equip: return "equip";
        case EntityBuildUnitKind::Other: return "other";
        case EntityBuildUnitKind::Count: break;
    }
    return "?";
}

EntityBuildScheduler::EntityBuildScheduler(EntityBuildSlicePolicy policy) : policy_(policy) {}

bool EntityBuildScheduler::legacyFromEnv() {
    const char* v = std::getenv("WILLEQ_ENTITY_BUILD_LEGACY");
    return v && std::strcmp(v, "1") == 0;
}

void EntityBuildScheduler::beginFrame(BudgetState state, float targetFrameMs) {
    state_ = state;
    float fraction = policy_.greenFraction;
    if (state == BudgetState::Yellow) fraction = policy_.yellowFraction;
    if (state == BudgetState::Red) fraction = policy_.redFraction;
    sliceMs_ = std::max(0.0f, targetFrameMs) * fraction;
    spentMs_ = 0.0f;
    unitsThisFrame_ = 0;
}

float EntityBuildScheduler::predictedMs(EntityBuildUnitKind kind) const {
    const size_t k = static_cast<size_t>(kind);
    if (k >= emaMs_.size() || !haveEma_[k]) return 0.0f;
    return emaMs_[k];
}

bool EntityBuildScheduler::mayStartUnit(EntityBuildUnitKind kind) const {
    if (legacy_) return state_ == BudgetState::Green && unitsThisFrame_ == 0;
    if (unitsThisFrame_ == 0) return true;   // guaranteed progress, every state
    return spentMs_ + predictedMs(kind) <= sliceMs_;
}

void EntityBuildScheduler::recordUnit(EntityBuildUnitKind kind, float ms) {
    ms = std::max(0.0f, ms);
    const size_t k = static_cast<size_t>(kind);
    if (k < emaMs_.size()) {
        if (!haveEma_[k]) {
            emaMs_[k] = ms;
            haveEma_[k] = true;
        } else {
            emaMs_[k] += policy_.emaAlpha * (ms - emaMs_[k]);
        }
        wKindUnits_[k]++;
    }
    spentMs_ += ms;
    unitsThisFrame_++;
    totalUnits_++;
    wUnits_++;
    wUnitMsSum_ += ms;
    wUnitMsMax_ = std::max(wUnitMsMax_, ms);
}

bool EntityBuildScheduler::endFrame(float frameMs, const EntityBuildBacklog& backlog,
                                    double nowSeconds, double windowSeconds) {
    totalFrames_++;
    if (windowStart_ < 0.0) {
        windowStart_ = nowSeconds;
        wBuiltAtStart_ = backlog.built;   // the first frame's count is fresh (windowDue)
        wHaveBuiltAtStart_ = true;
    }
    wFrames_++;
    wStateFrames_[std::min<size_t>(static_cast<size_t>(state_), 2)]++;
    wFrameMsSum_ += frameMs;
    wFrameMsMax_ = std::max(wFrameMsMax_, frameMs);
    if (unitsThisFrame_ > 0) {
        wFramesWithWork_++;
        wFrameWorkMsMax_ = std::max(wFrameWorkMsMax_, spentMs_);
        if (spentMs_ > sliceMs_) wFramesOverSlice_++;
        wFrameMsWithWorkSum_ += frameMs;
    } else {
        wFrameMsWithoutWorkSum_ += frameMs;
    }
    // The frame's units are accounted; a frame that never calls beginFrame()
    // again (zone change) must not carry them into the next window.
    unitsThisFrame_ = 0;
    spentMs_ = 0.0f;

    if (nowSeconds - windowStart_ < windowSeconds) return false;

    const bool interesting = wUnits_ > 0 || backlog.unbuiltVisible > 0 || backlog.inProgress > 0;
    if (interesting) {
        const auto avg = [](float sum, uint32_t n) { return n ? sum / static_cast<float>(n) : 0.0f; };
        const int builtDelta = static_cast<int>(backlog.built) - static_cast<int>(wBuiltAtStart_);
        windowLine_ = fmt::format(
            "[entity-build] mode={} window_s={:.2f} frames={} gov=G{}/Y{}/R{} units={} "
            "(upload={} create={} finalize={} equip={} other={}) unit_ms avg={:.2f} max={:.2f} "
            "frame_work_ms max={:.2f} frames_with_work={} over_slice={} "
            "frame_ms avg={:.1f} max={:.1f} with_work={:.1f} without_work={:.1f} "
            "built={} built_delta={:+d} unbuilt_visible={} unbuilt_total={} in_progress={} "
            "awaiting_prep={} pending_units={} pending_uploads={} pred_ms upload={:.2f} create={:.2f} "
            "finalize={:.2f} equip={:.2f}",
            legacy_ ? "legacy" : "sliced", nowSeconds - windowStart_, wFrames_,
            wStateFrames_[0], wStateFrames_[1], wStateFrames_[2], wUnits_,
            wKindUnits_[0], wKindUnits_[1], wKindUnits_[2], wKindUnits_[3], wKindUnits_[4],
            avg(wUnitMsSum_, wUnits_), wUnitMsMax_, wFrameWorkMsMax_, wFramesWithWork_,
            wFramesOverSlice_, avg(wFrameMsSum_, wFrames_), wFrameMsMax_,
            avg(wFrameMsWithWorkSum_, wFramesWithWork_),
            avg(wFrameMsWithoutWorkSum_, wFrames_ - wFramesWithWork_),
            backlog.built, builtDelta, backlog.unbuiltVisible, backlog.unbuiltTotal,
            backlog.inProgress, backlog.awaitingPrep, backlog.pendingUnits, backlog.pendingUploads,
            predictedMs(EntityBuildUnitKind::Upload), predictedMs(EntityBuildUnitKind::Create), predictedMs(EntityBuildUnitKind::Finalize),
            predictedMs(EntityBuildUnitKind::Equip));
    }

    windowStart_ = nowSeconds;
    wFrames_ = 0;
    wStateFrames_ = {};
    wUnits_ = 0;
    wKindUnits_ = {};
    wUnitMsSum_ = wUnitMsMax_ = wFrameWorkMsMax_ = 0.0f;
    wFramesWithWork_ = wFramesOverSlice_ = 0;
    wFrameMsSum_ = wFrameMsMax_ = wFrameMsWithWorkSum_ = wFrameMsWithoutWorkSum_ = 0.0f;
    wBuiltAtStart_ = backlog.built;
    return interesting;
}

}  // namespace Graphics
}  // namespace EQT
