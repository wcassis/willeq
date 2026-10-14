// B1068 (#388): swept zone-line crossing, BSP regions first. See the header.
#include "client/zone_line_crossing.h"

#include <algorithm>
#include <cmath>

namespace EQT {

namespace {

bool isKeep(float v) {
    return std::fabs(v) >= ZoneLineCrossingFinder::kKeepMarker - 1.0f;
}

bool regionIsZoneLine(const Graphics::BspRegion& region) {
    if (!region.zoneLineInfo) {
        return false;
    }
    return std::find(region.regionTypes.begin(), region.regionTypes.end(),
                     Graphics::RegionType::Zoneline) != region.regionTypes.end();
}

} // namespace

size_t ZoneLineCrossingFinder::CountBspZoneLineRegions(const Graphics::BspTree& tree) {
    size_t n = 0;
    for (const auto& region : tree.regions) {
        if (region && regionIsZoneLine(*region)) {
            ++n;
        }
    }
    return n;
}

ZoneLineCrossing ZoneLineCrossingFinder::ResolveBsp(const Graphics::ZoneLineInfo& info,
                                                    size_t regionIndex, const ZoneLines* lines,
                                                    float curX, float curY, float curZ,
                                                    float curHeading) {
    ZoneLineCrossing c;
    c.source = ZoneLineCrossing::Source::Bsp;
    c.regionIndex = regionIndex;
    c.result.isZoneLine = true;
    c.result.heading = curHeading;

    if (info.type == Graphics::ZoneLineType::Absolute) {
        // The string's coordinates are CLIENT order (qeynos's
        // DRNTP00002-00155-00007... lands at x=-155 y=-7 in qeynos2, beside
        // the server's own zone_points row qeynos #1 -> (-5,-151) in server
        // order). 999999 / 999 keep the player's own value.
        c.result.targetZoneId = info.zoneId;
        c.result.targetX = isKeep(info.x) ? curX : info.x;
        c.result.targetY = isKeep(info.y) ? curY : info.y;
        c.result.targetZ = isKeep(info.z) ? curZ : info.z;
        c.result.heading = (info.heading >= kKeepHeading) ? curHeading : info.heading;
        return c;
    }

    c.isReference = true;
    c.zonePointNumber = info.zonePointIndex;
    c.result.zonePointIndex = info.zonePointIndex;
    std::optional<ZonePoint> zp = lines ? lines->getZonePoint(info.zonePointIndex) : std::nullopt;
    if (!zp) {
        // The server has the row even when we never received it: send zone
        // id 0 and let Handle_OP_ZoneChange pick the closest zone point.
        c.result.needsServerLookup = true;
        c.result.targetZoneId = 0;
        c.result.targetX = curX;
        c.result.targetY = curY;
        c.result.targetZ = curZ;
        return c;
    }
    // OP_SendZonepoints rows are SERVER order (entities_component.cpp
    // ZoneProcessSendZonepoints stores wire x/y as-is); client x = server y.
    c.result.targetZoneId = zp->targetZoneId;
    c.result.targetX = isKeep(zp->targetY) ? curX : zp->targetY;
    c.result.targetY = isKeep(zp->targetX) ? curY : zp->targetX;
    c.result.targetZ = isKeep(zp->targetZ) ? curZ : zp->targetZ;
    c.result.heading = zp->heading;
    return c;
}

std::optional<ZoneLineCrossing> ZoneLineCrossingFinder::FindCrossing(const Graphics::BspTree* tree,
                                                             size_t bspZoneLineRegions,
                                                             const ZoneLines* lines,
                                                             float fromX, float fromY, float fromZ,
                                                             float toX, float toY, float toZ,
                                                             float heading, bool sweep) {
    const bool useBsp = tree && bspZoneLineRegions > 0;
    const bool useCurated = !useBsp && lines && lines->hasZoneLines();
    if (!useBsp && !useCurated) {
        return std::nullopt;
    }

    const float dx = toX - fromX, dy = toY - fromY, dz = toZ - fromZ;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    int steps = 1;
    if (sweep && len <= kTeleportDistance) {
        steps = std::max(1, static_cast<int>(std::ceil(len / kSweepStep)));
    }
    // i = 1..steps: the start point was already tested by the previous call.
    for (int i = 1; i <= steps; ++i) {
        const float t = (steps == 1) ? 1.0f : static_cast<float>(i) / static_cast<float>(steps);
        const float x = fromX + dx * t, y = fromY + dy * t, z = fromZ + dz * t;
        if (useBsp) {
            const size_t idx = tree->findRegionIndexForPoint(x, y, z);
            if (idx == SIZE_MAX || idx >= tree->regions.size() || !tree->regions[idx]) {
                continue;
            }
            const Graphics::BspRegion& region = *tree->regions[idx];
            if (!regionIsZoneLine(region)) {
                continue;
            }
            ZoneLineCrossing c = ResolveBsp(*region.zoneLineInfo, idx, lines, toX, toY, toZ, heading);
            c.hitX = x; c.hitY = y; c.hitZ = z;
            return c;
        }
        ZoneLineResult r = lines->checkPosition(x, y, z, toX, toY, toZ);
        if (r.isZoneLine) {
            ZoneLineCrossing c;
            c.source = ZoneLineCrossing::Source::Curated;
            c.result = r;
            c.hitX = x; c.hitY = y; c.hitZ = z;
            return c;
        }
    }
    return std::nullopt;
}

} // namespace EQT
