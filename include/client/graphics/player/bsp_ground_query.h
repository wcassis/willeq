#ifndef EQT_GRAPHICS_PLAYER_BSP_GROUND_QUERY_H
#define EQT_GRAPHICS_PLAYER_BSP_GROUND_QUERY_H

// B1067 (GitLab #387) -- the keyboard-movement ground query, device-free.
//
// It used to live inline in PlayerCameraComponent::findGroundZBspFiltered(),
// a body that needs an Irrlicht device and a loaded zone, so it had no unit
// test. (The step decision that reads its answer stays inline in
// resolveIrrlichtClearPath(): its statement order is pinned by
// test_movement_order's golden; test_bsp_ground_step pins the rule's text.)
//
// B1067 is what that cost: the ground ray was tested ONLY against the
// triangles of the BSP leaf holding the player's CENTRE point. A region's triangles are the polygons of its own
// leaf, and a floor is very often the bottom face of a thin leaf of its own
// (qeynos2's street curb at Y 391.75: the z = -1 floor is in region 1549,
// which spans only a sliver above the floor, while a standing player's centre
// at z 1.37 is in region 1083 with ten wall triangles and no floor). The ray
// met nothing, the answer was NoFloor, and resolveIrrlichtClearPath() refused
// the move: the player stopped dead -- on flat ground beside a step, and at
// every small step up or down where the leaf layout did the same. The ray
// now tests every leaf it actually passes through
// (BspTree::collectRegionsAlongSegment).

#include "client/graphics/player/ground_query.h"

#include <irrlicht.h>
#include <cstddef>
#include <functional>
#include <vector>

namespace EQT {
namespace Graphics {

struct BspTree;

// Tests one BSP region's geometry (zone triangles, placed objects, doors)
// against a ray. Returns true on a hit and, when it hits, writes the hit
// point and its squared distance from the ray origin. The renderer's
// implementation is RegionWorldTriangles + rayTestDoorsAndObjects().
using RegionRayTest = std::function<bool(size_t region,
                                         const irr::core::vector3df& origin,
                                         const irr::core::vector3df& dir,
                                         float length,
                                         irr::core::vector3df& hitPoint,
                                         float& hitDistSq)>;

// The two-phase downward ground ray, over every region it crosses
// (Irrlicht Y-up: x = EQ x, Y = EQ z, Z = EQ y).
//   phase 1: from feet + maxStepUp down by maxStepUp + 2 x maxStepUp; a floor
//            there (ClassifyBspGroundRay) is the answer;
//   phase 2: from head + 2 down to feet - 500, classified the same way.
// feet = currentZ - modelYOffset, head = currentZ + modelYOffset. The boat
// deck check stays with the caller (it is not region geometry).
GroundZ FindBspGround(const BspTree& bsp, const RegionRayTest& test,
                      float x, float y, float currentZ, float modelYOffset,
                      float maxStepUp);

}  // namespace Graphics
}  // namespace EQT

#endif
