#pragma once

#include "client/door_state.h"
#include "client/zone_lines.h"  // ZoneLineBoundingBox
#include <string>
#include <vector>
#include <cstdint>

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

    // Zone map (read-only pointer — owned by EverQuest, valid during loading)
    HCMap* zoneMap = nullptr;

    // Asset path
    std::string eqClientPath;
};

} // namespace eqt
