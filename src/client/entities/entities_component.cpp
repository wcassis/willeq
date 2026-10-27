// EverQuest zone-spawn cluster: zone spawns, time of day, spawn doors,
// zonepoints, and spawn appearance.
//
// Split out of eq.cpp by Batch F unit F11 and narrowed to these clusters by
// Batch J unit J08. Moved verbatim, no behavior change. See
// refactor/J08_eq_entities_split.md and refactor/F11_entities.md.
//
// NOTE: ZoneProcessAnimation (between ZoneProcessSpawnAppearance and
// ZoneProcessGroundSpawn in eq.cpp) is chat/emote text processing, not
// named in this unit's scope, and stays in eq.cpp.
//
// K06 (Batch K, refactor/K_component_extraction.md): moved again, from
// src/client/eq_entities.cpp to here, and the bodies rebased from EverQuest
// onto eqt::entities::EntitiesComponent. Still verbatim -- only the
// substitutions §Mechanism 4 allows (m_game_state/m_bridge/m_entities/
// m_character/m_combat_manager/m_trade_manager -> ctx_.*; m_guild_id/
// m_server_filter_sent/m_send_exp_zonein_received/m_zone_entry_sent/
// m_zone_lines/m_player_graphics_entity_pending -> owner_.m_*, they stay on
// EverQuest; SetLoadingPhase()/SetMySpawnId()/PosX()/PosY()/PosZ()/
// HeadingDeg()/SetPosLocal()/SyncEntityToGameState()/OnPetCreated()/
// OnPetButtonStateChanged()/UpdateDayNightState()/ZoneSendSetServerFilter()/
// ZoneSendClientReady()/MySpawnId() -> owner_.*, they stay on EverQuest;
// s_debug_level -> EverQuest::s_debug_level, reached through the friendship).
// m_entities itself is the map, and STAYS a core member (ctx_.entities) --
// this component owns the handlers that mutate it, not the storage. See the
// K06 Review.
//
// T341 (board B038): the "stay on EverQuest" clauses above are HISTORY, not
// the current shape. This family now reaches EverQuest not at all -- the
// `owner_` handle and the `friend class` line are both gone; see the T341
// note in this family's component header.

#include "client/eq.h"
#include "client/graphics/actor_scale.h"  // B1172: the client's resolved size
#include "client/corpse_predicate.h"  // T486: IsCorpseNpcType
#include "client/entities/entities_component.h"
#include "client/movement/movement_component.h"  // T344: ApplyPositionState
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
#include "client/core/memory_ledger.h"  // B919: the gs.entities owner row

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

void eqt::entities::EntitiesComponent::ZoneProcessZoneSpawns(const EQ::Net::Packet &p)
{
	// B919 (#238): named `game_state` owner row (WILLEQ_LEDGER_OWNERS=game_state):
	// every spawn parsed into ctx_.entities (the Entity map) and SyncEntityToGameState
	// (the state::EntityManager's own per-entity store), plus this batch's
	// bridge-event payloads, is gs.entities. The B941 rows (gs.navmesh,
	// gs.collision_map, gs.zone_lines, gs.zone_sources) name the four zone loads;
	// the per-spawn state the loads DO NOT cover gets its own row here.
	eqt::memory::StartupScope owner("gs.entities");
	ctx_.Bridge().SetLoadingPhase(LoadingPhase::ZONE_RECEIVING_SPAWNS);

	// ZoneSpawns contains multiple Spawn_Struct entries
	// Based on titanium_structs.h, Spawn_Struct is 385 bytes

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_ENTITY, "Received zone spawns packet, size: {} bytes", p.Length());
	}
	
	// Don't clear existing entities - ZoneSpawns packets can come in multiple batches
	// Only add/update spawns, don't remove existing ones
	
	// The ZoneSpawns packet appears to have a header before the spawn data
	size_t offset = 2;
	int spawn_count = 0;
	
	// T326: a commented-out packet-dump scratchpad from the original spawn-offset
	// investigation was deleted here; its one assignment is already made above.
	// Recover with: git show bcbf2ddc:src/client/eq_entities.cpp

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
		// Server uses 512 EQ units = 360 degrees, so multiply by 360/512
		uint32_t raw_heading = (field4 >> 13) & 0x7FF;
		float server_heading = (static_cast<float>(raw_heading) / 4.0f) * 360.0f / 512.0f;
		// Use heading directly for all entities (NPCs and players)
		entity.heading = server_heading;
		if (entity.heading >= 360.0f) entity.heading -= 360.0f;

		// Debug logging for current player at level 1+
		if (EverQuest::GetDebugLevel() >= 1 && entity.name == ctx_.character) {
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
			if (entity.name == ctx_.character) {
				if (ctx_.self.MySpawnId() != 0 && ctx_.self.MySpawnId() != entity.spawn_id) {
					LOG_WARN(MOD_ZONE, "Spawn ID changed in ZoneSpawns: {} -> {}", ctx_.self.MySpawnId(), entity.spawn_id);
				}
				ctx_.self.SetMySpawnId(entity.spawn_id);  // T288: one store
				if (ctx_.trade_manager) {
					ctx_.trade_manager->setMySpawnId(ctx_.self.MySpawnId());
				}

				// Debug: Zone-in position tracking at level 1
				if (EverQuest::GetDebugLevel() >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Found our spawn in ZoneSpawns: ID={} Name='{}'", ctx_.self.MySpawnId(), ctx_.character);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Spawn entity: pos=({:.2f},{:.2f},{:.2f}) heading={:.2f} size={:.2f}",
					         entity.x, entity.y, entity.z, entity.heading, entity.size);
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Previous client pos: ({:.2f},{:.2f},{:.2f}) heading={:.2f}",
					         ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.HeadingDeg());
				}

				if (EverQuest::GetDebugLevel() >= 2) {
					LOG_DEBUG(MOD_ENTITY, "=== FOUND OUR CHARACTER ===");
					LOG_DEBUG(MOD_ENTITY, "Name: '{}' Spawn ID: {}", ctx_.character, ctx_.self.MySpawnId());
					LOG_DEBUG(MOD_ENTITY, "Position: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
						entity.x, entity.y, entity.z, entity.heading);
					LOG_DEBUG(MOD_ENTITY, "Size: {:.2f} (THIS IS THE ACTUAL SIZE)", entity.size);
					LOG_DEBUG(MOD_ENTITY, "Previous z={:.2f}, updating to entity.z={:.2f}", ctx_.self.PosZ(), entity.z);
					LOG_DEBUG(MOD_ENTITY, "Previous ctx_.game_state.player().size()={:.2f}, updating to entity.size={:.2f}", ctx_.game_state.player().size(), entity.size);
					LOG_DEBUG(MOD_ENTITY, "==============================");
				}
#ifdef EQT_HAS_GRAPHICS
				// Notify bridge of player spawn ID (D17b: bridge is sole path to renderer)
				if (ctx_.bridge) {
					if (EverQuest::GetDebugLevel() >= 1) {
						LOG_INFO(MOD_ZONE, "[ZONE-IN] Publishing PlayerSpawnIdSet({}) from ZoneSpawns", ctx_.self.MySpawnId());
					}
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::PlayerSpawnIdSet,
						eqt::state::PlayerSpawnIdSetData{ctx_.self.MySpawnId()}));
				}
				// Mark player entity as pending creation - will be created when ClientUpdate arrives
				// This ensures the loading screen stays visible until the player model is fully loaded
				ctx_.Zone().PlayerGraphicsEntityPending() = true;
#endif
				// B1172 (#491) fix leg: our size is the client's ONE resolved number
				// (a size word not > 0 -> the race/gender default, written back at eqgame
				// 0x47fb30): the entity record and PlayerState carry the same value.
				entity.size = EQT::Graphics::clientResolvedActorSize(entity.race_id, entity.gender, entity.size);
				// Update our position to match server spawn position
				ctx_.game_state.player().setSize(entity.size);
				// T311 (closes T310 Leg B's note): the spawn struct's z is the
				// WIRE frame -- groundZ + 0.625*size, MEASURED off the official
				// Titanium client (T311 Leg A). GameState's player z is the
				// RENDER frame (the model node's origin, groundZ +
				// collisionZOffset), because it is fed straight to
				// IrrlichtRenderer::setPlayerPosition(). So convert.
				//
				// The old `- size/2` was neither: 3.0 for a default humanoid
				// against a true wire offset of 3.75 and a true mesh offset of
				// 3.004, and the comment that justified it (that
				// SendPositionUpdate added it back) was false. The send path now
				// converts explicitly at its own packet edge, so this is the
				// matching half and the round trip closes.
				// T288: one store; heading deliberately NOT written from spawn
				// data (see the note below), so this is a position-only write.
				//
				// The offset is normally NOT known yet here -- the self spawn
				// arrives before the model loads -- so this conversion is the
				// identity and the stored z is still the raw wire value. Say so,
				// or the send path will convert a value that was never in the
				// render frame (T311: that compounded 0.625 per login).
				ctx_.self.SetPosLocal(entity.x, entity.y,
				            eqt::wireZToRenderZ(entity.z,
				                                ctx_.game_state.player().collisionZOffset(),
				                                entity.size));
				ctx_.game_state.player().setZAwaitingFrameConversion(
					ctx_.game_state.player().collisionZOffset() <= 0.0f);
				// Don't update heading from spawn data as it may be scaled differently

				if (EverQuest::GetDebugLevel() >= 1) {
					LOG_INFO(MOD_ZONE, "[ZONE-IN] Updated client pos from spawn: ({:.2f},{:.2f},{:.2f}) (render/node-origin Z, wire Z was {:.2f}) heading unchanged={:.2f}",
					         ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), entity.z, ctx_.self.HeadingDeg());
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

			// Add to entity list (sync to both local map and EntityManager)
			ctx_.entities[entity.spawn_id] = entity;
			ctx_.Player().SyncEntityToGameState(entity);
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
	if (ctx_.self.MySpawnId() != 0 && ctx_.game_state.pet().petSpawnId() == 0) {
		for (const auto& pair : ctx_.entities) {
			const Entity& ent = pair.second;
			if (ent.is_pet != 0 && ent.pet_owner_id == ctx_.self.MySpawnId()) {
				// T286: PetState is the only store. setPet() (via
				// OnPetCreated below) clears the button states, so FOLLOW
				// is set AFTER it, not before.
				LOG_INFO(MOD_MAIN, "Pet detected in ZoneSpawns: {} (ID: {}) Level {} owned by us (ID: {})",
					ent.name, ent.spawn_id, ent.level, ctx_.self.MySpawnId());
				// T43: was #ifdef EQT_HAS_GRAPHICS. OnPetCreated() is a pure
				// bridge-event publish (eq_bridge.cpp; no renderer dependency)
				// -- gating it meant pet deltas never published in a headless
				// build (known residual named in refactor/deferred.md's
				// EQT_HAS_GRAPHICS pattern entry; T41's snapshot fix only
				// covered a pet present at zone-in, not this delta path).
				ctx_.Bridge().OnPetCreated(ent);
				ctx_.Bridge().OnPetButtonStateChanged(EQT::PET_BUTTON_FOLLOW, true);
				break;
			}
		}
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessTimeOfDay(const EQ::Net::Packet &p)
{
	if (p.Length() < 8) {  // needs GetUInt8(2..5) + GetUInt16(6)
		LOG_WARN(MOD_ZONE, "TimeOfDay packet too small: {} bytes (expected at least {})",
			p.Length(), 8);
		return;
	}

	// Phase 7.3 / T286: WorldState is the only store
	ctx_.game_state.world().setTimeOfDay(p.GetUInt8(2), p.GetUInt8(3), p.GetUInt8(4),
	                                  p.GetUInt8(5), p.GetUInt16(6));

	// Publish TimeOfDayChanged event to bridge
	if (ctx_.bridge) {
		eqt::state::TimeOfDayChangedData data;
		data.hour = ctx_.game_state.world().timeHour();
		data.minute = ctx_.game_state.world().timeMinute();
		data.day = ctx_.game_state.world().timeDay();
		data.month = ctx_.game_state.world().timeMonth();
		data.year = ctx_.game_state.world().timeYear();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TimeOfDayChanged, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Phase 13: Update day/night state for audio
	ctx_.Bridge().UpdateDayNightState();
#endif

	LOG_DEBUG(MOD_ZONE, "Time of day: {:02d}:{:02d} {:02d}/{:02d}{}",
		ctx_.game_state.world().timeHour(), ctx_.game_state.world().timeMinute(), ctx_.game_state.world().timeDay(), ctx_.game_state.world().timeMonth(), ctx_.game_state.world().timeYear());
}

std::string eqt::entities::FormatDoorSpawnDump(uint32_t loopIndex, const uint8_t* record)
{
	// B464: record[60] is the door's OWN id. Passing loopIndex here instead
	// was the defect -- it named the array slot and read as a door id.
	return fmt::format("Door {} (packet slot {}) raw bytes @60-67: {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
	                   record[60], loopIndex, record[60], record[61], record[62], record[63],
	                   record[64], record[65], record[66], record[67]);
}

void eqt::entities::EntitiesComponent::ZoneProcessSpawnDoor(const EQ::Net::Packet &p)
{
	// B919 (#238): named `game_state` owner row (WILLEQ_LEDGER_OWNERS=game_state):
	// the door spawn table (the DoorStateManager's doors_, reached through
	// ctx_.game_state.doors()) and the DoorSpawned bridge events are gs.doors.
	eqt::memory::StartupScope owner("gs.doors");
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
		ctx_.game_state.doors().addDoor(door);

		// Debug: dump raw bytes at critical offsets (60-68)
		LOG_DEBUG(MOD_ENTITY, "{}", FormatDoorSpawnDump(i, data));

		LOG_DEBUG(MOD_ENTITY, "Door {}: '{}' at ({:.1f}, {:.1f}, {:.1f}) heading={:.1f} incline={} size={} type={} state_at_spawn={} invert={}",
		         door.doorId, door.name, door.x, door.y, door.z, door.heading,
		         door.incline, door.size, door.opentype, door_data->state_at_spawn, door.invertState ? 1 : 0);

		// Publish DoorSpawned event to bridge
		if (ctx_.bridge) {
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
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::DoorSpawned, std::move(data)));
		}
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessSendZonepoints(const EQ::Net::Packet &p)
{
	// Packet format: opcode(2) + count(4) + count * ZonePoint_Entry(24)
	// Minimum size: 2 + 4 = 6 bytes
	if (p.Length() < 6) {
		return;
	}

	uint32_t count = p.GetUInt32(2);

	LOG_DEBUG(MOD_ZONE, "Received zone points: {} entries", count);

	// Parse zone points and store them in ZoneLines
	if (ctx_.Zone().ZoneLinesPtr() && count > 0) {
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

			if (EverQuest::GetDebugLevel() >= 3) {
				LOG_TRACE(MOD_ZONE, "  Zone point {}: zone={} pos=({},{},{}) heading={}",
					zp.number, zp.targetZoneId, zp.targetX, zp.targetY, zp.targetZ, zp.heading);
			}
		}

		ctx_.Zone().ZoneLinesPtr()->setServerZonePoints(zonePoints);

		// Publish ZoneLineBoundingBoxes event to bridge
		if (ctx_.bridge) {
			eqt::state::ZoneLineBoundingBoxesData data;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneLineBoundingBoxes, std::move(data)));
		}

		LOG_DEBUG(MOD_ZONE, "Stored {} zone points in ZoneLines", zonePoints.size());
	}

	// After zone points, check if we need to send server filter
	// This is old logic - server filter is now sent after GuildMOTD
	// But keeping it here for compatibility if the flow is different
	if (ctx_.Zone().ZoneEntrySent() && !ctx_.game_state.world().isClientReady()) {
		// This path shouldn't normally be taken anymore
		if (!ctx_.Zone().ServerFilterSent() && ctx_.Zone().SendExpZoneinReceived()) {
			LOG_DEBUG(MOD_ZONE, "ZoneProcessSendZonepoints calling ZoneSendSetServerFilter (fallback)");
			ctx_.Zone().ZoneSendSetServerFilter();
		}

		// Send client ready - This shouldn't happen here anymore
		if (!ctx_.game_state.world().isClientReady() && ctx_.Zone().ServerFilterSent()) {
			LOG_DEBUG(MOD_ZONE, "ZoneProcessSendZonepoints calling ZoneSendClientReady");
			ctx_.Zone().ZoneSendClientReady();
		}
	}
}

void eqt::entities::EntitiesComponent::ZoneProcessSpawnAppearance(const EQ::Net::Packet &p)
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
	case AT_WHO_LEVEL:
		// B1144 (#464): the spawn's level. The official client writes it to the
		// spawn's level byte (eqgame.exe 0x45242f: type 1 -> spawn +0x24c, spawn
		// id 0 = the local player), the byte its spellsnew.eff slot gate reads
		// (0x495cc3); eqemu sends it to every client on #level / a level-up. The
		// B1144 lane-5 observer drew a caster's level-24/39 slots only after it.
		{
			const uint16_t id = spawn_id != 0 ? spawn_id : ctx_.self.MySpawnId();
			auto it = ctx_.entities.find(id);
			if (it != ctx_.entities.end()) it->second.level = static_cast<uint8_t>(parameter);
		}
		break;

	case AT_ANIMATION:
		// Animation update - set the entity's animation
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.animation = static_cast<uint8_t>(parameter);
			}

			// T344 (B001): a pose the SERVER forces on US (root/mez
			// knockdown, a GM #kill/#summon, anything that makes eqemu
			// SendAppearancePacket(AT_Anim) for our own spawn id) has to
			// reach the player's positionState -- until now nothing did,
			// so game_state.player().positionState() stayed Standing
			// through every server-forced pose. It goes through
			// ApplyPositionState(), NOT SetPositionState(): echoing back a
			// pose we were just told about is wrong, and T311 Leg A's
			// capture has the official client never doing it.
			//
			// ANIM_LYING maps to POS_FEIGN_DEATH because that is the state
			// whose outbound pose IS ANIM_LYING (see SetPositionState) --
			// the wire cannot tell feign from lying-dead. If we already
			// know we are POS_DEAD (our own OP_Death set it, see
			// ZoneProcessDeath) the Lying appearance is that same death and
			// must not downgrade the state.
			if (spawn_id != 0 && spawn_id == ctx_.self.MySpawnId()) {
				bool haveState = true;
				PositionState selfState = POS_STANDING;
				switch (parameter) {
				case ANIM_STAND:
				case ANIM_STANDING: selfState = POS_STANDING;   break;
				case ANIM_SITTING:  selfState = POS_SITTING;    break;
				case ANIM_CROUCHING:selfState = POS_CROUCHING;  break;
				case ANIM_LYING:    selfState = POS_FEIGN_DEATH; break;
				default:            haveState = false;          break;
				}
				if (haveState &&
				    !(selfState == POS_FEIGN_DEATH &&
				      ctx_.game_state.player().positionState() ==
				          eqt::state::PositionState::Dead)) {
					ctx_.Movement().ApplyPositionState(selfState);
				}
			}

			// Publish animation events via bridge (D17b: bridge is sole path to renderer)
			if (ctx_.bridge) {
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
						ctx_.bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::EntityPoseStateChanged, std::move(data)));
					}
					eqt::state::EntityAnimationEventData data;
					data.spawnId = spawn_id;
					data.animCode = 0;
					data.animName = animCode;
					data.loop = loop;
					data.playThrough = playThrough;
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
					LogTargetEntity(spawn_id, "SpawnAppearance set animation '{}' pose={} on spawn_id={}",
						animCode, static_cast<int>(poseState), spawn_id);
				}
			}
		}
		break;

	case AT_DIE:
		// Entity died - trigger death animation (D17b: bridge-only)
		if (ctx_.bridge) {
			eqt::state::EntityDeathAnimationData data;
			data.spawnId = spawn_id;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityDeathAnimation, std::move(data)));
		}
		break;

	case AT_HP_UPDATE:
		// HP update
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.hp_percent = static_cast<uint8_t>(parameter);
			}
		}
		break;

	case AT_SPAWN_ID:
		// Server is assigning us a new spawn ID (happens after zoning)
		// spawn_id=0 means "self", parameter contains the new spawn ID
		if (spawn_id == 0 && parameter != 0) {
			uint16_t newSpawnId = static_cast<uint16_t>(parameter);
			LOG_INFO(MOD_ZONE, "Server assigned new spawn ID: {} (was {})", newSpawnId, ctx_.self.MySpawnId());
			ctx_.self.SetMySpawnId(newSpawnId);  // T288: one store
			if (ctx_.trade_manager) {
				ctx_.trade_manager->setMySpawnId(ctx_.self.MySpawnId());
			}
		}
		break;

	case AT_INVISIBLE:
		// Invisibility state change
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.is_invisible = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} invisibility set to {}", spawn_id, it->second.is_invisible);
			}
			// TODO: Graphics rendering could hide/fade invisible entities
		}
		break;

	case AT_SNEAK:
		// Sneaking state change
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.is_sneaking = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} sneaking set to {}", spawn_id, it->second.is_sneaking);
			}
		}
		break;

	case AT_LINKDEAD:
		// Linkdead state change
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.is_linkdead = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} linkdead set to {}", spawn_id, it->second.is_linkdead);
			}
		}
		break;

	case AT_FLYMODE:
		// Levitate/fly mode change
		// 0=ground, 1=flying, 2=levitate, 3=water, 4=floating, 5=levitate+run
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.flymode = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} flymode set to {}", spawn_id, parameter);
				// B892 (GitLab #208): a self flymode 2/5 IS levitation for the
				// predicate, even when the buff list is empty (a GM
				// #levitate-style effect reaches self even though the cast
				// path does not). Re-run the game-thread sync on the OR half.
				if (spawn_id == ctx_.self.MySpawnId()) {
					ctx_.Movement().SyncLevitation(it->second.flymode);
				}
			}
			// TODO: Graphics rendering could show levitation animation
		}
		break;

	case AT_AFK:
		// AFK state change
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.is_afk = (parameter != 0);
				LOG_DEBUG(MOD_ENTITY, "Entity {} AFK set to {}", spawn_id, it->second.is_afk);
			}
		}
		break;

	case AT_ANONYMOUS:
		// Anonymous/roleplay status change: 0=normal, 1=anon, 2=roleplay
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.anon_status = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} anon_status set to {}", spawn_id, parameter);
			}
		}
		break;

	case AT_PET:
		// Pet owner ID update
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.pet_owner_id = parameter;
				it->second.is_pet = (parameter != 0) ? 1 : 0;
				LOG_DEBUG(MOD_ENTITY, "Entity {} pet_owner_id set to {}", spawn_id, parameter);
			}
		}
		break;

	case AT_SIZE:
		// T318: shrink, grow and illusion. The SERVER is authoritative --
		// eqemu's Mob::ChangeSize() (zone/mob.cpp) does
		//
		//     size = clamp(in_size, 1, 255);           // clients/pets: [3, 15]
		//     SendAppearancePacket(AppearanceType::Size, (uint32) size);
		//
		// to the WHOLE ZONE including self, so `parameter` is the size itself,
		// truncated to an integer -- on exactly the scale Spawn_Struct::size
		// uses, where a normal humanoid is 6. It was being divided by 10 here
		// ("EQ uses size*10"), which the server source flatly contradicts: a
		// shrunk player would have landed at 0.3 instead of 3. Nothing read the
		// value anyway -- the renderer was never told, so every shrink and grow
		// in the game was dropped while the client reported success.
		{
			float new_size = static_cast<float>(parameter);
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.size = new_size;
				LOG_DEBUG(MOD_ENTITY, "Entity {} size set to {}", spawn_id, new_size);
			}
			if (ctx_.bridge && new_size > 0.0f) {
				eqt::state::EntitySizeChangedData data;
				data.spawnId = spawn_id;
				data.size = new_size;
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::EntitySizeChanged, std::move(data)));
			}
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
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.guild_id = normalized;
			}
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.Guild().SetGuildID(normalized);
				LOG_INFO(MOD_ENTITY, "Our guild id changed to {} via SpawnAppearance", normalized);
			}
		}
		break;

	case AT_LIGHT:
		// Light source change
		{
			auto it = ctx_.entities.find(spawn_id);
			if (it != ctx_.entities.end()) {
				it->second.light = static_cast<uint8_t>(parameter);
				LOG_DEBUG(MOD_ENTITY, "Entity {} light set to {}", spawn_id, parameter);
				if (ctx_.bridge) {
					eqt::state::EntityLightChangedData data;
					data.spawnId = spawn_id;
					data.lightLevel = static_cast<uint8_t>(parameter);
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
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

