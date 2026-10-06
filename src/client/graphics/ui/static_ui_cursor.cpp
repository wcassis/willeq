/*
 * B442 (GitLab #151) -- StaticUIInput's half of the click-to-cursor gesture:
 * the shared slot-click helper every item window's arm calls, the purse rows,
 * and the QuantityWnd. A separate TU so static_ui_input.cpp (shared with six
 * sibling units) gains only the call sites. The gesture itself is
 * eqt::inventory::CursorGesture (include/client/inventory/cursor_gesture.h).
 */

#include "client/graphics/ui/static_ui_input.h"

#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/quantity_layout.h"
#include "common/logging.h"
#include "common/packets/merchant.h"   // EQT::COINTYPE_*

namespace EQT {
namespace Graphics {

namespace {
// IW_Money0..3 is platinum, gold, silver, copper; the wire counts up from
// copper (EQT::COINTYPE_CP = 0 .. COINTYPE_PP = 3).
constexpr uint8_t kCoinTypeOfRow[4] = {EQT::COINTYPE_PP, EQT::COINTYPE_GP,
                                       EQT::COINTYPE_SP, EQT::COINTYPE_CP};
constexpr const char* kCoinNameOfRow[4] = {"Platinum", "Gold", "Silver", "Copper"};
}  // namespace

bool StaticUIInput::cursorHoldsItem() const {
    return cursor_ != nullptr && cursor_->hasItem;
}

void StaticUIInput::slotClick(int16_t slotId, bool leftClicked,
                              const eqt::inventory::SlotSnapshot& slot,
                              const std::string& name) {
    // While the QuantityWnd is up the official client ignores other slot
    // clicks (gestures.tsv row 18).
    if (quantity_.isOpen) return;
    const auto button = leftClicked ? eqt::inventory::ClickButton::Left
                                    : eqt::inventory::ClickButton::Right;
    const auto action = cursorGesture_.click(
        slotId, button, mods_, slot, eqt::inventory::CursorSnapshot{cursorHoldsItem()});
    if (action == eqt::inventory::GestureAction::OpenQuantity) {
        quantity_.openForStack(slotId, name, slot.quantity);
    }
    LOG_DEBUG(MOD_UI, "Cursor gesture: slot {} {} -> action {}", slotId,
              leftClicked ? "left" : "right", static_cast<int>(action));
}

void StaticUIInput::coinRowClick(int denom, int32_t onHand) {
    if (denom < 0 || denom >= 4 || quantity_.isOpen) return;
    // Held coin goes back (gestures.tsv rows 30, 44); the game thread decides
    // from its own model, so a stale coinHeld_ only ever costs a dialog.
    if (coinHeld_) {
        cursorGesture_.coinClick(kCoinTypeOfRow[denom], 0);
        coinHeld_ = false;
        return;
    }
    if (cursorHoldsItem() || onHand <= 0) {
        return;
    }
    // SHIFT takes the whole pile with no dialog -- measured on the BANK's coin
    // (row 43); the purse's SHIFT is inferred from it (oracle-owed). A plain
    // click opens the QuantityWnd prefilled with all of it (row 28).
    if (mods_.shift || onHand == 1) {
        cursorGesture_.coinClick(kCoinTypeOfRow[denom], 0);
        coinHeld_ = true;
        return;
    }
    quantity_.openForCoin(kCoinTypeOfRow[denom], kCoinNameOfRow[denom], onHand);
}

bool StaticUIInput::processQuantityInput(int mouseX, int mouseY, bool leftClicked,
                                         const UILayout& layout) {
    const QuantityClick result =
        processQuantityClick(layout, quantity_, mouseX, mouseY, leftClicked);
    switch (result) {
        case QuantityClick::Accept:
            if (quantity_.purpose == QuantityWindowState::Purpose::ItemStack) {
                cursorGesture_.pickUpQuantity(quantity_.slotId, quantity_.value);
            } else if (quantity_.purpose == QuantityWindowState::Purpose::Coin) {
                cursorGesture_.coinClick(quantity_.coinType, quantity_.value);
                coinHeld_ = true;
            }
            quantity_.close();
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

}  // namespace Graphics
}  // namespace EQT
