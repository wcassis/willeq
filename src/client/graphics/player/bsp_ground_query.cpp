// bsp_ground_query.cpp -- B1067 (GitLab #387). The keyboard-movement ground
// ray, device-free. See the header for the defect.

#include "client/graphics/player/bsp_ground_query.h"
#include "client/zone_bsp.h"

#include <limits>

namespace EQT {
namespace Graphics {

namespace {

// Casts straight down from `origin` by `length` through every region the
// segment crosses and keeps the nearest hit. Region lookups use EQ order
// (x, y, z) = Irrlicht (X, Z, Y).
bool castDown(const BspTree& bsp, const RegionRayTest& test,
              const irr::core::vector3df& origin, float length, float& hitY) {
    const irr::core::vector3df dir(0.0f, -1.0f, 0.0f);
    std::vector<size_t> regions;
    bsp.collectRegionsAlongSegment(origin.X, origin.Z, origin.Y,
                                   origin.X, origin.Z, origin.Y - length, regions);
    bool any = false;
    float best = std::numeric_limits<float>::max();
    for (size_t region : regions) {
        irr::core::vector3df hp;
        float distSq = 0.0f;
        if (test(region, origin, dir, length, hp, distSq) && distSq < best) {
            best = distSq;
            hitY = hp.Y;
            any = true;
        }
    }
    return any;
}

}  // namespace

GroundZ FindBspGround(const BspTree& bsp, const RegionRayTest& test,
                      float x, float y, float currentZ, float modelYOffset,
                      float maxStepUp) {
    const float feetZ = currentZ - modelYOffset;
    const float headZ = currentZ + modelYOffset;
    const float maxStepDown = maxStepUp * 2.0f;

    // Phase 1: the short ray near the current level. It only RETURNS on a
    // floor -- a miss or a ceiling falls through to the full ray (T440).
    float hitY = 0.0f;
    const bool nearHit = castDown(bsp, test, irr::core::vector3df(x, feetZ + maxStepUp, y),
                                  maxStepUp + maxStepDown, hitY);
    const GroundZ nearGround = ClassifyBspGroundRay(nearHit, hitY, feetZ, maxStepUp);
    if (nearGround.hasFloor()) return nearGround;

    // Phase 2: from above the head to far below the feet.
    hitY = 0.0f;
    const float top = headZ + 2.0f;
    const bool hit = castDown(bsp, test, irr::core::vector3df(x, top, y),
                              top - (feetZ - 500.0f), hitY);
    return ClassifyBspGroundRay(hit, hitY, feetZ, maxStepUp);
}

}  // namespace Graphics
}  // namespace EQT
