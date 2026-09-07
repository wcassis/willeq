/*
 * T290 — the loot window, restored on the static-panel pattern.
 *
 * Ancestor: include/client/graphics/ui/loot_window.h + .cpp (163 + 867
 * lines), deleted by U07c2 `0f25cb57` together with WindowManager. This file
 * is the port, not a redesign: the state machine (open/close/add/remove),
 * the packed "display row -> corpse slot" indirection, the scrolled visible
 * window, and the Loot All / Close buttons all come from there. What is gone
 * is the WindowBase shell (title-bar dragging, per-window callbacks, the
 * InventoryManager-backed canLootItem()/canLootAll() pre-flight), because
 * the static UI has neither a window manager nor a renderer-side
 * InventoryManager to ask — the server refuses an impossible loot and says
 * so in chat, which is the same answer one hop later.
 *
 * A separate TU rather than more of static_panels.cpp (915 lines before this
 * unit): the file-size check wants substantial new code in new TUs.
 */

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/text_batch.h"
#include "client/state/event_bus.h"

#include <fmt/format.h>
#include <algorithm>

namespace EQT {
namespace Graphics {

namespace {

// Geometry, in one place, used by BOTH renderLootPopup() and the two
// hit-test functions. The old window kept these in UISettings; there is no
// UISettings any more, and duplicating them across the render body and the
// input body is exactly how the buff bar's rows and slots drifted apart
// before T277.
constexpr irr::s32 kPad        = 8;
constexpr irr::s32 kTitleH     = 20;
constexpr irr::s32 kButtonW    = 80;
constexpr irr::s32 kButtonH    = 16;
constexpr irr::s32 kRowH       = 22;
constexpr irr::s32 kRowGap     = 2;
// T430 (board B376): the dragitem icon square at the head of a loot row. One
// pixel of inset top and bottom inside the 22px row.
constexpr irr::s32 kRowIcon    = 20;

irr::s32 rowsTop(const UILayout& layout) {
    // title row + the Loot All button row above the list
    return layout.centerPopup.UpperLeftCorner.Y + kTitleH + kPad + kButtonH + kPad;
}

irr::core::rect<irr::s32> lootAllRect(const UILayout& layout) {
    irr::s32 x = layout.centerPopup.UpperLeftCorner.X + kPad;
    irr::s32 y = layout.centerPopup.UpperLeftCorner.Y + kTitleH + kPad;
    return {x, y, x + kButtonW, y + kButtonH};
}

irr::core::rect<irr::s32> closeRect(const UILayout& layout) {
    irr::s32 cx = (layout.centerPopup.UpperLeftCorner.X +
                   layout.centerPopup.LowerRightCorner.X) / 2;
    irr::s32 y = layout.centerPopup.LowerRightCorner.Y - kPad - kButtonH;
    return {cx - kButtonW / 2, y, cx + kButtonW / 2, y + kButtonH};
}

// The visible window over the packed item list: [first, last).
void visibleRange(const LootPanelState& state, int& first, int& last) {
    int total = static_cast<int>(state.items.size());
    first = std::max(0, std::min(state.scrollOffset, std::max(0, total - 1)));
    last = std::min(first + LootPanelState::VISIBLE_ROWS, total);
}

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
                irr::video::SColor(255, 110, 110, 140));
    if (tb) {
        tb->addTextCentered(label, r, enabled
            ? irr::video::SColor(255, 230, 230, 230)
            : irr::video::SColor(160, 130, 130, 130));
    }
}

}  // namespace

void renderLootPopup(UIRenderer& ui, const UILayout& layout,
                     const LootPanelState& state) {
    if (!state.isOpen) return;

    auto* tb = ui.getTextBatch();
    const auto& popup = layout.centerPopup;

    ui.drawPanel(popup);

    // Title: the corpse's own name, as the old window's setTitle() did.
    if (tb) {
        tb->addText(state.corpseName.empty() ? std::string("Corpse") : state.corpseName,
            popup.UpperLeftCorner.X + kPad, popup.UpperLeftCorner.Y + 4,
            irr::video::SColor(255, 255, 215, 0));
    }

    drawButton(ui, tb, lootAllRect(layout), "Loot All", !state.items.empty());

    irr::s32 startX = popup.UpperLeftCorner.X + kPad;
    irr::s32 contentW = popup.getWidth() - kPad * 2;
    irr::s32 startY = rowsTop(layout);

    if (state.items.empty() && tb) {
        tb->addTextCentered("(empty)",
            {startX, startY, startX + contentW, startY + kRowH},
            irr::video::SColor(180, 150, 150, 150));
    }

    int first = 0, last = 0;
    visibleRange(state, first, last);

    for (int i = first; i < last; ++i) {
        int row = i - first;
        irr::s32 y = startY + row * (kRowH + kRowGap);
        irr::core::rect<irr::s32> rowRect(startX, y, startX + contentW, y + kRowH);

        ui.drawRect(rowRect, irr::video::SColor(90, 20, 20, 30));
        if (state.hoveredRow == row) {
            ui.drawRect(rowRect, irr::video::SColor(60, 255, 255, 255));
        }

        // T430 (board B376): the oracle's LW_LootSlot cells are 40x40
        // InvSlots carrying the item's dragitem decal
        // (`<Size><CX>40</CX><CY>40</CY></Size>` in EQUI_LootWnd.xml). A row
        // here is 22px, not 40, so the icon is drawn as a 20px square at the
        // head of the row and the name follows it -- the row stays a LINE,
        // which is what makes this window scannable, but it now says what
        // the thing looks like as well as what it is called.
        irr::s32 textX = startX + 4;
        const irr::core::rect<irr::s32> iconRect(startX + 1, y + 1,
                                                 startX + kRowIcon + 1,
                                                 y + kRowIcon + 1);
        if (ui.drawItemIcon(iconRect, state.items[i].iconId)) {
            textX = startX + kRowIcon + 5;
        }

        if (tb) {
            // Full item name, not the 3-character abbreviation the equipment
            // slots use: a loot row is a line, not a 32px icon cell, and the
            // whole point of the window is deciding what to take.
            tb->addText(state.items[i].itemName, textX, y + 4,
                irr::video::SColor(255, 210, 210, 210));
        }
    }

    // Scroll position, in the old window's "showing N of M" spirit (its
    // scrollbar thumb needs pixels this UI has no sprite for).
    int total = static_cast<int>(state.items.size());
    if (tb && total > LootPanelState::VISIBLE_ROWS) {
        irr::s32 navY = closeRect(layout).UpperLeftCorner.Y - kPad - 14;
        tb->addTextCentered(fmt::format("{}-{} of {}", first + 1, last, total),
            {startX, navY, startX + contentW, navY + 14},
            irr::video::SColor(200, 150, 150, 150));
    }

    drawButton(ui, tb, closeRect(layout), "Close", true);
}

int hitTestLootRow(const UILayout& layout, const LootPanelState& state,
                   int mouseX, int mouseY) {
    if (!state.isOpen) return -1;

    irr::s32 startX = layout.centerPopup.UpperLeftCorner.X + kPad;
    irr::s32 contentW = layout.centerPopup.getWidth() - kPad * 2;
    if (mouseX < startX || mouseX >= startX + contentW) return -1;

    irr::s32 startY = rowsTop(layout);
    int first = 0, last = 0;
    visibleRange(state, first, last);

    for (int i = first; i < last; ++i) {
        irr::s32 y = startY + (i - first) * (kRowH + kRowGap);
        if (mouseY >= y && mouseY < y + kRowH) {
            return i;  // index into state.items — NOT the corpse slot
        }
    }
    return -1;
}

LootButton hitTestLootButton(const UILayout& layout, const LootPanelState& state,
                             int mouseX, int mouseY) {
    if (!state.isOpen) return LootButton::None;
    const irr::core::vector2di p(mouseX, mouseY);
    if (lootAllRect(layout).isPointInside(p)) return LootButton::LootAll;
    if (closeRect(layout).isPointInside(p)) return LootButton::Close;
    return LootButton::None;
}

// --- the four bridge arms -------------------------------------------------

void applyLootWindowOpened(LootPanelState& state,
                           const eqt::state::LootWindowOpenedData& data) {
    state.isOpen = true;
    state.corpseId = data.corpseId;
    state.corpseName = data.corpseName;
    state.items.clear();          // old window's open() called clearLoot()
    state.scrollOffset = 0;
    state.hoveredRow = -1;
}

void applyLootWindowClosed(LootPanelState& state,
                           const eqt::state::LootWindowClosedData& data) {
    // A close for a corpse we are not showing is not ours to act on (the
    // old window guarded the same way with its already-closed early return).
    if (state.isOpen && state.corpseId != data.corpseId) return;
    state = LootPanelState{};
}

void applyLootItemAdded(LootPanelState& state,
                        const eqt::state::LootItemAddedData& data) {
    if (!state.isOpen) return;
    if (state.corpseId != data.corpseId) return;

    // Keep the list PACKED and in ascending corpse-slot order — the old
    // window got this from std::map's ordering; here it is an explicit
    // sorted insert. A repeat of the same slot updates in place rather than
    // duplicating (the server re-sends a slot after a failed loot).
    auto it = std::lower_bound(state.items.begin(), state.items.end(), data.slot,
        [](const LootItemDisplay& e, uint8_t s) { return e.slot < s; });
    if (it != state.items.end() && it->slot == data.slot) {
        it->itemId = data.itemId;
        it->itemName = data.itemName;
        it->iconId = data.iconId;
        return;
    }
    state.items.insert(it, LootItemDisplay{data.slot, data.itemId,
                                           data.itemName, data.iconId});
}

void applyLootItemRemoved(LootPanelState& state,
                          const eqt::state::LootItemRemovedData& data) {
    if (!state.isOpen) return;
    if (state.corpseId != data.corpseId) return;

    auto it = std::lower_bound(state.items.begin(), state.items.end(), data.slot,
        [](const LootItemDisplay& e, uint8_t s) { return e.slot < s; });
    if (it == state.items.end() || it->slot != data.slot) return;
    state.items.erase(it);

    state.scrollOffset = std::min(state.scrollOffset, state.maxScrollOffset());
    state.hoveredRow = -1;

    // The old window auto-closed itself when the last item left. It must NOT
    // here: the server owns the looting state, and CombatManager only leaves
    // its LootingItems state on the real close. An emptied corpse stays up
    // showing "(empty)" until LootWindowClosed arrives — which /lootall's
    // own close, or walking away, produces.
}

}  // namespace Graphics
}  // namespace EQT
