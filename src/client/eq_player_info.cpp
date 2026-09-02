// EverQuest player stat getters, tribute/AA stubs, GameState/group sync.
//
// Clusters 1-3, split out of eq.cpp by Batch F unit F07 and narrowed to these
// clusters by Batch J unit J07. Moved verbatim, no behavior change. See
// refactor/J07_eq_player_info_split.md and refactor/F07_player_info.md.

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

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#endif

#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

// ============================================================================
// Cluster 1: Tribute/AA stubs
// ============================================================================

void EverQuest::ZoneProcessTributeUpdate(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute update");
}

void EverQuest::ZoneProcessTributeTimer(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute timer");
}

void EverQuest::ZoneProcessSendAATable(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA table data");
	m_aa_table_count++;
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessRespondAA(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA response");
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessTributeInfo(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received tribute info");
	m_tribute_count++;
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessSendGuildTributes(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received guild tributes");
	m_guild_tribute_count++;
	
	// Check if all Zone Request phase responses received
	CheckZoneRequestPhaseComplete();
}

void EverQuest::ZoneProcessSendAAStats(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received AA stats");
}

void EverQuest::ZoneProcessSendExpZonein(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received exp zone in - this triggers SendZoneInPackets()");

	m_send_exp_zonein_received = true;

	// ExpZonein triggers the server to send ExpUpdate, RaidUpdate, and GuildMOTD
	// We'll wait for GuildMOTD before sending final packets
}

void EverQuest::ZoneProcessWorldObjectsSent(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received world objects sent");

	// After world objects, send exp zonein if not already sent
	if (!m_exp_zonein_sent) {
		ZoneSendSendExpZonein();
	}
}

// ============================================================================
// Cluster 2: Player stat getters
// ============================================================================

uint8_t EverQuest::GetLevel() const
{
	return m_game_state.player().level();
}

uint32_t EverQuest::GetClass() const
{
	return m_game_state.player().classId();
}

uint32_t EverQuest::GetRace() const
{
	return m_game_state.player().race();
}

uint32_t EverQuest::GetGender() const
{
	return m_game_state.player().gender();
}

uint32_t EverQuest::GetSTR() const
{
	return m_game_state.player().STR();
}

uint32_t EverQuest::GetSTA() const
{
	return m_game_state.player().STA();
}

uint32_t EverQuest::GetDEX() const
{
	return m_game_state.player().DEX();
}

uint32_t EverQuest::GetAGI() const
{
	return m_game_state.player().AGI();
}

uint32_t EverQuest::GetINT() const
{
	return m_game_state.player().INT();
}

uint32_t EverQuest::GetWIS() const
{
	return m_game_state.player().WIS();
}

uint32_t EverQuest::GetCHA() const
{
	return m_game_state.player().CHA();
}

uint32_t EverQuest::GetCurrentHP() const
{
	return m_game_state.player().curHP();
}

uint32_t EverQuest::GetMaxHP() const
{
	return m_game_state.player().maxHP();
}

uint32_t EverQuest::GetCurrentMana() const
{
	return m_game_state.player().curMana();
}

uint32_t EverQuest::GetMaxMana() const
{
	return m_game_state.player().maxMana();
}

uint32_t EverQuest::GetCurrentEndurance() const
{
	return m_game_state.player().curEndurance();
}

uint32_t EverQuest::GetMaxEndurance() const
{
	return m_game_state.player().maxEndurance();
}

uint32_t EverQuest::GetDeity() const
{
	return m_game_state.player().deity();
}

uint32_t EverQuest::GetPlatinum() const
{
	return m_game_state.player().platinum();
}

uint32_t EverQuest::GetGold() const
{
	return m_game_state.player().gold();
}

uint32_t EverQuest::GetSilver() const
{
	return m_game_state.player().silver();
}

uint32_t EverQuest::GetCopper() const
{
	return m_game_state.player().copper();
}

uint32_t EverQuest::GetBankPlatinum() const
{
	return m_game_state.player().bankPlatinum();
}

uint32_t EverQuest::GetBankGold() const
{
	return m_game_state.player().bankGold();
}

uint32_t EverQuest::GetBankSilver() const
{
	return m_game_state.player().bankSilver();
}

uint32_t EverQuest::GetBankCopper() const
{
	return m_game_state.player().bankCopper();
}

uint32_t EverQuest::GetPracticePoints() const
{
	return m_game_state.player().practicePoints();
}

float EverQuest::GetWeight() const
{
	return m_game_state.player().weight();
}

float EverQuest::GetMaxWeight() const
{
	return m_game_state.player().maxWeight();
}

// ============================================================================
// Cluster 3: GameState sync + group accessors
// ============================================================================

void EverQuest::SyncEntityToGameState(const Entity& entity)
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
	stateEntity.isPet = entity.is_pet;
	stateEntity.petOwnerId = entity.pet_owner_id;
	stateEntity.primaryWeaponSkill = entity.primary_weapon_skill;
	stateEntity.secondaryWeaponSkill = entity.secondary_weapon_skill;

	// Add or update in EntityManager
	if (!m_game_state.entities().hasEntity(entity.spawn_id)) {
		m_game_state.entities().addEntity(stateEntity);
	} else {
		// Update existing entity
		auto* existingEntity = m_game_state.entities().getEntityMutable(entity.spawn_id);
		if (existingEntity) {
			*existingEntity = stateEntity;
		}
	}

}

void EverQuest::RemoveEntityFromGameState(uint16_t spawnId)
{
	m_game_state.entities().removeEntity(spawnId);
}

// ============================================================================
// Group Accessors and Sync Helper (Phase 7.5 - read from GroupState)
// ============================================================================

bool EverQuest::IsInGroup() const
{
	return m_game_state.group().inGroup();
}

bool EverQuest::IsGroupLeader() const
{
	return m_game_state.group().isLeader();
}

int EverQuest::GetGroupMemberCount() const
{
	return m_game_state.group().memberCount();
}

const std::string& EverQuest::GetGroupLeaderName() const
{
	return m_game_state.group().leaderName();
}

bool EverQuest::HasPendingGroupInvite() const
{
	return m_game_state.group().hasPendingInvite();
}

const std::string& EverQuest::GetPendingInviterName() const
{
	return m_game_state.group().pendingInviterName();
}

// T286: SyncGroupMemberToGameState() is DELETED. It existed only to copy
// EverQuest's own GroupMember array into GroupState's after every write;
// with one array and one struct there is nothing left to convert.

