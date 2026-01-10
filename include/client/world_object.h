#pragma once

#include <cstdint>
#include <string>

namespace eqt {

/**
 * WorldObject - Represents a clickable world object (forge, loom, groundspawn, etc.)
 *
 * World objects are zone objects that players can interact with, including:
 * - Tradeskill containers (forges, looms, ovens, pottery wheels, etc.)
 * - Ground spawns (dropped items, quest objects)
 * - Other interactive objects
 *
 * Parsed from OP_GroundSpawn packets.
 */
struct WorldObject {
    uint32_t drop_id = 0;           // Unique object id for zone (from Object_Struct)
    std::string name;               // Object name (e.g., "IT63_ACTORDEF")
    float x = 0.0f;                 // X position (EQ coordinates)
    float y = 0.0f;                 // Y position (EQ coordinates)
    float z = 0.0f;                 // Z position (EQ coordinates)
    float heading = 0.0f;           // Heading/rotation
    float size = 1.0f;              // Object scale
    uint32_t object_type = 0;       // Object type (determines tradeskill type)
    uint16_t zone_id = 0;           // Zone this object is in
    uint16_t zone_instance = 0;     // Zone instance
    uint32_t incline = 0;           // Rotation/incline
    float tilt_x = 0.0f;            // X-axis tilt
    float tilt_y = 0.0f;            // Y-axis tilt
    uint16_t solid_type = 0;        // Collision type

    // Convenience methods
    bool isTradeskillContainer() const;
    const char* getTradeskillName() const;
};

} // namespace eqt
