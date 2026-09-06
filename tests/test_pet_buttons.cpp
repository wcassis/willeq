// T419 (board B352) -- the pet window's command-button row.
//
// `renderPetPanel()` drew the ten pet command indicators as a single run of
// text starting at `petPanel.top + 34` in a panel that is 40px tall, so the
// labels hung OUTSIDE the frame (visible in all three columns of
// review/T416/crops/pet_3way.png, which is how it was found), and the run's
// `bx > right - 10` guard silently dropped the tenth ("SH", SpellHold).
//
// Pure geometry: no Irrlicht device, no server, no DISPLAY. What is pinned:
// all ten rects exist, all ten lie inside the panel, none overlaps another,
// the order is row-major, and the panel is sized from the grid's own
// constants and still clears its neighbours in fable's left column at every
// supported resolution.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/pet_layout.h"
#include "client/pet_constants.h"

#include <irrlicht.h>
#include <string>

using namespace EQT::Graphics;

namespace {

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

UILayout layout800() { return layoutAt(800, 600); }

std::string rectStr(const irr::core::rect<irr::s32>& r) {
    return "{" + std::to_string(r.UpperLeftCorner.X) + "," +
           std::to_string(r.UpperLeftCorner.Y) + "," +
           std::to_string(r.LowerRightCorner.X) + "," +
           std::to_string(r.LowerRightCorner.Y) + "}";
}

}  // namespace

// ===========================================================================
// Leg B -- B352, the pet command grid
// ===========================================================================

// The defect, stated as an assertion: every one of the ten indicators has a
// rect, and every rect is INSIDE the panel. Before T419 the row started at
// petPanel.top + 34 in a 40px panel, so the labels were drawn below the frame.
TEST(PetButtonsT419, EveryButtonRectLiesInsideThePanel) {
    const struct { int w, h; } resolutions[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    for (const auto& rc : resolutions) {
        const UILayout l = layoutAt(rc.w, rc.h);
        const auto& p = l.petPanel;
        ASSERT_EQ(PetLayout::BUTTON_COUNT, EQT::PET_BUTTON_COUNT)
            << "the grid must hold exactly the PetButton set";

        for (int i = 0; i < PetLayout::BUTTON_COUNT; ++i) {
            const auto r = PetLayout::buttonRect(l, i);
            EXPECT_GT(r.getWidth(), 0) << rc.w << "x" << rc.h << " button " << i;
            EXPECT_GT(r.getHeight(), 0) << rc.w << "x" << rc.h << " button " << i;
            EXPECT_GE(r.UpperLeftCorner.X, p.UpperLeftCorner.X)
                << rc.w << "x" << rc.h << " button " << i << " " << rectStr(r);
            EXPECT_GE(r.UpperLeftCorner.Y, p.UpperLeftCorner.Y)
                << rc.w << "x" << rc.h << " button " << i << " " << rectStr(r);
            EXPECT_LE(r.LowerRightCorner.X, p.LowerRightCorner.X)
                << rc.w << "x" << rc.h << " button " << i << " " << rectStr(r);
            EXPECT_LE(r.LowerRightCorner.Y, p.LowerRightCorner.Y)
                << rc.w << "x" << rc.h << " button " << i << " runs past the panel "
                << rectStr(r) << " panel " << rectStr(p);
        }
    }
}

// The tenth button, by name. The old row's `bx > right - 10` guard dropped
// "SH" (SpellHold) without a word; a grid cannot.
TEST(PetButtonsT419, TheTenthButtonHasItsOwnCell) {
    const UILayout l = layout800();
    const auto tenth = PetLayout::buttonRect(l, EQT::PET_BUTTON_SPELLHOLD);
    EXPECT_GT(tenth.getWidth(), 0);
    EXPECT_LE(tenth.LowerRightCorner.X, l.petPanel.LowerRightCorner.X);
    EXPECT_LE(tenth.LowerRightCorner.Y, l.petPanel.LowerRightCorner.Y);
    EXPECT_EQ(EQT::PET_BUTTON_SPELLHOLD, PetLayout::BUTTON_COUNT - 1);
}

// No two cells share a pixel, and the order is row-major -- the order the
// oracle lays PIW_FollowButton..PIW_LostButton out in.
TEST(PetButtonsT419, CellsAreDisjointAndLaidOutRowMajor) {
    const UILayout l = layout800();
    for (int i = 0; i < PetLayout::BUTTON_COUNT; ++i) {
        const auto a = PetLayout::buttonRect(l, i);
        for (int j = i + 1; j < PetLayout::BUTTON_COUNT; ++j) {
            EXPECT_FALSE(a.isRectCollided(PetLayout::buttonRect(l, j)))
                << "button " << i << " " << rectStr(a) << " overlaps button " << j;
        }
        if (i % PetLayout::BUTTON_COLS > 0) {
            EXPECT_GT(a.UpperLeftCorner.X,
                      PetLayout::buttonRect(l, i - 1).UpperLeftCorner.X)
                << "button " << i << " should be right of its predecessor";
            EXPECT_EQ(a.UpperLeftCorner.Y,
                      PetLayout::buttonRect(l, i - 1).UpperLeftCorner.Y)
                << "button " << i << " should share its predecessor's row";
        } else if (i > 0) {
            EXPECT_GT(a.UpperLeftCorner.Y,
                      PetLayout::buttonRect(l, i - 1).UpperLeftCorner.Y)
                << "button " << i << " should start a new row";
        }
    }
    EXPECT_EQ(PetLayout::buttonRect(l, -1).getWidth(), 0);
    EXPECT_EQ(PetLayout::buttonRect(l, PetLayout::BUTTON_COUNT).getWidth(), 0);
}

// The grid sits below the name row and the HP bar, and the panel is exactly
// as tall as the two plus the grid -- so a change to one constant cannot
// re-create B352 by growing the grid past the frame.
TEST(PetButtonsT419, ThePanelIsSizedFromTheGridsOwnConstants) {
    const UILayout l = layout800();
    const auto& p = l.petPanel;

    EXPECT_EQ(p.getHeight(),
              UILayout::BAR_HEIGHT * 2 + UILayout::MARGIN +
                  UILayout::PET_BUTTON_ROWS * UILayout::PET_BUTTON_H);
    EXPECT_EQ(PetLayout::buttonAreaTop(l),
              p.UpperLeftCorner.Y + UILayout::BAR_HEIGHT * 2 + UILayout::MARGIN);
    EXPECT_GE(PetLayout::buttonRect(l, 0).UpperLeftCorner.Y,
              p.UpperLeftCorner.Y + UILayout::BAR_HEIGHT * 2)
        << "the grid must clear the name row and the HP bar";
}

// The taller panel must still clear its neighbours in fable's left column --
// this is the measurement the 5x2 grid choice rests on (see pet_layout.h).
TEST(PetButtonsT419, ThePanelStillClearsTheGroupPanelAndTheBuffBar) {
    const struct { int w, h; } resolutions[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    for (const auto& rc : resolutions) {
        const UILayout l = layoutAt(rc.w, rc.h);
        EXPECT_FALSE(l.petPanel.isRectCollided(l.groupPanel))
            << rc.w << "x" << rc.h << ": pet " << rectStr(l.petPanel)
            << " hits group " << rectStr(l.groupPanel);
        EXPECT_FALSE(l.petPanel.isRectCollided(l.buffBar))
            << rc.w << "x" << rc.h << ": pet " << rectStr(l.petPanel)
            << " hits buff bar " << rectStr(l.buffBar);
        EXPECT_FALSE(l.petPanel.isRectCollided(l.chatPanel))
            << rc.w << "x" << rc.h << ": pet " << rectStr(l.petPanel)
            << " hits chat " << rectStr(l.chatPanel);
    }
}
