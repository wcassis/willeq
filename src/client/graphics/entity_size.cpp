/*
 * T318 -- entity size changes (shrink, grow, illusion).
 *
 * Split out of entity_renderer.cpp, which was over the file-size ceiling and is
 * scheduled for splitting as Batch J's J03.
 */
#include "client/graphics/entity_renderer.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

// T318: shrink, grow and illusion. The server is authoritative -- eqemu's
// Mob::ChangeSize() sends OP_SpawnAppearance AT_SIZE (29) to the whole zone,
// and an illusion carries its own size in Illusion_Struct -- so this is a
// REACTION to the server's word, never a local prediction (the T310 rule).
//
// Before this existed the size arrived exactly once, at registerEntity(), and
// was destroyed by the mesh build; every shrink and grow in the game was
// silently dropped after the client had already told the player it worked.
bool EntityRenderer::setEntitySize(uint16_t spawnId, float serverSize) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return false;
    }
    if (!(serverSize > 0.0f)) {
        return false;  // 0 / negative / NaN is not a size the server can mean
    }

    EntityVisual& visual = it->second;
    visual.serverSize = serverSize;

    // Not built yet (or still a placeholder cube whose size is baked into its
    // mesh): the stored value is what the build will scale by, so there is
    // nothing to rescale here.
    if (!visual.meshBuilt || visual.usesPlaceholder || !visual.sceneNode) {
        return true;
    }

    float raceScale = getScaleForRace(visual.raceId);
    if (raceScale <= 0.0f) {
        return true;  // invisible race, deliberately never given a node
    }

    // B1172: the client's AT_Size path (0x4524e9 -> 0x481d5d) re-runs the
    // same actor-scale rule on the new size (actor_scale.h).
    float scale = clientActorScale(visual.raceId, visual.gender, serverSize);
    visual.sceneNode->setScale(irr::core::vector3df(scale, scale, scale));
    // The node origin stays on the server Z, so the origin-to-feet distance
    // has to follow the scale or the entity sinks into / floats over the floor.
    visual.collisionZOffset =
        collisionZOffsetFromBounds(visual.sceneNode->getBoundingBox(), scale);

    // T359 (board B011) -- THE DOCUMENTED LIMIT. A boat's collisionHeight and
    // deckZ (EntityVisual, used by findBoatDeckZ to stand the player on a deck)
    // are NOT recomputed here: they are derived once at mesh-build time from the
    // unscaled mesh bounding box times the build-time scale, so a resized boat
    // keeps its original deck surface. Deliberate, not an oversight -- nothing
    // in the game resizes a boat (eqemu's size changes come from
    // Mob::ChangeSize(), i.e. SPA 89 / SPA 58 on players and NPCs; boats are
    // zone-owned static spawns nothing casts on), and adding the recompute here
    // would be a THIRD copy of a formula whose two existing copies
    // (entity_mesh_build.cpp and entity_build_step.cpp) already read their
    // bounding box and their Z from different places. The limit is pinned, with
    // a real Ship mesh, by
    // EntityRescaleNodeTest.ResizingABoatLeavesItsDeckSurfaceWhereItWas -- so if
    // a future unit makes boats resizable, that test fails and says why.
    LOG_DEBUG(MOD_ENTITY, "Entity {} ({}) resized: serverSize={:.2f} scale={:.3f}",
              spawnId, visual.name, serverSize, scale);
    return true;
}

}  // namespace Graphics
}  // namespace EQT
