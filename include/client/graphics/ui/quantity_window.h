/*
 * The QuantityWnd -- the stack-split and coin-amount dialog (B442, #151).
 *
 * Opened by a plain left click on a stack of two or more (gestures.tsv row
 * 3) in an item slot, or on
 * a purse row (coin); its Accept picks that many up onto the cursor through
 * the same CursorGestureIntent every other click pushes
 * (eqt::inventory::CursorGesture::pickUpQuantity() / coinClick()). Nothing
 * here moves an item: the dialog only chooses a number.
 *
 * Geometry: quantity_layout.h, which quotes EQUI_QuantityWnd.xml. Like the
 * item display and Options windows it is `RelativePosition false`, so it is
 * drawn and hit-tested ABOVE every other static window.
 *
 * QTYW_SliderInput takes TYPED digits (B442b, gestures.tsv row 5: "Accept
 * after typing 2 (of 3)" sent qty=2): a click on the box gives it the
 * keyboard (the oracle rig clicked the box and then typed -- whether it is
 * focused on open without the click is assumption awaiting oracle), the first
 * digit replaces the shown number, the value is clamped to 0..maxValue as it
 * is typed, Return is Accept and Escape only releases the box (row 16: Escape
 * never closes the window). The keyboard arrives through
 * FrameComponent::processQuantityKeyInput(), the friends-window key gate's
 * shape.
 */

#pragma once

#include <cstdint>
#include <string>

#include <irrlicht.h>

#include "client/graphics/ui/text_entry.h"
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

class UIRenderer;

struct QuantityWindowState {
    // B442b: Sell -- the merchant's Sell on a selected stack (merchant.tsv
    // row 5: "Sell on a stack opens EQUI_QuantityWnd prefilled with the whole
    // stack"); Accept sells `value` straight from `slotId`.
    enum class Purpose : uint8_t { None = 0, ItemStack, Coin, Sell };

    bool isOpen = false;
    Purpose purpose = Purpose::None;
    int16_t slotId = -1;      // ItemStack / Sell: the Titanium slot of the stack
    uint8_t coinType = 0;     // Coin: EQT::COINTYPE_*
    uint8_t coinSource = 1;   // Coin: EQT::COINSLOT_INVENTORY (1) or _BANK (2)
    uint16_t npcId = 0;       // Sell: the merchant
    std::string label;        // what is being split, for the title row
    int32_t maxValue = 0;     // the stack count / the coins on hand
    int32_t value = 0;        // the slider's current number, 1..maxValue

    // QTYW_SliderInput, when it holds the keyboard (B442b, row 5).
    TextEntryState input;

    bool acceptHovered = false;
    bool closeHovered = false;

    void openForStack(int16_t slot, const std::string& name, int32_t count);
    void openForCoin(uint8_t type, const std::string& name, int32_t count);
    void openForSell(uint16_t merchant, int16_t slot, const std::string& name, int32_t count);
    void close() { *this = QuantityWindowState{}; }
};

/** What a click on the dialog did. */
enum class QuantityClick : uint8_t {
    None = 0,   // not over the dialog (or the dialog is closed)
    Consumed,   // over the dialog; the slider may have moved
    Accept,     // QTYW_Accept_Button -- the caller pushes the pick-up, then close()
    Close,      // Style_Closebox -- nothing is picked up
};

/**
 * Hover + click. Updates `value` when the slider is clicked. Pure: pushes
 * nothing; StaticUIInput turns Accept into the gesture.
 */
QuantityClick processQuantityClick(const UILayout& layout, QuantityWindowState& state,
                                   int mouseX, int mouseY, bool leftClicked);

/**
 * B442b (row 5): one key for QTYW_SliderInput while it holds the keyboard.
 * Digits and editing keys change `value` (clamped 0..maxValue); Return is
 * QuantityClick::Accept when value > 0; Escape releases the box and the
 * window stays (row 16). Anything else is QuantityClick::Consumed.
 */
QuantityClick applyQuantityKey(QuantityWindowState& state, irr::EKEY_CODE key,
                               wchar_t character);

void renderQuantityWindow(UIRenderer& ui, const UILayout& layout,
                          const QuantityWindowState& state);

}  // namespace Graphics
}  // namespace EQT
