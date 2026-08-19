#pragma once

#include <cstddef>
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
    uint32_t object_type = 0;       // Object type (determines tradeskill type)
    uint16_t zone_id = 0;           // Zone this object is in
    uint16_t zone_instance = 0;     // Zone instance
    // T144: size/solid_type/incline/tilt_x/tilt_y used to live here. They were
    // read from Object_Struct offsets that do not exist in the Titanium wire
    // format -- they came from eqemu's server-internal struct -- so every one
    // of them was noise. None had a consumer anywhere in the codebase, so they
    // are gone rather than "fixed". Do not re-add them: OP_GroundSpawn carries
    // no scale, collision or tilt data.

    // Convenience methods
    bool isTradeskillContainer() const;
    const char* getTradeskillName() const;
};

/**
 * Parse an OP_GroundSpawn body (the packet WITHOUT its 2-byte opcode prefix)
 * into a WorldObject, using the Titanium wire layout of EQT::Object_Struct.
 *
 * T144: this lives here, as a free function over a byte range, rather than
 * inline in EverQuest::ZoneProcessGroundSpawn, so the layout can be pinned by
 * a captured-bytes regression test (tests/test_packet_structs.cpp) instead of
 * only by a sizeof() static_assert. The struct passed its static_assert for
 * as long as the bug was live; only real bytes catch this class of defect.
 *
 * OP_GroundSpawn is variable length: eqemu emits strlen(object_name) + 91
 * body bytes, and everything in the 32-byte object_name field past the
 * terminating NUL is uninitialised heap. Returns false (leaving @p out
 * untouched) if the body is too short to reach object_type at offset 80.
 */
bool ParseGroundSpawn(const uint8_t* body, size_t body_len, WorldObject& out);

/// Body bytes required to reach the end of Object_Struct::object_type (84).
size_t GroundSpawnMinBodyBytes();

} // namespace eqt
