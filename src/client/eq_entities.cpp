// EverQuest entity/spawn/world-object cluster: zone spawns, exp/raid/client
// position updates, health, appearance, doors, ground spawns, and world
// object (tradeskill container) interaction.
//
// Split out of eq.cpp (Batch F, unit F11):
//  - Zone spawns / world entities: ZoneProcessZoneSpawns, ZoneProcessTimeOfDay,
//    ZoneProcessSpawnDoor, ZoneProcessSendZonepoints, ZoneProcessSpawnAppearance,
//    ZoneProcessGroundSpawn, ZoneProcessWeather, ZoneProcessNewSpawn.
//  - Exp / Raid / ClientUpdate: ZoneProcessExpUpdate (+ file-local
//    CalculateExpProgress/EXP_FOR_LEVEL/MAX_EXP_LEVEL helpers),
//    ZoneProcessRaidUpdate, ZoneProcessClientUpdate.
//  - DeleteSpawn / MobHealth / HPUpdate: ZoneProcessDeleteSpawn,
//    ZoneProcessMobHealth, ZoneProcessHPUpdate.
//  - Entity lookup helpers (left in eq.cpp by F08's plan): FindEntityByName,
//    ListEntities.
//  - Appearance: ZoneProcessWearChange, UpdatePlayerAppearanceFromInventory,
//    ZoneProcessIllusion.
//  - Doors: ZoneProcessMoveDoor (+ file-local getDoorTypeFromName helper).
//  - World object / animation senders+handlers: SendSpawnAppearance,
//    SendAnimation, SendClickDoor, SendClickObject, SendCloseContainer,
//    GetWorldObject, ClearWorldObjects, ZoneProcessClickObjectAction.
// Moved verbatim, no behavior change. See refactor/F11_entities.md.
//
// NOTE: ZoneProcessEmote (between ZoneProcessSpawnAppearance and
// ZoneProcessGroundSpawn in eq.cpp) is chat/emote text processing, not
// named in this unit's scope, and stays in eq.cpp.
//
// NOTE: ZoneProcessGuildMOTD (between ZoneProcessRaidUpdate and
// ZoneProcessClientUpdate in eq.cpp) is guild domain (see F04) and stays
// in eq.cpp.
//
// NOTE: SendTradeSkillCombine and ZoneProcessTradeSkillCombine (nestled
// among the world-object senders/handlers in eq.cpp, between
// SendClickObject/SendCloseContainer and ZoneProcessClickObjectAction/
// SendJump respectively) are item-economy, judgment-called to stay in
// eq.cpp for consistency with F05 (merchant/inventory/trade split).
//
// NOTE: ZoneProcessPlayerStateAdd/Death/PlayerStateRemove/Stamina/
// ZonePlayerToBind/LevelUpdate (immediately following ZoneProcessMoveDoor
// in eq.cpp) are not named in this unit's scope and stay in eq.cpp.

#include "client/eq.h"
#include "client/world_object.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/raid_update.h"
#include "client/spell/buff_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/bridge/irrlicht_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"

// P28: entity_pose_state.h is game-state-owned (S2 decoupling comment in the
// header), and inventory_manager.h/item_instance.h are the same
// renderer-free data model used unconditionally elsewhere — un-guarded.
#include "client/state/entity_pose_state.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/door_sounds.h"
#endif

#include "client/animation_constants.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cmath>

// Experience required for each level (Titanium-era values)
// These are cumulative totals needed to reach each level (levels 1-65)
static const uint32_t EXP_FOR_LEVEL[] = {
    0,          // Level 1 (no exp required)
    1000,       // Level 2
    2000,       // Level 3
    3250,       // Level 4
    5250,       // Level 5
    8750,       // Level 6
    14000,      // Level 7
    22000,      // Level 8
    34000,      // Level 9
    52000,      // Level 10
    77000,      // Level 11
    110000,     // Level 12
    152500,     // Level 13
    207500,     // Level 14
    277500,     // Level 15
    365000,     // Level 16
    472500,     // Level 17
    602500,     // Level 18
    757500,     // Level 19
    940000,     // Level 20
    1152500,    // Level 21
    1397500,    // Level 22
    1677500,    // Level 23
    1995000,    // Level 24
    2352500,    // Level 25
    2752500,    // Level 26
    3197500,    // Level 27
    3690000,    // Level 28
    4232500,    // Level 29
    4827500,    // Level 30
    5477500,    // Level 31
    6185000,    // Level 32
    6952500,    // Level 33
    7782500,    // Level 34
    8677500,    // Level 35
    9640000,    // Level 36
    10672500,   // Level 37
    11777500,   // Level 38
    12957500,   // Level 39
    14215000,   // Level 40
    15552500,   // Level 41
    16972500,   // Level 42
    18477500,   // Level 43
    20070000,   // Level 44
    21752500,   // Level 45
    23527500,   // Level 46
    25397500,   // Level 47
    27365000,   // Level 48
    29432500,   // Level 49
    31602500,   // Level 50
    33877500,   // Level 51
    39677500,   // Level 52
    46477500,   // Level 53
    54377500,   // Level 54
    63477500,   // Level 55
    73877500,   // Level 56
    85677500,   // Level 57
    98977500,   // Level 58
    113877500,  // Level 59
    130477500,  // Level 60
    148877500,  // Level 61
    169177500,  // Level 62
    191477500,  // Level 63
    215877500,  // Level 64
    242477500,  // Level 65
};
static const int MAX_EXP_LEVEL = sizeof(EXP_FOR_LEVEL) / sizeof(EXP_FOR_LEVEL[0]) - 1;

// Calculate experience progress as 0.0 to 1.0 for the current level
static float CalculateExpProgress(uint32_t exp, uint8_t level) {
    if (level < 1 || level >= MAX_EXP_LEVEL) {
        return 0.0f;  // Invalid level or max level
    }

    uint32_t expForCurrentLevel = EXP_FOR_LEVEL[level];
    uint32_t expForNextLevel = EXP_FOR_LEVEL[level + 1];

    if (exp < expForCurrentLevel) {
        return 0.0f;  // Below current level (shouldn't happen)
    }
    if (exp >= expForNextLevel) {
        return 1.0f;  // At or above next level
    }

    uint32_t expNeeded = expForNextLevel - expForCurrentLevel;
    uint32_t expGained = exp - expForCurrentLevel;

    return static_cast<float>(expGained) / static_cast<float>(expNeeded);
}

void EverQuest::ZoneProcessZoneSpawns(const EQ::Net::Packet &p)
{
	SetLoadingPhase(LoadingPhase::ZONE_RECEIVING_SPAWNS);

	// ZoneSpawns contains multiple Spawn_Struct entries
	// Based on titanium_structs.h, Spawn_Struct is 385 bytes

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_ENTITY, "Received zone spawns packet, size: {} bytes", p.Length());
	}
	
	// Don't clear existing entities - ZoneSpawns packets can come in multiple batches
	// Only add/update spawns, don't remove existing ones
	
	// The ZoneSpawns packet appears to have a header before the spawn data
	size_t offset = 2;
	int spawn_count = 0;
	
	/*	
	// Debug: Dump the beginning of the packet to understand structure
	if (s_debug_level >= 1) {
		std::cout << "First 50 bytes of ZoneSpawns packet:" << std::endl;
		for (int i = 0; i < 50 && i < p.Length(); i++) {
			std::cout << fmt::format("{:02x} ", p.GetUInt8(i));
			if ((i + 1) % 16 == 0) std::cout << std::endl;
		}
		std::cout << std::endl;
		
		// Let's analyze the hex dump more carefully:
		// At offset 9: "Renux_Herkanor000" - but this is the middle of a name
		// The name field in Spawn_Struct is at offset 7
		// If "Renux_Herkanor000" is at packet offset 9, and name is at struct offset 7,
		// then the spawn struct starts at packet offset 9 - 7 = 2
		
		// But wait - we're seeing truncated names like "e_rat005" instead of "a_large_rat005"
		// This means we're starting too late. Let's check different offsets
		
		// First, let's look for a complete name pattern
		for (int test_offset = 0; test_offset < 20 && test_offset + 70 < p.Length(); test_offset++) {
			char c = (char)p.GetUInt8(test_offset);
			if (c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z') {
				std::string test_name = p.GetCString(test_offset);
				if (test_name.length() > 3 && test_name.length() < 64) {
					std::cout << fmt::format("  Possible name at offset {}: '{}'", test_offset, test_name) << std::endl;
				}
			}
		}
		
		// If we're getting "e_rat005" instead of "a_large_rat005", we're missing 6 characters
		// Current offset is 8, name field is at +7, so we're reading from packet offset 15
		// We need to start 6 bytes earlier: 8 - 6 = 2
		offset = 2;
		
		// Calculate expected spawn count based on packet size
		size_t data_size = p.Length() - offset;
		size_t expected_spawns = data_size / 385;
		std::cout << fmt::format("Packet has {} bytes of data, expecting approximately {} spawns", 
			data_size, expected_spawns) << std::endl;
	}
	*/

	// Parse Spawn_Struct entries based on titanium_structs.h
	while (offset + 385 <= p.Length()) { // Spawn_Struct is 385 bytes
		Entity entity;
		
		// Based on Spawn_Struct from titanium_structs.h:
		// name is at offset 7 (64 bytes)
		entity.name = p.GetCString(offset + 7);
		
		// If name is empty, we've likely reached the end of spawn data
		if (entity.name.empty()) {
			LOG_DEBUG(MOD_ENTITY, "Found empty name at offset {}, ending spawn parsing", offset);
			break;
		}
		
		// spawnId is at offset 340 in the struct (uint32)
		entity.spawn_id = p.GetUInt32(offset + 340);
		
		// Debug: Check what's around the spawn_id location for first few spawns
		if (spawn_count < 3 && ShouldLog(MOD_ENTITY, LOG_TRACE)) {
			LOG_TRACE(MOD_ENTITY, "Spawn at offset {}: Name='{}', checking spawn_id area:", offset, entity.name);
			for (int i = 330; i < 350 && offset + i + 4 < p.Length(); i += 4) {
				uint32_t val = p.GetUInt32(offset + i);
				if (val > 0 && val < 100000) {
					LOG_TRACE(MOD_ENTITY, "  Offset +{}: u32={} ({:#010x})", i, val, val);
				}
			}
		}
		
		// level is at offset 151
		entity.level = p.GetUInt8(offset + 151);
		
		// Position data from the bitfield structure:
		// x is at offset 94 (19 bits within a bitfield)
		// y is at offset 98 (19 bits)
		// z is at offset 102 (19 bits)
		// heading is at offset 106 (12 bits)
		
		// Read the bitfields
		uint32_t field1 = p.GetUInt32(offset + 94);  // contains deltaHeading and x
		uint32_t field2 = p.GetUInt32(offset + 98);  // contains y and animation
		uint32_t field3 = p.GetUInt32(offset + 102); // contains z and deltaY
		uint32_t field4 = p.GetUInt32(offset + 106); // contains deltaX and heading
		
		// Extract x, y, z (19 bits each, signed)
		// Positions are stored as fixed-point values (multiplied by 8)
		// Note: Server x/y are swapped relative to zone geometry, so we swap them here
		int32_t x_raw = (field1 >> 10) & 0x7FFFF;
		if (x_raw & 0x40000) x_raw |= 0xFFF80000; // Sign extend
		float server_x = static_cast<float>(x_raw) / 8.0f;

		int32_t y_raw = field2 & 0x7FFFF;
		if (y_raw & 0x40000) y_raw |= 0xFFF80000; // Sign extend
		float server_y = static_cast<float>(y_raw) / 8.0f;

		int32_t z_raw = field3 & 0x7FFFF;
		if (z_raw & 0x40000) z_raw |= 0xFFF80000; // Sign extend
		entity.z = static_cast<float>(z_raw) / 8.0f;

		// Swap x and y from server
		entity.x = server_y;
		entity.y = server_x;

		// Read npc_type early - needed for heading transformation
		entity.npc_type = p.GetUInt8(offset + 83);
		bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

		// Extract heading (11 bits to match C->S format)
		// Server uses 512 EQ units = 360 degrees, so multiply by 360/512
		uint32_t raw_heading = (field4 >> 13) & 0x7FF;
		float server_heading = (static_cast<float>(raw_heading) / 4.0f) * 360.0f / 512.0f;
		// Use heading directly for all entities (NPCs and players)
		entity.heading = server_heading;
		if (entity.heading >= 360.0f) entity.heading -= 360.0f;

		// Debug logging for current player at level 1+
		if (s_debug_level >= 1 && entity.name == m_character) {
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] spawn_id={} name='{}'", entity.spawn_id, entity.name);
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] raw_heading={} -> server_heading={:.2f}deg -> entity.heading={:.2f}deg (isNPC={})",
				raw_heading, server_heading, entity.heading, isNPC);
			LOG_DEBUG(MOD_MOVEMENT, "POS S->C ZoneSpawns [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> entity_pos=({:.2f},{:.2f},{:.2f})",
				server_x, server_y, entity.z, entity.x, entity.y, entity.z);
		}

		// race is at offset 284 (uint32)
		entity.race_id = p.GetUInt32(offset + 284);

		// class_ is at offset 331
		entity.class_id = p.GetUInt8(offset + 331);

		// gender is at offset 334
		entity.gender = p.GetUInt8(offset + 334);

		// guildID is at offset 238
		entity.guild_id = p.GetUInt32(offset + 238);

		// curHp is at offset 86 (hp percentage)
		entity.hp_percent = p.GetUInt8(offset + 86);

		// animation is in the bitfield at offset 98
		entity.animation = (field2 >> 19) & 0x3FF;

		// size is at offset 75 (float)
		entity.size = *reinterpret_cast<const float*>(static_cast<const char*>(p.Data()) + offset + 75);

		// Appearance data from Spawn_Struct
		entity.face = p.GetUInt8(offset + 6);           // Face/head variant
		// npc_type already read above for heading transformation
		entity.haircolor = p.GetUInt8(offset + 85);     // Hair color
		entity.showhelm = p.GetUInt8(offset + 139);     // Show helm
		entity.hairstyle = p.GetUInt8(offset + 145);    // Hair style
		entity.beardcolor = p.GetUInt8(offset + 146);   // Beard color
		entity.beard = p.GetUInt8(offset + 156);        // Beard style
		entity.helm = p.GetUInt8(offset + 275);         // Helm texture
		entity.light = p.GetUInt8(offset + 330);        // Light source
		entity.bodytype = p.GetUInt8(offset + 335);     // Body type
		entity.equip_chest2 = p.GetUInt8(offset + 339); // Body texture variant

		// Equipment textures (TextureProfile at offset 197, 9 slots * 4 bytes)
		for (int i = 0; i < 9; i++) {
			entity.equipment[i] = p.GetUInt32(offset + 197 + i * 4);
		}

		// Equipment tint colors (TintProfile at offset 348, 9 slots * 4 bytes)
		for (int i = 0; i < 9; i++) {
			entity.equipment_tint[i] = p.GetUInt32(offset + 348 + i * 4);
		}

		// Initialize movement fields
		entity.delta_x = 0;
		entity.delta_y = 0;
		entity.delta_z = 0;
		entity.delta_heading = 0;
		entity.last_update_time = std::time(nullptr);

		// Store pet info (for deferred pet detection after our spawn ID is known)
		entity.is_pet = p.GetUInt8(offset + 329);
		entity.pet_owner_id = p.GetUInt32(offset + 189);

		// Validate the spawn data before adding
		if (entity.spawn_id > 0 && entity.spawn_id < 100000 && !entity.name.empty()) {
			// Check if this is our own character and update our position
			if (entity.name == m_character) {
				if (m_my_spawn_id != 0 && m_my_spawn_id != entity.spawn_id) {
					LOG_WARN(MOD_ZONE, "Spawn ID changed in ZoneSpawns: {} -> {}", m_my_spawn_id, entity.spawn_id);
				}
				m_my_spawn_id = entity.spawn_id;
				// Phase 7.4: Also update PlayerState
				m_game_state.player().setSpawnId(m_my_spawn_id);
				if (m_trade_manager) {
					m_trade_manager->setMySpawnId(m_my_spawn_id);
				}

				// Debug: Zone-in position tracking at level 1
				if (s_debug_level >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Found our spawn in ZoneSpawns: ID={} Name='{}'", m_my_spawn_id, m_character);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Spawn entity: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} size={:.2f}",
					         entity.x, entity.y, entity.z, entity.heading, entity.size);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Previous client pos: ({:.2f},{:.2f},{:.2f}) heading={:.2f}",
					         m_x, m_y, m_z, m_heading);
				}

				if (s_debug_level >= 2) {
					LOG_DEBUG(MOD_ENTITY, "=== FOUND OUR CHARACTER ===");
					LOG_DEBUG(MOD_ENTITY, "Name: '{}' Spawn ID: {}", m_character, m_my_spawn_id);
					LOG_DEBUG(MOD_ENTITY, "Position: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
						entity.x, entity.y, entity.z, entity.heading);
					LOG_DEBUG(MOD_ENTITY, "Size: {:.2f} (THIS IS THE ACTUAL SIZE)", entity.size);
					LOG_DEBUG(MOD_ENTITY, "Previous m_z={:.2f}, updating to entity.z={:.2f}", m_z, entity.z);
					LOG_DEBUG(MOD_ENTITY, "Previous m_size={:.2f}, updating to entity.size={:.2f}", m_size, entity.size);
					LOG_DEBUG(MOD_ENTITY, "==============================");
				}
#ifdef EQT_HAS_GRAPHICS
				// Notify bridge of player spawn ID (D17b: bridge is sole path to renderer)
				if (m_bridge) {
					if (s_debug_level >= 1) {
						LOG_INFO(MOD_ZONE, "[ZONE-IN] Publishing PlayerSpawnIdSet({}) from ZoneSpawns", m_my_spawn_id);
					}
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::PlayerSpawnIdSet,
						eqt::state::PlayerSpawnIdSetData{m_my_spawn_id}));
				}
				// Mark player entity as pending creation - will be created when ClientUpdate arrives
				// This ensures the loading screen stays visible until the player model is fully loaded
				m_player_graphics_entity_pending = true;
#endif
				// Update our position to match server spawn position
				m_x = entity.x;
				m_y = entity.y;
				m_size = entity.size;
				// Server Z is model center. Convert to feet position for internal use.
				// SendPositionUpdate will add m_size/2 to convert back to model center.
				m_z = entity.z - (entity.size / 2.0f);
				// Don't update heading from spawn data as it may be scaled differently

				if (s_debug_level >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from spawn: ({:.2f},{:.2f},{:.2f}) (feet Z, server Z was {:.2f}) heading unchanged={:.2f}",
					         m_x, m_y, m_z, entity.z, m_heading);
				}

				// Update bridge with corrected feet Z position
				if (m_bridge) {
					float heading512 = m_heading * 512.0f / 360.0f;
					eqt::state::PlayerMovedData pdata;
					pdata.x = m_x; pdata.y = m_y; pdata.z = m_z;
					pdata.heading = heading512;
					pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
					pdata.isMoving = false;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
				}
			}

			// Add to entity list (sync to both local map and EntityManager)
			m_entities[entity.spawn_id] = entity;
			SyncEntityToGameState(entity);
			spawn_count++;

			// NOTE: Graphics entities are NOT created here during ZoneSpawns processing.
			// All entities are created later in LoadZoneGraphics() during the graphics loading phase.
			// This ensures the progress bar shows during the entire loading process.

				LOG_DEBUG(MOD_ENTITY, "Loaded spawn {}: {} (ID: {}) Level {} Race {} Size {:.2f} at ({:.2f}, {:.2f}, {:.2f})",
				spawn_count, entity.name, entity.spawn_id, entity.level,
				entity.race_id, entity.size, entity.x, entity.y, entity.z);
			// Log all spawns with position and heading at trace level
			LOG_TRACE(MOD_ENTITY, "ZoneSpawn: {} (ID:{}) pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} npc_type={}",
				entity.name, entity.spawn_id, entity.x, entity.y, entity.z, entity.heading, entity.npc_type);
		} else {
			LOG_DEBUG(MOD_ENTITY, "Skipping invalid spawn at offset {}: ID={}, Name='{}'",
				offset, entity.spawn_id, entity.name);
		}
		
		// Move to next spawn struct
		offset += 385;
	}
	
	LOG_INFO(MOD_ZONE, "Loaded {} spawns in zone", spawn_count);

	// Second pass: Check for pet after all spawns are loaded (if we know our spawn ID)
	// This handles the case where our character is in ZoneSpawns. If not, deferred
	// pet detection in ClientUpdate will find the pet when our spawn ID is set.
	if (m_my_spawn_id != 0 && m_pet_spawn_id == 0) {
		for (const auto& pair : m_entities) {
			const Entity& ent = pair.second;
			if (ent.is_pet != 0 && ent.pet_owner_id == m_my_spawn_id) {
				m_pet_spawn_id = ent.spawn_id;
				std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
				m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;
				LOG_INFO(MOD_MAIN, "Pet detected in ZoneSpawns: {} (ID: {}) Level {} owned by us (ID: {})",
					ent.name, ent.spawn_id, ent.level, m_my_spawn_id);
				// T43: was #ifdef EQT_HAS_GRAPHICS. OnPetCreated() is a pure
				// bridge-event publish (eq_bridge.cpp; no renderer dependency)
				// -- gating it meant pet deltas never published in a headless
				// build (known residual named in refactor/deferred.md's
				// EQT_HAS_GRAPHICS pattern entry; T41's snapshot fix only
				// covered a pet present at zone-in, not this delta path).
				OnPetCreated(ent);
				break;
			}
		}
	}
}

void EverQuest::ZoneProcessTimeOfDay(const EQ::Net::Packet &p)
{
	if (p.Length() < 8) {  // needs GetUInt8(2..5) + GetUInt16(6)
		LOG_WARN(MOD_ZONE, "TimeOfDay packet too small: {} bytes (expected at least {})",
			p.Length(), 8);
		return;
	}

	m_time_hour = p.GetUInt8(2);
	m_time_minute = p.GetUInt8(3);
	m_time_day = p.GetUInt8(4);
	m_time_month = p.GetUInt8(5);
	m_time_year = p.GetUInt16(6);

	// Phase 7.3: Also update WorldState
	m_game_state.world().setTimeOfDay(m_time_hour, m_time_minute, m_time_day, m_time_month, m_time_year);

	// Publish TimeOfDayChanged event to bridge
	if (m_bridge) {
		eqt::state::TimeOfDayChangedData data;
		data.hour = m_time_hour;
		data.minute = m_time_minute;
		data.day = m_time_day;
		data.month = m_time_month;
		data.year = m_time_year;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TimeOfDayChanged, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Phase 13: Update day/night state for audio
	UpdateDayNightState();
#endif

	LOG_DEBUG(MOD_ZONE, "Time of day: {:02d}:{:02d} {:02d}/{:02d}{}",
		m_time_hour, m_time_minute, m_time_day, m_time_month, m_time_year);
}

void EverQuest::ZoneProcessSpawnDoor(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + array of Door_Struct(80)
	// No count field - calculate from packet size
	if (p.Length() < 2) {
		LOG_WARN(MOD_ENTITY, "SpawnDoor packet too small: {} bytes", p.Length());
		return;
	}

	size_t data_size = p.Length() - 2;  // Subtract opcode
	if (data_size % sizeof(EQT::Door_Struct) != 0) {
		LOG_WARN(MOD_ENTITY, "SpawnDoor packet size {} not divisible by Door_Struct size {}",
		         data_size, sizeof(EQT::Door_Struct));
		// Try to parse what we can
	}

	uint32_t count = static_cast<uint32_t>(data_size / sizeof(EQT::Door_Struct));
	LOG_INFO(MOD_ENTITY, "Received {} doors ({} bytes)", count, p.Length());

	// Parse each door
	for (uint32_t i = 0; i < count; ++i) {
		size_t offset = 2 + i * sizeof(EQT::Door_Struct);  // Start after opcode
		const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + offset;
		const EQT::Door_Struct* door_data = reinterpret_cast<const EQT::Door_Struct*>(data);

		EQT::DoorState door;
		door.doorId = door_data->doorId;

		// Extract name (null-terminated, max 32 chars)
		char name_buf[33] = {0};
		std::memcpy(name_buf, door_data->name, 32);
		door.name = name_buf;

		// Position - swap x and y from server (same as entity spawns)
		door.x = door_data->yPos;
		door.y = door_data->xPos;
		door.z = door_data->zPos;
		door.heading = door_data->heading;
		door.incline = door_data->incline;
		door.size = door_data->size;
		door.opentype = door_data->opentype;
		door.invertState = (door_data->invert_state != 0);
		door.doorParam = door_data->door_param;

		// Compute initial open state: XOR of spawn state and invert flag
		door.isOpen = (door_data->state_at_spawn != 0) != door.invertState;

		// Store in the write-model GameState (T219: was a separate third copy)
		m_game_state.doors().addDoor(door);

		// Debug: dump raw bytes at critical offsets (60-68)
		LOG_DEBUG(MOD_ENTITY, "Door {} raw bytes @60-67: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
		         i, data[60], data[61], data[62], data[63], data[64], data[65], data[66], data[67]);

		LOG_DEBUG(MOD_ENTITY, "Door {}: '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} incline={} size={} type={} state_at_spawn={} invert={}",
		         door.doorId, door.name, door.x, door.y, door.z, door.heading,
		         door.incline, door.size, door.opentype, door_data->state_at_spawn, door.invertState ? 1 : 0);

		// Publish DoorSpawned event to bridge
		if (m_bridge) {
			eqt::state::DoorSpawnedData data;
			data.doorId = door.doorId;
			data.name = door.name;
			data.x = door.x;
			data.y = door.y;
			data.z = door.z;
			data.heading = door.heading;
			data.incline = door.incline;
			data.size = door.size;
			data.opentype = door.opentype;
			data.isOpen = door.isOpen;
			data.invertState = door.invertState;
			data.doorParam = door.doorParam;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::DoorSpawned, std::move(data)));
		}
	}
}

void EverQuest::ZoneProcessSendZonepoints(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + count(4) + count * ZonePoint_Entry(24)
	// Minimum size: 2 + 4 = 6 bytes
	if (p.Length() < 6) {
		return;
	}

	uint32_t count = p.GetUInt32(2);

	LOG_DEBUG(MOD_ZONE, "Received zone points: {} entries", count);

	// Parse zone points and store them in ZoneLines
	if (m_zone_lines && count > 0) {
		std::vector<EQT::ZonePoint> zonePoints;
		zonePoints.reserve(count);

		// Each ZonePoint_Entry is 24 bytes, starting at offset 6
		const size_t entrySize = 24;
		const size_t dataStart = 6;

		for (uint32_t i = 0; i < count; ++i) {
			size_t offset = dataStart + i * entrySize;
			if (offset + entrySize > p.Length()) {
				break;
			}

			EQT::ZonePoint zp;
			zp.number = p.GetUInt32(offset);       // iterator/zone point ID
			zp.targetY = p.GetFloat(offset + 4);   // y
			zp.targetX = p.GetFloat(offset + 8);   // x
			zp.targetZ = p.GetFloat(offset + 12);  // z
			zp.heading = p.GetFloat(offset + 16);  // heading
			zp.targetZoneId = p.GetUInt16(offset + 20);  // zoneid
			// zoneinstance at offset + 22 is ignored for now

			zonePoints.push_back(zp);

			if (s_debug_level >= 3) {
				LOG_TRACE(MOD_ZONE, "  Zone point {}: zone={} pos=({},{},{}) heading={}",
					zp.number, zp.targetZoneId, zp.targetX, zp.targetY, zp.targetZ, zp.heading);
			}
		}

		m_zone_lines->setServerZonePoints(zonePoints);

		// Publish ZoneLineBoundingBoxes event to bridge
		if (m_bridge) {
			eqt::state::ZoneLineBoundingBoxesData data;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneLineBoundingBoxes, std::move(data)));
		}

		LOG_DEBUG(MOD_ZONE, "Stored {} zone points in ZoneLines", zonePoints.size());
	}

	// After zone points, check if we need to send server filter
	// This is old logic - server filter is now sent after GuildMOTD
	// But keeping it here for compatibility if the flow is different
	if (m_zone_entry_sent && !m_client_ready_sent) {
		// This path shouldn't normally be taken anymore
		if (!m_server_filter_sent && m_send_exp_zonein_received) {
			LOG_DEBUG(MOD_ZONE, "ZoneProcessSendZonepoints calling ZoneSendSetServerFilter (fallback)");
			ZoneSendSetServerFilter();
		}

		// Send client ready - This shouldn't happen here anymore
		if (!m_client_ready_sent && m_server_filter_sent) {
			LOG_DEBUG(MOD_ZONE, "ZoneProcessSendZonepoints calling ZoneSendClientReady");
			ZoneSendClientReady();
		}
	}
}

void EverQuest::ZoneProcessSpawnAppearance(const EQ::Net::Packet &p)
{
	// SpawnAppearance_Struct: spawn_id(2), type(2), parameter(4) = 8 bytes + 2 opcode = 10 bytes
	if (p.Length() < 10) {
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint16_t type = p.GetUInt16(4);
	uint32_t parameter = p.GetUInt32(6);

	LogTargetEntity(spawn_id, "SpawnAppearance spawn_id={}, type={}, parameter={}",
		spawn_id, type, parameter);

	switch (type) {
	case AT_ANIMATION:
		// Animation update - set the entity's animation
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.animation = static_cast<uint8_t>(parameter);
			}

			// Publish animation events via bridge (D17b: bridge is sole path to renderer)
			if (m_bridge) {
				// Map animation parameter to EQ model animation code
				std::string animCode;
				bool loop = true;
				bool playThrough = false;

				// Zone server animation values (100+) are pose/state animations
				// Lower values are action animations
				uint8_t poseState = static_cast<uint8_t>(
					eqt::state::EntityPoseState::Standing);
				bool setPose = false;

				switch (parameter) {
				case ANIM_STANDING:
				case ANIM_STAND:
					animCode = "o01";  // Idle/standing
					poseState = static_cast<uint8_t>(eqt::state::EntityPoseState::Standing);
					setPose = true;
					break;
				case ANIM_SITTING:
					animCode = "p02";  // Sitting idle pose
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = static_cast<uint8_t>(eqt::state::EntityPoseState::Sitting);
					setPose = true;
					break;
				case ANIM_CROUCHING:
					animCode = "l08";  // Crouching
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = static_cast<uint8_t>(eqt::state::EntityPoseState::Crouching);
					setPose = true;
					break;
				case ANIM_LYING:
					animCode = "d05";  // Lying down (death pose)
					loop = false;      // Don't loop (holds on last frame automatically)
					poseState = static_cast<uint8_t>(eqt::state::EntityPoseState::Lying);
					setPose = true;
					break;
				case ANIM_FREEZE:
					animCode = "o01";  // Freeze in place (use idle)
					break;
				case ANIM_LOOT:
					animCode = "t07";  // Looting animation
					loop = false;
					playThrough = true;
					break;
				default:
					// For other animations, don't change current animation
					// (movement animations come through ClientUpdate)
					break;
				}

				if (!animCode.empty()) {
					// Set pose state BEFORE animation to prevent updateEntity from overriding
					if (setPose) {
						eqt::state::EntityPoseStateChangedData data;
						data.spawnId = spawn_id;
						data.poseState = poseState;
						m_bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::EntityPoseStateChanged, std::move(data)));
					}
					eqt::state::EntityAnimationEventData data;
					data.spawnId = spawn_id;
					data.animCode = 0;
					data.animName = animCode;
					data.loop = loop;
					data.playThrough = playThrough;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
					LogTargetEntity(spawn_id, "SpawnAppearance set animation '{}' pose={} on spawn_id={}",
						animCode, static_cast<int>(poseState), spawn_id);
				}
			}
		}
		break;

	case AT_DIE:
		// Entity died - trigger death animation (D17b: bridge-only)
		if (m_bridge) {
			eqt::state::EntityDeathAnimationData data;
			data.spawnId = spawn_id;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityDeathAnimation, std::move(data)));
		}
		break;

	case AT_HP_UPDATE:
		// HP update
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.hp_percent = static_cast<uint8_t>(parameter);
			}
		}
		break;

	case AT_SPAWN_ID:
		// Server is assigning us a new spawn ID (happens after zoning)
		// spawn_id=0 means "self", parameter contains the new spawn ID
		if (spawn_id == 0 && parameter != 0) {
			uint16_t newSpawnId = static_cast<uint16_t>(parameter);
			LOG_INFO(MOD_ZONE, "Server assigned new spawn ID: {} (was {})", newSpawnId, m_my_spawn_id);
			m_my_spawn_id = newSpawnId;
			m_game_state.player().setSpawnId(m_my_spawn_id);
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
		}
		break;

	case AT_INVISIBLE:
		// Invisibility state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_invisible = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} invisibility set to {}", spawn_id, it->second.is_invisible);
			}
			// TODO: Graphics rendering could hide/fade invisible entities
		}
		break;

	case AT_SNEAK:
		// Sneaking state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_sneaking = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} sneaking set to {}", spawn_id, it->second.is_sneaking);
			}
		}
		break;

	case AT_LINKDEAD:
		// Linkdead state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_linkdead = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} linkdead set to {}", spawn_id, it->second.is_linkdead);
			}
		}
		break;

	case AT_FLYMODE:
		// Levitate/fly mode change
		// 0=ground, 1=flying, 2=levitate, 3=water, 4=floating, 5=levitate+run
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.flymode = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} flymode set to {}", spawn_id, parameter);
			}
			// TODO: Graphics rendering could show levitation animation
		}
		break;

	case AT_AFK:
		// AFK state change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.is_afk = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} AFK set to {}", spawn_id, it->second.is_afk);
			}
		}
		break;

	case AT_ANONYMOUS:
		// Anonymous/roleplay status change: 0=normal, 1=anon, 2=roleplay
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.anon_status = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} anon_status set to {}", spawn_id, parameter);
			}
		}
		break;

	case AT_PET:
		// Pet owner ID update
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.pet_owner_id = parameter;
				it->second.is_pet = (parameter != 0) ? 1 : 0;
				LOG_DEBUG(MOD_ENTITY, "Entity {} pet_owner_id set to {}", spawn_id, parameter);
			}
		}
		break;

	case AT_SIZE:
		// Size change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				// Size is stored as a float, parameter contains size value
				it->second.size = static_cast<float>(parameter) / 10.0f;  // EQ uses size*10
				LOG_DEBUG(MOD_ENTITY, "Entity {} size set to {}", spawn_id, it->second.size);
			}
			// TODO: Graphics rendering could update entity scale
		}
		break;

	case AT_GUILD_ID:
		// T133: guild membership changed mid-session (join/leave/kick).
		// The server sends this to the WHOLE ZONE, including self
		// (Mob::SendAppearancePacket(AppearanceType::GuildID, ..., true) in
		// zone/guild.cpp -- confirmed from the eqemu server source, not
		// guessed), on every SetGuild() call. On leave, `parameter` is
		// GUILD_NONE (server-side 0xFFFFFFFF, common/guilds.h), NOT 0 --
		// normalize that to 0 so the `m_guild_id == 0` guards every
		// SendGuild*() sender relies on still correctly read "guildless"
		// instead of latching a bogus nonzero id that would let a
		// guildless player's guild commands reach the wire with garbage
		// data.
		{
			uint32_t normalized = (parameter == 0xFFFFFFFFu) ? 0u : parameter;
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.guild_id = normalized;
			}
			if (spawn_id == m_my_spawn_id) {
				m_guild_id = normalized;
				LOG_INFO(MOD_ENTITY, "Our guild id changed to {} via SpawnAppearance", normalized);
			}
		}
		break;

	case AT_LIGHT:
		// Light source change
		{
			auto it = m_entities.find(spawn_id);
			if (it != m_entities.end()) {
				it->second.light = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} light set to {}", spawn_id, parameter);
				if (m_bridge) {
					eqt::state::EntityLightChangedData data;
					data.spawnId = spawn_id;
					data.lightLevel = static_cast<uint8_t>(parameter);
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityLightChanged, std::move(data)));
				}
			}
		}
		break;

	default:
		// Unhandled appearance types - log for debugging
		LOG_DEBUG(MOD_ENTITY, "Unhandled SpawnAppearance type {} for spawn_id={}, parameter={}",
			type, spawn_id, parameter);
		break;
	}
}

void EverQuest::ZoneProcessGroundSpawn(const EQ::Net::Packet &p)
{
	// T144: OP_GroundSpawn is genuinely VARIABLE LENGTH. eqemu's
	// ENCODE(OP_GroundSpawn) sizes the body
	// `strlen(object_name) + sizeof(structs::Object_Struct) - 1` == strlen + 91
	// and only strcpy's the name into an otherwise uninitialised buffer, so:
	//   * the body is 91 bytes for an empty name and strlen+91 otherwise
	//     (observed live: 104 for "IT66_ACTORDEF", 107 for "IT10804_ACTORDEF"),
	//   * the tail of the 32-byte object_name field is heap garbage past the
	//     terminating NUL and must never be treated as part of the name,
	//   * a length check against sizeof(Object_Struct) is meaningless.
	// The old "Server may send shorter packets (e.g., 97 bytes instead of 102)"
	// comment that used to sit here was reading that as jitter. It is not
	// jitter; it is the format. That misreading is what let a struct which was
	// wrong from offset 8 onward survive.
	//
	// The layout and the field reads live in eqt::ParseGroundSpawn
	// (src/client/world_object.cpp) so a captured-bytes regression test can
	// exercise exactly this decode without an EverQuest instance.
	eqt::WorldObject worldObj;
	if (p.Length() < 2 ||
	    !eqt::ParseGroundSpawn(static_cast<const uint8_t*>(p.Data()) + 2, p.Length() - 2, worldObj)) {
		LOG_WARN(MOD_ENTITY, "GroundSpawn packet too small: {} bytes (need at least {})",
			p.Length(), 2 + eqt::GroundSpawnMinBodyBytes());
		return;
	}

	// Store the world object
	m_world_objects[worldObj.drop_id] = worldObj;

	// Publish WorldObjectSpawned event to bridge
	if (m_bridge) {
		eqt::state::WorldObjectSpawnedData wdata;
		wdata.dropId = worldObj.drop_id;
		wdata.x = worldObj.x;
		wdata.y = worldObj.y;
		wdata.z = worldObj.z;
		wdata.heading = worldObj.heading;
		wdata.modelName = worldObj.name;
		wdata.objectType = static_cast<uint8_t>(worldObj.object_type);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::WorldObjectSpawned, std::move(wdata)));
	}

	if (worldObj.isTradeskillContainer()) {
		LOG_DEBUG(MOD_ENTITY, "Tradeskill object spawned: id={} name='{}' type={} ({}) at ({:.1f}, {:.1f}, {:.1f})",
			worldObj.drop_id, worldObj.name, worldObj.object_type,
			worldObj.getTradeskillName(), worldObj.x, worldObj.y, worldObj.z);
	} else {
		LOG_TRACE(MOD_ENTITY, "Ground object spawned: id={} name='{}' type={} at ({:.1f}, {:.1f}, {:.1f})",
			worldObj.drop_id, worldObj.name, worldObj.object_type,
			worldObj.x, worldObj.y, worldObj.z);
	}
}

void EverQuest::ZoneProcessWeather(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Weather update received");

	m_weather_received = true;

	// Parse weather packet (8 bytes)
	// type: 0=rain off, 1=snow off, 2=snow on (type 0 + intensity > 0 = rain on)
	// intensity: 1-10
	if (p.Length() >= 10) {  // 2 byte opcode + 8 byte struct
		uint8_t type = p.GetUInt8(2);
		uint8_t intensity = p.GetUInt8(6);

		LOG_INFO(MOD_ZONE, "Weather packet: type={}, intensity={}", type, intensity);

		// Publish WeatherChanged event to bridge
		if (m_bridge) {
			eqt::state::WeatherChangedData data;
			data.type = type;
			data.intensity = intensity;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::WeatherChanged, std::move(data)));
		}
	}

	// Weather is the last packet in Zone Entry phase
	// Now we can send ReqNewZone
	if (!m_req_new_zone_sent) {
		ZoneSendReqNewZone();
	}
}

void EverQuest::ZoneProcessNewSpawn(const EQ::Net::Packet &p)
{
	// NewSpawn packet is 387 bytes total: opcode (2 bytes) + Spawn_Struct (385 bytes)
	// The spawn_id is embedded within the Spawn_Struct, not separate
	
	if (p.Length() < 387) {
		LOG_WARN(MOD_ENTITY, "NewSpawn packet too small: {} bytes (expected 387)", p.Length());
		return;
	}
	
	Entity entity;
	
	// Spawn_Struct starts at offset 2 (after opcode)
	size_t offset = 2;
	
	// Use the same parsing logic as ZoneSpawns
	// spawnId is at offset 340 in the struct (uint32)
	entity.spawn_id = p.GetUInt32(offset + 340);
	
	// Name is at offset 7 in Spawn_Struct
	entity.name = p.GetCString(offset + 7);
	
	// Level at offset 151
	entity.level = p.GetUInt8(offset + 151);
	
	// Position data from the bitfield structure (same as ZoneSpawns):
	uint32_t field1 = p.GetUInt32(offset + 94);  // contains deltaHeading and x
	uint32_t field2 = p.GetUInt32(offset + 98);  // contains y and animation
	uint32_t field3 = p.GetUInt32(offset + 102); // contains z and deltaY
	uint32_t field4 = p.GetUInt32(offset + 106); // contains deltaX and heading
	
	// Extract x, y, z (19 bits each, signed)
	// Positions are stored as fixed-point values (multiplied by 8) -- same
	// encoding as ZoneSpawns (eq_entities.cpp ZoneProcessZoneSpawns). T83:
	// this path was missing the /8.0f that ZoneSpawns already applies,
	// producing coordinates 8x too large for any spawn that arrives via a
	// standalone NewSpawn packet instead of the initial bulk burst (see
	// T83's Review for the nektulos before/after evidence -- entities like
	// "a_deathfist_scout" at raw (-16544,5656,-19) belong at (-2068,707,-2.4)).
	// Note: Server x/y are swapped relative to zone geometry, so we swap them here
	int32_t x_raw = (field1 >> 10) & 0x7FFFF;
	if (x_raw & 0x40000) x_raw |= 0xFFF80000; // Sign extend
	float server_x = static_cast<float>(x_raw) / 8.0f;

	int32_t y_raw = field2 & 0x7FFFF;
	if (y_raw & 0x40000) y_raw |= 0xFFF80000; // Sign extend
	float server_y = static_cast<float>(y_raw) / 8.0f;

	int32_t z_raw = field3 & 0x7FFFF;
	if (z_raw & 0x40000) z_raw |= 0xFFF80000; // Sign extend
	entity.z = static_cast<float>(z_raw) / 8.0f;

	// Swap x and y from server
	entity.x = server_y;
	entity.y = server_x;

	// Read npc_type early - needed to determine heading transformation
	// NPC type: 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
	entity.npc_type = p.GetUInt8(offset + 83);
	bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

	// Extract heading (11 bits to match C->S format)
	// Server uses 512 EQ units = 360 degrees, so multiply by 360/2048
	uint32_t raw_heading = (field4 >> 13) & 0x7FF;
	float server_heading = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (NPCs and players)
	entity.heading = server_heading;
	if (entity.heading >= 360.0f) entity.heading -= 360.0f;

	// Debug logging for current player at level 1+
	if (s_debug_level >= 1 && entity.name == m_character) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] spawn_id={} name='{}'", entity.spawn_id, entity.name);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] raw_heading={} -> server_heading={:.2f}deg -> entity.heading={:.2f}deg (isNPC={})",
			raw_heading, server_heading, entity.heading, isNPC);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C NewSpawn [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> entity_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, entity.z, entity.x, entity.y, entity.z);
	}

	// race is at offset 284 (uint32)
	entity.race_id = p.GetUInt32(offset + 284);

	// class_ is at offset 331
	entity.class_id = p.GetUInt8(offset + 331);

	// gender is at offset 334
	entity.gender = p.GetUInt8(offset + 334);

	// guildID is at offset 238
	entity.guild_id = p.GetUInt32(offset + 238);

	// curHp is at offset 86 (hp percentage)
	entity.hp_percent = p.GetUInt8(offset + 86);
	
	// animation is in the bitfield at offset 98
	entity.animation = (field2 >> 19) & 0x3FF;

	// size is at offset 75 (float)
	entity.size = *reinterpret_cast<const float*>(static_cast<const char*>(p.Data()) + offset + 75);

	// Appearance data from Spawn_Struct
	entity.face = p.GetUInt8(offset + 6);           // Face/head variant
	// npc_type already read above for heading transformation
	entity.haircolor = p.GetUInt8(offset + 85);     // Hair color
	entity.showhelm = p.GetUInt8(offset + 139);     // Show helm
	entity.hairstyle = p.GetUInt8(offset + 145);    // Hair style
	entity.beardcolor = p.GetUInt8(offset + 146);   // Beard color
	entity.beard = p.GetUInt8(offset + 156);        // Beard style
	entity.helm = p.GetUInt8(offset + 275);         // Helm texture
	entity.light = p.GetUInt8(offset + 330);        // Light source
	entity.bodytype = p.GetUInt8(offset + 335);     // Body type
	entity.equip_chest2 = p.GetUInt8(offset + 339); // Body texture variant

	// Equipment textures (TextureProfile at offset 197, 9 slots * 4 bytes)
	for (int i = 0; i < 9; i++) {
		entity.equipment[i] = p.GetUInt32(offset + 197 + i * 4);
	}

	// Equipment tint colors (TintProfile at offset 348, 9 slots * 4 bytes)
	for (int i = 0; i < 9; i++) {
		entity.equipment_tint[i] = p.GetUInt32(offset + 348 + i * 4);
	}

	// Initialize movement fields
	entity.delta_x = 0;
	entity.delta_y = 0;
	entity.delta_z = 0;
	entity.delta_heading = 0;
	entity.last_update_time = std::time(nullptr);

	// Validate the spawn data before adding
	if (entity.spawn_id > 0 && entity.spawn_id < 100000 && !entity.name.empty()) {
		// Check if this is our own spawn by comparing name
		if (entity.name == m_character) {
			m_my_spawn_id = entity.spawn_id;
			// Phase 7.4: Also update PlayerState
			m_game_state.player().setSpawnId(m_my_spawn_id);
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
			if (s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Found our own spawn in NewSpawn! Name: {}, Spawn ID: {}, server pos=({:.2f}, {:.2f}, {:.2f}) size={:.2f}",
					entity.name, m_my_spawn_id, entity.x, entity.y, entity.z, entity.size);
			}
			// Update our position to match server spawn position
			m_x = entity.x;
			m_y = entity.y;
			m_size = entity.size;
			// Server Z is model center. Convert to feet position for internal use.
			// SendPositionUpdate will add m_size/2 to convert back to model center.
			m_z = entity.z - (entity.size / 2.0f);

			if (s_debug_level >= 1) {
				LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from NewSpawn: ({:.2f},{:.2f},{:.2f}) (feet Z, server Z was {:.2f})",
					m_x, m_y, m_z, entity.z);
			}

			// Update bridge with corrected feet Z position
			if (m_bridge) {
				float heading512 = m_heading * 512.0f / 360.0f;
				eqt::state::PlayerMovedData pdata;
				pdata.x = m_x; pdata.y = m_y; pdata.z = m_z;
				pdata.heading = heading512;
				pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
				pdata.isMoving = false;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PlayerMoved, std::move(pdata)));
			}
		}

		// Store pet info in entity (for deferred pet detection)
		// is_pet is at offset 329 in Spawn_Struct (uint8_t)
		// petOwnerId is at offset 189 in Spawn_Struct (uint32_t)
		// MUST be set BEFORE adding to m_entities
		entity.is_pet = p.GetUInt8(offset + 329);
		entity.pet_owner_id = p.GetUInt32(offset + 189);

		// Add to entity list (sync to both local map and EntityManager)
		m_entities[entity.spawn_id] = entity;
		SyncEntityToGameState(entity);

#ifdef EQT_HAS_GRAPHICS
		OnSpawnAddedGraphics(entity);
		// Notify bridge of player spawn ID (D17b: bridge is sole path to renderer)
		if (entity.name == m_character && m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::PlayerSpawnIdSet,
				eqt::state::PlayerSpawnIdSetData{m_my_spawn_id}));
		}
#endif

		// Check if this spawn is our pet (if we already know our spawn ID)
		if (entity.is_pet != 0 && entity.pet_owner_id == m_my_spawn_id && m_my_spawn_id != 0) {
			m_pet_spawn_id = entity.spawn_id;

			// Initialize default button states (Follow=ON, others=OFF per server behavior)
			std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
			m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;

			LOG_INFO(MOD_MAIN, "Pet detected: {} (ID: {}) Level {} owned by us (ID: {})",
				entity.name, entity.spawn_id, entity.level, m_my_spawn_id);

			// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical note at
			// the ZoneProcessSpawnAll second-pass call site above.
			OnPetCreated(entity);
		}

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ENTITY, "New spawn: {} (ID: {}) Level {} {} Race {} at ({:.2f}, {:.2f}, {:.2f})",
				entity.name, entity.spawn_id, entity.level,
				entity.class_id, entity.race_id, entity.x, entity.y, entity.z);
		}
	} else {
		LOG_WARN(MOD_ENTITY, "Invalid spawn data in NewSpawn: ID={}, Name='{}'",
			entity.spawn_id, entity.name);
	}
}

void EverQuest::ZoneProcessExpUpdate(const EQ::Net::Packet &p)
{
	// ExpUpdate_Struct: { uint32_t exp; uint32_t aaxp; }
	const size_t base = 2;  // Skip opcode

	if (p.Length() < base + 8) {
		LOG_WARN(MOD_ZONE, "ExpUpdate packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t exp = p.GetUInt32(base + 0);
	uint32_t aaxp = p.GetUInt32(base + 4);

	LOG_DEBUG(MOD_ZONE, "Received exp update: exp={} aaxp={} (was exp={} aaxp={})",
	          exp, aaxp, m_exp, m_exp_aa);

	m_exp = exp;
	m_exp_aa = aaxp;

	// Publish ExpProgressChanged event
	if (m_bridge) {
		eqt::state::ExpProgressChangedData data;
		data.progress = CalculateExpProgress(m_exp, m_level);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ExpProgressChanged, std::move(data)));
	}
}

// T169: every raid membership event arrives here, and until now this
// function read one uint32 and switched on six guessed values -- four of
// which disagreed with eqemu's own enum (common/raid.h). What that cost:
//   * action 2 was reported as "member joined"; it is raidMemberNameChange.
//   * action 3 was reported as "the raid leader has changed"; it is
//     raidRemove1, a member LEAVING.
//   * action 6 was reported as "the raid has disbanded"; it is raidMembers,
//     the full member list. The real disband is action 5, which was unhandled.
//   * action 10 was reported as a lock-state update; it is raidNoRaid, the
//     server telling us we are not in a raid. Lock/unlock are 17 and 18.
//   * action 20 -- what a raid INVITE actually produces -- fell through to
//     "Unknown raid action", so an invited player was told NOTHING AT ALL.
// The body was never parsed, so no message could name anyone.
//
// Decoding now lives in DecodeRaidUpdate() (raid_update.cpp), where the
// per-action body shapes are documented and pinned by test. This function is
// only the rendering.
//
// T179 added the accept path and the /raid commands; T180 added the roster
// below and split the zone-in handshake back out of action 10. The member-list
// parse T169 expected (action 6) turned out to be unavailable on Titanium --
// its ENCODE has no case for raidMembers and ships the general with the list
// stripped -- so the roster is assembled from the action-0 stream instead.
// See raid_update.h.
void EverQuest::ZoneProcessRaidUpdate(const EQ::Net::Packet &p)
{
	const EQT::RaidUpdate u = EQT::DecodeRaidUpdate(p, 2);

	if (!u.valid) {
		LOG_WARN(MOD_ZONE, "RaidUpdate too short for action {} ({}): {} bytes",
		         u.raw_action, EQT::RaidActionName(u.raw_action), p.Length());
		return;
	}

	// T180: not every OP_RaidUpdate is a raid event. Client::CompleteConnect
	// reuses this opcode for the zone-in name handshake, whose first uint32
	// happens to be 0x0A -- the raidNoRaid code. It arrives on EVERY zone
	// entry, with or without a raid. Until T180 this produced a WARN per
	// zone-in ("too short for action 10"); reporting it as "You are not in a
	// raid" instead would have been worse, since it would have cleared real
	// raid membership every time the player zoned.
	if (u.zone_in_name) {
		LOG_DEBUG(MOD_ZONE, "RaidUpdate: zone-in name handshake for '{}' (not a raid event)",
		          u.player_name);
		return;
	}

	LOG_DEBUG(MOD_ZONE, "RaidUpdate action={} ({}) player='{}' leader='{}' parameter={}",
	          u.raw_action, EQT::RaidActionName(u.raw_action),
	          u.player_name, u.leader_name, u.parameter);

	// Membership first, then the message: /raid list must be right the instant
	// the player is told something changed. The policy lives in RaidRoster so
	// it is testable without a live client.
	m_raid_roster.Apply(u, m_character);

	// Every event names a member, and several of them are about US. eqemu
	// sends the same packet to the whole raid, so without this comparison the
	// client would announce "You have joined the raid" for every member.
	const bool about_me = !u.player_name.empty() && u.player_name == m_character;

	switch (u.action) {
	case EQT::RaidAction::Add:
		if (about_me) {
			m_in_raid = true;
			m_pending_raid_inviter.clear();  // accepted / already in
			AddChatSystemMessage("You have joined the raid.");
		} else {
			AddChatSystemMessage(fmt::format("{} has joined the raid.", u.player_name));
		}
		break;

	case EQT::RaidAction::Remove1:
	case EQT::RaidAction::Remove2:
		if (about_me) {
			m_in_raid = false;
			m_raid_leader.clear();
			m_raid_locked = false;
			AddChatSystemMessage("You have left the raid.");
		} else {
			AddChatSystemMessage(fmt::format("{} has left the raid.", u.player_name));
		}
		break;

	case EQT::RaidAction::Disband:
		m_in_raid = false;
		m_raid_leader.clear();
		m_pending_raid_inviter.clear();
		m_raid_locked = false;
		AddChatSystemMessage("The raid has disbanded.");
		break;

	case EQT::RaidAction::Create:
		m_in_raid = true;
		m_raid_leader = u.leader_name;
		// A fresh raid is unlocked; Create also starts a fresh roster stream.
		m_raid_locked = false;
		AddChatSystemMessage(fmt::format("{} has formed a raid.", u.leader_name));
		break;

	case EQT::RaidAction::NoRaid:
		// Only a genuine 140-byte raidNoRaid reaches here; the 136-byte zone-in
		// handshake that also presents 10 returned above (T180).
		m_in_raid = false;
		m_raid_leader.clear();
		m_pending_raid_inviter.clear();
		m_raid_locked = false;
		AddChatSystemMessage("You are not in a raid.");
		break;

	case EQT::RaidAction::SetLeader:
		// The invite. eqemu builds this in Handle_OP_RaidCommand's
		// RaidCommandInvite arm (client_packet.cpp:12356): leader_name is the
		// player inviting, player_name is us.
		//
		// There is deliberately no accept here. Accepting means sending
		// OP_RaidCommand with action 1 (RaidCommandAcceptInvite),
		// player_name = the inviter, leader_name = us -- a sender this client
		// does not have (T168 deleted the broken raid sender rather than
		// leave it parked). Saying so out loud beats the previous behaviour,
		// which was to say nothing and log "Unknown raid action: 20".
		// T179: leader_name is the inviter (see the decode header). Remember it
		// so /raid accept can answer, and tell the player how.
		m_pending_raid_inviter = u.leader_name;
		AddChatSystemMessage(fmt::format(
			"{} invites you to join a raid. Type /raid accept to join.",
			u.leader_name));
		break;

	case EQT::RaidAction::MakeLeader:
		m_raid_leader = u.player_name;
		AddChatSystemMessage(fmt::format("{} is now the raid leader.", u.player_name));
		break;

	case EQT::RaidAction::NoLongerLeader:
		AddChatSystemMessage("You are no longer the raid leader.");
		break;

	case EQT::RaidAction::Lock:
	case EQT::RaidAction::Unlock: {
		// T182. eqemu sends the SAME action whether the state just CHANGED
		// (LockRaid -> SendRaidLock, broadcast to the raid) or was ALREADY
		// what you asked for (Handle_OP_RaidCommand -> SendRaidLockTo, to you
		// alone). The packets are identical, so "now locked" vs "already
		// locked" can only come from the state we track -- which is also why
		// we track it. It is re-established on zone-in: the rejoin path sends
		// SendRaidLockTo only when the raid is locked.
		const bool locking = (u.action == EQT::RaidAction::Lock);
		const bool changed = (m_raid_locked != locking);
		m_raid_locked = locking;
		if (changed) {
			AddChatSystemMessage(locking ? "The raid is now locked."
			                             : "The raid is now unlocked.");
		} else {
			AddChatSystemMessage(locking ? "The raid is already locked."
			                             : "The raid is not locked.");
		}
		break;
	}

	case EQT::RaidAction::SetMotd:
		if (!u.text.empty()) {
			AddChatSystemMessage(fmt::format("Raid MOTD: {}", u.text));
		}
		break;

	case EQT::RaidAction::SetNote:
		LOG_DEBUG(MOD_ZONE, "Raid note for {}: '{}'", u.player_name, u.text);
		break;

	case EQT::RaidAction::Members:
		// The roster (details + member list, 395+ bytes). Parsing it is the
		// raid unit's job; announcing something we have not parsed would be
		// the same class of guess this unit is removing.
		LOG_DEBUG(MOD_ZONE, "Raid member list received ({} bytes) -- not parsed", p.Length());
		break;

	default:
		// Named, not guessed: known-but-unhandled and genuinely unknown are
		// different facts and now read differently in the log.
		LOG_DEBUG(MOD_ZONE, "Raid action {} ({}) not handled by this client",
		          u.raw_action, EQT::RaidActionName(u.raw_action));
		break;
	}
}

void EverQuest::ZoneProcessClientUpdate(const EQ::Net::Packet &p)
{
	// ClientUpdate packets contain position updates from other players
	// Format: eqemu's 22-byte bit-packed PlayerPositionUpdateServer_Struct
	// (common/patches/titanium_structs.h:1201). T147 deleted the 30-byte
	// EQT::PlayerPositionUpdateServer_Struct that claimed to be this:
	// spawn_id (2 bytes) + 20 bytes of bit-packed data

	if (p.Length() < 24) { // 2 (opcode) + 2 (spawn_id) + 20 (bit-packed data)
		LOG_WARN(MOD_ZONE, "ClientUpdate packet too small: {} bytes", p.Length());
		return;
	}

	// Read spawn_id (first 2 bytes after opcode)
	uint16_t spawn_id = p.GetUInt16(2);

	// Log if this is potentially our own ClientUpdate
	if (m_my_spawn_id != 0 && spawn_id == m_my_spawn_id) {
		LOG_INFO(MOD_ZONE, "*** RECEIVED OUR OWN ClientUpdate! spawn_id={} ***", spawn_id);
	}
	
	// Read the bit-packed data as 5 32-bit integers
	uint32_t field1 = p.GetUInt32(4);  // delta_heading:10, x_pos:19, padding:3
	uint32_t field2 = p.GetUInt32(8);  // y_pos:19, animation:10, padding:3
	uint32_t field3 = p.GetUInt32(12); // z_pos:19, delta_y:13
	uint32_t field4 = p.GetUInt32(16); // delta_x:13, heading:12, padding:7
	uint32_t field5 = p.GetUInt32(20); // delta_z:13, padding:19
	
	// Extract values from bit fields
	int32_t delta_heading = (field1 & 0x3FF);          // bits 0-9 (10 bits)
	int32_t x_pos_raw = (field1 >> 10) & 0x7FFFF;      // bits 10-28 (19 bits)
	
	int32_t y_pos_raw = (field2 & 0x7FFFF);            // bits 0-18 (19 bits)
	int32_t animation = (field2 >> 19) & 0x3FF;        // bits 19-28 (10 bits, signed)
	
	int32_t z_pos_raw = (field3 & 0x7FFFF);            // bits 0-18 (19 bits)
	int32_t delta_y = (field3 >> 19) & 0x1FFF;         // bits 19-31 (13 bits)
	
	int32_t delta_x = (field4 & 0x1FFF);               // bits 0-12 (13 bits)
	uint32_t heading = (field4 >> 13) & 0xFFF;         // bits 13-24 (12 bits)
	
	int32_t delta_z = (field5 & 0x1FFF);               // bits 0-12 (13 bits)
	
	// Sign extend delta values (they are signed)
	if (delta_heading & 0x200) delta_heading |= 0xFFFFFC00; // sign extend from 10 bits
	if (delta_x & 0x1000) delta_x |= 0xFFFFE000;           // sign extend from 13 bits
	if (delta_y & 0x1000) delta_y |= 0xFFFFE000;           // sign extend from 13 bits
	if (delta_z & 0x1000) delta_z |= 0xFFFFE000;           // sign extend from 13 bits

	// Sign extend animation (negative = play animation in reverse)
	// abs(animation) = animation ID, sign = playback direction
	if (animation & 0x200) animation |= 0xFFFFFC00;        // sign extend from 10 bits
	
	// Sign extend position values (they are signed 19-bit integers)
	if (x_pos_raw & 0x40000) x_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	if (y_pos_raw & 0x40000) y_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	if (z_pos_raw & 0x40000) z_pos_raw |= 0xFFF80000;      // sign extend from 19 bits
	
	// Convert raw position values to floats (divided by 8 for fixed-point)
	// Note: Server x/y are swapped relative to zone geometry
	float server_x = static_cast<float>(x_pos_raw) / 8.0f;
	float server_y = static_cast<float>(y_pos_raw) / 8.0f;
	float x = server_y;  // Swap
	float y = server_x;  // Swap
	float z = static_cast<float>(z_pos_raw) / 8.0f;
	// Delta values must also be swapped to match position coordinate system
	float server_dx = static_cast<float>(delta_x) / 8.0f;
	float server_dy = static_cast<float>(delta_y) / 8.0f;
	float dx = server_dy;  // Swap to match position swap
	float dy = server_dx;  // Swap to match position swap
	float dz = static_cast<float>(delta_z) / 8.0f;
	float dh = static_cast<float>(delta_heading);
	// Convert heading to degrees: 512 EQ units = 360 degrees
	uint32_t raw_heading = heading & 0x7FF;  // Lower 11 bits
	float server_h = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (no mirroring)
	float h_player = server_h;
	if (h_player >= 360.0f) h_player -= 360.0f;
	float h_npc = server_h;
	if (h_npc >= 360.0f) h_npc -= 360.0f;

	// Debug: log position updates
	// Level 1: current player, current target, or tracked targets
	// Level 2+: all entities
	uint16_t currentTargetId = m_combat_manager ? m_combat_manager->GetTargetId() : 0;
	bool isCurrentPlayer = (spawn_id == m_my_spawn_id);
	bool shouldLogPosition = (s_debug_level >= 2) ||
	                         (s_debug_level >= 1 && (isCurrentPlayer || spawn_id == currentTargetId || IsTrackedTarget(spawn_id)));
	if (shouldLogPosition) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C spawn_id={} pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} delta=({:.2f}, {:.2f}, {:.2f}) (my_id={})",
			spawn_id, x, y, z, server_h, animation, dx, dy, dz, m_my_spawn_id);
	}
	// Additional detailed logging for current player at debug level 1+
	if (s_debug_level >= 1 && isCurrentPlayer) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] raw_heading={} (12-bit field={}) -> server_h={:.2f}deg -> h_player={:.2f}deg",
			raw_heading, heading, server_h, h_player);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, x, y, z);
	}

	// Check if this is an update for our own character
	if (spawn_id == m_my_spawn_id) {
		// Update our position to match what the server says
		m_x = x;
		m_y = y;
		m_z = z;
		// S->C transformation for our player: inverse of C->S (server_heading = 90 - m_heading)
		m_heading = 90.0f - server_h;
		if (m_heading < 0.0f) m_heading += 360.0f;
		if (m_heading >= 360.0f) m_heading -= 360.0f;

		// Track whether we need to create player entity on renderer
		bool needPlayerEntityCreation = false;

		// Also update our spawn ID if not set
		if (m_my_spawn_id == 0) {
			m_my_spawn_id = spawn_id;
			// Phase 7.4: Also update PlayerState
			m_game_state.player().setSpawnId(m_my_spawn_id);
			if (m_trade_manager) {
				m_trade_manager->setMySpawnId(m_my_spawn_id);
			}
			LOG_INFO(MOD_MOVEMENT, "Set our spawn ID to {} from ClientUpdate", m_my_spawn_id);

			// Deferred pet detection: now that we know our spawn ID, check if any
			// existing entity is our pet (spawns arrive before we know our ID)
			if (m_pet_spawn_id == 0) {
				for (const auto& pair : m_entities) {
					const Entity& ent = pair.second;
					if (ent.is_pet != 0 && ent.pet_owner_id == m_my_spawn_id) {
						m_pet_spawn_id = ent.spawn_id;
						std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
						m_pet_button_states[EQT::PET_BUTTON_FOLLOW] = true;
						LOG_INFO(MOD_MAIN, "Pet detected (deferred): {} (ID: {}) Level {} owned by us (ID: {})",
							ent.name, ent.spawn_id, ent.level, m_my_spawn_id);
						// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical
						// note at the ZoneProcessSpawnAll second-pass call site.
						OnPetCreated(ent);
						break;
					}
				}
			}

			needPlayerEntityCreation = true;
		}

		// Also create player entity if it's pending (after zoning, entities were cleared)
		if (m_player_graphics_entity_pending && m_my_spawn_id != 0) {
			needPlayerEntityCreation = true;
		}

		// Now create the player entity via bridge (D17b: bridge is sole path to renderer)
		// Deferred from OnZoneLoadedGraphics to ensure all player data has been received
		if (needPlayerEntityCreation && m_bridge) {
			auto playerIt = m_entities.find(m_my_spawn_id);
			if (playerIt != m_entities.end()) {
				const Entity& entity = playerIt->second;

				LOG_INFO(MOD_ENTITY, "Creating player entity {} ({}) from ClientUpdate - equipment: primary={} secondary={}",
				         m_my_spawn_id, entity.name, entity.equipment[7], entity.equipment[8]);

				// Publish EntitySpawned for player entity
				eqt::state::EntitySpawnedData data;
				data.spawnId = m_my_spawn_id;
				data.name = entity.name;
				data.x = x;
				data.y = y;
				data.z = z;
				data.heading = h_player;
				data.raceId = entity.race_id;
				data.classId = entity.class_id;
				data.level = entity.level;
				data.gender = entity.gender;
				data.npcType = entity.npc_type;
				data.isCorpse = false;
				data.isPlayer = true;
				data.serverSize = entity.size;
				data.face = entity.face;
				data.haircolor = entity.haircolor;
				data.hairstyle = entity.hairstyle;
				data.beardcolor = entity.beardcolor;
				data.beard = entity.beard;
				data.texture = entity.equip_chest2;
				data.helm = entity.helm;
				for (int i = 0; i < 9; i++) {
					data.equipment[i] = entity.equipment[i];
					data.equipmentTint[i] = entity.equipment_tint[i];
				}
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::EntitySpawned, std::move(data)));

				// Publish PlayerSpawnIdSet so renderer marks the player node
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PlayerSpawnIdSet,
					eqt::state::PlayerSpawnIdSetData{m_my_spawn_id}));

				// Set entity light source if equipped
				if (entity.light > 0) {
					eqt::state::EntityLightChangedData lightData;
					lightData.spawnId = m_my_spawn_id;
					lightData.lightLevel = entity.light;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityLightChanged, std::move(lightData)));
				}

				// Player entity has been created, clear the pending flag
				m_player_graphics_entity_pending = false;
			} else {
				LOG_WARN(MOD_ENTITY, "Player entity {} not found in m_entities when setting spawn ID from ClientUpdate", m_my_spawn_id);
			}
		}

		// Start the update loop when we first receive our ClientUpdate
		// This indicates we're fully in the game world
		if (!m_update_running) {
			SetLoadingPhase(LoadingPhase::ZONE_AWAITING_CONFIRM, "Player confirmed...");
			LOG_INFO(MOD_ZONE, "Zone connection complete! Player position confirmed (spawn_id={}).", m_my_spawn_id);
			m_game_state.world().setZoneConnected(true);

			// Signal renderer that network phase is complete and entities are ready (D17b: bridge-only)
			if (m_bridge) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::NetworkReady,
					eqt::state::NetworkReadyData{static_cast<uint32_t>(m_entities.size()), true}));
				LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, network ready: true", m_entities.size());
			}

			StartUpdateLoop();
			// Game state is now complete - trigger graphics loading if graphics mode
			OnGameStateComplete();
		}
		
		// Update our entity in the entity list
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.x = x;
			it->second.y = y;
			it->second.z = z;
			it->second.heading = h_player;
			it->second.animation = animation;
			it->second.delta_x = dx;
			it->second.delta_y = dy;
			it->second.delta_z = dz;
			it->second.delta_heading = dh;
			it->second.last_update_time = std::time(nullptr);

			// T221: source-side write; the EntityMoved projection becomes
			// redundant (D24 coalesced it per spawn -- last write wins on
			// both sides, so applying every packet here is state-identical).
			m_game_state.entities().updateEntityPosition(
				m_my_spawn_id, x, y, z, h_player, dx, dy, dz,
				static_cast<uint8_t>(animation));

#ifdef EQT_HAS_GRAPHICS
			OnSpawnMovedGraphics(m_my_spawn_id, x, y, z, h_player, dx, dy, dz, animation);
#endif
		}
		return;
	}

	// Update entity position in our list
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		// Use heading directly for all entity types
		bool isNPC = (it->second.npc_type == 1 || it->second.npc_type == 3);
		float entity_heading = isNPC ? h_npc : h_player;

		it->second.x = x;
		it->second.y = y;
		it->second.z = z;
		it->second.heading = entity_heading;
		it->second.animation = animation;
		it->second.delta_x = dx;
		it->second.delta_y = dy;
		it->second.delta_z = dz;
		it->second.delta_heading = dh;
		it->second.last_update_time = std::time(nullptr);

		// T221: source-side write (see the self branch above).
		m_game_state.entities().updateEntityPosition(
			spawn_id, x, y, z, entity_heading, dx, dy, dz,
			static_cast<uint8_t>(animation));

#ifdef EQT_HAS_GRAPHICS
		OnSpawnMovedGraphics(spawn_id, x, y, z, entity_heading, dx, dy, dz, animation);
#endif

	}
}

void EverQuest::ZoneProcessDeleteSpawn(const EQ::Net::Packet &p)
{
	// DeleteSpawn is sent when an entity despawns
	// Format: spawn_id (2 bytes)
	
	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "DeleteSpawn packet too small: {} bytes", p.Length());
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);

	// Special case: spawn_id 0 might mean "delete current target"
	if (spawn_id == 0 && m_combat_manager && m_combat_manager->HasTarget()) {
		spawn_id = m_combat_manager->GetTargetId();
		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "DeleteSpawn with ID 0 interpreted as current target: {}", spawn_id);
		}
	}
	
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		// Handle corpse deletion - only delete corpses we've finished looting
		if (it->second.is_corpse) {
			// If we're actively looting this corpse, close the loot window first
			if (m_player_looting_corpse_id == spawn_id) {
				LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) being deleted while looting, closing loot window", spawn_id, it->second.name);
				// Publish LootWindowClosed (D18a: bridge is sole path to renderer)
				if (m_bridge) {
					eqt::state::LootWindowClosedData lwdata;
					lwdata.corpseId = spawn_id;
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::LootWindowClosed, std::move(lwdata)));
				}
				m_loot_items.clear();
				m_player_looting_corpse_id = 0;
				m_loot_all_in_progress = false;
				m_loot_all_remaining_slots.clear();
				m_game_state.player().clearLootingCorpse();  // Phase 7.7
				// Mark as ready for deletion
				m_loot_complete_corpse_id = spawn_id;
			}

			// Only delete corpses we've completed looting
			// This prevents corpses from being deleted when NPCs die (server sends DeleteSpawn(0))
			if (m_loot_complete_corpse_id != spawn_id) {
				LOG_TRACE(MOD_ENTITY, "Ignoring DeleteSpawn for corpse {} ({}) - not finished looting", spawn_id, it->second.name);
				return;
			}

			LOG_DEBUG(MOD_ENTITY, "Corpse {} ({}) removed by server after looting", spawn_id, it->second.name);
			m_loot_complete_corpse_id = 0;  // Clear the flag
		}

		LOG_DEBUG(MOD_ENTITY, "Entity {} ({}) despawned", spawn_id, it->second.name);

		// Cancel trade if partner despawned
		if (m_trade_manager && m_trade_manager->isTrading() &&
			m_trade_manager->getPartnerSpawnId() == spawn_id) {
			LOG_DEBUG(MOD_MAIN, "Trade partner despawned, canceling trade");
			m_trade_manager->cancelTrade();
			AddChatSystemMessage("Trade cancelled - partner is no longer available");
		}

		// Check if our pet despawned
		if (spawn_id == m_pet_spawn_id) {
			LOG_INFO(MOD_MAIN, "Pet {} ({}) despawned", spawn_id, it->second.name);
			m_pet_spawn_id = 0;
			std::fill(std::begin(m_pet_button_states), std::end(m_pet_button_states), false);
			// Clear pet buffs
			if (m_buff_manager) {
				m_buff_manager->clearPetBuffs();
			}
			// T221: was #ifdef EQT_HAS_GRAPHICS -- same T43 reasoning as
			// OnPetCreated() above: a pure bridge publish (now also the
			// source-side pet().clearPet()); gating it left the headless
			// build's pet state set forever after a despawn.
			OnPetRemoved();
		}

		// Check if our combat target despawned - disable auto-attack
		if (m_combat_manager && m_combat_manager->HasTarget() &&
			m_combat_manager->GetTargetId() == spawn_id) {
			LOG_DEBUG(MOD_COMBAT, "Combat target {} ({}) despawned, disabling auto-attack", spawn_id, it->second.name);
			m_combat_manager->DisableAutoAttack();
			m_combat_manager->ClearTarget();
		}

#ifdef EQT_HAS_GRAPHICS
		OnSpawnRemovedGraphics(spawn_id);
#endif

		// Remove from both local map and EntityManager
		m_entities.erase(it);
		RemoveEntityFromGameState(spawn_id);
	} else {
		LOG_DEBUG(MOD_ENTITY, "DeleteSpawn for unknown spawn_id: {}", spawn_id);
	}
}

void EverQuest::ZoneProcessMobHealth(const EQ::Net::Packet &p)
{
	// MobHealth updates an entity's health percentage
	// Format: spawn_id (2 bytes), hp_percent (1 byte)
	
	if (p.Length() < 5) {
		LOG_WARN(MOD_ENTITY, "MobHealth packet too small: {} bytes", p.Length());
		return;
	}
	
	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t hp_percent = p.GetUInt8(4);
	
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		uint8_t old_hp = it->second.hp_percent;
		it->second.hp_percent = hp_percent;
		if (s_debug_level >= 2 || IsTrackedTarget(spawn_id)) {
			LOG_DEBUG(MOD_ENTITY, "[HP] Entity {} ({}) health: {}% -> {}%", spawn_id, it->second.name, old_hp, hp_percent);
		}
		// T221: source-side writes, hoisted out of the bridge guard below.
		// Target HP: the TargetChanged re-publish's projection (T104) is
		// redundant with this. Pet HP: the PetStatsChanged projection (T40)
		// is redundant with this. manaPercent keeps T40's honest derivation
		// (reads 0 for every pet until something populates cur/max_mana).
		if (m_current_target_id == spawn_id) {
			m_game_state.combat().setTarget(spawn_id, it->second.name, hp_percent, it->second.level);
		}
		if (m_pet_spawn_id == spawn_id) {
			uint8_t manaPercent = (it->second.max_mana > 0)
				? static_cast<uint8_t>((it->second.cur_mana * 100) / it->second.max_mana)
				: 0;
			m_game_state.pet().updatePetStats(hp_percent, manaPercent);
		}
		// Update target HP via bridge if this is our current target (D17b)
		// Note: EntityStatsChanged event published below covers this
		// Publish EntityStatsChanged event
		if (m_bridge) {
			eqt::state::EntityStatsChangedData data;
			data.spawnId = spawn_id;
			data.hpPercent = hp_percent;
			data.curMana = 0;
			data.maxMana = 0;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityStatsChanged, std::move(data)));
			// If this entity is the current target, push TargetChanged with updated HP
			if (m_current_target_id == spawn_id) {
				eqt::state::TargetChangedData tdata;
				tdata.spawnId = spawn_id;
				tdata.hpPercent = hp_percent;
				tdata.name = it->second.name;
				tdata.level = it->second.level;
				tdata.raceId = it->second.race_id;
				tdata.gender = it->second.gender;
				tdata.classId = it->second.class_id;
				tdata.bodyType = it->second.bodytype;
				tdata.npcType = it->second.npc_type;
				tdata.helm = it->second.helm;
				tdata.showHelm = it->second.showhelm;
				tdata.texture = it->second.equip_chest2;
				for (int i = 0; i < 9; ++i) {
					tdata.equipment[i] = it->second.equipment[i];
					tdata.equipmentTint[i] = it->second.equipment_tint[i];
				}
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TargetChanged, std::move(tdata)));
			}
			// T40: if this entity is our pet, push PetStatsChanged with the
			// updated HP. manaPercent is computed from the entity record for
			// honesty rather than fabricated -- but Entity::cur_mana/max_mana
			// are populated only for the player's own entity (ManaChange
			// packet is player-only), never for pets, so this will read 0
			// for every pet until/unless something starts populating it.
			if (m_pet_spawn_id == spawn_id) {
				eqt::state::PetStatsChangedData pdata;
				pdata.spawnId = spawn_id;
				pdata.hpPercent = hp_percent;
				pdata.manaPercent = (it->second.max_mana > 0)
					? static_cast<uint8_t>((it->second.cur_mana * 100) / it->second.max_mana)
					: 0;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PetStatsChanged, std::move(pdata)));
			}
		}
	}
}

void EverQuest::ZoneProcessHPUpdate(const EQ::Net::Packet &p)
{
	// HPUpdate is sent for an entity's HP
	// SpawnHPUpdate_Struct format: cur_hp (4 bytes), max_hp (4 bytes), spawn_id (2 bytes)
	// Total: 10 bytes + 2 byte opcode = 12 bytes
	// Note: Mana is sent separately via HC_OP_ManaChange

	if (p.Length() < 12) {
		LOG_WARN(MOD_ENTITY, "HPUpdate packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t cur_hp = p.GetUInt32(2);
	int32_t max_hp = p.GetInt32(6);
	uint16_t spawn_id = p.GetUInt16(10);

	// Check if this is our own HP update
	bool is_self = (spawn_id == m_my_spawn_id);

	if (is_self) {
		// Update player HP tracking (both local and GameState)
		m_cur_hp = cur_hp;
		m_max_hp = static_cast<uint32_t>(max_hp > 0 ? max_hp : 0);
		m_game_state.player().setHP(m_cur_hp, m_max_hp);
	}

	if (is_self) {
		LOG_DEBUG(MOD_ENTITY, "Player HP: {}/{} (spawn_id={})", cur_hp, max_hp, spawn_id);
	} else {
		LOG_TRACE(MOD_ENTITY, "Entity {} HP: {}/{}", spawn_id, cur_hp, max_hp);
	}

	// Update entity HP if we're tracking it
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		it->second.hp_percent = (max_hp > 0) ? static_cast<uint8_t>(cur_hp * 100 / max_hp) : 100;
	}

	// Update combat stats for our player
	if (is_self && m_combat_manager) {
		CombatStats stats;
		stats.current_hp = cur_hp;
		stats.max_hp = max_hp > 0 ? static_cast<uint32_t>(max_hp) : 0;
		// Don't update mana here - it comes from ManaChange packet
		stats.current_mana = m_mana;
		stats.max_mana = m_max_mana;
		stats.current_endurance = 0;
		stats.max_endurance = 0;
		stats.hp_percent = (max_hp > 0) ? (static_cast<float>(cur_hp) * 100.0f / static_cast<float>(max_hp)) : 100.0f;
		stats.mana_percent = (m_max_mana > 0) ? (static_cast<float>(m_mana) * 100.0f / static_cast<float>(m_max_mana)) : 100.0f;
		stats.endurance_percent = 100.0f;
		m_combat_manager->UpdateCombatStats(stats);
	}

	// T43: was #ifdef EQT_HAS_GRAPHICS. UpdateInventoryStats() (eq_bridge.cpp)
	// is unguarded/declassified since P28 -- pure inventory-manager
	// equipment-stat calculation + bridge event push, no renderer dependency.
	// Left gated here, a headless build's equipment stats (weight, weapon
	// skill types) never refreshed when player HP changed.
	if (is_self) {
		UpdateInventoryStats();
	}

	// Publish PlayerStatsChanged when our own HP changes
	if (is_self && m_bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = m_cur_hp;
		data.maxHP = m_max_hp;
		data.curMana = m_mana;
		data.maxMana = m_max_mana;
		data.curEndurance = m_endurance;
		data.maxEndurance = m_max_endurance;
		data.level = m_level;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}
}

Entity* EverQuest::FindEntityByName(const std::string& name)
{
	// Find entity by name (case-insensitive prefix match)
	// Replace spaces with underscores in search term
	std::string name_lower = name;
	std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
	std::replace(name_lower.begin(), name_lower.end(), ' ', '_');
	
	for (auto& pair : m_entities) {
		Entity& entity = pair.second;
		std::string entity_name_lower = entity.name;
		std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
		
		if (entity_name_lower.find(name_lower) == 0) {  // Starts with
			return &entity;
		}
	}
	
	return nullptr;
}

void EverQuest::ListEntities(const std::string& search) const
{
	if (!IsFullyZonedIn()) {
		std::cout << "Not in zone yet" << std::endl;
		return;
	}
	
	if (m_entities.empty()) {
		std::cout << "No entities in zone" << std::endl;
		return;
	}
	
	// Prepare search string
	std::string search_lower = search;
	if (!search.empty()) {
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
		std::replace(search_lower.begin(), search_lower.end(), ' ', '_');
	}
	
	if (search.empty()) {
		std::cout << fmt::format("Entities in zone ({} total):", m_entities.size()) << std::endl;
	} else {
		std::cout << fmt::format("Entities matching '{}' in zone:", search) << std::endl;
	}
	
	// Sort entities by distance from player
	std::vector<std::pair<float, const Entity*>> sorted_entities;
	for (const auto& pair : m_entities) {
		const Entity& entity = pair.second;
		
		// Filter by search term if provided
		if (!search.empty()) {
			std::string entity_name_lower = entity.name;
			std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
			if (entity_name_lower.find(search_lower) == std::string::npos) {
				continue;  // Skip entities that don't match search
			}
		}
		
		float dist = CalculateDistance(m_x, m_y, m_z, entity.x, entity.y, entity.z);
		sorted_entities.push_back({dist, &entity});
	}
	
	std::sort(sorted_entities.begin(), sorted_entities.end(), 
		[](const auto& a, const auto& b) { return a.first < b.first; });
	
	// Check if any entities matched
	if (sorted_entities.empty() && !search.empty()) {
		std::cout << fmt::format("  No entities found matching '{}'", search) << std::endl;
		return;
	}
	
	// Display up to 20 nearest entities
	int count = 0;
	for (const auto& [dist, entity] : sorted_entities) {
		if (count++ >= 20) {
			std::cout << "  ... and more" << std::endl;
			break;
		}
		
		std::cout << fmt::format("  {} (ID: {}) - Level {} {} - {:.1f} units away at ({:.0f}, {:.0f}, {:.0f})",
			entity->name, entity->spawn_id, entity->level,
			entity->class_id == 0 ? "NPC" : fmt::format("Class {}", entity->class_id),
			dist, entity->x, entity->y, entity->z) << std::endl;
			
		if (entity->hp_percent < 100) {
			std::cout << fmt::format("    HP: {}%", entity->hp_percent) << std::endl;
		}
	}
}

void EverQuest::ZoneProcessWearChange(const EQ::Net::Packet &p)
{
	// WearChange indicates equipment changes on entities
	// Titanium Format: spawn_id (2 bytes), material (2 bytes), color (4 bytes), wear_slot_id (1 byte)
	// Total: 9 bytes + 2 byte opcode = 11 bytes

	if (p.Length() != 11) {
		LOG_WARN(MOD_ENTITY, "WearChange packet wrong size: {} bytes (expected 11)", p.Length());
		return;
	}

	// Skip opcode (2 bytes), then parse struct
	uint16_t spawn_id = p.GetUInt16(2);      // offset 0 in struct, 2 in packet
	uint16_t material = p.GetUInt16(4);      // offset 2 in struct, 4 in packet
	uint32_t color = p.GetUInt32(6);         // offset 4 in struct, 6 in packet
	uint8_t wear_slot = p.GetUInt8(10);      // offset 8 in struct, 10 in packet

	LOG_DEBUG(MOD_ENTITY, "Equipment change for spawn_id={}: slot {} material {} color {:08X}",
		spawn_id, wear_slot, material, color);

	// Update the entity's equipment data
	auto it = m_entities.find(spawn_id);
	if (it != m_entities.end()) {
		Entity& entity = it->second;

		// Update equipment slot if valid (slots 0-8 map to equipment array)
		if (wear_slot < 9) {
			entity.equipment[wear_slot] = material;
			entity.equipment_tint[wear_slot] = color;
		}

		// Build EntityAppearanceChanged; written to the source GameState
		// unconditionally (T221), published to the bridge when present
		// (D17b: bridge is sole path to renderer).
		eqt::state::EntityAppearanceChangedData data;
		data.spawnId = spawn_id;
		data.raceId = entity.race_id;
		data.gender = entity.gender;
		data.isPlayer = (spawn_id == m_my_spawn_id);
		data.face = entity.face;
		data.haircolor = entity.haircolor;
		data.hairstyle = entity.hairstyle;
		data.beardcolor = entity.beardcolor;
		data.beard = entity.beard;
		data.texture = entity.equip_chest2;
		data.helm = entity.helm;
		for (int j = 0; j < 9; j++) {
			data.equipment[j] = entity.equipment[j];
			data.equipmentTint[j] = entity.equipment_tint[j];
		}
		ApplyAppearanceToGameState(data);
		if (m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
		}
	}
}

// T221: the source-side twin of GameStateSyncBridge's EntityAppearanceChanged
// projection (T81) -- the same partial merge (NOT size; that field is not on
// the event). Applied to the built event payload rather than to m_entities,
// because UpdatePlayerAppearanceFromInventory() derives equipment from
// INVENTORY items and never writes them back to m_entities -- merging the
// payload is the only way the source matches what the projection held.
// No-op for an unknown spawn, as the projection was.
void EverQuest::ApplyAppearanceToGameState(const eqt::state::EntityAppearanceChangedData& d)
{
	auto* existing = m_game_state.entities().getEntityMutable(d.spawnId);
	if (!existing) {
		return;
	}
	existing->raceId = d.raceId;
	existing->gender = d.gender;
	existing->face = d.face;
	existing->haircolor = d.haircolor;
	existing->hairstyle = d.hairstyle;
	existing->beardcolor = d.beardcolor;
	existing->beard = d.beard;
	existing->equipChest2 = d.texture;
	existing->helm = d.helm;
	for (int i = 0; i < 9; ++i) {
		existing->equipment[i] = d.equipment[i];
		existing->equipmentTint[i] = d.equipmentTint[i];
	}
}

void EverQuest::UpdatePlayerAppearanceFromInventory()
{
	// T221: was `!m_bridge || !m_inventory_manager` -- the source write below
	// must not depend on a bridge being attached; only the publish does.
	if (!m_inventory_manager) {
		return;
	}

	// Find the player entity
	auto it = m_entities.find(m_my_spawn_id);
	if (it == m_entities.end()) {
		LOG_WARN(MOD_ENTITY, "UpdatePlayerAppearanceFromInventory: player entity {} not found", m_my_spawn_id);
		return;
	}

	Entity& entity = it->second;

	// Build appearance data from inventory items (D17b: bridge is sole path to renderer)
	eqt::state::EntityAppearanceChangedData data;
	data.spawnId = m_my_spawn_id;
	data.raceId = entity.race_id;
	data.gender = entity.gender;
	data.isPlayer = true;
	data.face = entity.face;
	data.haircolor = entity.haircolor;
	data.hairstyle = entity.hairstyle;
	data.beardcolor = entity.beardcolor;
	data.beard = entity.beard;
	data.texture = entity.equip_chest2;
	data.helm = entity.helm;

	// Initialize equipment arrays from entity (fallback)
	for (int i = 0; i < 9; i++) {
		data.equipment[i] = entity.equipment[i];
		data.equipmentTint[i] = entity.equipment_tint[i];
	}

	// Mapping from inventory slots to equipment appearance indices
	// Note: EQ equipment indices are: 0=Head, 1=Chest, 2=Arms, 3=Wrist, 4=Hands, 5=Legs, 6=Feet, 7=Primary, 8=Secondary
	struct SlotMapping {
		int16_t inventorySlot;
		int equipSlotIndex;
	};
	const SlotMapping mappings[] = {
		{ eqt::inventory::SLOT_HEAD, 0 },      // Head
		{ eqt::inventory::SLOT_CHEST, 1 },     // Chest
		{ eqt::inventory::SLOT_ARMS, 2 },      // Arms
		{ eqt::inventory::SLOT_WRIST1, 3 },    // Wrist (use first wrist slot)
		{ eqt::inventory::SLOT_HANDS, 4 },     // Hands
		{ eqt::inventory::SLOT_LEGS, 5 },      // Legs
		{ eqt::inventory::SLOT_FEET, 6 },      // Feet
		{ eqt::inventory::SLOT_PRIMARY, 7 },   // Primary
		{ eqt::inventory::SLOT_SECONDARY, 8 }, // Secondary
	};

	LOG_DEBUG(MOD_INVENTORY, "UpdatePlayerAppearanceFromInventory: Building appearance from inventory");
	for (const auto& mapping : mappings) {
		const auto* item = m_inventory_manager->getItem(mapping.inventorySlot);
		if (item) {
			// Body armor: use material for texture variant and color for tint
			if (mapping.equipSlotIndex < 7) {
				data.equipment[mapping.equipSlotIndex] = item->material;
				data.equipmentTint[mapping.equipSlotIndex] = item->color;
			} else {
				// Weapons: use item ID for model lookup
				data.equipment[mapping.equipSlotIndex] = item->itemId;
			}
		} else {
			// No item in slot - clear the appearance slot
			data.equipment[mapping.equipSlotIndex] = 0;
			data.equipmentTint[mapping.equipSlotIndex] = 0;
		}
	}

	ApplyAppearanceToGameState(data);  // T221
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
	}

	LOG_DEBUG(MOD_INVENTORY, "UpdatePlayerAppearanceFromInventory: Updated player appearance");
}

void EverQuest::ZoneProcessIllusion(const EQ::Net::Packet &p)
{
	// Illusion packet indicates race/appearance changes from illusion spells
	// Packet format: opcode(2) + Illusion_Struct(168)

	if (p.Length() < 2 + sizeof(EQT::Illusion_Struct)) {
		LOG_WARN(MOD_ENTITY, "Illusion packet too small: {} bytes (expected {})",
		         p.Length(), 2 + sizeof(EQT::Illusion_Struct));
		return;
	}

	const EQT::Illusion_Struct* illusion = reinterpret_cast<const EQT::Illusion_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	uint32_t spawn_id = illusion->spawnid;
	int32_t new_race = illusion->race;
	uint8_t new_gender = illusion->gender;
	uint8_t new_texture = illusion->texture;
	uint8_t new_helm = illusion->helmtexture;
	float new_size = illusion->size;

	auto it = m_entities.find(static_cast<uint16_t>(spawn_id));
	if (it == m_entities.end()) {
		LOG_DEBUG(MOD_ENTITY, "Illusion for unknown entity {}", spawn_id);
		return;
	}

	Entity& entity = it->second;
	std::string entity_name = entity.name;

	LOG_INFO(MOD_ENTITY, "{} (ID: {}) illusioned to race {} gender {} texture {} helm {} size {:.2f}",
	         entity_name, spawn_id, new_race, new_gender, new_texture, new_helm, new_size);

	// Update entity data
	entity.race_id = static_cast<uint16_t>(new_race);
	entity.gender = new_gender;
	entity.equip_chest2 = new_texture;
	entity.helm = new_helm;
	entity.size = new_size;

	// Copy additional appearance fields if present
	entity.face = illusion->face;
	entity.hairstyle = illusion->hairstyle;
	entity.haircolor = illusion->haircolor;
	entity.beard = illusion->beard;
	entity.beardcolor = illusion->beardcolor;

	// Build EntityAppearanceChanged; source write unconditional (T221),
	// bridge publish when present (D17b: bridge is sole path to renderer).
	eqt::state::EntityAppearanceChangedData data;
	data.spawnId = static_cast<uint16_t>(spawn_id);
	data.raceId = entity.race_id;
	data.gender = entity.gender;
	data.isPlayer = (spawn_id == m_my_spawn_id);
	data.face = entity.face;
	data.haircolor = entity.haircolor;
	data.hairstyle = entity.hairstyle;
	data.beardcolor = entity.beardcolor;
	data.beard = entity.beard;
	data.texture = entity.equip_chest2;
	data.helm = entity.helm;
	for (int j = 0; j < 9; j++) {
		data.equipment[j] = entity.equipment[j];
		data.equipmentTint[j] = entity.equipment_tint[j];
	}
	ApplyAppearanceToGameState(data);
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::EntityAppearanceChanged, std::move(data)));
	}
}

// Helper function to determine door type from door name
#ifdef WITH_AUDIO
static EQT::Audio::DoorType getDoorTypeFromName(const std::string& name) {
	// Convert to lowercase for comparison
	std::string lowerName = name;
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

	// Check for secret doors
	if (lowerName.find("secret") != std::string::npos ||
	    lowerName.find("hidden") != std::string::npos) {
		return EQT::Audio::DoorType::Secret;
	}

	// Check for metal doors/gates
	if (lowerName.find("gate") != std::string::npos ||
	    lowerName.find("iron") != std::string::npos ||
	    lowerName.find("metal") != std::string::npos ||
	    lowerName.find("grate") != std::string::npos ||
	    lowerName.find("portcullis") != std::string::npos ||
	    lowerName.find("bars") != std::string::npos) {
		return EQT::Audio::DoorType::Metal;
	}

	// Check for stone doors
	if (lowerName.find("stone") != std::string::npos ||
	    lowerName.find("rock") != std::string::npos ||
	    lowerName.find("boulder") != std::string::npos ||
	    lowerName.find("crypt") != std::string::npos ||
	    lowerName.find("tomb") != std::string::npos) {
		return EQT::Audio::DoorType::Stone;
	}

	// Check for sliding doors
	if (lowerName.find("slide") != std::string::npos ||
	    lowerName.find("panel") != std::string::npos) {
		return EQT::Audio::DoorType::Sliding;
	}

	// Default to wood for most doors
	return EQT::Audio::DoorType::Wood;
}
#endif

void EverQuest::ZoneProcessMoveDoor(const EQ::Net::Packet &p)
{
	// MoveDoor indicates door animations (opening/closing)
	// Packet format: opcode(2) + MoveDoor_Struct(2)
	// action: 0x02 = close (go to closed visual state)
	//         0x03 = open (go to open visual state)

	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "MoveDoor packet too small: {} bytes", p.Length());
		return;
	}

	uint8_t door_id = p.GetUInt8(2);
	uint8_t action = p.GetUInt8(3);

	// Update door state in game state manager
	const EQT::DoorState* door = m_game_state.doors().getDoor(door_id);
	if (door) {
		bool is_open = (action == 0x03);
		m_game_state.doors().setDoorState(door_id, is_open);

		// Check if this was a user-initiated door click
		bool user_initiated = (m_pending_door_clicks.erase(door_id) > 0);

		// Log at debug level 2+ for all doors, or level 1+ for user-initiated
		if (s_debug_level >= 2 || (user_initiated && s_debug_level >= 1)) {
			LOG_DEBUG(MOD_ENTITY, "Door {} {}{}", door_id,
			          is_open ? "opened" : "closed",
			          user_initiated ? " (user)" : "");
		}

#ifdef WITH_AUDIO
		// Play door sound only if player is close to the door
		if (m_audio_manager) {
			glm::vec3 doorPos(door->x, door->y, door->z);
			glm::vec3 playerPos(m_x, m_y, m_z);
			float distance = glm::distance(doorPos, playerPos);

			// Only play door sounds within 10 units
			constexpr float DOOR_SOUND_MAX_DISTANCE = 10.0f;
			if (distance <= DOOR_SOUND_MAX_DISTANCE) {
				EQT::Audio::DoorType doorType = getDoorTypeFromName(door->name);
				uint32_t soundId = EQT::Audio::DoorSounds::getDoorSound(doorType, is_open);
				m_audio_manager->playSound(soundId, doorPos);
			}
		}
#endif

		// Publish DoorStateChanged event to bridge
		if (m_bridge) {
			eqt::state::DoorStateChangedData data;
			data.doorId = door_id;
			data.isOpen = is_open;
			data.userInitiated = user_initiated;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::DoorStateChanged, std::move(data)));
		}
	} else {
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_ENTITY, "MoveDoor for unknown door {}", door_id);
		}
	}
}

void EverQuest::SendSpawnAppearance(uint16_t type, uint32_t value)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(10);  // SpawnAppearance packet size: 2 bytes opcode + 8 bytes struct
	
	p.PutUInt16(0, HC_OP_SpawnAppearance);
	p.PutUInt16(2, m_my_spawn_id);  // spawn_id
	p.PutUInt16(4, type);            // type
	p.PutUInt32(6, value);           // parameter
	// Removed the extra 4 bytes that were at offset 10
	
	m_zone_connection->QueuePacket(p);
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent SpawnAppearance: spawn_id={}, type={}, value={}", 
			m_my_spawn_id, type, value);
	}
}

void EverQuest::SendAnimation(uint8_t animation_id, uint8_t animation_speed)
{
	if (!IsFullyZonedIn()) {
		return;
	}
	
	EQ::Net::DynamicPacket p;
	p.Resize(6);  // Animation packet size: 2 bytes opcode + 4 bytes data
	
	p.PutUInt16(0, HC_OP_Emote);          // opcode (OP_Animation)
	p.PutUInt16(2, m_my_spawn_id);        // spawn_id
	p.PutUInt8(4, animation_speed);       // animation speed (usually 10)
	p.PutUInt8(5, animation_id);          // animation ID
	
	m_zone_connection->QueuePacket(p, 0, false);  // Send unreliable
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sent Animation: spawn_id={}, speed={}, animation_id={}",
			m_my_spawn_id, animation_speed, animation_id);
	}
}

void EverQuest::SendClickDoor(uint8_t door_id, uint32_t item_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click door - not fully zoned in");
		return;
	}

	// Check if door exists
	const EQT::DoorState* door = m_game_state.doors().getDoor(door_id);
	if (!door) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown door {}", door_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking door {} ('{}')", door_id, door->name);

	// Track this as a user-initiated door click
	m_pending_door_clicks.insert(door_id);

	// Build ClickDoor packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickDoor_Struct));

	p.PutUInt16(0, HC_OP_ClickDoor);
	p.PutUInt8(2, door_id);           // doorid
	p.PutUInt8(3, 0);                 // unknown001
	p.PutUInt8(4, 0);                 // unknown002
	p.PutUInt8(5, 0);                 // unknown003
	p.PutUInt8(6, 0);                 // picklockskill
	p.PutUInt8(7, 0);                 // unknown005[0]
	p.PutUInt8(8, 0);                 // unknown005[1]
	p.PutUInt8(9, 0);                 // unknown005[2]
	p.PutUInt32(10, item_id);         // item_id (key)
	p.PutUInt16(14, m_my_spawn_id);   // player_id
	p.PutUInt16(16, 0);               // unknown014

	m_zone_connection->QueuePacket(p, 0, true);
}

// ============================================================================
// World Object / Tradeskill Methods
// ============================================================================

void EverQuest::SendClickObject(uint32_t drop_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot click object - not fully zoned in");
		return;
	}

	// Check if object exists
	auto it = m_world_objects.find(drop_id);
	if (it == m_world_objects.end()) {
		LOG_WARN(MOD_ENTITY, "Attempted to click unknown object {}", drop_id);
		return;
	}

	LOG_INFO(MOD_ENTITY, "Clicking object {} ('{}') type={} ({})",
		drop_id, it->second.name, it->second.object_type,
		it->second.getTradeskillName());

	// Build ClickObject packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObject_Struct));

	p.PutUInt16(0, HC_OP_ClickObject);
	p.PutUInt32(2, drop_id);           // drop_id
	p.PutUInt32(6, m_my_spawn_id);     // player_id

	m_zone_connection->QueuePacket(p, 0, true);
}

void EverQuest::SendCloseContainer(uint32_t drop_id)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot close container - not fully zoned in");
		return;
	}

	LOG_DEBUG(MOD_ENTITY, "Closing tradeskill container {}", drop_id);

	// Build ClickObjectAction packet to close the container
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ClickObjectAction_Struct));

	p.PutUInt16(0, HC_OP_ClickObjectAction);
	p.PutUInt32(2, m_my_spawn_id);     // player_id
	p.PutUInt32(6, drop_id);           // drop_id
	p.PutUInt32(10, 0);                // open = 0 (closing)
	p.PutUInt32(14, 0);                // type
	p.PutUInt32(18, 0);                // unknown16
	p.PutUInt32(22, 0);                // icon
	p.PutUInt32(26, 0);                // unknown24
	// object_name is 64 bytes of zeros (not needed for close)
	for (int i = 0; i < 64; i++) {
		p.PutUInt8(30 + i, 0);
	}

	m_zone_connection->QueuePacket(p, 0, true);

	// Clear active tradeskill object
	m_active_tradeskill_object_id = 0;
}

const eqt::WorldObject* EverQuest::GetWorldObject(uint32_t drop_id) const
{
	auto it = m_world_objects.find(drop_id);
	if (it != m_world_objects.end()) {
		return &it->second;
	}
	return nullptr;
}

void EverQuest::ClearWorldObjects()
{
	LOG_DEBUG(MOD_ENTITY, "Clearing {} world objects", m_world_objects.size());
	m_world_objects.clear();
	m_active_tradeskill_object_id = 0;
}

void EverQuest::ZoneProcessClickObjectAction(const EQ::Net::Packet &p)
{
	// Parse ClickObjectAction_Struct from packet (skip 2-byte opcode)
	if (p.Length() < 2 + sizeof(EQT::ClickObjectAction_Struct)) {
		LOG_WARN(MOD_ENTITY, "ClickObjectAction packet too small: {} bytes (need {})",
			p.Length(), 2 + sizeof(EQT::ClickObjectAction_Struct));
		return;
	}

	const auto* action = reinterpret_cast<const EQT::ClickObjectAction_Struct*>(p.Data() + 2);

	std::string objectName(action->object_name, strnlen(action->object_name, sizeof(action->object_name)));

	if (action->open == 1) {
		// Server is telling us to open a tradeskill container
		LOG_INFO(MOD_ENTITY, "Opening tradeskill container: drop_id={} type={} icon={} name='{}'",
			action->drop_id, action->type, action->icon, objectName);

		// Store the active tradeskill object ID
		m_active_tradeskill_object_id = action->drop_id;

		// Open tradeskill container via bridge
		if (m_bridge) {
			eqt::state::TradeskillContainerOpenedEvent data;
			data.isWorldObject = true;
			data.objectId = action->drop_id;
			data.inventorySlot = -1;
			data.containerName = objectName;
			data.containerType = static_cast<uint8_t>(action->type);
			data.slotCount = eqt::inventory::WORLD_COUNT;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeskillContainerOpened, std::move(data)));
		}
	} else {
		// Server is confirming container is closed
		LOG_DEBUG(MOD_ENTITY, "Tradeskill container closed: drop_id={}", action->drop_id);
		m_active_tradeskill_object_id = 0;

		// Close tradeskill container via bridge
		if (m_bridge) {
			eqt::state::TradeskillContainerClosedEvent data;
			data.wasWorldObject = true;
			data.objectId = action->drop_id;
			data.inventorySlot = -1;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeskillContainerClosed, std::move(data)));
		}
	}
}

// T179: send an OP_RaidCommand (Titanium wire opcode OP_RaidInvite, 0x5891,
// routed by eqemu to Handle_OP_RaidCommand). The body is the 140-byte internal
// RaidGeneral_Struct built by EQT::encodeRaidCommand -- the handler rejects
// anything shorter. Called only from the action layer (/raid commands).
void EverQuest::SendRaidCommand(uint32_t action, const std::string& player_name,
                               const std::string& leader_name, uint32_t parameter)
{
	if (!m_zone_connection || !m_zone_connected) {
		LOG_WARN(MOD_ZONE, "SendRaidCommand: not connected");
		return;
	}

	const std::vector<uint8_t> body =
		EQT::encodeRaidCommand(action, player_name, leader_name, parameter);

	EQ::Net::DynamicPacket p;
	p.PutUInt16(0, HC_OP_RaidInvite);
	p.PutData(2, const_cast<uint8_t*>(body.data()), body.size());
	m_zone_connection->QueuePacket(p, 0, true);

	LOG_INFO(MOD_ZONE, "Sent OP_RaidCommand action={} player='{}' leader='{}'",
	         action, player_name, leader_name);
}
