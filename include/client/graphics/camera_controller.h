#ifndef EQT_GRAPHICS_CAMERA_CONTROLLER_H
#define EQT_GRAPHICS_CAMERA_CONTROLLER_H

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

// Camera controller for FPS-style movement with EQ coordinate system
class CameraController {
public:
    CameraController(irr::scene::ICameraSceneNode* camera);
    ~CameraController() = default;

    // Update camera based on input
    void update(float deltaTime, bool forward, bool backward, bool left, bool right,
                bool up, bool down, int mouseDeltaX, int mouseDeltaY, bool mouseEnabled);

    // Set movement and rotation speeds
    void setMoveSpeed(float speed) { moveSpeed_ = speed; }
    void setRotateSpeed(float speed) { rotateSpeed_ = speed; }
    void setMouseSensitivity(float sens) { mouseSensitivity_ = sens; }

    // Get current settings
    float getMoveSpeed() const { return moveSpeed_; }
    float getRotateSpeed() const { return rotateSpeed_; }

    // Camera orientation
    float getYaw() const { return yaw_; }
    float getPitch() const { return pitch_; }
    void setOrientation(float yaw, float pitch);

    // Set camera to follow an entity position
    // Uses EQ coordinates (Z-up), converts internally
    void setFollowPosition(float eqX, float eqY, float eqZ, float eqHeading);

    // Get camera position in EQ coordinates
    void getPositionEQ(float& x, float& y, float& z) const;

    // Get camera heading in EQ format (0-512)
    float getHeadingEQ() const;

    // Follow mode zoom control
    void setFollowDistance(float distance);
    float getFollowDistance() const { return followDistance_; }
    void adjustFollowDistance(float delta);

    // Enable one-time zone-in logging (for debugging)
    void enableZoneInLogging() { logZoneIn_ = true; }

private:
    irr::scene::ICameraSceneNode* camera_;

    float moveSpeed_ = 500.0f;
    float rotateSpeed_ = 100.0f;
    float mouseSensitivity_ = 0.2f;

    float yaw_ = 0.0f;    // Horizontal rotation
    float pitch_ = 0.0f;  // Vertical rotation

    static constexpr float MIN_PITCH = -89.0f;
    static constexpr float MAX_PITCH = 89.0f;

    // Follow mode distance (zoom)
    float followDistance_ = 150.0f;
    static constexpr float MIN_FOLLOW_DISTANCE = 10.0f;
    static constexpr float MAX_FOLLOW_DISTANCE = 500.0f;

    // One-time zone-in logging flag
    bool logZoneIn_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CAMERA_CONTROLLER_H
