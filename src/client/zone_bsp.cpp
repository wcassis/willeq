/*
 * Zone BSP Tree implementation.
 * Moved from src/client/graphics/eq/wld_loader.cpp (D20f1).
 */

#include "client/zone_bsp.h"
#include "common/logging.h"

#include <array>
#include <mutex>
#include <unordered_map>

namespace EQT {
namespace Graphics {

// ── PVS: RLE storage, on-demand decode, bounded retention (T490) ──────────
//
// Fragment 0x22 carries each region's PVS as a run-length byte string; the
// dense bitset it decodes to is regionCount bits, and holding one per region
// is regions^2/8 bytes for the tree: 176.9 MB for iceclad (37,609 regions),
// 11.8 MB for firiona (9,686), 0.8 MB for nro (2,583) -- heaptrack,
// review/T490/. The RLE is kept verbatim (a few bytes to a few KB per
// region) and decoded per camera region through PvsStore below.

namespace {

// Walks an RLE PVS string, calling visit(regionIdx) for every visible
// region in ascending order. visit returns false to stop early. The byte
// grammar is unchanged from the decoder this replaces
// (wld_fragments_bsp.cpp, pre-T490); see the comments inline.
template <typename Visit>
void walkRlePvs(const uint8_t* data, size_t dataSize, uint32_t numRegions, Visit&& visit) {
    uint32_t region = 0;
    size_t i = 0;
    auto include = [&](uint32_t count) -> bool {
        for (uint32_t j = 0; j < count && region < numRegions; ++j) {
            if (!visit(region)) return false;
            ++region;
        }
        return true;
    };
    while (i < dataSize && region < numRegions) {
        uint8_t byte = data[i++];
        if (byte <= 0x3E) {
            // 0x00..0x3E - skip forward by this many region IDs (not visible)
            region += byte;
        } else if (byte == 0x3F) {
            // 0x3F, WORD - skip forward by the following 16-bit count
            if (i + 2 > dataSize) break;  // malformed
            region += static_cast<uint32_t>(data[i]) | (static_cast<uint32_t>(data[i + 1]) << 8);
            i += 2;
        } else if (byte <= 0x7F) {
            // 0x40..0x7F - skip by bits 3..5, then include by bits 0..2
            uint8_t val = byte - 0x40;
            region += (val >> 3) & 0x07;
            if (!include(val & 0x07)) return;
        } else if (byte <= 0xBF) {
            // 0x80..0xBF - include by bits 3..5, then skip by bits 0..2
            uint8_t val = byte - 0x80;
            if (!include((val >> 3) & 0x07)) return;
            region += val & 0x07;
        } else if (byte <= 0xFE) {
            // 0xC0..0xFE - (byte - 0xC0) regions are visible
            if (!include(byte - 0xC0)) return;
        } else {
            // 0xFF, WORD - the following 16-bit count of regions are visible
            if (i + 2 > dataSize) break;  // malformed
            uint32_t count = static_cast<uint32_t>(data[i]) | (static_cast<uint32_t>(data[i + 1]) << 8);
            i += 2;
            if (!include(count)) return;
        }
    }
}

} // namespace

std::vector<bool> decodeRlePvs(const uint8_t* data, size_t dataSize, uint32_t numRegions) {
    std::vector<bool> visible(numRegions, false);
    walkRlePvs(data, dataSize, numRegions, [&](uint32_t r) { visible[r] = true; return true; });
    return visible;
}

// One per tree: the RLE arena, the region count, a ring of the last
// kPvsCacheSlots decoded bitsets and the pinned sets of hand-built trees.
// Cache slots are keyed by the region's tree index, never by address, so a
// region freed and re-allocated at the same address cannot alias a slot.
// One mutex: the render thread, the simulation worker and the camera all
// read PVS; a miss decodes under the lock (microseconds), a hit copies a
// shared_ptr. Callers hold that shared_ptr for as long as they need the
// bits, so eviction can never pull a bitset out from under a loop.
class PvsStore {
public:
    uint32_t regionCount = 0;
    std::vector<uint8_t> arena;
    std::unordered_map<uint32_t, std::shared_ptr<const std::vector<bool>>> pinned;

    std::shared_ptr<const std::vector<bool>> lookup(uint32_t key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& slot : slots_) {
            if (slot.bits && slot.key == key) return slot.bits;
        }
        return nullptr;
    }
    void store(uint32_t key, std::shared_ptr<const std::vector<bool>> bits) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& slot : slots_) {
            if (slot.bits && slot.key == key) return;  // raced with another decoder
        }
        slots_[next_] = Slot{key, std::move(bits)};
        next_ = (next_ + 1) % slots_.size();
    }
    size_t cacheBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& slot : slots_) {
            if (slot.bits) total += sizeof(std::vector<bool>) + (slot.bits->capacity() + 7) / 8;
        }
        return total;
    }
private:
    struct Slot {
        uint32_t key = 0;
        std::shared_ptr<const std::vector<bool>> bits;
    };
    mutable std::mutex mutex_;
    std::array<Slot, BspTree::kPvsCacheSlots> slots_;
    size_t next_ = 0;
};

const std::vector<bool>& PvsBits::bits() const {
    static const std::vector<bool> kEmpty;
    return bits_ ? *bits_ : kEmpty;
}

void BspRegion::setPvsRle(const uint8_t* data, size_t size, uint32_t regionCount) {
    if (!pvsStore_) pvsStore_ = std::make_shared<PvsStore>();
    if (size > 0xFFFF) size = 0xFFFF;  // fragment 0x22 sizes the PVS as int16
    pvsStore_->regionCount = regionCount;
    pvsStore_->pinned.erase(pvsKey_);
    pvsPinned_ = false;
    pvsOffset_ = static_cast<uint32_t>(pvsStore_->arena.size());
    pvsLen_ = static_cast<uint16_t>(size);
    pvsStore_->arena.insert(pvsStore_->arena.end(), data, data + size);
}

void BspRegion::setPvs(std::vector<bool> bits) {
    if (!pvsStore_) pvsStore_ = std::make_shared<PvsStore>();
    pvsLen_ = 0;
    pvsPinned_ = true;
    pvsStore_->pinned[pvsKey_] = std::make_shared<const std::vector<bool>>(std::move(bits));
}

void BspRegion::attachPvsStore(std::shared_ptr<PvsStore> store, uint32_t key) {
    if (pvsStore_ == store && pvsKey_ == key) return;
    std::shared_ptr<PvsStore> old = pvsStore_;
    uint32_t oldKey = pvsKey_;
    pvsStore_ = std::move(store);
    pvsKey_ = key;
    if (!old) return;
    if (pvsLen_ > 0) {
        const uint8_t* src = old->arena.data() + pvsOffset_;
        pvsStore_->regionCount = old->regionCount;
        pvsOffset_ = static_cast<uint32_t>(pvsStore_->arena.size());
        pvsStore_->arena.insert(pvsStore_->arena.end(), src, src + pvsLen_);
    }
    if (pvsPinned_) {
        auto it = old->pinned.find(oldKey);
        if (it != old->pinned.end()) {
            pvsStore_->pinned[pvsKey_] = it->second;
            old->pinned.erase(it);
        }
    }
}

bool BspRegion::hasPvs() const {
    return pvsStore_ && (pvsLen_ > 0 || pvsPinned_);
}

uint32_t BspRegion::pvsRegionCount() const {
    if (!pvsStore_) return 0;
    if (pvsPinned_) {
        auto it = pvsStore_->pinned.find(pvsKey_);
        return it != pvsStore_->pinned.end() ? static_cast<uint32_t>(it->second->size()) : 0;
    }
    return pvsLen_ > 0 ? pvsStore_->regionCount : 0;
}

PvsBits BspRegion::pvs() const {
    if (!pvsStore_) return PvsBits();
    if (pvsPinned_) {
        auto it = pvsStore_->pinned.find(pvsKey_);
        return it != pvsStore_->pinned.end() ? PvsBits(it->second) : PvsBits();
    }
    if (pvsLen_ == 0) return PvsBits();
    if (auto hit = pvsStore_->lookup(pvsKey_)) return PvsBits(std::move(hit));
    auto bits = std::make_shared<const std::vector<bool>>(
        decodeRlePvs(pvsStore_->arena.data() + pvsOffset_, pvsLen_, pvsStore_->regionCount));
    pvsStore_->store(pvsKey_, bits);
    return PvsBits(std::move(bits));
}

bool BspRegion::pvsContains(size_t regionIdx) const {
    if (!pvsStore_) return false;
    if (pvsPinned_) {
        auto it = pvsStore_->pinned.find(pvsKey_);
        return it != pvsStore_->pinned.end() && regionIdx < it->second->size() && (*it->second)[regionIdx];
    }
    if (pvsLen_ == 0 || regionIdx >= pvsStore_->regionCount) return false;
    bool found = false;
    walkRlePvs(pvsStore_->arena.data() + pvsOffset_, pvsLen_, pvsStore_->regionCount, [&](uint32_t r) {
        if (r == regionIdx) { found = true; return false; }
        return r < regionIdx;  // regions arrive ascending: past the target, stop
    });
    return found;
}

size_t BspRegion::pvsVisibleCount() const {
    if (!pvsStore_) return 0;
    if (pvsPinned_) {
        auto it = pvsStore_->pinned.find(pvsKey_);
        if (it == pvsStore_->pinned.end()) return 0;
        size_t n = 0;
        for (bool v : *it->second) n += v ? 1 : 0;
        return n;
    }
    size_t n = 0;
    walkRlePvs(pvsStore_->arena.data() + pvsOffset_, pvsLen_, pvsStore_->regionCount,
               [&](uint32_t) { ++n; return true; });
    return n;
}

size_t BspRegion::pvsMemoryBytes() const {
    size_t total = pvsLen_;
    if (pvsPinned_ && pvsStore_) {
        auto it = pvsStore_->pinned.find(pvsKey_);
        if (it != pvsStore_->pinned.end()) total += sizeof(std::vector<bool>) + (it->second->capacity() + 7) / 8;
    }
    return total;
}

BspTree::BspTree() : pvsStore_(std::make_shared<PvsStore>()) {}

void BspTree::addRegion(std::shared_ptr<BspRegion> region) {
    if (region) region->attachPvsStore(pvsStore_, static_cast<uint32_t>(regions.size()));
    regions.push_back(std::move(region));
}

void BspTree::shrinkPvsArena() {
    if (pvsStore_) pvsStore_->arena.shrink_to_fit();
}

size_t BspTree::pvsCacheBytes() const {
    return pvsStore_ ? pvsStore_->cacheBytes() : 0;
}

size_t BspTree::pvsArenaBytes() const {
    return pvsStore_ ? pvsStore_->arena.capacity() : 0;
}

BspBounds BspTree::clipBoundsByPlane(const BspBounds& bounds,
                                      float nx, float ny, float nz, float dist,
                                      bool frontSide) {
    if (!bounds.valid) return bounds;

    float corners[8][3] = {
        {bounds.minX, bounds.minY, bounds.minZ},
        {bounds.maxX, bounds.minY, bounds.minZ},
        {bounds.minX, bounds.maxY, bounds.minZ},
        {bounds.maxX, bounds.maxY, bounds.minZ},
        {bounds.minX, bounds.minY, bounds.maxZ},
        {bounds.maxX, bounds.minY, bounds.maxZ},
        {bounds.minX, bounds.maxY, bounds.maxZ},
        {bounds.maxX, bounds.maxY, bounds.maxZ}
    };

    bool anyOnSide = false;
    bool allOnSide = true;
    for (int i = 0; i < 8; ++i) {
        float dot = corners[i][0] * nx + corners[i][1] * ny + corners[i][2] * nz + dist;
        bool onFront = (dot >= 0);
        bool onDesiredSide = (frontSide == onFront);
        if (onDesiredSide) anyOnSide = true;
        else allOnSide = false;
    }

    if (!anyOnSide) return BspBounds();
    if (allOnSide) return bounds;

    BspBounds result = bounds;
    const float EPSILON = 0.001f;

    if (std::abs(nx) > EPSILON) {
        float centerY = (bounds.minY + bounds.maxY) / 2.0f;
        float centerZ = (bounds.minZ + bounds.maxZ) / 2.0f;
        float xAtPlane = -(ny * centerY + nz * centerZ + dist) / nx;
        if (xAtPlane > bounds.minX && xAtPlane < bounds.maxX) {
            if ((nx > 0) == frontSide) result.minX = std::max(result.minX, xAtPlane);
            else result.maxX = std::min(result.maxX, xAtPlane);
        }
    }

    if (std::abs(ny) > EPSILON) {
        float centerX = (bounds.minX + bounds.maxX) / 2.0f;
        float centerZ = (bounds.minZ + bounds.maxZ) / 2.0f;
        float yAtPlane = -(nx * centerX + nz * centerZ + dist) / ny;
        if (yAtPlane > bounds.minY && yAtPlane < bounds.maxY) {
            if ((ny > 0) == frontSide) result.minY = std::max(result.minY, yAtPlane);
            else result.maxY = std::min(result.maxY, yAtPlane);
        }
    }

    if (std::abs(nz) > EPSILON) {
        float centerX = (bounds.minX + bounds.maxX) / 2.0f;
        float centerY = (bounds.minY + bounds.maxY) / 2.0f;
        float zAtPlane = -(nx * centerX + ny * centerY + dist) / nz;
        if (zAtPlane > bounds.minZ && zAtPlane < bounds.maxZ) {
            if ((nz > 0) == frontSide) result.minZ = std::max(result.minZ, zAtPlane);
            else result.maxZ = std::min(result.maxZ, zAtPlane);
        }
    }

    if (result.minX >= result.maxX || result.minY >= result.maxY || result.minZ >= result.maxZ) {
        return BspBounds();
    }

    return result;
}

BspBounds BspTree::computeRegionBoundsRecursive(int nodeIdx, size_t targetRegionIndex,
                                                 const BspBounds& currentBounds) const {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size() || !currentBounds.valid) {
        return BspBounds();
    }

    const BspNode& node = nodes[nodeIdx];

    if (node.regionId > 0) {
        size_t thisRegionIndex = static_cast<size_t>(node.regionId - 1);
        if (thisRegionIndex == targetRegionIndex) return currentBounds;
        return BspBounds();
    }

    BspBounds result;

    if (node.left >= 0) {
        BspBounds leftBounds = clipBoundsByPlane(currentBounds,
            node.normalX, node.normalY, node.normalZ, node.splitDistance, true);
        if (leftBounds.valid) {
            BspBounds leftResult = computeRegionBoundsRecursive(node.left, targetRegionIndex, leftBounds);
            result.merge(leftResult);
        }
    }

    if (node.right >= 0) {
        BspBounds rightBounds = clipBoundsByPlane(currentBounds,
            node.normalX, node.normalY, node.normalZ, node.splitDistance, false);
        if (rightBounds.valid) {
            BspBounds rightResult = computeRegionBoundsRecursive(node.right, targetRegionIndex, rightBounds);
            result.merge(rightResult);
        }
    }

    return result;
}

BspBounds BspTree::computeRegionBounds(size_t regionIndex, const BspBounds& initialBounds) const {
    if (nodes.empty() || regionIndex >= regions.size()) return BspBounds();
    return computeRegionBoundsRecursive(0, regionIndex, initialBounds);
}

std::shared_ptr<BspRegion> BspTree::findRegionForPoint(float x, float y, float z) const {
    if (nodes.empty()) return nullptr;

    int nodeIdx = 0;
    int depth = 0;
    static int debugCounter = 0;
    debugCounter++;
    bool shouldLog = (debugCounter % 50 == 0);
    bool verboseLog = (debugCounter <= 3);

    while (nodeIdx >= 0 && static_cast<size_t>(nodeIdx) < nodes.size()) {
        const BspNode& node = nodes[nodeIdx];

        if (verboseLog && depth < 20) {
            LOG_TRACE(MOD_MAP, "[BSP] depth={} node={} normal=({},{},{}) dist={} regionId={} left={} right={}",
                depth, nodeIdx, node.normalX, node.normalY, node.normalZ, node.splitDistance, node.regionId, node.left, node.right);
        }

        if (node.regionId > 0 && static_cast<size_t>(node.regionId - 1) < regions.size()) {
            if (GetDebugLevel() >= 2) {
                LOG_DEBUG(MOD_MAP, "[BSP TRAVERSE] Found region {} at depth {} for point ({}, {}, {})",
                    node.regionId - 1, depth, x, y, z);
            }
            return regions[node.regionId - 1];
        }

        float dot = x * node.normalX + y * node.normalY + z * node.normalZ + node.splitDistance;

        if (verboseLog && depth < 20) {
            LOG_TRACE(MOD_MAP, "[BSP]   dot={} -> going {}", dot, (dot >= 0 ? "FRONT" : "BACK"));
        }

        if (dot >= 0) {
            nodeIdx = node.left;
        } else {
            nodeIdx = node.right;
        }
        depth++;
    }

    if (shouldLog && GetDebugLevel() >= 2) {
        LOG_DEBUG(MOD_MAP, "[BSP TRAVERSE] No region found for point ({}, {}, {}) after depth {} (ended at nodeIdx={})",
            x, y, z, depth, nodeIdx);
    }

    return nullptr;
}

size_t BspTree::findRegionIndexForPoint(float x, float y, float z) const {
    if (nodes.empty()) return SIZE_MAX;

    int nodeIdx = 0;

    while (nodeIdx >= 0 && static_cast<size_t>(nodeIdx) < nodes.size()) {
        const BspNode& node = nodes[nodeIdx];

        if (node.regionId > 0 && static_cast<size_t>(node.regionId - 1) < regions.size()) {
            return static_cast<size_t>(node.regionId - 1);
        }

        float dot = x * node.normalX + y * node.normalY + z * node.normalZ + node.splitDistance;

        if (dot >= 0) {
            nodeIdx = node.left;
        } else {
            nodeIdx = node.right;
        }
    }

    return SIZE_MAX;
}

std::optional<ZoneLineInfo> BspTree::checkZoneLine(float x, float y, float z) const {
    static int checkCounter = 0;
    checkCounter++;
    bool shouldLog = (checkCounter % 20 == 0);

    auto region = findRegionForPoint(x, y, z);
    if (region) {
        if (shouldLog && GetDebugLevel() >= 2) {
            std::string typeStr;
            for (RegionType type : region->regionTypes) {
                switch (type) {
                    case RegionType::Normal: typeStr += "Normal "; break;
                    case RegionType::Water: typeStr += "Water "; break;
                    case RegionType::Lava: typeStr += "Lava "; break;
                    case RegionType::Pvp: typeStr += "Pvp "; break;
                    case RegionType::Zoneline: typeStr += "ZONELINE "; break;
                    case RegionType::WaterBlockLOS: typeStr += "WaterBlockLOS "; break;
                    case RegionType::FreezingWater: typeStr += "FreezingWater "; break;
                    case RegionType::Slippery: typeStr += "Slippery "; break;
                    default: typeStr += "Unknown "; break;
                }
            }
            LOG_DEBUG(MOD_MAP, "[BSP CHECK] Point ({}, {}, {}) -> region with {} types: {}",
                x, y, z, region->regionTypes.size(), typeStr);
        }

        for (RegionType type : region->regionTypes) {
            if (type == RegionType::Zoneline && region->zoneLineInfo) {
                LOG_INFO(MOD_MAP, "[BSP CHECK] ZONE LINE FOUND at ({}, {}, {})!", x, y, z);
                return region->zoneLineInfo;
            }
        }
    } else if (shouldLog && GetDebugLevel() >= 2) {
        LOG_DEBUG(MOD_MAP, "[BSP CHECK] Point ({}, {}, {}) -> NO REGION FOUND", x, y, z);
    }
    return std::nullopt;
}

} // namespace Graphics
} // namespace EQT
