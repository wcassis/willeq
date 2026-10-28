/*
 * The Options window's wired comboboxes and listboxes, and the one click
 * resolver every wired control goes through (B430/B433).
 *
 * Geometry is options_layout.h's (controlRect, listRowRect, listRowAt) --
 * this TU computes no rect of its own, T57's rule.
 *
 * DEVIATION, FLAGGED: the oracle's <Combobox> drops a list of its choices; a
 * click on a fable combobox CYCLES to the next choice in the order the
 * settings table lists them. Every choice is reachable and the values are the
 * oracle's, but the gesture is not.
 */

#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/options_keys.h"

#include <algorithm>
#include <string>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

namespace {

using OptionsLayout::Kind;
using OptionsLayout::Setting;
using OptionsLayout::ValueKind;

const irr::video::SColor kText(255, 230, 230, 210);
const irr::video::SColor kHeaderText(255, 255, 215, 0);
const irr::video::SColor kFrame(255, 60, 60, 70);
const irr::video::SColor kSelectedFill(110, 90, 90, 160);
const irr::video::SColor kHover(60, 255, 255, 255);

// The column split of OFP_FilterList: its two <Columns> are "Filter
// description" and "Filter Setting"; the second starts at 60 % of the width
// (fable's split -- the XML gives the columns no widths).
irr::s32 secondColumnX(const irr::core::rect<irr::s32>& r) {
    return r.UpperLeftCorner.X + (r.getWidth() * 3) / 5;
}

void drawFilterList(UIRenderer& ui, TextBatch* tb, const UILayout& layout,
                    const OptionsPanelState& state, int i) {
    const auto r = OptionsLayout::controlRect(layout, i);
    const irr::s32 col2 = secondColumnX(r);
    if (tb) {
        tb->addText("Filter description", r.UpperLeftCorner.X + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
        tb->addText("Filter Setting", col2 + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
    }
    const int rows = std::min(OptionsLayout::kFilterCount,
                              OptionsLayout::listVisibleRows(layout, i));
    for (int row = 0; row < rows; ++row) {
        const Setting f = OptionsLayout::filterAt(row);
        const auto rr = OptionsLayout::listRowRect(layout, i, row);
        if (row == state.filterSelectedRow) ui.drawRect(rr, kSelectedFill);
        if (!tb) continue;
        tb->addText(OptionsLayout::settingInfo(f).displayName, rr.UpperLeftCorner.X + 3,
                    rr.UpperLeftCorner.Y + 1, kText);
        tb->addText(OptionsLayout::choiceText(f, optionsSettingStep(state, f)), col2 + 3,
                    rr.UpperLeftCorner.Y + 1, kText);
    }
}

void drawKeysList(UIRenderer& ui, TextBatch* tb, const UILayout& layout,
                  const OptionsPanelState& state, int i) {
    const auto r = OptionsLayout::controlRect(layout, i);
    const irr::s32 x0 = r.UpperLeftCorner.X;
    const irr::s32 xKey = x0 + 180;   // <Columns> Command 180 / Keypress 75 / Alternate 75
    const irr::s32 xAlt = x0 + 255;
    if (tb) {
        tb->addText("Command", x0 + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
        tb->addText("Keypress", xKey + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
        tb->addText("Alternate", xAlt + 3, r.UpperLeftCorner.Y + 2, kHeaderText);
    }
    const auto& actions = OptionsKeys::actionsIn(state.keysCategory);
    const int rows = std::min(static_cast<int>(actions.size()),
                              OptionsLayout::listVisibleRows(layout, i));
    for (int row = 0; row < rows; ++row) {
        const auto a = actions[row];
        const auto rr = OptionsLayout::listRowRect(layout, i, row);
        const bool capturing = state.keysCaptureAction == static_cast<int>(a);
        if (capturing) ui.drawRect(rr, kSelectedFill);
        if (!tb) continue;
        const irr::s32 y = rr.UpperLeftCorner.Y + 1;
        tb->addText(OptionsKeys::commandText(a), x0 + 3, y, kText);
        const bool capKey = capturing && state.keysCaptureSlot == 0;
        const bool capAlt = capturing && state.keysCaptureSlot == 1;
        tb->addText(capKey ? "<press>" : OptionsKeys::bindingText(a, 0), xKey + 3, y,
                    capKey ? kHeaderText : kText);
        tb->addText(capAlt ? "<press>" : OptionsKeys::bindingText(a, 1), xAlt + 3, y,
                    capAlt ? kHeaderText : kText);
    }
}

}  // namespace

void renderOptionsWiredList(UIRenderer& ui, const UILayout& layout,
                            const OptionsPanelState& state, int i) {
    if (i < 0 || i >= OptionsLayout::CONTROL_COUNT) return;
    const auto& c = OptionsLayout::kControls[i];
    const auto r = OptionsLayout::controlRect(layout, i);
    auto* tb = ui.getTextBatch();
    ui.drawFrame(r, kFrame);
    if (state.hoveredControl == i && c.kind == Kind::Combobox) ui.drawRect(r, kHover);

    if (c.kind == Kind::Listbox) {
        if (c.setting == Setting::FilterSelection) drawFilterList(ui, tb, layout, state, i);
        else if (c.setting == Setting::KeysAssignment) drawKeysList(ui, tb, layout, state, i);
        else if (c.setting == Setting::TextColorList) renderOptionsColorList(ui, layout, state, i);
        return;
    }
    if (c.setting == Setting::KeysCategory) {
        if (tb) {
            tb->addText(OptionsKeys::categoryName(state.keysCategory), r.UpperLeftCorner.X + 4,
                        r.UpperLeftCorner.Y + 5, kText);
        }
        return;
    }

    // A combobox: its current choice's caption.
    Setting s = c.setting;
    if (s == Setting::FilterSelection) s = optionsSelectedFilter(state);
    if (tb) {
        tb->addText(OptionsLayout::choiceText(s, optionsSettingStep(state, s)),
                    r.UpperLeftCorner.X + 4, r.UpperLeftCorner.Y + 5, kText);
    }
}

Setting resolveOptionsClick(const UILayout& layout, OptionsPanelState& state, int i,
                            int mouseX, int mouseY) {
    if (i < 0 || i >= OptionsLayout::CONTROL_COUNT) return Setting::None;
    const auto& c = OptionsLayout::kControls[i];
    if (c.setting == Setting::None) return Setting::None;
    // B993: the Colors page acts on the table, not on a value slot.
    if (c.setting == Setting::TextColorList || c.setting == Setting::TextColorReset ||
        c.setting == Setting::TextColorChange) {
        return resolveOptionsColorsClick(layout, state, i, mouseX, mouseY);
    }

    switch (c.kind) {
        case Kind::Slider: {
            // B1175: the slider's own position count (0..maxValue): 20 for the
            // T434 / mouse sliders, 100 for the particle opacity sliders.
            const int step = OptionsLayout::sliderStepAt(layout, i, mouseX,
                                                         OptionsLayout::sliderSteps(c.setting));
            return setOptionsValue(state, c.setting, step) ? c.setting : Setting::None;
        }
        case Kind::Checkbox: {
            const int now = optionsSettingStep(state, c.setting);
            return setOptionsValue(state, c.setting, now ? 0 : 1) ? c.setting : Setting::None;
        }
        case Kind::Combobox: {
            if (c.setting == Setting::KeysCategory) {
                // A view choice, not a setting: cycles the listed category.
                state.keysCategory = (state.keysCategory + 1) % OptionsKeys::kCategoryCount;
                state.keysCaptureAction = -1;
                return Setting::None;
            }
            Setting s = c.setting;
            if (s == Setting::FilterSelection) s = optionsSelectedFilter(state);
            if (OptionsLayout::settingInfo(s).kind != ValueKind::Choice) return Setting::None;
            const int next = OptionsLayout::nextChoiceValue(s, optionsSettingStep(state, s));
            return setOptionsValue(state, s, next) ? s : Setting::None;
        }
        case Kind::Listbox: {
            const int row = OptionsLayout::listRowAt(layout, i, mouseY);
            if (c.setting == Setting::FilterSelection) {
                if (row >= 0 && row < OptionsLayout::kFilterCount) state.filterSelectedRow = row;
            } else if (c.setting == Setting::KeysAssignment) {
                // A click on a row arms a capture: the NEXT key pressed becomes
                // that slot's binding (FrameComponent::processOptionsKeyCapture).
                const auto& actions = OptionsKeys::actionsIn(state.keysCategory);
                if (row >= 0 && row < static_cast<int>(actions.size())) {
                    const auto r = OptionsLayout::controlRect(layout, i);
                    state.keysCaptureAction = static_cast<int>(actions[row]);
                    state.keysCaptureSlot =
                        OptionsKeys::columnAt(r.UpperLeftCorner.X, r.getWidth(), mouseX) == 2 ? 1 : 0;
                }
            }
            // A selection is UI state, not a setting.
            return Setting::None;
        }
        case Kind::Button:
            // An action button (Reload defaults): the setting IS the action.
            return c.setting;
        case Kind::TabBox: {
            // B995: ODP_ParticlesTabs -- a view choice, not a setting.
            const int tab = particleSubTabAt(layout, i, mouseX, mouseY);
            if (tab >= 0) state.particlesTab = tab;
            return Setting::None;
        }
        default:
            return Setting::None;
    }
}

}  // namespace Graphics
}  // namespace EQT
