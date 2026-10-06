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
 * QTYW_SliderInput is drawn as a read-only box showing the chosen number;
 * typing a number into it is not wired (owed -- the slider is the gesture).
 */

#pragma once

#include <cstdint>
#include <string>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

class UIRenderer;

struct QuantityWindowState {
    enum class Purpose : uint8_t { None = 0, ItemStack, Coin };

    bool isOpen = false;
    Purpose purpose = Purpose::None;
    int16_t slotId = -1;      // ItemStack: the Titanium slot of the stack
    uint8_t coinType = 0;     // Coin: EQT::COINTYPE_*
    std::string label;        // what is being split, for the title row
    int32_t maxValue = 0;     // the stack count / the coins on hand
    int32_t value = 0;        // the slider's current number, 1..maxValue

    bool acceptHovered = false;
    bool closeHovered = false;

    void openForStack(int16_t slot, const std::string& name, int32_t count);
    void openForCoin(uint8_t type, const std::string& name, int32_t count);
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

void renderQuantityWindow(UIRenderer& ui, const UILayout& layout,
                          const QuantityWindowState& state);

}  // namespace Graphics
}  // namespace EQT
