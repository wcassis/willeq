// bridge_component_cursor.cpp
//
// B442 (GitLab #151): the game-thread half of the click-to-cursor gesture.
// BridgeComponent::onCursorGestureIntent() (bridge_component_intents.cpp,
// where the intent-table test requires every arm to live) relays here.
//
// One body for every producer: a slot click in the inventory, bank, loot,
// merchant and (later) bag/trade windows, the QuantityWnd's Accept, a purse
// row, and /pickup /putdown /coin. The decision is
// eqt::inventory::ApplyCursorGesture() / PlanCoinGesture()
// (src/client/inventory/cursor_gesture.cpp), which call the EXISTING
// InventoryManager pickup/place calls -- those push the MoveItemIntent(s) the
// next drain turns into OP_MoveItem.
//
// eqemu answers a successful OP_MoveItem / OP_MoveCoin with silence, so the
// slots the move changed are republished here from the model (the same
// enriched InventorySlotChanged the zone-in snapshot and ZoneProcessMoveItem
// publish). A refused move comes back from the server as a resync and is
// republished by that path. The renderer never moves an item by itself.
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/inventory/cursor_gesture.h"
#include "client/inventory/inventory_manager.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <fmt/format.h>

void eqt::bridge::BridgeComponent::PublishInventorySlot(int16_t slot) {
	const auto* item = ctx_.inventory_manager ? ctx_.inventory_manager->getItem(slot) : nullptr;
	eqt::state::InventorySlotChangedData data;
	data.slotId = slot;
	data.hasItem = (item != nullptr && item->itemId != 0);
	// T221: the source-side occupancy write precedes the bridge guard.
	ctx_.Merchant().SyncInventorySlotToGameState(slot, data.hasItem);
	if (!ctx_.bridge) {
		return;
	}
	data.itemId = data.hasItem ? item->itemId : 0;
	data.itemName = data.hasItem ? item->name : "";
	data.iconId = data.hasItem ? item->icon : 0;
	data.quantity = data.hasItem ? static_cast<uint32_t>(item->quantity) : 0;
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
}

void eqt::bridge::BridgeComponent::RunCursorGesture(const eqt::events::CursorGestureIntent& i) {
	if (!ctx_.inventory_manager) {
		if (i.echo) {
			ctx_.out.System("Inventory not available.");
		}
		return;
	}
	auto& inv = *ctx_.inventory_manager;

	if (i.op == eqt::events::CursorGestureOp::CoinClick) {
		const eqt::inventory::CoinMovePlan plan = eqt::inventory::PlanCoinGesture(
			inv, i.coinType, i.quantity, BankCoinGet(i.coinType, /*bank=*/false));
		if (!plan.send) {
			LOG_DEBUG(MOD_INVENTORY, "Coin gesture refused: {}", plan.message);
			if (i.echo) {
				ctx_.out.System(plan.message);
			}
			return;
		}
		// eqemu Client::OPMoveCoin: slot 0 = cursor, 1 = inventory; same
		// type on both sides, so the server converts nothing.
		EQT::MoveCoin_Struct move;
		move.from_slot = plan.toCursor ? EQT::COINSLOT_INVENTORY : EQT::COINSLOT_CURSOR;
		move.to_slot = plan.toCursor ? EQT::COINSLOT_CURSOR : EQT::COINSLOT_INVENTORY;
		move.cointype1 = plan.coinType;
		move.cointype2 = plan.coinType;
		move.amount = plan.amount;
		ctx_.Merchant().SendMoveCoin(move);

		const uint8_t type = static_cast<uint8_t>(plan.coinType);
		const uint32_t purse = BankCoinGet(type, /*bank=*/false);
		if (plan.toCursor) {
			BankCoinSet(type, /*bank=*/false, purse - static_cast<uint32_t>(plan.amount));
			using MT = eqt::inventory::InventoryManager::CursorMoneyType;
			static constexpr MT kByType[4] = {MT::Copper, MT::Silver, MT::Gold, MT::Platinum};
			inv.pickupMoney(kByType[type], static_cast<uint32_t>(plan.amount));
		} else {
			BankCoinSet(type, /*bank=*/false, purse + static_cast<uint32_t>(plan.amount));
			inv.returnCursorMoney();
		}
		PublishBankCoinProjection(/*inventoryChanged=*/true);
		if (i.echo) {
			ctx_.out.System(plan.message);
		}
		return;
	}

	const eqt::inventory::CursorGestureOutcome outcome = eqt::inventory::ApplyCursorGesture(inv, i);
	LOG_DEBUG(MOD_INVENTORY, "Cursor gesture op={} slot={} qty={}: {} ({})",
		static_cast<int>(i.op), i.slot, i.quantity, outcome.success ? "ok" : "refused",
		outcome.message);
	for (const int16_t slot : outcome.touched) {
		PublishInventorySlot(slot);
	}
	if (outcome.success) {
		UpdateInventoryStats();
	}
	if (i.echo && !outcome.message.empty()) {
		ctx_.out.System(outcome.message);
	}
}
