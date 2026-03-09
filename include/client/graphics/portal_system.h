#ifndef EQT_GRAPHICS_PORTAL_SYSTEM_H
#define EQT_GRAPHICS_PORTAL_SYSTEM_H

#include <vector>
#include <map>
#include <unordered_map>
#include <cstddef>
#include <irrlicht.h>

namespace EQT {
namespace Graphics {

struct BspTree;

// A portal is a convex polygon connecting two adjacent BSP regions.
// Extracted from BSP split planes — represents the exact boundary between regions.
struct Portal {
    size_t regionA, regionB;            // Connected region indices (0-based)
    std::vector<float> vertices;        // Polygon vertices, packed xyz (N*3 floats)
    float normalX, normalY, normalZ;    // Split plane normal (A toward B)
    float centerX, centerY, centerZ;
    float area;

    size_t vertexCount() const { return vertices.size() / 3; }
    float vx(size_t i) const { return vertices[i * 3 + 0]; }
    float vy(size_t i) const { return vertices[i * 3 + 1]; }
    float vz(size_t i) const { return vertices[i * 3 + 2]; }
};

// All portal data for a zone
struct ZonePortalData {
    std::vector<Portal> portals;
    // region index -> list of portal indices touching that region
    std::unordered_map<size_t, std::vector<size_t>> regionPortals;
};

class PortalSystem {
public:
    PortalSystem() = default;

    // Build portals from BSP tree split planes.
    // zoneBoundsMin/Max: zone geometry bounds in EQ Z-up coords (used to create initial windings)
    void buildFromBsp(const BspTree& bsp,
                      float boundsMinX, float boundsMinY, float boundsMinZ,
                      float boundsMaxX, float boundsMaxY, float boundsMaxZ);

    // Check if any portals were extracted
    bool hasPortals() const { return !data_.portals.empty(); }

    // Access portal data
    const ZonePortalData& getData() const { return data_; }

    // Get portals for a specific region
    const std::vector<size_t>& getPortalsForRegion(size_t regionIdx) const;

    // Get other region connected by a portal (from the perspective of fromRegion)
    // Returns SIZE_MAX if fromRegion is not one of the portal's endpoints
    size_t getOtherRegion(size_t portalIdx, size_t fromRegion) const;

private:
    ZonePortalData data_;
    std::vector<size_t> emptyList_;  // Returned for regions with no portals
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PORTAL_SYSTEM_H
