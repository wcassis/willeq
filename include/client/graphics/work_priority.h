// work_priority.h — Unified priority key for cross-thread work ordering.
// Lower value = higher priority. Packed into a single uint32_t for trivial comparison.

#ifndef EQT_GRAPHICS_WORK_PRIORITY_H
#define EQT_GRAPHICS_WORK_PRIORITY_H

#include <algorithm>
#include <cstdint>

namespace EQT {
namespace Graphics {

// Asset type enum — determines priority within the same PVS depth.
// Lower numeric value = higher priority.
enum class AssetType : uint8_t {
    ZoneMesh      = 0,  // Highest priority within depth
    ZoneTexture   = 1,
    Door          = 2,
    EntityMesh    = 3,
    EntityTexture = 4,
    LightEffect   = 5,
    Icon          = 6,  // Non-spatial UI asset (lowest)
};

// Packed priority key: bits 31-24 = PVS depth, bits 23-20 = asset type, bits 19-0 = distance tiebreaker.
// Lower value = higher priority. Fits in one register for trivial comparison.
struct WorkPriorityKey {
    uint32_t value;

    static WorkPriorityKey make(uint8_t pvsDepth, AssetType type, float distanceSq = 0.0f) {
        // Quantize distanceSq into 20-bit range [0, 1048575]
        // distSq / 10.0f gives ~3200 unit range at full resolution
        uint32_t dist20 = static_cast<uint32_t>(
            std::min(1048575.0f, std::max(0.0f, distanceSq / 10.0f)));
        return { (static_cast<uint32_t>(pvsDepth) << 24) |
                 (static_cast<uint32_t>(type) << 20) |
                 dist20 };
    }

    // Non-spatial assets (icons, UI) — always lowest priority
    static WorkPriorityKey makeNonSpatial(AssetType type) {
        return make(255, type);
    }

    bool operator<(const WorkPriorityKey& o) const { return value < o.value; }
    bool operator>(const WorkPriorityKey& o) const { return value > o.value; }
    bool operator<=(const WorkPriorityKey& o) const { return value <= o.value; }
    bool operator>=(const WorkPriorityKey& o) const { return value >= o.value; }
    bool operator==(const WorkPriorityKey& o) const { return value == o.value; }
    bool operator!=(const WorkPriorityKey& o) const { return value != o.value; }
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_WORK_PRIORITY_H
