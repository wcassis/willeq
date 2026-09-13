// Dueling, skills, tradeskill recipe, cosmetic, phase 4 miscellaneous, GM
// command and petition wire layouts, plus the position bitfield helpers.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_GM_H
#define EQT_PACKETS_GM_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

// ============================================================================
// Phase 4: Dueling Structures
// ============================================================================

/**
 * Duel Struct
 * Used for duel requests and acceptance
 * Size: 8 bytes (Titanium format)
 * Used in: OP_RequestDuel (Bidirectional), OP_DuelAccept (C->S)
 */
struct Duel_Struct {
/*0000*/ uint32_t duel_initiator;     // ID of initiator
/*0004*/ uint32_t duel_target;        // ID of target
/*0008*/
};
static_assert(sizeof(Duel_Struct) == 8, "Duel_Struct must be 8 bytes");

/**
 * Duel Response Struct
 * Used for declining duels
 * Size: 12 bytes (Titanium format)
 * Used in: OP_DuelDecline (C->S)
 */
struct DuelResponse_Struct {
/*0000*/ uint32_t target_id;          // Player being declined
/*0004*/ uint32_t entity_id;          // Initiator of the duel
/*0008*/ uint32_t unknown;            // Unknown purpose
/*0012*/
};
static_assert(sizeof(DuelResponse_Struct) == 12, "DuelResponse_Struct must be 12 bytes");

// ============================================================================
// Phase 4: Skills Structures
// ============================================================================

/**
 * Bind Wound Struct
 * Used for bind wound skill
 * Size: 8 bytes (Titanium format)
 * Used in: OP_BindWound (C->S)
 */
struct BindWound_Struct {
/*0000*/ uint16_t to;                 // Target ID
/*0002*/ uint16_t unknown2;           // Placeholder
/*0004*/ uint16_t type;               // Bind wound type
/*0006*/ uint16_t unknown6;           // Unknown
/*0008*/
};
static_assert(sizeof(BindWound_Struct) == 8, "BindWound_Struct must be 8 bytes");

// ============================================================================
// Phase 4: Tradeskill Recipe Structures
// ============================================================================

/**
 * Recipe Search Struct
 * Used for searching recipes
 * Size: 80 bytes (Titanium format)
 * Used in: OP_RecipesSearch (C->S)
 */
struct RecipesSearch_Struct {
/*0000*/ uint32_t object_type;        // Object type
/*0004*/ uint32_t some_id;            // Object ID
/*0008*/ uint32_t mintrivial;         // Minimum trivial
/*0012*/ uint32_t maxtrivial;         // Maximum trivial
/*0016*/ char     query[56];          // Search query string
/*0072*/ uint32_t unknown4;           // Set to 0x00030000
/*0076*/ uint32_t unknown5;           // Set to 0x0012DD4C
/*0080*/
};
static_assert(sizeof(RecipesSearch_Struct) == 80, "RecipesSearch_Struct must be 80 bytes");

/**
 * Recipe Reply Struct
 * Used for recipe search results
 * Size: 84 bytes (Titanium format)
 * Used in: OP_RecipeReply (S->C)
 */
struct RecipeReply_Struct {
/*0000*/ uint32_t object_type;        // Object type
/*0004*/ uint32_t some_id;            // Object ID
/*0008*/ uint32_t component_count;    // Number of components
/*0012*/ uint32_t recipe_id;          // Recipe ID
/*0016*/ uint32_t trivial;            // Trivial value
/*0020*/ char     recipe_name[64];    // Recipe name
/*0084*/
};
static_assert(sizeof(RecipeReply_Struct) == 84, "RecipeReply_Struct must be 84 bytes");

/**
 * Recipe Auto-Combine Struct
 * Used for auto-combining recipes
 * Size: 20 bytes (Titanium format)
 * Used in: OP_RecipeAutoCombine (Bidirectional)
 */
struct RecipeAutoCombine_Struct {
/*0000*/ uint32_t object_type;        // Object type
/*0004*/ uint32_t some_id;            // Object ID
/*0008*/ uint32_t unknown1;           // Echoed in reply
/*0012*/ uint32_t recipe_id;          // Recipe ID
/*0016*/ uint32_t reply_code;         // Request: 0xe16493, Reply: 0=success, 0xfffff5=failure
/*0020*/
};
static_assert(sizeof(RecipeAutoCombine_Struct) == 20, "RecipeAutoCombine_Struct must be 20 bytes");

// ============================================================================
// Phase 4: Cosmetic Structures
// ============================================================================

/**
 * Surname Struct
 * Used for setting surname
 * Size: 100 bytes (Titanium format)
 * Used in: OP_Surname (C->S)
 */
struct Surname_Struct {
/*0000*/ char     name[64];           // Character name
/*0064*/ uint32_t unknown0064;        // Unknown
/*0068*/ char     lastname[32];       // Surname to set
/*0100*/
};
static_assert(sizeof(Surname_Struct) == 100, "Surname_Struct must be 100 bytes");

/**
 * Face Change Struct
 * Used for changing face appearance
 * Size: 7 bytes (Titanium format -- NOT padded to 8)
 * Used in: OP_FaceChange (C->S)
 *
 * Ground truth: eqemu Titanium::structs::FaceChange_Struct
 * (common/patches/titanium_structs.h:1860) -- seven uint8 fields, no padding.
 * titanium.cpp has DECODE(OP_FaceChange) (:2779) using DECODE_LENGTH_EXACT,
 * so the Titanium struct (7) is the wire format, NOT the 24-byte base
 * eq_packet_structs.h version (which carries Drakkin/entity_id fields that
 * Titanium never sends). A mismatch makes DECODE_LENGTH_EXACT rewrite the
 * opcode to OP_Unknown, so the packet is discarded before any handler runs.
 * (T145: we previously invented an 8th `unused` padding byte.)
 */
struct FaceChange_Struct {
/*0000*/ uint8_t haircolor;           // Hair color
/*0001*/ uint8_t beardcolor;          // Beard color
/*0002*/ uint8_t eyecolor1;           // Left eye color
/*0003*/ uint8_t eyecolor2;           // Right eye color
/*0004*/ uint8_t hairstyle;           // Hair style
/*0005*/ uint8_t beard;               // Beard style
/*0006*/ uint8_t face;                // Face type
/*0007*/
};
static_assert(sizeof(FaceChange_Struct) == 7, "FaceChange_Struct must be 7 bytes");

// ============================================================================
// Phase 4: Misc Structures
// ============================================================================

/**
 * Random Request Struct
 * Used for /random command
 * Size: 8 bytes (Titanium format)
 * Used in: OP_RandomReq (C->S)
 */
struct RandomReq_Struct {
/*0000*/ uint32_t low;                // Minimum value
/*0004*/ uint32_t high;               // Maximum value
/*0008*/
};
static_assert(sizeof(RandomReq_Struct) == 8, "RandomReq_Struct must be 8 bytes");

/**
 * Random Reply Struct
 * Used for /random result
 * Size: 76 bytes (Titanium format)
 * Used in: OP_RandomReply (S->C)
 */
struct RandomReply_Struct {
/*0000*/ uint32_t low;                // Minimum value
/*0004*/ uint32_t high;               // Maximum value
/*0008*/ uint32_t result;             // Random result
/*0012*/ char     name[64];           // Player name
/*0076*/
};
static_assert(sizeof(RandomReply_Struct) == 76, "RandomReply_Struct must be 76 bytes");

/**
 * Find Person Point
 * Used in find person path
 * Size: 12 bytes
 */
struct FindPerson_Point {
/*0000*/ float y;                     // Y coordinate
/*0004*/ float x;                     // X coordinate
/*0008*/ float z;                     // Z coordinate
/*0012*/
};
static_assert(sizeof(FindPerson_Point) == 12, "FindPerson_Point must be 12 bytes");

/**
 * Find Person Request Struct
 * Size: 16 bytes (Titanium format)
 * Used in: OP_FindPersonRequest (C->S)
 */
struct FindPersonRequest_Struct {
/*0000*/ uint32_t npc_id;             // NPC to find
/*0004*/ FindPerson_Point client_pos; // Client position
/*0016*/
};
static_assert(sizeof(FindPersonRequest_Struct) == 16, "FindPersonRequest_Struct must be 16 bytes");

/**
 * Camera Effect Struct
 * Used for camera shake/effects
 * Size: 8 bytes (Titanium format)
 * Used in: OP_CameraEffect (S->C)
 */
struct Camera_Struct {
/*0000*/ uint32_t duration;           // Duration in milliseconds
/*0004*/ float    intensity;          // Effect intensity
/*0008*/
};
static_assert(sizeof(Camera_Struct) == 8, "Camera_Struct must be 8 bytes");

// ============================================================================
// Phase 4: GM Command Structures
// ============================================================================

/**
 * GM Zone Request Struct
 * Size: 88 bytes (Titanium format)
 * Used in: OP_GMZoneRequest (Bidirectional)
 */
struct GMZoneRequest_Struct {
/*0000*/ char     charname[64];       // Character name
/*0064*/ uint32_t zone_id;            // Zone ID
/*0068*/ float    x;                  // X coordinate
/*0072*/ float    y;                  // Y coordinate
/*0076*/ float    z;                  // Z coordinate
/*0080*/ float    heading;            // Heading
/*0084*/ uint32_t success;            // Success flag
/*0088*/
};
static_assert(sizeof(GMZoneRequest_Struct) == 88, "GMZoneRequest_Struct must be 88 bytes");

/**
 * GM Summon Struct
 * Size: 152 bytes (Titanium format)
 * Used in: OP_GMSummon, OP_GMGoto (C->S; both of eqemu's handlers size-check
 * against sizeof(GMSummon_Struct) = 152).
 *
 * T229: y/x/z are FLOATS, not int32 -- eqemu common/eq_packet_structs.h:1949
 * declares `float y; float x; float z;` and Client::OPGMSummon
 * (zone/client_process.cpp:1877) READS them: for an in-zone player target it
 * calls MovePC(zone, inst, gms->x, gms->y, gms->z, GetHeading()), and for a
 * cross-zone target it copies them into ServerZonePlayer_Struct. The
 * coordinates are only ignored for a non-client mob (GMMove to the GM's own
 * position) and for a corpse. Until T229 SendGMSummon left them zero, which is
 * why T148's live run relocated the target to (0, 0, 0). Note the y-before-x
 * order, which differs from GMFind_Struct below.
 */
struct GMSummon_Struct {
/*0000*/ char     charname[64];       // Target character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint32_t success;            // Success flag
/*0132*/ uint32_t zoneID;             // Zone ID
/*0136*/ float    y;                  // Destination, server coordinates (float, T229)
/*0140*/ float    x;
/*0144*/ float    z;
/*0148*/ uint32_t unknown2;           // Unknown
/*0152*/
};
static_assert(sizeof(GMSummon_Struct) == 152, "GMSummon_Struct must be 152 bytes");

/**
 * GM Find Struct (T229)
 * Size: 152 bytes -- same size as GMSummon_Struct, DIFFERENT field order.
 * Used in: OP_GMFind (Bidirectional).
 *
 * eqemu common/eq_packet_structs.h:1960 -- `float x; float y; float z;` (x
 * FIRST, unlike GMSummon_Struct's y-first). Client::Handle_OP_GMFind
 * (zone/client_packet.cpp:6640) echoes the request back with success=1,
 * zoneID and the target's GetX()/GetY()/GetZ() when the target is in the
 * GM's zone; an out-of-zone name comes back with success=0 and zeroed coords.
 * Until T229 the client decoded the reply through GMSummon_Struct's int32
 * fields, printing float bit patterns (`-998490112` = 0xC47C4000 = -1009.0f,
 * review/T148/README.md).
 */
struct GMFind_Struct {
/*0000*/ char     charname[64];       // Target character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint32_t success;            // 1 = found in this zone
/*0132*/ uint32_t zoneID;             // Zone the target is in
/*0136*/ float    x;                  // Server coordinates (x first)
/*0140*/ float    y;
/*0144*/ float    z;
/*0148*/ uint32_t unknown2;
/*0152*/
};
static_assert(sizeof(GMFind_Struct) == 152, "GMFind_Struct must be 152 bytes");

/**
 * GM Kick/Kill Struct
 * Size: 129 bytes (Titanium format -- NOT padded to 132)
 * Used in: OP_GMKick, OP_GMKill (Bidirectional)
 *
 * Ground truth: eqemu GMKick_Struct -- base common/eq_packet_structs.h:2766
 * and Titanium common/patches/titanium_structs.h:2042 are byte-identical, and
 * titanium.cpp has no ENCODE/DECODE for either opcode, so this goes on the
 * wire raw. eqemu's GMKill_Struct is the same three fields, so one struct
 * serves both opcodes here.
 *
 * `Handle_OP_GMKick` (zone/client_packet.cpp:6729) does a BARE `return` on a
 * size mismatch with no log line at all, so an over-sized packet leaves zero
 * server-side trace. (T145: we previously invented a `padding[3]`.)
 */
struct GMKick_Struct {
/*0000*/ char     name[64];           // Target name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ uint8_t  unknown;            // Unknown
/*0129*/
};
static_assert(sizeof(GMKick_Struct) == 129, "GMKick_Struct must be 129 bytes");

/**
 * GM Toggle Struct
 * Size: 68 bytes (Titanium format)
 * Used in: OP_GMToggle (C->S)
 */
struct GMToggle_Struct {
/*0000*/ char     unknown0[64];       // Unknown
/*0064*/ uint32_t toggle;             // Toggle value
/*0068*/
};
static_assert(sizeof(GMToggle_Struct) == 68, "GMToggle_Struct must be 68 bytes");

/**
 * Become NPC Struct
 * Size: 8 bytes (Titanium format)
 * Used in: OP_GMBecomeNPC (C->S)
 */
struct BecomeNPC_Struct {
/*0000*/ uint32_t id;                 // NPC ID
/*0004*/ int32_t  maxlevel;           // Max level
/*0008*/
};
static_assert(sizeof(BecomeNPC_Struct) == 8, "BecomeNPC_Struct must be 8 bytes");

/**
 * GM Last Name Struct
 * Size: 200 bytes (Titanium format)
 * Used in: OP_GMLastName (Bidirectional)
 */
struct GMLastName_Struct {
/*0000*/ char     name[64];           // Character name
/*0064*/ char     gmname[64];         // GM name
/*0128*/ char     lastname[64];       // Last name to set
/*0192*/ uint16_t unknown[4];         // Unknown
/*0200*/
};
static_assert(sizeof(GMLastName_Struct) == 200, "GMLastName_Struct must be 200 bytes");

// ============================================================================
// Phase 4: Petition Structures
// ============================================================================

#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_GM_H
