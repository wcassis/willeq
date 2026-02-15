#include "client/graphics/software_occlusion_culler.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <fstream>

namespace EQT {
namespace Graphics {

static constexpr float kNearPlane = 0.5f;

const std::vector<OccluderTriangle> SoftwareOcclusionCuller::emptyOccluders_;

SoftwareOcclusionCuller::SoftwareOcclusionCuller()
    : SoftwareOcclusionCuller(OcclusionCullerConfig{}) {}

SoftwareOcclusionCuller::SoftwareOcclusionCuller(const OcclusionCullerConfig& config)
    : config_(config)
    , depthBuffer_(config.width * config.height, FLT_MAX)
{
}

void SoftwareOcclusionCuller::setRegionOccluders(size_t regionIdx, std::vector<OccluderTriangle> triangles) {
    if (!triangles.empty()) {
        regionOccluders_[regionIdx] = std::move(triangles);
    }
}

const std::vector<OccluderTriangle>& SoftwareOcclusionCuller::getRegionOccluders(size_t regionIdx) const {
    auto it = regionOccluders_.find(regionIdx);
    if (it != regionOccluders_.end()) {
        return it->second;
    }
    return emptyOccluders_;
}

void SoftwareOcclusionCuller::setCamera(float camX, float camY, float camZ,
                                         float fwdX, float fwdY, float fwdZ,
                                         float rightX, float rightY, float rightZ,
                                         float upX, float upY, float upZ,
                                         float fovRadV, float aspect) {
    camPos_[0] = camX; camPos_[1] = camY; camPos_[2] = camZ;
    fwd_[0] = fwdX;    fwd_[1] = fwdY;    fwd_[2] = fwdZ;
    right_[0] = rightX; right_[1] = rightY; right_[2] = rightZ;
    up_[0] = upX;      up_[1] = upY;      up_[2] = upZ;

    tanHalfFovV_ = std::tan(fovRadV * 0.5f);
    tanHalfFovH_ = tanHalfFovV_ * aspect;
}

void SoftwareOcclusionCuller::clear() {
    std::fill(depthBuffer_.begin(), depthBuffer_.end(), FLT_MAX);
}

SoftwareOcclusionCuller::ViewVertex SoftwareOcclusionCuller::toViewSpace(const float pos[3]) const {
    float dx = pos[0] - camPos_[0];
    float dy = pos[1] - camPos_[1];
    float dz = pos[2] - camPos_[2];
    return {
        dx * right_[0] + dy * right_[1] + dz * right_[2],
        dx * up_[0]    + dy * up_[1]    + dz * up_[2],
        dx * fwd_[0]   + dy * fwd_[1]   + dz * fwd_[2]
    };
}

void SoftwareOcclusionCuller::projectViewVertex(const ViewVertex& v, float& screenX, float& screenY, float& depth) const {
    float invZ = 1.0f / v.z;
    screenX = (v.x * invZ / tanHalfFovH_ * 0.5f + 0.5f) * config_.width;
    screenY = (v.y * invZ / tanHalfFovV_ * 0.5f + 0.5f) * config_.height;
    depth = v.z;
}

SoftwareOcclusionCuller::ViewVertex SoftwareOcclusionCuller::clipEdge(const ViewVertex& behind, const ViewVertex& inFront) const {
    float t = (kNearPlane - behind.z) / (inFront.z - behind.z);
    return {
        behind.x + t * (inFront.x - behind.x),
        behind.y + t * (inFront.y - behind.y),
        kNearPlane
    };
}

bool SoftwareOcclusionCuller::projectVertex(const float pos[3], float& screenX, float& screenY, float& depth) const {
    ViewVertex v = toViewSpace(pos);
    if (v.z < kNearPlane) return false;
    projectViewVertex(v, screenX, screenY, depth);
    return true;
}

void SoftwareOcclusionCuller::rasterizeScreenTri(float sx0, float sy0, float sz0,
                                                  float sx1, float sy1, float sz1,
                                                  float sx2, float sy2, float sz2) {
    // Sort vertices by Y (top to bottom)
    if (sy0 > sy1) { std::swap(sx0, sx1); std::swap(sy0, sy1); std::swap(sz0, sz1); }
    if (sy0 > sy2) { std::swap(sx0, sx2); std::swap(sy0, sy2); std::swap(sz0, sz2); }
    if (sy1 > sy2) { std::swap(sx1, sx2); std::swap(sy1, sy2); std::swap(sz1, sz2); }

    // Degenerate triangle check
    float totalHeight = sy2 - sy0;
    if (totalHeight < 0.5f) return;

    const int w = config_.width;
    const int h = config_.height;

    // Scanline rasterization with depth interpolation
    auto rasterizeScanline = [&](int y, float xLeft, float xRight, float zLeft, float zRight) {
        if (y < 0 || y >= h) return;
        int ixLeft = std::max(0, static_cast<int>(std::ceil(xLeft)));
        int ixRight = std::min(w - 1, static_cast<int>(std::floor(xRight)));
        if (ixLeft > ixRight) return;

        float xSpan = xRight - xLeft;
        float invXSpan = (xSpan > 0.001f) ? (1.0f / xSpan) : 0.0f;

        int rowOffset = y * w;
        for (int x = ixLeft; x <= ixRight; ++x) {
            float t = (static_cast<float>(x) - xLeft) * invXSpan;
            float z = zLeft + t * (zRight - zLeft);
            if (z < depthBuffer_[rowOffset + x]) {
                depthBuffer_[rowOffset + x] = z;
            }
        }
    };

    float invTotalHeight = 1.0f / totalHeight;
    float upperHeight = sy1 - sy0;

    if (upperHeight > 0.5f) {
        float invUpperHeight = 1.0f / upperHeight;
        int yStart = std::max(0, static_cast<int>(std::ceil(sy0)));
        int yEnd = std::min(h - 1, static_cast<int>(std::floor(sy1)));

        for (int y = yStart; y <= yEnd; ++y) {
            float tTotal = (static_cast<float>(y) - sy0) * invTotalHeight;
            float tUpper = (static_cast<float>(y) - sy0) * invUpperHeight;
            float xLong = sx0 + tTotal * (sx2 - sx0);
            float zLong = sz0 + tTotal * (sz2 - sz0);
            float xShort = sx0 + tUpper * (sx1 - sx0);
            float zShort = sz0 + tUpper * (sz1 - sz0);
            if (xLong > xShort) {
                rasterizeScanline(y, xShort, xLong, zShort, zLong);
            } else {
                rasterizeScanline(y, xLong, xShort, zLong, zShort);
            }
        }
    }

    float lowerHeight = sy2 - sy1;
    if (lowerHeight > 0.5f) {
        float invLowerHeight = 1.0f / lowerHeight;
        int yStart = std::max(0, static_cast<int>(std::ceil(sy1)));
        int yEnd = std::min(h - 1, static_cast<int>(std::floor(sy2)));

        for (int y = yStart; y <= yEnd; ++y) {
            float tTotal = (static_cast<float>(y) - sy0) * invTotalHeight;
            float tLower = (static_cast<float>(y) - sy1) * invLowerHeight;
            float xLong = sx0 + tTotal * (sx2 - sx0);
            float zLong = sz0 + tTotal * (sz2 - sz0);
            float xShort = sx1 + tLower * (sx2 - sx1);
            float zShort = sz1 + tLower * (sz2 - sz1);
            if (xLong > xShort) {
                rasterizeScanline(y, xShort, xLong, zShort, zLong);
            } else {
                rasterizeScanline(y, xLong, xShort, zLong, zShort);
            }
        }
    }

    stats_.trianglesRasterized++;
}

void SoftwareOcclusionCuller::rasterizeTriangle(const float v0[3], const float v1[3], const float v2[3]) {
    // Transform to view space
    ViewVertex vv[3] = { toViewSpace(v0), toViewSpace(v1), toViewSpace(v2) };

    int behind[3] = { vv[0].z < kNearPlane, vv[1].z < kNearPlane, vv[2].z < kNearPlane };
    int behindCount = behind[0] + behind[1] + behind[2];

    if (behindCount == 3) return;  // All behind camera

    if (behindCount == 0) {
        // All in front - project and rasterize
        float sx0, sy0, sz0, sx1, sy1, sz1, sx2, sy2, sz2;
        projectViewVertex(vv[0], sx0, sy0, sz0);
        projectViewVertex(vv[1], sx1, sy1, sz1);
        projectViewVertex(vv[2], sx2, sy2, sz2);
        rasterizeScreenTri(sx0, sy0, sz0, sx1, sy1, sz1, sx2, sy2, sz2);
        return;
    }

    // Near-plane clipping needed
    stats_.trianglesClipped++;

    if (behindCount == 2) {
        // 2 behind, 1 in front -> clip to 1 triangle
        int fi = behind[0] ? (behind[1] ? 2 : 1) : 0;
        int b0 = (fi + 1) % 3, b1 = (fi + 2) % 3;
        ViewVertex c0 = clipEdge(vv[b0], vv[fi]);
        ViewVertex c1 = clipEdge(vv[b1], vv[fi]);

        float sxF, syF, szF, sxC0, syC0, szC0, sxC1, syC1, szC1;
        projectViewVertex(vv[fi], sxF, syF, szF);
        projectViewVertex(c0, sxC0, syC0, szC0);
        projectViewVertex(c1, sxC1, syC1, szC1);
        rasterizeScreenTri(sxF, syF, szF, sxC0, syC0, szC0, sxC1, syC1, szC1);
    } else {
        // 1 behind, 2 in front -> clip to quad (2 triangles)
        int bi = behind[0] ? 0 : (behind[1] ? 1 : 2);
        int f0 = (bi + 1) % 3, f1 = (bi + 2) % 3;
        ViewVertex c0 = clipEdge(vv[bi], vv[f0]);
        ViewVertex c1 = clipEdge(vv[bi], vv[f1]);

        float sxF0, syF0, szF0, sxF1, syF1, szF1;
        float sxC0, syC0, szC0, sxC1, syC1, szC1;
        projectViewVertex(vv[f0], sxF0, syF0, szF0);
        projectViewVertex(vv[f1], sxF1, syF1, szF1);
        projectViewVertex(c0, sxC0, syC0, szC0);
        projectViewVertex(c1, sxC1, syC1, szC1);

        // Quad (f0, c0, f1, c1) as two triangles
        rasterizeScreenTri(sxF0, syF0, szF0, sxC0, syC0, szC0, sxF1, syF1, szF1);
        rasterizeScreenTri(sxF1, syF1, szF1, sxC0, syC0, szC0, sxC1, syC1, szC1);
    }
}

bool SoftwareOcclusionCuller::testAABB(float minX, float minY, float minZ,
                                        float maxX, float maxY, float maxZ) {
    stats_.regionsTested++;

    // Project all 8 AABB corners to screen space
    float corners[8][3] = {
        {minX, minY, minZ}, {maxX, minY, minZ}, {minX, maxY, minZ}, {maxX, maxY, minZ},
        {minX, minY, maxZ}, {maxX, minY, maxZ}, {minX, maxY, maxZ}, {maxX, maxY, maxZ}
    };

    float screenMinX = static_cast<float>(config_.width);
    float screenMaxX = 0.0f;
    float screenMinY = static_cast<float>(config_.height);
    float screenMaxY = 0.0f;
    float nearestDepth = FLT_MAX;
    int projectedCount = 0;

    for (int i = 0; i < 8; ++i) {
        float sx, sy, sz;
        if (projectVertex(corners[i], sx, sy, sz)) {
            screenMinX = std::min(screenMinX, sx);
            screenMaxX = std::max(screenMaxX, sx);
            screenMinY = std::min(screenMinY, sy);
            screenMaxY = std::max(screenMaxY, sy);
            nearestDepth = std::min(nearestDepth, sz);
            projectedCount++;
        }
    }

    // If no corners project (all behind camera), not occluded (conservative)
    if (projectedCount == 0) {
        stats_.rejectedBehindCamera++;
        return false;
    }

    // If some corners are behind camera, clip AABB edges at the near plane
    // and include the clipped intersection points in the screen rect.
    if (projectedCount < 8) {
        // Transform all 8 corners to view space to know which are behind
        ViewVertex viewCorners[8];
        bool cornerBehind[8];
        for (int i = 0; i < 8; ++i) {
            viewCorners[i] = toViewSpace(corners[i]);
            cornerBehind[i] = (viewCorners[i].z < kNearPlane);
        }

        // 12 AABB edges: 4 along each axis
        // X-axis edges: (0,1),(2,3),(4,5),(6,7)
        // Y-axis edges: (0,2),(1,3),(4,6),(5,7)
        // Z-axis edges: (0,4),(1,5),(2,6),(3,7)
        static const int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7},  // X-axis
            {0,2},{1,3},{4,6},{5,7},  // Y-axis
            {0,4},{1,5},{2,6},{3,7}   // Z-axis
        };

        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0], b = edges[e][1];
            // Only clip edges that cross the near plane (one behind, one in front)
            if (cornerBehind[a] == cornerBehind[b]) continue;

            const ViewVertex& behind = cornerBehind[a] ? viewCorners[a] : viewCorners[b];
            const ViewVertex& inFront = cornerBehind[a] ? viewCorners[b] : viewCorners[a];
            ViewVertex clipped = clipEdge(behind, inFront);

            float sx, sy, depth;
            projectViewVertex(clipped, sx, sy, depth);
            screenMinX = std::min(screenMinX, sx);
            screenMaxX = std::max(screenMaxX, sx);
            screenMinY = std::min(screenMinY, sy);
            screenMaxY = std::max(screenMaxY, sy);
            nearestDepth = std::min(nearestDepth, depth);
        }
    }

    // Clamp screen rect to buffer bounds
    int ixMin = std::max(0, static_cast<int>(std::floor(screenMinX)));
    int ixMax = std::min(config_.width - 1, static_cast<int>(std::ceil(screenMaxX)));
    int iyMin = std::max(0, static_cast<int>(std::floor(screenMinY)));
    int iyMax = std::min(config_.height - 1, static_cast<int>(std::ceil(screenMaxY)));

    // If screen rect is empty or outside buffer, not occluded
    if (ixMin > ixMax || iyMin > iyMax) {
        stats_.rejectedOffScreen++;
        return false;
    }

    // If the AABB covers too much of the screen, skip the test
    // (very large objects are unlikely to be fully occluded)
    int screenArea = (ixMax - ixMin + 1) * (iyMax - iyMin + 1);
    int totalPixels = config_.width * config_.height;
    if (screenArea > (totalPixels * 3) / 4) {
        stats_.rejectedScreenTooLarge++;
        return false;
    }

    // Check depth buffer pixels in the screen rect.
    // Count how many pixels have closer depth than the AABB's nearest corner.
    const int w = config_.width;
    int totalPixelsInRect = screenArea;
    int coveredPixels = 0;

    // Early-out: how many uncovered pixels can we tolerate?
    int maxUncovered = totalPixelsInRect - static_cast<int>(std::ceil(config_.occlusionThreshold * totalPixelsInRect));

    int uncoveredPixels = 0;
    for (int y = iyMin; y <= iyMax; ++y) {
        int rowOffset = y * w;
        for (int x = ixMin; x <= ixMax; ++x) {
            if (depthBuffer_[rowOffset + x] < nearestDepth) {
                coveredPixels++;
            } else {
                uncoveredPixels++;
                if (uncoveredPixels > maxUncovered) {
                    stats_.rejectedLowCoverage++;
                    return false;
                }
            }
        }
    }

    // Check if enough pixels are covered
    if (totalPixelsInRect > 0 &&
        static_cast<float>(coveredPixels) / static_cast<float>(totalPixelsInRect) >= config_.occlusionThreshold) {
        stats_.regionsCulled++;
        return true;
    }

    stats_.rejectedLowCoverage++;
    return false;
}

bool SoftwareOcclusionCuller::testPoint(float x, float y, float z) const {
    float pos[3] = {x, y, z};
    ViewVertex v = toViewSpace(pos);

    // Behind near plane — not occluded (it's behind the camera)
    if (v.z < kNearPlane) return false;

    float sx, sy, depth;
    projectViewVertex(v, sx, sy, depth);

    // Off screen — conservatively not occluded
    int px = static_cast<int>(sx);
    int py = static_cast<int>(sy);
    if (px < 0 || px >= config_.width || py < 0 || py >= config_.height) return false;

    // Occluded if something closer exists in the depth buffer at this pixel
    return depthBuffer_[py * config_.width + px] < depth;
}

SoftwareOcclusionCuller::PointTestResult SoftwareOcclusionCuller::testPointDebug(float x, float y, float z) const {
    PointTestResult result;
    float pos[3] = {x, y, z};
    ViewVertex v = toViewSpace(pos);
    result.viewZ = v.z;

    if (v.z < kNearPlane) {
        result.behindCamera = true;
        return result;
    }

    float sx, sy, depth;
    projectViewVertex(v, sx, sy, depth);
    result.screenX = sx;
    result.screenY = sy;
    result.entityDepth = depth;

    int px = static_cast<int>(sx);
    int py = static_cast<int>(sy);
    result.pixelX = px;
    result.pixelY = py;

    if (px < 0 || px >= config_.width || py < 0 || py >= config_.height) {
        result.offScreen = true;
        return result;
    }

    result.bufferDepth = depthBuffer_[py * config_.width + px];
    result.occluded = (result.bufferDepth < depth);
    return result;
}

void SoftwareOcclusionCuller::computeBufferFillStats() {
    int filled = 0;
    int total = config_.width * config_.height;
    for (int i = 0; i < total; ++i) {
        if (depthBuffer_[i] < FLT_MAX) {
            filled++;
        }
    }
    stats_.depthBufferFilledPixels = filled;
    stats_.depthBufferTotalPixels = total;
}

void SoftwareOcclusionCuller::dumpDepthBufferPGM(const std::string& path) const {
    int total = config_.width * config_.height;

    // Find min/max filled depth for normalization
    float minDepth = FLT_MAX, maxDepth = 0.0f;
    for (int i = 0; i < total; ++i) {
        if (depthBuffer_[i] < FLT_MAX) {
            if (depthBuffer_[i] < minDepth) minDepth = depthBuffer_[i];
            if (depthBuffer_[i] > maxDepth) maxDepth = depthBuffer_[i];
        }
    }

    float range = maxDepth - minDepth;
    if (range < 0.001f) range = 1.0f;

    std::ofstream f(path, std::ios::binary);
    if (!f) return;

    // PGM header
    f << "P5\n" << config_.width << " " << config_.height << "\n255\n";

    for (int i = 0; i < total; ++i) {
        uint8_t pixel;
        if (depthBuffer_[i] >= FLT_MAX) {
            pixel = 0;  // Black = no depth data
        } else {
            // White (255) = closest, dark gray = farthest filled
            float t = (depthBuffer_[i] - minDepth) / range;
            pixel = static_cast<uint8_t>(255 - t * 200);  // Range 255..55
        }
        f.put(static_cast<char>(pixel));
    }
}

} // namespace Graphics
} // namespace EQT
