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

// A portal is a quad connecting two adjacent BSP regions (typically a doorway).
struct Portal {
    size_t regionA, regionB;            // Connected region indices
    float vertices[4][3];               // Quad corners (EQ Z-up coords)
    float normalX, normalY, normalZ;    // Plane normal (A toward B)
    float centerX, centerY, centerZ;
    float area;
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

    // Build portals from BSP tree and region bounding boxes (EQ Z-up coords)
    // regionBoundingBoxes: map of regionIdx -> AABB (Irrlicht coords stored as EQ coords)
    void buildFromBsp(const BspTree& bsp,
                      const std::map<size_t, irr::core::aabbox3df>& regionBoundingBoxes);

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

    // Generate a portal quad from the overlap of two AABBs
    bool generatePortalFromOverlap(
        size_t regionA, size_t regionB,
        const irr::core::aabbox3df& bboxA,
        const irr::core::aabbox3df& bboxB,
        Portal& outPortal);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_PORTAL_SYSTEM_H
