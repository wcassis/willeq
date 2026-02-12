#include "client/graphics/frustum_culler.h"
#include <cmath>

namespace EQT {
namespace Graphics {

// Small angular margin in degrees to prevent pop-in at frustum edges
static constexpr float FRUSTUM_MARGIN_DEG = 2.0f;
static constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;

void FrustumCuller::update(float camX, float camY, float camZ,
                            float fwdX, float fwdY, float fwdZ,
                            float fovRadV, float aspectRatio,
                            float nearDist, float farDist) {
    if (!enabled_) return;

    // Dirty check - skip rebuild if nothing changed
    if (camX == lastCamX_ && camY == lastCamY_ && camZ == lastCamZ_ &&
        fwdX == lastFwdX_ && fwdY == lastFwdY_ && fwdZ == lastFwdZ_ &&
        fovRadV == lastFov_ && aspectRatio == lastAspect_ &&
        nearDist == lastNear_ && farDist == lastFar_) {
        return;
    }

    lastCamX_ = camX; lastCamY_ = camY; lastCamZ_ = camZ;
    lastFwdX_ = fwdX; lastFwdY_ = fwdY; lastFwdZ_ = fwdZ;
    lastFov_ = fovRadV; lastAspect_ = aspectRatio;
    lastNear_ = nearDist; lastFar_ = farDist;

    // Normalize forward direction
    float fwdLen = std::sqrt(fwdX*fwdX + fwdY*fwdY + fwdZ*fwdZ);
    if (fwdLen < 0.0001f) return; // Degenerate direction
    fwdX /= fwdLen; fwdY /= fwdLen; fwdZ /= fwdLen;

    // Right direction = forward x worldUp, where worldUp = (0, 0, 1) in EQ Z-up
    // cross(fwd, (0,0,1)) = (fwdY*1 - fwdZ*0, fwdZ*0 - fwdX*1, fwdX*0 - fwdY*0)
    //                      = (fwdY, -fwdX, 0)
    float rightX = fwdY;
    float rightY = -fwdX;
    float rightZ = 0.0f;

    // Normalize right (handles looking straight up/down gracefully)
    float rightLen = std::sqrt(rightX*rightX + rightY*rightY);
    if (rightLen < 0.0001f) {
        // Looking straight up or down - pick arbitrary right vector
        rightX = 1.0f; rightY = 0.0f; rightZ = 0.0f;
    } else {
        rightX /= rightLen; rightY /= rightLen;
    }

    // Up direction = right x forward (gives an up vector perpendicular to both)
    float upX = rightY * fwdZ - rightZ * fwdY;
    float upY = rightZ * fwdX - rightX * fwdZ;
    float upZ = rightX * fwdY - rightY * fwdX;

    // Normalize up
    float upLen = std::sqrt(upX*upX + upY*upY + upZ*upZ);
    if (upLen > 0.0001f) {
        upX /= upLen; upY /= upLen; upZ /= upLen;
    }

    // Store diagnostic directions
    diagFwdX_ = fwdX; diagFwdY_ = fwdY; diagFwdZ_ = fwdZ;
    diagRightX_ = rightX; diagRightY_ = rightY;
    diagUpX_ = upX; diagUpY_ = upY; diagUpZ_ = upZ;

    // Half-angles with margin to prevent edge pop-in
    float halfVFov = fovRadV * 0.5f + FRUSTUM_MARGIN_DEG * DEG2RAD;
    float halfHFov = std::atan(std::tan(halfVFov) * aspectRatio);

    float cosHalfH = std::cos(halfHFov);
    float sinHalfH = std::sin(halfHFov);
    float cosHalfV = std::cos(halfVFov);
    float sinHalfV = std::sin(halfVFov);

    // Left plane: normal points inward (toward right side of frustum)
    float lnx = fwdX * sinHalfH + rightX * cosHalfH;
    float lny = fwdY * sinHalfH + rightY * cosHalfH;
    float lnz = fwdZ * sinHalfH + rightZ * cosHalfH;
    planes_[0][0] = lnx;
    planes_[0][1] = lny;
    planes_[0][2] = lnz;
    planes_[0][3] = -(lnx * camX + lny * camY + lnz * camZ);

    // Right plane: normal points inward (toward left side of frustum)
    float rnx = fwdX * sinHalfH - rightX * cosHalfH;
    float rny = fwdY * sinHalfH - rightY * cosHalfH;
    float rnz = fwdZ * sinHalfH - rightZ * cosHalfH;
    planes_[1][0] = rnx;
    planes_[1][1] = rny;
    planes_[1][2] = rnz;
    planes_[1][3] = -(rnx * camX + rny * camY + rnz * camZ);

    // Top plane: normal points inward (toward bottom of frustum)
    float tnx = fwdX * sinHalfV - upX * cosHalfV;
    float tny = fwdY * sinHalfV - upY * cosHalfV;
    float tnz = fwdZ * sinHalfV - upZ * cosHalfV;
    planes_[2][0] = tnx;
    planes_[2][1] = tny;
    planes_[2][2] = tnz;
    planes_[2][3] = -(tnx * camX + tny * camY + tnz * camZ);

    // Bottom plane: normal points inward (toward top of frustum)
    float bnx = fwdX * sinHalfV + upX * cosHalfV;
    float bny = fwdY * sinHalfV + upY * cosHalfV;
    float bnz = fwdZ * sinHalfV + upZ * cosHalfV;
    planes_[3][0] = bnx;
    planes_[3][1] = bny;
    planes_[3][2] = bnz;
    planes_[3][3] = -(bnx * camX + bny * camY + bnz * camZ);

    // Near plane: normal = forward, d = -dot(forward, camPos + forward * near)
    planes_[4][0] = fwdX;
    planes_[4][1] = fwdY;
    planes_[4][2] = fwdZ;
    planes_[4][3] = -(fwdX * (camX + fwdX * nearDist) +
                       fwdY * (camY + fwdY * nearDist) +
                       fwdZ * (camZ + fwdZ * nearDist));

    // Far plane: normal = -forward, d = -dot(-forward, camPos + forward * far)
    planes_[5][0] = -fwdX;
    planes_[5][1] = -fwdY;
    planes_[5][2] = -fwdZ;
    planes_[5][3] = -(-fwdX * (camX + fwdX * farDist) +
                       -fwdY * (camY + fwdY * farDist) +
                       -fwdZ * (camZ + fwdZ * farDist));
}

bool FrustumCuller::testAABB(float minX, float minY, float minZ,
                              float maxX, float maxY, float maxZ) const {
    if (!enabled_) return true;

    // P-vertex test: for each plane, find the corner of the AABB most aligned
    // with the plane normal (the P-vertex). If the P-vertex is outside ANY plane,
    // the AABB is fully outside the frustum.
    for (int i = 0; i < 6; ++i) {
        float nx = planes_[i][0];
        float ny = planes_[i][1];
        float nz = planes_[i][2];
        float d  = planes_[i][3];

        // P-vertex: for each axis, pick max if normal component is positive, min otherwise
        float px = (nx >= 0.0f) ? maxX : minX;
        float py = (ny >= 0.0f) ? maxY : minY;
        float pz = (nz >= 0.0f) ? maxZ : minZ;

        // If P-vertex is outside this plane, AABB is fully outside
        if (nx * px + ny * py + nz * pz + d < 0.0f) {
            return false;
        }
    }
    return true;
}

bool FrustumCuller::testSphere(float cx, float cy, float cz, float radius) const {
    if (!enabled_) return true;

    // Test sphere center against each plane, offset by radius
    for (int i = 0; i < 6; ++i) {
        float dist = planes_[i][0] * cx + planes_[i][1] * cy +
                     planes_[i][2] * cz + planes_[i][3];
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

} // namespace Graphics
} // namespace EQT
