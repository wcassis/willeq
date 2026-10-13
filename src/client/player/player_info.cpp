// PlayerComponent: the tribute/AA/exp zone-in handlers and the two GameState
// entity-sync helpers.
//
// Was the mover half of src/client/eq_player_info.cpp (Batch F unit F07,
// narrowed by Batch J unit J07). K08 (Batch K,
// refactor/K_component_extraction.md) split that TU: the player-state facade
// -- the stat/currency/position/flag/group accessors the whole tree reads --
// STAYED on EverQuest in eq_player_info.cpp; everything else is here, rebased
// onto eqt::player::PlayerComponent. Bodies are verbatim; the only edits are
// the substitutions Mechanism 4 allows.

#include "client/eq.h"
#include "client/inspect_answer.h"
#include "client/bind_wound_outcome.h"
#include "client/who_format.h"
#include "client/who_response.h"
#include "client/zone_lines.h"
#include "client/combat.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/spell/buff_cancel_request.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/sound_assets.h"
#endif

#include "client/world_object.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"

#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

#include "client/player/player_component.h"

// ============================================================================
// Cluster 1: Tribute/AA stubs
// ============================================================================

void eqt::player::PlayerComponent::ZoneProcessTributeUpdate(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute update");
}

void eqt::player::PlayerComponent::ZoneProcessTributeTimer(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute timer");
}

void eqt::player::PlayerComponent::ZoneProcessSendAATable(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA table data");
	ctx_.Zone().AATableCount()++;
	
	// Check if all Zone Request phase responses received
	ctx_.Zone().CheckZoneRequestPhaseComplete();
}

void eqt::player::PlayerComponent::ZoneProcessRespondAA(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA response");
	
	// Check if all Zone Request phase responses received
	ctx_.Zone().CheckZoneRequestPhaseComplete();
}

void eqt::player::PlayerComponent::ZoneProcessTributeInfo(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute info");
	ctx_.Zone().TributeCount()++;
	
	// Check if all Zone Request phase responses received
	ctx_.Zone().CheckZoneRequestPhaseComplete();
}

void eqt::player::PlayerComponent::ZoneProcessSendGuildTributes(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received guild tributes");
	ctx_.Zone().GuildTributeCount()++;
	
	// Check if all Zone Request phase responses received
	ctx_.Zone().CheckZoneRequestPhaseComplete();
}

void eqt::player::PlayerComponent::ZoneProcessSendAAStats(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA stats");
}

void eqt::player::PlayerComponent::ZoneProcessSendExpZonein(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received exp zone in - this triggers SendZoneInPackets()");

	ctx_.Zone().SendExpZoneinReceived() = true;

	// ExpZonein triggers the server to send ExpUpdate, RaidUpdate, and GuildMOTD
	// We'll wait for GuildMOTD before sending final packets
}

void eqt::player::PlayerComponent::ZoneProcessWorldObjectsSent(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received world objects sent");

	// After world objects, send exp zonein if not already sent
	if (!ctx_.Zone().ExpZoneinSent()) {
		ctx_.Zone().ZoneSendSendExpZonein();
	}
}


// ============================================================================
// Cluster 3: GameState sync + group accessors
// ============================================================================

void eqt::player::PlayerComponent::SyncEntityToGameState(const Entity& entity)
{
	// Convert from EQ::Entity (snake_case) to eqt::state::Entity (camelCase)
	eqt::state::Entity stateEntity;
	stateEntity.spawnId = entity.spawn_id;
	stateEntity.name = entity.name;
	stateEntity.x = entity.x;
	stateEntity.y = entity.y;
	stateEntity.z = entity.z;
	stateEntity.heading = entity.heading;
	stateEntity.level = entity.level;
	stateEntity.classId = entity.class_id;
	stateEntity.raceId = entity.race_id;
	stateEntity.gender = entity.gender;
	stateEntity.guildId = entity.guild_id;
	stateEntity.animation = entity.animation;
	stateEntity.hpPercent = entity.hp_percent;
	stateEntity.curMana = entity.cur_mana;
	stateEntity.maxMana = entity.max_mana;
	stateEntity.size = entity.size;
	stateEntity.isCorpse = entity.is_corpse;
	stateEntity.face = entity.face;
	stateEntity.haircolor = entity.haircolor;
	stateEntity.hairstyle = entity.hairstyle;
	stateEntity.beardcolor = entity.beardcolor;
	stateEntity.beard = entity.beard;
	stateEntity.equipChest2 = entity.equip_chest2;
	stateEntity.helm = entity.helm;
	stateEntity.showhelm = entity.showhelm;
	stateEntity.bodytype = entity.bodytype;
	stateEntity.npcType = entity.npc_type;
	stateEntity.light = entity.light;
	for (int i = 0; i < 9; ++i) {
		stateEntity.equipment[i] = entity.equipment[i];
		stateEntity.equipmentTint[i] = entity.equipment_tint[i];
	}
	stateEntity.deltaX = entity.delta_x;
	stateEntity.deltaY = entity.delta_y;
	stateEntity.deltaZ = entity.delta_z;
	stateEntity.deltaHeading = entity.delta_heading;
	stateEntity.lastUpdateTime = entity.last_update_time;
	stateEntity.petOwnerId = entity.pet_owner_id;
	stateEntity.primaryWeaponSkill = entity.primary_weapon_skill;
	stateEntity.secondaryWeaponSkill = entity.secondary_weapon_skill;

	// Add or update in EntityManager
	if (!ctx_.game_state.entities().hasEntity(entity.spawn_id)) {
		ctx_.game_state.entities().addEntity(stateEntity);
	} else {
		// Update existing entity
		auto* existingEntity = ctx_.game_state.entities().getEntityMutable(entity.spawn_id);
		if (existingEntity) {
			*existingEntity = stateEntity;
		}
	}

}

void eqt::player::PlayerComponent::RemoveEntityFromGameState(uint16_t spawnId)
{
	ctx_.game_state.entities().removeEntity(spawnId);
}

