// renderer_frame_entity_build.cpp - B973 (GitLab #291): the post-load entity
// work of one frame and its accounting.
//
// Before B973 every piece of post-load entity work -- distributing the prep
// worker's results AND every build step -- ran only on frames the
// FrameBudgetGovernor called Green, one unit per Green frame (and a re-armed
// progressive load ran under the same gate). A crowd keeps the frame Red, so
// nothing was built and the frame never improved (refactor/B973/README.md).
// Now:
//   * the prep side (queue, dispatch to the worker, distribute its results)
//     is data movement on this thread and runs every frame, in every state;
//   * build units run under EntityRenderer::buildScheduler(): the first unit
//     of a frame always, further units while they fit the state's slice;
//   * the zone VBO registration (legacy per-region path) stays Green-only,
//     its time counted against the same slice.
// WILLEQ_ENTITY_BUILD_LEGACY=1 keeps the pre-B973 code path in
// renderer_frame.cpp / renderer_zone_stream.cpp (the A/B).
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/overlays/overlays_component.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
#include "client/graphics/zone_stream/zone_stream_component.h"
#include "common/logging.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace EQT {
namespace Graphics {

namespace {
// A cheap unit (a name tag, a finalize) can measure ~0 ms; the slice alone
// would then admit any number. Bound the frame's unit count regardless.
constexpr int kMaxUnitsPerFrame = 32;

float msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

bool FrameComponent::runEntityBuildSlice() {
    if (!ctx_.entityRenderer) return false;
    EntityRenderer& er = *ctx_.entityRenderer;
    EntityBuildScheduler& sched = er.buildScheduler();
    bool any = false;

#ifdef EQT_HAS_GLES2
    // Zone VBO registration from the GPU upload thread (the legacy per-region
    // path; no producer with the zone pools). Green-only as before; its time
    // is charged to the slice so the entity units after it see it.
    if (!ctx_.progressiveLoadingActive && sched.state() == BudgetState::Green) {
        const auto t0 = std::chrono::steady_clock::now();
        if (ctx_.ZoneLifecycle().processOneGPUResult()) {
            sched.recordUnit(EntityBuildUnitKind::Other, msSince(t0));
            any = true;
        }
    }
#endif

    if (!ctx_.entityPrepReady) return any;
    RaceModelLoader* models = er.getRaceModelLoader();
    if (models) models->sweepEvictable();   // T504b Leg 2b (cheap when empty)

    // The prep side: data movement, every frame, never a unit.
    if (ctx_.entityPrepWorker && models) {
        models->promotePreparedModels();
        if (++ctx_.entityPrepScanCounter % 10 == 0) ctx_.ZoneStream().queueEntityPrepRequests();
        if (ctx_.entityPrepWorker->isIdle()) ctx_.entityPrepWorker->dispatchOne();
    }
    er.pollAndDistributePrepResults();

    if (ctx_.config.constrainedConfig.skipEntityBuild) return any;
    // Texture uploads first, one texture per unit: a create unit expects its
    // textures resident, and before B973 it uploaded EVERY pending texture in
    // one step (up to 68 / 485 ms on x86). While uploads are pending no create
    // runs; an upload that makes no progress hands over to the creates.
    int units = 0;
    while (er.hasPendingTextureUploads() && units < kMaxUnitsPerFrame) {
        const bool progressed = er.processOnePendingTextureUpload(&sched);
        if (sched.unitsThisFrame() > units) any = true;
        units = sched.unitsThisFrame();
        if (!progressed) break;
    }
    if (er.hasPendingTextureUploads() && units > 0 && !sched.mayStartUnit(EntityBuildUnitKind::Upload))
        return any;   // the slice is spent on uploads; creates wait for them
    const uint16_t target = ctx_.Overlays().currentTargetId();
    for (; units < kMaxUnitsPerFrame; ++units) {
        if (!er.processOneEntityBuildStep(ctx_.currentPvsRegion, target, &sched)) break;
        any = true;
    }
    return any;
}

void FrameComponent::finishEntityBuildFrame(float frameMs) {
    if (!ctx_.entityRenderer) return;
    EntityRenderer& er = *ctx_.entityRenderer;
    EntityBuildScheduler& sched = er.buildScheduler();
    if (entityBuildTrace_ < 0) {
        const char* v = std::getenv("WILLEQ_ENTITY_BUILD_TRACE");
        entityBuildTrace_ = (v && std::strcmp(v, "1") == 0) ? 1 : 0;
    }
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (entityBuildTrace_ == 1 || sched.windowDue(now)) lastBuildBacklog_ = er.countBuildBacklog();

    if (entityBuildTrace_ == 1 && (sched.unitsThisFrame() > 0 || lastBuildBacklog_.unbuiltVisible > 0 ||
                                   lastBuildBacklog_.inProgress > 0)) {
        static const char* kState[] = {"G", "Y", "R"};
        LOG_INFO(MOD_GRAPHICS,
                 "[entity-build-frame] f={} gov={} slice_ms={:.2f} units={} work_ms={:.2f} frame_ms={:.2f} "
                 "unbuilt_visible={} in_progress={} awaiting_prep={} pending_units={} built={}",
                 ctx_.frameNumber, kState[static_cast<int>(sched.state()) % 3], sched.sliceMs(),
                 sched.unitsThisFrame(), sched.spentMs(), frameMs, lastBuildBacklog_.unbuiltVisible,
                 lastBuildBacklog_.inProgress, lastBuildBacklog_.awaitingPrep,
                 lastBuildBacklog_.pendingUnits, lastBuildBacklog_.built);
    }
    if (sched.endFrame(frameMs, lastBuildBacklog_, now)) {
        LOG_INFO(MOD_GRAPHICS, "{}", sched.windowLine());
    }
}

}  // namespace Graphics
}  // namespace EQT
