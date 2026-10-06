/*
 * B442 (GitLab #151) -- StaticUIInput's half of the click-to-cursor gesture:
 * the shared slot-click helper every item window's arm calls, the purse rows,
 * and the QuantityWnd. A separate TU so static_ui_input.cpp (shared with six
 * sibling units) gains only the call sites. The gesture itself is
 * eqt::inventory::CursorGesture (include/client/inventory/cursor_gesture.h).
 *
 * B442b (#151 remainder) added: the bag toggle hook (rows 19-21), the
 * held/kept item display request (rows 25-26), the bank's coin rows (43-44),
 * the coin decision read off the cursor attachment instead of B442's
 * coinHeld_ shadow (row 29), the confirmation dialog (rows 31-35), the typed
 * Quantity box (row 5) and the QuantityWnd's Sell purpose (#152 row 5).
 */

#include "client/graphics/ui/static_ui_input.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/quantity_layout.h"
#include "client/inventory/inventory_constants.h"
#include "common/logging.h"
#include "common/packets/merchant.h"   // EQT::COINTYPE_*, EQT::COINSLOT_*

namespace EQT {
namespace Graphics {

namespace {
// IW_Money0..3 is platinum, gold, silver, copper; the wire counts up from
// copper (EQT::COINTYPE_CP = 0 .. COINTYPE_PP = 3). BW_Money0..3 is the same.
constexpr uint8_t kCoinTypeOfRow[4] = {EQT::COINTYPE_PP, EQT::COINTYPE_GP,
                                       EQT::COINTYPE_SP, EQT::COINTYPE_CP};
constexpr const char* kCoinNameOfRow[4] = {"Platinum", "Gold", "Silver", "Copper"};
}  // namespace

bool StaticUIInput::cursorHoldsItem() const {
    return cursor_ != nullptr && cursor_->hasItem;
}

bool StaticUIInput::cursorHoldsCoin() const {
    return cursor_ != nullptr && cursor_->holdsCoin();
}

void StaticUIInput::installOpenContainerHook() {
    // Rows 19-21: RIGHT on a carried bag toggles its window -- the /bag body,
    // reached through CursorGestureOp::ToggleBag. Claimed only for the click
    // slotClick() is making on a general slot that holds a container
    // (pendingBag_); anything else falls back to the item display.
    cursorGesture_.setOpenContainerHook([this](int16_t slotId) {
        if (!pendingBag_ || !bridge_) return false;
        eqt::events::CursorGestureIntent toggle;
        toggle.op = eqt::events::CursorGestureOp::ToggleBag;
        toggle.slot = slotId;
        bridge_->pushIntent(toggle);
        return true;
    });
}

void StaticUIInput::slotClick(int16_t slotId, bool leftClicked,
                              const eqt::inventory::SlotSnapshot& slot,
                              const std::string& name, uint32_t iconId,
                              uint8_t bagSlots) {
    // While the QuantityWnd is up the official client ignores other slot
    // clicks (gestures.tsv row 18); the confirmation dialog is given the same
    // rule (assumption awaiting oracle).
    if (quantity_.isOpen || confirm_.isOpen) return;
    // #152 (merchant.tsv row 3): with a merchant open and nothing held, a plain
    // left click SELECTS the item for sale -- no pick-up.
    if (leftClicked && !mods_.alt && selectForSale(slotId, slot, name, iconId)) return;

    const auto button = leftClicked ? eqt::inventory::ClickButton::Left
                                    : eqt::inventory::ClickButton::Right;
    pendingBag_ = !leftClicked && bagSlots > 0 && eqt::inventory::isGeneralSlot(slotId);
    const auto action = cursorGesture_.click(
        slotId, button, mods_, slot, eqt::inventory::CursorSnapshot{cursorHoldsItem()});
    pendingBag_ = false;
    if (action == eqt::inventory::GestureAction::OpenQuantity) {
        quantity_.openForStack(slotId, name, slot.quantity);
    } else if (action == eqt::inventory::GestureAction::ItemDisplay) {
        // Row 25: a RIGHT press shows it while held; row 26: ALT+LEFT keeps it.
        displayRequest_ = leftClicked ? DisplayRequest::Kept : DisplayRequest::Held;
    }
    LOG_DEBUG(MOD_UI, "Cursor gesture: slot {} {} -> action {}", slotId,
              leftClicked ? "left" : "right", static_cast<int>(action));
}

void StaticUIInput::coinRowClick(int denom, int32_t onHand) {
    coinStoreClick(denom, onHand, EQT::COINSLOT_INVENTORY);
}

void StaticUIInput::coinStoreClick(int denom, int32_t onHand, uint8_t source) {
    if (denom < 0 || denom >= 4 || quantity_.isOpen || confirm_.isOpen) return;
    // Held coin goes onto the clicked store (rows 30, 44). The cursor
    // attachment says whether coin is held -- the game thread's
    // CursorCoinChanged feeds it, so a /coin typed behind the UI's back is
    // seen here and costs no dialog.
    if (cursorHoldsCoin()) {
        cursorGesture_.coinClick(kCoinTypeOfRow[denom], 0, source);
        return;
    }
    if (cursorHoldsItem() || onHand <= 0) {
        return;
    }
    // SHIFT takes the whole pile with no dialog -- measured on the BANK's coin
    // (row 43); the purse's SHIFT is inferred from it (oracle-owed). A plain
    // click opens the QuantityWnd prefilled with all of it (row 28; the bank's
    // plain click is inferred from the purse's, oracle-owed).
    if (mods_.shift || onHand == 1) {
        cursorGesture_.coinClick(kCoinTypeOfRow[denom], 0, source);
        return;
    }
    quantity_.openForCoin(kCoinTypeOfRow[denom], kCoinNameOfRow[denom], onHand);
    quantity_.coinSource = source;
}

void StaticUIInput::acceptQuantity() {
    switch (quantity_.purpose) {
        case QuantityWindowState::Purpose::ItemStack:
            cursorGesture_.pickUpQuantity(quantity_.slotId, quantity_.value);
            break;
        case QuantityWindowState::Purpose::Coin:
            cursorGesture_.coinClick(quantity_.coinType, quantity_.value, quantity_.coinSource);
            break;
        case QuantityWindowState::Purpose::Sell:
            // #152 row 6: Accept with 2 of 5 sold 2 straight from the slot.
            if (bridge_ && quantity_.value > 0) {
                const int32_t qty = quantity_.value > 255 ? 255 : quantity_.value;
                bridge_->pushIntent(eqt::events::VendorSellIntent{
                    quantity_.npcId, static_cast<uint16_t>(quantity_.slotId),
                    static_cast<uint8_t>(qty)});
            }
            break;
        case QuantityWindowState::Purpose::None:
        default:
            break;
    }
    quantity_.close();
}

bool StaticUIInput::processQuantityInput(int mouseX, int mouseY, bool leftClicked,
                                         const UILayout& layout) {
    const QuantityClick result =
        processQuantityClick(layout, quantity_, mouseX, mouseY, leftClicked);
    switch (result) {
        case QuantityClick::Accept:
            acceptQuantity();
            return true;
        case QuantityClick::Close:
            // A pure client view: nothing was picked up, nothing to tell the
            // server.
            quantity_.close();
            return true;
        case QuantityClick::Consumed:
            return true;
        case QuantityClick::None:
        default:
            return false;
    }
}

bool StaticUIInput::processQuantityKey(irr::EKEY_CODE key, wchar_t character) {
    const QuantityClick result = applyQuantityKey(quantity_, key, character);
    if (result == QuantityClick::Accept) {
        acceptQuantity();
    }
    return result != QuantityClick::None;
}

bool StaticUIInput::processDialogInput(int mouseX, int mouseY, bool leftClicked,
                                       const UILayout& layout) {
    // The confirmation dialog is asked first: it is the newest modal and the
    // one a world click or Destroy just raised.
    switch (processConfirmationClick(layout, confirm_, mouseX, mouseY, leftClicked)) {
        case ConfirmationClick::Yes: {
            using eqt::inventory::ConfirmPurpose;
            const auto purpose = static_cast<ConfirmPurpose>(confirm_.purpose);
            if (bridge_ && (purpose == ConfirmPurpose::DropItem ||
                            purpose == ConfirmPurpose::DestroyItem)) {
                eqt::events::CursorGestureIntent yes;
                yes.op = purpose == ConfirmPurpose::DropItem
                             ? eqt::events::CursorGestureOp::ConfirmDrop
                             : eqt::events::CursorGestureOp::ConfirmDestroy;
                bridge_->pushIntent(yes);
            }
            confirm_.close();
            return true;
        }
        case ConfirmationClick::No:
        case ConfirmationClick::Ok:
            // Rows 32: No closes it and the item stays on the cursor --
            // nothing was sent, so nothing is told.
            confirm_.close();
            return true;
        case ConfirmationClick::Consumed:
            return true;
        case ConfirmationClick::None:
        default:
            break;
    }
    return processQuantityInput(mouseX, mouseY, leftClicked, layout);
}

}  // namespace Graphics
}  // namespace EQT
