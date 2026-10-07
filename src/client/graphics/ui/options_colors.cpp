/*
 * The Options window's Colors page (B993, #313): OCP_ColorChooseList,
 * OCP_UserColorDefault and OCP_UserColorChange over the player's
 * [TextColors] table (OptionsPanelState::textColors).
 *
 * Geometry is options_layout.h's (controlRect / listRowRect / listRowAt);
 * the picker's is color_picker.h's.
 *
 * FLAGGED (assumptions awaiting oracle, none is in the XML):
 *   C1. the list is User_1..User_86 in index order (chat_text_colors.h);
 *   C2. the "Color Sample" column is a filled swatch of the colour;
 *   C3. the list's VScroll (Style_VScroll true) is fable's 12px strip on the
 *       right: a click in its upper half pages up, lower half pages down;
 *   C4. Reset Default Colors applies at once (no confirmation);
 *   C5. a recoloured row recolours the lines already in the chat window
 *       (renderChatPanel() resolves the colour at draw time).
 */

#include "client/graphics/ui/options_panel.h"

#include <algorithm>
#include <string>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

namespace {

using OptionsLayout::Setting;

const irr::video::SColor kText(255, 230, 230, 210);
const irr::video::SColor kHeaderText(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 60, 60, 70);
const irr::video::SColor kSelectedFill(110, 90, 90, 160);
const irr::video::SColor kTrack(200, 20, 20, 25);
const irr::video::SColor kThumb(255, 200, 180, 90);

// <Columns> Width 150 "Text description", Width 150 "Color Sample".
constexpr irr::s32 kSampleColumnX = 150;
constexpr irr::s32 kSampleW = 60;

int maxListTop(int visibleRows) {
    return std::max(1, eqt::ui::kUserColorCount - visibleRows + 1);
}

}  // namespace

void renderOptionsColorList(UIRenderer& ui, const UILayout& layout,
                            const OptionsPanelState& state, int i) {
    auto* tb = ui.getTextBatch();
    const auto r = OptionsLayout::controlRect(layout, i);
    const irr::s32 x0 = r.UpperLeftCorner.X;
    if (tb) {
        tb->addText("Text description", x0 + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
        tb->addText("Color Sample", x0 + kSampleColumnX + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
    }
    const int rows = OptionsLayout::listVisibleRows(layout, i);
    for (int row = 0; row < rows; ++row) {
        const int n = state.colorListTop + row;
        if (n > eqt::ui::kUserColorCount) break;
        const auto rr = OptionsLayout::listRowRect(layout, i, row);
        if (n == state.colorSelectedRow) {
            ui.drawRect(irr::core::rect<irr::s32>(rr.UpperLeftCorner.X, rr.UpperLeftCorner.Y,
                                                  rr.LowerRightCorner.X - LIST_VSCROLL_W,
                                                  rr.LowerRightCorner.Y),
                        kSelectedFill);
        }
        if (tb) tb->addText(eqt::ui::userColorName(n), x0 + 3, rr.UpperLeftCorner.Y + 1, kText);
        const auto& c = state.textColors[n - 1];
        ui.drawRect(irr::core::rect<irr::s32>(x0 + kSampleColumnX + 3, rr.UpperLeftCorner.Y + 2,
                                              x0 + kSampleColumnX + 3 + kSampleW,
                                              rr.LowerRightCorner.Y - 2),
                    irr::video::SColor(255, c.r, c.g, c.b));
    }
    // C3: the VScroll strip, with a thumb sized to the visible fraction.
    const irr::core::rect<irr::s32> strip(r.LowerRightCorner.X - LIST_VSCROLL_W,
                                          r.UpperLeftCorner.Y + OptionsLayout::LIST_HEADER_H,
                                          r.LowerRightCorner.X, r.LowerRightCorner.Y);
    ui.drawRect(strip, kTrack);
    ui.drawFrame(strip, kFrame);
    const int track = strip.getHeight();
    if (rows > 0 && track > 0) {
        const int thumbH = std::max(8, track * rows / eqt::ui::kUserColorCount);
        const int span = std::max(1, maxListTop(rows) - 1);
        const int y = strip.UpperLeftCorner.Y + (track - thumbH) * (state.colorListTop - 1) / span;
        ui.drawRect(irr::core::rect<irr::s32>(strip.UpperLeftCorner.X + 2, y,
                                              strip.LowerRightCorner.X - 2, y + thumbH),
                    kThumb);
    }
}

bool resetOptionsTextColors(OptionsPanelState& state) {
    const bool moved = state.textColors != eqt::ui::defaultChatTextColors();
    state.textColors = eqt::ui::defaultChatTextColors();
    return moved;
}

Setting resolveOptionsColorsClick(const UILayout& layout, OptionsPanelState& state, int i,
                                  int mouseX, int mouseY) {
    if (i < 0 || i >= OptionsLayout::CONTROL_COUNT) return Setting::None;
    const auto& c = OptionsLayout::kControls[i];
    switch (c.setting) {
        case Setting::TextColorReset:  // C4
            state.colorPicker.isOpen = false;
            return resetOptionsTextColors(state) ? Setting::TextColorReset : Setting::None;
        case Setting::TextColorChange:
            openColorPicker(state.colorPicker, state.textColors, state.colorSelectedRow);
            return Setting::None;
        case Setting::TextColorList: {
            const auto r = OptionsLayout::controlRect(layout, i);
            const int rows = OptionsLayout::listVisibleRows(layout, i);
            if (mouseX >= r.LowerRightCorner.X - LIST_VSCROLL_W) {  // C3
                const int mid = (r.UpperLeftCorner.Y + OptionsLayout::LIST_HEADER_H +
                                 r.LowerRightCorner.Y) / 2;
                const int step = std::max(1, rows);
                state.colorListTop += (mouseY < mid) ? -step : step;
                state.colorListTop = std::max(1, std::min(maxListTop(rows), state.colorListTop));
                return Setting::None;
            }
            const int row = OptionsLayout::listRowAt(layout, i, mouseY);
            const int n = state.colorListTop + row;
            if (row >= 0 && n <= eqt::ui::kUserColorCount) state.colorSelectedRow = n;
            return Setting::None;  // a selection is UI state, not a setting
        }
        default:
            return Setting::None;
    }
}

}  // namespace Graphics
}  // namespace EQT
