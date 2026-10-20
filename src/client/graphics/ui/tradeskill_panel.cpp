/*
 * T415 (board B321) — the tradeskill container window.
 *
 * A separate TU rather than more of static_panels.cpp (1,099 lines at this
 * tip): the file-size check wants substantial new code in new TUs, and this
 * is the same call loot_panel.cpp made in T290.
 *
 * The geometry is NOT here. Every rect comes from
 * include/client/graphics/ui/tradeskill_layout.h, which quotes
 * EQUI_TradeskillWnd.xml and writes out the anchor arithmetic — one shared
 * source for the draw body and the two hit tests, T57's rule.
 */

#include "client/graphics/ui/tradeskill_panel.h"

#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"

#include <fmt/format.h>
#include <algorithm>

namespace EQT {
namespace Graphics {

namespace {

void drawButton(UIRenderer& ui, TextBatch* tb,
                const irr::core::rect<irr::s32>& r,
                const std::string& label, bool enabled) {
    ui.drawRect(r, enabled ? irr::video::SColor(220, 50, 50, 70)
                           : irr::video::SColor(180, 35, 35, 40));
    ui.drawRect({r.UpperLeftCorner.X, r.UpperLeftCorner.Y,
                 r.LowerRightCorner.X, r.UpperLeftCorner.Y + 1},
                irr::video::SColor(255, 110, 110, 140));
    ui.drawRect({r.UpperLeftCorner.X, r.LowerRightCorner.Y - 1,
                 r.LowerRightCorner.X, r.LowerRightCorner.Y},
                irr::video::SColor(255, 30, 30, 40));
    if (tb) {
        tb->addTextCentered(label, r,
            enabled ? irr::video::SColor(255, 230, 230, 230)
                    : irr::video::SColor(255, 120, 120, 120));
    }
}

} // namespace

int tradeskillVisibleSlots(const TradeskillPanelState& state) {
    int n = static_cast<int>(state.slotCount);
    if (n < 0) n = 0;
    if (n > TradeskillPanelState::MAX_SLOTS) n = TradeskillPanelState::MAX_SLOTS;
    return n;
}

void applyTradeskillContainerOpened(
    TradeskillPanelState& state,
    const eqt::state::TradeskillContainerOpenedEvent& data) {
    state.isOpen = true;
    state.containerName = data.containerName;
    state.isWorldObject = data.isWorldObject;
    state.slotCount = data.slotCount;
    state.objectId = data.objectId;
    state.inventorySlot = data.inventorySlot;
    state.hoveredSlot = -1;
    // A second container must never show the first one's contents; the
    // server re-sends every occupied index on open.
    for (auto& s : state.slots) {
        s = TradeskillSlotDisplay{};
    }
}

void applyTradeskillContainerClosed(
    TradeskillPanelState& state,
    const eqt::state::TradeskillContainerClosedEvent& data) {
    (void)data;  // wasWorldObject/objectId identify WHICH container closed;
                 // fable can only have one open at a time (there is a single
                 // ctx_.active_tradeskill_object_id), so any close closes it.
    state = TradeskillPanelState{};
}

void applyTradeskillSlotChanged(
    TradeskillPanelState& state,
    const eqt::state::InventorySlotChangedData& data) {
    if (!eqt::inventory::isWorldContainerItemSlot(data.slotId)) {
        return;
    }
    const int idx = data.slotId - eqt::inventory::WORLD_BEGIN;
    if (idx < 0 || idx >= TradeskillPanelState::MAX_SLOTS) {
        return;
    }
    auto& slot = state.slots[idx];
    slot.hasItem = data.hasItem;
    slot.itemName = data.hasItem ? data.itemName : std::string();
    slot.iconId = data.hasItem ? data.iconId : 0;
    slot.quantity = data.hasItem ? static_cast<int32_t>(data.quantity) : 0;
}

void renderTradeskillPopup(UIRenderer& ui, const UILayout& layout,
                           const TradeskillPanelState& state) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    const auto& popup = TradeskillLayout::windowRect(layout);

    ui.drawPanelFor(popup, UIWindow::Tradeskill);

    // COMBW_ContainerName — the title is the container's own name.
    if (tb) {
        tb->addTextCentered(
            state.containerName.empty() ? std::string("Tradeskill Container")
                                        : state.containerName,
            TradeskillLayout::titleRect(layout),
            irr::video::SColor(255, 255, 215, 0));
    }

    const int visible = tradeskillVisibleSlots(state);
    for (int i = 0; i < visible; ++i) {
        const auto r = TradeskillLayout::slotRect(layout, i);
        ui.drawSprite(r, static_cast<uint8_t>(UISprite::SlotBackground));
        if (state.hoveredSlot == i) {
            ui.drawRect(r, irr::video::SColor(60, 255, 255, 255));
        }
        if (!state.slots[i].hasItem) continue;

        // T430 (board B376): the container's cells carry an A_DragItem decal
        // in the oracle (EQUI_TradeskillWnd.xml's COMBW_Slot buttons name it
        // by that exact element), so draw the real icon and keep the
        // 3-character abbreviation only as the no-art fallback.
        const bool drewIcon = ui.drawItemIcon(r, state.slots[i].iconId);
        if (!tb) continue;
        if (!drewIcon) {
            tb->addText(state.slots[i].itemName.substr(
                            0, std::min<size_t>(3, state.slots[i].itemName.size())),
                        r.UpperLeftCorner.X + 3, r.UpperLeftCorner.Y + 2,
                        irr::video::SColor(255, 220, 220, 220));
        }
        if (state.slots[i].quantity > 1) {
            tb->addText(fmt::format("{}", state.slots[i].quantity),
                        r.UpperLeftCorner.X + 3, r.UpperLeftCorner.Y + 14,
                        irr::video::SColor(255, 180, 220, 180));
        }
    }

    drawButton(ui, tb, TradeskillLayout::combineRect(layout), "Combine", true);
    drawButton(ui, tb, TradeskillLayout::closeRect(layout), "Close", true);

    // COMBW_LeftSideArea -- the search box, the recipe list, the favourites
    // row (T419, board B335). Its own TU; see tradeskill_recipe_list.cpp.
    renderTradeskillRecipeList(ui, layout, state);
}

TradeskillButton hitTestTradeskillButton(const UILayout& layout,
                                         const TradeskillPanelState& state,
                                         int mouseX, int mouseY) {
    if (!state.isOpen) return TradeskillButton::None;
    const irr::core::vector2di p(mouseX, mouseY);
    if (TradeskillLayout::combineRect(layout).isPointInside(p)) {
        return TradeskillButton::Combine;
    }
    if (TradeskillLayout::closeRect(layout).isPointInside(p)) {
        return TradeskillButton::Close;
    }
    // T421 (board B365): the four the left side drew and could not return.
    if (TradeskillLayout::searchButtonRect(layout).isPointInside(p)) {
        return TradeskillButton::Search;
    }
    if (TradeskillLayout::addFavoriteRect(layout).isPointInside(p)) {
        return TradeskillButton::AddFavorite;
    }
    if (TradeskillLayout::removeFavoriteRect(layout).isPointInside(p)) {
        return TradeskillButton::RemoveFavorite;
    }
    if (TradeskillLayout::showFavoritesRect(layout).isPointInside(p)) {
        return TradeskillButton::ShowFavorites;
    }
    return TradeskillButton::None;
}

int hitTestTradeskillSlot(const UILayout& layout,
                          const TradeskillPanelState& state,
                          int mouseX, int mouseY) {
    if (!state.isOpen) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    const int visible = tradeskillVisibleSlots(state);
    for (int i = 0; i < visible; ++i) {
        if (TradeskillLayout::slotRect(layout, i).isPointInside(p)) {
            return i;
        }
    }
    return -1;
}

} // namespace Graphics
} // namespace EQT
