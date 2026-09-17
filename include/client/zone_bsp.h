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
#include <unordered_map>
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

// A decoded PVS bitset (T490). Holds shared ownership of the decoded
// vector so a caller can keep it across a loop while the tree's retention
// cache rotates underneath (the decode is per camera region; the cache is
// bounded, see PvsStore). Indexed by 0-based region index; size() is
// the zone's total region count, or 0 when the region carries no PVS.
class PvsBits {
public:
    PvsBits() = default;
    explicit PvsBits(std::shared_ptr<const std::vector<bool>> bits) : bits_(std::move(bits)) {}
    bool empty() const { return !bits_ || bits_->empty(); }
    size_t size() const { return bits_ ? bits_->size() : 0; }
    // Caller guards i < size(), exactly as with the vector it replaces.
    bool operator[](size_t i) const { return (*bits_)[i]; }
    const std::vector<bool>& bits() const;
    std::vector<bool>::const_iterator begin() const { return bits().begin(); }
    std::vector<bool>::const_iterator end() const { return bits().end(); }
private:
    std::shared_ptr<const std::vector<bool>> bits_;
};

// The tree's PVS store (T490): one arena holding every region's RLE bytes
// back to back, the zone's region count, a bounded ring of decoded bitsets
// (kPvsCacheSlots x regionCount/8 bytes -- 16 x 4.7 KB for iceclad, never
// regions^2/8 = 176.9 MB, the AUDIT-0914 OOM) and, for hand-built trees,
// pinned decoded sets. Per region the handle is 16 + 12 bytes; the arena
// replaces 37,609 separate heap blocks and their malloc overhead.
class PvsStore;

// BSP region (fragment 0x22)
struct BspRegion {
    bool containsPolygons = false;
    int32_t meshReference = -1;
    std::vector<RegionType> regionTypes;
    std::optional<ZoneLineInfo> zoneLineInfo;

    // PVS (Potentially Visible Set): which regions are visible from this one.
    //
    // T490: stored as the WLD's own RLE byte string (fragment 0x22) in the
    // tree's PvsStore arena, not as a dense std::vector<bool> sized to the
    // zone's region count. The dense form is regions^2/8 bytes across the
    // tree -- 176.9 MB for iceclad's 37,609 regions, measured by heaptrack at
    // AUDIT-0914 -- and it OOM-killed the Orange Pi at "Computing spatial
    // data". The RLE form is what the 1999 client kept; a bitset is decoded
    // on demand (pvs()) through the store's bounded cache, and single lookups
    // (pvsContains()) walk the RLE without materialising anything.
    void setPvsRle(const uint8_t* data, size_t size, uint32_t regionCount);
    // Pinned, pre-decoded form for hand-built trees (tests, tools). Never used
    // by the WLD parser.
    void setPvs(std::vector<bool> bits);
    bool hasPvs() const;
    uint32_t pvsRegionCount() const;
    // Decoded bitset (cached through the store when the region belongs to a
    // tree). Empty when the region has no PVS.
    PvsBits pvs() const;
    // Is regionIdx visible from this region? False when this region has no PVS
    // or regionIdx is out of range -- the same answer the old
    // (idx < bitset.size() && bitset[idx]) guard gave against the dense form.
    bool pvsContains(size_t regionIdx) const;
    size_t pvsVisibleCount() const;
    // Bytes this region's PVS holds resident (its arena share + pinned form);
    // the decode cache is tallied by the tree.
    size_t pvsMemoryBytes() const;
    // Moves this region's PVS into `store` under `key` (BspTree::addRegion).
    void attachPvsStore(std::shared_ptr<PvsStore> store, uint32_t key);

private:
    std::shared_ptr<PvsStore> pvsStore_;
    uint32_t pvsOffset_ = 0;   // into the store's arena
    uint32_t pvsKey_ = 0;      // region index in the tree; cache / pinned key
    uint16_t pvsLen_ = 0;      // RLE bytes (fragment 0x22 sizes it as int16)
    bool pvsPinned_ = false;   // the store holds a pinned bitset under pvsKey_
};

// Decodes a fragment-0x22 RLE PVS string into a dense bitset of numRegions
// bits. Exposed for tests; production goes through BspRegion::pvs().
std::vector<bool> decodeRlePvs(const uint8_t* data, size_t dataSize, uint32_t numRegions);

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

    BspTree();

    // Appends a region and moves its PVS into this tree's PvsStore (T490).
    // Regions pushed straight onto `regions` still work; each then keeps a
    // private store of its own.
    void addRegion(std::shared_ptr<BspRegion> region);
    // Trims the arena's growth slack once every region is in (parser end).
    void shrinkPvsArena();
    // Bytes held by the decode cache right now (bounded; see PvsStore).
    size_t pvsCacheBytes() const;
    // Arena bytes (every region's RLE, back to back) plus growth slack.
    size_t pvsArenaBytes() const;
    // Number of decoded bitsets the store retains at most.
    static constexpr size_t kPvsCacheSlots = 16;

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
    std::shared_ptr<PvsStore> pvsStore_;

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
