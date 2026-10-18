#pragma once

// B888-CC3 (GitLab #204): the Explore mode's overlay geometry, QUOTED from
// /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterSelect.xml
// (stock Titanium -- Will, 2026-09-23). No .cpp computes a control rect.
//
// The EXW_* screens (the card's "ExploreModeWindow overlay from the XML
// rects"):
//
//   <Screen item="ExploreModeWindow">   RelativePosition false, (0,0), 400x20,
//       Style_Transparent true, WDT_RoundedNoTitle, no border -- a bare
//       container; it holds the two real windows below.
//   <Screen item="EXW_ExploreModeWindow2">  RelativePosition false, (0,0),
//       400x25, Style_Border true (WDT_RoundedNoTitle) -- the top-LEFT char
//       info plate. Holds:
//         EXW_CharInfoLabel  RelativePosition true, (5,2), 390x15,
//         AlignCenter -- the selected character's info line (MEASURED
//         top-left, oracle-a6 A4).
//   <Screen item="EXW_ExploreModeWindow3">  RelativePosition false, (0,420),
//       400x40, Style_Border true -- the lower-LEFT instruction plate. Holds:
//         EXW_InfoLabel1  RelativePosition true, (5,2), 390x15,
//             "Click on a character to select that character."
//         EXW_InfoLabel2  RelativePosition true, (5,18), 390x15,
//             "Press Enter to enter the game, or press Esc to leave explore
//             mode."
//
// The two windows are RelativePosition false at ABSOLUTE screen positions, so
// they are drawn unscaled (the same rule as the quantity / confirmation /
// container windows); the container's own (0,0) 400x20 is transparent and
// draws nothing. The char info plate sits at the top-left of the screen
// (the ledger's "EXW_CharInfoLabel (top-left)"), the instruction plate
// lower-left at y 420.
//
// This header is device-free (only irrlicht rect types), so the layout test
// can assert the rects against the XML with no render thread.

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

struct ExploreLayout {
    using Rect = irr::core::rect<irr::s32>;

    // EXW_ExploreModeWindow2 (the char-info plate).
    static constexpr int kInfoWinW = 400, kInfoWinH = 25;
    // EXW_ExploreModeWindow3 (the instruction plate).
    static constexpr int kInstructionsY = 420;
    static constexpr int kInstrWinW = 400, kInstrWinH = 40;
    // The plates' client inset (their children's RelativePosition true origin).
    static constexpr int kInsetX = 5;
    static constexpr int kLabelW = 390, kLabelH = 15;

    static Rect make(int x, int y, int w, int h) { return Rect(x, y, x + w, y + h); }

    /** EXW_ExploreModeWindow2: the top-left char-info plate, absolute (0,0) 400x25. */
    static Rect charInfoWindowRect(int, int) { return make(0, 0, kInfoWinW, kInfoWinH); }
    /** EXW_CharInfoLabel: (5,2) 390x15 inside Window2 -> screen (5,2). */
    static Rect charInfoLabelRect(int, int) { return make(kInsetX, 2, kLabelW, kLabelH); }

    /** EXW_ExploreModeWindow3: the lower-left instruction plate, absolute (0,420) 400x40. */
    static Rect instructionsWindowRect(int, int) { return make(0, kInstructionsY, kInstrWinW, kInstrWinH); }
    /** EXW_InfoLabel1: Window3 + (5,2). */
    static Rect infoLabel1Rect(int, int) { return make(kInsetX, kInstructionsY + 2, kLabelW, kLabelH); }
    /** EXW_InfoLabel2: Window3 + (5,18). */
    static Rect infoLabel2Rect(int, int) { return make(kInsetX, kInstructionsY + 18, kLabelW, kLabelH); }
};

}  // namespace Graphics
}  // namespace EQT
