/*
 * Zone BSP Tree — game-state-accessible zone spatial data.
 *
 * Extracted from graphics/eq/wld_loader.h (D20f1). No graphics dependencies.
 * Used by both game state (water detection, zone lines, collision) and renderer
 * (PVS culling, entity visibility, camera collision).
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

// Region types for BSP regions
enum class RegionType : uint8_t {
    Normal = 0,
    Water = 1,
    Lava = 2,
    Pvp = 3,
    Zoneline = 4,
    WaterBlockLOS = 5,
    FreezingWater = 6,
    Slippery = 7,
    Unknown = 8
};

// Zone line types
enum class ZoneLineType : uint8_t {
    Reference = 0,  // References a zone_point from the DB
    Absolute = 1    // Direct zone coordinates embedded in the name
};

// Zone line destination info
struct ZoneLineInfo {
    ZoneLineType type = ZoneLineType::Reference;
    uint16_t zoneId = 0;           // Target zone ID (for Absolute type)
    uint32_t zonePointIndex = 0;   // Zone point index (for Reference type)
    float x = 0.0f, y = 0.0f, z = 0.0f;  // Destination coordinates
    float heading = 0.0f;          // Destination heading (rotation)
};

// BSP tree node
struct BspNode {
    float normalX = 0.0f, normalY = 0.0f, normalZ = 0.0f;
    float splitDistance = 0.0f;
    int32_t regionId = 0;   // 1-indexed, 0 = no region
    int32_t left = -1;      // Left child index (-1 = no child)
    int32_t right = -1;     // Right child index (-1 = no child)
};

// BSP region (fragment 0x22)
struct BspRegion {
    bool containsPolygons = false;
    int32_t meshReference = -1;
    std::vector<RegionType> regionTypes;
    std::optional<ZoneLineInfo> zoneLineInfo;
    // PVS (Potentially Visible Set) data - which regions are visible from this region
    // Indexed by region ID (0-based), true = that region is visible from this one
    std::vector<bool> visibleRegions;
};

// Axis-aligned bounding box for BSP region bounds calculation
struct BspBounds {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    bool valid = false;

    BspBounds() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0), valid(false) {}
    BspBounds(float x1, float y1, float z1, float x2, float y2, float z2)
        : minX(x1), minY(y1), minZ(z1), maxX(x2), maxY(y2), maxZ(z2), valid(true) {}

    // Merge with another bounds (union)
    void merge(const BspBounds& other) {
        if (!other.valid) return;
        if (!valid) {
            *this = other;
            return;
        }
        minX = std::min(minX, other.minX);
        minY = std::min(minY, other.minY);
        minZ = std::min(minZ, other.minZ);
        maxX = std::max(maxX, other.maxX);
        maxY = std::max(maxY, other.maxY);
        maxZ = std::max(maxZ, other.maxZ);
    }
};

// BSP tree structure for zone
struct BspTree {
    std::vector<BspNode> nodes;
    std::vector<std::shared_ptr<BspRegion>> regions;

    // Find which region a point is in by traversing the BSP tree
    // Returns nullptr if not in any region
    std::shared_ptr<BspRegion> findRegionForPoint(float x, float y, float z) const;

    // Find the 0-based region index for a point by traversing the BSP tree
    // Returns SIZE_MAX if not in any region
    // More efficient than findRegionForPoint() when only the index is needed
    size_t findRegionIndexForPoint(float x, float y, float z) const;

    // Check if a point is in a zone line region
    // Returns the zone line info if in a zone line, nullopt otherwise
    std::optional<ZoneLineInfo> checkZoneLine(float x, float y, float z) const;

    // Compute bounding box for a specific region by traversing the BSP tree
    // regionIndex is 0-based index into the regions vector
    // initialBounds provides the starting search area (typically zone geometry bounds)
    BspBounds computeRegionBounds(size_t regionIndex, const BspBounds& initialBounds) const;

private:
    // Recursive helper for computeRegionBounds
    // Returns bounds for the target region found in this subtree
    BspBounds computeRegionBoundsRecursive(int nodeIdx, size_t targetRegionIndex,
                                            const BspBounds& currentBounds) const;

    // Clip bounds by a plane, returning the portion on the specified side
    // frontSide=true: return portion where dot >= 0
    // frontSide=false: return portion where dot < 0
    static BspBounds clipBoundsByPlane(const BspBounds& bounds,
                                        float nx, float ny, float nz, float dist,
                                        bool frontSide);
};

} // namespace Graphics
} // namespace EQT
