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
    // deltaTime is used for smooth collision zoom recovery
    void setFollowPosition(float eqX, float eqY, float eqZ, float eqHeading, float deltaTime = 0.0f);

    // Get camera position in EQ coordinates
    void getPositionEQ(float& x, float& y, float& z) const;

    // Get camera heading in EQ format (0-512)
    float getHeadingEQ() const;

    // Follow mode zoom control (sets preferred distance)
    void setFollowDistance(float distance);
    float getFollowDistance() const { return preferredFollowDistance_; }
    float getActualFollowDistance() const { return actualFollowDistance_; }
    void adjustFollowDistance(float delta);

    // Collision detection for camera zoom
    void setCollisionManager(irr::scene::ISceneCollisionManager* mgr,
                             irr::scene::ITriangleSelector* selector);

    // Camera orbit control (for third-person mode)
    // The orbit angle determines where the camera is positioned around the player
    // Independent of player heading so player can rotate without camera following
    void setOrbitAngle(float eqHeading);
    float getOrbitAngle() const { return orbitAngle_; }
    void adjustOrbitAngle(float delta);  // delta in EQ heading units (0-512)

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

    // Follow mode distance (zoom) - 0 = first person, >0 = third person
    float preferredFollowDistance_ = 20.0f;  // User's desired distance
    float actualFollowDistance_ = 20.0f;     // Current distance (may be reduced due to collision)
    static constexpr float MIN_FOLLOW_DISTANCE = 0.0f;
    static constexpr float MAX_FOLLOW_DISTANCE = 300.0f;

    // Camera collision zoom
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;
    irr::scene::ITriangleSelector* collisionSelector_ = nullptr;
    static constexpr float CAMERA_COLLISION_OFFSET = 0.5f;   // Distance to stay away from walls
    static constexpr float MIN_COLLISION_DISTANCE = 1.0f;    // Minimum zoom when colliding
    static constexpr float ZOOM_RECOVERY_SPEED = 30.0f;      // Units per second to zoom back out

    // Camera orbit angle in EQ heading units (0-512)
    // This is independent of player heading - player can rotate without camera following
    float orbitAngle_ = 0.0f;
    bool orbitAngleInitialized_ = false;  // Set to true after first setFollowPosition

    // One-time zone-in logging flag
    bool logZoneIn_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CAMERA_CONTROLLER_H
