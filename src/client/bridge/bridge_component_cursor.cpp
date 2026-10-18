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
//
// B442b (#151, the remainder) added, all measured in
// review/UIGAP-ORACLE/151/gestures.tsv:
//   - ToggleBag (rows 19-21): a right click on a carried bag runs the body
//     /bag runs, CommandsComponent::RequestToggleBag().
//   - the coin feed (row 29): every cursor-coin change publishes
//     CursorCoinChanged, so the cursor draws the coin and the render thread
//     needs no "coin is held" shadow of its own; and the bank's coin rows
//     (rows 43-44, OP_MoveCoin slot 2).
//   - the two confirmations (rows 31-35): RequestDrop / RequestDestroy publish
//     ConfirmationRequested with the eqstr prompt; the dialog's Yes comes back
//     as ConfirmDrop (OP_GroundSpawn, EMPTY body) or ConfirmDestroy
//     (OP_MoveItem 30 -> -1 qty 0 -- the official wire, not OP_DeleteItem).
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/commands/commands_component.h"
#include "client/display_eqstr.h"
#include "client/eq_opcodes.h"
#include "client/inventory/cursor_gesture.h"
#include "client/inventory/inventory_constants.h"
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
	data.bagSlots = data.hasItem ? item->bagSlots : 0;
	data.scrollSpellId = data.hasItem ? item->getScrollSpellId() : 0;  // B1076
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
}

// B442b (#151 row 29). The ONE publisher of CursorCoinChanged; called after
// every cursor-coin change: RunCursorCoinGesture(), and B896's coin row in the
// trade window (RunTradeWindowIntent, cursor -> trade).
void eqt::bridge::BridgeComponent::PublishCursorCoin() {
	if (!ctx_.bridge || !ctx_.inventory_manager) {
		return;
	}
	using MT = eqt::inventory::InventoryManager::CursorMoneyType;
	const auto& inv = *ctx_.inventory_manager;
	eqt::state::CursorCoinChangedData data;
	if (inv.hasCursorMoney()) {
		switch (inv.getCursorMoneyType()) {
		case MT::Platinum: data.coinType = EQT::COINTYPE_PP; break;
		case MT::Gold: data.coinType = EQT::COINTYPE_GP; break;
		case MT::Silver: data.coinType = EQT::COINTYPE_SP; break;
		case MT::Copper:
		case MT::None:
		default: data.coinType = EQT::COINTYPE_CP; break;
		}
		data.amount = inv.getCursorMoneyAmount();
	}
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::CursorCoinChanged, std::move(data)));
}

void eqt::bridge::BridgeComponent::RunCursorCoinGesture(const eqt::events::CursorGestureIntent& i) {
	auto& inv = *ctx_.inventory_manager;
	// Rows 43-44: the bank's BW_Money rows are OP_MoveCoin slot 2, the purse
	// slot 1. Anything else is the purse (the intent's default).
	const bool bank = i.coinSource == EQT::COINSLOT_BANK;
	if (bank && !ctx_.self.IsBankWindowOpen()) {
		// B996 (#316): the bank's coin rows exist only in the open window.
		LOG_DEBUG(MOD_INVENTORY, "Bank coin gesture refused: the bank is not open");
		if (i.echo) {
			ctx_.out.System("The bank is not open.");
		}
		return;
	}
	const eqt::inventory::CoinMovePlan plan = eqt::inventory::PlanCoinGesture(
		inv, i.coinType, i.quantity, BankCoinGet(i.coinType, bank), bank);
	if (!plan.send) {
		LOG_DEBUG(MOD_INVENTORY, "Coin gesture refused: {}", plan.message);
		if (i.echo) {
			ctx_.out.System(plan.message);
		}
		return;
	}
	// eqemu Client::OPMoveCoin: slot 0 = cursor, 1 = inventory, 2 = bank;
	// same type on both sides, so the server converts nothing.
	const int32_t store = bank ? EQT::COINSLOT_BANK : EQT::COINSLOT_INVENTORY;
	EQT::MoveCoin_Struct move;
	move.from_slot = plan.toCursor ? store : EQT::COINSLOT_CURSOR;
	move.to_slot = plan.toCursor ? EQT::COINSLOT_CURSOR : store;
	move.cointype1 = plan.coinType;
	move.cointype2 = plan.coinType;
	move.amount = plan.amount;
	ctx_.Merchant().SendMoveCoin(move);

	const uint8_t type = static_cast<uint8_t>(plan.coinType);
	const uint32_t have = BankCoinGet(type, bank);
	if (plan.toCursor) {
		BankCoinSet(type, bank, have - static_cast<uint32_t>(plan.amount));
		using MT = eqt::inventory::InventoryManager::CursorMoneyType;
		static constexpr MT kByType[4] = {MT::Copper, MT::Silver, MT::Gold, MT::Platinum};
		inv.pickupMoney(kByType[type], static_cast<uint32_t>(plan.amount));
	} else {
		BankCoinSet(type, bank, have + static_cast<uint32_t>(plan.amount));
		inv.returnCursorMoney();
	}
	// The purse moved unless this was the bank; the bank event is always sent.
	PublishBankCoinProjection(/*inventoryChanged=*/!bank);
	PublishCursorCoin();
	if (i.echo) {
		ctx_.out.System(plan.message);
	}
}

void eqt::bridge::BridgeComponent::RunCursorConfirmation(const eqt::events::CursorGestureIntent& i) {
	using Op = eqt::events::CursorGestureOp;
	auto& inv = *ctx_.inventory_manager;

	if (i.op == Op::RequestDrop || i.op == Op::RequestDestroy) {
		const eqt::inventory::ConfirmPromptPlan plan = eqt::inventory::PlanConfirmPrompt(inv, i.op);
		if (!plan.prompt) {
			LOG_DEBUG(MOD_INVENTORY, "Confirmation not asked: {}", plan.message);
			if (i.echo) {
				ctx_.out.System(plan.message);
			}
			return;
		}
		eqt::state::ConfirmationRequestedData data;
		data.purpose = static_cast<uint8_t>(plan.purpose);
		data.timeoutSeconds = plan.timeoutSeconds;
		data.text =eqt::EqStrLine(ctx_.strings.Db(), plan.eqstrId, {plan.itemName});
		if (data.text.empty()) {
			// display_eqstr.h's rule: a table miss draws nothing rather than a
			// transcribed copy -- and a prompt with no words cannot be asked.
			LOG_WARN(MOD_INVENTORY, "eqstr {} missing: no confirmation can be shown", plan.eqstrId);
			return;
		}
		if (ctx_.bridge) {
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ConfirmationRequested, std::move(data)));
		}
		return;
	}

	// ConfirmDrop / ConfirmDestroy: the dialog's Yes. The item may have left
	// the cursor while the dialog was up (a /putdown, a resync): then there is
	// nothing to drop and nothing is sent.
	const eqt::inventory::ItemInstance* held = inv.getCursorItem();
	if (!held) {
		LOG_DEBUG(MOD_INVENTORY, "Confirmation answered with an empty cursor: nothing sent");
		return;
	}
	const std::string name = held->name;
	const bool bag = held->isContainer();
	if (i.op == Op::ConfirmDrop) {
		// Row 33: OP_GroundSpawn (0x0f47) with an EMPTY body; the server drops
		// the cursor item and answers with an S->C OP_GroundSpawn object.
		EQ::Net::DynamicPacket empty;
		ctx_.net.Queue(HC_OP_GroundSpawn, &empty);
	} else {
		// Row 35: OP_MoveItem from 30 to -1, quantity 0.
		ctx_.Merchant().SendMoveItem(eqt::inventory::CURSOR_SLOT, eqt::inventory::SLOT_INVALID, 0);
	}
	inv.discardCursorFront();
	PublishInventorySlot(eqt::inventory::CURSOR_SLOT);
	if (bag) {
		for (int16_t n = 0; n < eqt::inventory::BAG_SLOT_COUNT; ++n) {
			const int16_t child = eqt::inventory::calcContainerSlotId(eqt::inventory::CURSOR_SLOT, n);
			if (child != eqt::inventory::SLOT_INVALID) {
				PublishInventorySlot(child);
			}
		}
	}
	UpdateInventoryStats();
	LOG_DEBUG(MOD_INVENTORY, "{} {} from the cursor", i.op == Op::ConfirmDrop ? "Dropped" : "Destroyed", name);
	if (i.echo) {
		ctx_.out.System(fmt::format("{} {}.", i.op == Op::ConfirmDrop ? "Dropped" : "Destroyed", name));
	}
}

void eqt::bridge::BridgeComponent::RunCursorGesture(const eqt::events::CursorGestureIntent& i) {
	using Op = eqt::events::CursorGestureOp;
	if (!ctx_.inventory_manager) {
		if (i.echo) {
			ctx_.out.System("Inventory not available.");
		}
		return;
	}

	switch (i.op) {
	case Op::CoinClick:
		RunCursorCoinGesture(i);
		return;
	case Op::ToggleBag:
		// Rows 19-21: the same body /bag runs, so a right click and the
		// command open, toggle and refuse identically.
		if (ctx_.commands_) {
			ctx_.Commands().RequestToggleBag(i.slot);
		}
		return;
	case Op::RequestDrop:
	case Op::ConfirmDrop:
	case Op::RequestDestroy:
	case Op::ConfirmDestroy:
		RunCursorConfirmation(i);
		return;
	case Op::Click:
	case Op::PickUp:
	case Op::PutDown:
	default:
		break;
	}

	// UIGAP-residue (B996 #316, MEASURED -- review/UIGAP-ORACLE/316/README.md):
	// with the bank shut the official client draws no bank slot, and the open
	// bank closes itself past 20 units, so it never sends a 2000+ move unless
	// the window is open. The click, /pickup and /putdown share this gate
	// (/equip, fable's own power tool, does not go through here).
	if ((eqt::inventory::isBankSlot(i.slot) || eqt::inventory::isBankBagSlot(i.slot)) &&
	    !ctx_.self.IsBankWindowOpen()) {
		LOG_DEBUG(MOD_INVENTORY, "Cursor gesture on bank slot {} refused: the bank is not open", i.slot);
		if (i.echo) {
			ctx_.out.System("The bank is not open.");
		}
		return;
	}

	// B896 (#212): a trade slot's rules, before the move (an offered item
	// cannot be taken back out) and after it (our change resets both accepts).
	if (const std::string refusal = TradeGestureRefusal(i); !refusal.empty()) {
		LOG_DEBUG(MOD_INVENTORY, "Cursor gesture refused on trade slot {}: {}", i.slot, refusal);
		if (i.echo) {
			ctx_.out.System(refusal);
		}
		return;
	}
	auto& inv = *ctx_.inventory_manager;
	const eqt::inventory::CursorGestureOutcome outcome = eqt::inventory::ApplyCursorGesture(inv, i);
	if (outcome.success) {
		AfterTradeGesture(i);
	}
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
