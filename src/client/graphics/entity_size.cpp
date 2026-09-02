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

    float scale = entityScaleForServerSize(raceScale, serverSize);
    visual.sceneNode->setScale(irr::core::vector3df(scale, scale, scale));
    // The node origin stays on the server Z, so the origin-to-feet distance
    // has to follow the scale or the entity sinks into / floats over the floor.
    visual.collisionZOffset =
        collisionZOffsetFromBounds(visual.sceneNode->getBoundingBox(), scale);

    LOG_DEBUG(MOD_ENTITY, "Entity {} ({}) resized: serverSize={:.2f} scale={:.3f}",
              spawnId, visual.name, serverSize, scale);
    return true;
}

}  // namespace Graphics
}  // namespace EQT
