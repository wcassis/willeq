// EverQuest trade-callback, loot and shop transaction cluster.
//
// Split out of eq_merchant.cpp (Batch J, unit J27), which was itself split
// out of eq.cpp (Batch F, unit F05): SetupTradeManagerCallbacks and the
// loot/vendor/bank/trade-window/tradeskill Setup*Callbacks, the loot
// transfer handlers, and the shop request/buy/sell/money/end handlers.
// Moved verbatim, no behavior change. See refactor/F05_merchant.md and
// refactor/J27_eq_merchant_split.md.

#include "client/eq.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/spell/spell_manager.h"
#include "client/skill/skill_manager.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "client/bridge/game_state_bridge.h"

// P28: renderer-free inventory data model, used unconditionally below
// (m_inventory_manager->processCharInventory).
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#endif

#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>

void EverQuest::SetupTradeManagerCallbacks()
{
	if (!m_trade_manager) {
		return;
	}

	// Set up network callbacks
	m_trade_manager->setSendTradeRequest([this](const EQT::TradeRequest_Struct& req) {
		SendTradeRequest(req);
	});

	m_trade_manager->setSendTradeRequestAck([this](const EQT::TradeRequestAck_Struct& ack) {
		SendTradeRequestAck(ack);
	});

	m_trade_manager->setSendMoveCoin([this](const EQT::MoveCoin_Struct& move) {
		SendMoveCoin(move);
	});

	m_trade_manager->setSendTradeAcceptClick([this](const EQT::TradeAcceptClick_Struct& accept) {
		SendTradeAcceptClick(accept);
	});

	m_trade_manager->setSendCancelTrade([this](const EQT::CancelTrade_Struct& cancel) {
		SendCancelTrade(cancel);
	});

	// Set up UI callbacks (via bridge events)
	//
	// T103: each lambda below now ALSO writes the same data directly into
	// m_game_state.trade() (EverQuest's OWN GameState instance), same-thread,
	// synchronous -- mirroring how SyncEntityToGameState() already writes
	// m_game_state.entities() directly (GetMaxWeight, eq_player_info.cpp) rather than
	// only through the cross-thread bridge. This is what EqActionHandler
	// reads (m_eq.GetGameState().trade()); the bridge events these lambdas
	// already pushed are, separately, what GameStateSyncBridge now also
	// consumes into Application::m_gameState.trade() (see
	// game_state_sync_bridge.cpp) for any render-side consumer. Two
	// instances, two mechanisms, one source of truth -- same shape T81
	// established for entities.
	m_trade_manager->setOnRequestReceived([this](uint32_t spawnId, const std::string& name) {
		m_game_state.trade().setPendingRequest(spawnId, name);
		if (m_bridge) {
			eqt::state::TradeRequestReceivedData tdata;
			tdata.spawnId = spawnId;
			tdata.name = name;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeRequestReceived, std::move(tdata)));
		}
	});

	m_trade_manager->setOnStateChanged([this](TradeState state) {
		// Update inventory manager NPC trade flag for item validation
		if (m_inventory_manager) {
			if (state == TradeState::Active) {
				m_inventory_manager->setNpcTrade(m_trade_manager->isNpcTrade());
			} else if (state == TradeState::None || state == TradeState::Completed) {
				m_inventory_manager->setNpcTrade(false);
			}
		}

		if (state == TradeState::Active) {
			// Publish TradeStarted
			m_game_state.trade().startTrade(
				static_cast<uint16_t>(m_trade_manager->getPartnerSpawnId()),
				m_trade_manager->getPartnerName(),
				m_trade_manager->isNpcTrade());
			if (m_bridge) {
				eqt::state::TradeStartedData tdata;
				tdata.partnerId = m_trade_manager->getPartnerSpawnId();
				tdata.partnerName = m_trade_manager->getPartnerName();
				tdata.isNpc = m_trade_manager->isNpcTrade();
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TradeStarted, std::move(tdata)));
			}
		} else if (state == TradeState::None) {
			// Only close on None state (cancel) - Completed is handled by m_on_completed
			m_game_state.trade().reset();
			if (m_bridge) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TradeCancelled, eqt::state::TradeCancelledData{}));
			}
		}
	});

	m_trade_manager->setOnItemUpdated([this](bool isOwn, int slot) {
		if (!isOwn) {
			// Partner item updated - publish to bridge and the projection.
			// Own items are displayed from inventory trade slots (not
			// tracked here -- see trade_state.h's class comment; this isOwn
			// branch is never actually taken in production, since nothing
			// calls TradeManager::addItemToTrade()).
			const auto* item = m_trade_manager->getPartnerItem(slot);
			uint32_t itemId = item ? item->itemId : 0;
			std::string itemName = item ? item->name : "";
			m_game_state.trade().setPartnerItem(slot, itemId, itemName);
			if (m_bridge) {
				eqt::state::TradeItemUpdatedData tdata;
				tdata.who = 1;  // partner
				tdata.slot = static_cast<uint8_t>(slot);
				tdata.itemId = itemId;
				tdata.itemName = itemName;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TradeItemUpdated, std::move(tdata)));
			}
		}
	});

	m_trade_manager->setOnMoneyUpdated([this](bool isOwn) {
		const auto& srcMoney = isOwn ? m_trade_manager->getOwnMoney() : m_trade_manager->getPartnerMoney();
		eqt::state::TradeMoney projMoney;
		projMoney.platinum = srcMoney.platinum;
		projMoney.gold = srcMoney.gold;
		projMoney.silver = srcMoney.silver;
		projMoney.copper = srcMoney.copper;
		m_game_state.trade().setMoney(isOwn, projMoney);
		if (m_bridge) {
			eqt::state::TradeMoneyUpdatedData tdata;
			tdata.who = isOwn ? 0 : 1;
			tdata.platinum = srcMoney.platinum;
			tdata.gold = srcMoney.gold;
			tdata.silver = srcMoney.silver;
			tdata.copper = srcMoney.copper;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeMoneyUpdated, std::move(tdata)));
		}
	});

	m_trade_manager->setOnAcceptStateChanged([this](bool ownAccepted, bool partnerAccepted) {
		m_game_state.trade().setAcceptState(ownAccepted, partnerAccepted);
		if (m_bridge) {
			eqt::state::TradeAcceptStateChangedData tdata;
			tdata.ownAccepted = ownAccepted;
			tdata.partnerAccepted = partnerAccepted;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeAcceptStateChanged, std::move(tdata)));
		}
	});

	m_trade_manager->setOnCompleted([this]() {
		// Generate chat messages for what was exchanged
		std::string partnerName = EQT::toDisplayName(m_trade_manager->getPartnerName());
		const auto& partnerMoney = m_trade_manager->getPartnerMoney();
		const auto& ownMoney = m_trade_manager->getOwnMoney();

		// Report money received from partner
		if (partnerMoney.platinum > 0) {
			AddChatSystemMessage(fmt::format("You receive {} platinum from {}.", partnerMoney.platinum, partnerName));
		}
		if (partnerMoney.gold > 0) {
			AddChatSystemMessage(fmt::format("You receive {} gold from {}.", partnerMoney.gold, partnerName));
		}
		if (partnerMoney.silver > 0) {
			AddChatSystemMessage(fmt::format("You receive {} silver from {}.", partnerMoney.silver, partnerName));
		}
		if (partnerMoney.copper > 0) {
			AddChatSystemMessage(fmt::format("You receive {} copper from {}.", partnerMoney.copper, partnerName));
		}

		// Report items received from partner
		for (int i = 0; i < 8; i++) {
			const auto* item = m_trade_manager->getPartnerItem(i);
			if (item) {
				AddChatSystemMessage(fmt::format("You receive {} from {}.", item->name, partnerName));
			}
		}

		// Report money given to partner
		if (ownMoney.platinum > 0) {
			AddChatSystemMessage(fmt::format("You give {} platinum to {}.", ownMoney.platinum, partnerName));
		}
		if (ownMoney.gold > 0) {
			AddChatSystemMessage(fmt::format("You give {} gold to {}.", ownMoney.gold, partnerName));
		}
		if (ownMoney.silver > 0) {
			AddChatSystemMessage(fmt::format("You give {} silver to {}.", ownMoney.silver, partnerName));
		}
		if (ownMoney.copper > 0) {
			AddChatSystemMessage(fmt::format("You give {} copper to {}.", ownMoney.copper, partnerName));
		}

		// Report items given to partner
		for (int i = 0; i < 8; i++) {
			const auto* item = m_trade_manager->getOwnItem(i);
			if (item) {
				AddChatSystemMessage(fmt::format("You give {} to {}.", item->name, partnerName));
			}
		}

		// Publish TradeCompleted event to bridge. Read from the source
		// TradeManager above (still populated -- TradeManager::
		// resetTradeState() hasn't run yet at this point in
		// handleFinishTrade()'s sequence), then clear the projection now
		// that the summary has been read out of it.
		m_game_state.trade().reset();
		if (m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeCompleted, eqt::state::TradeCompletedData{}));
		}
	});

	m_trade_manager->setOnCancelled([this](TradeEndReason reason, const std::string& requesterName) {
		// T222: exactly one chat line per end reason -- declineTrade()
		// no longer prints its own.
		if (reason == TradeEndReason::DeclinedLocal) {
			AddChatSystemMessage(fmt::format("Declined trade request from {}.",
				EQT::toDisplayName(requesterName)));
		} else {
			AddChatSystemMessage("Trade cancelled.");
		}

		// Publish TradeCancelled event to bridge
		m_game_state.trade().reset();
		if (m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeCancelled, eqt::state::TradeCancelledData{}));
		}
	});
}

void EverQuest::ZoneProcessLootItemToUI(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootItemToUI called, packet length={}", p.Length());

	// Parse the serialized item data for the loot window
	// The packet contains pipe-delimited item data
	if (p.Length() < 10) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootItemToUI: Packet too short, ignoring");
		return;
	}

	// Skip the first 2 bytes (opcode is already removed, but there's a header)
	const char* data = (const char*)p.Data() + 2;
	int data_len = p.Length() - 2;

	// Skip the first 4 bytes of the data (unknown header)
	if (data_len > 4) {
		data += 4;
		data_len -= 4;
	}

	std::string item_data(data, data_len);

	if (s_debug_level >= 3) {
		LOG_TRACE(MOD_INVENTORY, "Item data length={}", item_data.length());
		LOG_TRACE(MOD_INVENTORY, "Item data preview: {}", item_data.substr(0, std::min(size_t(200), item_data.length())));
	}

	// Parse the pipe-delimited fields to extract item info
	std::vector<std::string> fields;
	std::stringstream ss(item_data);
	std::string field;
	while (std::getline(ss, field, '|')) {
		fields.push_back(field);
	}

	if (s_debug_level >= 3) {
		LOG_TRACE(MOD_INVENTORY, "Parsed {} fields", fields.size());
		for (size_t i = 0; i < fields.size() && i < 20; i++) {
			LOG_TRACE(MOD_INVENTORY, "  field[{}] = '{}'", i, fields[i].substr(0, 50));
		}
	}

	// The format is: 1|0|slot|0|1|0|icon|0|0|0|0|"0|item_name|lore_name|model|item_id|...
	// field[2] = slot number
	// field[6] = icon ID
	// field[12] = item name
	if (fields.size() > 15) {
		try {
			int16_t slot_num = static_cast<int16_t>(std::stoi(fields[2]));  // Slot number in loot window
			std::string item_name = fields[12];         // Item name (was incorrectly field[11])

			LOG_DEBUG(MOD_INVENTORY, "Creating loot item: slot={} name='{}'", slot_num, item_name);

			// Parse item data and publish via bridge (D18a: bridge is sole path to renderer)
			uint32_t itemId = 0;
			uint32_t icon = 0;

			// Parse icon from static data field[11] (absolute field[22])
			try {
				if (fields.size() > 22) {
					icon = static_cast<uint32_t>(std::stoi(fields[22]));
					LOG_TRACE(MOD_INVENTORY, "  icon={} (from field[22])", icon);
				}
			} catch (...) {
				LOG_DEBUG(MOD_INVENTORY, "  icon parsing failed for field[22]='{}'",
					(fields.size() > 22 ? fields[22] : "N/A"));
			}

			// Parse item ID from static data field[4] (absolute field[15])
			try {
				itemId = static_cast<uint32_t>(std::stoi(fields[15]));
				LOG_TRACE(MOD_INVENTORY, "  itemId={} (from field[15])", itemId);
			} catch (...) {}

			// Track loot item in game state (D18a: game-side loot item tracking)
			m_loot_items[slot_num] = GameLootItem{itemId, item_name};

			// T268: feed the auto-loot queue too (this replaces the deleted
			// headless-only ZoneProcessLootItem). CheckAutoLoot() is a
			// non-blocking nudge; UpdateLootState() decides on the game tick.
			if (m_combat_manager) {
				m_combat_manager->AddLootItem(static_cast<uint32_t>(slot_num));
				m_combat_manager->CheckAutoLoot();
			}

			// Publish LootItemAdded
			if (m_bridge) {
				eqt::state::LootItemAddedData ldata;
				ldata.corpseId = m_game_state.player().lootingCorpseId();
				ldata.slot = static_cast<uint8_t>(slot_num);
				ldata.itemId = itemId;
				ldata.itemName = item_name;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::LootItemAdded, std::move(ldata)));
			}
		} catch (const std::exception& e) {
			LOG_WARN(MOD_INVENTORY, "Failed to parse loot item data: {}", e.what());
		}
	} else {
		LOG_DEBUG(MOD_INVENTORY, "Not enough fields ({}), need > 15", fields.size());
	}
}

void EverQuest::ZoneProcessLootedItemToInventory(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory called, pending_slots={}", m_pending_loot_slots.size());

	// This is called when an item is being looted from a corpse to inventory
	// The item packet format is the same as ZoneProcessLootItemToUI

	// T42: processItemPacket() is the same choke point the generic
	// eq_zone_dispatch.cpp
	// ItemPacket fallback uses for vendor-buy/bank-window placement; publish
	// InventorySlotChanged here too (not one of T41's three named paths, but
	// the same gap -- a looted item landing in inventory never told the UI
	// either) using its returned slot rather than duplicating parse logic.
	auto publishLootedSlot = [this](int16_t slot) {
		if (slot == eqt::inventory::SLOT_INVALID) {
			return;
		}
		eqt::state::InventorySlotChangedData data;
		data.slotId = slot;
		const auto* item = m_inventory_manager ? m_inventory_manager->getItem(slot) : nullptr;
		data.hasItem = (item != nullptr && item->itemId != 0);
		SyncInventorySlotToGameState(slot, data.hasItem);  // T221
		if (!m_bridge) {
			return;
		}
		data.itemId = data.hasItem ? item->itemId : 0;
		data.itemName = data.hasItem ? item->name : "";
		data.iconId = data.hasItem ? item->icon : 0;
		data.quantity = data.hasItem ? item->quantity : 0;
		LOG_DEBUG(MOD_INVENTORY, "Publishing InventorySlotChanged (loot-to-inventory): slot={} hasItem={} itemId={} name='{}' qty={}",
			data.slotId, data.hasItem, data.itemId, data.itemName, data.quantity);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	};

	if (m_pending_loot_slots.empty()) {
		// No pending loot - shouldn't happen, but handle gracefully
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory: No pending loot slots! Treating as inventory item.");
		if (m_inventory_manager) {
			publishLootedSlot(m_inventory_manager->processItemPacket(p));
		}
		return;
	}

	// Get the slot we're expecting to receive
	int16_t expected_slot = m_pending_loot_slots.front();
	m_pending_loot_slots.erase(m_pending_loot_slots.begin());

	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory: Processing looted item for corpse slot {}, remaining pending={}",
		expected_slot, m_pending_loot_slots.size());

	// Add item to player's inventory
	if (m_inventory_manager) {
		LOG_TRACE(MOD_INVENTORY, "Adding item to inventory via processItemPacket");
		publishLootedSlot(m_inventory_manager->processItemPacket(p));
	}

	// Remove from game-side loot tracking and publish event
	m_loot_items.erase(expected_slot);

	if (m_bridge) {
		eqt::state::LootItemRemovedData data;
		data.corpseId = m_game_state.player().lootingCorpseId();
		data.slot = static_cast<uint8_t>(expected_slot);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootItemRemoved, std::move(data)));
	}
	LOG_TRACE(MOD_INVENTORY, "Removed item from loot window slot {}", expected_slot);

	// T268: keep the auto-loot queue in step (a hand-taken slot must not be
	// re-requested by a later /lootall). Loot-all sequencing itself is
	// CombatManager's LootingItems state -- the confirmation-driven
	// m_loot_all_* loop that used to continue here was the drifted duplicate.
	if (m_combat_manager) {
		m_combat_manager->RemoveLootItem(static_cast<uint32_t>(expected_slot));
	}
}

void EverQuest::SetupLootCallbacks()
{
	/* D20a: intents handle loot actions */
}

void EverQuest::SetupVendorCallbacks()
{
	/* D20a: intents handle vendor actions */
}

void EverQuest::SetupBankCallbacks()
{
	/* D20a: intents handle bank actions */
}

void EverQuest::SetupTradeWindowCallbacks()
{
	// D20e3: Trade window init moved to InitGraphics (needs renderer)
	// This stub remains for the call in InitGraphics; actual init is done there.
}

void EverQuest::SetupTradeskillCallbacks()
{
	/* D20b1: WindowManager pushes TradeskillCombineIntent/TradeskillCloseIntent via bridge */
}

void EverQuest::ZoneProcessShopRequest(const EQ::Net::Packet &p)
{
	// Server response to our shop request
	// Wire layout: npc_id(4), player_id(4), action(4), sell_rate(4) -- eqemu's
	// Merchant_Click_Struct (npcid/playerid/command/rate), used for OP_ShopRequest
	// in both directions. T147 deleted the dead EQT::Merchant_Open_Struct that
	// duplicated this; EQT::Merchant_Click_Struct is the one to use.
	if (p.Length() < 18) {
		LOG_WARN(MOD_INVENTORY, "ShopRequest packet too short: {} bytes", p.Length());
		return;
	}

	uint32_t npc_id = p.GetUInt32(2);
	uint32_t action = p.GetUInt32(10);
	float sell_rate = 1.0f;
	// Read sell_rate as float from bytes 14-17
	if (p.Length() >= 18) {
		uint32_t sell_rate_bits = p.GetUInt32(14);
		memcpy(&sell_rate, &sell_rate_bits, sizeof(float));
	}

	LOG_DEBUG(MOD_INVENTORY, "ShopRequest response: npc_id={} action={} sell_rate={:.4f}", npc_id, action, sell_rate);

	if (action == 1) {
		// Success - vendor is open
		const uint16_t vendorNpcId = static_cast<uint16_t>(npc_id);

		// Get vendor name from entity if available (converted to display format)
		auto it = m_entities.find(vendorNpcId);
		const std::string vendorName = (it != m_entities.end())
			? EQT::toDisplayName(it->second.name)
			: std::string("Merchant");

		// T227: source write, ABOVE the bridge guard. Until stage 3 only the
		// GameStateSyncBridge projection wrote player().vendor; the projection
		// is gone, so the handler that owns the data writes it.
		// T286: it is now the ONLY write -- EverQuest keeps no vendor copy.
		m_game_state.player().setVendor(vendorNpcId, sell_rate, vendorName);

		// Publish VendorWindowOpened (D18a: bridge is sole path to renderer)
		if (m_bridge) {
			eqt::state::WindowOpenedData data;
			data.npcId = m_game_state.player().vendorNpcId();
			data.npcName = m_game_state.player().vendorName();
			data.sellRate = m_game_state.player().vendorSellRate();
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::VendorWindowOpened, std::move(data)));
		}

#ifdef WITH_AUDIO
		// Start vendor/bank music (context-based, will restore on close)
		if (m_audio_manager) {
			m_audio_manager->startVendorBankMusic();
		}
#endif

		LOG_INFO(MOD_INVENTORY, "Opened vendor window for {} (id={})", m_game_state.player().vendorName(), m_game_state.player().vendorNpcId());
	} else {
		LOG_WARN(MOD_INVENTORY, "Vendor open failed: action={}", action);
	}
}

void EverQuest::ZoneProcessShopPlayerBuy(const EQ::Net::Packet &p)
{
	// Server confirmation of purchase
	// Merchant_Purchase_Struct: npc_id(4), player_id(4), itemslot(4), unknown12(4), quantity(4), action(4)
	if (p.Length() < 26) {
		LOG_WARN(MOD_INVENTORY, "ShopPlayerBuy packet too short: {} bytes", p.Length());
		return;
	}

	uint32_t npc_id = p.GetUInt32(2);
	uint32_t player_id = p.GetUInt32(6);
	uint32_t itemslot = p.GetUInt32(10);
	uint32_t quantity = p.GetUInt32(18);
	uint32_t action = p.GetUInt32(22);

	LOG_DEBUG(MOD_INVENTORY, "ShopPlayerBuy: npc_id={} player_id={} slot={} qty={} action={}",
		npc_id, player_id, itemslot, quantity, action);

	// Display purchase confirmation and update money (D18a: uses game-side vendor items)
	auto vendorIt = m_vendor_items.find(itemslot);
	if (vendorIt != m_vendor_items.end()) {
		int32_t totalPrice = vendorIt->second.price * static_cast<int32_t>(quantity);
		std::string priceStr = FormatPrice(totalPrice);
		if (quantity > 1) {
			AddChatSystemMessage(fmt::format("You purchased {} x{} for {}", vendorIt->second.name, quantity, priceStr));
		} else {
			AddChatSystemMessage(fmt::format("You purchased {} for {}", vendorIt->second.name, priceStr));
		}

		// Deduct money from local currency (server confirms purchase = money spent)
		int64_t totalCopper = static_cast<int64_t>(m_game_state.player().platinum()) * 1000 +
		                      static_cast<int64_t>(m_game_state.player().gold()) * 100 +
		                      static_cast<int64_t>(m_game_state.player().silver()) * 10 +
		                      static_cast<int64_t>(m_game_state.player().copper());
		totalCopper -= totalPrice;
		if (totalCopper < 0) totalCopper = 0;

		// Convert back to currency (T286: PlayerState is the only store)
		const uint32_t newPlatinum = static_cast<uint32_t>(totalCopper / 1000);
		totalCopper %= 1000;
		const uint32_t newGold = static_cast<uint32_t>(totalCopper / 100);
		totalCopper %= 100;
		const uint32_t newSilver = static_cast<uint32_t>(totalCopper / 10);
		const uint32_t newCopper = static_cast<uint32_t>(totalCopper % 10);
		m_game_state.player().setCurrency(newPlatinum, newGold, newSilver, newCopper);

		LOG_DEBUG(MOD_INVENTORY, "Updated money: {}pp {}gp {}sp {}cp", m_game_state.player().platinum(), m_game_state.player().gold(), m_game_state.player().silver(), m_game_state.player().copper());
	}

	// ItemPacket with type 103 (inventory) handles the actual item placement
	// and stack quantity updates via InventoryManager::setItem()
}

void EverQuest::ZoneProcessShopPlayerSell(const EQ::Net::Packet &p)
{
	// Server confirmation of sale
	// Merchant_Sell_Response_Struct: npc_id(4), itemslot(4), quantity(4), price(4)
	if (p.Length() < sizeof(EQT::Merchant_Sell_Response_Struct) + 2) {
		LOG_WARN(MOD_INVENTORY, "ShopPlayerSell packet too short: {} bytes", p.Length());
		return;
	}

	auto* resp = reinterpret_cast<const EQT::Merchant_Sell_Response_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	LOG_INFO(MOD_INVENTORY, "Sold {} items from slot {} for {} copper",
		resp->quantity, resp->itemslot, resp->price);

	// Display sale confirmation and update inventory (D18a: no renderer dependency)
	{
		std::string priceStr = FormatPrice(static_cast<int32_t>(resp->price));
		if (resp->quantity > 1) {
			AddChatSystemMessage(fmt::format("You sold {} items for {}", resp->quantity, priceStr));
		} else {
			AddChatSystemMessage(fmt::format("You sold item for {}", priceStr));
		}

		// Remove item from local inventory
		int16_t invSlot = static_cast<int16_t>(resp->itemslot);
		if (m_inventory_manager) {
			const auto* item = m_inventory_manager->getItem(invSlot);
			if (item) {
				if (item->stackable && item->quantity > static_cast<int32_t>(resp->quantity)) {
					auto* mutableItem = m_inventory_manager->getItemMutable(invSlot);
					if (mutableItem) {
						mutableItem->quantity -= static_cast<int32_t>(resp->quantity);
						LOG_DEBUG(MOD_INVENTORY, "Reduced stack at slot {} to {} items",
							invSlot, mutableItem->quantity);
					}
				} else {
					m_inventory_manager->removeItem(invSlot);
					LOG_DEBUG(MOD_INVENTORY, "Removed item from slot {}", invSlot);
				}

				// Publish InventorySlotChanged (T42: vendor sell was a direct
				// mutation -- removeItem()/quantity decrement above -- with no
				// publish anywhere after it; the UI never learned the slot
				// changed). Re-read the slot post-mutation so a partial-stack
				// sell reports the remaining quantity and a full sell reports
				// empty, in one place regardless of which branch ran above.
				{
					eqt::state::InventorySlotChangedData data;
					data.slotId = invSlot;
					const auto* postItem = m_inventory_manager->getItem(invSlot);
					data.hasItem = (postItem != nullptr && postItem->itemId != 0);
					SyncInventorySlotToGameState(invSlot, data.hasItem);  // T221
					if (m_bridge) {
						data.itemId = data.hasItem ? postItem->itemId : 0;
						data.itemName = data.hasItem ? postItem->name : "";
						data.iconId = data.hasItem ? postItem->icon : 0;
						data.quantity = data.hasItem ? postItem->quantity : 0;
						LOG_DEBUG(MOD_INVENTORY, "Publishing InventorySlotChanged (vendor sell): slot={} hasItem={} itemId={} name='{}' qty={}",
							data.slotId, data.hasItem, data.itemId, data.itemName, data.quantity);
						m_bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
					}
				}
			}
		}
	}
}

void EverQuest::ZoneProcessMoneyUpdate(const EQ::Net::Packet &p)
{
	// Money update after transaction
	// MoneyUpdate_Struct: platinum(4), gold(4), silver(4), copper(4)
	if (p.Length() < sizeof(EQT::MoneyUpdate_Struct) + 2) {
		LOG_WARN(MOD_INVENTORY, "MoneyUpdate packet too short: {} bytes", p.Length());
		return;
	}

	auto* money = reinterpret_cast<const EQT::MoneyUpdate_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	// Update player money (T286: PlayerState is the only store)
	m_game_state.player().setCurrency(static_cast<uint32_t>(money->platinum),
	                                  static_cast<uint32_t>(money->gold),
	                                  static_cast<uint32_t>(money->silver),
	                                  static_cast<uint32_t>(money->copper));

	LOG_DEBUG(MOD_INVENTORY, "Money updated: {}pp {}gp {}sp {}cp",
		m_game_state.player().platinum(), m_game_state.player().gold(), m_game_state.player().silver(), m_game_state.player().copper());

	// Publish CurrencyChanged (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::CurrencyChangedData data;
		data.platinum = static_cast<int32_t>(m_game_state.player().platinum());
		data.gold = static_cast<int32_t>(m_game_state.player().gold());
		data.silver = static_cast<int32_t>(m_game_state.player().silver());
		data.copper = static_cast<int32_t>(m_game_state.player().copper());
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(data)));
	}
}

void EverQuest::ZoneProcessShopEndConfirm(const EQ::Net::Packet &p)
{
	// Server confirms vendor session ended
	LOG_DEBUG(MOD_INVENTORY, "ShopEndConfirm received, closing vendor window");

	// T286: the closed-window event must carry the id we were AT, so it is
	// captured before the (now single) store is cleared.
	const uint16_t closedVendorNpcId = m_game_state.player().vendorNpcId();

	// T227: source write (see ZoneProcessShopRequest).
	m_game_state.player().clearVendor();

	// Publish VendorWindowClosed (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = closedVendorNpcId;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VendorWindowClosed, std::move(data)));
	}

	m_vendor_items.clear();
}

