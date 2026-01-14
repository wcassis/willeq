#include "client/graphics/camera_controller.h"
#include "common/logging.h"
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

void CameraController::update(float deltaTime, bool forward, bool backward, bool left, bool right,
                              bool up, bool down, int mouseDeltaX, int mouseDeltaY, bool mouseEnabled) {
    if (!camera_) {
        return;
    }

    // Update rotation from mouse
    if (mouseEnabled) {
        yaw_ += mouseDeltaX * mouseSensitivity_;
        pitch_ -= mouseDeltaY * mouseSensitivity_;

        // Clamp pitch
        pitch_ = std::max(MIN_PITCH, std::min(MAX_PITCH, pitch_));

        // Wrap yaw
        while (yaw_ > 180.0f) yaw_ -= 360.0f;
        while (yaw_ < -180.0f) yaw_ += 360.0f;
    }

    // Calculate direction vectors
    float yawRad = yaw_ * irr::core::DEGTORAD;
    float pitchRad = pitch_ * irr::core::DEGTORAD;

    // Forward direction (in XZ plane)
    irr::core::vector3df forwardDir(
        std::sin(yawRad) * std::cos(pitchRad),
        std::sin(pitchRad),
        std::cos(yawRad) * std::cos(pitchRad)
    );

    // Right direction (perpendicular to forward in XZ plane)
    irr::core::vector3df rightDir(
        std::cos(yawRad),
        0,
        -std::sin(yawRad)
    );

    // Up direction (world up)
    irr::core::vector3df upDir(0, 1, 0);

    // Calculate movement
    irr::core::vector3df movement(0, 0, 0);
    float speed = moveSpeed_ * deltaTime;

    if (forward) {
        movement += forwardDir * speed;
    }
    if (backward) {
        movement -= forwardDir * speed;
    }
    if (right) {
        movement += rightDir * speed;
    }
    if (left) {
        movement -= rightDir * speed;
    }
    if (up) {
        movement += upDir * speed;
    }
    if (down) {
        movement -= upDir * speed;
    }

    // Update camera position
    irr::core::vector3df pos = camera_->getPosition() + movement;
    camera_->setPosition(pos);

    // Update camera target
    irr::core::vector3df target = pos + forwardDir * 100.0f;
    camera_->setTarget(target);
}

void CameraController::setFollowPosition(float eqX, float eqY, float eqZ, float eqHeading, float deltaTime) {
    if (!camera_) return;

    // Debug: Log heading values (throttled)
    static float lastLoggedHeading = 0;
    static int camLogCounter = 0;
    if (std::abs(eqHeading - lastLoggedHeading) > 1.5f && ++camLogCounter % 10 == 0) {
        LOG_DEBUG(MOD_GRAPHICS, "[ROT-CAMERA] setFollowPosition: heading={:.1f} preferredDist={:.1f} actualDist={:.1f}",
                  eqHeading, preferredFollowDistance_, actualFollowDistance_);
        lastLoggedHeading = eqHeading;
    }

    // Track orbit angle for potential future use (mouse orbit control)
    orbitAngle_ = eqHeading;
    orbitAngleInitialized_ = true;

    // Convert EQ heading (0-512) to radians
    // EQ heading: 0=North, increases clockwise
    float headingRad = eqHeading / 512.0f * 2.0f * irr::core::PI;

    // Eye height for first-person (human scale)
    const float eyeHeight = 6.0f;

    irr::core::vector3df pos;
    irr::core::vector3df target;

    // Player eye position in Irrlicht coordinates
    irr::core::vector3df playerEye(eqX, eqZ + eyeHeight, eqY);

    if (preferredFollowDistance_ <= 1.0f) {
        // First-person mode: camera at player's eye level, looking forward
        // No collision check needed in first-person
        actualFollowDistance_ = preferredFollowDistance_;
        pos = playerEye;

        // Look 100 units ahead in the direction the player is facing
        target = irr::core::vector3df(
            eqX + std::sin(headingRad) * 100.0f,
            eqZ + eyeHeight,
            eqY + std::cos(headingRad) * 100.0f
        );
    } else {
        // Third-person mode: camera behind and above player
        // Fixed height offset - camera maintains consistent forward-looking angle regardless of zoom
        float heightAboveEye = 4.0f;  // Fixed height above eye level
        float effectiveHeightOffset = eyeHeight + heightAboveEye;

        // Check for collision between player and camera
        float effectiveDistance = preferredFollowDistance_;

        if (collisionManager_ && collisionSelector_) {
            irr::core::vector3df hitPoint;
            irr::core::triangle3df hitTriangle;
            irr::scene::ISceneNode* hitNode = nullptr;

            // First, check for ceiling above player's head
            // Cast a ray upward from player eye to intended camera height
            irr::core::vector3df ceilingCheckStart = playerEye;
            irr::core::vector3df ceilingCheckEnd(
                eqX,
                eqZ + effectiveHeightOffset + 1.0f,  // Check slightly above camera height
                eqY
            );
            irr::core::line3df ceilingRay(ceilingCheckStart, ceilingCheckEnd);

            if (collisionManager_->getCollisionPoint(ceilingRay, collisionSelector_,
                                                     hitPoint, hitTriangle, hitNode)) {
                // Ceiling detected - limit camera height to below ceiling
                float ceilingHeight = hitPoint.Y - CAMERA_COLLISION_OFFSET;
                float maxHeightOffset = ceilingHeight - eqZ;
                if (maxHeightOffset < effectiveHeightOffset) {
                    effectiveHeightOffset = std::max(eyeHeight * 0.5f, maxHeightOffset);
                    // Log ceiling detection (throttled)
                    static int ceilingLogCount = 0;
                    if (++ceilingLogCount % 60 == 1) {
                        LOG_DEBUG(MOD_GRAPHICS, "Camera ceiling: limiting height to {:.1f} (ceiling at {:.1f})",
                                  effectiveHeightOffset, ceilingHeight);
                    }
                }
            }

            // Calculate ideal camera position at preferred distance with adjusted height
            irr::core::vector3df idealPos(
                eqX - std::sin(headingRad) * preferredFollowDistance_,
                eqZ + effectiveHeightOffset,
                eqY - std::cos(headingRad) * preferredFollowDistance_
            );

            // Now check for wall/object collision along the path to camera
            hitNode = nullptr;
            irr::core::line3df ray(playerEye, idealPos);

            if (collisionManager_->getCollisionPoint(ray, collisionSelector_,
                                                     hitPoint, hitTriangle, hitNode)) {
                // Calculate distance from player eye to hit point
                float hitDist = playerEye.getDistanceFrom(hitPoint);
                // Back off from the wall slightly
                float collisionDist = hitDist - CAMERA_COLLISION_OFFSET;
                collisionDist = std::max(MIN_COLLISION_DISTANCE, collisionDist);

                // Immediately zoom in if obstructed
                if (collisionDist < actualFollowDistance_) {
                    // Log significant zoom-ins (throttled)
                    static int collisionLogCount = 0;
                    if (++collisionLogCount % 30 == 1) {
                        LOG_DEBUG(MOD_GRAPHICS, "Camera collision: zooming from {:.1f} to {:.1f} (hit at {:.1f})",
                                  actualFollowDistance_, collisionDist, hitDist);
                    }
                    actualFollowDistance_ = collisionDist;
                }
                effectiveDistance = actualFollowDistance_;
            } else {
                // No obstruction - smoothly restore to preferred distance
                if (actualFollowDistance_ < preferredFollowDistance_ && deltaTime > 0.0f) {
                    actualFollowDistance_ = std::min(
                        actualFollowDistance_ + ZOOM_RECOVERY_SPEED * deltaTime,
                        preferredFollowDistance_);
                }
                effectiveDistance = actualFollowDistance_;
            }
        } else {
            // No collision manager - use preferred distance directly
            actualFollowDistance_ = preferredFollowDistance_;
            effectiveDistance = preferredFollowDistance_;
        }

        // Calculate final camera position using effective distance and height
        pos = irr::core::vector3df(
            eqX - std::sin(headingRad) * effectiveDistance,
            eqZ + effectiveHeightOffset,
            eqY - std::cos(headingRad) * effectiveDistance
        );

        // Look slightly ahead of the player, not directly at them
        // Scale look-ahead with effective distance for consistent feel
        float lookAheadDist = std::min(effectiveDistance * 0.5f, 20.0f);
        target = irr::core::vector3df(
            eqX + std::sin(headingRad) * lookAheadDist,
            eqZ + eyeHeight,
            eqY + std::cos(headingRad) * lookAheadDist
        );
    }

    if (logZoneIn_) {
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] CameraController::setFollowPosition: EQ({:.2f},{:.2f},{:.2f}) heading={:.2f}",
                 eqX, eqY, eqZ, eqHeading);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   preferredDist={:.2f} actualDist={:.2f} firstPerson={}",
                 preferredFollowDistance_, actualFollowDistance_, preferredFollowDistance_ <= 1.0f);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   Camera pos: Irrlicht({:.2f},{:.2f},{:.2f})",
                 pos.X, pos.Y, pos.Z);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   Camera target: Irrlicht({:.2f},{:.2f},{:.2f})",
                 target.X, target.Y, target.Z);
        logZoneIn_ = false;  // Only log once per zone-in
    }

    camera_->setPosition(pos);
    camera_->setTarget(target);

    // Update internal yaw based on EQ heading
    yaw_ = (eqHeading / 512.0f * 360.0f) - 180.0f;
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

void CameraController::setFollowDistance(float distance) {
    preferredFollowDistance_ = std::max(MIN_FOLLOW_DISTANCE, std::min(MAX_FOLLOW_DISTANCE, distance));
    // Also update actual if no collision manager (or if setting smaller)
    if (!collisionManager_ || distance < actualFollowDistance_) {
        actualFollowDistance_ = preferredFollowDistance_;
    }
}

void CameraController::adjustFollowDistance(float delta) {
    setFollowDistance(preferredFollowDistance_ + delta);
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
