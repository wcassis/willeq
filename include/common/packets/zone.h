// Zone change, zone points and door wire layouts.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_ZONE_H
#define EQT_PACKETS_ZONE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

/*
 * Zone Change structure
 * Size: 88 bytes
 */
struct ZoneChange_Struct {
/*000*/ char     char_name[64];
/*064*/ uint16_t zone_id;
/*066*/ uint16_t instance_id;
/*068*/ float    y;
/*072*/ float    x;
/*076*/ float    z;
/*080*/ uint32_t zone_reason;  // 0x0A == death
/*084*/ int32_t  success;      // 0 = client->server request, 1 = server->client response, -X = error
/*088*/
};
static_assert(sizeof(ZoneChange_Struct) == 88, "ZoneChange_Struct must be 88 bytes");  // eqemu titanium_structs.h:1088

/*
 * RequestClientZoneChange_Struct
 * Server requests client to initiate a zone change
 * Size: 24 bytes
 */
struct RequestClientZoneChange_Struct {
/*00*/ uint16_t zone_id;
/*02*/ uint16_t instance_id;
/*04*/ float    y;
/*08*/ float    x;
/*12*/ float    z;
/*16*/ float    heading;
/*20*/ uint32_t type;  // Usually 0x01, exact meaning unknown
/*24*/
};
static_assert(sizeof(RequestClientZoneChange_Struct) == 24, "RequestClientZoneChange_Struct must be 24 bytes");  // eqemu titanium_structs.h:1100

/*
 * Loot Request structure
 * Size: 4 bytes
 */
struct LootRequest_Struct {
/*00*/ uint32_t corpse_id;
/*04*/
};
static_assert(sizeof(LootRequest_Struct) == 4, "LootRequest_Struct must be 4 bytes");  // eqemu zone/client_packet.cpp:10303 demands exactly sizeof(uint32)

/*
 * Zone Point Entry structure (Titanium)
 * Size: 24 bytes
 * Sent in HC_OP_SendZonepoints packet
 */
struct ZonePoint_Entry {
/*0000*/ uint32_t iterator;      // Zone point number/ID
/*0004*/ float    y;             // Target Y coordinate
/*0008*/ float    x;             // Target X coordinate
/*0012*/ float    z;             // Target Z coordinate
/*0016*/ float    heading;       // Target heading
/*0020*/ uint16_t zoneid;        // Target zone ID
/*0022*/ uint16_t zoneinstance;  // Instance ID (LDoN)
/*0024*/
};
static_assert(sizeof(ZonePoint_Entry) == 24, "ZonePoint_Entry must be 24 bytes");  // eqemu titanium_structs.h:1799

/*
 * Zone Points header structure
 * Variable size: 4 + count * sizeof(ZonePoint_Entry)
 */
struct ZonePoints_Header {
/*0000*/ uint32_t count;
/*0004*/ // Followed by count ZonePoint_Entry structures
};

/*
 * Door structure (Titanium)
 * Size: 80 bytes
 * Used in: SpawnDoor packet (array of doors)
 */
struct Door_Struct {
/*0000*/ char     name[32];        // Door model name (matches zone object)
/*0032*/ float    yPos;            // Y position (EQ coords)
/*0036*/ float    xPos;            // X position
/*0040*/ float    zPos;            // Z position
/*0044*/ float    heading;         // Door orientation (EQ 512 format, convert: * 360/512)
/*0048*/ uint32_t incline;         // Open rotation offset (EQ 512 format)
/*0052*/ uint16_t size;            // Scale (100 = normal, 50 = half, 200 = double)
/*0054*/ uint8_t  unknown0054[6];  // Padding
/*0060*/ uint8_t  doorId;          // Door's unique identifier in zone
/*0061*/ uint8_t  opentype;        // Door behavior type (5=door, 55=board, 56=chest, etc.)
/*0062*/ uint8_t  state_at_spawn;  // Initial state (0=closed, 1=open)
/*0063*/ uint8_t  invert_state;    // If 1, door normally spawns open
/*0064*/ uint32_t door_param;      // Lock type / key item ID
/*0068*/ uint8_t  unknown0068[12]; // Padding
/*0080*/
};
static_assert(sizeof(Door_Struct) == 80, "Door_Struct must be 80 bytes");

/*
 * Move Door structure (Titanium)
 * Size: 2 bytes
 * Used in: MoveDoor packet (door state change)
 */
struct MoveDoor_Struct {
/*0000*/ uint8_t doorid;   // Door ID to animate
/*0001*/ uint8_t action;   // 0x02=close, 0x03=open
/*0002*/
};
static_assert(sizeof(MoveDoor_Struct) == 2, "MoveDoor_Struct must be 2 bytes");

/*
 * Click Door structure (Titanium)
 * Size: 16 bytes
 * Used in: ClickDoor packet (client requests door activation)
 */
struct ClickDoor_Struct {
/*0000*/ uint8_t  doorid;          // Door to activate
/*0001*/ uint8_t  unknown001[3];   // Padding
/*0004*/ uint8_t  picklockskill;   // 0 for normal open
/*0005*/ uint8_t  unknown005[3];   // Padding
/*0008*/ uint32_t item_id;         // Key item ID (0 for normal open)
/*0012*/ uint16_t player_id;       // Player's spawn ID
/*0014*/ uint16_t unknown014;      // Padding
/*0016*/
};
static_assert(sizeof(ClickDoor_Struct) == 16, "ClickDoor_Struct must be 16 bytes");


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_ZONE_H
