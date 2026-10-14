/*
 * B1068 (#388): did the player's movement since the last check cross a zone
 * line, and where does that line send them?
 *
 * Two sources, in this order (the official client's own model first):
 *
 *  1. The zone's BSP: fragment 0x29 region flags whose body string starts
 *     DRNTP / WTNTP / LANTP mark BSP regions as zone lines
 *     (docs/reference/wld_fragments.md). The official client zones the moment
 *     the player ENTERS such a region; an Absolute string carries the target
 *     zone and coordinates, a Reference string (zone id 255) names a zone
 *     point NUMBER that the server's OP_SendZonepoints rows resolve.
 *  2. The curated boxes (data/zone_lines/<zone>.json, data/zone_lines.json)
 *     -- ONLY for a zone whose BSP carries no zone-line region at all.
 *
 * The movement is SWEPT, not point-sampled: the renderer publishes the
 * player's position every ~250 ms (renderer_player_physics.cpp
 * sendPositionUpdateToServer), i.e. every ~10 units at run speed, and a
 * zone-line region or box can be under one unit deep. The segment from the
 * last checked position to this one is walked in <= kSweepStep steps; a jump
 * longer than kTeleportDistance is a warp (zone-in, #goto, a server move), so
 * only its end point is tested.
 *
 * Pure: no client, no network -- tests/test_zone_line_crossing.cpp drives it.
 */
#pragma once

#include "client/zone_bsp.h"
#include "client/zone_lines.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace EQT {

struct ZoneLineCrossing {
    enum class Source : uint8_t { Bsp, Curated };
    Source source = Source::Bsp;
    // The BSP region entered (Source::Bsp), SIZE_MAX for a curated box.
    size_t regionIndex = SIZE_MAX;
    // The point on the swept segment that was inside the line.
    float hitX = 0.0f, hitY = 0.0f, hitZ = 0.0f;
    // Destination in CLIENT format (T52's convention, zone_component.cpp).
    // targetZoneId 0 = "the server decides" (an unresolved Reference: eqemu's
    // Handle_OP_ZoneChange then takes the closest zone point,
    // zone/zoning.cpp ZoneUnsolicited).
    ZoneLineResult result;
    // Reference-type lines: the zone point number the WLD names.
    bool isReference = false;
    uint32_t zonePointNumber = 0;
};

class ZoneLineCrossingFinder {
public:
    static constexpr float kSweepStep = 1.0f;
    static constexpr float kTeleportDistance = 50.0f;
    // The DRNTP field value meaning "keep the player's own coordinate".
    static constexpr float kKeepMarker = 999999.0f;
    static constexpr float kKeepHeading = 999.0f;

    // Number of BSP regions flagged Zoneline that carry a decoded
    // destination. 0 means the zone's BSP has no zone lines and the curated
    // boxes are used instead.
    static size_t CountBspZoneLineRegions(const Graphics::BspTree& tree);

    // Resolve one BSP zone-line destination. `currentZoneId` lets a Reference
    // to a same-zone point (an in-zone teleport row) be recognised.
    static ZoneLineCrossing ResolveBsp(const Graphics::ZoneLineInfo& info, size_t regionIndex,
                                       const ZoneLines* lines,
                                       float curX, float curY, float curZ, float curHeading);

    // The crossing between (fromX..) and (toX..), or nullopt. `tree` may be
    // null; `bspZoneLineRegions` is CountBspZoneLineRegions(*tree), passed in
    // so the caller caches it per zone. `sweep` false = test `to` only.
    static std::optional<ZoneLineCrossing> FindCrossing(const Graphics::BspTree* tree,
                                                size_t bspZoneLineRegions,
                                                const ZoneLines* lines,
                                                float fromX, float fromY, float fromZ,
                                                float toX, float toY, float toZ,
                                                float heading, bool sweep);
};

} // namespace EQT
