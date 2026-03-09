#ifndef EQT_GRAPHICS_PORTAL_GEOMETRY_CLIPPER_H
#define EQT_GRAPHICS_PORTAL_GEOMETRY_CLIPPER_H

#include <vector>
#include <cstddef>

class HCMap;

namespace EQT {
namespace Graphics {

struct Portal;

// A single opening polygon within a portal boundary.
// Represents an actual doorway, window, arch, or gap in the wall geometry.
struct PortalOpening {
    std::vector<float> vertices;  // 3D boundary polygon, packed xyz
    float area;
    float centerX, centerY, centerZ;

    size_t vertexCount() const { return vertices.size() / 3; }
    float vx(size_t i) const { return vertices[i * 3 + 0]; }
    float vy(size_t i) const { return vertices[i * 3 + 1]; }
    float vz(size_t i) const { return vertices[i * 3 + 2]; }
};

// Result of probing a BSP boundary portal against collision geometry via raycasting.
struct ClippedPortalResult {
    size_t regionA, regionB;
    float normalX, normalY, normalZ;

    float totalBoundaryArea;       // BSP boundary polygon area
    float wallCoveredArea;         // Area blocked by geometry (raycast hits)
    float totalOpeningArea;        // Area open (raycast misses)
    int blockedCellCount;          // Number of grid cells blocked by geometry

    std::vector<PortalOpening> openings;  // Actual opening polygons

    bool isFullyCovered() const { return openings.empty() && wallCoveredArea > 0; }
    bool isFullyOpen() const { return wallCoveredArea == 0 && totalBoundaryArea > 0; }
    bool isPartial() const { return !openings.empty() && wallCoveredArea > 0; }
    float coverageRatio() const {
        return totalBoundaryArea > 0 ? wallCoveredArea / totalBoundaryArea : 0;
    }
};

// Probe a BSP boundary portal against collision mesh (HCMap) using raycasting.
// Casts rays perpendicular to the portal plane at grid sample points.
// Rays that hit geometry = blocked (wall/floor/ceiling).
// Rays that miss = open (doorway/hole).
// Connected open regions are extracted as opening polygons.
//
// portal: BSP boundary polygon (from split plane extraction)
// map: loaded HCMap collision mesh for the zone
// rayLength: how far to cast rays in each direction from the portal plane
// gridSpacing: distance between sample points on the portal grid
ClippedPortalResult clipPortalAgainstGeometry(
    const Portal& portal,
    const HCMap* map,
    float rayLength = 15.0f,
    float gridSpacing = 0.5f);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PORTAL_GEOMETRY_CLIPPER_H
