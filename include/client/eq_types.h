// Small free-standing structs, the two NPC interaction distance constants and
// the two group aliases used across the client.
// Lifted verbatim out of the umbrella client header by
// refactor/J16_eq_header_type_extraction.md. Move-only.

#pragma once

#include <cstdint>
#include <string>

#include "client/state/game_state.h"

struct WorldServer
{
	std::string long_name;
	std::string address;
	int type;
	std::string lang;
	std::string region;
	int status;
	int players;
};

// T14: one occupied character-select slot, captured from OP_SendCharInfo.
// name/level/class/race/zone are exactly what the Titanium select screen
// displays, so this is product state a select UI can consume, not test-only.
struct CharacterSelectEntry
{
	int slot = -1;              // 0-9 position in the select screen
	std::string name;
	uint8_t level = 0;
	uint8_t char_class = 0;
	uint32_t race = 0;
	uint32_t zone = 0;
	// B888-CC2 (#204): the preview leg reads the character's body from the
	// T14 wire, which the server (eqemu worlddb.cpp, eq_packet_structs.h
	// CharacterSelect_Struct) fills with the row's Gender/Face/Hair*. fable's
	// T14 parse kept only the first five fields; these four come from the
	// Titanium arrays (see the offsets in login_world_component.cpp) so the
	// select preview can stand the selected character up. gender is 0/1
	// (charcreate::Male/Female); face indexes the race's head variants.
	uint8_t gender = 0;
	uint8_t face = 0;
	uint8_t hair_color = 0;
	uint8_t hair_style = 0;
};

struct Entity
{
	uint16_t spawn_id = 0;
	std::string name;
	float x = 0, y = 0, z = 0;
	float heading = 0;
	uint8_t level = 0;
	uint8_t class_id = 0;
	uint16_t race_id = 0;
	uint8_t gender = 0;
	uint32_t guild_id = 0;
	uint8_t animation = 0;
	// B1174 (#493): the spawn's stand byte as fable knows it (the client's
	// spawn +0x251; EQ::SpawnStandStateAfterAppearance, spell_action_anim.h):
	// the server's last AT_Anim stand value, 100 = standing by default (the
	// Titanium Spawn_Struct has no stand state: eqemu fills SoF+'s
	// StandState, akk-stack zone/mob.cpp:1328, and sends Titanium
	// animation 0, :1304).
	uint8_t stand_state = 100;
	uint8_t hp_percent = 100;
	uint16_t cur_mana = 0;
	uint16_t max_mana = 0;
	float size = 0;
	bool is_corpse = false;

	// Appearance data (from Spawn_Struct)
	uint8_t face = 0;           // Face/head variant (offset 6 in Spawn_Struct)
	uint8_t haircolor = 0;      // Hair color (offset 85)
	uint8_t hairstyle = 0;      // Hair style (offset 145)
	uint8_t beardcolor = 0;     // Beard color (offset 146)
	uint8_t beard = 0;          // Beard style (offset 156)
	uint8_t equip_chest2 = 0;   // Body texture variant (offset 339)
	uint8_t helm = 0;           // Helm texture (offset 275)
	uint8_t showhelm = 0;       // Show helm (offset 139)
	uint8_t bodytype = 0;       // Body type (offset 335)
	uint8_t npc_type = 0;       // NPC type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse (offset 83)
	uint8_t light = 0;          // Light source (offset 330)

	// Equipment textures (9 slots: head, chest, arms, wrist, hands, legs, feet, primary, secondary)
	uint32_t equipment[9] = {0};      // Material IDs from TextureProfile (offset 197)
	uint32_t equipment_tint[9] = {0}; // Tint colors from TintProfile (offset 348)

	// Weapon skill types for combat animations (255 = unknown/none)
	uint8_t primary_weapon_skill = 255;    // Weapon skill type of primary weapon (7 = H2H default)
	uint8_t secondary_weapon_skill = 255;  // Weapon skill type of secondary weapon

	// Movement tracking
	float delta_x = 0, delta_y = 0, delta_z = 0;
	float delta_heading = 0;
	uint32_t last_update_time = 0;

	// Pet tracking
	uint8_t is_pet = 0;           // Non-zero if this entity is a pet (offset 329)
	uint32_t pet_owner_id = 0;    // Spawn ID of pet's owner (offset 189)

	// Appearance state flags (updated via SpawnAppearance packets)
	bool is_invisible = false;    // AT_INVISIBLE: true if invisible to other players
	bool is_sneaking = false;     // AT_SNEAK: true if sneaking
	bool is_linkdead = false;     // AT_LINKDEAD: true if connection lost
	bool is_afk = false;          // AT_AFK: true if player is AFK
	uint8_t flymode = 0;          // AT_FLYMODE: 0=ground, 1=flying, 2=levitate
	uint8_t anon_status = 0;      // AT_ANONYMOUS: 0=normal, 1=anonymous, 2=roleplay
};

// S06: Door struct replaced by DoorState (include/client/door_state.h)
// and DoorStateManager (include/client/door_state_manager.h)

// Maximum group size (leader + 5 members). T286: one definition -- the
// state layer's -- rather than a second constant that happened to agree.
using eqt::state::MAX_GROUP_MEMBERS;

// Interaction distance for vendors, tradeskill containers, trainers, etc.
// Player must be within this distance to interact with NPCs/objects.
// NOT measured on the official client (only the banker's below is).
static constexpr float NPC_INTERACTION_DISTANCE = 15.0f;
static constexpr float NPC_INTERACTION_DISTANCE_SQUARED = NPC_INTERACTION_DISTANCE * NPC_INTERACTION_DISTANCE;

// The banker's range, MEASURED on the official Titanium client (UIGAP-live2,
// review/UIGAP-ORACLE/316/range.tsv): a right-click opens the bank at 10.0,
// 15.0, 19.8 and 20.0 units and is a /consider at 21.2, 22.6 and 28.3; an
// open bank closes itself past the same 20 ("Come back soon!", eqstr 12143).
static constexpr float BANKER_INTERACTION_DISTANCE = 20.0f;
static constexpr float BANKER_INTERACTION_DISTANCE_SQUARED =
    BANKER_INTERACTION_DISTANCE * BANKER_INTERACTION_DISTANCE;

// T286: EverQuest used to declare its OWN GroupMember (snake_case) and
// keep an m_group_members array of it next to GroupState's, with a
// SyncGroupMemberToGameState() converter run by hand at every write site.
// Both the second struct and the second array are gone; this is the one
// type, and GroupState is the one store.
using GroupMember = eqt::state::GroupMember;
