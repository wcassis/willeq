// door_collision.cpp -- see include/client/graphics/door_collision.h.
// T435, board B425.
#include "client/graphics/door_collision.h"

#include <cmath>

namespace EQT {
namespace Graphics {

bool DoorOpentypeHasCollision(uint8_t opentype) {
    // The table, with T433's evidence for every row that says NO. Everything
    // not listed collides -- including opentype 0/5/56 hinged doors and the
    // opentype 59 lifts, whose platform is a standing surface wherever it is.
    switch (opentype) {
        // Invisible types. DoorManager::createDoor()/registerDoor() return
        // before any node exists (INVISIBLE_OPENTYPES = {50, 53, 54}), so these
        // normally never reach here at all; listed so that if board B427 ever
        // makes them real, they do not silently become walls.
        case 50:
        case 53:
        case 54:
            return false;

        // Teleport doors. eqemu zone/doors.cpp:
        //   if (EQ::ValueWithin(m_open_type, 57, 58) && HasDestinationZone()) {
        //           ... sender->MovePC(...)
        // The destination push is the SERVER's; the client's only job is to let
        // the player walk into the thing that triggers it.
        case 57:
        case 58:
            return false;

        // Hazards. T433's era census: sebilis SBDOOR103 x14 and bazaar
        // BAZIGATE302/303 x4 at opentype 71, soldunga SPEARDOWN x2 at opentype
        // 120 (the only era rows at 120 in the game). The damage arrives as an
        // ordinary OP_Damage from the server -- turning a trap into a wall would
        // be a client-invented obstacle.
        case 71:
        case 120:
            return false;

        // Spinning objects (DoorManager: 100 = Y-spin lamps/gems, 105 = Z-spin
        // blades, both at SPIN_SPEED and never latching). They have no settled
        // pose to bake, so there is nothing honest to put in the two triangle
        // sets.
        case 100:
        case 105:
            return false;

        default:
            return true;
    }
}

irr::core::matrix4 DoorPoseTransform(const irr::core::vector3df& pivotPosition,
                                     float heading512) {
    // Same arithmetic as DoorManager::createDoor()/buildDoorMesh()/update():
    // heading is EQ 512-format, and the door mesh faces perpendicular to it.
    const float irrRotation = -heading512 * 360.0f / 512.0f + 90.0f;
    irr::core::matrix4 xf;
    xf.setRotationDegrees(irr::core::vector3df(0.0f, irrRotation, 0.0f));
    xf.setTranslation(pivotPosition);
    return xf;
}

void AppendMeshWorldTriangles(irr::scene::IMesh* mesh,
                              const irr::core::matrix4& xf,
                              std::vector<irr::core::triangle3df>& out) {
    if (!mesh) return;

    for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
        irr::scene::IMeshBuffer* mb = mesh->getMeshBuffer(b);
        if (!mb) continue;
        const irr::u32 idxCount = mb->getIndexCount();
        if (idxCount < 3) continue;

        // Door meshes come from ZoneMeshBuilder as 16-bit buffers, but honour
        // the buffer's declared index type rather than assuming it.
        const bool wide = (mb->getIndexType() == irr::video::EIT_32BIT);
        const irr::u16* idx16 = mb->getIndices();
        const irr::u32* idx32 = reinterpret_cast<const irr::u32*>(mb->getIndices());
        auto index = [&](irr::u32 i) -> irr::u32 {
            return wide ? idx32[i] : static_cast<irr::u32>(idx16[i]);
        };

        for (irr::u32 i = 0; i + 2 < idxCount; i += 3) {
            irr::core::vector3df a = mb->getPosition(index(i));
            irr::core::vector3df b2 = mb->getPosition(index(i + 1));
            irr::core::vector3df c = mb->getPosition(index(i + 2));
            xf.transformVect(a);
            xf.transformVect(b2);
            xf.transformVect(c);
            out.emplace_back(a, b2, c);
        }
    }
}

bool RayIntersectTriangles(const irr::core::vector3df& rayOrigin,
                           const irr::core::vector3df& rayDir, float rayLen,
                           const std::vector<irr::core::triangle3df>& triangles,
                           irr::core::vector3df& hitPoint,
                           irr::core::triangle3df& hitTriangle,
                           float& hitDistSq) {
    // Moller-Trumbore ray-triangle intersection -- no matrix math.
    constexpr float EPSILON = 0.000001f;
    bool anyHit = false;
    float closestT = rayLen + 1.0f;

    for (const auto& tri : triangles) {
        const irr::core::vector3df& v0 = tri.pointA;
        const irr::core::vector3df edge1 = tri.pointB - v0;
        const irr::core::vector3df edge2 = tri.pointC - v0;

        const irr::core::vector3df h = rayDir.crossProduct(edge2);
        float a = edge1.dotProduct(h);
        if (a > -EPSILON && a < EPSILON) continue;  // Parallel

        float f = 1.0f / a;
        const irr::core::vector3df s = rayOrigin - v0;
        float u = f * s.dotProduct(h);
        if (u < 0.0f || u > 1.0f) continue;

        const irr::core::vector3df q = s.crossProduct(edge1);
        float v = f * rayDir.dotProduct(q);
        if (v < 0.0f || u + v > 1.0f) continue;

        float t = f * edge2.dotProduct(q);
        if (t > EPSILON && t < closestT) {
            closestT = t;
            hitPoint = rayOrigin + rayDir * t;
            hitTriangle = tri;
            anyHit = true;
        }
    }

    if (anyHit) {
        hitDistSq = hitPoint.getDistanceFromSQ(rayOrigin);
    }
    return anyHit;
}

bool RayTestDoorCollision(const std::map<uint8_t, DoorCollisionData>& doors,
                          const DoorRuntimeStateFn& stateOf,
                          const irr::core::vector3df& rayOrigin,
                          const irr::core::vector3df& rayDir, float rayLen,
                          size_t bspRegion,
                          irr::core::vector3df& hitPoint,
                          irr::core::triangle3df& hitTriangle,
                          float& hitDistSq, bool anyHit) {
    for (const auto& [doorId, doorData] : doors) {
        if (!doorData.hasCollision) continue;
        if (doorData.bspRegion != bspRegion) continue;

        const DoorRuntimeState state = stateOf ? stateOf(doorId) : DoorRuntimeState{};
        if (!state.known) continue;
        // Walk-through during animation (original EQ behaviour). Board B423
        // owns undoing this for lifts, which are animating for their whole
        // ride -- that needs the oracle measurement B421 is gated on.
        if (state.isAnimating) continue;

        const auto& tris = state.isOpen ? doorData.openTriangles : doorData.closedTriangles;
        if (tris.empty()) continue;

        irr::core::vector3df hp;
        irr::core::triangle3df ht;
        float distSq = 0.0f;
        if (RayIntersectTriangles(rayOrigin, rayDir, rayLen, tris, hp, ht, distSq)) {
            if (!anyHit || distSq < hitDistSq) {
                hitPoint = hp;
                hitTriangle = ht;
                hitDistSq = distSq;
                anyHit = true;
            }
        }
    }
    return anyHit;
}

} // namespace Graphics
} // namespace EQT
