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

void CameraController::setFollowPosition(float eqX, float eqY, float eqZ, float eqHeading) {
    if (!camera_) return;

    // Calculate zoom factor (0 = zoomed all the way in, 1 = zoomed all the way out)
    float zoomFactor = (followDistance_ - MIN_FOLLOW_DISTANCE) /
                       (MAX_FOLLOW_DISTANCE - MIN_FOLLOW_DISTANCE);

    // Height offset scales with distance for diagonal camera angle
    // At min zoom: just above player's head (~8 units)
    // At max zoom: high above (~200 units for steep diagonal angle)
    float minHeight = 8.0f;
    float maxHeight = 200.0f;
    float heightOffset = minHeight + zoomFactor * (maxHeight - minHeight);

    // Convert EQ heading (0-512) to radians
    // EQ heading: 0=North, increases clockwise
    float headingRad = eqHeading / 512.0f * 2.0f * irr::core::PI;

    // Calculate camera position behind character using current follow distance
    // EQ: (x, y, z) -> Irrlicht: (x, z, y)
    irr::core::vector3df pos(
        eqX - std::sin(headingRad) * followDistance_,
        eqZ + heightOffset,
        eqY - std::cos(headingRad) * followDistance_
    );

    // Look target: at close zoom, look ahead of player (over-the-shoulder view)
    // At far zoom, look at the player
    float lookAheadDistance = (1.0f - zoomFactor) * 80.0f;
    float targetHeight = eqZ + 5.0f;  // Look at roughly head height

    irr::core::vector3df target(
        eqX + std::sin(headingRad) * lookAheadDistance,
        targetHeight,
        eqY + std::cos(headingRad) * lookAheadDistance
    );

    if (logZoneIn_) {
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN] CameraController::setFollowPosition: EQ({:.2f},{:.2f},{:.2f}) heading={:.2f}",
                 eqX, eqY, eqZ, eqHeading);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   headingRad={:.4f} followDist={:.2f} heightOffset={:.2f}",
                 headingRad, followDistance_, heightOffset);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   Camera pos: Irrlicht({:.2f},{:.2f},{:.2f})",
                 pos.X, pos.Y, pos.Z);
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   Camera target: Irrlicht({:.2f},{:.2f},{:.2f})",
                 target.X, target.Y, target.Z);
    }

    camera_->setPosition(pos);
    camera_->setTarget(target);

    // Update internal yaw based on EQ heading
    yaw_ = (eqHeading / 512.0f * 360.0f) - 180.0f;

    if (logZoneIn_) {
        LOG_INFO(MOD_GRAPHICS, "[ZONE-IN]   Calculated yaw: {:.2f} degrees", yaw_);
        logZoneIn_ = false;  // Only log once per zone-in
    }
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
    followDistance_ = std::max(MIN_FOLLOW_DISTANCE, std::min(MAX_FOLLOW_DISTANCE, distance));
}

void CameraController::adjustFollowDistance(float delta) {
    setFollowDistance(followDistance_ + delta);
}

} // namespace Graphics
} // namespace EQT
