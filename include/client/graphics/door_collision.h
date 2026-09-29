// door_collision.h -- the door half of player collision, kept device-free.
//
// T435 (board B425). DoorCollisionData and the ray test that reads it used to
// live in renderer_types.h / renderer_player_collision.cpp, where nothing but a
// whole IrrlichtRenderer could reach them -- and the map had no writer at all,
// so every door in every zone was walk-through and non-standable. The pieces
// that decide a door's collision are pure functions of a mesh, a transform and
// an opentype, so they live here and are unit-tested directly.
#ifndef EQT_GRAPHICS_DOOR_COLLISION_H
#define EQT_GRAPHICS_DOOR_COLLISION_H

#include <irrlicht.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "client/graphics/region_world_triangles.h"

namespace EQT {
namespace Graphics {

// Pre-baked door collision: the door's world-space triangles in each of its two
// settled poses. Which set a query uses is decided by the door's live isOpen.
struct DoorCollisionData {
    std::vector<irr::core::triangle3df> closedTriangles;
    std::vector<irr::core::triangle3df> openTriangles;
    size_t bspRegion = SIZE_MAX;
    // Set from the door's OPENTYPE by DoorOpentypeHasCollision(); see that
    // function for the table and the evidence behind each row.
    bool hasCollision = true;
};

// Does a door of this opentype take part in collision at all?
//
// T433's census settled that the door NAME is not a classifier and the opentype
// is: 52 rows named OBJ_BLUETORCHSWITCH are opentype 59 (the lift type), and 16
// rows named FAYLEVATOR in gfaydark are opentype 54 (the invisible type). So
// this is a pure function of the opentype and of nothing else.
bool DoorOpentypeHasCollision(uint8_t opentype);

// The world transform of a door's PIVOT at a given heading. Mirrors
// DoorManager::createDoor()/update() exactly: the pivot sits at the door's EQ
// position converted to Irrlicht Y-up, rotated about Y by
// (-heading * 360/512 + 90) degrees.
irr::core::matrix4 DoorPoseTransform(const irr::core::vector3df& pivotPosition,
                                     float heading512);

// Append every triangle of `mesh`, transformed by `xf`, to `out`.
void AppendMeshWorldTriangles(irr::scene::IMesh* mesh,
                              const irr::core::matrix4& xf,
                              std::vector<irr::core::triangle3df>& out);

// Moller-Trumbore ray/triangle intersection over pre-transformed world-space
// triangles. Moved here from PlayerCameraComponent so the door query does not
// need a renderer; the static member still exists and forwards to this.
bool RayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                           const irr::core::vector3df& rayDir, float rayLen,
                           const std::vector<irr::core::triangle3df>& triangles,
                           irr::core::vector3df& hitPoint,
                           irr::core::triangle3df& hitTriangle,
                           float& hitDistSq);
// B909: the same test over one region of RegionWorldTriangles (owned or a
// view onto the zone draw pools) -- identical arithmetic, identical results.
bool RayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                           const irr::core::vector3df& rayDir, float rayLen,
                           const RegionTrianglesView& triangles,
                           irr::core::vector3df& hitPoint,
                           irr::core::triangle3df& hitTriangle,
                           float& hitDistSq);

// The live state a door query needs from the door manager.
struct DoorRuntimeState {
    bool known = false;
    bool isOpen = false;
    bool isAnimating = false;
    // T438 (board B423): a hinged door is walked THROUGH while it swings --
    // that is EQ's own behaviour and the reason `isAnimating` suppresses the
    // test. A LIFT is animating for its whole ride, so the same rule made the
    // one door kind you are supposed to stand on the one door kind that never
    // collides. The suppression is now scoped to non-lifts.
    bool isLift = false;
};
using DoorRuntimeStateFn = std::function<DoorRuntimeState(uint8_t)>;

// Ray-test every collidable door in `bspRegion`. `anyHit`/`hitDistSq` are
// in/out so a caller that has already hit something (zone geometry, an object)
// keeps the nearest hit. Returns true if the door set produced the nearest hit.
bool RayTestDoorCollision(const std::map<uint8_t, DoorCollisionData>& doors,
                          const DoorRuntimeStateFn& stateOf,
                          const irr::core::vector3df& rayOrigin,
                          const irr::core::vector3df& rayDir, float rayLen,
                          size_t bspRegion,
                          irr::core::vector3df& hitPoint,
                          irr::core::triangle3df& hitTriangle,
                          float& hitDistSq, bool anyHit,
                          uint8_t* hitDoorId = nullptr);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DOOR_COLLISION_H
