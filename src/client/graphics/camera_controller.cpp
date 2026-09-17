#include "client/graphics/camera_controller.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

CameraController::CameraController(irr::scene::ICameraSceneNode* camera)
    : camera_(camera) {
    if (camera_) {
        // Initialize orientation from current camera direction
        irr::core::vector3df dir = camera_->getTarget() - camera_->getPosition();
        dir.normalize();

        yaw_ = std::atan2(dir.X, dir.Z) * 180.0f / irr::core::PI;
        pitch_ = std::asin(dir.Y) * 180.0f / irr::core::PI;
    }
}

void CameraController::setOrientation(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = std::max(MIN_PITCH, std::min(MAX_PITCH, pitch));
}

// T474 (board B650): setFollowPosition() -- the pre-T474 Chase camera -- is
// gone; see camera_controller.h. Its T464 ceiling clamp lives on in
// placeChaseCamera() below and its wall test in placeRigCamera().

float CameraController::placeRigCamera(const CameraRigPose& pose) {
    const float dx = pose.camX - pose.targetX;
    const float dy = pose.camY - pose.targetY;
    const float dz = pose.camZ - pose.targetZ;
    const float idealDist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!camera_) {
        lastPlacedDistance_ = idealDist;
        return idealDist;
    }

    // Irrlicht is Y-up: EQ (x, y, z) -> (x, z, y).
    const irr::core::vector3df target(pose.targetX, pose.targetZ, pose.targetY);
    float scale = 1.0f;

    // T464: the ray starts at the look-at point, so the PVS origin is ITS region.
    const size_t originRegion =
        bspTree_ ? bspTree_->findRegionIndexForPoint(pose.targetX, pose.targetY, pose.targetZ)
                 : SIZE_MAX;

    if (idealDist > 0.0f && bspTree_ && originRegion != SIZE_MAX) {
        if (!isBspPointVisible(originRegion, pose.camX, pose.camY, pose.camZ)) {
            float hit = bspFindCollisionDistance(originRegion,
                                                 pose.targetX, pose.targetY, pose.targetZ,
                                                 pose.camX, pose.camY, pose.camZ);
            hit = std::max(MIN_COLLISION_DISTANCE, hit - CAMERA_COLLISION_OFFSET);
            scale = std::min(1.0f, hit / idealDist);
        }
    } else if (idealDist > 0.0f && collisionManager_ && collisionSelector_) {
        const irr::core::vector3df ideal(pose.camX, pose.camZ, pose.camY);
        irr::core::vector3df hitPoint;
        irr::core::triangle3df hitTriangle;
        irr::scene::ISceneNode* hitNode = nullptr;
        irr::core::line3df ray(target, ideal);
        if (collisionManager_->getCollisionPoint(ray, collisionSelector_,
                                                 hitPoint, hitTriangle, hitNode)) {
            const float hit = std::max(MIN_COLLISION_DISTANCE,
                                       target.getDistanceFrom(hitPoint) - CAMERA_COLLISION_OFFSET);
            scale = std::min(1.0f, hit / idealDist);
        }
    }

    camera_->setPosition(irr::core::vector3df(pose.targetX + dx * scale,
                                              pose.targetZ + dz * scale,
                                              pose.targetY + dy * scale));
    camera_->setTarget(target);
    lastPlacedDistance_ = idealDist * scale;
    return lastPlacedDistance_;
}

float CameraController::placeChaseCamera(const CameraRigPose& idealPose, float eyeZ) {
    CameraRigPose pose = idealPose;
    const float topOfPose = std::max(pose.camZ, pose.targetZ);

    // T464 (boards B507, B508), kept by T474: the ceiling is the top of the box
    // of the region the player's EYE is in -- looked up here from the pose,
    // never fed back from the camera's own region -- and read on the VERTICAL
    // axis (MaxEdge.Z; the boxes are in the EQ Z-up frame). A top at or below
    // the eye is not over the player's head, so it is no ceiling.
    float ceiling = topOfPose;
    bool haveCeiling = false;
    const size_t eyeRegion =
        bspTree_ ? bspTree_->findRegionIndexForPoint(pose.targetX, pose.targetY, eyeZ) : SIZE_MAX;
    if (bspTree_ && eyeRegion != SIZE_MAX) {
        if (regionBoundsMap_) {
            auto it = regionBoundsMap_->find(eyeRegion);
            if (it != regionBoundsMap_->end() && it->second.MaxEdge.Z > eyeZ) {
                ceiling = it->second.MaxEdge.Z - CAMERA_COLLISION_OFFSET;
                haveCeiling = true;
            }
        }
    } else if (collisionManager_ && collisionSelector_) {
        // Legacy fallback: a vertical ray from the eye to just above the pose.
        const irr::core::vector3df eye(pose.targetX, eyeZ, pose.targetY);
        const irr::core::vector3df up(pose.targetX, topOfPose + 1.0f, pose.targetY);
        irr::core::vector3df hitPoint;
        irr::core::triangle3df hitTriangle;
        irr::scene::ISceneNode* hitNode = nullptr;
        if (collisionManager_->getCollisionPoint(irr::core::line3df(eye, up), collisionSelector_,
                                                 hitPoint, hitTriangle, hitNode)) {
            ceiling = hitPoint.Y - CAMERA_COLLISION_OFFSET;
            haveCeiling = true;
        }
    }

    if (haveCeiling && topOfPose > ceiling) {
        // Never pushed below the eye by the clamp (the old body's floor was a
        // fixed half eye height above the node origin; the T474 pose has an eye).
        const float limit = std::max(ceiling, eyeZ);
        pose.camZ = std::min(pose.camZ, limit);
        pose.targetZ = std::min(pose.targetZ, limit);
        static int ceilingLogCount = 0;
        if (++ceilingLogCount % 60 == 1) {
            LOG_DEBUG(MOD_GRAPHICS, "Chase camera ceiling: limiting height to {:.2f} (eye {:.2f})",
                      limit, eyeZ);
        }
    }
    return placeRigCamera(pose);
}

void CameraController::getPositionEQ(float& x, float& y, float& z) const {
    if (!camera_) {
        x = y = z = 0;
        return;
    }

    // Convert Irrlicht (Y-up) to EQ (Z-up)
    // Irrlicht: (x, y, z) -> EQ: (x, z, y)
    irr::core::vector3df pos = camera_->getPosition();
    x = pos.X;
    y = pos.Z;
    z = pos.Y;
}

float CameraController::getHeadingEQ() const {
    // Convert yaw to EQ heading (0-512)
    float heading = (yaw_ + 180.0f) / 360.0f * 512.0f;
    while (heading < 0) heading += 512.0f;
    while (heading >= 512.0f) heading -= 512.0f;
    return heading;
}

void CameraController::setBspCollision(const BspTree* bspTree,
                                       const std::map<size_t, irr::core::aabbox3df>* regionBounds) {
    bspTree_ = bspTree;
    regionBoundsMap_ = regionBounds;
    // Clear legacy collision when BSP is set
    collisionManager_ = nullptr;
    collisionSelector_ = nullptr;
    LOG_DEBUG(MOD_GRAPHICS, "Camera BSP collision set (bspTree={}, regionBounds={})",
              bspTree != nullptr, regionBounds != nullptr);
}

bool CameraController::isBspPointVisible(size_t originRegion, float eqX, float eqY, float eqZ) const {
    if (!bspTree_ || originRegion == SIZE_MAX) return true;

    size_t pointRegion = bspTree_->findRegionIndexForPoint(eqX, eqY, eqZ);

    // Outside BSP entirely — not visible (behind a wall or above ceiling)
    if (pointRegion == SIZE_MAX) return false;

    // Same region as the ray's origin — always visible
    if (pointRegion == originRegion) return true;

    // Check PVS: is pointRegion visible from the origin region?
    if (originRegion < bspTree_->regions.size()) {
        const auto playerPvs = bspTree_->regions[originRegion]->pvs();
        if (pointRegion < playerPvs.size()) {
            return playerPvs[pointRegion];
        }
    }

    // No PVS data — conservatively treat as not visible
    return false;
}

float CameraController::bspFindCollisionDistance(size_t originRegion,
                                                  float startEqX, float startEqY, float startEqZ,
                                                  float endEqX, float endEqY, float endEqZ) const {
    // Binary search along the ray from start (the look-at point) to end (ideal camera pos)
    // Find the furthest point that is still in a PVS-visible region
    float lo = 0.0f, hi = 1.0f;

    // 8 iterations gives ~1/256 precision along the ray (sub-unit accuracy for typical distances)
    for (int i = 0; i < 8; ++i) {
        float mid = (lo + hi) * 0.5f;
        float mx = startEqX + (endEqX - startEqX) * mid;
        float my = startEqY + (endEqY - startEqY) * mid;
        float mz = startEqZ + (endEqZ - startEqZ) * mid;

        if (isBspPointVisible(originRegion, mx, my, mz)) {
            lo = mid;  // midpoint is visible, search further out
        } else {
            hi = mid;  // midpoint is blocked, search closer
        }
    }

    // Return the distance from start to the collision point
    float dx = endEqX - startEqX;
    float dy = endEqY - startEqY;
    float dz = endEqZ - startEqZ;
    float totalDist = std::sqrt(dx * dx + dy * dy + dz * dz);
    return lo * totalDist;
}

void CameraController::setCollisionManager(irr::scene::ISceneCollisionManager* mgr,
                                           irr::scene::ITriangleSelector* selector) {
    collisionManager_ = mgr;
    collisionSelector_ = selector;
    LOG_DEBUG(MOD_GRAPHICS, "Camera collision manager set (mgr={}, selector={})",
              mgr != nullptr, selector != nullptr);
}

void CameraController::setOrbitAngle(float eqHeading) {
    orbitAngle_ = eqHeading;
    // Normalize to 0-512 range
    while (orbitAngle_ < 0) orbitAngle_ += 512.0f;
    while (orbitAngle_ >= 512.0f) orbitAngle_ -= 512.0f;
    orbitAngleInitialized_ = true;
}

void CameraController::adjustOrbitAngle(float delta) {
    orbitAngle_ += delta;
    // Normalize to 0-512 range
    while (orbitAngle_ < 0) orbitAngle_ += 512.0f;
    while (orbitAngle_ >= 512.0f) orbitAngle_ -= 512.0f;
}

} // namespace Graphics
} // namespace EQT
