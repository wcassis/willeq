#pragma once

// B998 (GitLab #318): the Character Select window's geometry, QUOTED from
// /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterSelect.xml
// (stock Titanium -- Will, 2026-09-23). No .cpp computes a control rect.
//
//   <Screen item="CharacterSelectWindow">  RelativePosition false, (0,20),
//       252x484, WDT_Filigree3, Style_Titlebar true, Style_Sizable false
//   Children (RelativePosition true), offset by the client inset:
//       CSW_Explore_Button (2,60) 48x32      CSW_Delete_Button (80,60) 58x32
//       CSW_Rotate_Button (168,60) 48x32     CSW_Char1..8_Button (5, 105+35n) 210x32
//       CSW_Tutorial_Button (5,393) 100x32   CSW_Go_Home_Button (117,393) 100x32
//       CSW_Quit_Button (5,429) 100x32       CSW_Enter_World_Button (117,429) 100x32
//       CS_TitleLabel (17,20) 182x15 "Character Select" over the five
//       CS_*TitlePiece animations at (17..199, 0) 56 tall
//
// PLACEMENT, MEASURED on the oracle at 1024x768 by pixel edges
// (review/UIGAP-ORACLE/318/charselect.00_initial_Testcaster_selected.png; the
// same in 204/00_character_select.png): the window sits on the LEFT edge and
// is VERTICALLY CENTRED -- the XML's own (0,20) is not where the oracle draws
// it (the same placement B888 measured for the creation columns). The
// children's CLIENT ORIGIN is (14, 150): the Char buttons' and Quit's left
// edge is x 19 (= 14 + 5), Enter World's x 131 (= 14 + 117); Char2..Char8's top
// edges are y 290, 325 ... 500 (= 150 + 140 + 35n) and Quit's 579
// (= 150 + 429). 150 = (768 - 484) / 2 + 8, which is the rule taken for every
// height. The drawn frame is x 6..241, y 145..625 (235x480).
//
// NOT DRAWN (each a decision, not an omission):
//   CSW_Tutorial_Button, CSW_Go_Home_Button  OUT -- no tutorial, and the
//       home-city return is the tutorial pair's (#204, Will 2026-09-23).
//   CSW_Explore_Button, CSW_Rotate_Button    drawn GREYED and not
//       hit-testable: both act on the 3D scene, which this unit does not
//       build (the T363 rule: a control with nothing behind it is greyed).
//   The EXW_* explore-mode windows           the explore mode is that scene's.

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

struct CharSelectLayout {
    using Rect = irr::core::rect<irr::s32>;

    static constexpr int kWindowW = 252, kWindowH = 484;   // CharacterSelectWindow
    static constexpr int kClientX = 14;                    // measured (header)
    static constexpr int kFrameW = 235, kFrameH = 480;     // the drawn frame, measured
    static constexpr int kCharButtons = 8;                 // CSW_Char1..8_Button
    static constexpr int kCharButtonY0 = 105, kCharButtonStep = 35;

    static Rect make(int x, int y, int w, int h) { return Rect(x, y, x + w, y + h); }
    /** The XML children's (0,0): (14, 150) at 1024x768. */
    static irr::core::vector2di clientOrigin(int, int sh) {
        return irr::core::vector2di(kClientX, (sh - kWindowH) / 2 + 8);
    }
    /** The window's drawn frame (x 6..241, y 145..625 at 1024x768). */
    static Rect windowRect(int sw, int sh) {
        const auto o = clientOrigin(sw, sh);
        return make(o.X - 8, o.Y - 5, kFrameW, kFrameH);
    }
    static Rect child(int sw, int sh, int x, int y, int w, int h) {
        const auto o = clientOrigin(sw, sh);
        return make(o.X + x, o.Y + y, w, h);
    }

    /** The CS_*TitlePiece strip, (17,0) to (199,56). */
    static Rect titlePiecesRect(int sw, int sh) { return child(sw, sh, 17, 0, 182, 56); }
    /** CS_TitleLabel (17,20) 182x15. */
    static Rect titleLabelRect(int sw, int sh) { return child(sw, sh, 17, 20, 182, 15); }

    static Rect exploreRect(int sw, int sh) { return child(sw, sh, 2, 60, 48, 32); }
    static Rect deleteRect(int sw, int sh) { return child(sw, sh, 80, 60, 58, 32); }
    static Rect rotateRect(int sw, int sh) { return child(sw, sh, 168, 60, 48, 32); }
    /** CSW_Char<n+1>_Button, n = 0..7. */
    static Rect charButtonRect(int sw, int sh, int n) {
        return child(sw, sh, 5, kCharButtonY0 + kCharButtonStep * n, 210, 32);
    }
    /**
     * The 3D model area -- where B888-CC2's preview stands the character up.
     * The select window is the left 252px; the model is the screen's CENTRE,
     * right of the window (the oracle floats "Testchar [60 Warrior]" over it
     * at centre x ~530 at 1024x768, and the 3D figure is centre-screen). A
     * square centred horizontally, its top at 1/4 height -- the standing
     * character's framing. Scaled from 768.
     */
    static Rect modelAreaRect(int sw, int sh) {
        const int side = sh * 3 / 4;
        return Rect((sw - side) / 2, (sh - side) / 3, (sw + side) / 2, (sh + side) / 3);
    }
    static Rect quitRect(int sw, int sh) { return child(sw, sh, 5, 429, 100, 32); }
    static Rect enterWorldRect(int sw, int sh) { return child(sw, sh, 117, 429, 100, 32); }

    /**
     * The selected character's two lines. The oracle floats them over the 3D
     * model's head (centre x ~530, line 1 y ~107, line 2 y ~135 at 1024x768);
     * with no model fable centres them horizontally at that height, scaled
     * from 768 -- assumption awaiting the 3D preview leg.
     */
    static Rect infoLineRect(int sw, int sh) {
        const int y = sh * 98 / 768;
        return Rect(0, y, sw, y + 18);
    }
    static Rect zoneLineRect(int sw, int sh) {
        const Rect a = infoLineRect(sw, sh);
        return Rect(a.UpperLeftCorner.X, a.LowerRightCorner.Y + 6, a.LowerRightCorner.X,
                    a.LowerRightCorner.Y + 24);
    }
};

}  // namespace Graphics
}  // namespace EQT
