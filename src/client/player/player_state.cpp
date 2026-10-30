#include "client/player/player_component.h"
#include "client/movement/movement_component.h"  // T344: ApplyPositionState
#include "client/entities/entities_component.h"  // T346: SendSpawnAppearance
#include "client/eq.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/corpse_predicate.h"  // T185: player-corpse rename on the death path
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// P28: inventory/chat/hotbar/command-registry are plain data-model headers
// with no renderer dependency (used unconditionally below: m_inventory_manager,
// m_hotbar_model, m_command_registry are complete-type-required here). Only
// hotkey_manager.h (pulls in <irrlicht.h>) and json_config.h/loading_thread.h
// (renderer-only helpers) stay gated.
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/hotbar/hotbar_model.h"
#include "client/graphics/ui/command_registry.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#endif

#include "client/bridge/game_state_bridge.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/loading_thread.h"
#endif
#include "client/bridge/irrlicht_bridge.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_assets.h"
#include "client/audio/door_sounds.h"
#include "client/audio/water_sounds.h"
#endif

#include "common/net/posix_udp_transport.h"
#include "client/animation_constants.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <map>
#include <tuple>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Combat State Sync (Phase 7.6 - sync to CombatState)
// ============================================================================

void eqt::player::PlayerComponent::SetCombatStopDistance(float distance)
{
	ctx_.game_state.combat().setCombatStopDistance(distance);  // T286: only store
}
void eqt::player::PlayerComponent::ZoneProcessPlayerStateAdd(const EQ::Net::Packet &p)
{
	// PlayerStateAdd adds a state/buff to a player
	// This could be buffs, debuffs, or other state changes

	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "PlayerStateAdd packet too small: {} bytes", p.Length());
		return;
	}
	
	// The exact structure depends on the state being added (buff/debuff/state change)
	// Parse what we can from the packet
	if (EverQuest::GetDebugLevel() >= 2 && p.Length() >= 6) {
		uint16_t stateType = p.GetUInt16(2);
		uint16_t stateValue = p.GetUInt16(4);
		LOG_DEBUG(MOD_ZONE, "[PlayerStateAdd] State added: type={:#06x}, value={:#06x}, size={} bytes",
			stateType, stateValue, p.Length());
	}
}

void eqt::player::PlayerComponent::ZoneProcessDeath(const EQ::Net::Packet &p)
{
	// Death packet indicates an entity has died
	// Death_Struct layout (all uint32):
	//   spawn_id, killer_id, corpseid, bindzoneid, spell_id, attack_skill, damage, unknown

	if (p.Length() < 30) {  // Need at least through damage field
		LOG_WARN(MOD_ENTITY, "Death packet too small: {} bytes", p.Length());
		return;
	}

	// Parse death information - Death_Struct uses uint32 fields
	// Offsets include 2-byte opcode prefix
	uint32_t victim_id = p.GetUInt32(2);    // spawn_id - Who died
	uint32_t killer_id = p.GetUInt32(6);    // killer_id - Who killed them
	// uint32_t corpse_id = p.GetUInt32(10);  // corpseid (unused)
	// uint32_t bind_zone = p.GetUInt32(14);  // bindzoneid (unused)
	uint32_t spell_id = p.GetUInt32(18);    // spell_id - Spell that killed (0 if melee)
	// uint32_t attack_skill = p.GetUInt32(22);  // attack_skill (unused)
	uint32_t damage = p.GetUInt32(26);      // damage - Final damage amount
	
	// Get entity names if we have them
	std::string victim_name = "Unknown";
	std::string killer_name = "Unknown";
	
	// Special case: killer_id 0 means the player character killed it
	if (killer_id == 0) {
		killer_id = ctx_.self.MySpawnId();
		killer_name = ctx_.character;
	} else {
		auto killer_it = ctx_.entities.find(killer_id);
		if (killer_it != ctx_.entities.end()) {
			killer_name = killer_it->second.name;
		}
	}
	
	auto victim_it = ctx_.entities.find(victim_id);
	if (victim_it != ctx_.entities.end()) {
		victim_name = victim_it->second.name;
		// Mark entity as dead (0 HP)
		victim_it->second.hp_percent = 0;
		victim_it->second.is_corpse = true;  // Mark as corpse
		// B1183 (#502): the client's death handler writes the corpse stand byte
		// 0x78 (120) on the dying spawn (eqgame.exe.c:105881/111059) -- the
		// local player's own byte already lands here via POS_DEAD, so this is
		// the NPC/other-player half (same value either way).
		victim_it->second.stand_state.apply(eqt::state::StandValue::Corpse);

		// T185: rename a PLAYER corpse ourselves. eqemu broadcasts
		// OP_MobRename for NPC corpses only (NPC::Death, attack.cpp), so
		// without this the entity keeps its living name for the rest of the
		// session while the server calls it "<name>'s corpse<id>" -- and
		// anything reaching for it BY NAME (/target, /consider, /loot, the
		// target window) misses it until the player re-zones. This is not a
		// workaround for a missing packet: Corpse::CalcCorpseName's own
		// comment says "the client does this too, so it's unchangable".
		//
		// The corpse inherits the victim's entity id -- every corpse-creation
		// site is `entity_list.AddCorpse(corpse, GetID())` followed by
		// `SetID(0)` on the dying client -- so victim_id IS the corpse id.
		//
		// NPCs are deliberately left alone: their rename is coming, and it
		// uses a DIFFERENT form ("'s_corpse", apostrophe) than the one
		// CalcCorpseName would give ("`s_corpse", backtick), so synthesizing
		// here would briefly disagree with the server.
		if (victim_it->second.npc_type == 0 &&
		    !ParseCorpseName(victim_it->second.name)) {
			const std::string corpse_name =
				MakeCorpseName(victim_it->second.name, victim_id, /*is_player=*/true);
			LOG_DEBUG(MOD_ENTITY, "Death: renaming player corpse '{}' -> '{}' (entity {})",
			          victim_it->second.name, corpse_name, victim_id);
			victim_it->second.name = corpse_name;
		}
		SyncEntityToGameState(victim_it->second);  // T268: isCorpse/name, as at the MobRename site
#ifdef WITH_AUDIO
		// Play death sound at victim's position
		if (ctx_.audio_manager) {
			ctx_.audio_manager->playSound(EQT::Audio::SoundId::DEATH,
				glm::vec3(victim_it->second.x, victim_it->second.y, victim_it->second.z));
		}
#endif

		// Trigger death animation via bridge (D17b: bridge is sole path to renderer)
		if (ctx_.bridge) {
			eqt::state::EntityDeathAnimationData data;
			data.spawnId = victim_id;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityDeathAnimation, std::move(data)));
		}
	}

	if (EverQuest::GetDebugLevel() >= 1) {
		// spell_id of 0, 0xFFFF (65535), or 0xFFFFFFFF means no spell was used
		bool hasValidSpell = spell_id > 0 && spell_id != 0xFFFF && spell_id != 0xFFFFFFFF;
		if (hasValidSpell) {
			LOG_INFO(MOD_COMBAT, "{} ({}) was killed by {} ({}) for {} damage (spell: {})",
				victim_name, victim_id, killer_name, killer_id, damage, spell_id);
		} else {
			LOG_INFO(MOD_COMBAT, "{} ({}) was killed by {} ({}) for {} damage",
				victim_name, victim_id, killer_name, killer_id, damage);
		}
	}

	// Add combat chat messages for deaths
	std::string victim_display = EQT::toDisplayName(victim_name);
	std::string killer_display = EQT::toDisplayName(killer_name);
	if (killer_id == ctx_.self.MySpawnId()) {
		// Player killed something
		ctx_.out.Combat(fmt::format("You hit {} for {} points of damage.", victim_display, damage), true);
		ctx_.out.Combat(fmt::format("You have slain {}!", victim_display), true);
	} else if (victim_id == ctx_.self.MySpawnId()) {
		// Player was killed
		ctx_.out.Combat(fmt::format("{} hit you for {} points of damage.", killer_display, damage), true);
		ctx_.out.Combat(fmt::format("You have been slain by {}!", killer_display), true);
	} else if (!victim_display.empty() && !killer_display.empty()) {
		// Observing a kill
		ctx_.out.Combat(fmt::format("{} hits {} for {} points of damage.", killer_display, victim_display, damage), false);
		ctx_.out.Combat(fmt::format("{} has been slain by {}!", victim_display, killer_display), false);
	}

	// Track the last entity we killed for SimpleMessage substitution
	// Store the display name now before it gets modified to include "'s corpse"
	if (killer_id == ctx_.self.MySpawnId()) {
		ctx_.game_state.combat().setLastSlainEntityName(EQT::toDisplayName(victim_name));  // Phase 7.6 / T286
	}

	// Notify combat manager if this was our target
	if (ctx_.combat_manager && victim_id == ctx_.combat_manager->GetTargetId()) {
		// Target died - disable auto-attack
		LOG_DEBUG(MOD_COMBAT, "Our combat target {} died, disabling auto-attack", victim_id);
		ctx_.combat_manager->DisableAutoAttack();

		// T33: this is the real, authoritative death signal -- wire the
		// loot state machine here instead of the ProcessCombatRound()
		// hp_percent<=0 poll DisableAutoAttack() above just foreclosed
		// (see refactor/T33_loot_death_event_wiring.md and the deferred.md
		// "Auto-loot on kill has NEVER worked against a real server"
		// finding). Gated internally on auto_loot && auto_hunting, same
		// gate the dead poll always used.
		ctx_.combat_manager->NotifyTargetDied(victim_id);
	}
	
	// If we died, log it prominently
	if (victim_id == ctx_.self.MySpawnId()) {
		std::cout << "YOU HAVE BEEN SLAIN!" << std::endl;

		// T349 (BOARD B059). OP_Save is the FIRST C->S packet of the official
		// client's self-death burst -- before the Lying appearance, before the
		// bind OP_ZoneChange -- in all four of T311 Leg A's deaths, and it
		// appears at those four moments and NOWHERE ELSE in the whole capture.
		// It is a death step, not housekeeping.
		//
		// It must be sent BEFORE ApplyPositionState(POS_DEAD) as well as
		// before the appearance: the body carries our current position, and
		// the oracle's carries the position we DIED at (death 1's OP_Save
		// holds the last OP_ClientUpdate's y/x/z byte for byte, not the bind
		// point the server had just named).
		//
		// announce=false: the burst prints nothing. "Saving character..." is
		// for the /save command.
		SendSave(/*announce=*/false);

		// T344 (B001): OP_Death for our own spawn id is the authoritative
		// signal that WE are dead, and until now nothing wrote it to
		// game_state.player().positionState() -- the only path that could
		// reach POS_DEAD was the debug setPositionState(4) action, and it
		// sent an AT_DIE appearance that T311 Leg A measured appearing zero
		// times in either direction of a real official-client session.
		// ApplyPositionState(), never SetPositionState(): the server told
		// us, so there is nothing to tell it back.
		//
		// T346 closes what T344 left owed: the Lying appearance below and
		// the OP_ClientUpdate silence that follows it.
		ctx_.Movement().ApplyPositionState(POS_DEAD);

		// T346 step 1 (BOARD B001 remainder). T311 Leg A captured a real
		// Titanium client dying four times; every one of the four windows
		// answers its own OP_Death with exactly one C->S OP_SpawnAppearance,
		// type 0x0e (AT_ANIMATION), param 0x73 (115, Animation::Lying), and
		// never AT_DIE. See tests/goldens/self_death_outbound_g07.txt.
		//
		// A DEDICATED call here, not SetPositionState's POS_DEAD arm, and
		// that is deliberate: the arm is also reachable from the debug
		// setPositionState(4) action, where we are not actually dead and
		// telling the server we are lying down would be a lie. T344 pinned
		// that arm as sending nothing (ForcedPoseNoEcho.
		// UserDeadState_NeverSendsAtDie) and it still sends nothing. Only a
		// real OP_Death for our own spawn id produces this packet.
		ctx_.Entities().SendSpawnAppearance(AT_ANIMATION, ANIM_LYING);
		// Close vendor window if open (can't shop while dead)
		if (ctx_.self.IsVendorWindowOpen()) {
			LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to player death");
			ctx_.Merchant().CloseVendorWindow();
		}

		// Cancel trade if we died
		if (ctx_.trade_manager && ctx_.trade_manager->isTrading()) {
			LOG_DEBUG(MOD_MAIN, "Player died during trade, canceling trade");
			ctx_.trade_manager->cancelTrade();
		}
	}

	// Cancel trade if trade partner died
	if (ctx_.trade_manager && ctx_.trade_manager->isTrading() &&
		ctx_.trade_manager->getPartnerSpawnId() == victim_id) {
		LOG_DEBUG(MOD_MAIN, "Trade partner died, canceling trade");
		ctx_.trade_manager->cancelTrade();
		ctx_.out.System("Trade cancelled - partner died");
	}

	// If the vendor NPC died, close vendor window
	if (ctx_.game_state.player().vendorNpcId() != 0 && ctx_.game_state.player().vendorNpcId() == victim_id) {
		LOG_DEBUG(MOD_INVENTORY, "Closed vendor window due to vendor death");
		ctx_.Merchant().CloseVendorWindow();
	}
}

void eqt::player::PlayerComponent::ZoneProcessPlayerStateRemove(const EQ::Net::Packet &p)
{
	// PlayerStateRemove removes a state/buff from a player
	// This is the counterpart to PlayerStateAdd
	
	if (p.Length() < 4) {
		LOG_WARN(MOD_ENTITY, "PlayerStateRemove packet too small: {} bytes", p.Length());
		return;
	}
	
	// The exact structure depends on the state being removed (buff/debuff/state change)
	// Parse what we can from the packet
	if (EverQuest::GetDebugLevel() >= 2 && p.Length() >= 6) {
		uint16_t stateType = p.GetUInt16(2);
		uint16_t stateValue = p.GetUInt16(4);
		LOG_DEBUG(MOD_ZONE, "[PlayerStateRemove] State removed: type={:#06x}, value={:#06x}, size={} bytes",
			stateType, stateValue, p.Length());
	}
}

void eqt::player::PlayerComponent::ZoneProcessStamina(const EQ::Net::Packet &p)
{
	// Stamina (hunger/thirst) update packet — always refers to our own
	// character (there is no spawn_id field; the server's Stamina_Struct
	// is just { uint32 food; uint32 water; }, confirmed against
	// akk-stack/code/zone/client.cpp's SetHunger/SetThirst/SetConsumption,
	// which are the only senders of OP_Stamina).
	//
	// H03: this previously assumed a spawn_id (2 bytes) + current_stamina
	// (4 bytes) + max_stamina (4 bytes) layout that doesn't match the real
	// protocol, requiring 12 bytes total (2 opcode + 10 struct) but only
	// checking for 10 — an out-of-range read on real 8-byte payloads (2
	// opcode + 8 struct), reproduced replaying a real recorded session
	// (see refactor/H03_eqemu_recording.md). Fixed to the real 8-byte
	// food/water layout.
	if (p.Length() < 10) {  // 2 opcode + 8 bytes (food + water)
		LOG_WARN(MOD_ENTITY, "Stamina packet too small: {} bytes", p.Length());
		return;
	}

	uint32_t food = p.GetUInt32(2);
	uint32_t water = p.GetUInt32(6);

	LOG_DEBUG(MOD_ENTITY, "Stamina update: food={}, water={}", food, water);

	// TODO: Store hunger/thirst values as member variables if needed.
}

void eqt::player::PlayerComponent::ZoneProcessZonePlayerToBind(const EQ::Net::Packet &p)
{
	// ZonePlayerToBind is sent when the player dies and needs to respawn at bind point
	// Uses ZonePlayerToBind_Struct (variable length):
	// bind_zone_id (2), bind_instance_id (2), x (4), y (4), z (4), heading (4), zone_name[] (variable)
	// Minimum size: 2 opcode + 20 fixed = 22 bytes

	LOG_INFO(MOD_MAIN, "You are being sent to your bind point...");

	if (p.Length() < 22) {  // 2 bytes opcode + 20 bytes minimum struct
		LOG_WARN(MOD_MAIN, "ZonePlayerToBind packet too small: {} bytes (need at least 22)", p.Length());
		// Fall back to stored bind point
		LOG_DEBUG(MOD_MAIN, "Using stored bind point: zone {} at ({:.2f}, {:.2f}, {:.2f})",
			ctx_.game_state.player().bindZoneId(), ctx_.game_state.player().bindX(), ctx_.game_state.player().bindY(), ctx_.game_state.player().bindZ());
		return;
	}

	// Parse ZonePlayerToBind_Struct
	uint16_t target_zone_id = p.GetUInt16(2);
	uint16_t instance_id = p.GetUInt16(4);
	float zone_x = p.GetFloat(6);   // Server x
	float zone_y = p.GetFloat(10);  // Server y
	float zone_z = p.GetFloat(14);
	float zone_heading = p.GetFloat(18);

	// Extract zone_name if present (starts at offset 22)
	std::string zone_name;
	if (p.Length() > 22) {
		const char* name_ptr = static_cast<const char*>(p.Data()) + 22;
		size_t max_len = p.Length() - 22;
		zone_name = std::string(name_ptr, strnlen(name_ptr, max_len));
	}

	// Swap x/y for our coordinate system (server sends x,y but we use y,x internally)
	float bind_x = zone_y;
	float bind_y = zone_x;
	float bind_z = zone_z;
	float bind_heading = zone_heading;

	LOG_DEBUG(MOD_MAIN, "ZonePlayerToBind: zone={} instance={} pos=({:.2f}, {:.2f}, {:.2f}) heading={:.1f} name='{}'",
		target_zone_id, instance_id, bind_x, bind_y, bind_z, bind_heading, zone_name);

	// Store pending zone info

	// T55: mirror producer B's WorldState publish (CheckZoneLine, eq_zone.cpp)
	// so producer C's pending-zone state is visible from GameState too, not
	// just via the raw EverQuest members -- see
	// refactor/T55_position_state_propagation.md.
	ctx_.game_state.world().setPendingZone(target_zone_id, bind_x, bind_y, bind_z, bind_heading);

	// Check if we're staying in the same zone or going to a different zone
	bool is_same_zone = (target_zone_id == ctx_.self.CurrentZoneId()) || (target_zone_id == 0);

	// T55: this handler used to store the pending zone coordinates (above)
	// but perform NO local position write at all -- the T39 pattern
	// ("setters written but never wired") surviving in the death/bind path.
	// For the same-zone branch this meant the client kept showing the
	// character's PRE-DEATH position until (if ever) a fresh PlayerProfile
	// round-trip corrected it; see this unit's Review for the replay
	// evidence that confirmed this live.
	//
	// The local write is applied ONLY on the same-zone branch, and this is
	// deliberate, not a missed case. At the moment this handler runs, the
	// client is still fully zoned in to the OLD zone -- IsFullyZonedIn() is
	// true and m_zone_connection is still live; the disconnect doesn't
	// happen until ProcessDeferredZoneChange runs, after the OP_ZoneChange
	// handshake below completes. SendPositionUpdate() (eq_movement.cpp) is
	// gated on exactly that same IsFullyZonedIn()+m_zone_connection check,
	// throttled only by a 250ms timer -- so writing the BIND ZONE's
	// coordinates into the client position here on a CROSS-zone death would let the
	// client transmit them to the OLD zone's server on its very next
	// position update, which will accept and persist them. This is T52's
	// central lesson verbatim ("the server respects whatever position the
	// client sends it... a client that mis-applies an approved teleport
	// position will immediately correct the server back to the wrong place
	// via its next SendPositionUpdate()") -- and it would bite even harder
	// on the DENIED path (pinned by
	// ZonePositionConvention.ProducerA_DeniedChangeClearsPendingZone):
	// a denied cross-zone change would leave the character sitting in the
	// OLD zone, at coordinates that belong to a different zone. Same-zone
	// has no such risk -- the coordinates are valid for the very server
	// that will receive them. On cross-zone, the far-side landing position
	// is the zone-in consumer's job (WorldProcessZoneServerInfo /
	// PlayerProfile), not this handler's.
	if (is_same_zone) {
		ctx_.self.SetPosition(bind_x, bind_y, bind_z);
		ctx_.self.SetHeading(bind_heading);
		LOG_DEBUG(MOD_MAIN, "Same-zone respawn to ({:.2f}, {:.2f}, {:.2f})", bind_x, bind_y, bind_z);
	} else {
		LOG_DEBUG(MOD_MAIN, "Cross-zone respawn: current zone {} -> bind zone {}",
			ctx_.self.CurrentZoneId(), target_zone_id);
	}

	// Client must respond with OP_ZoneChange to initiate the zone change
	// The server will then send OP_ZoneChange back with success=1
	// For same-zone deaths, the server still expects this handshake
	ctx_.Zone().RequestZoneChange(target_zone_id, bind_x, bind_y, bind_z, bind_heading);
}

void eqt::player::PlayerComponent::ZoneProcessLevelUpdate(const EQ::Net::Packet &p)
{
	// LevelUpdate_Struct: level (4), level_old (4), exp (4) = 12 bytes
	// Sent when player levels up or down (death exp loss can cause level loss)

	if (p.Length() < 14) {  // 2 bytes opcode + 12 bytes struct
		LOG_WARN(MOD_MAIN, "LevelUpdate packet too small: {} bytes (need 14)", p.Length());
		return;
	}

	uint32_t new_level = p.GetUInt32(2);
	uint32_t old_level = p.GetUInt32(6);
	uint32_t exp = p.GetUInt32(10);

	LOG_DEBUG(MOD_MAIN, "LevelUpdate: level {} -> {}, exp={}", old_level, new_level, exp);

	// Update player level (T286: PlayerState is the only store, so it is
	// written here, at the source, BEFORE anything reads it back)
	ctx_.game_state.player().setLevel(static_cast<uint8_t>(new_level));

	// Update entity level if we have a spawn ID
	if (ctx_.self.MySpawnId() != 0) {
		auto it = ctx_.entities.find(ctx_.self.MySpawnId());
		if (it != ctx_.entities.end()) {
			it->second.level = ctx_.game_state.player().level();
		}
	}

	if (new_level > old_level) {
		LOG_INFO(MOD_MAIN, "LEVEL UP! You are now level {}!", new_level);
	} else if (new_level < old_level) {
		LOG_INFO(MOD_MAIN, "You have lost a level. You are now level {}.", new_level);
	}
}







void eqt::player::PlayerComponent::SendTradeSkillCombine(int16_t container_slot)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		LOG_DEBUG(MOD_ENTITY, "Cannot combine - not fully zoned in");
		return;
	}

	LOG_INFO(MOD_ENTITY, "Sending tradeskill combine for container slot {}", container_slot);

	// Build TradeSkillCombine packet
	EQ::Net::DynamicPacket p;
	p.Resize(2 + sizeof(EQT::NewCombine_Struct));

	p.PutUInt16(0, HC_OP_TradeSkillCombine);
	p.PutInt16(2, container_slot);     // container_slot
	p.PutInt16(4, 0);                  // guildtribute_slot (usually 0)

	ctx_.net.SafeQueueZone(p, 0, true);
}





void eqt::player::PlayerComponent::ZoneProcessTradeSkillCombine(const EQ::Net::Packet &p)
{
	// Server response to a combine attempt
	// The server sends an empty acknowledgment packet (0 bytes payload)
	// Actual results come via separate packets:
	// - OP_FormattedMessage with string_id 339 for success ("You create a <item>!")
	// - OP_ItemPacket for inventory updates
	// - OP_SkillUpdate for skill increases
	// These are already handled in their respective handlers.

	LOG_DEBUG(MOD_ENTITY, "TradeSkillCombine acknowledgment received ({} bytes)", p.Length());
}




