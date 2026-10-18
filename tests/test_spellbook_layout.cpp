// B1076 (GitLab #396) -- the spellbook window's geometry.
//
// Every rect in spellbook_layout.h is quoted from the official client's
// uifiles/default/EQUI_SpellBookWnd.xml (stock Titanium). The literals
// below are the XML's own numbers, not the header's constants, so a
// constant that drifts is a red test rather than a tautology (the same
// discipline as test_ninth_spell_gem).
//
// The window is 510x360 (Style_Sizable false). Two 2x4 grids of 44x44
// spell buttons -- SBW_Spell0..7 the left page (columns X=80, 171; rows
// Y=24, 97, 169, 242), SBW_Spell8..15 the right page (columns X=292,
// 383, same rows). Row-major per page (the XML: SBW_Spell0 (80,24) and
// SBW_Spell1 (171,24) are the first row, left then right). Each
// button's 84x16 name text sits at (x-20, y+44). Page numbers at (137,9)
// and (350,9); page arrows 18x32 at (6,145) and (487,145); the memorize
// gauge 100x8 at (204,312); the
// done "X" 13x13 at (449,5).

#include <gtest/gtest.h>

#include "client/graphics/ui/spellbook_layout.h"

#include <irrlicht.h>

#include <string>
#include <vector>

using namespace EQT::Graphics::SpellBookLayout;

namespace {

irr::core::rect<irr::s32> Rect(int x, int y, int cx, int cy) {
    return irr::core::rect<irr::s32>(x, y, x + cx, y + cy);
}

bool SameRect(const irr::core::rect<irr::s32>& a,
              const irr::core::rect<irr::s32>& b) {
    return a.UpperLeftCorner == b.UpperLeftCorner &&
           a.LowerRightCorner == b.LowerRightCorner;
}

}  // namespace

// Window: 510x360, unscaled (Style_Sizable false).
TEST(SpellBookLayout, WindowSizeIsTheOracle510x360) {
    EXPECT_EQ(kWindowW, 510);
    EXPECT_EQ(kWindowH, 360);
    EXPECT_EQ(kHalfPageW, 256);
    EXPECT_EQ(kBackdropStripH, 107);
}

// The sixteen spell buttons, verbatim from the XML. Row-major per page
// (SBW_Spell0 (80,24), SBW_Spell1 (171,24) ...); the row Ys are the
// XML's own irregular 24/97/169/242.
TEST(SpellBookLayout, AllSixteenSlotRectsMatchTheXml) {
    const irr::core::rect<irr::s32> expect[16] = {
        // Left page (SBW_Spell0..7), row-major.
        Rect(80, 24, 44, 44),    // SBW_Spell0
        Rect(171, 24, 44, 44),   // SBW_Spell1
        Rect(80, 97, 44, 44),    // SBW_Spell2
        Rect(171, 97, 44, 44),   // SBW_Spell3
        Rect(80, 169, 44, 44),   // SBW_Spell4
        Rect(171, 169, 44, 44),  // SBW_Spell5
        Rect(80, 242, 44, 44),   // SBW_Spell6
        Rect(171, 242, 44, 44),  // SBW_Spell7
        // Right page (SBW_Spell8..15).
        Rect(292, 24, 44, 44),   // SBW_Spell8
        Rect(383, 24, 44, 44),   // SBW_Spell9
        Rect(292, 97, 44, 44),   // SBW_Spell10
        Rect(383, 97, 44, 44),   // SBW_Spell11
        Rect(292, 169, 44, 44),  // SBW_Spell12
        Rect(383, 169, 44, 44),  // SBW_Spell13
        Rect(292, 242, 44, 44),  // SBW_Spell14
        Rect(383, 242, 44, 44),  // SBW_Spell15
    };
    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(SameRect(slotRect(i), expect[i]))
            << "slot " << i;
    }
    EXPECT_TRUE(slotIsLeftPage(7));
    EXPECT_FALSE(slotIsLeftPage(8));
    EXPECT_FALSE(slotIsLeftPage(15));
}

// The 40x40 icon decal sits at (2,2) inside each button (DecalOffset).
TEST(SpellBookLayout, IconRectIs40x40AtOffset2) {
    for (int i = 0; i < 16; ++i) {
        const irr::core::rect<irr::s32> s = slotRect(i);
        const irr::core::rect<irr::s32> c = slotIconRect(i);
        EXPECT_EQ(c.UpperLeftCorner.X, s.UpperLeftCorner.X + 2) << i;
        EXPECT_EQ(c.UpperLeftCorner.Y, s.UpperLeftCorner.Y + 2) << i;
        EXPECT_EQ(c.LowerRightCorner.X - c.UpperLeftCorner.X, 40) << i;
        EXPECT_EQ(c.LowerRightCorner.Y - c.UpperLeftCorner.Y, 40) << i;
    }
}

// Each name text is 84x16, centred under its button: (x-20, y+44). The
// XML pairs, e.g. SBW_Spell0 (80,24) -> SBW_SpellName0 (60,68).
TEST(SpellBookLayout, NameRectsMatchTheXmlPairs) {
    const irr::core::rect<irr::s32> expect[16] = {
        // Row-major: name text (x-20, y+44) below each button.
        Rect(60, 68, 84, 16),   Rect(151, 68, 84, 16),
        Rect(60, 141, 84, 16),  Rect(151, 141, 84, 16),
        Rect(60, 213, 84, 16),  Rect(151, 213, 84, 16),
        Rect(60, 286, 84, 16),  Rect(151, 286, 84, 16),
        Rect(272, 68, 84, 16),  Rect(363, 68, 84, 16),
        Rect(272, 141, 84, 16), Rect(363, 141, 84, 16),
        Rect(272, 213, 84, 16), Rect(363, 213, 84, 16),
        Rect(272, 286, 84, 16), Rect(363, 286, 84, 16),
    };
    for (int i = 0; i < 16; ++i) {
        EXPECT_TRUE(SameRect(slotNameRect(i), expect[i])) << "slot " << i;
    }
}

// Controls: page numbers, arrows, gauge, done. The literals are the
// XML's own numbers, not the header's constants: SBW_PageDown_Button is
// X=6 in the stock EQUI_SpellBookWnd.xml (18x32 at Y=145), so a header
// constant that drifts from the XML is a red test, not a tautology.
TEST(SpellBookLayout, ControlRectsMatchTheXml) {
    EXPECT_TRUE(SameRect(leftPageNumRect(), Rect(137, 9, 20, 16)));
    EXPECT_TRUE(SameRect(rightPageNumRect(), Rect(350, 9, 20, 16)));
    EXPECT_TRUE(SameRect(pageDownRect(), Rect(6, 145, 18, 32)));
    EXPECT_TRUE(SameRect(pageUpRect(), Rect(487, 145, 18, 32)));
    EXPECT_TRUE(SameRect(memorizeGaugeRect(), Rect(204, 312, 100, 8)));
    EXPECT_TRUE(SameRect(doneRect(), Rect(449, 5, 13, 13)));
}

// Every control fits inside the 510x360 window.
TEST(SpellBookLayout, EverythingFitsInTheWindow) {
    for (int i = 0; i < 16; ++i) {
        for (const auto& r : {slotRect(i), slotIconRect(i), slotNameRect(i)}) {
            EXPECT_GE(r.UpperLeftCorner.X, 0) << i;
            EXPECT_GE(r.UpperLeftCorner.Y, 0) << i;
            EXPECT_LE(r.LowerRightCorner.X, kWindowW) << i;
            EXPECT_LE(r.LowerRightCorner.Y, kWindowH) << i;
        }
    }
    for (const auto& r :
         {leftPageNumRect(), rightPageNumRect(), pageDownRect(),
          pageUpRect(), memorizeGaugeRect(), doneRect()}) {
        EXPECT_GE(r.UpperLeftCorner.X, 0);
        EXPECT_GE(r.UpperLeftCorner.Y, 0);
        EXPECT_LE(r.LowerRightCorner.X, kWindowW);
        EXPECT_LE(r.LowerRightCorner.Y, kWindowH);
    }
}

// Backdrop: the XML's four tiles -- two 256x256 page halves and two
// 256x107 control strips.
TEST(SpellBookLayout, BackdropTilesMatchTheXml) {
    const irr::core::rect<irr::s32> a = backdropTopPageRect();
    const irr::core::rect<irr::s32> b = backdropRightTopRect();
    const irr::core::rect<irr::s32> c = backdropBottomStripRect();
    const irr::core::rect<irr::s32> d = backdropRightBottomRect();
    EXPECT_EQ(a, irr::core::rect<irr::s32>(0, 0, 256, 256));
    EXPECT_EQ(b, irr::core::rect<irr::s32>(256, 0, 512, 256));
    EXPECT_EQ(c, irr::core::rect<irr::s32>(0, 256, 256, 363));
    EXPECT_EQ(d, irr::core::rect<irr::s32>(256, 256, 512, 363));
}
