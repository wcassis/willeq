// B999 (GitLab #319): the group and pet panel's overlap under the left HUD
// column, at 800x600.
//
// THE MEASURED DEFECT (review/B992/, the hud_all capture): with the group
// and pet panels both open at 800x600, the pet panel covers the group
// panel's third member row -- the row's HP bar is hidden and its name text
// is dimmed under the pet panel. renderGroupPanel() lays each member row as
// a BAR_HEIGHT name line, a BAR_HEIGHT-2 HP bar and a MARGIN pad -- a
// BAR_HEIGHT + MARGIN pitch, GroupPanelState::MAX_MEMBERS rows -- but the
// table's group panel height (5 * (BAR_HEIGHT + MARGIN) + MARGIN) is
// MARGIN + a full row SHORT of the 14px title band plus those five rows, so
// the panel's bottom edge lands inside the third row and the pet panel,
// which starts MARGIN below the panel, paints over it.
//
// The rects this test prints are the ground truth (the numbers come from
// the program, never from reading the arithmetic): the red run at the
// parent is logged in refactor/B999/README.md, and the same test is GREEN
// at the head.
//
// What is pinned:
//  * for every pair the layout's co-open rules allow open together (the
//    minimum the card names -- player, target, group, pet, buff, hotbar,
//    chat, spell gems -- widened to the table's HUD sets: xp bar and
//    casting bar included) and every supported resolution, no two rects
//    intersect and every rect is wholly on screen;
//  * group x pet, THE pair, named as a required co-open set in the table,
//    so a layout change that re-opens the overlap fails there, not in a
//    throwaway pair loop;
//  * the rect dump itself: `PrintTheRectsAtEveryResolution` writes one line
//    per window per resolution (the table in refactor/B999/README.md is a
//    copy of its stdout, never hand arithmetic).
//
// Headless: no device, no GL, no Irrlicht beyond the rect type. Same shape
// as tests/test_ui_layout_coopen.cpp.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_layout_table.h"
#include "client/graphics/ui/ui_window_rects.h"

#include <string>

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

// The pair the card names. Must exist in the table (not just in this test):
// the table is the spec, and a future table edit that drops the pair must
// be a conscious decision caught here.
TEST(UiLayoutRectsB999, GroupAndPetAreARequiredCoOpenSetInTheTable) {
    EXPECT_TRUE(inRequiredSet(UIWindow::Group, UIWindow::Pet))
        << "the card's pair must be a required co-open set in "
           "ui_layout_table.h, not just a test-local pair";
}

// The card's minimum pair list, widened by the two always-on HUD members the
// table already carries (xp bar, casting bar -- the table's "HUD" and
// "HUD+casting bar" sets). Every pair, every resolution: no intersection,
// every rect wholly on screen. This is RED at the parent for group x pet at
// 800x600 (the hud_all capture's finding) and GREEN at the head.
TEST(UiLayoutRectsB999, TheCoOpenHudPairListNeverIntersectsAtAnyResolution) {
    const UIWindow hud[] = {
        UIWindow::PlayerStatus, UIWindow::Target, UIWindow::Group,
        UIWindow::Pet,         UIWindow::Buffs,  UIWindow::Hotbar,
        UIWindow::Chat,        UIWindow::SpellGems, UIWindow::XpBar,
        UIWindow::CastingBar};
    int pairs = 0;
    for (const auto& res : kSupportedResolutions) {
        const UILayout l = layoutAt(res.w, res.h);
        for (size_t i = 0; i < sizeof(hud) / sizeof(hud[0]); ++i) {
            const Rect a = windowRectFor(hud[i], l);
            if (!onScreen(a, l)) {
                ADD_FAILURE() << res.w << "x" << res.h << " "
                              << layoutRow(hud[i]).name << " off screen "
                              << str(a);
            }
            for (size_t j = i + 1; j < sizeof(hud) / sizeof(hud[0]); ++j) {
                const Rect b = windowRectFor(hud[j], l);
                if (overlaps(a, b)) {
                    ADD_FAILURE() << res.w << "x" << res.h << " "
                                  << layoutRow(hud[i]).name << " " << str(a)
                                  << " overlaps " << layoutRow(hud[j]).name
                                  << " " << str(b);
                }
                ++pairs;
            }
        }
    }
    EXPECT_GT(pairs, 0);
}

// THE dump: one line per window per resolution, from windowRectFor() -- the
// same function each render body and hit test call. The rect table in
// refactor/B999/README.md is a copy of this test's stdout.
TEST(UiLayoutRectsB999, PrintTheRectsAtEveryResolution) {
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

}  // namespace
