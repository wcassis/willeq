// B999 (GitLab #319): the group and pet panels' overlap in the left HUD
// column, at 800x600.
//
// THE MEASURED DEFECT (review/B992/, the hud_all capture; issue #319): with
// the group and pet panels both open, the pet panel covers a group member
// row -- that row's HP bar is hidden and its name text is dimmed under the
// pet panel. The group frame the table gives it is
// 5 * (BAR_HEIGHT + MARGIN) + MARGIN = 94px, but renderGroupPanel() draws a
// BAR_HEIGHT + MARGIN title band above the first row and the same pitch per
// row -- 18 + 5*18 = 108px of content, so the 94px frame's bottom lands
// inside the member rows and the last rows overflow the frame. The pet
// panel, which starts MARGIN below the frame's bottom, paints over the
// overflow (at 800x600 the group frame ends at y=206, the pet frame spans
// y=210..262, and the fifth row -- name y=202..216, HP bar y=216..228 -- is
// under the pet panel). The rects this test prints are the ground truth
// (the numbers come from the program, never from reading the arithmetic):
// the red run at the parent and the rect table are logged in
// refactor/B999/README.md.
//
// What is pinned:
//  * the CARD's pair list (player, target, group, pet, buff, hotbar, chat,
//    spell gems): every pair, every supported resolution -- no two frame
//    rects intersect and every frame rect is wholly on screen;
//  * the table's own required co-open sets at every supported resolution --
//    the same guarantee tests/test_ui_layout_coopen.cpp gives the whole
//    table, kept here so a B999 layout edit that breaks a set other than
//    group x pet fails in this binary too;
//  * group x pet, THE pair, named as a required co-open set in the table,
//    so a table edit that drops the pair is caught here, not in a
//    throwaway pair list;
//  * the drawn CONTENT (THE red assertion at the parent): every group row
//    renderGroupPanel() paints -- the title band and all five member rows
//    at the render's own pitch, through the same BAR_HEIGHT/MARGIN
//    constants the render uses -- lies INSIDE the group frame and clears
//    the pet frame at every supported resolution. RED at the parent (the
//    hud_all capture's finding); GREEN at the head;
//  * the rect dump itself: `PrintRectsAndResSupported` writes one line per
//    window per resolution (the table in refactor/B999/README.md is a copy
//    of its stdout, never hand arithmetic).
//
// Headless: no device, no GL, no Irrlicht beyond the rect type. Same shape
// to tests/test_ui_layout_coopen.cpp.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_layout_table.h"
#include "client/graphics/ui/ui_window_rects.h"

#include <string>
#include <vector>

namespace {

using namespace EQT::Graphics;
using Rect = irr::core::rect<irr::s32>;

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

// Strict interior overlap: windows that share only an edge do not overlap
// (the same rule test_ui_layout_coopen.cpp uses).
bool overlaps(const Rect& a, const Rect& b) {
    return a.UpperLeftCorner.X < b.LowerRightCorner.X &&
           b.UpperLeftCorner.X < a.LowerRightCorner.X &&
           a.UpperLeftCorner.Y < b.LowerRightCorner.Y &&
           b.UpperLeftCorner.Y < a.LowerRightCorner.Y;
}

bool onScreen(const Rect& r, const UILayout& l) {
    return r.getWidth() > 0 && r.getHeight() > 0 && r.UpperLeftCorner.X >= 0 &&
           r.UpperLeftCorner.Y >= 0 && r.LowerRightCorner.X <= l.screenW &&
           r.LowerRightCorner.Y <= l.screenH;
}

std::string str(const Rect& r) {
    return "(" + std::to_string(r.UpperLeftCorner.X) + "," +
           std::to_string(r.UpperLeftCorner.Y) + ")-(" +
           std::to_string(r.LowerRightCorner.X) + "," +
           std::to_string(r.LowerRightCorner.Y) + ")";
}

// One rect renderGroupPanel() actually paints: the title band, or the FULL
// vertical extent of one member row -- the BAR_HEIGHT name line plus the
// BAR_HEIGHT-2 HP bar drawn in the same row (the row box, not the name box,
// because the HP bar's bottom edge is what the hud_all capture lost).
// renderGroupPanel in src/client/graphics/ui/static_panels.cpp: the title
// at frame top+2, the first row at top + BAR_HEIGHT + MARGIN, and a
// BAR_HEIGHT + MARGIN pitch per row. `row` is 0 = the title band, 1..5 a
// member row. The frame the render draws is layout.groupPanel when in a
// group (groupPanelFrameRect), so this is the content that must fit it.
// The row count is GroupPanelState::MAX_MEMBERS (5), the render's loop
// bound, kept local so this test stays header-only.
constexpr int kGroupMaxMembers = 5;

// The full painted height of one member row: the name line plus the HP bar
// (BAR_HEIGHT-2) that the render draws in the same row.
constexpr irr::s32 kGroupRowH =
    UILayout::BAR_HEIGHT + (UILayout::BAR_HEIGHT - 2);

Rect groupPaintedRect(const UILayout& l, int row) {
    const irr::s32 x = l.groupPanel.UpperLeftCorner.X;
    const irr::s32 w = l.groupPanel.getWidth();
    const irr::s32 top = l.groupPanel.UpperLeftCorner.Y;
    if (row == 0) {
        return {x, top, x + w, top + UILayout::BAR_HEIGHT + UILayout::MARGIN};
    }
    const irr::s32 y = top + UILayout::BAR_HEIGHT + UILayout::MARGIN +
                       (row - 1) * (UILayout::BAR_HEIGHT + UILayout::MARGIN);
    return {x, y, x + w, y + kGroupRowH};
}

// The card's minimum pair list: the always-on HUD panels a player can have
// open at once.
constexpr UIWindow kCardHud[] = {
    UIWindow::PlayerStatus, UIWindow::Target, UIWindow::Group,
    UIWindow::Pet,          UIWindow::Buffs,  UIWindow::Hotbar,
    UIWindow::Chat,         UIWindow::SpellGems};

// Every violation among `members` on `layout`, one line each.
std::vector<std::string> violations(const std::vector<UIWindow>& members,
                                    const UILayout& layout) {
    std::vector<std::string> out;
    for (size_t i = 0; i < members.size(); ++i) {
        const Rect a = windowRectFor(members[i], layout);
        if (!onScreen(a, layout)) {
            out.push_back(std::string(layoutRow(members[i]).name) +
                          " off screen " + str(a));
        }
        for (size_t j = i + 1; j < members.size(); ++j) {
            const Rect b = windowRectFor(members[j], layout);
            if (overlaps(a, b)) {
                out.push_back(std::string(layoutRow(members[i]).name) + " " +
                              str(a) + " overlaps " +
                              layoutRow(members[j]).name + " " + str(b));
            }
        }
    }
    return out;
}

}  // namespace

// The pair the card names. Must exist in the table (not just in this test):
// the table is the spec, and a table edit that drops the pair must be
// caught here.
TEST(UiLayoutRectsB999, GroupAndPetAreARequiredCoOpenSetInTheTable) {
    EXPECT_TRUE(inRequiredSet(UIWindow::Group, UIWindow::Pet))
        << "the card's pair must be a required co-open set in "
           "ui_layout_table.h, not just a test-local pair";
}

// The card's minimum pair list: every pair, every supported resolution --
// no intersection, every rect wholly on screen.
TEST(UiLayoutRectsB999, TheCardsPairListNeverIntersectsAtAnyResolution) {
    std::vector<UIWindow> hud(kCardHud, kCardHud + sizeof(kCardHud) / sizeof(kCardHud[0]));
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (const auto& v : violations(hud, l)) {
            ADD_FAILURE() << res.w << "x" << res.h << " " << v;
        }
    }
}

// The table's own required co-open sets at every supported resolution.
TEST(UiLayoutRectsB999, EveryRequiredCoOpenSetIsCleanAtEveryResolution) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (const auto& set : kRequiredCoOpenSets) {
            std::vector<UIWindow> members(set.members, set.members + set.count);
            for (const auto& v : violations(members, l)) {
                ADD_FAILURE() << res.w << "x" << res.h << " [" << set.name
                              << "] " << v;
            }
        }
    }
}

// THE drawn-content net (RED at the parent): renderGroupPanel() paints the
// title band and all five member rows; every one of those painted rects must
// lie INSIDE the group frame (the frame must hold its own content) and clear
// the pet frame (the hud_all capture's finding: a row under the pet panel).
// The fix is confined to the layout table: the frame grows to hold the rows
// (BAR_HEIGHT + MARGIN for the title plus the same pitch per row -- no row
// height changes, the content is the oracle's) and the pet panel, anchored
// below it, moves down with it.
TEST(UiLayoutRectsB999, TheGroupRowsFitTheFrameAndClearThePet) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        const Rect frame = l.groupPanel;
        const Rect pet = l.petPanel;
        for (int row = 0; row <= kGroupMaxMembers; ++row) {
            const Rect r = groupPaintedRect(l, row);
            EXPECT_GE(r.UpperLeftCorner.X, frame.UpperLeftCorner.X)
                << res.w << "x" << res.h << " row " << row << " " << str(r)
                << " left of frame " << str(frame);
            EXPECT_GE(r.UpperLeftCorner.Y, frame.UpperLeftCorner.Y)
                << res.w << "x" << res.h << " row " << row << " " << str(r)
                << " above frame " << str(frame);
            EXPECT_LE(r.LowerRightCorner.X, frame.LowerRightCorner.X)
                << res.w << "x" << res.h << " row " << row << " " << str(r)
                << " right of frame " << str(frame);
            EXPECT_LE(r.LowerRightCorner.Y, frame.LowerRightCorner.Y)
                << res.w << "x" << res.h << " row " << row << " " << str(r)
                << " runs past the group frame " << str(frame);
            EXPECT_FALSE(overlaps(r, pet))
                << res.w << "x" << res.h << " group row " << row << " "
                << str(r) << " is under the pet panel " << str(pet);
        }
    }
}

// The dump: one line per window per resolution, from windowRectFor() -- the
// same function each render body and hit test call. The rect table in
// refactor/B999/README.md is a copy of this test's stdout.
TEST(UiLayoutRectsB999, PrintRectsAndResSupported) {
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (const auto& row : kUILayoutTable) {
            const Rect r = windowRectFor(row.window, l);
            std::cout << res.w << "x" << res.h << "  " << row.name << "  "
                      << str(r) << "\n";
        }
    }
    SUCCEED();
}
