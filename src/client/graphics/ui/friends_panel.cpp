/*
 * B897 (GitLab #213) -- the Friends window. Its own TU (M00). Geometry is
 * include/client/graphics/ui/friends_layout.h, which quotes
 * EQUI_FriendsWnd.xml; this file computes no rect.
 */

#include "client/graphics/ui/friends_panel.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/state/event_bus.h"
#include "client/state/social_lists.h"

namespace EQT {
namespace Graphics {

namespace {

const irr::video::SColor kText(255, 225, 225, 225);
const irr::video::SColor kTitle(255, 255, 215, 0);
// <TabTextColor> 255,255,255 and <TabTextActiveColor> 255,255,0, verbatim.
const irr::video::SColor kTabText(255, 255, 255, 255);
const irr::video::SColor kTabTextActive(255, 255, 255, 0);
const irr::video::SColor kFrame(255, 80, 80, 80);

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return r.isPointInside(irr::core::vector2di(x, y));
}

// `enabled == false` is the oracle's A_BtnDisabled state: friends.tsv row 12
// measured Contact and Delete disabled without a selection and Add disabled
// while the name field is empty. A disabled button is also inert (the click
// resolves to nothing in applyFriendsClick()).
void drawButton(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
                const char* label, bool enabled = true) {
    ui.drawRect(r, enabled ? irr::video::SColor(200, 55, 55, 66)
                           : irr::video::SColor(160, 40, 40, 46));
    ui.drawFrame(r, kFrame);
    if (tb) {
        tb->addTextCentered(label, r, enabled ? kText : irr::video::SColor(255, 120, 120, 128));
    }
}

// WDT_Inner Editbox: the same sunken field and blinking caret the tradeskill
// window's three Editboxes draw (T421), because it is the same widget.
void drawField(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
               const std::string& text, bool focused) {
    ui.drawRect(r, irr::video::SColor(200, 18, 18, 24));
    ui.drawFrame(r, focused ? irr::video::SColor(255, 120, 150, 200) : kFrame);
    if (!tb) return;
    if (!text.empty()) {
        tb->addText(text, r.UpperLeftCorner.X + 3, r.UpperLeftCorner.Y + 5, kText);
    }
    if (focused) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        if ((ms / 500) % 2 == 0) {
            const irr::s32 cx = r.UpperLeftCorner.X + 3 + tb->getTextWidth(text);
            ui.drawRect({cx, r.UpperLeftCorner.Y + 4, cx + 1, r.LowerRightCorner.Y - 4},
                        irr::video::SColor(255, 255, 255, 255));
        }
    }
}

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

// --- state -----------------------------------------------------------------

int FriendsPanelState::maxScroll(int page) const {
    if (page < 0 || page >= PAGE_COUNT) return 0;
    return std::max(0, static_cast<int>(names[page].size()) - FriendsLayout::VISIBLE_ROWS);
}

std::string FriendsPanelState::selectedName(int page) const {
    if (page < 0 || page >= PAGE_COUNT) return std::string();
    const int s = selected[page];
    if (s < 0 || s >= static_cast<int>(names[page].size())) return std::string();
    return names[page][static_cast<size_t>(s)];
}

void applySocialListsChanged(FriendsPanelState& state,
                             const eqt::state::SocialListsChangedData& data) {
    state.names[0] = data.friends;
    state.names[1] = data.ignored;
    for (int p = 0; p < FriendsPanelState::PAGE_COUNT; ++p) {
        const int n = static_cast<int>(state.names[p].size());
        if (state.selected[p] >= n) state.selected[p] = -1;
        state.scroll[p] = clampInt(state.scroll[p], 0, state.maxScroll(p));
    }
}

// --- draw ------------------------------------------------------------------

void renderFriendsWindow(UIRenderer& ui, const UILayout& layout,
                         const FriendsPanelState& state) {
    if (!state.isOpen) return;
    auto* tb = ui.getTextBatch();
    const int page = clampInt(state.activePage, 0, FriendsPanelState::PAGE_COUNT - 1);

    ui.drawPanelFor(FriendsLayout::windowRect(layout), UIWindow::Friends);

    // --- Style_Titlebar: <Text>Friends Window</Text> ---
    const auto title = FriendsLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) {
        tb->addText("Friends Window", title.UpperLeftCorner.X + 6,
                    title.UpperLeftCorner.Y + 5, kTitle);
    }
    const auto close = FriendsLayout::closeBoxRect(layout);
    ui.drawRect(close, state.closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                          : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", close, kText);

    // --- FW_Subwindows: the tab strip, then the page frame ---
    for (int p = 0; p < FriendsPanelState::PAGE_COUNT; ++p) {
        const auto tab = FriendsLayout::tabRect(layout, p);
        ui.drawRect(tab, p == page ? irr::video::SColor(220, 50, 50, 64)
                                   : irr::video::SColor(200, 30, 30, 38));
        ui.drawFrame(tab, kFrame);
        if (tb) {
            tb->addTextCentered(FriendsLayout::tabText(p), tab,
                                p == page ? kTabTextActive : kTabText);
        }
    }
    ui.drawFrame(FriendsLayout::pageRect(layout), kFrame);

    // --- the list (FW_FriendsList / FW_IgnoreList) ---
    const auto list = FriendsLayout::listRect(layout);
    ui.drawRect(list, irr::video::SColor(200, 18, 18, 24));
    ui.drawFrame(list, kFrame);
    const auto& names = state.names[page];
    for (int i = 0; i < FriendsLayout::VISIBLE_ROWS; ++i) {
        const int idx = state.scroll[page] + i;
        if (idx >= static_cast<int>(names.size())) break;
        const auto row = FriendsLayout::rowRect(layout, i);
        if (idx == state.selected[page]) {
            ui.drawRect(row, irr::video::SColor(160, 70, 90, 140));
        } else if (idx == state.hoveredRow) {
            ui.drawRect(row, irr::video::SColor(60, 255, 255, 255));
        }
        if (tb) {
            tb->addText(names[static_cast<size_t>(idx)], row.UpperLeftCorner.X + 3,
                        row.UpperLeftCorner.Y + 1, kText);
        }
    }
    // Style_VScroll: the strip, with a thumb when the list overflows.
    const auto strip = FriendsLayout::scrollStripRect(layout);
    ui.drawRect(strip, irr::video::SColor(200, 34, 34, 42));
    const int maxScroll = state.maxScroll(page);
    if (maxScroll > 0) {
        const irr::s32 h = strip.getHeight();
        const irr::s32 thumbH =
            std::max<irr::s32>(8, h * FriendsLayout::VISIBLE_ROWS /
                                      static_cast<irr::s32>(names.size()));
        const irr::s32 y = strip.UpperLeftCorner.Y +
                           (h - thumbH) * state.scroll[page] / maxScroll;
        ui.drawRect({strip.UpperLeftCorner.X + 2, y, strip.LowerRightCorner.X - 2, y + thumbH},
                    irr::video::SColor(220, 110, 110, 135));
    }

    // --- the Editbox and the buttons ---
    drawField(ui, tb, FriendsLayout::nameInputRect(layout),
              state.nameEntry[page].inputText, state.focusedEntry == page);
    const bool hasSelection = !state.selectedName(page).empty();
    const bool hasName =
        !eqt::state::SocialNameFromArgs(state.nameEntry[page].inputText).empty();
    drawButton(ui, tb, FriendsLayout::addButtonRect(layout), "Add", hasName);
    drawButton(ui, tb, FriendsLayout::deleteButtonRect(layout), "Delete", hasSelection);
    if (page == static_cast<int>(FriendsLayout::Page::Friends)) {
        drawButton(ui, tb, FriendsLayout::contactButtonRect(layout), "Contact", hasSelection);
        drawButton(ui, tb, FriendsLayout::whoButtonRect(layout), "Who");
    }
}

// --- hit test --------------------------------------------------------------

FriendsClickTarget hitTestFriendsWindow(const UILayout& layout,
                                        const FriendsPanelState& state,
                                        int x, int y) {
    FriendsClickTarget t;
    if (!state.isOpen || !inside(FriendsLayout::windowRect(layout), x, y)) return t;
    const int page = clampInt(state.activePage, 0, FriendsPanelState::PAGE_COUNT - 1);

    if (inside(FriendsLayout::closeBoxRect(layout), x, y)) { t.hit = FriendsHit::Close; return t; }
    if (inside(FriendsLayout::tabRect(layout, 0), x, y)) { t.hit = FriendsHit::Tab0; return t; }
    if (inside(FriendsLayout::tabRect(layout, 1), x, y)) { t.hit = FriendsHit::Tab1; return t; }

    const auto strip = FriendsLayout::scrollStripRect(layout);
    if (inside(strip, x, y)) {
        t.hit = (y < strip.getCenter().Y) ? FriendsHit::ScrollUp : FriendsHit::ScrollDown;
        return t;
    }
    for (int i = 0; i < FriendsLayout::VISIBLE_ROWS; ++i) {
        if (!inside(FriendsLayout::rowRect(layout, i), x, y)) continue;
        const int idx = state.scroll[page] + i;
        if (idx < static_cast<int>(state.names[page].size())) {
            t.hit = FriendsHit::Row;
            t.row = idx;
            return t;
        }
        break;
    }
    if (inside(FriendsLayout::nameInputRect(layout), x, y)) { t.hit = FriendsHit::NameInput; return t; }
    if (inside(FriendsLayout::addButtonRect(layout), x, y)) { t.hit = FriendsHit::Add; return t; }
    if (inside(FriendsLayout::deleteButtonRect(layout), x, y)) { t.hit = FriendsHit::Delete; return t; }
    if (page == static_cast<int>(FriendsLayout::Page::Friends)) {
        if (inside(FriendsLayout::contactButtonRect(layout), x, y)) { t.hit = FriendsHit::Contact; return t; }
        if (inside(FriendsLayout::whoButtonRect(layout), x, y)) { t.hit = FriendsHit::Who; return t; }
    }
    t.hit = FriendsHit::Window;
    return t;
}

FriendsAction applyFriendsClick(FriendsPanelState& state, const FriendsClickTarget& target) {
    FriendsAction a;
    const int page = clampInt(state.activePage, 0, FriendsPanelState::PAGE_COUNT - 1);
    a.page = page;

    // Any click that is not on the Editbox takes the caret away from it.
    if (target.hit != FriendsHit::NameInput && target.hit != FriendsHit::Add &&
        target.hit != FriendsHit::None) {
        if (state.focusedEntry >= 0) state.nameEntry[state.focusedEntry].inputActive = false;
        state.focusedEntry = -1;
    }

    switch (target.hit) {
        case FriendsHit::None:
        case FriendsHit::Window:
            break;
        case FriendsHit::Close:
            a.kind = FriendsAction::Kind::Close;
            break;
        case FriendsHit::Tab0:
        case FriendsHit::Tab1:
            state.activePage = (target.hit == FriendsHit::Tab0) ? 0 : 1;
            state.hoveredRow = -1;
            break;
        case FriendsHit::Row:
            if (target.row >= 0 && target.row < static_cast<int>(state.names[page].size())) {
                state.selected[page] = target.row;
            }
            break;
        case FriendsHit::ScrollUp:
            state.scroll[page] = clampInt(state.scroll[page] - FriendsLayout::VISIBLE_ROWS, 0,
                                          state.maxScroll(page));
            break;
        case FriendsHit::ScrollDown:
            state.scroll[page] = clampInt(state.scroll[page] + FriendsLayout::VISIBLE_ROWS, 0,
                                          state.maxScroll(page));
            break;
        case FriendsHit::NameInput:
            if (state.focusedEntry != page) {
                if (state.focusedEntry >= 0) {
                    state.nameEntry[state.focusedEntry].inputActive = false;
                }
                state.focusedEntry = page;
                state.nameEntry[page].inputActive = true;
                state.nameEntry[page].cursorPos =
                    static_cast<int>(state.nameEntry[page].inputText.size());
            }
            break;
        case FriendsHit::Add: {
            const std::string name =
                eqt::state::SocialNameFromArgs(state.nameEntry[page].inputText);
            if (!name.empty()) {
                a.kind = FriendsAction::Kind::Add;
                a.name = name;
                clearTextEntry(state.nameEntry[page]);
                if (state.focusedEntry == page) state.nameEntry[page].inputActive = true;
            }
            break;
        }
        case FriendsHit::Delete: {
            const std::string name = state.selectedName(page);
            if (!name.empty()) {
                a.kind = FriendsAction::Kind::Remove;
                a.name = name;
                state.selected[page] = -1;
            }
            break;
        }
        case FriendsHit::Contact:
            if (page == 0) {
                const std::string name = state.selectedName(page);
                if (!name.empty()) {
                    a.kind = FriendsAction::Kind::Contact;
                    a.name = name;
                }
            }
            break;
        case FriendsHit::Who:
            // friends.tsv rows 13-14: no selection asks about the stored
            // list; a selected friend is a who-all for that one name.
            if (page == 0) {
                a.kind = FriendsAction::Kind::Who;
                a.name = state.selectedName(page);
            }
            break;
    }
    return a;
}

}  // namespace Graphics
}  // namespace EQT
