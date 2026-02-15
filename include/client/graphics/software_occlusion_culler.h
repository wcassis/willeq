#ifndef EQT_GRAPHICS_SOFTWARE_OCCLUSION_CULLER_H
#define EQT_GRAPHICS_SOFTWARE_OCCLUSION_CULLER_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace EQT {
namespace Graphics {

// A triangle used as an occluder, stored in world-space EQ coordinates (Z-up).
// Vertices have the region center offset already applied.
struct OccluderTriangle {
    float v0[3], v1[3], v2[3];
    float area;  // World-space area in square EQ units
};

// Configuration for the software occlusion culler
struct OcclusionCullerConfig {
    int width = 64;                    // Depth buffer width in pixels
    int height = 32;                   // Depth buffer height in pixels
    int maxOccluderRegions = 16;       // Max nearby regions to rasterize occluders from
    int maxTrianglesPerRegion = 12;    // Max occluder triangles stored per region
    float minOccluderArea = 20.0f;     // Minimum triangle area to qualify as occluder (sq EQ units)
    float occlusionThreshold = 0.95f;  // Fraction of pixels that must be covered to count as occluded (0.0-1.0)
    bool enabled = true;
};

// Runtime statistics for debugging/logging
struct OcclusionStats {
    int regionsRasterized = 0;     // Regions whose occluders were rasterized
    int trianglesRasterized = 0;   // Occluder triangles rasterized to depth buffer
    int trianglesClipped = 0;      // Triangles that needed near-plane clipping
    int regionsTested = 0;         // Regions tested for occlusion
    int regionsCulled = 0;         // Regions found to be fully occluded
    // Rejection reason counts (for testAABB failures)
    int rejectedBehindCamera = 0;  // All or some corners behind near plane
    int rejectedOffScreen = 0;     // Screen rect empty or outside buffer
    int rejectedScreenTooLarge = 0;// AABB covers too much of screen
    int rejectedLowCoverage = 0;   // Depth buffer didn't cover enough pixels
    // Depth buffer fill
    int depthBufferFilledPixels = 0; // Non-FLT_MAX pixels after rasterization
    int depthBufferTotalPixels = 0;  // Total pixels in buffer
};

// CPU-side software occlusion culler.
// Rasterizes nearby wall geometry to a small depth buffer and tests region AABBs
// against it to detect fully occluded regions that PVS marked as visible.
//
// All coordinates are in EQ space (Z-up), matching FrustumCuller convention.
class SoftwareOcclusionCuller {
public:
    SoftwareOcclusionCuller();
    explicit SoftwareOcclusionCuller(const OcclusionCullerConfig& config);
    ~SoftwareOcclusionCuller() = default;

    // Store occluder triangles for a region (called at zone load time)
    void setRegionOccluders(size_t regionIdx, std::vector<OccluderTriangle> triangles);

    // Get occluder triangles for a region
    const std::vector<OccluderTriangle>& getRegionOccluders(size_t regionIdx) const;

    // Set camera state from FrustumCuller basis vectors.
    // All in EQ coordinates (Z-up).
    void setCamera(float camX, float camY, float camZ,
                   float fwdX, float fwdY, float fwdZ,
                   float rightX, float rightY, float rightZ,
                   float upX, float upY, float upZ,
                   float fovRadV, float aspect);

    // Clear depth buffer (memset to FLT_MAX)
    void clear();

    // Rasterize a triangle as an occluder into the depth buffer.
    // Vertices in EQ world-space (Z-up). Clips against near plane.
    void rasterizeTriangle(const float v0[3], const float v1[3], const float v2[3]);

    // Test if an AABB is fully occluded by the depth buffer.
    // Returns true if occluded (enough covered pixels have closer depth).
    // Coordinates in EQ space (Z-up).
    bool testAABB(float minX, float minY, float minZ,
                  float maxX, float maxY, float maxZ);

    // Test if a single world-space point is behind an occluder in the depth buffer.
    // Returns true if occluded (depth buffer has closer geometry at that pixel).
    // Coordinates in EQ space (Z-up).
    bool testPoint(float x, float y, float z) const;

    // Debug version of testPoint that fills in diagnostic info
    struct PointTestResult {
        bool occluded = false;
        bool behindCamera = false;
        bool offScreen = false;
        float viewZ = 0;           // View-space depth
        float screenX = 0, screenY = 0;  // Projected screen coords
        int pixelX = 0, pixelY = 0;      // Integer pixel coords
        float entityDepth = 0;     // Entity's depth (viewZ)
        float bufferDepth = 0;     // Depth buffer value at pixel
    };
    PointTestResult testPointDebug(float x, float y, float z) const;

    // Get last frame's statistics
    const OcclusionStats& getStats() const { return stats_; }

    // Get mutable stats (for external counters like regionsRasterized)
    OcclusionStats& getStatsMutable() { return stats_; }

    // Reset statistics (call at start of each occlusion pass)
    void resetStats() { stats_ = OcclusionStats{}; }

    // Configuration access
    const OcclusionCullerConfig& getConfig() const { return config_; }
    bool isEnabled() const { return config_.enabled; }
    void setEnabled(bool enabled) { config_.enabled = enabled; }

    // Check if any occluders have been loaded
    bool hasOccluders() const { return !regionOccluders_.empty(); }

    // Clear all occluder data (call when changing zones)
    void clearOccluders() { regionOccluders_.clear(); }

    // Count non-empty pixels in depth buffer and store in stats
    void computeBufferFillStats();

    // Dump depth buffer as a PGM image file for debugging
    // Pixels with no depth (FLT_MAX) are black, closest = white, farthest filled = dark gray
    void dumpDepthBufferPGM(const std::string& path) const;

private:
    // Project a world-space point to screen coordinates.
    // Returns false if the point is behind the near plane.
    // screenX, screenY: pixel coordinates (may be outside [0, width/height))
    // depth: view-space depth (distance along forward axis)
    bool projectVertex(const float pos[3], float& screenX, float& screenY, float& depth) const;

    // View-space vertex for near-plane clipping
    struct ViewVertex {
        float x, y, z;  // View-space coordinates (z = depth along forward)
    };

    // Transform world-space vertex to view-space
    ViewVertex toViewSpace(const float pos[3]) const;

    // Project view-space vertex to screen coordinates
    void projectViewVertex(const ViewVertex& v, float& screenX, float& screenY, float& depth) const;

    // Clip edge against near plane, returning intersection point
    ViewVertex clipEdge(const ViewVertex& behind, const ViewVertex& inFront) const;

    // Rasterize a triangle already in screen space (sx, sy, depth)
    void rasterizeScreenTri(float sx0, float sy0, float sz0,
                            float sx1, float sy1, float sz1,
                            float sx2, float sy2, float sz2);

    OcclusionCullerConfig config_;
    OcclusionStats stats_;

    // Depth buffer: row-major, [y * width + x]
    std::vector<float> depthBuffer_;

    // Camera state
    float camPos_[3] = {};
    float fwd_[3] = {};
    float right_[3] = {};
    float up_[3] = {};
    float tanHalfFovH_ = 1.0f;  // tan(fovH / 2)
    float tanHalfFovV_ = 1.0f;  // tan(fovV / 2)

    // Per-region occluder storage (populated at zone load)
    std::unordered_map<size_t, std::vector<OccluderTriangle>> regionOccluders_;

    // Empty vector for getRegionOccluders when region not found
    static const std::vector<OccluderTriangle> emptyOccluders_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SOFTWARE_OCCLUSION_CULLER_H
