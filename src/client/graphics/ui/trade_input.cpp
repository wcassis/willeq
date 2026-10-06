/*
 * B896 (GitLab #212) -- input for the trade window and the NPC give window.
 *
 * A StaticUIInput member in its own TU (the container window's shape), so
 * static_ui_input.cpp grows by nothing. The geometry and the hit test live in
 * trade_panel.cpp; this file only turns a hit into an intent. Rows cite
 * review/UIGAP-ORACLE/212/trade.tsv.
 */

#include "client/graphics/ui/static_ui_input.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/trade_panel.h"

namespace EQT {
namespace Graphics {

namespace {

void push(eqt::bridge::GameStateBridge* bridge, eqt::events::TradeWindowOp op) {
    if (!bridge) return;
    eqt::events::TradeWindowIntent intent;
    intent.op = op;
    bridge->pushIntent(intent);
}

}  // namespace

bool StaticUIInput::processTradeInput(int mouseX, int mouseY, bool leftClicked,
                                      const UILayout& layout, TradePanelState& trade) {
    trade.hoveredOwnSlot = -1;
    trade.hoveredCoin = -1;
    trade.acceptHovered = trade.cancelHovered = trade.closeHovered = false;

    const TradeHit hit = hitTestTradeWindow(layout, trade, mouseX, mouseY);
    if (hit.part == TradePart::None) return false;

    using Op = eqt::events::TradeWindowOp;
    switch (hit.part) {
        case TradePart::OwnSlot: {
            trade.hoveredOwnSlot = hit.index;
            const TradeSlotDisplay& cell = trade.own[hit.index];
            eqt::inventory::SlotSnapshot snap;
            snap.occupied = cell.hasItem;
            snap.quantity = cell.quantity;
            const int16_t slot = tradeOwnSlotId(hit.index);
            if (rightClicked_ && cell.hasItem) {
                // The item display, the RIGHT click every item window shares.
                slotClick(slot, /*leftClicked=*/false, snap, cell.itemName);
            } else if (leftClicked && cell.hasItem && mods_.alt && !cursorHoldsItem()) {
                slotClick(slot, /*leftClicked=*/true, snap, cell.itemName);
            } else if (leftClicked && cursorHoldsItem() && !cell.hasItem) {
                // B442 CursorGesture call site -- the held item goes into
                // TRDW_TradeSlot<n> / GVW_MyItemSlot<n>, wire slot 3000+n
                // (rows 3 and 6: OP_MoveItem 30 -> 3000 / 3004). The game
                // thread places it and republishes the slot; nothing moves here.
                slotClick(slot, /*leftClicked=*/true, snap, cell.itemName);
            }
            // Row 5: a click on an item already offered, cursor empty, did
            // nothing and sent nothing (twice) -- it cannot be taken back.
            // Onto an occupied slot with an item held would swap, i.e. a
            // withdrawal: refused here too (an assumption; the game side
            // refuses it as well, BridgeComponent::TradeGestureRefusal()).
            break;
        }
        case TradePart::OwnCoin:
            trade.hoveredCoin = hit.index;
            // Row 2: coin picked from the purse onto the cursor, then a click on
            // our own coin field: OP_MoveCoin cursor -> trade.
            // B442b: held coin is read off the cursor attachment (fed by the
            // game thread's CursorCoinChanged), not a render-side shadow; the
            // game thread clears the cursor coin and republishes it.
            if (leftClicked && cursorHoldsCoin()) {
                push(bridge_, Op::CoinFromCursor);
            }
            break;
        case TradePart::Accept:
            trade.acceptHovered = true;
            if (leftClicked) push(bridge_, Op::Accept);   // rows 4, 7, 10
            break;
        case TradePart::Cancel:
            trade.cancelHovered = true;
            if (leftClicked) push(bridge_, Op::Cancel);   // row 8
            break;
        case TradePart::CloseBox:
            // Style_Closebox: a trade window cannot outlive its trade, so the
            // close box is Cancel (an assumption -- not exercised on the oracle).
            trade.closeHovered = true;
            if (leftClicked) push(bridge_, Op::Cancel);
            break;
        case TradePart::PartnerSlot:
        case TradePart::PartnerCoin:
        case TradePart::Body:
        case TradePart::None:
            // The partner's column is read-only.
            break;
    }
    return true;
}

}  // namespace Graphics
}  // namespace EQT
