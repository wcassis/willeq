/*
 * B442 (#151) -- the QuantityWnd. See include/client/graphics/ui/
 * quantity_window.h; the geometry is quantity_layout.h, which quotes
 * EQUI_QuantityWnd.xml. This TU computes no rect.
 */

#include "client/graphics/ui/quantity_window.h"
#include "client/graphics/ui/window_flags.h"  // B1002

#include "client/graphics/ui/quantity_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"

#include <string>

namespace EQT {
namespace Graphics {

namespace {
bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return x >= r.UpperLeftCorner.X && x < r.LowerRightCorner.X &&
           y >= r.UpperLeftCorner.Y && y < r.LowerRightCorner.Y;
}
}  // namespace

// The dialog opens on the WHOLE amount: Accept with the slider untouched
// picks the stack up as a plain click would. Measured: gestures.tsv rows 3 and 28.
void QuantityWindowState::openForStack(int16_t slot, const std::string& name, int32_t count) {
    *this = QuantityWindowState{};
    isOpen = count > 0;
    purpose = Purpose::ItemStack;
    slotId = slot;
    label = name;
    maxValue = count;
    value = count;
}

void QuantityWindowState::openForCoin(uint8_t type, const std::string& name, int32_t count) {
    *this = QuantityWindowState{};
    isOpen = count > 0;
    purpose = Purpose::Coin;
    coinType = type;
    label = name;
    maxValue = count;
    value = count;
}

// B442b: merchant.tsv row 5 -- the merchant's Sell on a stack opens the same
// dialog prefilled with the whole stack.
void QuantityWindowState::openForSell(uint16_t merchant, int16_t slot, const std::string& name,
                                      int32_t count) {
    *this = QuantityWindowState{};
    isOpen = count > 0;
    purpose = Purpose::Sell;
    npcId = merchant;
    slotId = slot;
    label = name;
    maxValue = count;
    value = count;
}

QuantityClick applyQuantityKey(QuantityWindowState& state, irr::EKEY_CODE key,
                               wchar_t character) {
    if (!state.isOpen || !state.input.inputActive) return QuantityClick::None;
    // QTYW_SliderInput takes a number: a printable non-digit is swallowed.
    const bool printable = character >= 32 && character < 127;
    if (printable && (character < L'0' || character > L'9')) return QuantityClick::Consumed;

    const TextEntryAction action = applyTextEntryKey(state.input, key, character);
    if (action == TextEntryAction::Submit) {
        state.input.inputActive = true;   // Return keeps the box; Accept closes the window
        return state.value > 0 ? QuantityClick::Accept : QuantityClick::Consumed;
    }
    if (action == TextEntryAction::Cancel) {
        // Row 16: Escape does not close the Quantity window -- it only lets
        // go of the box.
        state.input.inputActive = false;
        return QuantityClick::Consumed;
    }
    if (action == TextEntryAction::Changed) {
        long typed = 0;
        for (char c : state.input.inputText) {
            if (c < '0' || c > '9') continue;
            typed = typed * 10 + (c - '0');
            if (typed > state.maxValue) break;
        }
        if (typed > state.maxValue) {
            typed = state.maxValue;
            focusTextEntry(state.input, std::to_string(typed));
        }
        state.value = static_cast<int32_t>(typed);
    }
    return QuantityClick::Consumed;
}

QuantityClick processQuantityClick(const UILayout& layout, QuantityWindowState& state,
                                   int mouseX, int mouseY, bool leftClicked) {
    state.acceptHovered = false;
    state.closeHovered = false;
    if (!state.isOpen) return QuantityClick::None;
    if (!inside(QuantityLayout::windowRect(layout), mouseX, mouseY)) {
        return QuantityClick::None;
    }
    if (leftClicked && inside(QuantityLayout::inputRect(layout), mouseX, mouseY)) {
        // Row 5: the box takes the keyboard; the next digit replaces the
        // number shown (an empty entry -- "select all" on focus).
        focusTextEntry(state.input, "");
        return QuantityClick::Consumed;
    }
    if (leftClicked) {
        // Any other click on the dialog lets go of the box.
        state.input.inputActive = false;
    }
    if (inside(QuantityLayout::closeBoxRect(layout), mouseX, mouseY)) {
        state.closeHovered = true;
        return leftClicked ? QuantityClick::Close : QuantityClick::Consumed;
    }
    if (inside(QuantityLayout::acceptRect(layout), mouseX, mouseY)) {
        state.acceptHovered = true;
        return leftClicked && state.value > 0 ? QuantityClick::Accept : QuantityClick::Consumed;
    }
    if (leftClicked && inside(QuantityLayout::sliderRect(layout), mouseX, mouseY)) {
        state.value = QuantityLayout::valueAt(layout, state.maxValue, mouseX);
    }
    return QuantityClick::Consumed;
}

void renderQuantityWindow(UIRenderer& ui, const UILayout& layout,
                          const QuantityWindowState& state) {
    if (!state.isOpen) return;
    auto* tb = ui.getTextBatch();

    ui.drawPanelFor(QuantityLayout::windowRect(layout), UIWindow::Quantity);

    // Style_Titlebar: <Text>Quantity</Text>.
    const auto title = QuantityLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText("Quantity", title.UpperLeftCorner.X + 6, title.UpperLeftCorner.Y + 2,
                    irr::video::SColor(255, 255, 215, 0));
    }
    const auto close = QuantityLayout::closeBoxRect(layout);
    ui.drawRect(close, state.closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                          : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, irr::video::SColor(255, 235, 235, 235));

    // QTYW_Slider: the track and the thumb at the chosen value.
    const auto slider = QuantityLayout::sliderRect(layout);
    const irr::s32 midY = (slider.UpperLeftCorner.Y + slider.LowerRightCorner.Y) / 2;
    ui.drawRect({slider.UpperLeftCorner.X, midY - 2, slider.LowerRightCorner.X, midY + 2},
                irr::video::SColor(220, 90, 90, 100));
    const irr::s32 tx = QuantityLayout::thumbX(layout, state.maxValue, state.value);
    ui.drawRect({tx - 3, slider.UpperLeftCorner.Y + 2, tx + 4, slider.LowerRightCorner.Y - 2},
                irr::video::SColor(240, 210, 190, 120));

    // QTYW_SliderInput (WDT_Inner, white text): the number.
    const auto input = QuantityLayout::inputRect(layout);
    ui.drawRect(input, irr::video::SColor(220, 18, 18, 24));
    if (state.input.inputActive) {
        ui.drawFrame(input, irr::video::SColor(255, 255, 215, 0));   // it holds the keyboard
    }
    if (tb) {
        const std::string shown = state.input.inputActive
            ? state.input.inputText + "_"
            : std::to_string(state.value);
        tb->addTextCentered(shown, input, irr::video::SColor(255, 255, 255, 255));
    }

    // QTYW_Accept_Button: "Accept".
    const auto accept = QuantityLayout::acceptRect(layout);
    ui.drawRect(accept, state.acceptHovered ? irr::video::SColor(230, 90, 80, 60)
                                            : irr::video::SColor(220, 60, 55, 48));
    if (tb) tb->addTextCentered("Accept", accept, irr::video::SColor(255, 255, 255, 255));
}

}  // namespace Graphics
}  // namespace EQT
