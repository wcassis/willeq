// EverQuest task/DZ, spell-cast, mana, buff and pet-buff handlers, plus
// poison and track.
//
// Clusters 4-5 of eq_player_info.cpp, split off by Batch J unit J07. Moved
// verbatim, no behavior change. See refactor/J07_eq_player_info_split.md and
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

	// Update player mana tracking (T286: PlayerState is the only store)
	m_game_state.player().setCurMana(new_mana);

	// Update our entity if we're tracking it
	if (MySpawnId() != 0) {
		auto it = m_entities.find(MySpawnId());
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

	// Also update endurance tracking (T286: PlayerState is the only store)
	m_game_state.player().setEndurance(stamina, m_game_state.player().maxEndurance());

	// Update combat stats with new mana value
	if (m_combat_manager) {
		CombatStats stats;
		stats.current_hp = m_game_state.player().curHP();
		stats.max_hp = m_game_state.player().maxHP();
		stats.current_mana = new_mana;
		stats.max_mana = m_game_state.player().maxMana();
		stats.current_endurance = stamina;
		stats.max_endurance = 0;
		stats.hp_percent = (m_game_state.player().maxHP() > 0) ? (static_cast<float>(m_game_state.player().curHP()) * 100.0f / static_cast<float>(m_game_state.player().maxHP())) : 100.0f;
		stats.mana_percent = (m_game_state.player().maxMana() > 0) ? (static_cast<float>(new_mana) * 100.0f / static_cast<float>(m_game_state.player().maxMana())) : 100.0f;
		stats.endurance_percent = 100.0f;
		m_combat_manager->UpdateCombatStats(stats);
	}

	// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical note in
	// eq_entities.cpp's HP-change UpdateInventoryStats() call site.
	UpdateInventoryStats();

	// Publish PlayerStatsChanged when mana/endurance changes
	if (m_bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = m_game_state.player().curHP();
		data.maxHP = m_game_state.player().maxHP();
		data.curMana = new_mana;
		data.maxMana = m_game_state.player().maxMana();
		data.curEndurance = stamina;
		data.maxEndurance = m_game_state.player().maxEndurance();
		data.level = m_game_state.player().level();
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
	bool is_self = (entity_id == MySpawnId());

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

	auto bytes = EQ::BuildBuffCancelPacket(HC_OP_Buff, MySpawnId(), slot, *buff);

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
	if (m_game_state.pet().petSpawnId() == 0) {
		LOG_DEBUG(MOD_SPELL, "Received PetBuffWindow but no pet tracked, petid={}", petBuffs->petid);
		return;
	}

	if (petBuffs->petid != m_game_state.pet().petSpawnId()) {
		LOG_DEBUG(MOD_SPELL, "Received PetBuffWindow for different pet: {} (our pet: {})",
			petBuffs->petid, m_game_state.pet().petSpawnId());
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

