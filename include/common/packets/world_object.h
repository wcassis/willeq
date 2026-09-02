// World object and tradeskill wire layouts, plus track, resurrect, /who and
// inspect.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_WORLD_OBJECT_H
#define EQT_PACKETS_WORLD_OBJECT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

// ============================================================================
// World Object / Tradeskill Structures (from titanium_structs.h)
// ============================================================================

/**
 * Object Struct
 * Used for forges, ovens, ground spawns, items dropped to ground, etc.
 * Size: 92 bytes (Titanium wire format)
 * Used in: OP_GroundSpawn packet (server->client)
 *
 * T144: this was 100 bytes and wrong from offset 8 onwards. It had been
 * derived from eqemu's SERVER-INTERNAL Object_Struct
 * (common/eq_packet_structs.h), which carries size/solid_type/incline/
 * tilt_x/tilt_y -- fields that do not exist in the Titanium wire format at
 * all -- plus an invented padding014. The authority is
 * Titanium::structs::Object_Struct (common/patches/titanium_structs.h) as
 * populated field-by-field by ENCODE(OP_GroundSpawn) in
 * common/patches/titanium.cpp. Confirmed against captured bytes from a live
 * qeynos session; see review/T144/ and tests/test_packet_structs.cpp's
 * GroundSpawn captured-bytes cases.
 *
 * NOTE: OP_GroundSpawn is a VARIABLE-LENGTH packet. ENCODE sizes it
 * `strlen(object_name) + sizeof(Object_Struct) - 1`, i.e. strlen + 91, and
 * only strcpy's the name -- the rest of the 32-byte object_name field is
 * whatever was in the freshly-allocated (uninitialised) buffer. Never
 * length-check an OP_GroundSpawn against sizeof(Object_Struct); check only
 * that it reaches the field you are about to read.
 */
struct Object_Struct {
/*0000*/ uint32_t linked_list_addr[2];  // Prev and next pointers (client-side linked list)
/*0008*/ uint16_t unknown008[2];        // Unknown (zeroed by the server)
/*0012*/ uint32_t drop_id;              // Unique object id for zone (an ENTITY id, not the DB object id)
/*0016*/ uint16_t zone_id;              // Zone the object appears in
/*0018*/ uint16_t zone_instance;        // Zone instance
/*0020*/ uint32_t unknown020;           // Unknown (zeroed by the server)
/*0024*/ uint32_t unknown024;           // Unknown (zeroed by the server)
/*0028*/ float    heading;              // Heading/rotation (EQ 512 format)
/*0032*/ float    z;                    // Z coordinate
/*0036*/ float    x;                    // X coordinate
/*0040*/ float    y;                    // Y coordinate
/*0044*/ char     object_name[32];      // Object name (e.g., "IT63_ACTORDEF"); only strlen+1 bytes are meaningful
/*0076*/ uint32_t unknown076;           // Unknown (zeroed by the server)
/*0080*/ uint32_t object_type;          // Type of object (tradeskill container type, etc.)
/*0084*/ uint32_t unknown084;           // Set to 0xFFFFFFFF by the server
/*0088*/ uint32_t spawn_id;             // Spawn id of client interacting with object (0 from the server)
/*0092*/
};
static_assert(sizeof(Object_Struct) == 92, "Object_Struct must be 92 bytes");

/**
 * Click Object Struct
 * Client clicking on a zone object (forge, groundspawn, etc.)
 * Size: 8 bytes
 * Used in: OP_ClickObject packet (client->server)
 */
struct ClickObject_Struct {
/*0000*/ uint32_t drop_id;      // Unique object id for zone (from Object_Struct)
/*0004*/ uint32_t player_id;    // Player's spawn ID
/*0008*/
};
static_assert(sizeof(ClickObject_Struct) == 8, "ClickObject_Struct must be 8 bytes");

/**
 * Click Object Action Struct
 * Server response to client clicking on a World Container (forge, loom, etc.)
 * Also sent by client when they close the container.
 * Size: 92 bytes
 * Used in: OP_ClickObjectAction packet (server->client, client->server for close)
 */
struct ClickObjectAction_Struct {
/*0000*/ uint32_t player_id;        // Entity ID of player who clicked object
/*0004*/ uint32_t drop_id;          // Zone-specified unique object identifier
/*0008*/ uint32_t open;             // 1=opening, 0=closing
/*0012*/ uint32_t type;             // Object type (see TradeskillContainerType)
/*0016*/ uint32_t unknown16;        // Set to 0xA
/*0020*/ uint32_t icon;             // Icon to display for tradeskill containers
/*0024*/ uint32_t unknown24;        // Unknown
/*0028*/ char     object_name[64];  // Object name to display
/*0092*/
};
static_assert(sizeof(ClickObjectAction_Struct) == 92, "ClickObjectAction_Struct must be 92 bytes");

/**
 * New Combine Struct
 * Client requesting to perform a tradeskill combine
 * Size: 4 bytes
 * Used in: OP_TradeSkillCombine packet (client->server)
 */
struct NewCombine_Struct {
/*0000*/ int16_t container_slot;     // 1000 for world container, or bag slot ID
/*0002*/ int16_t guildtribute_slot;  // Usually 0
/*0004*/
};
static_assert(sizeof(NewCombine_Struct) == 4, "NewCombine_Struct must be 4 bytes");

/**
 * Apply Poison Struct
 * Used for applying poison to weapons (Rogue skill)
 * Size: 8 bytes
 * Used in: OP_ApplyPoison packet (bidirectional)
 */
struct ApplyPoison_Struct {
/*0000*/ uint32_t inventorySlot;  // Inventory slot of the poison item
/*0004*/ uint32_t success;        // 0 = failed, 1 = success (set by server in response)
/*0008*/
};
static_assert(sizeof(ApplyPoison_Struct) == 8, "ApplyPoison_Struct must be 8 bytes");

/**
 * Track Entry Struct
 * Single entry in tracking results
 * Size: 8 bytes (Titanium format)
 */
struct Track_Struct {
/*0000*/ uint32_t entityid;     // Spawn ID of trackable entity
/*0004*/ float distance;        // Distance to entity
/*0008*/
};
static_assert(sizeof(Track_Struct) == 8, "Track_Struct must be 8 bytes");

/**
 * Resurrect Struct
 * Used for resurrection offers and responses
 * Size: 228 bytes (Titanium format)
 * Used in: OP_RezzRequest (S->C), OP_RezzAnswer (C->S), OP_RezzComplete (S->C)
 */
// T134: this struct was missing a 4-byte field (unknown020) right after z,
// found live -- a real OP_RezzRequest packet captured from the eqemu zone
// log had "Testmelee" (your_name) starting at byte offset 24, not 20 as
// this struct assumed, shifting every field after z by 4 bytes. Confirmed
// byte-for-byte against eqemu's own Resurrect_Struct
// (common/eq_packet_structs.h): total size 228, not 224. Before this fix,
// every resurrection offer parsed with an empty rezzer_name, a garbled
// corpse_name, and spellid=0 (LOG_INFO's "Resurrection offer from  for
// corpse '<garbage>' (spell 0)" was this bug, not a formatting issue), and
// every OP_RezzAnswer sent back had `action` written at the wrong byte
// offset, so the server could not reliably tell accept from decline.
struct Resurrect_Struct {
/*0000*/ uint32_t unknown00;          // Unknown, always 0
/*0004*/ uint16_t zone_id;            // Zone to resurrect in
/*0006*/ uint16_t instance_id;        // Instance ID (0 for non-instanced)
/*0008*/ float    y;                  // Y coordinate to rez at
/*0012*/ float    x;                  // X coordinate to rez at
/*0016*/ float    z;                  // Z coordinate to rez at
/*0020*/ uint32_t unknown020;         // Unknown, always 0 (T134: the missing field)
/*0024*/ char     your_name[64];      // Name of the person being rezzed
/*0088*/ uint32_t unknown88;          // Unknown
/*0092*/ char     rezzer_name[64];    // Name of the person casting rez
/*0156*/ uint32_t spellid;            // Spell ID of the resurrection spell
/*0160*/ char     corpse_name[64];    // Name of the corpse (with "'s corpse")
/*0224*/ uint32_t action;             // 0=decline, 1=accept (in OP_RezzAnswer)
/*0228*/
};
static_assert(sizeof(Resurrect_Struct) == 228, "Resurrect_Struct must be 228 bytes");

/**
 * Who All Request Struct
 * Used to request /who results
 * Size: 152 bytes (Titanium format)
 * Used in: OP_WhoAllRequest (C->S)
 */
struct Who_All_Struct {
/*0000*/ char     whom[64];       // Name filter (empty = all)
/*0064*/ uint32_t wrace;          // Race filter (0xFFFFFFFF = all)
/*0068*/ uint32_t wclass;         // Class filter (0xFFFFFFFF = all)
/*0072*/ uint32_t lvllow;         // Minimum level (0xFFFFFFFF = no min)
/*0076*/ uint32_t lvlhigh;        // Maximum level (0xFFFFFFFF = no max)
/*0080*/ uint32_t gmlookup;       // GM lookup flag (0xFFFFFFFF = normal)
/*0084*/ uint32_t unknown084;     // Unknown
/*0088*/ uint8_t  unknown088[64]; // Padding
/*0152*/
};
static_assert(sizeof(Who_All_Struct) == 152, "Who_All_Struct must be 152 bytes");

/**
 * Who All Return Header
 * Header for /who response (followed by variable player entries)
 * Used in: OP_WhoAllResponse (S->C)
 */
struct WhoAllReturnHeader {
/*0000*/ uint32_t id;                   // Unknown
/*0004*/ uint32_t playerineqstring;     // String ID for "Players in EverQuest"
/*0008*/ char     line[27];             // Header text
/*0035*/ uint8_t  unknown035;           // Padding (0x0A)
/*0036*/ uint32_t unknown036;           // Unknown (0)
/*0040*/ uint32_t playersinzonestring;  // String ID for "Players in zone"
/*0044*/ uint32_t unknown044[2];        // Unknown (0s)
/*0052*/ uint32_t unknown052;           // Unknown (1)
/*0056*/ uint32_t unknown056;           // Unknown (1)
/*0060*/ uint32_t playercount;          // Number of players in response
/*0064*/
};
static_assert(sizeof(WhoAllReturnHeader) == 64, "WhoAllReturnHeader must be 64 bytes");

/**
 * Inspect Request Struct
 * Size: 8 bytes (Titanium format)
 * Used in: OP_InspectRequest (C->S when we inspect, S->C when we are inspected)
 *
 * Ground truth: titanium_structs.h:1969. NOTE that eqemu's INTERNAL
 * Inspect_Struct (eq_packet_structs.h:2687) is two uint16s, i.e. 4 bytes, and
 * Handle_OP_InspectRequest length-checks against THAT. It is not a
 * contradiction and this struct is not wrong: titanium.cpp's
 * DECODE(OP_InspectRequest) converts 8 -> 4 on the way in and
 * ENCODE(OP_InspectRequest) converts 4 -> 8 on the way out. Do not "fix" this
 * to 4 bytes -- that would break the wire.
 */
struct Inspect_Struct {
/*0000*/ uint32_t TargetID;    // Entity ID of the player being inspected
/*0004*/ uint32_t PlayerID;    // Entity ID of the player doing the inspecting
/*0008*/
};
static_assert(sizeof(Inspect_Struct) == 8, "Inspect_Struct must be 8 bytes");

/**
 * Inspect Response Struct
 * Size: 1792 bytes (Titanium format)
 * Used in: OP_InspectAnswer (C->S when we answer, S->C when we receive one)
 *
 * ⚠ THE TWO ID FIELDS MEAN THE OPPOSITE OF Inspect_Struct'S. T170 found the
 * receive path reading them the request's way, which named the wrong player.
 * Ground truth is eqemu's own construction of this packet,
 * Client::ProcessInspectRequest (zone/client.cpp:6590):
 *
 *     insr->TargetID = requester->GetID();   // who ASKED -- the recipient
 *     insr->playerid = requestee->GetID();   // who is being INSPECTED
 *
 * TargetID is a ROUTING field: Handle_OP_InspectAnswer
 * (zone/client_packet.cpp:9020) looks it up to decide whom to forward the
 * answer to. The inspected player is `playerid`. Named as eqemu names them,
 * lower-case `playerid` included, so the asymmetry stays visible.
 *
 * ⚠ `text` IS PERSISTED BY THE SERVER. Handle_OP_InspectAnswer memcpy()s it
 * over the character's inspect message and calls SaveCharacterInspectMessage()
 * -- unconditionally, on every answer. Whatever a client puts here BECOMES the
 * character's stored inspect message. See EverQuest::SendInspectAnswer().
 */
struct InspectResponse_Struct {
/*0000*/ uint32_t TargetID;           // ANSWER: the requester (routing target)
/*0004*/ uint32_t playerid;           // ANSWER: the player being inspected
/*0008*/ char     itemnames[22][64];  // Equipment slot names (22 slots * 64 chars)
/*1416*/ uint32_t itemicons[22];      // Equipment slot icon IDs
/*1504*/ char     text[288];          // Inspect message -- SERVER PERSISTS THIS
/*1792*/
};
static_assert(sizeof(InspectResponse_Struct) == 1792, "InspectResponse_Struct must be 1792 bytes");


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_WORLD_OBJECT_H
