// Guild, corpse-management and consent-system wire layouts, plus emote and
// the server message filter.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_GUILD_H
#define EQT_PACKETS_GUILD_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

// ============================================================================
// Guild Structures (from titanium_structs.h)
// ============================================================================

/**
 * Guild Command Struct
 * Used for guild invites
 * Size: 134 bytes (Titanium format)
 * Used in: OP_GuildInvite (C->S)
 */
struct GuildCommand_Struct {
/*0000*/ char     othername[64];  // Target player name
/*0064*/ char     myname[64];     // Inviter name
/*0128*/ uint16_t guildeqid;      // Guild ID
/*0130*/ uint8_t  unknown130[2];  // Padding
/*0132*/ uint32_t officer;        // T317: NOT the inviter's rank, despite the
                                  // name. eqemu reads it as the rank to GRANT
                                  // `othername` (0=member, 1=officer,
                                  // 2=leader, Titanium encoding); the sender's
                                  // authority comes from the server's own
                                  // GuildRank(). See client/guild_packet_bodies.h.
/*0136*/
};
static_assert(sizeof(GuildCommand_Struct) == 136, "GuildCommand_Struct must be 136 bytes");

/**
 * Guild Invite Accept Struct
 * Used when accepting guild invites
 * Size: 140 bytes (Titanium format)
 * Used in: OP_GuildInviteAccept (C->S)
 */
struct GuildInviteAccept_Struct {
/*0000*/ char     inviter[64];    // Who sent the invite
/*0064*/ char     newmember[64];  // Player accepting
/*0128*/ uint32_t response;       // 0=decline, 1=accept
/*0132*/ uint32_t guildeqid;      // Guild ID
/*0136*/
};
static_assert(sizeof(GuildInviteAccept_Struct) == 136, "GuildInviteAccept_Struct must be 136 bytes");

/**
 * Guild Remove Struct
 * Used for removing/kicking guild members
 * Size: 136 bytes (Titanium format)
 * Used in: OP_GuildRemove (C->S)
 */
struct GuildRemove_Struct {
/*0000*/ char     target[64];     // Player being removed
/*0064*/ char     name[64];       // Player doing the removal
/*0128*/ uint32_t unknown128;     // Unknown
/*0132*/ uint32_t leaderstatus;   // Remover's rank (Titanium encoding). T317:
                                  // eqemu's Handle_OP_GuildRemove casts the
                                  // body to GuildCommand_Struct and reads only
                                  // `othername`; this field is never read.
/*0136*/
};
static_assert(sizeof(GuildRemove_Struct) == 136, "GuildRemove_Struct must be 136 bytes");

/**
 * Guild Demote Struct
 * Used for demoting guild members
 * Size: 128 bytes (Titanium format)
 * Used in: OP_GuildDemote (C->S)
 */
struct GuildDemote_Struct {
/*0000*/ char     name[64];       // Player doing the demotion
/*0064*/ char     target[64];     // Player being demoted
/*0128*/
};
static_assert(sizeof(GuildDemote_Struct) == 128, "GuildDemote_Struct must be 128 bytes");

/**
 * Emote Struct (T243)
 * Used in: OP_Emote (C->S) -- the /emote text form.
 * eqemu common/patches/titanium_structs.h Emote_Struct; the server-side
 * Handle_OP_Emote rejects any packet whose size != sizeof(Emote_Struct), so
 * the client must always send the full 1028 bytes. S->C the same opcode
 * carries u32 type + a NUL-terminated variable-length string (the Titanium
 * ENCODE trims it), with the actor's name already prepended by the server.
 */
struct Emote_Struct {
/*0000*/ uint32_t type;           // 0 = custom text, 0xffffffff = command (/dance etc.)
/*0004*/ char     message[1024];
/*1028*/
};
static_assert(sizeof(Emote_Struct) == 1028, "Emote_Struct must be 1028 bytes");

/**
 * Set Server Filter Struct (T243)
 * Used in: OP_SetServerFilter (C->S), eqemu titanium_structs.h.
 * Slot indices are eqemu's eqFilterType (common/eq_constants.h). The
 * per-slot convention is NOT uniform -- see ServerFilterSlot below.
 */
struct SetServerFilter_Struct {
/*0000*/ uint32_t filters[29];
/*0116*/
};
static_assert(sizeof(SetServerFilter_Struct) == 116, "SetServerFilter_Struct must be 116 bytes");

/**
 * eqemu eqFilterType slot indices with the value convention eqemu's
 * Client::ServerFilter() applies to each (zone/client.cpp):
 *   "0=hide, 1=show" slots (Filter0 macro -- ONLY the value 1 shows):
 *     GuildChat, Socials, GroupChat, Shouts, Auctions, OOC, BadWords,
 *     MyMisses, OthersMiss, OthersHit, MissedMe
 *   every other slot: 0 = show; 1.. = hide / mine-only / group-only.
 * Sending 0xFFFFFFFF everywhere (the pre-T243 payload) therefore hid ALL
 * of them, including FilterSocials which gates the OP_Emote broadcast.
 */
enum ServerFilterSlot : uint32_t {
	FILTER_NONE = 0,
	FILTER_GUILD_CHAT = 1,      // 0=hide, 1=show
	FILTER_SOCIALS = 2,         // 0=hide, 1=show (emotes)
	FILTER_GROUP_CHAT = 3,      // 0=hide, 1=show
	FILTER_SHOUTS = 4,          // 0=hide, 1=show
	FILTER_AUCTIONS = 5,        // 0=hide, 1=show
	FILTER_OOC = 6,             // 0=hide, 1=show
	FILTER_BAD_WORDS = 7,       // 0=hide, 1=show
	FILTER_PC_SPELLS = 8,       // 0=show, 1=hide, 2=group only
	FILTER_NPC_SPELLS = 9,      // 0=show, 1=hide
	FILTER_BARD_SONGS = 10,     // 0=show, 1=mine, 2=group, 3=hide
	FILTER_SPELL_CRITS = 11,    // 0=show, 1=mine, 2=hide
	FILTER_MELEE_CRITS = 12,    // 0=show, 1=hide
	FILTER_SPELL_DAMAGE = 13,   // 0=show, 1=mine, 2=hide
	FILTER_MY_MISSES = 14,      // 0=hide, 1=show
	FILTER_OTHERS_MISS = 15,    // 0=hide, 1=show
	FILTER_OTHERS_HIT = 16,     // 0=hide, 1=show
	FILTER_MISSED_ME = 17,      // 0=hide, 1=show
	FILTER_DAMAGE_SHIELDS = 18, // 0=show, 1=hide
	FILTER_DOT = 19,            // 0=show, 1=hide
	FILTER_PET_HITS = 20,       // 0=show, 1=hide
	FILTER_PET_MISSES = 21,     // 0=show, 1=hide
	FILTER_FOCUS_EFFECTS = 22,  // 0=show, 1=hide
	FILTER_PET_SPELLS = 23,     // 0=show, 1=hide
	FILTER_HEAL_OVER_TIME = 24, // 0=show, 1=mine, 2=hide
	FILTER_ITEM_SPEECH = 25,    // 0=show, 1=hide
	FILTER_STRIKETHROUGH = 26,  // 0=show, 1=hide
	FILTER_STUNS = 27,          // 0=show, 1=hide
	FILTER_BARD_SONGS_ON_PETS = 28, // 0=show, 1=hide
	FILTER_COUNT = 29
};

/**
 * Guild Make Leader Struct
 * Used for transferring guild leadership
 * Size: 128 bytes (Titanium format)
 * Used in: OP_GuildLeader (C->S)
 */
struct GuildMakeLeader_Struct {
/*0000*/ char     name[64];       // Current leader name
/*0064*/ char     target[64];     // New leader name
/*0128*/
};
static_assert(sizeof(GuildMakeLeader_Struct) == 128, "GuildMakeLeader_Struct must be 128 bytes");

/**
 * Guild MOTD Struct
 * Used for get/set guild MOTD
 * Size: 648 bytes (Titanium format)
 * Used in: OP_GetGuildMOTDReply (S->C), OP_SetGuildMOTD (C->S)
 */
struct GuildMOTD_Struct {
/*0000*/ uint32_t unknown0;       // Unknown
/*0004*/ char     name[64];       // Guild name or player name
/*0068*/ char     setby_name[64]; // Who set the MOTD
/*0132*/ uint32_t unknown132;     // Unknown
/*0136*/ char     motd[512];      // MOTD text
/*0648*/
};
static_assert(sizeof(GuildMOTD_Struct) == 648, "GuildMOTD_Struct must be 648 bytes");

/**
 * Guild Member Update Struct
 * Used for member status updates (zone change, etc.)
 * Size: 76 bytes (Titanium format)
 * Used in: OP_GuildMemberUpdate (S->C)
 */
struct GuildMemberUpdate_Struct {
/*0000*/ uint32_t guild_id;        // Guild ID
/*0004*/ char     member_name[64]; // Member name
/*0068*/ uint16_t zone_id;         // Zone ID
/*0070*/ uint16_t instance_id;     // Instance ID
/*0072*/ uint32_t unknown072;      // Unknown
/*0076*/
};
static_assert(sizeof(GuildMemberUpdate_Struct) == 76, "GuildMemberUpdate_Struct must be 76 bytes");

// ============================================================================
// Phase 3: Corpse Management Structures
// ============================================================================

/**
 * Corpse Drag Struct
 * Used for dragging corpses
 * Size: 152 bytes (Titanium format)
 * Used in: OP_CorpseDrag, OP_CorpseDrop (C->S)
 */
struct CorpseDrag_Struct {
/*0000*/ char     CorpseName[64];     // Name of corpse being dragged
/*0064*/ char     DraggerName[64];    // Name of player dragging
/*0128*/ uint8_t  Unknown128[24];     // Unknown padding
/*0152*/
};
static_assert(sizeof(CorpseDrag_Struct) == 152, "CorpseDrag_Struct must be 152 bytes");

// ============================================================================
// Phase 3: Consent System Structures
// ============================================================================

/**
 * Consent Request Struct
 * Used for granting consent to loot corpse
 * Variable size (name is null-terminated string)
 * Used in: OP_Consent, OP_ConsentDeny (C->S)
 */
struct Consent_Struct {
/*0000*/ char     name[1];            // Variable length player name
};
static_assert(sizeof(Consent_Struct) == 1, "Consent_Struct must be 1 bytes");  // eqemu titanium_structs.h:3196 (variable-length name)

/**
 * Consent Response Struct
 * Used for consent response from server
 * Size: 161 bytes (Titanium format)
 * Used in: OP_ConsentResponse, OP_DenyResponse (S->C)
 */
struct ConsentResponse_Struct {
/*0000*/ char     grantname[64];      // Player granted consent
/*0064*/ char     ownername[64];      // Corpse owner name
/*0128*/ uint8_t  permission;         // 1=granted, 0=denied
/*0129*/ char     zonename[32];       // Zone where corpse is
/*0161*/
};
static_assert(sizeof(ConsentResponse_Struct) == 161, "ConsentResponse_Struct must be 161 bytes");


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_GUILD_H
