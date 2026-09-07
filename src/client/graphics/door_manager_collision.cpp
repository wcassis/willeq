// door_manager_collision.cpp -- DoorManager's collision writer.
//
// T435, board B425: ctx_.doorCollisionData had no writer anywhere in the tree,
// so every door in every zone was walk-through and non-standable. These four
// bodies are that writer. Split out of door_manager.cpp, which was already at
// 1,394 lines against the 1,500 ceiling.
#include "client/graphics/door_manager.h"
#include "client/graphics/door_collision.h"

namespace EQT {
namespace Graphics {

void DoorManager::markCollisionDirty(uint8_t doorId)
{
    collisionDirty_.insert(doorId);
    collisionRemoved_.erase(doorId);
}

void DoorManager::markAllCollisionDirty()
{
    for (const auto& [id, visual] : doors_) {
        collisionDirty_.insert(id);
    }
}

void DoorManager::drainCollisionUpdates(std::vector<uint8_t>& changed,
                                        std::vector<uint8_t>& removed)
{
    changed.assign(collisionDirty_.begin(), collisionDirty_.end());
    removed.assign(collisionRemoved_.begin(), collisionRemoved_.end());
    collisionDirty_.clear();
    collisionRemoved_.clear();
}

bool DoorManager::computeDoorCollision(uint8_t doorId, DoorCollisionData& out) const
{
    auto it = doors_.find(doorId);
    if (it == doors_.end()) {
        return false;
    }
    const DoorVisual& visual = it->second;

    out.closedTriangles.clear();
    out.openTriangles.clear();
    out.bspRegion = visual.bspRegion;
    out.hasCollision = DoorOpentypeHasCollision(visual.opentype);

    // A non-colliding opentype still gets an entry, so the renderer's map is a
    // full census of the zone's doors and the decision is visible in the load
    // diagnostic rather than being an absence.
    if (!out.hasCollision) {
        return true;
    }
    if (!visual.pivotNode || !visual.sceneNode || !visual.sceneNode->getMesh()) {
        return false;
    }

    // The pivot's own transform is rebuilt from the heading rather than read
    // off the node, because the node carries whatever pose the animation left
    // it in -- and because a PVS-hidden door is detached from the scene graph,
    // where absolute transforms go stale. The child (mesh) transform IS read
    // off the node: it is the fixed hinge offset plus scale, set once at build.
    const irr::core::matrix4 child = visual.sceneNode->getRelativeTransformation();
    const irr::core::vector3df pivotPos = visual.pivotNode->getPosition();

    // DoorManager's own naming is inverted and load-bearing: createDoor()
    // records "When closed: door is at openHeading" / "When open: door is at
    // closedHeading". Bake each set from the heading that pose actually uses.
    AppendMeshWorldTriangles(visual.sceneNode->getMesh(),
                             DoorPoseTransform(pivotPos, visual.openHeading) * child,
                             out.closedTriangles);
    AppendMeshWorldTriangles(visual.sceneNode->getMesh(),
                             DoorPoseTransform(pivotPos, visual.closedHeading) * child,
                             out.openTriangles);
    return true;
}

} // namespace Graphics
} // namespace EQT
