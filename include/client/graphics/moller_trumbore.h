// moller_trumbore.h -- THE Moller-Trumbore ray/triangle step every collision
// set runs (B909 region triangles, door triangles, B909b object triangles).
//
// One definition, so every set does the SAME arithmetic on the same floats and
// a set that stores its triangles differently still gives bit-identical hits
// (test_region_world_triangles, test_object_collision_set). Moved here from an
// anonymous namespace in door_collision.cpp by B909b, unchanged.
#ifndef EQT_GRAPHICS_MOLLER_TRUMBORE_H
#define EQT_GRAPHICS_MOLLER_TRUMBORE_H

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

// One step: tests (v0, v1, v2); on a hit nearer than `closestT` (and past
// EPSILON) updates closestT, hitPoint, hitTriangle and sets anyHit.
inline void MollerTrumboreStep(const irr::core::vector3df& rayOrigin,
                               const irr::core::vector3df& rayDir,
                               const irr::core::vector3df& v0,
                               const irr::core::vector3df& v1,
                               const irr::core::vector3df& v2,
                               float& closestT, irr::core::vector3df& hitPoint,
                               irr::core::triangle3df& hitTriangle, bool& anyHit) {
    constexpr float EPSILON = 0.000001f;
    const irr::core::vector3df edge1 = v1 - v0;
    const irr::core::vector3df edge2 = v2 - v0;

    const irr::core::vector3df h = rayDir.crossProduct(edge2);
    float a = edge1.dotProduct(h);
    if (a > -EPSILON && a < EPSILON) return;  // Parallel

    float f = 1.0f / a;
    const irr::core::vector3df s = rayOrigin - v0;
    float u = f * s.dotProduct(h);
    if (u < 0.0f || u > 1.0f) return;

    const irr::core::vector3df q = s.crossProduct(edge1);
    float v = f * rayDir.dotProduct(q);
    if (v < 0.0f || u + v > 1.0f) return;

    float t = f * edge2.dotProduct(q);
    if (t > EPSILON && t < closestT) {
        closestT = t;
        hitPoint = rayOrigin + rayDir * t;
        hitTriangle = irr::core::triangle3df(v0, v1, v2);
        anyHit = true;
    }
}

}  // namespace Graphics
}  // namespace EQT

#endif
