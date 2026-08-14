#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_prep_worker.h"
#include "common/logging.h"

// T109: entity load distance gate support. Split into its own TU (rather
// than growing renderer_zone_load.cpp, already at the file-size gate's
// ceiling) to hold the EntityPrepWorker construction — the piece of this
// unit's work that isn't the gate itself but is required for the gate to be
// safe: EntityPrepWorker (background CPU-side entity model prep, consumed by
// queueEntityPrepRequests()/processFrameProgressiveLoad() in
// renderer_zone_stream.cpp) was fully implemented and wired into the
// post-load streaming path, but had exactly zero construction sites in the
// whole codebase — entityPrepWorker_ was permanently null, so deferred
// entities would never have been picked back up. See
// refactor/T109_entity_load_distance_gate.md's Review for the full finding
// and verification.

namespace EQT {
namespace Graphics {

void IrrlichtRenderer::ensureEntityPrepWorkerStarted(RaceModelLoader* modelLoader,
                                                       EquipmentModelLoader* equipLoader) {
    // unloadZone() (renderer_zone_lifecycle.cpp) tears this down —
    // entityPrepWorker_.reset() + entityRenderer_->setEntityPrepWorker(nullptr)
    // — on every zone transition, so it must be rebuilt here on every zone
    // load. Idempotent within a load: only the first Step 8 entity triggers
    // real construction.
    if (entityPrepWorker_) return;

    entityPrepWorker_ = std::make_unique<EntityPrepWorker>(modelLoader, equipLoader);
    if (backgroundThreadPool_) {
        entityPrepWorker_->setBackgroundThreadPool(backgroundThreadPool_.get());
    }
    entityPrepWorker_->start();
    if (entityRenderer_) {
        entityRenderer_->setEntityPrepWorker(entityPrepWorker_.get());
    }
    LOG_INFO(MOD_GRAPHICS, "SEQ Step8: EntityPrepWorker constructed and started "
             "(background prep for entities deferred by the T109 load-distance gate)");
}

// The distance load gate itself: entities beyond entityLoadRadius skip the
// whole inline 8a-8f prep+build chain (no S3D read, WLD parse, or mesh
// build) at Step 8. Left with meshBuilt=false, inSceneGraph defaulted true
// (EntityVisual's own default) — exactly what
// EntityRenderer::getUnbuiltEntities()/queueEntityPrepRequests() look for,
// so the entity BECOMES A CANDIDATE for background streaming. Becoming a
// candidate is not the same as being reliably built, though: T109
// live-confirmed that candidates outside the player's landing BSP region
// are never actually drained by the current streaming path (see
// constrained_renderer_config.h's entityLoadRadius comment) — which is why
// entityLoadRadius ships at an inert default and this function currently
// never returns true in any shipped configuration.
bool IrrlichtRenderer::deferEntityForLoadGate(uint16_t spawnId, const std::string& name,
                                               uint16_t raceId, uint8_t gender, float distFromPlayer,
                                               size_t& deferredCount,
                                               std::set<std::pair<uint16_t, uint8_t>>& deferredPairs) const {
    float radius = config_.constrainedConfig.entityLoadRadius;
    if (distFromPlayer <= radius) return false;
    ++deferredCount;
    deferredPairs.insert({raceId, gender});
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entity {} ({}) race={} gender={} deferred — dist={:.1f} > entityLoadRadius={:.1f}",
              spawnId, name, raceId, gender, distFromPlayer, radius);
    return true;
}

} // namespace Graphics
} // namespace EQT
