// EverQuest player-info and low-coupling misc clusters.
//
// Split out of eq.cpp (Batch F, unit F07): tribute/AA stubs, player stat
// getters, GameState sync + group accessors, task/DZ/spell-effect handlers,
// poison/track, emote/AFK/camp/logout, rezz/who/inspect, corpse/consent,
// assist/boat/split/LFG/shielding/discipline/banker, save/popup/duel/
// bindwound/track-target, cosmetic/misc social commands, and camera/mp3/
// sound handlers. Moved verbatim, no behavior change. See
// refactor/F07_player_info.md.

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

void EverQuest::SyncGroupMemberToGameState(int index, const GroupMember& member)
{
	// Convert from EQ::GroupMember (snake_case) to eqt::state::GroupMember (camelCase)
	eqt::state::GroupMember stateMember;
	stateMember.name = member.name;
	stateMember.spawnId = member.spawn_id;
	stateMember.level = member.level;
	stateMember.classId = member.class_id;
	stateMember.hpPercent = member.hp_percent;
	stateMember.manaPercent = member.mana_percent;
	stateMember.isLeader = member.is_leader;
	stateMember.inZone = member.in_zone;

	m_game_state.group().setMember(index, stateMember);
}

// ============================================================================
// Cluster 4: Tasks/DZ/BeginCast/ManaChange/Buff/PetBuff
// ============================================================================

void EverQuest::ZoneProcessCompletedTasks(const EQ::Net::Packet &p)
{
	// CompletedTasks sends the list of completed tasks
	// This is informational - we can track it if needed
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received completed tasks list ({} bytes)", p.Length());
	}
}

void EverQuest::ZoneProcessDzCompass(const EQ::Net::Packet &p)
{
	// DzCompass updates the expedition/DZ compass direction
	// Format: heading (4 bytes float), x (4 bytes float), y (4 bytes float), z (4 bytes float)
	
	if (s_debug_level >= 2) {
		if (p.Length() >= 18) {
			float heading = p.GetFloat(2);
			float x = p.GetFloat(6);
			float y = p.GetFloat(10);
			float z = p.GetFloat(14);
			LOG_DEBUG(MOD_MAIN, "DZ compass update: heading {:.1f} to ({:.2f}, {:.2f}, {:.2f})",
				heading, x, y, z);
		}
	}
}

void EverQuest::ZoneProcessDzExpeditionLockoutTimers(const EQ::Net::Packet &p)
{
	// DzExpeditionLockoutTimers sends lockout timer information
	// This is informational for expeditions/DZs
	
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received DZ expedition lockout timers ({} bytes)", p.Length());
	}
}

void EverQuest::ZoneProcessBeginCast(const EQ::Net::Packet &p)
{
	// BeginCast indicates an entity is starting to cast a spell
	// Format: spawn_id (2 bytes), spell_id (2 bytes), cast_time (4 bytes)

	if (p.Length() < 10) {
		LOG_WARN(MOD_SPELL, "BeginCast packet too small: {} bytes", p.Length());
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint16_t spell_id = p.GetUInt16(4);
	uint32_t cast_time = p.GetUInt32(6);

	if (ShouldLog(MOD_SPELL, LOG_DEBUG)) {
		auto it = m_entities.find(spawn_id);
		std::string name = (it != m_entities.end()) ? EQT::toDisplayName(it->second.name) : "Unknown";
		LOG_DEBUG(MOD_SPELL, "{} (ID: {}) begins casting spell {} ({}ms)",
			name, spawn_id, spell_id, cast_time);
	}

#ifdef WITH_AUDIO
	// Play spell casting sound at caster's position
	if (m_audio_manager) {
		auto it = m_entities.find(spawn_id);
		if (it != m_entities.end()) {
			m_audio_manager->playSound(EQT::Audio::SoundId::SPELL_CAST,
				glm::vec3(it->second.x, it->second.y, it->second.z));
		}
	}
#endif

	// Notify spell manager about the cast
	if (m_spell_manager) {
		m_spell_manager->handleBeginCast(spawn_id, spell_id, cast_time);
	}

	// Publish SpellCastStarted event to bridge. This is the server-echoed
	// confirmation of BeginCast for *any* caster (self, NPC, other player);
	// GameStateSyncBridge (T44) filters to our own casts by casterId. For
	// our own cast, SpellManager already knows the target it chose at
	// beginCastFromGem()/beginCastById() time -- carry it so the bridge
	// doesn't need a second source of truth. targetId is meaningless for
	// non-self casters and left at its default (0).
	//
	// T221: source-side twin of the projection's self-filter -- re-applies
	// the echo for our own cast (castTimeMs may now be the server's, as the
	// projection allowed); the SpellCastStarted projection is redundant.
	if (spawn_id == GetEntityID()) {
		uint16_t self_target = m_spell_manager ? m_spell_manager->getCurrentTargetId() : 0;
		m_game_state.spells().setCasting(true, spell_id, self_target, cast_time);
	}
	if (m_bridge) {
		eqt::state::SpellCastStartedData data;
		data.casterId = spawn_id;
		data.spellId = spell_id;
		data.castTimeMs = cast_time;
		if (m_spell_manager && spawn_id == GetEntityID()) {
			data.targetId = m_spell_manager->getCurrentTargetId();
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SpellCastStarted, std::move(data)));
	}
}

void EverQuest::ZoneProcessManaChange(const EQ::Net::Packet &p)
{
	// ManaChange updates the player's own mana (sent after casting spells, etc.)
	// ManaChange_Struct format:
	//   new_mana (4 bytes), stamina (4 bytes), spell_id (4 bytes),
	//   keepcasting (1 byte), padding (3 bytes)
	// Total: 16 bytes + 2 byte opcode = 18 bytes

	if (p.Length() < 18) {
		LOG_WARN(MOD_SPELL, "ManaChange packet too small: {} bytes", p.Length());
		return;
	}

	// Skip opcode (2 bytes), then parse ManaChange_Struct
	uint32_t new_mana = p.GetUInt32(2);    // offset 0 in struct
	uint32_t stamina = p.GetUInt32(6);     // offset 4 in struct (endurance)
	uint32_t spell_id = p.GetUInt32(10);   // offset 8 in struct
	uint8_t keepcasting = p.GetUInt8(14);  // offset 12 in struct

	// Update player mana tracking (both local and GameState)
	m_mana = new_mana;
	m_game_state.player().setCurMana(new_mana);

	// Update our entity if we're tracking it
	if (m_my_spawn_id != 0) {
		auto it = m_entities.find(m_my_spawn_id);
		if (it != m_entities.end()) {
			it->second.cur_mana = static_cast<uint16_t>(std::min(new_mana, static_cast<uint32_t>(65535)));
		}
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_SPELL, "ManaChange: mana={}, stamina={}, spell_id={}, keepcasting={}",
			new_mana, stamina, spell_id, keepcasting);
	}

	// Notify spell manager if available
	if (m_spell_manager) {
		m_spell_manager->handleManaChange(new_mana, stamina, spell_id);
	}

	// Also update endurance tracking (both local and GameState)
	m_endurance = stamina;
	m_game_state.player().setEndurance(stamina, m_max_endurance);

	// Update combat stats with new mana value
	if (m_combat_manager) {
		CombatStats stats;
		stats.current_hp = m_cur_hp;
		stats.max_hp = m_max_hp;
		stats.current_mana = new_mana;
		stats.max_mana = m_max_mana;
		stats.current_endurance = stamina;
		stats.max_endurance = 0;
		stats.hp_percent = (m_max_hp > 0) ? (static_cast<float>(m_cur_hp) * 100.0f / static_cast<float>(m_max_hp)) : 100.0f;
		stats.mana_percent = (m_max_mana > 0) ? (static_cast<float>(new_mana) * 100.0f / static_cast<float>(m_max_mana)) : 100.0f;
		stats.endurance_percent = 100.0f;
		m_combat_manager->UpdateCombatStats(stats);
	}

	// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical note in
	// eq_entities.cpp's HP-change UpdateInventoryStats() call site.
	UpdateInventoryStats();

	// Publish PlayerStatsChanged when mana/endurance changes
	if (m_bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = m_cur_hp;
		data.maxHP = m_max_hp;
		data.curMana = new_mana;
		data.maxMana = m_max_mana;
		data.curEndurance = stamina;
		data.maxEndurance = m_max_endurance;
		data.level = m_level;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}
}

void EverQuest::ZoneProcessBuff(const EQ::Net::Packet &p)
{
	// SpellBuffPacket_Struct format:
	//   entityid (4 bytes), SpellBuff_Struct (20 bytes), slotid (4 bytes), bufffade (4 bytes)
	// Total: 32 bytes + 2 byte opcode = 34 bytes minimum

	if (p.Length() < 34) {
		LOG_WARN(MOD_SPELL, "Buff packet too small: {} bytes", p.Length());
		return;
	}

	// Parse SpellBuffPacket_Struct
	uint32_t entity_id = p.GetUInt32(2);

	// SpellBuff_Struct at offset 6
	EQT::SpellBuff_Struct buff;
	buff.effect_type = p.GetUInt8(6);
	buff.level = p.GetUInt8(7);
	buff.bard_modifier = p.GetUInt8(8);
	buff.unknown003 = p.GetUInt8(9);
	buff.spellid = p.GetUInt32(10);
	buff.duration = p.GetInt32(14);
	buff.counters = p.GetUInt32(18);
	buff.player_id = p.GetUInt32(22);

	uint32_t slot_id = p.GetUInt32(26);
	uint32_t buff_fade = p.GetUInt32(30);

	// Check if this is our buff
	bool is_self = (entity_id == m_my_spawn_id);

	LOG_DEBUG(MOD_SPELL, "Buff: entity={}, spell={}, slot={}, duration={}, fade={}, effect_type={}",
		entity_id, buff.spellid, slot_id, buff.duration, buff_fade, buff.effect_type);

	// Update buff manager
	if (m_buff_manager) {
		if (buff_fade == 1) {
			// Buff is fading
			if (is_self) {
				m_buff_manager->removePlayerBuffBySlot(static_cast<uint8_t>(slot_id));

				// Publish BuffRemoved event to bridge
				if (m_bridge) {
					eqt::state::BuffRemovedData data;
					data.slot = static_cast<uint8_t>(slot_id);
					m_bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::BuffRemoved, std::move(data)));
				}
			} else {
				m_buff_manager->removeBuffBySlot(static_cast<uint16_t>(entity_id), static_cast<uint8_t>(slot_id));
			}
		} else if (buff.effect_type == 2 && buff.spellid != 0 && buff.spellid != 0xFFFFFFFF) {
			// Buff is being applied
			if (is_self) {
				m_buff_manager->setPlayerBuff(static_cast<uint8_t>(slot_id), buff);
				PublishPlayerBuffUpdated(static_cast<uint8_t>(slot_id), buff);
			} else {
				m_buff_manager->setEntityBuff(static_cast<uint16_t>(entity_id),
					static_cast<uint8_t>(slot_id), buff);
			}
		}
	}
}

// T277: game -> renderer for ONE player buff slot. Extracted from
// ZoneProcessBuff()'s apply branch so the PlayerProfile's zone-in buffs
// (eq.cpp) can publish the same event through the same body -- before this
// they went into BuffManager and stopped there, so the buff bar started
// every session empty no matter what the player was actually holding.
void EverQuest::PublishPlayerBuffUpdated(uint8_t slot, const EQT::SpellBuff_Struct& buff)
{
	if (!m_bridge) {
		return;
	}
	eqt::state::BuffUpdatedData data;
	data.slot = slot;
	data.spellId = buff.spellid;
	data.ticksLeft = static_cast<uint32_t>(buff.duration);
	auto caster_it = m_entities.find(static_cast<uint16_t>(buff.player_id));
	if (caster_it != m_entities.end()) {
		data.casterName = EQT::toDisplayName(caster_it->second.name);
	}
	if (m_spell_manager) {
		const auto* spell = m_spell_manager->getSpell(buff.spellid);
		data.spellName = spell ? spell->name : "";
		data.iconId = spell ? spell->spell_icon : 0;
	}
	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::BuffUpdated, std::move(data)));
}

// T277: publish every occupied slot of the PlayerProfile's buff array.
//
// eqemu only emits OP_Buff for a DURATION CHANGE or a FADE
// (Client::SendBuffDurationPacket / MakeBuffFadePacket) -- a buff that is
// simply still running never produces one -- so the profile is the only
// place a session ever hears about the buffs it starts with. Before this
// they reached BuffManager and stopped, and renderBuffBar() drew an empty
// bar all session with nothing for the click gesture to hit.
void EverQuest::PublishPlayerBuffs(const EQT::SpellBuff_Struct* buffs, size_t count)
{
	if (!buffs) {
		return;
	}
	for (size_t i = 0; i < count && i < EQ::BuffManager::MAX_PLAYER_BUFFS; ++i) {
		// Same "occupied" test BuffManager::setPlayerBuffs() applies, so the
		// bar and the model can never disagree about which slots are in use.
		if (buffs[i].spellid == 0 || buffs[i].spellid == 0xFFFFFFFF ||
		    buffs[i].effect_type == 0) {
			continue;
		}
		PublishPlayerBuffUpdated(static_cast<uint8_t>(i), buffs[i]);
	}
}

// T277: resolve a user-facing /removebuff argument to a buff slot.
//
// Accepts either a 1-based slot number as the buff bar counts them, or a
// spell-name substring (case-insensitive), matched against the player's
// active buffs only -- never the whole spell database, so "there is no such
// buff" and "you are not holding that buff" are the same answer.
void EverQuest::RequestCancelBuff(const std::string& args)
{
	if (!m_buff_manager) {
		AddChatSystemMessage("Buff system not initialized.");
		return;
	}

	std::string arg = args;
	arg.erase(0, arg.find_first_not_of(" \t"));
	size_t end = arg.find_last_not_of(" \t");
	if (end != std::string::npos) arg.erase(end + 1);

	if (arg.empty()) {
		AddChatSystemMessage("Usage: /removebuff <slot# | spell name>");
		return;
	}

	// All digits -> a slot number, 1-based on the way in.
	if (arg.find_first_not_of("0123456789") == std::string::npos) {
		unsigned long n = std::strtoul(arg.c_str(), nullptr, 10);
		if (n < 1 || n > EQ::BuffManager::MAX_PLAYER_BUFFS) {
			AddChatSystemMessage(fmt::format("Buff slot must be 1-{}.",
				EQ::BuffManager::MAX_PLAYER_BUFFS));
			return;
		}
		SendBuffCancelRequest(static_cast<uint8_t>(n - 1));
		return;
	}

	if (!m_spell_manager) {
		AddChatSystemMessage("Spell system not initialized.");
		return;
	}

	std::string needle = arg;
	std::transform(needle.begin(), needle.end(), needle.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	int match = -1;
	int matchCount = 0;
	std::string matchNames;
	for (const EQ::ActiveBuff& b : m_buff_manager->getPlayerBuffs()) {
		if (b.slot < 0) continue;
		const EQ::SpellData* spell = m_spell_manager->getSpell(b.spell_id);
		if (!spell) continue;
		std::string name = spell->name;
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower == needle) {  // exact wins outright
			SendBuffCancelRequest(static_cast<uint8_t>(b.slot));
			return;
		}
		if (lower.find(needle) != std::string::npos) {
			++matchCount;
			if (match < 0) match = b.slot;
			if (matchCount <= 5) {
				if (!matchNames.empty()) matchNames += ", ";
				matchNames += fmt::format("{} (slot {})", name, b.slot + 1);
			}
		}
	}

	if (matchCount == 0) {
		AddChatSystemMessage(fmt::format("You do not have a buff matching '{}'.", arg));
		return;
	}
	if (matchCount > 1) {
		AddChatSystemMessage(fmt::format("Multiple buffs match '{}': {}", arg, matchNames));
		return;
	}
	SendBuffCancelRequest(static_cast<uint8_t>(match));
}

// T277: ask the server to drop one of OUR buffs (the click-off gesture and
// /removebuff both land here). See include/client/spell/buff_cancel_request.h
// for the wire truth -- Titanium has no OP_BuffRemoveRequest, this is OP_Buff.
//
// Deliberately NOT optimistic: nothing is removed from BuffManager here. The
// buff leaves the model only when the server confirms with its own OP_Buff
// carrying bufffade == 1, which ZoneProcessBuff() above already handles. A
// refusal (detrimental spell) echoes our own bytes back and the buff stays.
void EverQuest::SendBuffCancelRequest(uint8_t slot)
{
	if (!m_buff_manager) {
		LOG_WARN(MOD_SPELL, "No buff manager, cannot cancel buff");
		return;
	}

	// The two things the client can know on its own: a slot that cannot
	// exist, and a slot that holds nothing. Everything else (detrimental,
	// server-pinned) is the server's call.
	if (slot >= EQ::BuffManager::MAX_PLAYER_BUFFS) {
		AddChatSystemMessage(fmt::format("Buff slot must be 1-{}.",
			EQ::BuffManager::MAX_PLAYER_BUFFS));
		return;
	}

	const EQ::ActiveBuff* buff = m_buff_manager->getPlayerBuffBySlot(slot);
	if (!buff) {
		AddChatSystemMessage(fmt::format("No buff in slot {}.", slot + 1));
		return;
	}

	if (!m_zone_connection) {
		++m_replay_suppressed_sends;
		LOG_WARN(MOD_SPELL, "No zone connection, cannot cancel buff in slot {}", slot);
		return;
	}

	auto bytes = EQ::BuildBuffCancelPacket(HC_OP_Buff, m_my_spawn_id, slot, *buff);

	EQ::Net::DynamicPacket packet;
	packet.Resize(bytes.size());
	std::memcpy(packet.Data(), bytes.data(), bytes.size());

	std::string name;
	if (m_spell_manager) {
		const EQ::SpellData* spell = m_spell_manager->getSpell(buff->spell_id);
		if (spell) name = spell->name;
	}
	LOG_INFO(MOD_SPELL, "Requesting buff cancel: slot={} spell={} ({})",
		slot, buff->spell_id, name.empty() ? "unknown" : name);

	DumpPacket("C->S", HC_OP_Buff, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::ZoneProcessPetBuffWindow(const EQ::Net::Packet &p)
{
	// PetBuff_Struct format:
	//   petid (4 bytes), spellid[30] (120 bytes), ticsremaining[30] (120 bytes), buffcount (4 bytes)
	// Total: 248 bytes + 2 byte opcode = 250 bytes

	constexpr size_t MIN_PACKET_SIZE = 2 + sizeof(EQT::PetBuff_Struct);
	if (p.Length() < MIN_PACKET_SIZE) {
		LOG_WARN(MOD_ZONE, "PetBuffWindow packet too small: {} bytes (expected {})", p.Length(), MIN_PACKET_SIZE);
		return;
	}

	const EQT::PetBuff_Struct* petBuffs = reinterpret_cast<const EQT::PetBuff_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Verify this is for our pet
	if (m_pet_spawn_id == 0) {
		LOG_DEBUG(MOD_SPELL, "Received PetBuffWindow but no pet tracked, petid={}", petBuffs->petid);
		return;
	}

	if (petBuffs->petid != m_pet_spawn_id) {
		LOG_DEBUG(MOD_SPELL, "Received PetBuffWindow for different pet: {} (our pet: {})",
			petBuffs->petid, m_pet_spawn_id);
		return;
	}

	LOG_DEBUG(MOD_SPELL, "PetBuffWindow: petid={} buffcount={}", petBuffs->petid, petBuffs->buffcount);

	// Update buff manager with pet buffs
	if (m_buff_manager) {
		m_buff_manager->setPetBuffs(static_cast<uint16_t>(petBuffs->petid), *petBuffs);
	}
}

// ============================================================================
// Cluster 5: ApplyPoison / Track
// ============================================================================

void EverQuest::SendApplyPoison(uint32_t inventory_slot)
{
	if (!IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot apply poison - not fully zoned in");
		return;
	}

	LOG_INFO(MOD_ENTITY, "Sending apply poison for inventory slot {}", inventory_slot);

	// Build ApplyPoison packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::ApplyPoison_Struct));  // opcode + struct

	p.PutUInt16(0, HC_OP_ApplyPoison);
	p.PutUInt32(2, inventory_slot);  // inventorySlot
	p.PutUInt32(6, 0);               // success (0 when sending, server fills in response)

	m_zone_connection->QueuePacket(p, 0, true);
}

void EverQuest::ZoneProcessApplyPoison(const EQ::Net::Packet &p)
{
	// Server response to apply poison attempt
	// ApplyPoison_Struct: inventorySlot (4), success (4) = 8 bytes + 2 opcode = 10 bytes
	if (p.Length() < 10) {
		LOG_WARN(MOD_ENTITY, "ApplyPoison response too short: {} bytes", p.Length());
		return;
	}

	uint32_t inventory_slot = p.GetUInt32(2);
	uint32_t success = p.GetUInt32(6);

	LOG_DEBUG(MOD_ENTITY, "ApplyPoison response: slot={}, success={}", inventory_slot, success);

	if (success) {
		AddChatSystemMessage("You apply the poison to your weapon.");
	} else {
		AddChatSystemMessage("You failed to apply the poison.");
	}
}

void EverQuest::ZoneProcessBindWound(const EQ::Net::Packet &p)
{
	// Server reply to our OP_Bind_Wound. BindWound_Struct: to@0, unknown2@2,
	// type@4, unknown6@6 -- +2 for the opcode = 10 bytes. `type` is the outcome;
	// see EQT::bindWoundOutcomeMessage (bind_wound_outcome.h) for the mapping and
	// the eqemu authority. type 0/3 are control ticks with no message to show.
	if (p.Length() < 10) {
		LOG_WARN(MOD_ENTITY, "BindWound response too short: {} bytes", p.Length());
		return;
	}

	const uint16_t type = p.GetUInt16(6);
	const std::string message = EQT::bindWoundOutcomeMessage(type);
	LOG_DEBUG(MOD_ENTITY, "BindWound response: type={} -> '{}'", type, message);

	if (!message.empty()) {
		AddChatSystemMessage(message);
	}
}

void EverQuest::ZoneProcessTrack(const EQ::Net::Packet &p)
{
	// Track response contains variable number of Track_Struct entries
	// Each entry is 8 bytes: entityid (4) + distance (4)
	// Plus 2 bytes for opcode = minimum 2 bytes

	if (p.Length() < 2) {
		LOG_WARN(MOD_ENTITY, "Track response too short: {} bytes", p.Length());
		return;
	}

	size_t data_len = p.Length() - 2;  // Subtract opcode
	size_t entry_count = data_len / sizeof(EQT::Track_Struct);

	LOG_DEBUG(MOD_ENTITY, "Track response: {} bytes, {} entries", p.Length(), entry_count);

	if (entry_count == 0) {
		AddChatSystemMessage("No trackable entities nearby.");
		return;
	}

	AddChatSystemMessage(fmt::format("=== Tracking ({} targets) ===", entry_count));

	size_t offset = 2;  // Skip opcode
	for (size_t i = 0; i < entry_count && offset + sizeof(EQT::Track_Struct) <= p.Length(); i++) {
		uint32_t entity_id = p.GetUInt32(offset);
		float distance = p.GetFloat(offset + 4);
		offset += sizeof(EQT::Track_Struct);

		// Look up entity name
		std::string name = "Unknown";
		auto it = m_entities.find(static_cast<uint16_t>(entity_id));
		if (it != m_entities.end()) {
			name = it->second.name;
		}

		AddChatSystemMessage(fmt::format("  {} - {:.0f} units", name, distance));
		LOG_DEBUG(MOD_ENTITY, "  Track entry: entity_id={}, name={}, distance={:.1f}",
			entity_id, name, distance);
	}
}

// ============================================================================
// Cluster 6: Emote/AFK/Anon/RP/Camp/Logout
// ============================================================================

void EverQuest::PerformEmote(uint32_t animation)
{
	if (!IsFullyZonedIn()) {
		return;
	}

	// Send the emote animation using the proper Animation packet
	SendAnimation(static_cast<uint8_t>(animation));
	
	// Some emotes should stop movement
	if (animation == ANIM_KNEEL || animation == ANIM_DEATH) {
		if (m_is_moving) {
			StopMovement();
		}
	}
	
	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Performing emote animation {}", animation);
	}
}

void EverQuest::SetAFK(bool afk)
{
	if (m_is_afk == afk) {
		return;
	}

	m_is_afk = afk;
	m_game_state.player().setAFK(afk);  // Phase 7.8
	SendSpawnAppearance(AT_AFK, afk ? 1 : 0);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "AFK status: {}", afk ? "ON" : "OFF");
	}
}

void EverQuest::SetAnonymous(bool anon)
{
	if (m_is_anonymous == anon) {
		return;
	}

	m_is_anonymous = anon;
	m_game_state.player().setAnonymous(anon);  // Phase 7.8
	SendSpawnAppearance(AT_ANONYMOUS, anon ? 1 : 0);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Anonymous status: {}", anon ? "ON" : "OFF");
	}
}

void EverQuest::SetRoleplay(bool rp)
{
	if (m_is_roleplay == rp) {
		return;
	}

	m_is_roleplay = rp;
	m_game_state.player().setRoleplay(rp);  // Phase 7.8
	SendSpawnAppearance(AT_ANONYMOUS, rp ? 2 : 0);  // 2 = roleplay

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Roleplay status: {}", rp ? "ON" : "OFF");
	}
}

void EverQuest::StartCampTimer()
{
	if (m_is_camping) {
		return;  // Already camping
	}

	m_is_camping = true;
	m_camp_start_time = std::chrono::steady_clock::now();
	m_game_state.player().setCamping(true);  // Phase 7.8

	// Send OP_Camp to server to notify we're camping
	SendCamp();

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Camp timer started, will log out in {} seconds", CAMP_TIMER_SECONDS);
	}
}

void EverQuest::CancelCamp()
{
	if (!m_is_camping) {
		return;
	}

	m_is_camping = false;
	m_game_state.player().setCamping(false);  // Phase 7.8
	AddChatSystemMessage("You are no longer camping.");

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Camp timer cancelled");
	}
}

void EverQuest::UpdateCampTimer()
{
	if (!m_is_camping) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_camp_start_time).count();

	if (elapsed >= CAMP_TIMER_SECONDS) {
		// Camp timer complete, send logout to server
		m_is_camping = false;
		m_game_state.player().setCamping(false);  // Phase 7.8
		AddChatSystemMessage("You have camped.");

		// Send OP_Logout to server - server will respond with OP_LogoutReply
		SendLogout();

		if (s_debug_level >= 1) {
			LOG_DEBUG(MOD_MAIN, "Camp timer complete, sending logout request");
		}
	}
}

void EverQuest::SendCamp()
{
	// OP_Camp is an empty packet -- body-only convention: QueuePacket(opcode,
	// pkt) prepends the opcode itself (eq.cpp), so writing it into the body
	// here too made this a 4-byte packet where the server expects 2.
	EQ::Net::DynamicPacket packet;
	QueuePacket(HC_OP_Camp, &packet);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Camp to server");
	}
}

void EverQuest::SendLogout()
{
	// OP_Logout is an empty packet -- body-only convention, see SendCamp().
	EQ::Net::DynamicPacket packet;
	QueuePacket(HC_OP_Logout, &packet);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Logout to server");
	}
}

void EverQuest::ZoneProcessLogoutReply(const EQ::Net::Packet &p)
{
	// Server confirmed logout - now we can safely disconnect
	LOG_INFO(MOD_MAIN, "Logout confirmed by server");

	// D20c4: Signal quit via flag — Application checks and exits
	RequestQuit();

	// H03: Don't call DisconnectFromZone() here — this handler runs from inside
	// DaybreakConnectionManager::Tick()'s packet-processing loop (Tick -> ProcessPacket
	// -> ZoneOnPacketRecv -> DispatchZonePacket -> here). DisconnectFromZone() resets
	// m_zone_connection_manager, destroying the very object whose Tick() is still on
	// the call stack; the next loop iteration then dereferences a dangling m_transport
	// (use-after-free crash — reproduced via GM fast-camp, which triggers an
	// almost-immediate OP_LogoutReply). Defer to UpdateMovement(), which — like the
	// existing m_zone_change_approved deferral — runs outside any callback context.
	m_pending_logout_disconnect = true;
}

// ============================================================================
// Cluster 7: Rezz / WhoAll / Inspect
// ============================================================================

void EverQuest::ZoneProcessRezzRequest(const EQ::Net::Packet &p)
{
	// Server is offering a resurrection
	if (p.Length() < 2 + sizeof(EQT::Resurrect_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_RezzRequest packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::Resurrect_Struct* rez = reinterpret_cast<const EQT::Resurrect_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Store the pending rez offer
	m_has_pending_rezz = true;
	m_pending_rezz = *rez;

	// Display the rez offer in chat
	std::string rezzer_name(rez->rezzer_name, strnlen(rez->rezzer_name, 64));
	std::string corpse_name(rez->corpse_name, strnlen(rez->corpse_name, 64));

	LOG_INFO(MOD_MAIN, "Resurrection offer from {} for corpse '{}' (spell {})",
		rezzer_name, corpse_name, rez->spellid);

	AddChatSystemMessage(fmt::format("{} is offering to resurrect you.", rezzer_name));
	AddChatSystemMessage("Type /accept to accept or /decline to decline the resurrection.");

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Rez location: zone={}, instance={}, pos=({}, {}, {})",
			rez->zone_id, rez->instance_id, rez->x, rez->y, rez->z);
	}
}

void EverQuest::ZoneProcessRezzComplete(const EQ::Net::Packet &p)
{
	// Resurrection is complete
	m_has_pending_rezz = false;
	memset(&m_pending_rezz, 0, sizeof(m_pending_rezz));

	LOG_INFO(MOD_MAIN, "Resurrection complete");
	AddChatSystemMessage("You have been resurrected!");

	// The server will handle the zone change if needed via OP_ZoneChange
}

void EverQuest::SendRezzAnswer(bool accept)
{
	if (!m_has_pending_rezz) {
		AddChatSystemMessage("You don't have a pending resurrection offer.");
		return;
	}

	// Copy the pending rez struct and set the action
	EQT::Resurrect_Struct rez = m_pending_rezz;
	rez.action = accept ? 1 : 0;

	// Build and send the packet. T134: this used to build its own
	// opcode-prefixed packet (Resize(2+struct); PutUInt16(0, opcode);
	// memcpy at +2) and THEN call EverQuest::QueuePacket(opcode, &packet),
	// which itself prepends another 2-byte opcode -- the same
	// double-opcode-prefix bug T133 found and fixed in eq_guild.cpp's
	// senders (confirmed via the eqemu zone log's "Wrong size" complaint).
	// Fixed by building a body-only packet and calling QueuePacket once.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::Resurrect_Struct));
	packet.PutData(0, &rez, sizeof(EQT::Resurrect_Struct));
	QueuePacket(HC_OP_RezzAnswer, &packet);

	if (accept) {
		LOG_INFO(MOD_MAIN, "Accepted resurrection offer");
		AddChatSystemMessage("You have accepted the resurrection.");
	} else {
		LOG_INFO(MOD_MAIN, "Declined resurrection offer");
		AddChatSystemMessage("You have declined the resurrection.");
		// Clear pending rez on decline
		m_has_pending_rezz = false;
		memset(&m_pending_rezz, 0, sizeof(m_pending_rezz));
	}
}

void EverQuest::SendWhoAllRequest(const std::string& name, int lvllow, int lvlhigh,
	int race, int class_, bool gm)
{
	EQT::Who_All_Struct who;
	memset(&who, 0, sizeof(who));

	// Copy name filter (up to 63 chars + null)
	if (!name.empty()) {
		strncpy(who.whom, name.c_str(), 63);
		who.whom[63] = '\0';
	}

	// Set filters (-1 means no filter, use 0xFFFFFFFF)
	who.wrace = (race < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(race);
	who.wclass = (class_ < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(class_);
	who.lvllow = (lvllow < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(lvllow);
	who.lvlhigh = (lvlhigh < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(lvlhigh);
	who.gmlookup = gm ? 1 : 0xFFFFFFFF;

	// Build and send the packet
	// Body-only: QueuePacket(opcode, pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::Who_All_Struct));
	packet.PutData(0, &who, sizeof(EQT::Who_All_Struct));
	QueuePacket(HC_OP_WhoAllRequest, &packet);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_WhoAllRequest: name='{}', race={}, class={}, lvl={}-{}",
			name, race, class_, lvllow, lvlhigh);
	}
}

void EverQuest::ZoneProcessWhoAllResponse(const EQ::Net::Packet &p)
{
	// 64-byte WhoAllReturnHeader followed by `playercount` packed
	// variable-length entries (layout documented in client/who_response.h).
	// T224: before this the entries were never parsed and /who printed only
	// the count line.
	if (p.Length() < 2 + sizeof(EQT::WhoAllReturnHeader)) {
		LOG_WARN(MOD_MAIN, "OP_WhoAllResponse packet too small: {} bytes", p.Length());
		return;
	}

	const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;
	const size_t data_len = p.Length() - 2;
	const EQT::WhoAllReturnHeader* header = reinterpret_cast<const EQT::WhoAllReturnHeader*>(data);

	const uint32_t player_count = header->playercount;
	LOG_INFO(MOD_MAIN, "Who response: {} player(s)", player_count);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Who header: id={}, playerineqstring={}, playersinzonestring={}, playercount={}",
			header->id, header->playerineqstring, header->playersinzonestring, header->playercount);
	}

	// String-table hooks: the client's eqstr db first, the built-in copies
	// of the who strings when it has no entry (unloaded db, headless tests).
	EQT::WhoFormatContext ctx;
	ctx.eqstr = [this](uint32_t id) {
		std::string s = m_string_db.getString(id);
		return s.empty() ? EQT::whoDefaultString(id) : s;
	};
	ctx.className = [](uint32_t c) { return EverQuest::GetClassName(c); };
	ctx.raceName = [](uint32_t r) { return EverQuest::GetRaceName(r); };
	ctx.zoneName = [](uint32_t z) {
		return EQT::ZoneLines::zoneShortNameFromId(static_cast<uint16_t>(z));
	};

	// Line 1: "Players in EverQuest:" (5001). World's friends-who sends
	// 0xFFFFFFFF here and an empty rule line -- both are then skipped.
	if (header->playerineqstring != 0 && header->playerineqstring != EQT::kWhoStringNone) {
		std::string title = ctx.eqstr(header->playerineqstring);
		if (!title.empty()) {
			AddChatSystemMessage(title);
		}
	}

	// Line 2: the rule ("---------------------------").
	if (header->line[0] != '\0') {
		AddChatSystemMessage(std::string(header->line, strnlen(header->line, sizeof(header->line))));
	}

	// Entries.
	std::vector<EQT::WhoAllEntry> entries;
	const uint8_t* body = data + sizeof(EQT::WhoAllReturnHeader);
	const size_t body_len = data_len - sizeof(EQT::WhoAllReturnHeader);
	if (!EQT::parseWhoAllEntries(body, body_len, player_count, entries)) {
		LOG_WARN(MOD_MAIN, "OP_WhoAllResponse: header claims {} entries, only {} parsed from {} bytes",
			player_count, entries.size(), body_len);
	}
	for (const auto& e : entries) {
		// T256 live leg: world's SendWhoAll counts an anon player whose admin
		// level equals the viewer's (a GM /who-ing itself while /anon) but
		// then skips writing that entry, so the count says N and the packet
		// carries 49 zeroed bytes in its place. A zero format string is never
		// a real entry (5022..5025 on the wire); rendering it produced a
		// phantom "[0 Class0] (Race0)" line.
		if (e.formatString == 0) {
			continue;
		}
		AddChatSystemMessage(EQT::formatWhoAllEntry(e, ctx));
	}

	// Last line: the count string (5028 / 5036 / 5029). T178: eqemu's string
	// carries a `%1` placeholder for the count; fill it rather than appending
	// a raw ": N". Fall back to appending only when the string had no
	// placeholder, so an unexpected string never drops the number.
	std::string count_text;
	if (header->playersinzonestring != 0 && header->playersinzonestring != EQT::kWhoStringNone) {
		count_text = ctx.eqstr(header->playersinzonestring);
	}
	if (count_text.empty()) {
		count_text = EQT::whoDefaultString(player_count == 0 ? EQT::kWhoStrNoPlayers
		                                  : player_count == 1 ? EQT::kWhoStrOnePlayer
		                                                      : EQT::kWhoStrManyPlayers);
	}
	bool substituted = false;
	std::string filled = EQT::substituteWhoCount(count_text, player_count, substituted);
	if (substituted || player_count == 0) {
		AddChatSystemMessage(filled);
	} else {
		AddChatSystemMessage(fmt::format("{}: {}", filled, player_count));
	}
}

void EverQuest::SendInspectRequest(uint32_t target_id)
{
	// Build the inspect request
	EQT::Inspect_Struct inspect;
	inspect.TargetID = target_id;
	inspect.PlayerID = m_my_spawn_id;

	// Build and send the packet
	// Body-only: QueuePacket(opcode, pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::Inspect_Struct));
	packet.PutData(0, &inspect, sizeof(EQT::Inspect_Struct));
	QueuePacket(HC_OP_InspectRequest, &packet);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_InspectRequest for target {}", target_id);
	}
}

// Answers an inspect request aimed at us.
//
// ⚠ THIS WRITES TO THE CHARACTER'S SERVER-SIDE INSPECT MESSAGE, and there is
// no way to avoid it. Handle_OP_InspectAnswer (zone/client_packet.cpp:9045-
// 9049) memcpy()s our `text` field over the character's inspect message and
// calls SaveCharacterInspectMessage() unconditionally, on every answer. The
// packet is length-checked EXACT, so the field cannot be omitted.
//
// We send it EMPTY, because this client genuinely does not know the value:
// eqemu loads the inspect message at zone-in (client_packet.cpp:1367) and
// NEVER sends it to a Titanium client -- the real client owns the text, in its
// own inspect window. So a character who set an inspect message elsewhere will
// have it cleared the first time a WillEQ session is inspected.
//
// That is a real, if small, loss, and it is accepted deliberately: the
// alternative is not answering at all, which leaves inspect completely broken
// between two WillEQ clients. The fix is a client-side inspect message the
// user can set and this sender can echo -- recorded as the follow-up in
// refactor/T170_inspect_answer.md, NOT done here, because a setter with no way
// to reach it is the trap T168 just finished removing.
void EverQuest::SendInspectAnswer(uint32_t requester_id)
{
	EQT::InspectResponse_Struct answer =  // non-const: PutData takes void*
		EQT::MakeInspectAnswer(requester_id, m_my_spawn_id, m_inspect_message);

	// T170 test seam, same shape as T88's and T167's: capture the body before
	// the connection gate so a test can assert on the real encoder output.
	m_test_last_inspect_answer.sent = true;
	m_test_last_inspect_answer.target_id = answer.TargetID;
	m_test_last_inspect_answer.playerid = answer.playerid;

	// Body-only: QueuePacket(opcode, pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::InspectResponse_Struct));
	packet.PutData(0, &answer, sizeof(EQT::InspectResponse_Struct));
	QueuePacket(HC_OP_InspectAnswer, &packet);

	LOG_DEBUG(MOD_MAIN, "Answered inspect request from entity {}", requester_id);
}

void EverQuest::ZoneProcessInspectRequest(const EQ::Net::Packet &p)
{
	// Someone is inspecting us
	if (p.Length() < 2 + sizeof(EQT::Inspect_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_InspectRequest packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::Inspect_Struct* inspect = reinterpret_cast<const EQT::Inspect_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Find the inspector's name
	std::string inspector_name = "Someone";
	auto it = m_entities.find(static_cast<uint16_t>(inspect->PlayerID));
	if (it != m_entities.end()) {
		inspector_name = it->second.name;
	}

	LOG_INFO(MOD_MAIN, "{} is inspecting you", inspector_name);
	AddChatSystemMessage(fmt::format("{} is inspecting you.", inspector_name));

	// T170: ANSWER IT. This is the half that was missing. eqemu forwards the
	// request to us precisely because a Titanium client is expected to reply
	// (Handle_OP_InspectRequest, client_packet.cpp:9084-9087 -- only SoF+
	// clients and bots get a server-built answer). Without this reply the
	// inspector's window stays empty forever, and no single-client test could
	// ever have shown it: the request and the empty window are on two
	// different machines.
	SendInspectAnswer(inspect->PlayerID);
}

void EverQuest::ZoneProcessInspectAnswer(const EQ::Net::Packet &p)
{
	// Received inspection data about someone
	if (p.Length() < 2 + sizeof(EQT::InspectResponse_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_InspectAnswer packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::InspectResponse_Struct* response = reinterpret_cast<const EQT::InspectResponse_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// T170: the inspected player is `playerid`, NOT TargetID. In an ANSWER,
	// TargetID is the ROUTING field -- the requester, i.e. US, since we only
	// receive answers we asked for. Reading it here named the inspecting
	// player (ourselves) as the inspected one. eqemu's own builder settles it:
	// Client::ProcessInspectRequest, zone/client.cpp:6590-6591.
	std::string target_name = "Unknown";
	auto it = m_entities.find(static_cast<uint16_t>(response->playerid));
	if (it != m_entities.end()) {
		target_name = it->second.name;
	}

	LOG_INFO(MOD_MAIN, "Received inspect data for {}", target_name);
	AddChatSystemMessage(fmt::format("Inspecting {}:", target_name));

	// Display equipped items
	for (int i = 0; i < 22; i++) {
		if (response->itemnames[i][0] != '\0') {
			std::string item_name(response->itemnames[i], strnlen(response->itemnames[i], 64));
			AddChatSystemMessage(fmt::format("  Slot {}: {}", i, item_name));
		}
	}

	// Display inspect message if present
	if (response->text[0] != '\0') {
		std::string message(response->text, strnlen(response->text, 288));
		AddChatSystemMessage(fmt::format("Message: {}", message));
	}
}

// ============================================================================
// Cluster 8: Corpse / Consent
// ============================================================================

void EverQuest::SendCorpseDrag(const std::string& corpse_name)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_CorpseDrag);

	EQT::CorpseDrag_Struct drag;
	memset(&drag, 0, sizeof(drag));
	strncpy(drag.CorpseName, corpse_name.c_str(), 63);
	strncpy(drag.DraggerName, m_character.c_str(), 63);
	packet.PutData(2, &drag, sizeof(drag));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		m_is_dragging_corpse = true;
		m_dragged_corpse_name = corpse_name;
		LOG_INFO(MOD_MAIN, "Started dragging corpse: {}", corpse_name);
		AddChatSystemMessage(fmt::format("You begin to drag {}.", corpse_name));
	}
}

void EverQuest::SendCorpseDrop()
{
	if (!m_is_dragging_corpse) {
		AddChatSystemMessage("You are not dragging a corpse.");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_CorpseDrop);

	EQT::CorpseDrag_Struct drop;
	memset(&drop, 0, sizeof(drop));
	strncpy(drop.CorpseName, m_dragged_corpse_name.c_str(), 63);
	strncpy(drop.DraggerName, m_character.c_str(), 63);
	packet.PutData(2, &drop, sizeof(drop));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Dropped corpse: {}", m_dragged_corpse_name);
		AddChatSystemMessage(fmt::format("You stop dragging {}.", m_dragged_corpse_name));
		m_is_dragging_corpse = false;
		m_dragged_corpse_name.clear();
	}
}

void EverQuest::SendConsiderCorpse(uint32_t corpse_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_ConsiderCorpse);

	// T153: this used to write a bare 4-byte corpse id, so the packet was
	// dropped before the handler ever ran. eqemu's Handle_OP_ConsiderCorpse
	// (zone/client_packet.cpp:5245) gates on
	// `app->size != sizeof(Consider_Struct)` -- 28 bytes -- and returns via a
	// LogDebug with no error path, so the failure was silent on both ends.
	// titanium.cpp has no ENCODE/DECODE for OP_Consider, and
	// Titanium::structs::Consider_Struct (common/patches/titanium_structs.h:1160)
	// is byte-identical to the base common/eq_packet_structs.h:1351 layout, so
	// the 28-byte body goes raw. The handler reads only `targetid`; every other
	// field is server-filled on the reply, which is why the live
	// CombatManager::ConsiderTarget() path sends them zeroed too -- this is the
	// same body, not an invented one.
	EQT::Consider_Struct con;
	memset(&con, 0, sizeof(con));
	con.playerid = GetEntityID();
	con.targetid = corpse_id;
	packet.PutData(2, &con, sizeof(con));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Considering corpse ID: {}", corpse_id);
	}
}

// ============================================================================
// Phase 3: Consent System
// ============================================================================

void EverQuest::SendConsent(const std::string& player_name)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Consent);
	packet.PutString(2, player_name);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Granting corpse consent to: {}", player_name);
		AddChatSystemMessage(fmt::format("You have granted {} permission to drag your corpse.", player_name));
	}
}

void EverQuest::SendConsentDeny(const std::string& player_name)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_ConsentDeny);
	packet.PutString(2, player_name);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Denying corpse consent for: {}", player_name);
		AddChatSystemMessage(fmt::format("You have denied {} permission to drag your corpse.", player_name));
	}
}

void EverQuest::ZoneProcessConsentResponse(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::ConsentResponse_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_ConsentResponse packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::ConsentResponse_Struct* consent = reinterpret_cast<const EQT::ConsentResponse_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string grantname(consent->grantname, strnlen(consent->grantname, 64));
	std::string ownername(consent->ownername, strnlen(consent->ownername, 64));
	std::string zonename(consent->zonename, strnlen(consent->zonename, 32));

	LOG_INFO(MOD_MAIN, "Consent granted: {} can now loot {}'s corpse in {}",
		grantname, ownername, zonename);
	AddChatSystemMessage(fmt::format("{} has been given permission to drag your corpse.", grantname));
}

void EverQuest::ZoneProcessDenyResponse(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::ConsentResponse_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_DenyResponse packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::ConsentResponse_Struct* consent = reinterpret_cast<const EQT::ConsentResponse_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string grantname(consent->grantname, strnlen(consent->grantname, 64));

	LOG_INFO(MOD_MAIN, "Consent denied for: {}", grantname);
	AddChatSystemMessage(fmt::format("{}'s permission to drag your corpse has been revoked.", grantname));
}

// ============================================================================
// Cluster 9: Assist/Boat/Split/LFG/Shielding/EnvDamage/Discipline/Banker
// ============================================================================

void EverQuest::SendAssist(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Assist);
	packet.PutUInt32(2, target_id);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Assist request for target ID: {}", target_id);
	}
}

void EverQuest::SendAssistGroup(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_AssistGroup);
	packet.PutUInt32(2, target_id);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Group assist request for target ID: {}", target_id);
	}
}

// ============================================================================
// Phase 3: Travel System
// ============================================================================

// T168: SendBoardBoat / SendLeaveBoat / SendControlBoat were DELETED here.
//
// All three were zero-caller senders, and all three were dead on arrival, not
// merely unwired -- the arc's standing lesson is that a sender nothing ever
// called is UNVERIFIED, not "correct but unreached":
//
//   * SendBoardBoat sent a 4-byte entity id. eqemu's Handle_OP_BoardBoat
//     (zone/client_packet.cpp:4133) requires `5 < size <= 64` and resolves the
//     body as a NAME via entity_list.GetMob(boatname). A 4-byte body is
//     rejected before anything else happens, so boarding could never succeed.
//   * SendLeaveBoat guarded on m_is_on_boat, which only SendBoardBoat ever
//     set, so it could never be reached -- and eqemu's Handle_OP_LeaveBoat
//     ignores the body entirely, so the id it carried was meaningless.
//   * SendControlBoat is the T151/T154 HAZARD: Handle_OP_ControlBoat
//     (:5421) resolves the id through entity_list.GetMob() and any hit that is
//     not a controllable boat writes RecordPlayerEventLog(POSSIBLE_HACK)
//     AGAINST OUR OWN ACCOUNT. Its only id source was the broken boarder.
//
// Keeping them was the trap T147 names: three functions that look like a
// half-built feature but cannot work, with a comment block warning the reader
// off the only one with a body worth reading. The opcodes, ControlBoat_Struct
// and the findings above are kept -- see the HC_OP_BoardBoat annotation in
// eq.h and refactor/T168_sender_triage.md. Boats need a ride/entity model
// first; steering is OP_ClientUpdate with a foreign spawn_id, not a boat
// opcode at all.

// ============================================================================
// Phase 3: Group Split
// ============================================================================

void EverQuest::SendSplit(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper)
{
	if (!m_in_group) {
		AddChatSystemMessage("You are not in a group.");
		return;
	}

	// Check if we have enough money
	uint64_t total_copper = copper + silver * 10 + gold * 100 + platinum * 1000;
	uint64_t my_copper = m_copper + m_silver * 10 + m_gold * 100 + m_platinum * 1000;

	if (total_copper > my_copper) {
		AddChatSystemMessage("You don't have that much money.");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Split);

	EQT::Split_Struct split;
	split.platinum = platinum;
	split.gold = gold;
	split.silver = silver;
	split.copper = copper;
	packet.PutData(2, &split, sizeof(split));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Splitting {}p {}g {}s {}c with group", platinum, gold, silver, copper);
		AddChatSystemMessage(fmt::format("You split {}p {}g {}s {}c with your group.",
			platinum, gold, silver, copper));
	}
}

// ============================================================================
// Phase 3: LFG System
// ============================================================================

void EverQuest::SendLFGCommand(bool lfg_on)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_LFGCommand);
	packet.PutUInt32(2, lfg_on ? 1 : 0);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		m_is_lfg = lfg_on;
		LOG_INFO(MOD_MAIN, "LFG status: {}", lfg_on ? "ON" : "OFF");
		AddChatSystemMessage(fmt::format("You are {} looking for a group.", lfg_on ? "now" : "no longer"));
	}
}

void EverQuest::ZoneProcessLFGAppearance(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::LFG_Appearance_Struct)) {
		return;
	}

	const EQT::LFG_Appearance_Struct* lfg = reinterpret_cast<const EQT::LFG_Appearance_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Update entity's LFG status
	auto it = m_entities.find(static_cast<uint16_t>(lfg->spawn_id));
	if (it != m_entities.end()) {
		// If we had an LFG flag on Entity, we'd update it here
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Entity {} LFG status: {}", lfg->spawn_id, lfg->lfg);
		}
	}

	// Update our own status if it's us
	if (lfg->spawn_id == m_my_spawn_id) {
		m_is_lfg = (lfg->lfg != 0);
	}
}

// ============================================================================
// Phase 3: Combat Abilities
// ============================================================================

void EverQuest::SendShielding(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Shielding);

	EQT::Shielding_Struct shield;
	shield.target_id = target_id;
	packet.PutData(2, &shield, sizeof(shield));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Shielding target ID: {}", target_id);
	}
}

// NOTE (T145): OP_ENVDAMAGE IS CLIENT->SERVER IN EQEMU. It is registered in
// `ConnectedOpcodes` (zone/client_packet.cpp:198) and the server never sends
// it, so this receive path cannot fire against an eqemu server no matter what
// size the struct is. The struct and this guard are now correct against
// eqemu's 31-byte EnvDamage2_Struct (the guard previously demanded 34 bytes
// for a 33-byte packet), but that only means the path would parse correctly
// IF something sent it. This client has no OP_EnvDamage sender at all.
void EverQuest::ZoneProcessEnvDamage(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::EnvDamage2_Struct)) {
		return;
	}

	const EQT::EnvDamage2_Struct* env = reinterpret_cast<const EQT::EnvDamage2_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// eqemu EQ::constants::EnvironmentalDamage (common/emu_constants.h:308)
	std::string damage_type;
	switch (env->dmgtype) {
		case 250: damage_type = "lava"; break;
		case 251: damage_type = "drowning"; break;
		case 252: damage_type = "falling"; break;
		case 253: damage_type = "trap"; break;
		default: damage_type = "environmental"; break;
	}

	if (env->id == m_my_spawn_id) {
		LOG_INFO(MOD_MAIN, "You took {} {} damage!", env->damage, damage_type);
		AddChatSystemMessage(fmt::format("You took {} points of {} damage.", env->damage, damage_type));
	}
}

// ============================================================================
// Phase 3: Discipline System
// ============================================================================

void EverQuest::ZoneProcessDisciplineUpdate(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::Disciplines_Struct)) {
		return;
	}

	const EQT::Disciplines_Struct* disc = reinterpret_cast<const EQT::Disciplines_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Update discipline list in game state if needed
	LOG_DEBUG(MOD_MAIN, "Received discipline update");

	// Count known disciplines
	int count = 0;
	for (uint32_t i = 0; i < EQT::MAX_PP_DISCIPLINES; i++) {
		if (disc->values[i] != 0) {
			count++;
		}
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "You know {} disciplines", count);
	}
}

void EverQuest::ZoneProcessDisciplineTimer(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::DisciplineTimer_Struct)) {
		return;
	}

	const EQT::DisciplineTimer_Struct* timer = reinterpret_cast<const EQT::DisciplineTimer_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Discipline timer {}: {} seconds remaining",
			timer->timer_id, timer->timer_value);
	}
}

// ============================================================================
// Phase 3: Banking
// ============================================================================

void EverQuest::ZoneProcessBankerChange(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::BankerChange_Struct)) {
		return;
	}

	const EQT::BankerChange_Struct* bank = reinterpret_cast<const EQT::BankerChange_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Update bank currency
	m_bank_platinum = bank->platinum;
	m_bank_gold = bank->gold;
	m_bank_silver = bank->silver;
	m_bank_copper = bank->copper;

	// Also update GameState
	m_game_state.player().setBankPlatinum(bank->platinum);
	m_game_state.player().setBankGold(bank->gold);
	m_game_state.player().setBankSilver(bank->silver);
	m_game_state.player().setBankCopper(bank->copper);

	LOG_DEBUG(MOD_MAIN, "Bank updated: {}p {}g {}s {}c",
		m_bank_platinum, m_bank_gold, m_bank_silver, m_bank_copper);
}

// ============================================================================
// Cluster 10: Save/Popup/ClearObject/Duel/BindWound/Track
// ============================================================================

void EverQuest::SendSave()
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Save);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Character save requested");
		AddChatSystemMessage("Saving character...");
	}
}

// T278: this used to read the first four payload bytes as a world-object
// drop_id and erase m_world_objects[that id]. OP_ClearObject carries no such
// field. eqemu's ClearObject_Struct is { uint8 Clear; uint8 Unknown001[7]; }
// and every one of its three senders sets Clear = 1 -- so the "object id"
// this read was always the literal 1, and any ground spawn that happened to
// have drop_id 1 was silently deleted from the client's world-object map.
//
// What the packet actually means is "the WORLD CONTAINER's contents are
// gone". eqemu queues it from exactly three places, all with an open world
// container: Object::Close() (the container was closed), and the
// world-container branches of Object::HandleCombine() and
// Object::HandleAugmentation() after they Clear() the container and delete
// its row. It is NOT a close notification -- after a combine the window
// stays open with empty slots, which is why the active container id is left
// alone here.
//
// So the correct response is to empty the world container slots
// (4000-4009). InventoryManager::clearWorldContainerSlots() already existed
// for this and had ZERO callers; this is its caller.
void EverQuest::ZoneProcessClearObject(const EQ::Net::Packet &p)
{
	(void)p;  // ClearObject_Struct carries only a "did something" flag

	LOG_DEBUG(MOD_MAIN, "ClearObject: emptying world container slots (active container {})",
		m_active_tradeskill_object_id);

	if (!m_inventory_manager) {
		return;
	}
	m_inventory_manager->clearWorldContainerSlots();

	// The UI holds its own copy of each slot, so an emptied slot has to be
	// announced the same way a deleted item is (D18a: bridge-only).
	for (int16_t slot = eqt::inventory::WORLD_BEGIN; slot <= eqt::inventory::WORLD_END; slot++) {
		SyncInventorySlotToGameState(slot, false);
		if (!m_bridge) {
			continue;
		}
		eqt::state::InventorySlotChangedData data;
		data.slotId = slot;
		data.hasItem = false;
		data.itemId = 0;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	}
}

// ============================================================================
// Phase 4: Dueling System
// ============================================================================

void EverQuest::SendDuelRequest(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RequestDuel);

	EQT::Duel_Struct duel;
	duel.duel_initiator = m_my_spawn_id;
	duel.duel_target = target_id;
	packet.PutData(2, &duel, sizeof(duel));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		m_has_pending_duel = true;
		m_duel_target_id = target_id;
		LOG_INFO(MOD_MAIN, "Sent duel request to target ID: {}", target_id);
		AddChatSystemMessage("You have challenged someone to a duel.");
	}
}

void EverQuest::SendDuelAccept(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_DuelAccept);

	EQT::Duel_Struct duel;
	duel.duel_initiator = target_id;
	duel.duel_target = m_my_spawn_id;
	packet.PutData(2, &duel, sizeof(duel));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		m_is_dueling = true;
		m_has_pending_duel = false;
		m_duel_target_id = target_id;
		LOG_INFO(MOD_MAIN, "Accepted duel from: {}", target_id);
		AddChatSystemMessage("You have accepted the duel!");
	}
}

void EverQuest::SendDuelDecline(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_DuelDecline);

	EQT::DuelResponse_Struct response;
	response.target_id = m_my_spawn_id;
	response.entity_id = target_id;
	response.unknown = 0;
	packet.PutData(2, &response, sizeof(response));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		m_has_pending_duel = false;
		m_duel_initiator_id = 0;
		LOG_INFO(MOD_MAIN, "Declined duel from: {}", target_id);
		AddChatSystemMessage("You have declined the duel.");
	}
}

void EverQuest::ZoneProcessDuelRequest(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::Duel_Struct)) {
		return;
	}

	const EQT::Duel_Struct* duel = reinterpret_cast<const EQT::Duel_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Someone challenged us to a duel
	if (duel->duel_target == m_my_spawn_id) {
		m_has_pending_duel = true;
		m_duel_initiator_id = duel->duel_initiator;

		// Find challenger name
		std::string challenger = "Someone";
		auto it = m_entities.find(static_cast<uint16_t>(duel->duel_initiator));
		if (it != m_entities.end()) {
			challenger = it->second.name;
		}

		LOG_INFO(MOD_MAIN, "{} has challenged you to a duel!", challenger);
		AddChatSystemMessage(fmt::format("{} has challenged you to a duel! Type /duelaccept or /dueldecline.", challenger));
	}
}

// ============================================================================
// Phase 4: Skills
// ============================================================================

void EverQuest::SendBindWound(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_BindWound);

	EQT::BindWound_Struct bw;
	bw.to = static_cast<uint16_t>(target_id);
	bw.unknown2 = 0;
	bw.type = 0;
	bw.unknown6 = 0;
	packet.PutData(2, &bw, sizeof(bw));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Bind wound on target: {}", target_id);
	}
}

void EverQuest::SendTrackTarget(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_TrackTarget);
	packet.PutUInt32(2, target_id);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Track target: {}", target_id);
	}
}

// ============================================================================
// Cluster 11: Surname/FaceChange/Random/FindPerson + social misc
// ============================================================================

void EverQuest::SendSurname(const std::string& surname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Surname);

	EQT::Surname_Struct sn;
	memset(&sn, 0, sizeof(sn));
	strncpy(sn.name, m_character.c_str(), 63);
	strncpy(sn.lastname, surname.c_str(), 31);
	packet.PutData(2, &sn, sizeof(sn));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Set surname to: {}", surname);
		AddChatSystemMessage(fmt::format("Requesting surname: {}", surname));
	}
}

void EverQuest::SendFaceChange(uint8_t haircolor, uint8_t beardcolor, uint8_t eyecolor1, uint8_t eyecolor2,
	uint8_t hairstyle, uint8_t beard, uint8_t face)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_FaceChange);

	EQT::FaceChange_Struct fc;
	fc.haircolor = haircolor;
	fc.beardcolor = beardcolor;
	fc.eyecolor1 = eyecolor1;
	fc.eyecolor2 = eyecolor2;
	fc.hairstyle = hairstyle;
	fc.beard = beard;
	fc.face = face;
	packet.PutData(2, &fc, sizeof(fc));

	// T167 test seam, mirroring SendPositionUpdate()'s: capture the body we
	// built BEFORE the connection gate, so a test can assert on the real
	// encoder output without a live connection. See Test_GetLastFaceChange().
	m_test_last_face_change.sent = true;
	m_test_last_face_change.body.haircolor = haircolor;
	m_test_last_face_change.body.beardcolor = beardcolor;
	m_test_last_face_change.body.eyecolor1 = eyecolor1;
	m_test_last_face_change.body.eyecolor2 = eyecolor2;
	m_test_last_face_change.body.hairstyle = hairstyle;
	m_test_last_face_change.body.beard = beard;
	m_test_last_face_change.body.face = face;
	m_test_last_face_change.body.valid = true;

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Face change requested");
	}
}

// ============================================================================
// Phase 4: Misc
// ============================================================================

void EverQuest::SendRandom(uint32_t low, uint32_t high)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RandomReq);

	EQT::RandomReq_Struct rnd;
	rnd.low = low;
	rnd.high = high;
	packet.PutData(2, &rnd, sizeof(rnd));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Random {} to {}", low, high);
	}
}

void EverQuest::ZoneProcessRandomReply(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::RandomReply_Struct)) {
		return;
	}

	const EQT::RandomReply_Struct* rnd = reinterpret_cast<const EQT::RandomReply_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string name(rnd->name, strnlen(rnd->name, 64));
	LOG_INFO(MOD_MAIN, "{} rolled {} ({} to {})", name, rnd->result, rnd->low, rnd->high);
	AddChatSystemMessage(fmt::format("**A Magic Die is rolled by {}. It could have been {} to {}. It is {}.",
		name, rnd->low, rnd->high, rnd->result));
}

void EverQuest::SendFindPerson(uint32_t npc_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_FindPersonRequest);

	EQT::FindPersonRequest_Struct fp;
	fp.npc_id = npc_id;
	fp.client_pos.x = m_x;
	fp.client_pos.y = m_y;
	fp.client_pos.z = m_z;
	packet.PutData(2, &fp, sizeof(fp));

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Find person request for NPC: {}", npc_id);
	}
}

void EverQuest::ZoneProcessFindPersonReply(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::FindPerson_Point)) {
		return;
	}

	const EQT::FindPerson_Point* dest = reinterpret_cast<const EQT::FindPerson_Point*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	LOG_INFO(MOD_MAIN, "Find person result: destination at ({}, {}, {})", dest->x, dest->y, dest->z);
	AddChatSystemMessage(fmt::format("Path destination: {:.1f}, {:.1f}, {:.1f}", dest->x, dest->y, dest->z));
}

void EverQuest::SendRewind()
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Rewind);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Rewind requested");
		AddChatSystemMessage("Attempting to rewind to a safe position...");
	}
}

void EverQuest::SendYellForHelp()
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_YellForHelp);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Yelling for help!");
		AddChatSystemMessage("You yell for help!");
	}
}

void EverQuest::SendReport(const std::string& report_text)
{
	// Simplified report - just send the text
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Report);
	packet.PutUInt32(2, 0);  // category_id
	// For simplicity, we'll just send minimal data
	packet.PutString(6, report_text);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Report submitted");
		AddChatSystemMessage("Your report has been submitted.");
	}
}

// T150: payload corrected against eqemu source while wiring the first caller.
// zone/client_packet.cpp's Handle_OP_FriendsWho does:
//     char *FriendsString = (char*)app->pBuffer;  FriendsWho(FriendsString);
// i.e. the ENTIRE application payload is one NUL-terminated, comma-delimited
// list of character names -- there is no struct and no Titanium encoder for
// this opcode (grep FriendsWho over common/patches finds only the opcode name).
// world/clientlist.cpp's ClientList::SendFriendsWho() then strchr()s that
// string for ',' and FindCharacter()s each name; the requester's spawn id and
// name are taken server-side from ServerFriendsWho_Struct::FromID/FromName
// (filled by the ZONE from GetID()/GetName()), never from the client payload.
// The previous form wrote a uint32 spawn id at offset 2 and the string at 6,
// so the first "name" the server parsed began with four raw binary bytes and
// could never match a character. It had zero callers, so nothing regressed.
void EverQuest::SendFriendsWho(const std::string& friends_list)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_FriendsWho);
	// PutCString, not PutString: the server strchr()s this buffer, so the
	// NUL terminator is load-bearing (Packet::PutString does not write one).
	packet.PutCString(2, friends_list.c_str());

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Friends who requested for: {}", friends_list);
	}
}

// ============================================================================
// Cluster 12: CameraEffect / PlayMP3 / Sound
// ============================================================================

void EverQuest::ZoneProcessCameraEffect(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::Camera_Struct)) {
		return;
	}

	const EQT::Camera_Struct* cam = reinterpret_cast<const EQT::Camera_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	LOG_DEBUG(MOD_MAIN, "Camera effect: duration={}ms, intensity={}", cam->duration, cam->intensity);
	// Could trigger camera shake in renderer if implemented
}

void EverQuest::ZoneProcessPlayMP3(const EQ::Net::Packet &p)
{
	if (p.Length() < 4) {
		return;
	}

	const char* filename = reinterpret_cast<const char*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string file(filename, strnlen(filename, p.Length() - 2));
	LOG_DEBUG(MOD_AUDIO, "Play MP3 request: {}", file);

#ifdef WITH_AUDIO
	if (m_audio_manager) {
		m_audio_manager->playMusic(file, false);  // Play once, don't loop
	}
#endif
}

void EverQuest::ZoneProcessSound(const EQ::Net::Packet &p)
{
	// Sound packet format (Titanium):
	// Offset 0-1: Opcode
	// Offset 2-5: Sound ID (uint32_t)
	// Offset 6-9: X position (float, optional)
	// Offset 10-13: Y position (float, optional)
	// Offset 14-17: Z position (float, optional)

	if (p.Length() < 6) {  // Minimum: opcode + sound ID
		LOG_DEBUG(MOD_AUDIO, "Sound packet too small: {} bytes", p.Length());
		return;
	}

	const uint8_t* data = static_cast<const uint8_t*>(p.Data());
	uint32_t soundId = *reinterpret_cast<const uint32_t*>(data + 2);

	// Check for position data
	bool hasPosition = (p.Length() >= 18);  // opcode + sound ID + 3 floats
	float x = 0, y = 0, z = 0;
	if (hasPosition) {
		x = *reinterpret_cast<const float*>(data + 6);
		y = *reinterpret_cast<const float*>(data + 10);
		z = *reinterpret_cast<const float*>(data + 14);
	}

	LOG_DEBUG(MOD_AUDIO, "Sound effect: id={} pos=({:.1f}, {:.1f}, {:.1f}) hasPos={}",
		soundId, x, y, z, hasPosition);

#ifdef WITH_AUDIO
	// Don't play sounds during zone loading
	if (m_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
		if (hasPosition) {
			m_audio_manager->playSound(soundId, glm::vec3(x, y, z));
		} else {
			m_audio_manager->playSound(soundId);
		}
	}
#endif
}

