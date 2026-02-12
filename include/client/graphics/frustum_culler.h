#ifndef EQT_GRAPHICS_FRUSTUM_CULLER_H
#define EQT_GRAPHICS_FRUSTUM_CULLER_H

#include "common/simd_detect.h"

namespace EQT {
namespace Graphics {

// CPU-side frustum culling in EQ coordinates (Z-up).
// Constructs 6 frustum planes from camera position + direction and tests AABBs/spheres.
// No dependency on Irrlicht - uses raw math only.
//
// The forward direction should come from the actual camera direction vector
// (camera target - camera position), NOT from CameraController yaw/pitch
// which can be stale or represent the player facing direction rather than
// the camera view direction (especially in follow/third-person mode).
class FrustumCuller {
public:
    FrustumCuller() = default;

    // Toggle frustum culling on/off (Ctrl+V)
    void toggle() { enabled_ = !enabled_; invalidate(); }
    void setEnabled(bool enabled) { enabled_ = enabled; invalidate(); }
    bool isEnabled() const { return enabled_; }

    // Rebuild frustum planes from camera state.
    // All coordinates are in EQ space (Z-up).
    // camX/Y/Z: camera position
    // fwdX/Y/Z: camera forward direction (will be normalized internally)
    // fovRadV: vertical field of view in radians
    // aspectRatio: width / height
    // nearDist: near clip distance
    // farDist: far clip distance
    void update(float camX, float camY, float camZ,
                float fwdX, float fwdY, float fwdZ,
                float fovRadV, float aspectRatio,
                float nearDist, float farDist);

    // Test if an axis-aligned bounding box intersects the frustum.
    // Coordinates are in EQ space (Z-up).
    // Returns true if the AABB is at least partially inside.
    bool testAABB(float minX, float minY, float minZ,
                  float maxX, float maxY, float maxZ) const;

    // Test if a sphere intersects the frustum.
    // cx/cy/cz: center in EQ coords, radius: sphere radius.
    // Returns true if the sphere is at least partially inside.
    bool testSphere(float cx, float cy, float cz, float radius) const;

    // Diagnostic: computed directions from last update (EQ coords)
    float getFwdX() const { return diagFwdX_; }
    float getFwdY() const { return diagFwdY_; }
    float getFwdZ() const { return diagFwdZ_; }
    float getRightX() const { return diagRightX_; }
    float getRightY() const { return diagRightY_; }
    float getUpX() const { return diagUpX_; }
    float getUpY() const { return diagUpY_; }
    float getUpZ() const { return diagUpZ_; }
    // Get plane data for diagnostics
    const float* getPlane(int i) const { return planes_[i]; }

private:
    void invalidate() {
        lastCamX_ = -99999.0f; // Force rebuild on next update
    }

    bool enabled_ = true;

    // 6 frustum planes: left, right, top, bottom, near, far
    // Each plane: (nx, ny, nz, d) where nx*x + ny*y + nz*z + d >= 0 means inside
    EQT_ALIGN(16) float planes_[6][4] = {};

    // Cached camera state for dirty checking
    float lastCamX_ = -99999.0f, lastCamY_ = -99999.0f, lastCamZ_ = -99999.0f;
    float lastFwdX_ = -99999.0f, lastFwdY_ = -99999.0f, lastFwdZ_ = -99999.0f;
    float lastFov_ = -99999.0f, lastAspect_ = -99999.0f;
    float lastNear_ = -99999.0f, lastFar_ = -99999.0f;

    // Diagnostic: store computed directions
    float diagFwdX_ = 0, diagFwdY_ = 0, diagFwdZ_ = 0;
    float diagRightX_ = 0, diagRightY_ = 0;
    float diagUpX_ = 0, diagUpY_ = 0, diagUpZ_ = 0;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_FRUSTUM_CULLER_H
