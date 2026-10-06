/*
 * T421 (board B298, item half) -- the item display window.
 *
 * A separate TU from static_panels.cpp for the same reason
 * tradeskill_recipe_list.cpp is: substantial new code goes in a new TU
 * (M00). The geometry is in include/client/graphics/ui/item_display_layout.h
 * (which quotes EQUI_ItemDisplay.xml); the TEXT is composed game-side by
 * eqt::inventory::BuildItemDisplayText() and arrives already formatted.
 */

#include "client/graphics/ui/item_display_panel.h"

#include "client/graphics/ui/item_display_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"
#include "client/state/event_bus.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

int ItemDisplayPanelState::visibleLines(const UILayout& layout) const {
    const int fit = ItemDisplayLayout::visibleDescriptionLines(layout);
    const int remaining = static_cast<int>(lines.size()) - scrollOffset;
    if (remaining <= 0 || fit <= 0) return 0;
    return std::min(remaining, fit);
}

int ItemDisplayPanelState::maxScrollOffset(const UILayout& layout) const {
    const int over = static_cast<int>(lines.size()) -
                     ItemDisplayLayout::visibleDescriptionLines(layout);
    return over > 0 ? over : 0;
}

void noteItemDisplayRequest(ItemDisplayPanelState& state, bool held) {
    state.heldRequestPending = held;
    state.heldReleasedEarly = false;
    state.heldReleaseAgeFrames = 0;
    if (!held) state.heldOnly = false;
}

void updateHeldItemDisplay(ItemDisplayPanelState& state, bool rightButtonDown) {
    if (rightButtonDown) return;
    if (state.heldOnly) {
        // Row 25: "gone on release".
        state.isOpen = false;
        state.heldOnly = false;
    }
    if (state.heldRequestPending) {
        state.heldRequestPending = false;
        state.heldReleasedEarly = true;
        state.heldReleaseAgeFrames = 0;
    } else if (state.heldReleasedEarly &&
               ++state.heldReleaseAgeFrames > kHeldDisplayForgetFrames) {
        state.heldReleasedEarly = false;
    }
}

void applyItemDisplayShown(ItemDisplayPanelState& state,
                           const eqt::state::ItemDisplayShownData& data) {
    if (state.heldReleasedEarly) {
        // Row 24: the right button came up before the contents did.
        state.heldReleasedEarly = false;
        return;
    }
    state.heldOnly = state.heldRequestPending;
    state.heldRequestPending = false;
    state.isOpen = true;
    state.itemId = data.itemId;
    state.name = data.name;
    state.iconId = data.iconId;
    state.lines = data.lines;
    state.scrollOffset = 0;
}

void renderItemDisplayPopup(UIRenderer& ui, const UILayout& layout,
                            const ItemDisplayPanelState& state) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();

    // The frame. WDT_Filigree in the oracle; fable's one panel chrome here.
    ui.drawPanel(ItemDisplayLayout::windowRect(layout));

    // --- Style_Titlebar: the item's name ---
    const auto title = ItemDisplayLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText(state.name, title.UpperLeftCorner.X + 6,
                    title.UpperLeftCorner.Y + 2,
                    irr::video::SColor(255, 255, 215, 0));
    }

    // --- Style_Closebox ---
    const auto close = ItemDisplayLayout::closeBoxRect(layout);
    ui.drawRect(close, state.closeHovered
                    ? irr::video::SColor(220, 120, 60, 60)
                    : irr::video::SColor(200, 60, 60, 72));
    if (tb) {
        tb->addTextCentered("X", close, irr::video::SColor(255, 235, 235, 235));
    }

    // --- IDW_IconButton: RedIconBackground + a 40x40 decal ---
    // T430 (board B376, CLOSED): the decal is the real dragitem art now. The
    // oracle's own EQUI_ItemDisplay.xml gives IDW_IconButton
    // `<ButtonDrawTemplate><Normal>RedIconBackground</Normal>
    // <NormalDecal>BuffIcons</NormalDecal></ButtonDrawTemplate>` and swaps
    // that decal for the item's icon at runtime; the decal rect here is
    // already the 40x40 the sheets are cut at, so this is a 1:1 blit.
    //
    // The icon NUMBER that stood here from T421 until T430 is the fallback,
    // not the deleted placeholder: a client with no pack still says which
    // icon it would have drawn rather than showing an empty frame.
    const auto iconBtn = ItemDisplayLayout::iconButtonRect(layout);
    ui.drawRect(iconBtn, irr::video::SColor(220, 70, 24, 24));
    const auto decal = ItemDisplayLayout::iconDecalRect(layout);
    ui.drawRect(decal, irr::video::SColor(200, 18, 18, 24));
    if (!ui.drawItemIcon(decal, state.iconId) && tb && state.iconId != 0) {
        tb->addTextCentered(std::to_string(state.iconId), decal,
                            irr::video::SColor(255, 170, 170, 190));
    }

    // --- IDW_ItemDescription: the sunken STMLbox ---
    const auto desc = ItemDisplayLayout::descriptionRect(layout);
    ui.drawRect(desc, irr::video::SColor(200, 18, 18, 24));

    const int rows = state.visibleLines(layout);
    for (int row = 0; row < rows; ++row) {
        const size_t idx = static_cast<size_t>(state.scrollOffset + row);
        const auto r = ItemDisplayLayout::descriptionLineRect(layout, row);
        if (!tb) continue;
        tb->addText(state.lines[idx], r.UpperLeftCorner.X, r.UpperLeftCorner.Y,
                    irr::video::SColor(255, 225, 225, 225));
    }
}

bool hitTestItemDisplayCloseBox(const UILayout& layout,
                                const ItemDisplayPanelState& state,
                                int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return ItemDisplayLayout::closeBoxRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

bool itemDisplayContains(const UILayout& layout,
                         const ItemDisplayPanelState& state,
                         int mouseX, int mouseY) {
    if (!state.isOpen) return false;
    return ItemDisplayLayout::windowRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

} // namespace Graphics
} // namespace EQT
