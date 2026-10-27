#include "client/core/memory_ledger.h"
#include "client/eq.h"
#include "client/say_link_text.h"
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

#include <cstddef>
#include <cstring>

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
#include <unordered_map>
#include <map>
#include <tuple>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// G01 (refactor/G01_dispatch_zone_packet_table.md): DispatchZonePacket's
// 145-arm opcode switch is now a table lookup. Every arm that carried local
// logic became one of the named handlers below -- body VERBATIM, the arm's
// terminating `break;` dropped and (in OnMemorizeSpell, the one arm that used
// `break` as an early exit) rewritten as `return;`. Every arm that was a
// single call is a table entry. The three deliberate no-op arms are entries in
// kIgnoredOpcodes carrying their comment as the reason. The old `default:` arm
// is the table-miss path at the bottom of this file.
//
// tests/test_zone_dispatch_table.cpp holds the regression net: the golden list
// of every case label the switch had (tests/goldens/zone_opcodes_g01.txt) must
// equal the table's keys plus the ignored set, in both directions.

void eqt::zone::ZoneComponent::OnSessionReady(const EQ::Net::Packet & p)
{
	LOG_DEBUG(MOD_ZONE, "Zone session established, sending ack and zone entry");
	// Send ack packet
	ZoneSendAckPacket();
	// Send zone entry
	ZoneSendZoneEntry();
}

void eqt::zone::ZoneComponent::OnZoneEntry(const EQ::Net::Packet & p)
{
	// Server echoes our zone entry back with ServerZoneEntry_Struct
	LOG_DEBUG(MOD_MAIN, "Zone entry response, size: {}", p.Length());
	
	// ServerZoneEntry_Struct contains NewSpawn_Struct which contains Spawn_Struct
	// Let's check for spawn ID at various offsets
	if (p.Length() > 10) {
		// Try to find spawn ID - it might be early in the spawn struct
		LOG_DEBUG(MOD_MAIN, "Potential spawn IDs in ZoneEntry response:");
		LOG_DEBUG(MOD_MAIN, "  uint16 at offset 2: {}", p.GetUInt16(2));
		LOG_DEBUG(MOD_MAIN, "  uint16 at offset 4: {}", p.GetUInt16(4));
		LOG_DEBUG(MOD_MAIN, "  uint32 at offset 2: {}", p.GetUInt32(2));
		LOG_DEBUG(MOD_MAIN, "  uint32 at offset 6: {}", p.GetUInt32(6));
		
		// The spawn struct has name at offset 7, let's check before that
		if (p.Length() > 71) {
			auto spawn_name = p.GetCString(9); // offset 2 + 7
			LOG_DEBUG(MOD_MAIN, "  Spawn name at offset 7: '{}'", spawn_name);
		}
	}

	// B1164 (#483) fix leg: this packet is our own Spawn_Struct (385 bytes,
	// include/common/packets/spawn.h; the oracle capture's S->C OP_ZoneEntry
	// follows OP_PlayerProfile). ZoneProcessPlayerProfile gave our entity a
	// placeholder size of 6.0 (zone_entry.cpp), and on a graphical zone-in no
	// other spawn packet for ourselves arrives (T332), so the placeholder
	// stood for the session. The official client takes this struct's size
	// (+75; Testcaster's reads 0.000 = "none" -> the race default, eqgame
	// 0x47fb07..0x47fb30) for its actor scale, which is the attach scale of
	// our own spell slots (spell_fx_attach_scale.h). The entity record
	// carries it from here; PlayerState's size (the wire-z offset) is not
	// touched.
	if (p.Length() >= 2 + sizeof(EQT::Spawn_Struct)) {
		const uint32_t spawn_id = p.GetUInt32(2 + offsetof(EQT::Spawn_Struct, spawnId));
		auto it = ctx_.entities.find(ctx_.self.MySpawnId());
		if (it != ctx_.entities.end() && spawn_id == ctx_.self.MySpawnId()) {
			float size = 0.0f;
			std::memcpy(&size, static_cast<const char*>(p.Data()) + 2 + offsetof(EQT::Spawn_Struct, size),
			            sizeof(size));
			it->second.size = size;
			ctx_.Player().SyncEntityToGameState(it->second);
			LOG_DEBUG(MOD_MAIN, "OP_ZoneEntry: own spawn {} size {:.3f}", spawn_id, size);
		}
	}
}

void eqt::zone::ZoneComponent::OnTaskDescription(const EQ::Net::Packet & p)
{
	// Task description data - not used in P1999
	LOG_DEBUG(MOD_ZONE, "Received TaskDescription ({} bytes)", p.Length());
}

void eqt::zone::ZoneComponent::OnTaskActivity(const EQ::Net::Packet & p)
{
	// Task activity update - not used in P1999
	LOG_DEBUG(MOD_ZONE, "Received TaskActivity ({} bytes)", p.Length());
}

void eqt::zone::ZoneComponent::OnClearLeadershipAbilities(const EQ::Net::Packet & p)
{
	// Clear leadership AA on zone-in
	LOG_DEBUG(MOD_ZONE, "Received ClearLeadershipAbilities");
}

void eqt::zone::ZoneComponent::OnClearAA(const EQ::Net::Packet & p)
{
	// Clear AA on zone-in (resets client state)
	LOG_DEBUG(MOD_ZONE, "Received ClearAA");
}

void eqt::zone::ZoneComponent::OnAAExpUpdate(const EQ::Net::Packet & p)
{
	// AA experience update
	LOG_DEBUG(MOD_ZONE, "Received AAExpUpdate ({} bytes)", p.Length());
}

void eqt::zone::ZoneComponent::OnLootRequest(const EQ::Net::Packet & p)
{
	// Server responds to our loot request - confirms we can loot the corpse
	// The actual loot items come via ItemPacket (type 102) BEFORE this response
	if (p.Length() >= 6) {
		uint32_t corpse_id = p.GetUInt32(2);
		LOG_DEBUG(MOD_INVENTORY, "LootRequest response: corpseId={}", corpse_id);

		// Check if loot window is empty (no items to loot)
		// If empty, auto-complete the loot session immediately
		if (ctx_.game_state.player().lootingCorpseId() != 0 && ctx_.Bridge().LootItems().empty()) {
			LOG_DEBUG(MOD_INVENTORY, "LootRequest response: No items on corpse, auto-completing loot");
			ctx_.Bridge().CloseLootWindow(static_cast<uint16_t>(corpse_id));
		}
	}
}

void eqt::zone::ZoneComponent::OnLootItem(const EQ::Net::Packet & p)
{
	// Server echoes OP_LootItem back as acknowledgment
	if (p.Length() >= 16) {
		uint32_t lootee = p.GetUInt32(2);
		uint32_t looter = p.GetUInt32(6);
		uint16_t slot_id = p.GetUInt16(10);
		int32_t auto_loot = static_cast<int32_t>(p.GetUInt32(12));
		LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: lootee={} looter={} slot={} auto_loot={}",
			lootee, looter, slot_id, auto_loot);
		// auto_loot == -1 means the loot was denied
		if (auto_loot == -1) {
			LOG_WARN(MOD_INVENTORY, "Loot DENIED by server!");
			// Remove from pending slots
			auto it = std::find(ctx_.Bridge().PendingLootSlots().begin(), ctx_.Bridge().PendingLootSlots().end(), slot_id);
			if (it != ctx_.Bridge().PendingLootSlots().end()) {
				ctx_.Bridge().PendingLootSlots().erase(it);
			}
		}
	} else {
		LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: short packet, len={}", p.Length());
	}
}

void eqt::zone::ZoneComponent::OnLootComplete(const EQ::Net::Packet & p)
{
	// Server signals end of loot session (rarely sent by P1999/Titanium servers)
	LOG_DEBUG(MOD_INVENTORY, "LootComplete received from server");
	ctx_.Bridge().PendingLootSlots().clear();
}

void eqt::zone::ZoneComponent::OnItemPacket(const EQ::Net::Packet & p)
{
	// Item packets can be: vendor items (100), inventory items (103), or loot items
	// Check the packet type in the first 4 bytes after the header
	{
		uint32_t item_packet_type = 0;
		if (p.Length() >= 6) {
			item_packet_type = p.GetUInt32(2);
		}
		LOG_DEBUG(MOD_INVENTORY, "ItemPacket received, type={} m_game_state.player().lootingCorpseId()={} m_game_state.player().vendorNpcId()={} pending_slots={}",
			item_packet_type, ctx_.game_state.player().lootingCorpseId(), ctx_.game_state.player().vendorNpcId(), ctx_.Bridge().PendingLootSlots().size());
		// Check if this is a vendor item (type 100)
		if (item_packet_type == EQT::ITEM_PACKET_MERCHANT && ctx_.game_state.player().vendorNpcId() != 0) {
			// Vendor item - add to vendor window via bridge
			ctx_.Merchant().ZoneProcessVendorItemToUI(p);
		} else if (ctx_.game_state.player().lootingCorpseId() != 0) {
			// Looting (T268: every open path sets this). A pending slot means
			// this is the item we asked for; otherwise it populates the window.
			if (!ctx_.Bridge().PendingLootSlots().empty()) { ctx_.Merchant().ZoneProcessLootedItemToInventory(p); }
			else { ctx_.Merchant().ZoneProcessLootItemToUI(p); }
		} else {
			// Check if this is a trade partner item.
			// T42: was #ifdef EQT_HAS_GRAPHICS -- TradeManager and
			// InventoryManager are both renderer-free (P28/this file's
			// own header comment), so an EQT_GRAPHICS=OFF build was
			// dropping regular inventory item packets (vendor buy, bank
			// window population) entirely. Un-guarded; no genuine
			// graphics dependency found.
			bool handled_as_trade = false;
			if (ctx_.trade_manager && ctx_.trade_manager->isTrading()) {
				handled_as_trade = ctx_.Merchant().ZoneProcessTradePartnerItem(p);
			}
			if (!handled_as_trade) {
				// Regular inventory item packet - pass to inventory manager.
				// This is the single choke point for vendor-buy item
				// placement and bank-window population (T41's Review
				// traced both here) -- publish InventorySlotChanged here
				// so both get the UI update, not just the game-side model.
				if (ctx_.inventory_manager) {
					int16_t changedSlot = ctx_.inventory_manager->processItemPacket(p);
					if (changedSlot != eqt::inventory::SLOT_INVALID) {
						const auto* item = ctx_.inventory_manager->getItem(changedSlot);
						ctx_.Merchant().SyncInventorySlotToGameState(changedSlot,
							item != nullptr && item->itemId != 0);  // T221
					}
					if (ctx_.bridge && changedSlot != eqt::inventory::SLOT_INVALID) {
						eqt::state::InventorySlotChangedData data;
						data.slotId = changedSlot;
						const auto* item = ctx_.inventory_manager->getItem(changedSlot);
						data.hasItem = (item != nullptr && item->itemId != 0);
						data.itemId = data.hasItem ? item->itemId : 0;
						data.itemName = data.hasItem ? item->name : "";
						data.iconId = data.hasItem ? item->icon : 0;
						data.quantity = data.hasItem ? item->quantity : 0;
						data.bagSlots = data.hasItem ? item->bagSlots : 0;  // B442b
						data.scrollSpellId = data.hasItem ? item->getScrollSpellId() : 0;  // B1076
						LOG_DEBUG(MOD_INVENTORY, "Publishing InventorySlotChanged (generic ItemPacket fallback): slot={} hasItem={} itemId={} name='{}' qty={}",
							data.slotId, data.hasItem, data.itemId, data.itemName, data.quantity);
						ctx_.bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
					}
				}
			}
			if (EverQuest::GetDebugLevel() >= 2) {
				LOG_DEBUG(MOD_MAIN, "Received ItemPacket type={} while not looting/trading", item_packet_type);
			}
		}
	}
}

void eqt::zone::ZoneComponent::OnTargetCommandResponse(const EQ::Net::Packet & p)
{
	// Server may reject our target
	if (p.Length() >= 4) {
		uint16_t result = p.GetUInt16(2);
		if (result != 0 && EverQuest::GetDebugLevel() >= 1) {
			LOG_DEBUG(MOD_MAIN, "Target rejected: {}", result);
		}
	}
}

void eqt::zone::ZoneComponent::OnBecomeCorpse(const EQ::Net::Packet & p)
{
	// Someone died and became a corpse
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Received BecomeCorpse packet");
	}
}

void eqt::zone::ZoneComponent::OnTargetHoTT(const EQ::Net::Packet & p)
{
	// Target's target information
	if (p.Length() >= 6) {
		uint16_t target_id = p.GetUInt16(2);
		uint16_t hott_id = p.GetUInt16(4);
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Target {} has target {}", target_id, hott_id);
		}
	}
}

void eqt::zone::ZoneComponent::OnSkillUpdate(const EQ::Net::Packet & p)
{
	// Skill level update
	// SkillUpdate_Struct: skillId (uint32) + value (uint32) = 8 bytes + 2 byte header
	if (p.Length() >= 10) {
		uint32_t skill_id_32 = p.GetUInt32(2);  // 4-byte skillId at offset 2
		uint32_t value = p.GetUInt32(6);        // 4-byte value at offset 6
		uint8_t skill_id = static_cast<uint8_t>(skill_id_32);  // Cast to uint8_t for skill manager

		// Update skill manager
		if (ctx_.skill_manager) {
			ctx_.skill_manager->updateSkill(skill_id, value);
		}

		// Decrement practice points if trainer window is open (training
		// succeeded). PlayerState is the ONE store (T283) and
		// decrementPracticePoints() already floors at zero.
		if (ctx_.game_state.player().trainerNpcId() != 0) {
			ctx_.game_state.player().decrementPracticePoints();
		}

		// Publish SkillValueChanged event to bridge
		if (ctx_.bridge) {
			eqt::state::SkillValueChangedData sdata;
			sdata.skillId = static_cast<uint16_t>(skill_id);
			sdata.value = static_cast<uint16_t>(value);
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SkillValueChanged, std::move(sdata)));
		}

		// B898 (#214): on Titanium this packet is the server's ONLY answer to
		// a train (commands_train.cpp), so it is what refreshes an open
		// trainer window -- new value, new price, the practice just spent.
		if (ctx_.game_state.player().trainerNpcId() != 0) {
			ctx_.Merchant().PublishTrainerWindow();
		}

		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Skill {} updated to {}", static_cast<int>(skill_id), value);
		}
	}
}

void eqt::zone::ZoneComponent::OnCancelTrade(const EQ::Net::Packet & p)
{
	// Trade cancelled
	if (p.Length() >= 2 + sizeof(EQT::CancelTrade_Struct)) {
		const EQT::CancelTrade_Struct* cancel = reinterpret_cast<const EQT::CancelTrade_Struct*>(static_cast<const uint8_t*>(p.Data()) + 2);
		if (ctx_.trade_manager) {
			ctx_.trade_manager->handleCancelTrade(*cancel);
		}
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Trade cancelled by spawn {}", cancel->spawn_id);
		}
	}
}

void eqt::zone::ZoneComponent::OnTradeRequest(const EQ::Net::Packet & p)
{
	// Trade request from another player
	if (p.Length() >= 2 + sizeof(EQT::TradeRequest_Struct)) {
		const EQT::TradeRequest_Struct* req = reinterpret_cast<const EQT::TradeRequest_Struct*>(static_cast<const uint8_t*>(p.Data()) + 2);
		if (ctx_.trade_manager) {
			// Look up the name of the requesting player
			std::string from_name = "Unknown";
			auto it = ctx_.entities.find(static_cast<uint16_t>(req->from_spawn_id));
			if (it != ctx_.entities.end()) {
				from_name = it->second.name;
			}
			ctx_.trade_manager->handleTradeRequest(*req, from_name);
			// B896 (#212): the official client ACKs a trade request by itself,
			// with no prompt, 0.17 s after it arrives, and both windows open
			// (MEASURED, review/UIGAP-ORACLE/212/trade.tsv row 1). /accept and
			// /tradedecline keep their pending-request bodies for a request
			// the manager is holding, but none is held here any more.
			if (ctx_.trade_manager->getState() == TradeState::PendingAccept) {
				ctx_.trade_manager->acceptTradeRequest();
			}
		}
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Trade request from spawn {} to spawn {}",
			         req->from_spawn_id, req->target_spawn_id);
		}
	}
}

void eqt::zone::ZoneComponent::OnTradeRequestAck(const EQ::Net::Packet & p)
{
	// Trade request accepted
	if (p.Length() >= 2 + sizeof(EQT::TradeRequestAck_Struct)) {
		const EQT::TradeRequestAck_Struct* ack = reinterpret_cast<const EQT::TradeRequestAck_Struct*>(static_cast<const uint8_t*>(p.Data()) + 2);
		if (ctx_.trade_manager) {
			// B896 (#212): OUR request answered (a player's client, or the
			// server for an NPC) -> the held item goes into slot 3000 by
			// itself (trade.tsv rows 1 and 9).
			const bool weAsked = ctx_.trade_manager->getState() == TradeState::PendingRequest;
			ctx_.trade_manager->handleTradeRequestAck(*ack);
			if (weAsked && ctx_.trade_manager->isTrading()) {
				ctx_.Bridge().OfferHeldItemOnTradeOpen();
			}
		}
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Trade request ack from spawn {} to spawn {}",
			         ack->from_spawn_id, ack->target_spawn_id);
		}
	}
}

void eqt::zone::ZoneComponent::OnTradeCoins(const EQ::Net::Packet & p)
{
	// Trade partner changed money
	if (p.Length() >= 2 + sizeof(EQT::TradeCoins_Struct)) {
		const EQT::TradeCoins_Struct* coins = reinterpret_cast<const EQT::TradeCoins_Struct*>(static_cast<const uint8_t*>(p.Data()) + 2);
		if (ctx_.trade_manager) {
			ctx_.trade_manager->handleTradeCoins(*coins);
		}
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Trade coins from spawn {}: slot {} amount {}",
			         coins->spawn_id, coins->slot, coins->amount);
		}
	}
}

void eqt::zone::ZoneComponent::OnTradeAcceptClick(const EQ::Net::Packet & p)
{
	// Trade partner clicked accept
	if (p.Length() >= 2 + sizeof(EQT::TradeAcceptClick_Struct)) {
		const EQT::TradeAcceptClick_Struct* accept = reinterpret_cast<const EQT::TradeAcceptClick_Struct*>(static_cast<const uint8_t*>(p.Data()) + 2);
		if (ctx_.trade_manager) {
			ctx_.trade_manager->handleTradeAcceptClick(*accept);
		}
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Trade accept click from spawn {}: accepted={}",
			         accept->spawn_id, accept->accepted);
		}
	}
}

void eqt::zone::ZoneComponent::OnFinishTrade(const EQ::Net::Packet & p)
{
	// Trade completed. OP_FinishTrade has a ZERO-LENGTH payload -- eqemu
	// sends `new EQApplicationPacket(OP_FinishTrade, 0)`
	// (zone/client_packet.cpp:15358). The 2 "unknown" bytes the old
	// EQT::FinishTrade_Struct declared were fictional; T147 deleted it.
	// Nothing changes behaviourally: the old code already fell back to a
	// zeroed struct when the length guard failed, which it always did.
	if (ctx_.trade_manager) {
		ctx_.trade_manager->handleFinishTrade();
	}
	LOG_INFO(MOD_MAIN, "Trade finished");
}

void eqt::zone::ZoneComponent::OnPreLogoutReply(const EQ::Net::Packet & p)
{
	// Pre-logout confirmation (server acknowledges camp request)
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Pre-logout reply received (camp acknowledged)");
	}
}

void eqt::zone::ZoneComponent::OnMobRename(const EQ::Net::Packet & p)
{
	// Mob name change - typically when mob becomes corpse
	// The packet contains 3 name fields (each 64 bytes)
	if (p.Length() >= 4) {  // Need at least opcode + 2 bytes for parsing
		// Parse the packet to find the entity ID
		// From the log, we see the mob name appears multiple times with "'s_corpse" appended
		const char* data = (const char*)p.Data() + 2;  // Skip opcode
		int data_len = p.Length() - 2;
		
		// Extract the new name from the first occurrence
		char new_name[65] = {0};
		if (data_len >= 64) {
			memcpy(new_name, data, 64);
			new_name[64] = '\0';
		}
		
		// The mob ID isn't directly in the packet, but the corpse name ends
		// in the corpse's own entity id, and failing that we can match the
		// base name. T160: ParseCorpseName (combat.h) is the single shared
		// test -- this used to carry its own copy of the "'s_corpse"
		// literal, which meant the space form eqemu uses for PLAYER corpses
		// was never recognised here either.
		std::string name_str(new_name);
		std::string base_name;
		uint32_t parsed_corpse_id = 0;
		if (ParseCorpseName(name_str, &base_name, &parsed_corpse_id)) {
			uint16_t entity_id = static_cast<uint16_t>(parsed_corpse_id);
			if (ctx_.entities.find(entity_id) == ctx_.entities.end()) {
				// Id in the name doesn't resolve (truncated name, or a
				// server that numbers corpses differently) -- fall back to
				// matching the base name.
				for (auto& [id, entity] : ctx_.entities) {
					if (entity.name == base_name + "001" || entity.name == base_name) {
						entity_id = id;
						break;
					}
				}
			}

			// Entity id 0 is never a real spawn here (it is the
			// unset/self sentinel); unchanged from the pre-T160 guard.
			auto it = entity_id > 0 ? ctx_.entities.find(entity_id) : ctx_.entities.end();
			if (it != ctx_.entities.end()) {
				std::string old_name = it->second.name;
				it->second.name = new_name;
				it->second.is_corpse = true;
				// T268: GameState kept the LIVING name + isCorpse=false forever
				// (markAsCorpse had no caller) -- /target and /loot missed it.
				ctx_.Player().SyncEntityToGameState(it->second);
				if (EverQuest::GetDebugLevel() >= 1) {
					LOG_INFO(MOD_ENTITY, "Entity {} became corpse: '{}' -> '{}'",
						entity_id, old_name, new_name);
				}
			}
		}
	}
}

void eqt::zone::ZoneComponent::OnStun(const EQ::Net::Packet & p)
{
	// T247. eqemu Stun_Struct is 4 bytes: uint32 duration in MILLISECONDS
	// (Client::Stun -> stunned_timer.Start(duration)); Client::UnStun sends
	// duration 0. It is only ever sent to the stunned client itself, so
	// there is no target field -- the pre-T247 code read a target/duration
	// pair out of the low/high halves of the duration and stored nothing.
	if (p.Length() >= 6) {
		ctx_.Commands().ZoneProcessStun(p.GetUInt32(2));
	}
}

void eqt::zone::ZoneComponent::OnInterruptCast(const EQ::Net::Packet & p)
{
	// T469 (board B604). eqemu InterruptCast_Struct (common/patches/
	// titanium_structs.h:426; Titanium has no ENCODE for it, so this is the
	// wire form): uint32 spawnid, uint32 messageid, then -- on the copy
	// Mob::InterruptSpell sends to BYSTANDERS only (zone/spells.cpp:1339) --
	// the caster's name as a C string. The caster's own copy is 8 bytes
	// (spells.cpp:1306). Neither carries a spell id.
	if (p.Length() < 10) {
		LOG_WARN(MOD_SPELL, "InterruptCast packet too small: {} bytes", p.Length());
		return;
	}
	uint32_t spawn_id = p.GetUInt32(2);
	uint32_t message_id = p.GetUInt32(6);
	std::string caster_name;
	if (p.Length() > 10) {
		const char* text = static_cast<const char*>(p.Data()) + 10;
		caster_name.assign(text, strnlen(text, p.Length() - 10));
	}
	if (ctx_.spell_manager) {
		ctx_.spell_manager->handleInterrupt(static_cast<uint16_t>(spawn_id), message_id, caster_name);
	}
}

void eqt::zone::ZoneComponent::OnMemorizeSpell(const EQ::Net::Packet & p)
{
	{
		// MemorizeSpell_Struct: slot(4), spell_id(4), scribing(4), unknown0(4)
		// Total: 16 bytes after opcode (2 bytes)
		if (p.Length() < 18) {
			LOG_WARN(MOD_SPELL, "MemorizeSpell packet too short: {} bytes", p.Length());
			return;
		}

		uint32_t slot = p.GetUInt32(2);
		uint32_t spell_id = p.GetUInt32(6);
		uint32_t scribing = p.GetUInt32(10);

		LOG_DEBUG(MOD_SPELL, "MemorizeSpell response: slot={} spell_id={} scribing={}",
			slot, spell_id, scribing);

		// Check if this is a scribe confirmation (scribing flag cleared to 0)
		if (m_pending_scribe_spell_id != 0 &&
			m_pending_scribe_spell_id == spell_id &&
			scribing == 0) {

			// Add spell to spellbook
			if (ctx_.spell_manager->scribeSpell(spell_id, static_cast<uint16_t>(slot))) {
				// Get spell name for message
				const EQ::SpellData* spellData = ctx_.spell_manager->getSpell(spell_id);
				std::string spellName = spellData ? spellData->name : "spell";

				ctx_.out.System(fmt::format("You have learned {}!", spellName));
				LOG_INFO(MOD_SPELL, "Successfully scribed spell {} ({}) to slot {}",
					spellName, spell_id, slot);

				// Refresh spellbook UI via bridge
				if (ctx_.bridge) {
					eqt::state::SpellScribeCompletedData sdata;
					sdata.spellId = spell_id;
					sdata.slot = static_cast<uint16_t>(slot);
					sdata.spellName = spellName;
					sdata.iconId = spellData ? spellData->spell_icon : 0;
					ctx_.bridge->pushEvent(eqt::state::GameEvent(
						eqt::state::GameEventType::SpellScribeCompleted, std::move(sdata)));
				}
			} else {
				LOG_WARN(MOD_SPELL, "Failed to add spell {} to spellbook slot {}",
					spell_id, slot);
			}

			// T276: the server ate the scroll off the cursor
			// (Client::OPMemorizeSpell -> DeleteItemInInventory(
			// slotCursor, 1, true)) and confirms with OP_DeleteItem,
			// which processDeleteItemResponse() does not act on. Drop
			// our own copy locally, or the spent scroll stays on the
			// cursor and blocks the next /scribe.
			if (ctx_.inventory_manager) {
				ctx_.inventory_manager->consumeScribedScroll();
			}

			// Clear pending scribe state
			m_pending_scribe_spell_id = 0;
			m_pending_scribe_book_slot = 0;
			m_pending_scribe_source_slot = -1;
		}
		// Handle gem slot memorization confirmations (slot < EQT::SPELL_GEM_COUNT -- T483: all nine)
		else if (slot < EQT::SPELL_GEM_COUNT && scribing == 0) {
			// Existing gem memorization - SpellManager handles this via other mechanisms
			LOG_DEBUG(MOD_SPELL, "Gem memorization confirmation: gem={} spell={}", slot, spell_id);
		}
		// T404 (board B254): the MEMORISE and UN-MEMORISE arms, which did not
		// exist. This handler acted only on scribing == 0 (a scribe into the
		// spellbook), so nothing the server said about a GEM ever reached
		// SpellManager: m_spell_gems moved only on our own local memorise
		// timer and on the PlayerProfile bulk load. T401 measured the
		// consequence -- #memspell 93 0 into an occupied gem produced the
		// correct pair of responses (slot=0 spell_id=467 scribing=2, then
		// slot=0 spell_id=93 scribing=1) and /cast 1 still sent spell=467,
		// "Beginning cast of 'Lightning Storm'". A player clicking a gem got
		// a different spell than its icon showed.
		//
		// One packet, one edit, applied in ARRIVAL ORDER -- so the un-memorise
		// that precedes the memorise for the same slot cannot undo it.
		else if (slot < EQT::SPELL_GEM_COUNT && (scribing == 1 || scribing == 2)) {
			if (ctx_.spell_manager) {
				ctx_.spell_manager->applyServerGemUpdate(slot, spell_id, scribing);
			}
		}
		// T482 (board B631): memSpellSpellbar, the last packet of eqemu's
		// successful-cast tail (Mob::CastedSpellFinished, zone/spells.cpp:1803,
		// :1824). Titanium maps the server casting slot for this case only
		// (titanium.cpp:1266), so `slot` is a gem index, 10 for an item.
		else if (scribing == 3) {
			if (ctx_.spell_manager) {
				ctx_.spell_manager->handleSpellBarRefresh(slot, spell_id);
			}
		}
	}
}

void eqt::zone::ZoneComponent::OnSpecialMesg(const EQ::Net::Packet & p)
{
	{
		// Special messages (trade notifications, skill-ups, etc.)
		// Format: 2 bytes opcode + 24 bytes header + null-terminated message
		constexpr size_t HEADER_SIZE = 24;
		constexpr size_t MSG_OFFSET = 2 + HEADER_SIZE;  // After opcode and header

		if (p.Length() > MSG_OFFSET) {
			const char* msgStart = reinterpret_cast<const char*>(p.Data()) + MSG_OFFSET;
			size_t maxLen = p.Length() - MSG_OFFSET;
			std::string message(msgStart, strnlen(msgStart, maxLen));

			if (!message.empty()) {
				// T395 (board B205): server text can carry 0x12-delimited EQ
				// links -- eqemu wraps names in Saylink::Silent() -- and the
				// 45-character Titanium link body was reaching the player's
				// chat line verbatim. See client/say_link_text.h.
				ctx_.out.System(eqt::StripSayLinks(message));
			}
		}
	}
}

void eqt::zone::ZoneComponent::OnBegging(const EQ::Net::Packet & p)
{
	// Begging response - BeggingResponse_Struct (20 bytes after opcode)
	if (p.Length() >= 22) {
		uint32_t result = p.GetUInt32(14);  // offset 12 in struct + 2 for opcode
		uint32_t amount = p.GetUInt32(18);  // offset 16 in struct + 2 for opcode
		// Result: 0=Fail, 1=Plat, 2=Gold, 3=Silver, 4=Copper
		if (result == 0) {
			ctx_.out.System("You have been unable to convince your target to give you money.");
		} else {
			const char* coin_types[] = {"", "platinum", "gold", "silver", "copper"};
			if (result <= 4) {
				ctx_.out.System(fmt::format("You receive {} {}.", amount, coin_types[result]));
			}
		}
	}
}

void eqt::zone::ZoneComponent::OnHide(const EQ::Net::Packet & p)
{
	// Hide skill response - typically no data, success indicated by SpawnAppearance
	LOG_DEBUG(MOD_MAIN, "Hide response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnSneak(const EQ::Net::Packet & p)
{
	// Sneak skill response
	LOG_DEBUG(MOD_MAIN, "Sneak response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnSenseHeading(const EQ::Net::Packet & p)
{
	// Sense Heading response - may contain heading info
	LOG_DEBUG(MOD_MAIN, "SenseHeading response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnForage(const EQ::Net::Packet & p)
{
	// Forage response - contains item found or failure
	LOG_DEBUG(MOD_MAIN, "Forage response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnFishing(const EQ::Net::Packet & p)
{
	// Fishing response
	LOG_DEBUG(MOD_MAIN, "Fishing response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnMend(const EQ::Net::Packet & p)
{
	// Mend response
	LOG_DEBUG(MOD_MAIN, "Mend response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnFeignDeath(const EQ::Net::Packet & p)
{
	// Feign Death response
	LOG_DEBUG(MOD_MAIN, "FeignDeath response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnPickPocket(const EQ::Net::Packet & p)
{
	// Pick Pocket response
	LOG_DEBUG(MOD_MAIN, "PickPocket response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnSenseTraps(const EQ::Net::Packet & p)
{
	// Sense Traps response
	LOG_DEBUG(MOD_MAIN, "SenseTraps response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnDisarmTraps(const EQ::Net::Packet & p)
{
	// Disarm Traps response
	LOG_DEBUG(MOD_MAIN, "DisarmTraps response received: {} bytes", p.Length());
}

void eqt::zone::ZoneComponent::OnInstillDoubt(const EQ::Net::Packet & p)
{
	// Intimidation response
	LOG_DEBUG(MOD_MAIN, "InstillDoubt response received: {} bytes", p.Length());
}

const std::unordered_map<uint16_t, eqt::zone::ZoneComponent::ZoneArmHandler>&
eqt::zone::ZoneComponent::ZoneOpcodeTable()
{
	static const std::unordered_map<uint16_t, ZoneArmHandler> kZoneOpcodeTable = {
		{HC_OP_SessionReady, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnSessionReady(p); }},
		{HC_OP_PlayerProfile, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ZoneProcessPlayerProfile(p); }},
		{HC_OP_ZoneEntry, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnZoneEntry(p); }},
		{HC_OP_ZoneSpawns, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessZoneSpawns(p); }},
		{HC_OP_TimeOfDay, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessTimeOfDay(p); }},
		{HC_OP_TributeUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessTributeUpdate(p); }},
		{HC_OP_TributeTimer, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessTributeTimer(p); }},
		{HC_OP_CharInventory, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessCharInventory(p); }},
		{HC_OP_Weather, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessWeather(p); }},
		{HC_OP_NewZone, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ZoneProcessNewZone(p); }},
		{HC_OP_SendAATable, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessSendAATable(p); }},
		{HC_OP_RespondAA, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessRespondAA(p); }},
		{HC_OP_TributeInfo, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessTributeInfo(p); }},
		{HC_OP_SendGuildTributes, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessSendGuildTributes(p); }},
		{HC_OP_SpawnDoor, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessSpawnDoor(p); }},
		{HC_OP_GroundSpawn, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessGroundSpawn(p); }},
		{HC_OP_ClickObjectAction, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessClickObjectAction(p); }},
		{HC_OP_TradeSkillCombine, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessTradeSkillCombine(p); }},
		{HC_OP_SendZonepoints, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessSendZonepoints(p); }},
		{HC_OP_SendAAStats, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessSendAAStats(p); }},
		{HC_OP_SendExpZonein, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessSendExpZonein(p); }},
		{HC_OP_WorldObjectsSent, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessWorldObjectsSent(p); }},
		{HC_OP_SpawnAppearance, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessSpawnAppearance(p); }},
		{HC_OP_ExpUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessExpUpdate(p); }},
		{HC_OP_RaidUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessRaidUpdate(p); }},
		{HC_OP_GuildMOTD, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ZoneProcessGuildMOTD(p); }},
		{HC_OP_NewSpawn, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessNewSpawn(p); }},
		{HC_OP_ClientUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessClientUpdate(p); }},
		{HC_OP_DeleteSpawn, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessDeleteSpawn(p); }},
		{HC_OP_MobHealth, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessMobHealth(p); }},
		{HC_OP_HPUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessHPUpdate(p); }},
		{HC_OP_ChannelMessage, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Chat().ZoneProcessChannelMessage(p); }},
		{HC_OP_WearChange, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessWearChange(p); }},
		{HC_OP_Illusion, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessIllusion(p); }},
		{HC_OP_MoveDoor, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Entities().ZoneProcessMoveDoor(p); }},
		{HC_OP_CompletedTasks, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessCompletedTasks(p); }},
		{HC_OP_TaskDescription, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTaskDescription(p); }},
		{HC_OP_TaskActivity, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTaskActivity(p); }},
		{HC_OP_ClearLeadershipAbilities, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnClearLeadershipAbilities(p); }},
		{HC_OP_ClearAA, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnClearAA(p); }},
		{HC_OP_AAExpUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnAAExpUpdate(p); }},
		{HC_OP_DzCompass, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDzCompass(p); }},
		{HC_OP_DzExpeditionLockoutTimers, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDzExpeditionLockoutTimers(p); }},
		{HC_OP_BeginCast, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessBeginCast(p); }},
		{HC_OP_ManaChange, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessManaChange(p); }},
		{HC_OP_Buff, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessBuff(p); }},
		{HC_OP_ColoredText, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Chat().ZoneProcessColoredText(p); }},
		{HC_OP_FormattedMessage, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Chat().ZoneProcessFormattedMessage(p); }},
		{HC_OP_PlayerStateAdd, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessPlayerStateAdd(p); }},
		{HC_OP_Death, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDeath(p); }},
		{HC_OP_PlayerStateRemove, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessPlayerStateRemove(p); }},
		{HC_OP_Stamina, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessStamina(p); }},
		// Animation packet - applies to all entities (not just self)
		// This is used for combat animations, social animations (/wave), and
		// other action animations
		{HC_OP_Animation, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ZoneProcessAnimation(p); }},
		// T243: text emote broadcast ("Testchar waves at ...")
		{HC_OP_Emote, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Chat().ZoneProcessEmote(p); }},

		// Combat-related opcodes
		{HC_OP_Consider, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Commands().ZoneProcessConsider(p); }},  // OP_Consider response
		{HC_OP_Action, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Commands().ZoneProcessAction(p); }},  // OP_Action (combat/spell actions)
		{HC_OP_Damage, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Commands().ZoneProcessDamage(p); }},
		{HC_OP_MoneyOnCorpse, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Commands().ZoneProcessMoneyOnCorpse(p); }},
		{HC_OP_LootRequest, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnLootRequest(p); }},
		{HC_OP_LootItem, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnLootItem(p); }},
		{HC_OP_LootComplete, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnLootComplete(p); }},
		{HC_OP_ItemPacket, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnItemPacket(p); }},
		// T42: this whole block was #ifdef EQT_HAS_GRAPHICS. eq_merchant.cpp's own
		// header comment (P28) documents that ZoneProcessMoveItem/DeleteItem/
		// ShopRequest/ShopPlayerBuy/ShopPlayerSell/ShopEndConfirm/MoneyUpdate/
		// GMTraining are all "plain Player mode bridge-driven ... logic with no
		// Irrlicht/renderer usage anywhere in it" -- confirmed by reading each;
		// none touch a renderer. Left gated here, an EQT_GRAPHICS=OFF build never
		// dispatched vendor buy/sell, bank-adjacent move/delete, or money-update
		// packets at all, regardless of any publish fix inside them. Un-guarded;
		// no genuine graphics dependency found.
		{HC_OP_MoveItem, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessMoveItem(p); }},
		{HC_OP_DeleteItem, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessDeleteItem(p); }},
		{HC_OP_ShopRequest, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessShopRequest(p); }},
		{HC_OP_ShopPlayerBuy, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessShopPlayerBuy(p); }},
		{HC_OP_ShopPlayerSell, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessShopPlayerSell(p); }},
		{HC_OP_ShopEndConfirm, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessShopEndConfirm(p); }},
		{HC_OP_MoneyUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessMoneyUpdate(p); }},
		{HC_OP_GMTraining, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessGMTraining(p); }},
		{HC_OP_TargetCommandReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTargetCommandResponse(p); }},  // OP_TargetCommand response

		// Additional opcodes
		{HC_OP_BecomeCorpse, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnBecomeCorpse(p); }},
		{HC_OP_ZonePlayerToBind, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessZonePlayerToBind(p); }},
		{HC_OP_LevelUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessLevelUpdate(p); }},
		{HC_OP_ZoneChange, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ZoneProcessZoneChange(p); }},
		{HC_OP_RequestClientZoneChange, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ZoneProcessRequestClientZoneChange(p); }},
		{HC_OP_SimpleMessage, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Chat().ZoneProcessSimpleMessage(p); }},
		{HC_OP_TargetHoTT, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTargetHoTT(p); }},
		{HC_OP_SkillUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnSkillUpdate(p); }},
		{HC_OP_CancelTrade, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnCancelTrade(p); }},
		{HC_OP_TradeRequest, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTradeRequest(p); }},
		{HC_OP_TradeRequestAck, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTradeRequestAck(p); }},
		{HC_OP_TradeCoins, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTradeCoins(p); }},
		{HC_OP_TradeAcceptClick, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnTradeAcceptClick(p); }},
		{HC_OP_FinishTrade, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnFinishTrade(p); }},
		{HC_OP_PreLogoutReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnPreLogoutReply(p); }},
		{HC_OP_LogoutReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessLogoutReply(p); }},
		{HC_OP_RezzRequest, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessRezzRequest(p); }},
		{HC_OP_RezzComplete, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessRezzComplete(p); }},
		{HC_OP_WhoAllResponse, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessWhoAllResponse(p); }},
		{HC_OP_InspectRequest, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessInspectRequest(p); }},
		{HC_OP_InspectAnswer, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessInspectAnswer(p); }},
		{HC_OP_GuildInvite, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Guild().ZoneProcessGuildInvite(p); }},
		{HC_OP_GetGuildMOTDReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Guild().ZoneProcessGuildMOTDReply(p); }},
		{HC_OP_GuildMemberUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Guild().ZoneProcessGuildMemberUpdate(p); }},
		{HC_OP_GuildMemberAdd, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Guild().ZoneProcessGuildMemberAdd(p); }},
		// Phase 3 packet handlers
		{HC_OP_ConsentResponse, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessConsentResponse(p); }},
		{HC_OP_DenyResponse, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDenyResponse(p); }},
		{HC_OP_EnvDamage, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessEnvDamage(p); }},
		{HC_OP_DisciplineUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDisciplineUpdate(p); }},
		{HC_OP_DisciplineTimer, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDisciplineTimer(p); }},
		{HC_OP_BankerChange, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessBankerChange(p); }},
		{HC_OP_ClearObject, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessClearObject(p); }},
		{HC_OP_LFGAppearance, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessLFGAppearance(p); }},
		// Phase 4 packet handlers
		{HC_OP_RequestDuel, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessDuelRequest(p); }},
		{HC_OP_RecipeReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessRecipeReply(p); }},
		{HC_OP_RecipeAutoCombine, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessRecipeAutoCombine(p); }},
		{HC_OP_RandomReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessRandomReply(p); }},
		{HC_OP_FindPersonReply, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessFindPersonReply(p); }},
		{HC_OP_CameraEffect, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessCameraEffect(p); }},
		{HC_OP_PlayMP3, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessPlayMP3(p); }},
		{HC_OP_Sound, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessSound(p); }},
		{HC_OP_GMZoneRequest, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Gm().ZoneProcessGMZoneRequest(p); }},
		{HC_OP_GMFind, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Gm().ZoneProcessGMFind(p); }},
		{HC_OP_GMSummon, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Gm().ZoneProcessGMSummon(p); }},
		{HC_OP_MobRename, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnMobRename(p); }},
		{HC_OP_Stun, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnStun(p); }},
		{HC_OP_InterruptCast, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnInterruptCast(p); }},

		// Group packet handlers
		{HC_OP_GroupInvite, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessGroupInvite(p); }},
		// Alternate invite opcode - same handler
		{HC_OP_GroupInvite2, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessGroupInvite(p); }},
		{HC_OP_GroupFollow, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessGroupFollow(p); }},
		{HC_OP_GroupUpdate, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessGroupUpdate(p); }},
		{HC_OP_GroupDisband, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessGroupDisband(p); }},
		{HC_OP_GroupCancelInvite, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Group().ZoneProcessGroupCancelInvite(p); }},
		// Note: HC_OP_LFGAppearance handled earlier with ZoneProcessLFGAppearance
		{HC_OP_MemorizeSpell, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnMemorizeSpell(p); }},
		{HC_OP_SpecialMesg, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnSpecialMesg(p); }},

		// ========================================================================
		// Skill Response Handlers
		// ========================================================================
		{HC_OP_Begging, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnBegging(p); }},
		{HC_OP_Hide, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnHide(p); }},
		{HC_OP_Sneak, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnSneak(p); }},
		{HC_OP_SenseHeading, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnSenseHeading(p); }},
		{HC_OP_Forage, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnForage(p); }},
		{HC_OP_Fishing, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnFishing(p); }},
		{HC_OP_Mend, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnMend(p); }},
		{HC_OP_FeignDeath, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnFeignDeath(p); }},
		// Track response - contains list of trackable entities
		{HC_OP_Track, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessTrack(p); }},
		// Apply Poison response - success/failure from server
		{HC_OP_ApplyPoison, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessApplyPoison(p); }},
		{HC_OP_PickPocket, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnPickPocket(p); }},
		// Bind Wound reply: `type` carries the outcome (done / no target /
		// target gone / no bandage). T177 -- before this the player saw
		// nothing 10s after starting a bandage.
		{HC_OP_BindWound, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessBindWound(p); }},
		{HC_OP_SenseTraps, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnSenseTraps(p); }},
		{HC_OP_DisarmTraps, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnDisarmTraps(p); }},
		{HC_OP_InstillDoubt, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.OnInstillDoubt(p); }},
		{HC_OP_ReadBook, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Merchant().ZoneProcessReadBook(p); }},
		{HC_OP_PetBuffWindow, [](ZoneComponent& z, const EQ::Net::Packet& p) { z.ctx_.Player().ZoneProcessPetBuffWindow(p); }},
	};
	return kZoneOpcodeTable;
}

// Opcodes the client deliberately does nothing with. Each reason is the
// comment its former switch arm carried, verbatim. Being here (rather than
// absent) is what keeps them off the unhandled-opcode WARN.
const std::unordered_map<uint16_t, const char*>&
eqt::zone::ZoneComponent::IgnoredZoneOpcodes()
{
	static const std::unordered_map<uint16_t, const char*> kIgnoredOpcodes = {
		{HC_OP_SetGroupTarget, "Group target indicator - ignore for now"},
		{HC_OP_LinkedReuse, "Linked spell reuse timer - ignore for now"},
		// T469 (board B604): HC_OP_InterruptCast used to sit here as "spell
		// manager handles this via other packets". No other packet did -- see
		// OnInterruptCast.
	};
	return kIgnoredOpcodes;
}

void eqt::zone::ZoneComponent::DispatchZonePacket(const EQ::Net::Packet & p, bool reliable)
{
	// T491: everything a zone packet creates (entities, inventory, spells,
	// doors on the game side) is game state, not the network buffer it came in.
	eqt::memory::MemoryScope gsScope(eqt::memory::Category::GameState);
	// B919 (#238): the dispatch path's OWN `game_state` allocations (WILLEQ_
	// LEDGER_OWNERS=game_state) -- anything a handler that opens no row of its
	// own allocates under the GameState scope. The rows that DO open (gs.
	// entities, gs.doors, gs.world_objects, gs.event_queue, B941's gs.navmesh
	// /gs.collision_map/gs.zone_lines/gs.zone_sources) win per block.
	eqt::memory::StartupScope owner("gs.dispatch");
	auto opcode = p.GetUInt16(0);

	// B760 (issue #45): one line per zone packet, named by opcode, with
	// whether it came off the reliable in-order stream (rel=1) or was an
	// unreliable send delivered on arrival (rel=0). It is the wire record the
	// cast-tail measurements (refactor/B760/README.md) and the B630 analyzer
	// read; DEBUG keeps it out of a normal run.
	LOG_DEBUG(MOD_ZONE, "[ZonePacket] {} rel={}", eqt::strings::GetOpcodeName(opcode), reliable ? 1 : 0);

	// T482 (board B631): a confirmed cast released by keepcasting 0 is a
	// success only if the success tail's OP_MemorizeSpell follows; any other
	// RELIABLE packet first means StopCasting (B760: an unreliable one is not
	// in the ordered stream). SpellManager decides, before the arm.
	if (ctx_.spell_manager) {
		ctx_.spell_manager->noteZonePacket(opcode, reliable);
	}

	const auto& table = ZoneOpcodeTable();
	auto it = table.find(opcode);
	if (it != table.end()) {
		it->second(*this, p);
		return;
	}
	if (IgnoredZoneOpcodes().count(opcode) != 0) {
		return;
	}
	LOG_WARN(MOD_ZONE, "Unhandled zone opcode: {}", eqt::strings::GetOpcodeName(opcode));
}
