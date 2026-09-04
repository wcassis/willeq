#include "client/graphics/zone_load/zone_load_component.h"
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
// whole codebase — ctx_.entityPrepWorker was permanently null, so deferred
// entities would never have been picked back up. See
// refactor/T109_entity_load_distance_gate.md's Review for the full finding
// and verification.
//
// L12 (Batch L close-out) folded both bodies into ZoneLoadComponent and moved
// the TU into zone_load/ -- L10 deviation 1, closed. They were single-family
// by every measurement (their only callers are loadZoneStep8_Entities in
// zone_load/renderer_zone_load_entities.cpp); L10 left them behind only
// because this TU was outside that unit's territory. The bodies are verbatim:
// the only edits are the mechanism's substitutions (ctx_.config -> ctx_.config,
// ctx_.entityPrepWorker -> ctx_.entityPrepWorker, ctx_.entityRenderer ->
// ctx_.entityRenderer, owner_.backgroundThreadPool_ -> owner_.backgroundThreadPool_).

namespace EQT {
namespace Graphics {

void ZoneLoadComponent::ensureEntityPrepWorkerStarted(RaceModelLoader* modelLoader,
                                                       EquipmentModelLoader* equipLoader) {
    // unloadZone() (renderer_zone_lifecycle.cpp) tears this down —
    // entityPrepWorker_.reset() + entityRenderer_->setEntityPrepWorker(nullptr)
    // — on every zone transition, so it must be rebuilt here on every zone
    // load. Idempotent within a load: only the first Step 8 entity triggers
    // real construction.
    if (ctx_.entityPrepWorker) return;

    ctx_.entityPrepWorker = std::make_unique<EntityPrepWorker>(modelLoader, equipLoader);
    if (owner_.backgroundThreadPool_) {
        ctx_.entityPrepWorker->setBackgroundThreadPool(owner_.backgroundThreadPool_.get());
    }
    ctx_.entityPrepWorker->start();
    if (ctx_.entityRenderer) {
        ctx_.entityRenderer->setEntityPrepWorker(ctx_.entityPrepWorker.get());
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
bool ZoneLoadComponent::deferEntityForLoadGate(uint16_t spawnId, const std::string& name,
                                               uint16_t raceId, uint8_t gender, float distFromPlayer,
                                               size_t& deferredCount,
                                               std::set<std::pair<uint16_t, uint8_t>>& deferredPairs) const {
    float radius = ctx_.config.constrainedConfig.entityLoadRadius;
    if (distFromPlayer <= radius) return false;
    ++deferredCount;
    deferredPairs.insert({raceId, gender});
    LOG_DEBUG(MOD_GRAPHICS, "SEQ Step8: entity {} ({}) race={} gender={} deferred — dist={:.1f} > entityLoadRadius={:.1f}",
              spawnId, name, raceId, gender, distFromPlayer, radius);
    return true;
}

} // namespace Graphics
} // namespace EQT
