// EverQuest task/DZ, spell-cast, mana, buff and pet-buff handlers, plus
// poison and track.
//
// Clusters 4-5 of eq_player_info.cpp, split off by Batch J unit J07. Moved
// verbatim, no behavior change. See refactor/J07_eq_player_info_split.md and
// refactor/F07_player_info.md.

#include "client/player/player_component.h"
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

// ============================================================================
// Cluster 4: Tasks/DZ/BeginCast/ManaChange/Buff/PetBuff
// ============================================================================

void eqt::player::PlayerComponent::ZoneProcessCompletedTasks(const EQ::Net::Packet &p)
{
	// CompletedTasks sends the list of completed tasks
	// This is informational - we can track it if needed
	
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received completed tasks list ({} bytes)", p.Length());
	}
}

void eqt::player::PlayerComponent::ZoneProcessDzCompass(const EQ::Net::Packet &p)
{
	// DzCompass updates the expedition/DZ compass direction
	// Format: heading (4 bytes float), x (4 bytes float), y (4 bytes float), z (4 bytes float)
	
	if (EverQuest::GetDebugLevel() >= 2) {
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

void eqt::player::PlayerComponent::ZoneProcessDzExpeditionLockoutTimers(const EQ::Net::Packet &p)
{
	// DzExpeditionLockoutTimers sends lockout timer information
	// This is informational for expeditions/DZs
	
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received DZ expedition lockout timers ({} bytes)", p.Length());
	}
}

// B1170 (#489): OP_SomeItemPacketMaybe (0x4033). eqgame's message arm builds
// the missile object from the payload (004606f9 je 0x46070f -> 00460737 call
// 0x476f34); SpellManager decodes it and hands it to the renderer.
void eqt::player::PlayerComponent::ZoneProcessSomeItemPacketMaybe(const EQ::Net::Packet &p)
{
	if (p.Length() < 2) return;
	if (ctx_.spell_manager) {
		ctx_.spell_manager->handleMissileObject(
			static_cast<const uint8_t*>(p.Data()) + 2, p.Length() - 2);
	}
}

void eqt::player::PlayerComponent::ZoneProcessBeginCast(const EQ::Net::Packet &p)
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
		auto it = ctx_.entities.find(spawn_id);
		std::string name = (it != ctx_.entities.end()) ? EQT::toDisplayName(it->second.name) : "Unknown";
		LOG_DEBUG(MOD_SPELL, "{} (ID: {}) begins casting spell {} ({}ms)",
			name, spawn_id, spell_id, cast_time);
	}

#ifdef WITH_AUDIO
	// Play spell casting sound at caster's position
	if (ctx_.audio_manager) {
		auto it = ctx_.entities.find(spawn_id);
		if (it != ctx_.entities.end()) {
			ctx_.audio_manager->playSound(EQT::Audio::SoundId::SPELL_CAST,
				glm::vec3(it->second.x, it->second.y, it->second.z));
		}
	}
#endif

	// Notify spell manager about the cast
	if (ctx_.spell_manager) {
		ctx_.spell_manager->handleBeginCast(spawn_id, spell_id, cast_time);
	}

	// Publish SpellCastStarted event to bridge. This is the server-echoed
	// confirmation of BeginCast for *any* caster (self, NPC, other player);
	// GameStateSyncBridge (T44) filters to our own casts by casterId. For
	// our own cast, SpellManager already knows the target it chose at
	// beginCastFromGem() time -- carry it so the bridge
	// doesn't need a second source of truth. targetId is meaningless for
	// non-self casters and left at its default (0).
	//
	// T221: source-side twin of the projection's self-filter -- re-applies
	// the echo for our own cast (castTimeMs may now be the server's, as the
	// projection allowed); the SpellCastStarted projection is redundant.
	if (spawn_id == ctx_.self.GetEntityID()) {
		uint16_t self_target = ctx_.spell_manager ? ctx_.spell_manager->getCurrentTargetId() : 0;
		ctx_.game_state.spells().setCasting(true, spell_id, self_target, cast_time);
	}
	if (ctx_.bridge) {
		eqt::state::SpellCastStartedData data;
		data.casterId = spawn_id;
		data.spellId = spell_id;
		data.castTimeMs = cast_time;
		if (ctx_.spell_manager && spawn_id == ctx_.self.GetEntityID()) {
			data.targetId = ctx_.spell_manager->getCurrentTargetId();
		}
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SpellCastStarted, std::move(data)));
	}
}

void eqt::player::PlayerComponent::ZoneProcessManaChange(const EQ::Net::Packet &p)
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
	ctx_.game_state.player().setCurMana(new_mana);

	// Update our entity if we're tracking it
	if (ctx_.self.MySpawnId() != 0) {
		auto it = ctx_.entities.find(ctx_.self.MySpawnId());
		if (it != ctx_.entities.end()) {
			it->second.cur_mana = static_cast<uint16_t>(std::min(new_mana, static_cast<uint32_t>(65535)));
		}
	}

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_SPELL, "ManaChange: mana={}, stamina={}, spell_id={}, keepcasting={}",
			new_mana, stamina, spell_id, keepcasting);
	}

	// Notify spell manager if available
	if (ctx_.spell_manager) {
		ctx_.spell_manager->handleManaChange(new_mana, stamina, spell_id, keepcasting);
	}

	// Also update endurance tracking (T286: PlayerState is the only store)
	ctx_.game_state.player().setEndurance(stamina, ctx_.game_state.player().maxEndurance());

	// Update combat stats with new mana value
	if (ctx_.combat_manager) {
		CombatStats stats;
		stats.current_hp = ctx_.game_state.player().curHP();
		stats.max_hp = ctx_.game_state.player().maxHP();
		stats.current_mana = new_mana;
		stats.max_mana = ctx_.game_state.player().maxMana();
		stats.current_endurance = stamina;
		stats.max_endurance = 0;
		stats.hp_percent = (ctx_.game_state.player().maxHP() > 0) ? (static_cast<float>(ctx_.game_state.player().curHP()) * 100.0f / static_cast<float>(ctx_.game_state.player().maxHP())) : 100.0f;
		stats.mana_percent = (ctx_.game_state.player().maxMana() > 0) ? (static_cast<float>(new_mana) * 100.0f / static_cast<float>(ctx_.game_state.player().maxMana())) : 100.0f;
		stats.endurance_percent = 100.0f;
		ctx_.combat_manager->UpdateCombatStats(stats);
	}

	// T43: was #ifdef EQT_HAS_GRAPHICS -- see the identical note in
	// eq_entities.cpp's HP-change UpdateInventoryStats() call site.
	ctx_.Bridge().UpdateInventoryStats();

	// Publish PlayerStatsChanged when mana/endurance changes
	if (ctx_.bridge) {
		eqt::state::PlayerStatsChangedData data;
		data.curHP = ctx_.game_state.player().curHP();
		data.maxHP = ctx_.game_state.player().maxHP();
		data.curMana = new_mana;
		data.maxMana = ctx_.game_state.player().maxMana();
		data.curEndurance = stamina;
		data.maxEndurance = ctx_.game_state.player().maxEndurance();
		data.level = ctx_.game_state.player().level();
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}
}

void eqt::player::PlayerComponent::ZoneProcessBuff(const EQ::Net::Packet &p)
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
	bool is_self = (entity_id == ctx_.self.MySpawnId());

	LOG_DEBUG(MOD_SPELL, "Buff: entity={}, spell={}, slot={}, duration={}, fade={}, effect_type={}",
		entity_id, buff.spellid, slot_id, buff.duration, buff_fade, buff.effect_type);

	// Update buff manager
	if (ctx_.buff_manager) {
		if (buff_fade == 1) {
			// Buff is fading
			if (is_self) {
				ctx_.buff_manager->removePlayerBuffBySlot(static_cast<uint8_t>(slot_id));

				// Publish BuffRemoved event to bridge
				if (ctx_.bridge) {
					eqt::state::BuffRemovedData data;
					data.slot = static_cast<uint8_t>(slot_id);
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::BuffRemoved, std::move(data)));
				}
			} else {
				ctx_.buff_manager->removeBuffBySlot(static_cast<uint16_t>(entity_id), static_cast<uint8_t>(slot_id));
			}
		} else if (buff.effect_type == 2 && buff.spellid != 0 && buff.spellid != 0xFFFFFFFF) {
			// Buff is being applied
			if (is_self) {
				ctx_.buff_manager->setPlayerBuff(static_cast<uint8_t>(slot_id), buff);
				PublishPlayerBuffUpdated(static_cast<uint8_t>(slot_id), buff);
			} else {
				ctx_.buff_manager->setEntityBuff(static_cast<uint16_t>(entity_id),
					static_cast<uint8_t>(slot_id), buff);
			}
		}
	}
}

// ===========================================================================
// T348 (refactor/T348_buff_landing_producer.md, board B055): THE PRODUCER.
//
// MEASURED from T345's live session log (review/T345/logs/live_session_vision.log),
// a self-cast of Ultravision (#castspell 46) on eqemu, in wire order:
//
//     [SPELL] ManaChange: mana=0, stamina=243, spell=0
//     [SPELL] SpellAction: caster=287, target=287, spell='Ultravision' (46), effect_flag=0
//     [SPELL] SpellAction: caster=287, target=287, spell='Ultravision' (46), effect_flag=4
//
// and on a RE-cast, the server first fades the copy already up:
//
//     [SPELL] SpellAction: ... effect_flag=0
//     [SPELL] Buff: entity=287, spell=46,         slot=0, duration=0, fade=1, effect_type=2
//     [SPELL] Buff: entity=287, spell=4294967295, slot=0, duration=0, fade=1, effect_type=2
//     [SPELL] SpellAction: ... effect_flag=4
//
// There is NO OP_Buff with fade=0 anywhere in the session: every OP_Buff eqemu
// sends a Titanium client is a FADE (MakeBuffFadePacket), because the only
// sender of bufffade=0 (Client::SendBuffDurationPacket) is gated on
// zone->BuffTimersSuspended(). So ZoneProcessBuff()'s apply branch above --
// the only production caller of setPlayerBuff(), and therefore of
// BuffManager's apply hook -- is unreachable on this server, and T339's
// correctly-registered vision hook never fired in a live session.
//
// The landing is carried by the SECOND OP_Action of the pair, effect_flag == 4.
// That is where the buff list has to change, and that is where this fires --
// matching the official client, which shows the icon the moment the cast lands.
//
// applyBuff() (not setPlayerBuff()) is the entry used because the Action packet
// carries no slot: applyBuff picks a free one, drops any earlier copy of the
// same spell, derives the effect type from the spell data, and fires the apply
// hook. Nothing in BuffManager changes.
//
// Gated on isBuffSpell() -- BENEFICIAL and with a duration. effect_flag == 4 is
// sent for a RESISTED detrimental spell too (resists come on a separate message
// packet, see SpellManager::handleAction), so admitting detrimental spells here
// would post debuffs that never landed. A detrimental landing is still cleaned
// up correctly if it arrives: its fade packet's removeBuffBySlot() is a no-op.
// ===========================================================================
void eqt::player::PlayerComponent::NoteBuffLandedOnPlayer(uint32_t spell_id,
	uint16_t caster_id, uint8_t caster_level)
{
	if (!ctx_.buff_manager || !ctx_.spell_manager) {
		return;
	}
	const EQ::SpellData* spell = ctx_.spell_manager->getSpell(spell_id);
	if (!spell || !spell->isBuffSpell()) {
		return;
	}

	// The Action packet has no duration either; the client computes it the
	// same way the server did, from the formula and the caster's level.
	const uint32_t ticks = spell->calculateDuration(caster_level);

	ctx_.buff_manager->applyBuff(/*target=player*/ 0, spell_id, caster_id,
		caster_level, ticks);

	// applyBuff() chose the slot; read it back so the buff bar and the model
	// agree, then publish through the same body ZoneProcessBuff() uses.
	const EQ::ActiveBuff* landed = ctx_.buff_manager->getPlayerBuff(spell_id);
	if (!landed || landed->slot < 0) {
		return;  // blocked by stacking, or no free slot
	}

	LOG_DEBUG(MOD_SPELL, "Buff landed (OP_Action): spell={} caster={} level={} ticks={} slot={}",
		spell_id, caster_id, caster_level, ticks, static_cast<int>(landed->slot));

	EQT::SpellBuff_Struct wire{};
	wire.effect_type = static_cast<uint8_t>(EQ::BuffEffectType::Buff);
	wire.level = caster_level;
	wire.spellid = spell_id;
	wire.duration = static_cast<int32_t>(ticks == 0xFFFFFFFFu ? -1 : static_cast<int32_t>(ticks));
	wire.counters = 0;
	wire.player_id = caster_id;
	PublishPlayerBuffUpdated(static_cast<uint8_t>(landed->slot), wire);
}

// T277: game -> renderer for ONE player buff slot. Extracted from
// ZoneProcessBuff()'s apply branch so the PlayerProfile's zone-in buffs
// (ZoneProcessPlayerProfile, eq_zone_entry.cpp) can publish the same event
// through the same body -- before this
// they went into BuffManager and stopped there, so the buff bar started
// every session empty no matter what the player was actually holding.
void eqt::player::PlayerComponent::PublishPlayerBuffUpdated(uint8_t slot, const EQT::SpellBuff_Struct& buff)
{
	if (!ctx_.bridge) {
		return;
	}
	eqt::state::BuffUpdatedData data;
	data.slot = slot;
	data.spellId = buff.spellid;
	data.ticksLeft = static_cast<uint32_t>(buff.duration);
	auto caster_it = ctx_.entities.find(static_cast<uint16_t>(buff.player_id));
	if (caster_it != ctx_.entities.end()) {
		data.casterName = EQT::toDisplayName(caster_it->second.name);
	}
	if (ctx_.spell_manager) {
		const auto* spell = ctx_.spell_manager->getSpell(buff.spellid);
		data.spellName = spell ? spell->name : "";
		data.iconId = spell ? spell->spell_icon : 0;
	}
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
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
void eqt::player::PlayerComponent::PublishPlayerBuffs(const EQT::SpellBuff_Struct* buffs, size_t count)
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
void eqt::player::PlayerComponent::RequestCancelBuff(const std::string& args)
{
	if (!ctx_.buff_manager) {
		ctx_.out.System("Buff system not initialized.");
		return;
	}

	std::string arg = args;
	arg.erase(0, arg.find_first_not_of(" \t"));
	size_t end = arg.find_last_not_of(" \t");
	if (end != std::string::npos) arg.erase(end + 1);

	if (arg.empty()) {
		ctx_.out.System("Usage: /removebuff <slot# | spell name>");
		return;
	}

	// All digits -> a slot number, 1-based on the way in.
	if (arg.find_first_not_of("0123456789") == std::string::npos) {
		unsigned long n = std::strtoul(arg.c_str(), nullptr, 10);
		if (n < 1 || n > EQ::BuffManager::MAX_PLAYER_BUFFS) {
			ctx_.out.System(fmt::format("Buff slot must be 1-{}.",
				EQ::BuffManager::MAX_PLAYER_BUFFS));
			return;
		}
		SendBuffCancelRequest(static_cast<uint8_t>(n - 1));
		return;
	}

	if (!ctx_.spell_manager) {
		ctx_.out.System("Spell system not initialized.");
		return;
	}

	std::string needle = arg;
	std::transform(needle.begin(), needle.end(), needle.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	int match = -1;
	int matchCount = 0;
	std::string matchNames;
	for (const EQ::ActiveBuff& b : ctx_.buff_manager->getPlayerBuffs()) {
		if (b.slot < 0) continue;
		const EQ::SpellData* spell = ctx_.spell_manager->getSpell(b.spell_id);
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
		ctx_.out.System(fmt::format("You do not have a buff matching '{}'.", arg));
		return;
	}
	if (matchCount > 1) {
		ctx_.out.System(fmt::format("Multiple buffs match '{}': {}", arg, matchNames));
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
void eqt::player::PlayerComponent::SendBuffCancelRequest(uint8_t slot)
{
	if (!ctx_.buff_manager) {
		LOG_WARN(MOD_SPELL, "No buff manager, cannot cancel buff");
		return;
	}

	// The two things the client can know on its own: a slot that cannot
	// exist, and a slot that holds nothing. Everything else (detrimental,
	// server-pinned) is the server's call.
	if (slot >= EQ::BuffManager::MAX_PLAYER_BUFFS) {
		ctx_.out.System(fmt::format("Buff slot must be 1-{}.",
			EQ::BuffManager::MAX_PLAYER_BUFFS));
		return;
	}

	const EQ::ActiveBuff* buff = ctx_.buff_manager->getPlayerBuffBySlot(slot);
	if (!buff) {
		ctx_.out.System(fmt::format("No buff in slot {}.", slot + 1));
		return;
	}

	if (!ctx_.zone_connection) {
		ctx_.net.NoteSuppressedSend();
		LOG_WARN(MOD_SPELL, "No zone connection, cannot cancel buff in slot {}", slot);
		return;
	}

	auto bytes = EQ::BuildBuffCancelPacket(HC_OP_Buff, ctx_.self.MySpawnId(), slot, *buff);

	EQ::Net::DynamicPacket packet;
	packet.Resize(bytes.size());
	std::memcpy(packet.Data(), bytes.data(), bytes.size());

	std::string name;
	if (ctx_.spell_manager) {
		const EQ::SpellData* spell = ctx_.spell_manager->getSpell(buff->spell_id);
		if (spell) name = spell->name;
	}
	LOG_INFO(MOD_SPELL, "Requesting buff cancel: slot={} spell={} ({})",
		slot, buff->spell_id, name.empty() ? "unknown" : name);

	eqt::strings::DumpPacket("C->S", HC_OP_Buff, packet);
	ctx_.net.SafeQueueZone(packet);
}

void eqt::player::PlayerComponent::ZoneProcessPetBuffWindow(const EQ::Net::Packet &p)
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
	if (ctx_.game_state.pet().petSpawnId() == 0) {
		LOG_DEBUG(MOD_SPELL, "Received PetBuffWindow but no pet tracked, petid={}", petBuffs->petid);
		return;
	}

	if (petBuffs->petid != ctx_.game_state.pet().petSpawnId()) {
		LOG_DEBUG(MOD_SPELL, "Received PetBuffWindow for different pet: {} (our pet: {})",
			petBuffs->petid, ctx_.game_state.pet().petSpawnId());
		return;
	}

	LOG_DEBUG(MOD_SPELL, "PetBuffWindow: petid={} buffcount={}", petBuffs->petid, petBuffs->buffcount);

	// Update buff manager with pet buffs
	if (ctx_.buff_manager) {
		ctx_.buff_manager->setPetBuffs(static_cast<uint16_t>(petBuffs->petid), *petBuffs);
	}
}

// ============================================================================
// Cluster 5: ApplyPoison / Track
// ============================================================================

void eqt::player::PlayerComponent::SendApplyPoison(uint32_t inventory_slot)
{
	if (!ctx_.self.IsFullyZonedIn()) {
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

	ctx_.net.SafeQueueZone(p, 0, true);
}

void eqt::player::PlayerComponent::ZoneProcessApplyPoison(const EQ::Net::Packet &p)
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
		ctx_.out.System("You apply the poison to your weapon.");
	} else {
		ctx_.out.System("You failed to apply the poison.");
	}
}

void eqt::player::PlayerComponent::ZoneProcessBindWound(const EQ::Net::Packet &p)
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
		ctx_.out.System(message);
	}
}

void eqt::player::PlayerComponent::ZoneProcessTrack(const EQ::Net::Packet &p)
{
	// B895 (GitLab #211): the reply is the Tracking Window's feed. It used to
	// be dumped into chat here ("name - N units", plus an invented "No
	// trackable entities nearby."); the official client prints nothing and
	// opens its window instead. The chat form survives as /tracklist.
	if (p.Length() < 2) {
		LOG_WARN(MOD_ENTITY, "Track response too short: {} bytes", p.Length());
		return;
	}
	m_tracking.OnTrackReply(p);
}

