// bridge_component_trade.cpp
//
// B896 (GitLab #212): the game-thread half of the trade window and the NPC
// give window.
//
//   RunTradeWindowIntent()   the window's Trade/Give, Cancel (and close box)
//                            and coin rows -> the TradeManager bodies /accept
//                            and /canceltrade already run, plus the one coin
//                            move the window adds (cursor -> trade).
//   TradeGestureRefusal()    B442's cursor gesture, asked BEFORE it runs on a
//   AfterTradeGesture()      trade slot and told AFTER: items cannot be taken
//                            back out of a trade, and a change on our side
//                            resets both accepts.
//   OfferHeldItemOnTradeOpen()  our request was acknowledged: the item we
//                            opened the trade with goes into slot 3000 by
//                            itself (held coin: OP_MoveCoin 0 -> 3, row 14).
//   HeldItemTargetRefusal()  UIGAP-residue: the world clicks the official
//                            client refuses client-side (rows 12-13).
//
// Every rule is MEASURED on two official Titanium clients:
// /mnt/projects/fable/review/UIGAP-ORACLE/212/trade.tsv; the row is cited at
// each. The renderer never moves an item or a coin itself: what it shows is
// what this side republishes (InventorySlotChanged for 3000..3007) or the
// TradeManager callbacks publish (TradeMoneyUpdated, TradeAcceptStateChanged).
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/trade_manager.h"
#include "common/logging.h"
#include "common/packet_structs.h"

namespace inv = eqt::inventory;

namespace {

// Does the gesture address a trade-window slot (or the contents of a bag in
// one)? Those are the only slots these hooks are about.
bool isTradeRange(int16_t slot) {
	return inv::isTradeSlot(slot) || inv::isTradeBagSlot(slot);
}

int32_t coinTypeOf(inv::InventoryManager::CursorMoneyType t) {
	using MT = inv::InventoryManager::CursorMoneyType;
	switch (t) {
		case MT::Platinum: return EQT::COINTYPE_PP;
		case MT::Gold: return EQT::COINTYPE_GP;
		case MT::Silver: return EQT::COINTYPE_SP;
		case MT::Copper: return EQT::COINTYPE_CP;
		case MT::None:
		default: return -1;
	}
}

}  // namespace

void eqt::bridge::BridgeComponent::RunTradeWindowIntent(const eqt::events::TradeWindowIntent& i) {
	TradeManager* trade = ctx_.trade_manager.get();
	if (!trade) {
		return;
	}
	using Op = eqt::events::TradeWindowOp;
	switch (i.op) {
		case Op::Accept:
			// Rows 4 and 10: TRDW_Trade_Button / GVW_Give_Button send
			// OP_TradeAcceptClick. The window says nothing in chat (no row
			// prints a line), so unlike /accept this prints nothing; a second
			// press while already accepted sends nothing.
			if (trade->isTrading() && !trade->isOwnAccepted()) {
				trade->clickAccept();
			}
			return;
		case Op::Cancel:
			// Row 8: Cancel sends OP_CancelTrade; the "You have cancelled the
			// trade." line (eqstr 1450) is the cancelled callback's, the same
			// one /canceltrade reaches.
			if (trade->getState() != TradeState::None) {
				trade->cancelTrade();
			}
			return;
		case Op::CoinFromCursor: {
			// Row 2: the coin was picked onto the cursor from the purse (its own
			// OP_MoveCoin 1 -> 0, the purse click), and the own coin field's
			// click sends OP_MoveCoin 0 -> 3 for the held pile.
			if (!ctx_.inventory_manager || !trade->isTrading()) {
				return;
			}
			auto& im = *ctx_.inventory_manager;
			if (!im.hasCursorMoney()) {
				return;
			}
			const int32_t type = coinTypeOf(im.getCursorMoneyType());
			const uint32_t amount = im.getCursorMoneyAmount();
			if (type < 0 || !trade->addOwnCoinFromCursor(type, amount)) {
				return;
			}
			im.clearCursorMoney();
			PublishCursorCoin();   // B442b's one publisher: the cursor is empty again
			LOG_DEBUG(MOD_INVENTORY, "Trade: {} of coin type {} from the cursor into the trade",
			          amount, type);
			return;
		}
	}
}

std::string eqt::bridge::BridgeComponent::TradeGestureRefusal(
		const eqt::events::CursorGestureIntent& i) const {
	using Op = eqt::events::CursorGestureOp;
	if (i.op == Op::CoinClick || !isTradeRange(i.slot) || !ctx_.inventory_manager) {
		return std::string();
	}
	const auto& im = *ctx_.inventory_manager;
	const bool holding = im.getCursorItem() != nullptr;
	// Row 5: clicking an item already in our trade slot did nothing, twice, and
	// sent no packet -- an offered item cannot be taken back out. That covers
	// a pick-up, and a bag's contents in a trade slot.
	if (i.op == Op::PickUp || !holding || inv::isTradeBagSlot(i.slot)) {
		return "Items cannot be taken back out of a trade.";
	}
	// Placing onto an OCCUPIED trade slot would swap, which is a withdrawal by
	// another name. Not measured (row 5 is the empty-cursor case); refused
	// here with no packet, the same as row 5 -- an assumption awaiting the
	// oracle, written in the unit's report.
	if (im.getItem(i.slot) != nullptr) {
		return "Items cannot be taken back out of a trade.";
	}
	return std::string();
}

void eqt::bridge::BridgeComponent::AfterTradeGesture(const eqt::events::CursorGestureIntent& i) {
	if (!isTradeRange(i.slot) || !ctx_.trade_manager) {
		return;
	}
	// Row 6: B added a second item after A had accepted, and A's green reset
	// to red on BOTH windows -- B's included, with no packet coming back to B
	// about its own change. So our own change resets both accepts here.
	ctx_.trade_manager->resetAccept();
}

uint32_t eqt::bridge::BridgeComponent::HeldItemTargetRefusal(const Entity& target) const {
	if (!ctx_.inventory_manager) {
		return 0;
	}
	const inv::ItemInstance* held = ctx_.inventory_manager->getCursorItem();
	if (!held) {
		// Coin held is never refused at a player (row 14: the trade opens).
		// Coin held at a banker is not measured: left as it was.
		return 0;
	}
	// Row 12 (UIGAP-live2, B1001 #321): a NO-DROP item held + a motionless
	// click on a PLAYER -> no trade window, the item stays on the cursor, the
	// client prints "That item cannot be dropped, traded, or sold." (eqstr
	// 12921) and sends only OP_TargetMouse. A no-drop item to an NPC is a
	// normal give (give_npc hands Captain Tillin a Gnoll Fang), so NPCs are
	// not refused for it. (eqstr 12922, the container-holding-a-no-drop
	// variant, is not measured and not applied.)
	constexpr uint32_t kEqStrCannotDropTradeSell = 12921;
	if (target.npc_type == 0 && held->noDrop) {
		return kEqStrCannotDropTradeSell;
	}
	// Row 13: an item held + a click on a BANKER (class 40): nothing opens, the
	// item stays held, and the client prints "This NPC cannot accept items
	// directly, right-click if you wish to make transactions." (eqstr 8505)
	// with only targeting on the wire. Only the banker was measured; other
	// non-quest NPC classes (merchants, guildmasters) keep the give window.
	constexpr uint8_t kClassBanker = 40;
	constexpr uint32_t kEqStrNpcCannotAcceptItems = 8505;
	if (target.npc_type == 1 && target.class_id == kClassBanker) {
		return kEqStrNpcCannotAcceptItems;
	}
	return 0;
}

void eqt::bridge::BridgeComponent::OfferHeldItemOnTradeOpen() {
	if (!ctx_.inventory_manager || !ctx_.trade_manager || !ctx_.trade_manager->isTrading()) {
		return;
	}
	if (!ctx_.inventory_manager->getCursorItem()) {
		// Row 14 (UIGAP-live2): COIN held + a click on a PLAYER opens the trade
		// exactly like an item, and right after the ack the held pile goes into
		// our trade money row: OP_MoveCoin from_slot=0 (cursor) to_slot=3
		// (trade) -- the same body the own coin field's click runs (row 2).
		// The NPC give window with coin held is not measured: left as it was
		// (the coin stays on the cursor until a coin row is clicked).
		if (ctx_.inventory_manager->hasCursorMoney() && !ctx_.trade_manager->isNpcTrade()) {
			eqt::events::TradeWindowIntent coin;
			coin.op = eqt::events::TradeWindowOp::CoinFromCursor;
			RunTradeWindowIntent(coin);
		}
		return;
	}
	// Rows 1 and 9: "OP_MoveItem from=30 to=3000" right after the ack, for a
	// player and an NPC alike. The same place a click on slot 3000 does.
	eqt::events::CursorGestureIntent place;
	place.op = eqt::events::CursorGestureOp::PutDown;
	place.slot = inv::TRADE_BEGIN;
	RunCursorGesture(place);
}
