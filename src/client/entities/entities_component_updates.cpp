// EverQuest entity-update cluster: the exp-progress helpers, ground spawns,
// weather, new spawns, exp updates, and client position updates.
//
// Clusters of eq_entities.cpp (Batch F unit F11), split off by Batch J unit
// J08. Moved verbatim, no behavior change. See
// refactor/J08_eq_entities_split.md and refactor/F11_entities.md.
//
// NOTE: ZoneProcessGuildMOTD (between ZoneProcessRaidUpdate and
// ZoneProcessClientUpdate in eq.cpp) is guild domain (see F04) and stays
// in eq.cpp.
//
// K06 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_entities_updates.cpp to here, and the bodies rebased from
// EverQuest onto eqt::entities::EntitiesComponent. Still verbatim -- only the
// substitutions §Mechanism 4 allows (m_bridge/m_entities/m_game_state/
// m_trade_manager/m_character/m_combat_manager -> ctx_.*; m_exp/m_exp_aa/
// m_player_graphics_entity_pending/m_world_objects/m_weather_received/
// m_update_running/m_req_new_zone_sent -> owner_.m_* (zone-family members,
// K09 owns the eventual move); SetMySpawnId()/SetPosLocal()/
// SyncEntityToGameState()/OnSpawnAddedGraphics()/OnPetCreated()/
// OnPetButtonStateChanged()/SetHeadingLocal()/SetLoadingPhase()/
// StartUpdateLoop()/OnGameStateComplete()/OnSpawnMovedGraphics()/
// MySpawnId() -> owner_.*, they stay on EverQuest; s_debug_level ->
// EverQuest::s_debug_level. IsTrackedTarget() is a free function
// (common/logging.h), not an EverQuest member -- left bare. See the K06
// Review.
//
// T341 (board B038): the "stay on EverQuest" clauses above are HISTORY, not
// the current shape. This family now reaches EverQuest not at all -- the
// `owner_` handle and the `friend class` line are both gone; see the T341
// note in this family's component header.

#include "client/eq.h"
#include "client/graphics/actor_scale.h"  // B1172: the client's resolved size
#include "client/corpse_predicate.h"  // T486: IsCorpseNpcType
#include "client/entities/entities_component.h"
#include "client/world_object.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/raid_update.h"
#include "client/spell/buff_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/position_frames.h"
#include "client/bridge/irrlicht_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "client/core/memory_ledger.h"  // B919: the gs.world_objects owner row

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

void eqt::entities::EntitiesComponent::ZoneProcessGroundSpawn(const EQ::Net::Packet &p)
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
	// B919 (#238): named `game_state` owner row (WILLEQ_LEDGER_OWNERS=game_state):
	// the ground-spawn world object in m_world_objects and its bridge event are
	// gs.world_objects.
	eqt::memory::StartupScope owner("gs.world_objects");
	if (p.Length() < 2 ||
	    !eqt::ParseGroundSpawn(static_cast<const uint8_t*>(p.Data()) + 2, p.Length() - 2, worldObj)) {
		LOG_WARN(MOD_ENTITY, "GroundSpawn packet too small: {} bytes (need at least {})",
			p.Length(), 2 + eqt::GroundSpawnMinBodyBytes());
		return;
	}

	// Store the world object
	m_world_objects[worldObj.drop_id] = worldObj;

	// Publish WorldObjectSpawned event to bridge
	if (ctx_.bridge) {
		eqt::state::WorldObjectSpawnedData wdata;
		wdata.dropId = worldObj.drop_id;
		wdata.x = worldObj.x;
		wdata.y = worldObj.y;
		wdata.z = worldObj.z;
		wdata.heading = worldObj.heading;
		wdata.modelName = worldObj.name;
		wdata.objectType = static_cast<uint8_t>(worldObj.object_type);
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
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

void eqt::entities::EntitiesComponent::ZoneProcessWeather(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Weather update received");

	ctx_.Zone().WeatherReceived() = true;

	// Parse weather packet (8 bytes)
	// type: 0=rain off, 1=snow off, 2=snow on (type 0 + intensity > 0 = rain on)
	// intensity: 1-10
	if (p.Length() >= 10) {  // 2 byte opcode + 8 byte struct
		uint8_t type = p.GetUInt8(2);
		uint8_t intensity = p.GetUInt8(6);

		LOG_INFO(MOD_ZONE, "Weather packet: type={}, intensity={}", type, intensity);

		// Publish WeatherChanged event to bridge
		if (ctx_.bridge) {
			eqt::state::WeatherChangedData data;
			data.type = type;
			data.intensity = intensity;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::WeatherChanged, std::move(data)));
		}
	}

	// Weather is the last packet in Zone Entry phase
	// Now we can send ReqNewZone
	if (!ctx_.Zone().ReqNewZoneSent()) {
		ctx_.Zone().ZoneSendReqNewZone();
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessNewSpawn(const EQ::Net::Packet &p)
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
	// T486 (2026-09-14 audit, item 2): the flag has ONE owner -- this read.
	// eqemu writes NPC=2 for every corpse it spawns, and a corpse already
	// lying here when we zoned in gets no OP_Death / OP_MobRename, so without
	// this the bare is_corpse readers (a corpse-target spell's validateCast,
	// the CorpseDecayStarted fade, the name suffix on an appearance change)
	// saw a live mob. Same rule as IsCorpseByFields(); a parser that skips
	// this fails tests/test_corpse_death_pose.cpp.
	entity.is_corpse = IsCorpseNpcType(entity.npc_type);
	bool isNPC = (entity.npc_type == 1 || entity.npc_type == 3);

	// Extract heading (11 bits to match C->S format)
	// Server uses 512 EQ units = 360 degrees, so multiply by 360/2048
	uint32_t raw_heading = (field4 >> 13) & 0x7FF;
	float server_heading = static_cast<float>(raw_heading) * 360.0f / 2048.0f;
	// Use heading directly for all entities (NPCs and players)
	entity.heading = server_heading;
	if (entity.heading >= 360.0f) entity.heading -= 360.0f;

	// Debug logging for current player at level 1+
	if (EverQuest::GetDebugLevel() >= 1 && entity.name == ctx_.character) {
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
		if (entity.name == ctx_.character) {
			ctx_.self.SetMySpawnId(entity.spawn_id);  // T288: one store
			if (ctx_.trade_manager) {
				ctx_.trade_manager->setMySpawnId(ctx_.self.MySpawnId());
			}
			if (EverQuest::GetDebugLevel() >= 1) {
				LOG_DEBUG(MOD_MAIN, "Found our own spawn in NewSpawn! Name: {}, Spawn ID: {}, server pos=({:.2f}, {:.2f}, {:.2f}) size={:.2f}",
					entity.name, ctx_.self.MySpawnId(), entity.x, entity.y, entity.z, entity.size);
			}
			// B1172 (#491) fix leg: our size is the client's ONE resolved number
			// (a size word not > 0 -> the race/gender default, written back at eqgame
			// 0x47fb30): the entity record and PlayerState carry the same value.
			entity.size = EQT::Graphics::clientResolvedActorSize(entity.race_id, entity.gender, entity.size);
			// Update our position to match server spawn position
			ctx_.game_state.player().setSize(entity.size);
			// T311: same wire-frame -> render-frame conversion as the
			// ZoneSpawns path above -- see the note there.
			ctx_.self.SetPosLocal(entity.x, entity.y,
			            eqt::wireZToRenderZ(entity.z,
			                                ctx_.game_state.player().collisionZOffset(),
			                                entity.size));  // T288: one store
			ctx_.game_state.player().setZAwaitingFrameConversion(
				ctx_.game_state.player().collisionZOffset() <= 0.0f);

			if (EverQuest::GetDebugLevel() >= 1) {
				LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from NewSpawn: ({:.2f},{:.2f},{:.2f}) (render/node-origin Z, wire Z was {:.2f})",
					ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity.z);
			}

			// Update bridge with the render-frame (node-origin) Z
			if (ctx_.bridge) {
				float heading512 = ctx_.self.HeadingDeg() * 512.0f / 360.0f;
				eqt::state::PlayerMovedData pdata;
				pdata.x = ctx_.self.PosX(); pdata.y = ctx_.self.PosY(); pdata.z = ctx_.self.PosZ();
				pdata.heading = heading512;
				pdata.dx = 0; pdata.dy = 0; pdata.dz = 0;
				pdata.isMoving = false;
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
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
		ctx_.entities[entity.spawn_id] = entity;
		ctx_.Player().SyncEntityToGameState(entity);

#ifdef EQT_HAS_GRAPHICS
		ctx_.Bridge().OnSpawnAddedGraphics(entity);
		// Notify bridge of player spawn ID (D17b: bridge is sole path to renderer)
		if (entity.name == ctx_.character && ctx_.bridge) {
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::PlayerSpawnIdSet,
				eqt::state::PlayerSpawnIdSetData{ctx_.self.MySpawnId()}));
		}
#endif

		// Check if this spawn is our pet (if we already know our spawn ID)
		if (entity.is_pet != 0 && entity.pet_owner_id == ctx_.self.MySpawnId() && ctx_.self.MySpawnId() != 0) {
			// T286: PetState is the only store; default button states
			// (Follow=ON, others=OFF per server behavior) are applied after
			// OnPetCreated, whose setPet() clears them.
			LOG_INFO(MOD_MAIN, "Pet detected: {} (ID: {}) Level {} owned by us (ID: {})",
				entity.name, entity.spawn_id, entity.level, ctx_.self.MySpawnId());

			// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical note at
			// the ZoneProcessSpawnAll second-pass call site above.
			ctx_.Bridge().OnPetCreated(entity);
			ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
		}

		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_ENTITY, "New spawn: {} (ID: {}) Level {} {} Race {} at ({:.2f}, {:.2f}, {:.2f})",
				entity.name, entity.spawn_id, entity.level,
				entity.class_id, entity.race_id, entity.x, entity.y, entity.z);
		}
	} else {
		LOG_WARN(MOD_ENTITY, "Invalid spawn data in NewSpawn: ID={}, Name='{}'",
			entity.spawn_id, entity.name);
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessExpUpdate(const EQ::Net::Packet &p)
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
	          exp, aaxp, ctx_.Zone().Exp(), ctx_.Zone().ExpAA());

	ctx_.Zone().Exp() = exp;
	ctx_.Zone().ExpAA() = aaxp;

	// Publish ExpProgressChanged event
	if (ctx_.bridge) {
		eqt::state::ExpProgressChangedData data;
		data.progress = CalculateExpProgress(ctx_.Zone().Exp(), ctx_.game_state.player().level());
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ExpProgressChanged, std::move(data)));
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessClientUpdate(const EQ::Net::Packet &p)
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
	if (ctx_.self.MySpawnId() != 0 && spawn_id == ctx_.self.MySpawnId()) {
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
	uint16_t currentTargetId = ctx_.combat_manager ? ctx_.combat_manager->GetTargetId() : 0;
	bool isCurrentPlayer = (spawn_id == ctx_.self.MySpawnId());
	bool shouldLogPosition = (EverQuest::GetDebugLevel() >= 2) ||
	                         (EverQuest::GetDebugLevel() >= 1 && (isCurrentPlayer || spawn_id == currentTargetId || IsTrackedTarget(spawn_id)));
	if (shouldLogPosition) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C spawn_id={} pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} anim={} delta=({:.2f}, {:.2f}, {:.2f}) (my_id={})",
			spawn_id, x, y, z, server_h, animation, dx, dy, dz, ctx_.self.MySpawnId());
	}
	// Additional detailed logging for current player at debug level 1+
	if (EverQuest::GetDebugLevel() >= 1 && isCurrentPlayer) {
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] raw_heading={} (12-bit field={}) -> server_h={:.2f}deg -> h_player={:.2f}deg",
			raw_heading, heading, server_h, h_player);
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, x, y, z);
	}

	// Check if this is an update for our own character
	if (spawn_id == ctx_.self.MySpawnId()) {
		// Update our position to match what the server says.
		// T288: one store -- before T288 this branch wrote EverQuest's private
		// copy only, so PlayerState (and therefore GetHeading(),
		// clickNearestDoor(), getNearestPlayer(), GameState::playerPosition())
		// never moved off the zone-in value.
		// T332 (B049): the server's echo of our own position is a WIRE z, the
		// same as every other inbound self-position edge -- convert it, and
		// mark the conversion owed when no mesh offset is measurable yet. It
		// used to be stored raw, so an echo arriving after the model had loaded
		// re-planted a wire value in the render-frame store.
		ctx_.self.SetPosLocal(x, y,
		            eqt::wireZToRenderZ(z, ctx_.game_state.player().collisionZOffset(),
		                                ctx_.game_state.player().size()));
		ctx_.game_state.player().setZAwaitingFrameConversion(
			ctx_.game_state.player().collisionZOffset() <= 0.0f);
		// S->C transformation for our player: inverse of C->S
		// (server_heading = 90 - client heading). LOAD-BEARING and PLAYER-ONLY:
		// NPC headings a few lines above take server_h verbatim. Do not
		// harmonise -- docs/CLAUDE.md, Heading/Rotation Conventions.
		float self_heading = 90.0f - server_h;
		if (self_heading < 0.0f) self_heading += 360.0f;
		if (self_heading >= 360.0f) self_heading -= 360.0f;
		ctx_.self.SetHeadingLocal(self_heading);

		// Track whether we need to create player entity on renderer
		bool needPlayerEntityCreation = false;

		// Also update our spawn ID if not set
		if (ctx_.self.MySpawnId() == 0) {
			ctx_.self.SetMySpawnId(spawn_id);  // T288: one store
			if (ctx_.trade_manager) {
				ctx_.trade_manager->setMySpawnId(ctx_.self.MySpawnId());
			}
			LOG_INFO(MOD_MOVEMENT, "Set our spawn ID to {} from ClientUpdate", ctx_.self.MySpawnId());

			// Deferred pet detection: now that we know our spawn ID, check if any
			// existing entity is our pet (spawns arrive before we know our ID)
			if (ctx_.game_state.pet().petSpawnId() == 0) {
				for (const auto& pair : ctx_.entities) {
					const Entity& ent = pair.second;
					if (ent.is_pet != 0 && ent.pet_owner_id == ctx_.self.MySpawnId()) {
						// T286: PetState is the only store (see above).
						LOG_INFO(MOD_MAIN, "Pet detected (deferred): {} (ID: {}) Level {} owned by us (ID: {})",
							ent.name, ent.spawn_id, ent.level, ctx_.self.MySpawnId());
						// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical
						// note at the ZoneProcessSpawnAll second-pass call site.
						ctx_.Bridge().OnPetCreated(ent);
						ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
						break;
					}
				}
			}

			needPlayerEntityCreation = true;
		}

		// Also create player entity if it's pending (after zoning, entities were cleared)
		if (ctx_.Zone().PlayerGraphicsEntityPending() && ctx_.self.MySpawnId() != 0) {
			needPlayerEntityCreation = true;
		}

		// Now create the player entity via bridge (D17b: bridge is sole path to renderer)
		// Deferred from OnZoneLoadedGraphics to ensure all player data has been received
		if (needPlayerEntityCreation && ctx_.bridge) {
			auto playerIt = ctx_.entities.find(ctx_.self.MySpawnId());
			if (playerIt != ctx_.entities.end()) {
				const Entity& entity = playerIt->second;

				LOG_INFO(MOD_ENTITY, "Creating player entity {} ({}) from ClientUpdate - equipment: primary={} secondary={}",
				         ctx_.self.MySpawnId(), entity.name, entity.equipment[7], entity.equipment[8]);

				// Publish EntitySpawned for player entity
				eqt::state::EntitySpawnedData data;
				data.spawnId = ctx_.self.MySpawnId();
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
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::EntitySpawned, std::move(data)));

				// Publish PlayerSpawnIdSet so renderer marks the player node
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::PlayerSpawnIdSet,
					eqt::state::PlayerSpawnIdSetData{ctx_.self.MySpawnId()}));

				// Set entity light source if equipped
				if (entity.light > 0) {
					eqt::state::EntityLightChangedData lightData;
					lightData.spawnId = ctx_.self.MySpawnId();
					lightData.lightLevel = entity.light;
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityLightChanged, std::move(lightData)));
				}

				// Player entity has been created, clear the pending flag
				ctx_.Zone().PlayerGraphicsEntityPending() = false;
			} else {
				LOG_WARN(MOD_ENTITY, "Player entity {} not found in ctx_.entities when setting spawn ID from ClientUpdate", ctx_.self.MySpawnId());
			}
		}

		// Start the update loop when we first receive our ClientUpdate
		// This indicates we're fully in the game world
		if (!ctx_.lifecycle.m_update_running) {
			ctx_.Bridge().SetLoadingPhase(LoadingPhase::ZONE_AWAITING_CONFIRM, "Player confirmed...");
			LOG_INFO(MOD_ZONE, "Zone connection complete! Player position confirmed (spawn_id={}).", ctx_.self.MySpawnId());
			// T286: this setZoneConnected(true) is GONE. With one store,
			// keeping it here would make "zone connected" true on a
			// ClientUpdate replayed with no socket at all -- it describes
			// "player confirmed in world", not "the zone connection is up".
			// The DaybreakConnection status callback
			// (ZoneOnStatusChangeReconnectEnabled, eq.cpp) is the sole
			// writer, and it fires strictly before this point in a live
			// session, so production behaviour is unchanged.

			// Signal renderer that network phase is complete and entities are ready (D17b: bridge-only)
			if (ctx_.bridge) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::NetworkReady,
					eqt::state::NetworkReadyData{static_cast<uint32_t>(ctx_.entities.size()), true}));
				LOG_DEBUG(MOD_GRAPHICS, "Expected entity count: {}, network ready: true", ctx_.entities.size());
			}

			ctx_.lifecycle.StartUpdateLoop();
			// Game state is now complete - trigger graphics loading if graphics mode
			ctx_.lifecycle.OnGameStateComplete();
		}
		
		// Update our entity in the entity list
		auto it = ctx_.entities.find(ctx_.self.MySpawnId());
		if (it != ctx_.entities.end()) {
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
			ctx_.game_state.entities().updateEntityPosition(
				ctx_.self.MySpawnId(), x, y, z, h_player, dx, dy, dz,
				static_cast<uint8_t>(animation));

#ifdef EQT_HAS_GRAPHICS
			ctx_.Bridge().OnSpawnMovedGraphics(ctx_.self.MySpawnId(), x, y, z, h_player, dx, dy, dz, animation);
#endif
		}
		return;
	}

	// Update entity position in our list
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end()) {
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
		ctx_.game_state.entities().updateEntityPosition(
			spawn_id, x, y, z, entity_heading, dx, dy, dz,
			static_cast<uint8_t>(animation));

#ifdef EQT_HAS_GRAPHICS
		ctx_.Bridge().OnSpawnMovedGraphics(spawn_id, x, y, z, entity_heading, dx, dy, dz, animation);
#endif

	}
}

