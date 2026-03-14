/*
 * Zone BSP Tree implementation.
 * Moved from src/client/graphics/eq/wld_loader.cpp (D20f1).
 */

#include "client/zone_bsp.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

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
