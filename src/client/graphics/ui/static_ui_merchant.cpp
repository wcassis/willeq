/*
 * B442b (GitLab #152, board B443) -- the merchant's select-to-sell model.
 *
 * MEASURED on the official Titanium client (review/UIGAP-ORACLE/152/
 * merchant.tsv), and contradicting T436's "an item on the cursor means
 * SELL":
 *   row 1  the window opens with the shared button reading Buy;
 *   row 2  a merchant list row selects it -> Buy;
 *   row 3  a left click on an item in YOUR inventory SELECTS it (yellow frame)
 *          and does NOT pick it up -> Sell;
 *   row 4  a merchant row after that -> Buy again (the LAST selection wins);
 *   row 5  Sell on a stack opens the QuantityWnd prefilled with the whole stack;
 *   row 6  Accept with 2 -> OP_ShopPlayerSell itemslot=27 quantity=2: sold
 *          straight from the slot, no OP_MoveItem, the cursor never used;
 *   row 10 ctrl+Buy buys exactly one.
 * SHIFT on Sell/Buy is the whole stack per the official Merchant Tip text
 * (help/tips/ctxmerchant.html), not a captured row.
 *
 * StaticUIInput members, in their own TU so static_ui_input.cpp (shared with
 * sibling units) gains only the two call sites.
 */

#include "client/graphics/ui/static_ui_input.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool StaticUIInput::selectForSale(int16_t slotId, const eqt::inventory::SlotSnapshot& slot,
                                  const std::string& name, uint32_t iconId) {
    if (!merchantSel_ || !merchantSel_->isOpen) return false;
    // With something held the click is the ordinary gesture: the oracle's
    // held-item clicks at a merchant (merchant.tsv rows 7-8, M10-M11) left a
    // ghost icon and are unresolved, so fable does not invent a rule there.
    if (cursorHoldsItem() || cursorHoldsCoin()) return false;
    if (!slot.occupied) return false;
    // The bank and a merchant are never open together; a bank slot is not
    // the player's to sell from in any case.
    if (eqt::inventory::isBankSlot(slotId)) return false;

    merchantSel_->selectPlayerItem(slotId, name, iconId, slot.quantity > 0 ? slot.quantity : 1);
    if (inventorySel_) {
        // The yellow frame is drawn by the inventory popup for its own 30
        // slots; a bag cell's selection is recorded and sold the same way but
        // not framed (the container window draws no selection yet).
        inventorySel_->selectedSlot =
            (slotId >= 0 && slotId < eqt::inventory::GENERAL_END + 1) ? slotId : -1;
    }
    LOG_DEBUG(MOD_UI, "Merchant: selected slot {} ('{}', {}) for sale", slotId, name,
              slot.quantity);
    return true;
}

void StaticUIInput::merchantTradeClick(MerchantPanelState& merchant) {
    if (!bridge_) return;
    if (merchant.sellSelected()) {
        const int32_t have = merchant.playerQuantity > 0 ? merchant.playerQuantity : 1;
        if (have > 1 && !mods_.shift && !mods_.ctrl) {
            // Row 5: the QuantityWnd, whole stack prefilled; its Accept sells.
            quantity_.openForSell(merchant.npcId, merchant.playerSlot,
                                  merchant.playerItemName, have);
            return;
        }
        // CTRL: one (the Tip; measured for Buy, row 10). SHIFT or a single
        // item: all of it.
        int32_t qty = mods_.ctrl ? 1 : have;
        if (qty > 255) qty = 255;
        bridge_->pushIntent(eqt::events::VendorSellIntent{
            merchant.npcId, static_cast<uint16_t>(merchant.playerSlot),
            static_cast<uint8_t>(qty)});
        return;
    }
    if (merchant.selectedRow >= 0 &&
        merchant.selectedRow < static_cast<int>(merchant.items.size())) {
        // BUY one. Row 10 measured ctrl+Buy = 1. A plain Buy on a stackable
        // row opens the QuantityWnd in the oracle (the Tip) -- the row carries
        // no stack size to prefill it with, so fable buys one (owed).
        bridge_->pushIntent(eqt::events::VendorBuyIntent{
            merchant.npcId,
            merchant.items[static_cast<size_t>(merchant.selectedRow)].vendorSlot, 1});
    }
}

void applyMerchantSelectionSlotChanged(MerchantPanelState& merchant, InventoryPanelState& inventory,
                                       const eqt::state::InventorySlotChangedData& data) {
    if (merchant.playerSlot < 0 || data.slotId != merchant.playerSlot) return;
    if (!data.hasItem) {
        // Sold out (or moved away): nothing is selected on the player's side.
        merchant.clearPlayerItem();
        if (inventory.selectedSlot == data.slotId) inventory.selectedSlot = -1;
        return;
    }
    merchant.playerItemName = data.itemName;
    merchant.playerIconId = data.iconId;
    merchant.playerQuantity = data.quantity > 0 ? static_cast<int32_t>(data.quantity) : 1;
}

}  // namespace Graphics
}  // namespace EQT
