/*
 * Zone Object Data — game-state-accessible object/door bounding boxes.
 *
 * Created by D20f1. No graphics dependencies.
 * Structs defined for future collision/pathfinding use.
 */

#pragma once

#include "zone_bsp.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace EQT {

// Axis-aligned bounding box for a placeable zone object (tree, building, rock, etc.)
struct ZoneObjectBounds {
    std::string name;           // Object model name
    float x, y, z;              // World position
    float heading;              // Rotation (degrees)
    Graphics::BspBounds bounds; // AABB in world space
};

// Door types that affect collision behavior
enum class DoorCollisionType : uint8_t {
    Normal = 0,     // Standard door — open/closed AABBs
    Lift = 1,       // Vertical platform — floor moves with Z
    Sliding = 2,    // Slides horizontally
    Revolving = 3,  // Rotates (treated as open/closed only)
    Static = 4      // Designer-placed geometry (tree, building, rock) — always closed
};

// Bounding box data for a door in open and closed states
struct ZoneDoorBounds {
    uint8_t doorId;
    std::string name;
    float x, y, z;              // Base position
    float heading;              // Rotation
    DoorCollisionType type = DoorCollisionType::Normal;

    Graphics::BspBounds closedBounds;  // AABB when door is closed
    Graphics::BspBounds openBounds;    // AABB when door is open
    bool isOpen = false;               // Current state

    // Lift-specific: floor Z range (min = lowest position, max = highest)
    float liftFloorZMin = 0.0f;
    float liftFloorZMax = 0.0f;
    float liftFloorZCurrent = 0.0f;    // Current floor Z position
};

// Light source data for audio emitter placement
struct ZoneLightData {
    float x, y, z;
    float radius;
    uint32_t colorRGBA;         // Packed RGBA color
};

// Complete zone object data for game state layer
struct ZoneObjectData {
    std::vector<ZoneObjectBounds> objects;
    std::map<uint8_t, ZoneDoorBounds> doors;  // Keyed by door ID
    std::vector<ZoneLightData> lights;
};

} // namespace EQT
