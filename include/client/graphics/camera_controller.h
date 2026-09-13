#ifndef EQT_GRAPHICS_CAMERA_CONTROLLER_H
#define EQT_GRAPHICS_CAMERA_CONTROLLER_H

#include <irrlicht.h>
#include "client/graphics/player/camera_rig.h"  // T461: CameraRigPose
#include <cstdint>
#include <map>
#include <vector>

namespace EQT {
namespace Graphics {

struct BspTree;  // forward declare

// Camera controller for FPS-style movement with EQ coordinate system
class CameraController {
public:
    CameraController(irr::scene::ICameraSceneNode* camera);
    ~CameraController() = default;

    // T461 DELETED update(), the free-fly camera. T319 kept it because it was
    // the only body of CameraMode::Free; T461 matched the camera set to the
    // official client's (player/camera_mode.h) and Free is not one of its
    // cameras, so the mode and its never-called body went together. (T319's
    // note also said `--camera free` selected it on the command line; no such
    // option exists in the tree.) The fly-camera settings and the yaw/pitch/
    // orbit-angle accessors below are residue with no reader -- board B533.

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

    // T461: place a rig camera (Overhead, User Defined 1/2, Tether). `pose` is
    // EQ client coordinates from computeCameraRigPose(). When the ideal
    // position is behind a wall the camera is pulled in along the look-at ->
    // camera ray, by the same two collision systems setFollowPosition() uses
    // (BSP PVS, else the legacy triangle selector); unlike Chase there is no
    // zoom-recovery easing, because a rig is placed exactly every frame.
    // Returns the look-at -> camera distance actually used.
    float placeRigCamera(const CameraRigPose& pose);

    // Get camera position in EQ coordinates
    void getPositionEQ(float& x, float& y, float& z) const;

    // Get camera heading in EQ format (0-512)
    float getHeadingEQ() const;

    // Follow mode zoom control (sets preferred distance)
    void setFollowDistance(float distance);
    float getFollowDistance() const { return preferredFollowDistance_; }
    float getActualFollowDistance() const { return actualFollowDistance_; }
    void adjustFollowDistance(float delta);

    // BSP-based collision detection for camera zoom (replaces triangle selectors)
    // regionBounds: per-region AABB in the EQ Z-UP frame (Z is vertical), indexed
    // by BSP region index -- the renderer's regionBoundingBoxes, built by
    // ZoneLoadComponent::loadZoneStep2_BSP().
    // T464 (B507/B508): there is no setBspPlayerRegion() any more. The camera
    // looks up the region of each ray's origin (the player's eye, a rig's
    // look-at point) itself, from the pose it is handed; it used to be fed the
    // SimulationWorker's CAMERA region, a frame late and only on a change.
    void setBspCollision(const BspTree* bspTree,
                         const std::map<size_t, irr::core::aabbox3df>* regionBounds);

    // Legacy triangle selector collision (fallback for zones without BSP)
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

    // BSP-based camera collision
    const BspTree* bspTree_ = nullptr;
    const std::map<size_t, irr::core::aabbox3df>* regionBoundsMap_ = nullptr;

    // Legacy collision (fallback)
    irr::scene::ISceneCollisionManager* collisionManager_ = nullptr;
    irr::scene::ITriangleSelector* collisionSelector_ = nullptr;

    // BSP collision: check if a point is in a region PVS-visible from originRegion
    bool isBspPointVisible(size_t originRegion, float eqX, float eqY, float eqZ) const;
    // BSP collision: binary search along ray to find collision point
    float bspFindCollisionDistance(size_t originRegion,
                                  float startEqX, float startEqY, float startEqZ,
                                  float endEqX, float endEqY, float endEqZ) const;

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
