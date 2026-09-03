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


void EverQuest::DispatchZonePacket(const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);

	switch (opcode) {
	case HC_OP_SessionReady:
		LOG_DEBUG(MOD_ZONE, "Zone session established, sending ack and zone entry");
		// Send ack packet
		ZoneSendAckPacket();
		// Send zone entry
		ZoneSendZoneEntry();
		break;
	case HC_OP_PlayerProfile:
		ZoneProcessPlayerProfile(p);
		break;
	case HC_OP_ZoneEntry:
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
		break;
	case HC_OP_ZoneSpawns:
		ZoneProcessZoneSpawns(p);
		break;
	case HC_OP_TimeOfDay:
		ZoneProcessTimeOfDay(p);
		break;
	case HC_OP_TributeUpdate:
		ZoneProcessTributeUpdate(p);
		break;
	case HC_OP_TributeTimer:
		ZoneProcessTributeTimer(p);
		break;
	case HC_OP_CharInventory:
		m_merchant->ZoneProcessCharInventory(p);
		break;
	case HC_OP_Weather:
		ZoneProcessWeather(p);
		break;
	case HC_OP_NewZone:
		ZoneProcessNewZone(p);
		break;
	case HC_OP_SendAATable:
		ZoneProcessSendAATable(p);
		break;
	case HC_OP_RespondAA:
		ZoneProcessRespondAA(p);
		break;
	case HC_OP_TributeInfo:
		ZoneProcessTributeInfo(p);
		break;
	case HC_OP_SendGuildTributes:
		ZoneProcessSendGuildTributes(p);
		break;
	case HC_OP_SpawnDoor:
		ZoneProcessSpawnDoor(p);
		break;
	case HC_OP_GroundSpawn:
		ZoneProcessGroundSpawn(p);
		break;
	case HC_OP_ClickObjectAction:
		ZoneProcessClickObjectAction(p);
		break;
	case HC_OP_TradeSkillCombine:
		ZoneProcessTradeSkillCombine(p);
		break;
	case HC_OP_SendZonepoints:
		ZoneProcessSendZonepoints(p);
		break;
	case HC_OP_SendAAStats:
		ZoneProcessSendAAStats(p);
		break;
	case HC_OP_SendExpZonein:
		ZoneProcessSendExpZonein(p);
		break;
	case HC_OP_WorldObjectsSent:
		ZoneProcessWorldObjectsSent(p);
		break;
	case HC_OP_SpawnAppearance:
		ZoneProcessSpawnAppearance(p);
		break;
	case HC_OP_ExpUpdate:
		ZoneProcessExpUpdate(p);
		break;
	case HC_OP_RaidUpdate:
		m_group->ZoneProcessRaidUpdate(p);
		break;
	case HC_OP_GuildMOTD:
		ZoneProcessGuildMOTD(p);
		break;
	case HC_OP_NewSpawn:
		ZoneProcessNewSpawn(p);
		break;
	case HC_OP_ClientUpdate:
		ZoneProcessClientUpdate(p);
		break;
	case HC_OP_DeleteSpawn:
		ZoneProcessDeleteSpawn(p);
		break;
	case HC_OP_MobHealth:
		ZoneProcessMobHealth(p);
		break;
	case HC_OP_HPUpdate:
		ZoneProcessHPUpdate(p);
		break;
	case HC_OP_ChannelMessage:
		m_chat->ZoneProcessChannelMessage(p);
		break;
	case HC_OP_WearChange:
		ZoneProcessWearChange(p);
		break;
	case HC_OP_Illusion:
		ZoneProcessIllusion(p);
		break;
	case HC_OP_MoveDoor:
		ZoneProcessMoveDoor(p);
		break;
	case HC_OP_CompletedTasks:
		ZoneProcessCompletedTasks(p);
		break;
	case HC_OP_TaskDescription:
		// Task description data - not used in P1999
		LOG_DEBUG(MOD_ZONE, "Received TaskDescription ({} bytes)", p.Length());
		break;
	case HC_OP_TaskActivity:
		// Task activity update - not used in P1999
		LOG_DEBUG(MOD_ZONE, "Received TaskActivity ({} bytes)", p.Length());
		break;
	case HC_OP_ClearLeadershipAbilities:
		// Clear leadership AA on zone-in
		LOG_DEBUG(MOD_ZONE, "Received ClearLeadershipAbilities");
		break;
	case HC_OP_ClearAA:
		// Clear AA on zone-in (resets client state)
		LOG_DEBUG(MOD_ZONE, "Received ClearAA");
		break;
	case HC_OP_AAExpUpdate:
		// AA experience update
		LOG_DEBUG(MOD_ZONE, "Received AAExpUpdate ({} bytes)", p.Length());
		break;
	case HC_OP_DzCompass:
		ZoneProcessDzCompass(p);
		break;
	case HC_OP_DzExpeditionLockoutTimers:
		ZoneProcessDzExpeditionLockoutTimers(p);
		break;
	case HC_OP_BeginCast:
		ZoneProcessBeginCast(p);
		break;
	case HC_OP_ManaChange:
		ZoneProcessManaChange(p);
		break;
	case HC_OP_Buff:
		ZoneProcessBuff(p);
		break;
	case HC_OP_ColoredText:
		m_chat->ZoneProcessColoredText(p);
		break;
	case HC_OP_FormattedMessage:
		m_chat->ZoneProcessFormattedMessage(p);
		break;
	case HC_OP_PlayerStateAdd:
		ZoneProcessPlayerStateAdd(p);
		break;
	case HC_OP_Death:
		ZoneProcessDeath(p);
		break;
	case HC_OP_PlayerStateRemove:
		ZoneProcessPlayerStateRemove(p);
		break;
	case HC_OP_Stamina:
		ZoneProcessStamina(p);
		break;
	case HC_OP_Animation:
		// Animation packet - applies to all entities (not just self)
		// This is used for combat animations, social animations (/wave), and
		// other action animations
		ZoneProcessAnimation(p);
		break;
	case HC_OP_Emote:
		// T243: text emote broadcast ("Testchar waves at ...")
		m_chat->ZoneProcessEmote(p);
		break;
	
	// Combat-related opcodes
	case 0x65ca: // OP_Consider response
		ZoneProcessConsider(p);
		break;
	case HC_OP_Action: // OP_Action (combat/spell actions)
		ZoneProcessAction(p);
		break;
	case HC_OP_Damage:
		ZoneProcessDamage(p);
		break;
	case HC_OP_MoneyOnCorpse:
		ZoneProcessMoneyOnCorpse(p);
		break;
	case HC_OP_LootRequest:
		// Server responds to our loot request - confirms we can loot the corpse
		// The actual loot items come via ItemPacket (type 102) BEFORE this response
		if (p.Length() >= 6) {
			uint32_t corpse_id = p.GetUInt32(2);
			LOG_DEBUG(MOD_INVENTORY, "LootRequest response: corpseId={}", corpse_id);

			// Check if loot window is empty (no items to loot)
			// If empty, auto-complete the loot session immediately
			if (m_game_state.player().lootingCorpseId() != 0 && m_loot_items.empty()) {
				LOG_DEBUG(MOD_INVENTORY, "LootRequest response: No items on corpse, auto-completing loot");
				CloseLootWindow(static_cast<uint16_t>(corpse_id));
			}
		}
		break;
	case HC_OP_LootItem:
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
				auto it = std::find(m_pending_loot_slots.begin(), m_pending_loot_slots.end(), slot_id);
				if (it != m_pending_loot_slots.end()) {
					m_pending_loot_slots.erase(it);
				}
			}
		} else {
			LOG_DEBUG(MOD_INVENTORY, "LootItem ACK: short packet, len={}", p.Length());
		}
		break;
	case HC_OP_LootComplete:
		// Server signals end of loot session (rarely sent by P1999/Titanium servers)
		LOG_DEBUG(MOD_INVENTORY, "LootComplete received from server");
		m_pending_loot_slots.clear();
		break;
	case HC_OP_ItemPacket:
		// Item packets can be: vendor items (100), inventory items (103), or loot items
		// Check the packet type in the first 4 bytes after the header
		{
			uint32_t item_packet_type = 0;
			if (p.Length() >= 6) {
				item_packet_type = p.GetUInt32(2);
			}
			LOG_DEBUG(MOD_INVENTORY, "ItemPacket received, type={} m_game_state.player().lootingCorpseId()={} m_game_state.player().vendorNpcId()={} pending_slots={}",
				item_packet_type, m_game_state.player().lootingCorpseId(), m_game_state.player().vendorNpcId(), m_pending_loot_slots.size());
			// Check if this is a vendor item (type 100)
			if (item_packet_type == EQT::ITEM_PACKET_MERCHANT && m_game_state.player().vendorNpcId() != 0) {
				// Vendor item - add to vendor window via bridge
				m_merchant->ZoneProcessVendorItemToUI(p);
			} else if (m_game_state.player().lootingCorpseId() != 0) {
				// Looting (T268: every open path sets this). A pending slot means
				// this is the item we asked for; otherwise it populates the window.
				if (!m_pending_loot_slots.empty()) { m_merchant->ZoneProcessLootedItemToInventory(p); }
				else { m_merchant->ZoneProcessLootItemToUI(p); }
			} else {
				// Check if this is a trade partner item.
				// T42: was #ifdef EQT_HAS_GRAPHICS -- TradeManager and
				// InventoryManager are both renderer-free (P28/this file's
				// own header comment), so an EQT_GRAPHICS=OFF build was
				// dropping regular inventory item packets (vendor buy, bank
				// window population) entirely. Un-guarded; no genuine
				// graphics dependency found.
				bool handled_as_trade = false;
				if (m_trade_manager && m_trade_manager->isTrading()) {
					handled_as_trade = m_merchant->ZoneProcessTradePartnerItem(p);
				}
				if (!handled_as_trade) {
					// Regular inventory item packet - pass to inventory manager.
					// This is the single choke point for vendor-buy item
					// placement and bank-window population (T41's Review
					// traced both here) -- publish InventorySlotChanged here
					// so both get the UI update, not just the game-side model.
					if (m_inventory_manager) {
						int16_t changedSlot = m_inventory_manager->processItemPacket(p);
						if (changedSlot != eqt::inventory::SLOT_INVALID) {
							const auto* item = m_inventory_manager->getItem(changedSlot);
							m_merchant->SyncInventorySlotToGameState(changedSlot,
								item != nullptr && item->itemId != 0);  // T221
						}
						if (m_bridge && changedSlot != eqt::inventory::SLOT_INVALID) {
							eqt::state::InventorySlotChangedData data;
							data.slotId = changedSlot;
							const auto* item = m_inventory_manager->getItem(changedSlot);
							data.hasItem = (item != nullptr && item->itemId != 0);
							data.itemId = data.hasItem ? item->itemId : 0;
							data.itemName = data.hasItem ? item->name : "";
							data.iconId = data.hasItem ? item->icon : 0;
							data.quantity = data.hasItem ? item->quantity : 0;
							LOG_DEBUG(MOD_INVENTORY, "Publishing InventorySlotChanged (generic ItemPacket fallback): slot={} hasItem={} itemId={} name='{}' qty={}",
								data.slotId, data.hasItem, data.itemId, data.itemName, data.quantity);
							m_bridge->pushEvent(eqt::state::GameEvent(
								eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
						}
					}
				}
				if (s_debug_level >= 2) {
					LOG_DEBUG(MOD_MAIN, "Received ItemPacket type={} while not looting/trading", item_packet_type);
				}
			}
		}
		break;
	// T42: this whole block was #ifdef EQT_HAS_GRAPHICS. eq_merchant.cpp's own
	// header comment (P28) documents that ZoneProcessMoveItem/DeleteItem/
	// ShopRequest/ShopPlayerBuy/ShopPlayerSell/ShopEndConfirm/MoneyUpdate/
	// GMTraining are all "plain Player mode bridge-driven ... logic with no
	// Irrlicht/renderer usage anywhere in it" -- confirmed by reading each;
	// none touch a renderer. Left gated here, an EQT_GRAPHICS=OFF build never
	// dispatched vendor buy/sell, bank-adjacent move/delete, or money-update
	// packets at all, regardless of any publish fix inside them. Un-guarded;
	// no genuine graphics dependency found.
	case HC_OP_MoveItem:
		m_merchant->ZoneProcessMoveItem(p);
		break;
	case HC_OP_DeleteItem:
		m_merchant->ZoneProcessDeleteItem(p);
		break;
	case HC_OP_ShopRequest:
		m_merchant->ZoneProcessShopRequest(p);
		break;
	case HC_OP_ShopPlayerBuy:
		m_merchant->ZoneProcessShopPlayerBuy(p);
		break;
	case HC_OP_ShopPlayerSell:
		m_merchant->ZoneProcessShopPlayerSell(p);
		break;
	case HC_OP_ShopEndConfirm:
		m_merchant->ZoneProcessShopEndConfirm(p);
		break;
	case HC_OP_MoneyUpdate:
		m_merchant->ZoneProcessMoneyUpdate(p);
		break;
	case HC_OP_GMTraining:
		m_merchant->ZoneProcessGMTraining(p);
		break;
	case 0x61f9: // OP_TargetCommand response
		// Server may reject our target
		if (p.Length() >= 4) {
			uint16_t result = p.GetUInt16(2);
			if (result != 0 && s_debug_level >= 1) {
				LOG_DEBUG(MOD_MAIN, "Target rejected: {}", result);
			}
		}
		break;
	
	// Additional opcodes
	case HC_OP_BecomeCorpse:
		// Someone died and became a corpse
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received BecomeCorpse packet");
		}
		break;
	case HC_OP_ZonePlayerToBind:
		ZoneProcessZonePlayerToBind(p);
		break;
	case HC_OP_LevelUpdate:
		ZoneProcessLevelUpdate(p);
		break;
	case HC_OP_ZoneChange:
		ZoneProcessZoneChange(p);
		break;
	case HC_OP_RequestClientZoneChange:
		ZoneProcessRequestClientZoneChange(p);
		break;
	case HC_OP_SimpleMessage:
		m_chat->ZoneProcessSimpleMessage(p);
		break;
	case HC_OP_TargetHoTT:
		// Target's target information
		if (p.Length() >= 6) {
			uint16_t target_id = p.GetUInt16(2);
			uint16_t hott_id = p.GetUInt16(4);
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Target {} has target {}", target_id, hott_id);
			}
		}
		break;
	case HC_OP_SkillUpdate:
		// Skill level update
		// SkillUpdate_Struct: skillId (uint32) + value (uint32) = 8 bytes + 2 byte header
		if (p.Length() >= 10) {
			uint32_t skill_id_32 = p.GetUInt32(2);  // 4-byte skillId at offset 2
			uint32_t value = p.GetUInt32(6);        // 4-byte value at offset 6
			uint8_t skill_id = static_cast<uint8_t>(skill_id_32);  // Cast to uint8_t for skill manager

			// Update skill manager
			if (m_skill_manager) {
				m_skill_manager->updateSkill(skill_id, value);
			}

			// Decrement practice points if trainer window is open (training
			// succeeded). PlayerState is the ONE store (T283) and
			// decrementPracticePoints() already floors at zero.
			if (m_game_state.player().trainerNpcId() != 0) {
				m_game_state.player().decrementPracticePoints();
			}

			// Publish SkillValueChanged event to bridge
			if (m_bridge) {
				eqt::state::SkillValueChangedData sdata;
				sdata.skillId = static_cast<uint16_t>(skill_id);
				sdata.value = static_cast<uint16_t>(value);
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::SkillValueChanged, std::move(sdata)));
			}

			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Skill {} updated to {}", static_cast<int>(skill_id), value);
			}
		}
		break;
	case HC_OP_CancelTrade:
		// Trade cancelled
		if (p.Length() >= 2 + sizeof(EQT::CancelTrade_Struct)) {
			const EQT::CancelTrade_Struct* cancel = reinterpret_cast<const EQT::CancelTrade_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleCancelTrade(*cancel);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade cancelled by spawn {}", cancel->spawn_id);
			}
		}
		break;
	case HC_OP_TradeRequest:
		// Trade request from another player
		if (p.Length() >= 2 + sizeof(EQT::TradeRequest_Struct)) {
			const EQT::TradeRequest_Struct* req = reinterpret_cast<const EQT::TradeRequest_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				// Look up the name of the requesting player
				std::string from_name = "Unknown";
				auto it = m_entities.find(static_cast<uint16_t>(req->from_spawn_id));
				if (it != m_entities.end()) {
					from_name = it->second.name;
				}
				m_trade_manager->handleTradeRequest(*req, from_name);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade request from spawn {} to spawn {}",
				         req->from_spawn_id, req->target_spawn_id);
			}
		}
		break;
	case HC_OP_TradeRequestAck:
		// Trade request accepted
		if (p.Length() >= 2 + sizeof(EQT::TradeRequestAck_Struct)) {
			const EQT::TradeRequestAck_Struct* ack = reinterpret_cast<const EQT::TradeRequestAck_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeRequestAck(*ack);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade request ack from spawn {} to spawn {}",
				         ack->from_spawn_id, ack->target_spawn_id);
			}
		}
		break;
	case HC_OP_TradeCoins:
		// Trade partner changed money
		if (p.Length() >= 2 + sizeof(EQT::TradeCoins_Struct)) {
			const EQT::TradeCoins_Struct* coins = reinterpret_cast<const EQT::TradeCoins_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeCoins(*coins);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade coins from spawn {}: slot {} amount {}",
				         coins->spawn_id, coins->slot, coins->amount);
			}
		}
		break;
	case HC_OP_TradeAcceptClick:
		// Trade partner clicked accept
		if (p.Length() >= 2 + sizeof(EQT::TradeAcceptClick_Struct)) {
			const EQT::TradeAcceptClick_Struct* accept = reinterpret_cast<const EQT::TradeAcceptClick_Struct*>(p.Data() + 2);
			if (m_trade_manager) {
				m_trade_manager->handleTradeAcceptClick(*accept);
			}
			if (s_debug_level >= 2) {
				LOG_DEBUG(MOD_MAIN, "Trade accept click from spawn {}: accepted={}",
				         accept->spawn_id, accept->accepted);
			}
		}
		break;
	case HC_OP_FinishTrade:
		// Trade completed. OP_FinishTrade has a ZERO-LENGTH payload -- eqemu
		// sends `new EQApplicationPacket(OP_FinishTrade, 0)`
		// (zone/client_packet.cpp:15358). The 2 "unknown" bytes the old
		// EQT::FinishTrade_Struct declared were fictional; T147 deleted it.
		// Nothing changes behaviourally: the old code already fell back to a
		// zeroed struct when the length guard failed, which it always did.
		if (m_trade_manager) {
			m_trade_manager->handleFinishTrade();
		}
		LOG_INFO(MOD_MAIN, "Trade finished");
		break;
	case HC_OP_PreLogoutReply:
		// Pre-logout confirmation (server acknowledges camp request)
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Pre-logout reply received (camp acknowledged)");
		}
		break;
	case HC_OP_LogoutReply:
		ZoneProcessLogoutReply(p);
		break;
	case HC_OP_RezzRequest:
		ZoneProcessRezzRequest(p);
		break;
	case HC_OP_RezzComplete:
		ZoneProcessRezzComplete(p);
		break;
	case HC_OP_WhoAllResponse:
		ZoneProcessWhoAllResponse(p);
		break;
	case HC_OP_InspectRequest:
		ZoneProcessInspectRequest(p);
		break;
	case HC_OP_InspectAnswer:
		ZoneProcessInspectAnswer(p);
		break;
	case HC_OP_GuildInvite:
		m_guild->ZoneProcessGuildInvite(p);
		break;
	case HC_OP_GetGuildMOTDReply:
		m_guild->ZoneProcessGuildMOTDReply(p);
		break;
	case HC_OP_GuildMemberUpdate:
		m_guild->ZoneProcessGuildMemberUpdate(p);
		break;
	case HC_OP_GuildMemberAdd:
		m_guild->ZoneProcessGuildMemberAdd(p);
		break;
	// Phase 3 packet handlers
	case HC_OP_ConsentResponse:
		ZoneProcessConsentResponse(p);
		break;
	case HC_OP_DenyResponse:
		ZoneProcessDenyResponse(p);
		break;
	case HC_OP_EnvDamage:
		ZoneProcessEnvDamage(p);
		break;
	case HC_OP_DisciplineUpdate:
		ZoneProcessDisciplineUpdate(p);
		break;
	case HC_OP_DisciplineTimer:
		ZoneProcessDisciplineTimer(p);
		break;
	case HC_OP_BankerChange:
		ZoneProcessBankerChange(p);
		break;
	case HC_OP_ClearObject:
		ZoneProcessClearObject(p);
		break;
	case HC_OP_LFGAppearance:
		ZoneProcessLFGAppearance(p);
		break;
	// Phase 4 packet handlers
	case HC_OP_RequestDuel:
		ZoneProcessDuelRequest(p);
		break;
	case HC_OP_RecipeReply:
		m_merchant->ZoneProcessRecipeReply(p);
		break;
	case HC_OP_RecipeAutoCombine:
		m_merchant->ZoneProcessRecipeAutoCombine(p);
		break;
	case HC_OP_RandomReply:
		ZoneProcessRandomReply(p);
		break;
	case HC_OP_FindPersonReply:
		ZoneProcessFindPersonReply(p);
		break;
	case HC_OP_CameraEffect:
		ZoneProcessCameraEffect(p);
		break;
	case HC_OP_PlayMP3:
		ZoneProcessPlayMP3(p);
		break;
	case HC_OP_Sound:
		ZoneProcessSound(p);
		break;
	case HC_OP_GMZoneRequest:
		m_gm->ZoneProcessGMZoneRequest(p);
		break;
	case HC_OP_GMFind:
		m_gm->ZoneProcessGMFind(p);
		break;
	case HC_OP_GMSummon:
		m_gm->ZoneProcessGMSummon(p);
		break;
	case HC_OP_MobRename:
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
				if (m_entities.find(entity_id) == m_entities.end()) {
					// Id in the name doesn't resolve (truncated name, or a
					// server that numbers corpses differently) -- fall back to
					// matching the base name.
					for (auto& [id, entity] : m_entities) {
						if (entity.name == base_name + "001" || entity.name == base_name) {
							entity_id = id;
							break;
						}
					}
				}

				// Entity id 0 is never a real spawn here (it is the
				// unset/self sentinel); unchanged from the pre-T160 guard.
				auto it = entity_id > 0 ? m_entities.find(entity_id) : m_entities.end();
				if (it != m_entities.end()) {
					std::string old_name = it->second.name;
					it->second.name = new_name;
					it->second.is_corpse = true;
					// T268: GameState kept the LIVING name + isCorpse=false forever
					// (markAsCorpse had no caller) -- /target and /loot missed it.
					SyncEntityToGameState(it->second);
					if (s_debug_level >= 1) {
						LOG_INFO(MOD_ENTITY, "Entity {} became corpse: '{}' -> '{}'",
							entity_id, old_name, new_name);
					}
				}
			}
		}
		break;
	case HC_OP_Stun:
		// T247. eqemu Stun_Struct is 4 bytes: uint32 duration in MILLISECONDS
		// (Client::Stun -> stunned_timer.Start(duration)); Client::UnStun sends
		// duration 0. It is only ever sent to the stunned client itself, so
		// there is no target field -- the pre-T247 code read a target/duration
		// pair out of the low/high halves of the duration and stored nothing.
		if (p.Length() >= 6) {
			ZoneProcessStun(p.GetUInt32(2));
		}
		break;

	// Group packet handlers
	case HC_OP_GroupInvite:
		m_group->ZoneProcessGroupInvite(p);
		break;
	case HC_OP_GroupInvite2:
		// Alternate invite opcode - same handler
		m_group->ZoneProcessGroupInvite(p);
		break;
	case HC_OP_GroupFollow:
		m_group->ZoneProcessGroupFollow(p);
		break;
	case HC_OP_GroupUpdate:
		m_group->ZoneProcessGroupUpdate(p);
		break;
	case HC_OP_GroupDisband:
		m_group->ZoneProcessGroupDisband(p);
		break;
	case HC_OP_GroupCancelInvite:
		m_group->ZoneProcessGroupCancelInvite(p);
		break;
	case HC_OP_SetGroupTarget:
		// Group target indicator - ignore for now
		break;
	// Note: HC_OP_LFGAppearance handled earlier with ZoneProcessLFGAppearance
	case HC_OP_LinkedReuse:
		// Linked spell reuse timer - ignore for now
		break;
	case HC_OP_MemorizeSpell:
		{
			// MemorizeSpell_Struct: slot(4), spell_id(4), scribing(4), unknown0(4)
			// Total: 16 bytes after opcode (2 bytes)
			if (p.Length() < 18) {
				LOG_WARN(MOD_SPELL, "MemorizeSpell packet too short: {} bytes", p.Length());
				break;
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
				if (m_spell_manager->scribeSpell(spell_id, static_cast<uint16_t>(slot))) {
					// Get spell name for message
					const EQ::SpellData* spellData = m_spell_manager->getSpell(spell_id);
					std::string spellName = spellData ? spellData->name : "spell";

					AddChatSystemMessage(fmt::format("You have learned {}!", spellName));
					LOG_INFO(MOD_SPELL, "Successfully scribed spell {} ({}) to slot {}",
						spellName, spell_id, slot);

					// Refresh spellbook UI via bridge
					if (m_bridge) {
						eqt::state::SpellScribeCompletedData sdata;
						sdata.spellId = spell_id;
						sdata.slot = static_cast<uint16_t>(slot);
						sdata.spellName = spellName;
						sdata.iconId = spellData ? spellData->spell_icon : 0;
						m_bridge->pushEvent(eqt::state::GameEvent(
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
				if (m_inventory_manager) {
					m_inventory_manager->consumeScribedScroll();
				}

				// Clear pending scribe state
				m_pending_scribe_spell_id = 0;
				m_pending_scribe_book_slot = 0;
				m_pending_scribe_source_slot = -1;
			}
			// Handle gem slot memorization confirmations (slot < MAX_SPELL_GEMS)
			else if (slot < EQ::MAX_SPELL_GEMS && scribing == 0) {
				// Existing gem memorization - SpellManager handles this via other mechanisms
				LOG_DEBUG(MOD_SPELL, "Gem memorization confirmation: gem={} spell={}", slot, spell_id);
			}
		}
		break;
	case HC_OP_SpecialMesg:
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
					AddChatSystemMessage(message);
				}
			}
		}
		break;
	case HC_OP_InterruptCast:
		// Cast interrupted - spell manager handles this via other packets
		break;

	// ========================================================================
	// Skill Response Handlers
	// ========================================================================
	case HC_OP_Begging:
		// Begging response - BeggingResponse_Struct (20 bytes after opcode)
		if (p.Length() >= 22) {
			uint32_t result = p.GetUInt32(14);  // offset 12 in struct + 2 for opcode
			uint32_t amount = p.GetUInt32(18);  // offset 16 in struct + 2 for opcode
			// Result: 0=Fail, 1=Plat, 2=Gold, 3=Silver, 4=Copper
			if (result == 0) {
				AddChatSystemMessage("You have been unable to convince your target to give you money.");
			} else {
				const char* coin_types[] = {"", "platinum", "gold", "silver", "copper"};
				if (result <= 4) {
					AddChatSystemMessage(fmt::format("You receive {} {}.", amount, coin_types[result]));
				}
			}
		}
		break;
	case HC_OP_Hide:
		// Hide skill response - typically no data, success indicated by SpawnAppearance
		LOG_DEBUG(MOD_MAIN, "Hide response received: {} bytes", p.Length());
		break;
	case HC_OP_Sneak:
		// Sneak skill response
		LOG_DEBUG(MOD_MAIN, "Sneak response received: {} bytes", p.Length());
		break;
	case HC_OP_SenseHeading:
		// Sense Heading response - may contain heading info
		LOG_DEBUG(MOD_MAIN, "SenseHeading response received: {} bytes", p.Length());
		break;
	case HC_OP_Forage:
		// Forage response - contains item found or failure
		LOG_DEBUG(MOD_MAIN, "Forage response received: {} bytes", p.Length());
		break;
	case HC_OP_Fishing:
		// Fishing response
		LOG_DEBUG(MOD_MAIN, "Fishing response received: {} bytes", p.Length());
		break;
	case HC_OP_Mend:
		// Mend response
		LOG_DEBUG(MOD_MAIN, "Mend response received: {} bytes", p.Length());
		break;
	case HC_OP_FeignDeath:
		// Feign Death response
		LOG_DEBUG(MOD_MAIN, "FeignDeath response received: {} bytes", p.Length());
		break;
	case HC_OP_Track:
		// Track response - contains list of trackable entities
		ZoneProcessTrack(p);
		break;
	case HC_OP_ApplyPoison:
		// Apply Poison response - success/failure from server
		ZoneProcessApplyPoison(p);
		break;
	case HC_OP_PickPocket:
		// Pick Pocket response
		LOG_DEBUG(MOD_MAIN, "PickPocket response received: {} bytes", p.Length());
		break;
	case HC_OP_BindWound:
		// Bind Wound reply: `type` carries the outcome (done / no target /
		// target gone / no bandage). T177 -- before this the player saw
		// nothing 10s after starting a bandage.
		ZoneProcessBindWound(p);
		break;
	case HC_OP_SenseTraps:
		// Sense Traps response
		LOG_DEBUG(MOD_MAIN, "SenseTraps response received: {} bytes", p.Length());
		break;
	case HC_OP_DisarmTraps:
		// Disarm Traps response
		LOG_DEBUG(MOD_MAIN, "DisarmTraps response received: {} bytes", p.Length());
		break;
	case HC_OP_InstillDoubt:
		// Intimidation response
		LOG_DEBUG(MOD_MAIN, "InstillDoubt response received: {} bytes", p.Length());
		break;
	case HC_OP_ReadBook:
		m_merchant->ZoneProcessReadBook(p);
		break;
	case HC_OP_PetBuffWindow:
		ZoneProcessPetBuffWindow(p);
		break;

	default:
		LOG_WARN(MOD_ZONE, "Unhandled zone opcode: {}", GetOpcodeName(opcode));
		break;
	}
}

