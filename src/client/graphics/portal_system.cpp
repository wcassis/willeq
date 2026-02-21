#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

static const std::vector<size_t> kEmptyList;

const std::vector<size_t>& PortalSystem::getPortalsForRegion(size_t regionIdx) const {
    auto it = data_.regionPortals.find(regionIdx);
    if (it != data_.regionPortals.end()) {
        return it->second;
    }
    return emptyList_;
}

size_t PortalSystem::getOtherRegion(size_t portalIdx, size_t fromRegion) const {
    if (portalIdx >= data_.portals.size()) return SIZE_MAX;
    const auto& p = data_.portals[portalIdx];
    if (p.regionA == fromRegion) return p.regionB;
    if (p.regionB == fromRegion) return p.regionA;
    return SIZE_MAX;
}

void PortalSystem::buildFromBsp(const BspTree& bsp,
                                 const std::map<size_t, irr::core::aabbox3df>& regionBoundingBoxes) {
    data_.portals.clear();
    data_.regionPortals.clear();

    if (bsp.regions.empty() || regionBoundingBoxes.empty()) {
        return;
    }

    // Collect region indices that have geometry (mesh nodes)
    std::vector<size_t> regionsWithGeometry;
    for (const auto& [idx, bbox] : regionBoundingBoxes) {
        if (idx < bsp.regions.size() && bsp.regions[idx] && bsp.regions[idx]->containsPolygons) {
            regionsWithGeometry.push_back(idx);
        }
    }

    // Find adjacent region pairs based on mutual PVS visibility and AABB proximity
    const float kMaxGap = 50.0f;         // Max distance between AABBs to consider adjacent
    const float kMinPortalArea = 100.0f;  // Min portal area (sq units)

    for (size_t i = 0; i < regionsWithGeometry.size(); ++i) {
        size_t regionA = regionsWithGeometry[i];
        const auto& regA = bsp.regions[regionA];
        auto bboxAIt = regionBoundingBoxes.find(regionA);
        if (bboxAIt == regionBoundingBoxes.end()) continue;

        for (size_t j = i + 1; j < regionsWithGeometry.size(); ++j) {
            size_t regionB = regionsWithGeometry[j];
            const auto& regB = bsp.regions[regionB];
            auto bboxBIt = regionBoundingBoxes.find(regionB);
            if (bboxBIt == regionBoundingBoxes.end()) continue;

            // Check mutual PVS visibility
            bool aSeesB = (regionB < regA->visibleRegions.size()) && regA->visibleRegions[regionB];
            bool bSeesA = (regionA < regB->visibleRegions.size()) && regB->visibleRegions[regionA];
            if (!aSeesB || !bSeesA) continue;

            // Check AABB proximity: min distance between AABBs
            const auto& bboxA = bboxAIt->second;
            const auto& bboxB = bboxBIt->second;

            float gapX = std::max(0.0f, std::max(bboxA.MinEdge.X - bboxB.MaxEdge.X,
                                                   bboxB.MinEdge.X - bboxA.MaxEdge.X));
            float gapY = std::max(0.0f, std::max(bboxA.MinEdge.Y - bboxB.MaxEdge.Y,
                                                   bboxB.MinEdge.Y - bboxA.MaxEdge.Y));
            float gapZ = std::max(0.0f, std::max(bboxA.MinEdge.Z - bboxB.MaxEdge.Z,
                                                   bboxB.MinEdge.Z - bboxA.MaxEdge.Z));
            float dist = std::sqrt(gapX*gapX + gapY*gapY + gapZ*gapZ);
            if (dist > kMaxGap) continue;

            // Generate portal quad from AABB overlap
            Portal portal;
            if (generatePortalFromOverlap(regionA, regionB, bboxA, bboxB, portal)) {
                if (portal.area >= kMinPortalArea) {
                    size_t portalIdx = data_.portals.size();
                    data_.portals.push_back(portal);
                    data_.regionPortals[regionA].push_back(portalIdx);
                    data_.regionPortals[regionB].push_back(portalIdx);
                }
            }
        }
    }

    LOG_INFO(MOD_GRAPHICS, "Portal system: extracted {} portals from {} regions with geometry",
             data_.portals.size(), regionsWithGeometry.size());
}

bool PortalSystem::generatePortalFromOverlap(
        size_t regionA, size_t regionB,
        const irr::core::aabbox3df& bboxA,
        const irr::core::aabbox3df& bboxB,
        Portal& outPortal) {

    // Expand AABBs slightly to catch touching edges
    const float margin = 5.0f;

    float overlapMinX = std::max(bboxA.MinEdge.X - margin, bboxB.MinEdge.X - margin);
    float overlapMaxX = std::min(bboxA.MaxEdge.X + margin, bboxB.MaxEdge.X + margin);
    float overlapMinY = std::max(bboxA.MinEdge.Y - margin, bboxB.MinEdge.Y - margin);
    float overlapMaxY = std::min(bboxA.MaxEdge.Y + margin, bboxB.MaxEdge.Y + margin);
    float overlapMinZ = std::max(bboxA.MinEdge.Z - margin, bboxB.MinEdge.Z - margin);
    float overlapMaxZ = std::min(bboxA.MaxEdge.Z + margin, bboxB.MaxEdge.Z + margin);

    // Must overlap in all three axes
    if (overlapMinX >= overlapMaxX || overlapMinY >= overlapMaxY || overlapMinZ >= overlapMaxZ) {
        return false;
    }

    float extentX = overlapMaxX - overlapMinX;
    float extentY = overlapMaxY - overlapMinY;
    float extentZ = overlapMaxZ - overlapMinZ;

    // The portal lies on the thinnest axis of the overlap region (the separating plane)
    // EQ coords: Z is up, X/Y are horizontal
    outPortal.regionA = regionA;
    outPortal.regionB = regionB;

    float centerAX = (bboxA.MinEdge.X + bboxA.MaxEdge.X) * 0.5f;
    float centerAY = (bboxA.MinEdge.Y + bboxA.MaxEdge.Y) * 0.5f;
    float centerBX = (bboxB.MinEdge.X + bboxB.MaxEdge.X) * 0.5f;
    float centerBY = (bboxB.MinEdge.Y + bboxB.MaxEdge.Y) * 0.5f;

    if (extentX <= extentY && extentX <= extentZ) {
        // Separating plane is perpendicular to X axis
        float portalX = (overlapMinX + overlapMaxX) * 0.5f;
        // Portal quad in YZ plane
        outPortal.vertices[0][0] = portalX; outPortal.vertices[0][1] = overlapMinY; outPortal.vertices[0][2] = overlapMinZ;
        outPortal.vertices[1][0] = portalX; outPortal.vertices[1][1] = overlapMaxY; outPortal.vertices[1][2] = overlapMinZ;
        outPortal.vertices[2][0] = portalX; outPortal.vertices[2][1] = overlapMaxY; outPortal.vertices[2][2] = overlapMaxZ;
        outPortal.vertices[3][0] = portalX; outPortal.vertices[3][1] = overlapMinY; outPortal.vertices[3][2] = overlapMaxZ;
        // Normal points from A toward B
        outPortal.normalX = (centerBX > centerAX) ? 1.0f : -1.0f;
        outPortal.normalY = 0.0f;
        outPortal.normalZ = 0.0f;
        outPortal.area = extentY * extentZ;
    } else if (extentY <= extentX && extentY <= extentZ) {
        // Separating plane is perpendicular to Y axis
        float portalY = (overlapMinY + overlapMaxY) * 0.5f;
        // Portal quad in XZ plane
        outPortal.vertices[0][0] = overlapMinX; outPortal.vertices[0][1] = portalY; outPortal.vertices[0][2] = overlapMinZ;
        outPortal.vertices[1][0] = overlapMaxX; outPortal.vertices[1][1] = portalY; outPortal.vertices[1][2] = overlapMinZ;
        outPortal.vertices[2][0] = overlapMaxX; outPortal.vertices[2][1] = portalY; outPortal.vertices[2][2] = overlapMaxZ;
        outPortal.vertices[3][0] = overlapMinX; outPortal.vertices[3][1] = portalY; outPortal.vertices[3][2] = overlapMaxZ;
        outPortal.normalX = 0.0f;
        outPortal.normalY = (centerBY > centerAY) ? 1.0f : -1.0f;
        outPortal.normalZ = 0.0f;
        outPortal.area = extentX * extentZ;
    } else {
        // Separating plane is perpendicular to Z axis (vertical portal — rare but possible for stacked rooms)
        float portalZ = (overlapMinZ + overlapMaxZ) * 0.5f;
        float centerAZ = (bboxA.MinEdge.Z + bboxA.MaxEdge.Z) * 0.5f;
        float centerBZ = (bboxB.MinEdge.Z + bboxB.MaxEdge.Z) * 0.5f;
        // Portal quad in XY plane
        outPortal.vertices[0][0] = overlapMinX; outPortal.vertices[0][1] = overlapMinY; outPortal.vertices[0][2] = portalZ;
        outPortal.vertices[1][0] = overlapMaxX; outPortal.vertices[1][1] = overlapMinY; outPortal.vertices[1][2] = portalZ;
        outPortal.vertices[2][0] = overlapMaxX; outPortal.vertices[2][1] = overlapMaxY; outPortal.vertices[2][2] = portalZ;
        outPortal.vertices[3][0] = overlapMinX; outPortal.vertices[3][1] = overlapMaxY; outPortal.vertices[3][2] = portalZ;
        outPortal.normalX = 0.0f;
        outPortal.normalY = 0.0f;
        outPortal.normalZ = (centerBZ > centerAZ) ? 1.0f : -1.0f;
        outPortal.area = extentX * extentY;
    }

    // Compute center
    outPortal.centerX = outPortal.centerY = outPortal.centerZ = 0.0f;
    for (int i = 0; i < 4; ++i) {
        outPortal.centerX += outPortal.vertices[i][0];
        outPortal.centerY += outPortal.vertices[i][1];
        outPortal.centerZ += outPortal.vertices[i][2];
    }
    outPortal.centerX *= 0.25f;
    outPortal.centerY *= 0.25f;
    outPortal.centerZ *= 0.25f;

    return true;
}

} // namespace Graphics
} // namespace EQT
