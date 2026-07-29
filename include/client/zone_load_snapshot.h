#pragma once

#include "client/door_state.h"
#include "client/zone_lines.h"  // ZoneLineBoundingBox
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

struct Entity;  // Forward declaration (defined in eq.h)
class HCMap;

namespace eqt {

/**
 * ZoneLoadSnapshot — immutable copy of game state for zone loading.
 *
 * Created at the start of zone loading (before StartLoadingThread).
 * The loading thread reads from this snapshot instead of live game state.
 * Any game state changes during loading are captured by bridge events
 * and applied after JoinLoadingThread via ProcessBridgeEvents.
 */
struct ZoneLoadSnapshot {
    // Zone identity
    std::string zoneName;
    uint16_t zoneId = 0;

    // Player position
    float playerX = 0, playerY = 0, playerZ = 0;
    float playerHeading = 0;
    uint16_t playerSpawnId = 0;
    std::string playerName;
    uint8_t playerLevel = 0;

    // Zone environment
    uint8_t skyType = 0;
    uint8_t zoneType = 0;
    uint8_t fogRed[4] = {0};
    uint8_t fogGreen[4] = {0};
    uint8_t fogBlue[4] = {0};
    float fogMinClip[4] = {0};
    float fogMaxClip[4] = {0};

    // Entities (copy of game state at snapshot time)
    std::vector<Entity> entities;

    // Doors (copy of game state at snapshot time)
    std::vector<EQT::DoorState> doors;

    // Zone line bounding boxes (pre-computed from zone_lines)
    std::vector<EQT::ZoneLineBoundingBox> zoneLineBBoxes;

    // Zone map (P35: shared ownership — was a raw "owned by EverQuest,
    // valid during loading" pointer; a shared_ptr copy keeps the object
    // alive for the loading thread even if EverQuest's own m_zone_map is
    // reset by a subsequent zone change before this snapshot is consumed)
    std::shared_ptr<const HCMap> zoneMap;

    // P04: zoneArchive/zoneWld ("pre-parsed zone sources") removed — never read
    // by any consumer (the sequential loader parses its own S3D copy in
    // loadZoneStep1_S3D), and holding the shared_ptrs kept the entire
    // compressed .s3d + game-side WLD parse artifacts alive past their need.
    size_t playerBspRegion = SIZE_MAX;

    // Asset path
    std::string eqClientPath;
};

} // namespace eqt
