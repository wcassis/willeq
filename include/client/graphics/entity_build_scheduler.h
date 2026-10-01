#ifndef EQT_GRAPHICS_ENTITY_BUILD_SCHEDULER_H
#define EQT_GRAPHICS_ENTITY_BUILD_SCHEDULER_H

// B973 (GitLab #291): how much post-load entity build work the render thread
// may do in one frame. Device-free: it decides and accounts, it builds nothing.
//
// Before B973 an entity that arrived after zone-in (a spawn, a GM #spawn, a
// player walking in) was built only on frames the FrameBudgetGovernor called
// Green, one unit per Green frame. A crowd keeps the frame Red, so no Green
// frame came, nothing was built and the frame never improved: 0 of 200
// GM-spawned NPCs built in 4 minutes on the Orange Pi (B964,
// refactor/B973/README.md has the trace).
//
// The governor keeps its purpose (it still decides how MUCH work a frame may
// take); it no longer decides WHETHER any work happens:
//
//   - the first unit of a frame always runs when one is pending (progress is
//     guaranteed in every state, Red included);
//   - further units run while the frame's build work so far plus the
//     predicted cost of the next unit fits the state's slice, a fraction of
//     the governor's target frame time (Green 20 % -- the headroom the Green
//     band guarantees -- Yellow 12 %, Red 6 %: 6.7 / 4.0 / 2.0 ms at 30 fps);
//   - the prediction is the unit KIND's own measured cost (an exponential
//     average of the units of that kind that ran), so a kind that measured
//     expensive does not start a second time in a frame it does not fit.
//
// WILLEQ_ENTITY_BUILD_LEGACY=1 restores the pre-B973 Green-only, one unit per
// Green frame scheduling (the A/B the README's tables are measured with).

#include "client/graphics/frame_budget_governor.h"

#include <array>
#include <cstdint>
#include <string>

namespace EQT {
namespace Graphics {

// What a build unit does; the predictor keeps one average per kind.
enum class EntityBuildUnitKind : uint8_t {
    Create = 0,   // Placeholder -> node created and its materials promoted (B966)
    Finalize,     // MeshFinalize: name tag, corpse pose, light, race ref
    Equip,        // EquipmentAttach: weapon meshes on the point bones
    Other,        // anything else the frame did under the same slice (GPU result)
    Count
};

const char* entityBuildUnitKindName(EntityBuildUnitKind kind);

struct EntityBuildSlicePolicy {
    float greenFraction = 0.20f;   // of the governor's target frame time
    float yellowFraction = 0.12f;
    float redFraction = 0.06f;
    float emaAlpha = 0.3f;         // weight of the newest unit in its kind's average
};

// The work still to do, counted by EntityRenderer::countBuildBacklog().
struct EntityBuildBacklog {
    uint32_t built = 0;             // entities in Built
    uint32_t unbuiltTotal = 0;      // every entity not Built
    uint32_t unbuiltVisible = 0;    // not Built and in the scene graph (eligible)
    uint32_t inProgress = 0;        // past Placeholder, not Built
    uint32_t awaitingPrep = 0;      // visible Placeholders whose prep has not landed
    uint32_t pendingUnits = 0;      // estimated units left for the eligible ones
};

class EntityBuildScheduler {
public:
    explicit EntityBuildScheduler(EntityBuildSlicePolicy policy = {});

    // WILLEQ_ENTITY_BUILD_LEGACY=1 (read once by the renderer).
    static bool legacyFromEnv();
    void setLegacy(bool legacy) { legacy_ = legacy; }
    bool legacy() const { return legacy_; }

    // Per frame, before any unit: the governor's state and target frame time.
    void beginFrame(BudgetState state, float targetFrameMs);

    // May a unit of this kind start now? The first unit of a frame: always
    // (legacy: only in Green). Later units: work so far + the kind's
    // predicted cost <= the state's slice (legacy: never).
    bool mayStartUnit(EntityBuildUnitKind kind) const;

    // A unit ran and took `ms` (its own measured cost).
    void recordUnit(EntityBuildUnitKind kind, float ms);

    float sliceMs() const { return sliceMs_; }
    float spentMs() const { return spentMs_; }
    int unitsThisFrame() const { return unitsThisFrame_; }
    BudgetState state() const { return state_; }
    // The kind's average measured cost, or 0 before one has run.
    float predictedMs(EntityBuildUnitKind kind) const;

    // ---- accounting: one [entity-build] line per window -------------------
    // Called once per frame after the frame is finished with its total time.
    // Returns true when a window closed with something to say (then
    // windowLine() is the line). `backlog` is read only when the window
    // closes, so a caller may pass a stale one on the other frames
    // (windowDue() says which frame needs a fresh count).
    bool endFrame(float frameMs, const EntityBuildBacklog& backlog, double nowSeconds,
                  double windowSeconds = 1.0);
    bool windowDue(double nowSeconds, double windowSeconds = 1.0) const {
        return windowStart_ < 0.0 || nowSeconds - windowStart_ >= windowSeconds;
    }
    const std::string& windowLine() const { return windowLine_; }

    // Totals since construction (for tests and the trace).
    uint64_t totalUnits() const { return totalUnits_; }
    uint64_t totalFrames() const { return totalFrames_; }

private:
    EntityBuildSlicePolicy policy_;
    bool legacy_ = false;

    // the current frame
    BudgetState state_ = BudgetState::Green;
    float sliceMs_ = 0.0f;
    float spentMs_ = 0.0f;
    int unitsThisFrame_ = 0;

    // per-kind predictor
    std::array<float, static_cast<size_t>(EntityBuildUnitKind::Count)> emaMs_{};
    std::array<bool, static_cast<size_t>(EntityBuildUnitKind::Count)> haveEma_{};

    // the open window
    double windowStart_ = -1.0;
    uint32_t wFrames_ = 0;
    std::array<uint32_t, 3> wStateFrames_{};   // Green / Yellow / Red
    uint32_t wUnits_ = 0;
    std::array<uint32_t, static_cast<size_t>(EntityBuildUnitKind::Count)> wKindUnits_{};
    float wUnitMsSum_ = 0.0f;
    float wUnitMsMax_ = 0.0f;
    float wFrameWorkMsMax_ = 0.0f;          // most build work in one frame
    uint32_t wFramesWithWork_ = 0;
    uint32_t wFramesOverSlice_ = 0;         // build work > the frame's slice
    float wFrameMsSum_ = 0.0f;
    float wFrameMsMax_ = 0.0f;
    float wFrameMsWithWorkSum_ = 0.0f;
    float wFrameMsWithoutWorkSum_ = 0.0f;
    uint32_t wBuiltAtStart_ = 0;
    bool wHaveBuiltAtStart_ = false;
    std::string windowLine_;

    uint64_t totalUnits_ = 0;
    uint64_t totalFrames_ = 0;
};

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_ENTITY_BUILD_SCHEDULER_H
