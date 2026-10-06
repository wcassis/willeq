#pragma once

// B888 (GitLab #204): the two-page Character Creation screen's geometry,
// QUOTED from /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterCreate.xml
// (stock Titanium -- Will, 2026-09-23). No .cpp computes a rect: every number
// below is an XML <Location>/<Size>/<*AnchorOffset> or one of the PLACEMENT
// rules measured on the official client at 1024x768
// (/mnt/projects/eq-oracle/logs/shots/cc_01_screen.png, cc_06_stats.png,
// cc_08_deity_city.png), because the XML places every top-level piece of
// both pages at (0,0) (RelativePosition false) and the client positions them
// itself:
//
//   CC_CharacterCreationTitle  top, horizontally centred   (measured x 406..618)
//   CC_Back/Next_Button        their anchors: RIGHT-anchored L/R, TOP-anchored
//   ...Screen0a / 1b           left edge, vertically centred (0a measured y 144..624)
//   ...Screen0b / 1c           right edge, vertically centred (0b measured y 186..581)
//   ...Screen1a                top-left corner                 (measured (0,0))
//   ...Screen0c / 1d           bottom edge, horizontally centred, at the XML's
//                              own size -- which reproduces the XML's
//                              (200,300) exactly at 640x480. The oracle draws
//                              the pane ~478x254 at 1024x768: its size rule is
//                              not in the XML and is AWAITING ORACLE.
//
// Children are RelativePosition true: offset by the parent's client inset,
// kClientInset (WDT_RoundedNoTitle's border; measured 3-5 px on the oracle's
// frames, taken as 4). Anchored children (the Abilities panel, Back/Next)
// follow the SIDL rule: <LeftAnchorToLeft>false</> measures from the RIGHT
// edge, top/bottom default to the top edge.
//
// HIDDEN ENTRIES (Will 2026-09-23): the in-era buttons are LAID OUT WITHOUT
// the post-Velious ones. The race and class columns are ordered by label, so
// in-era entry i takes the i-th slot of the column (the XML's slot ys,
// sorted); Froglok's slot (CC0_Race14 at y 181) is reused by Gnome.

#include <irrlicht.h>

namespace EQT {
namespace Graphics {

struct CharCreateLayout {
    using Rect = irr::core::rect<irr::s32>;

    // <Screen item="CharacterCreation"> 640x480, the design resolution.
    static constexpr int kDesignW = 640, kDesignH = 480;
    static constexpr int kClientInset = 4;

    // --- top-level pieces (sizes verbatim) ---
    static constexpr int kTitleW = 212, kTitleH = 86;           // CC_CharacterCreationTitle
    static constexpr int kScreen0aW = 210, kScreen0aH = 480;    // CharacterCreationScreen0a
    static constexpr int kScreen0bW = 168, kScreen0bH = 395;    // CharacterCreationScreen0b
    static constexpr int kScreen0cW = 240, kScreen0cH = 180;    // CharacterCreationScreen0c
    static constexpr int kScreen1aW = 224, kScreen1aH = 108;    // CharacterCreationScreen1a
    static constexpr int kScreen1bW = 218, kScreen1bH = 430;    // CharacterCreationScreen1b
    static constexpr int kScreen1cW = 128, kScreen1cH = 389;    // CharacterCreationScreen1c
    static constexpr int kScreen1dW = 280, kScreen1dH = 180;    // CharacterCreationScreen1d

    // --- column slots ---
    static constexpr int kRaceSlots = 15;    // CC0_Race0..14
    static constexpr int kClassSlots = 16;   // CC0_Class0..15
    static constexpr int kDeitySlots = 17;   // CC1_Deity0..16
    static constexpr int kCitySlots = 15;    // CC1_City0..14
    static constexpr int kStatRows = 7;      // CC0_Stat0..6

    static Rect make(int x, int y, int w, int h) { return Rect(x, y, x + w, y + h); }
    static Rect child(const Rect& parent, int x, int y, int w, int h) {
        return make(parent.UpperLeftCorner.X + kClientInset + x,
                    parent.UpperLeftCorner.Y + kClientInset + y, w, h);
    }
    /** Right-anchored horizontally (<Left/RightAnchorToLeft>false), top-anchored vertically. */
    static Rect anchoredRight(const Rect& parent, int left, int top, int right, int bottom) {
        const int cw = parent.getWidth() - 2 * kClientInset;
        const int ox = parent.UpperLeftCorner.X + kClientInset;
        const int oy = parent.UpperLeftCorner.Y + kClientInset;
        return Rect(ox + cw - left, oy + top, ox + cw - right, oy + bottom);
    }

    // --- shared ---
    static Rect titleRect(int sw, int) { return make((sw - kTitleW) / 2, 0, kTitleW, kTitleH); }
    /** CCT_TitleLabel (-1,20) 200x15, <Text>Character Creation</Text>. */
    static Rect titleLabelRect(int sw, int sh) { return child(titleRect(sw, sh), -1, 20, 200, 15); }
    /** CC_Help_Button (73,55) 54x18. */
    static Rect helpButtonRect(int sw, int sh) { return child(titleRect(sw, sh), 73, 55, 54, 18); }
    /** CC_Back_Button anchors L157 T8 R83 B40 against the whole screen. */
    static Rect backButtonRect(int sw, int) { return Rect(sw - 157, 8, sw - 83, 40); }
    /** CC_Next_Button anchors L69 T8 R5 B40. */
    static Rect nextButtonRect(int sw, int) { return Rect(sw - 69, 8, sw - 5, 40); }

    // --- page 0 ---
    static Rect screen0aRect(int, int sh) { return make(0, (sh - kScreen0aH) / 2, kScreen0aW, kScreen0aH); }
    static Rect screen0bRect(int sw, int sh) {
        return make(sw - kScreen0bW, (sh - kScreen0bH) / 2, kScreen0bW, kScreen0bH);
    }
    static Rect screen0cRect(int sw, int sh) {
        return make((sw - kScreen0cW) / 2, sh - kScreen0cH, kScreen0cW, kScreen0cH);
    }
    /** CC0a_Title (25,10) 150x25 holding CC0a_TitleLabel "Character". */
    static Rect characterTitleRect(int sw, int sh) { return child(screen0aRect(sw, sh), 25, 10, 150, 25); }
    /** CC0_Face_Button (2,45), CC0_Male_Button (70,45), CC0_Female_Button (134,45); all 64x24. */
    static Rect faceButtonRect(int sw, int sh) { return child(screen0aRect(sw, sh), 2, 45, 64, 24); }
    static Rect maleButtonRect(int sw, int sh) { return child(screen0aRect(sw, sh), 70, 45, 64, 24); }
    static Rect femaleButtonRect(int sw, int sh) { return child(screen0aRect(sw, sh), 134, 45, 64, 24); }
    /** The XML's race-column slot ys, sorted (CC0_Race14 Froglok sits at 181). */
    static int raceSlotY(int slot) { return 85 + 24 * slot; }
    /** CC0_Race<n>: x 2, 96x24. */
    static Rect raceSlotRect(int sw, int sh, int slot) {
        return child(screen0aRect(sw, sh), 2, raceSlotY(slot), 96, 24);
    }
    /** CC0_Class<n>: x 102, y 85 + 24n, 96x24. */
    static Rect classSlotRect(int sw, int sh, int slot) {
        return child(screen0aRect(sw, sh), 102, 85 + 24 * slot, 96, 24);
    }
    /** CC0b_Title (30,10) 100x25 holding CC0b_TitleLabel "Abilities". */
    static Rect abilitiesTitleRect(int sw, int sh) { return child(screen0bRect(sw, sh), 30, 10, 100, 25); }
    /** CC0_Points_Left_Button L157 T45 R56 B83; ..._Display_Button L54 T45 R22 B77. */
    static Rect pointsLeftButtonRect(int sw, int sh) { return anchoredRight(screen0bRect(sw, sh), 157, 45, 56, 83); }
    static Rect pointsLeftDisplayRect(int sw, int sh) { return anchoredRight(screen0bRect(sw, sh), 54, 45, 22, 77); }
    /** CC0_Stat<n>_Button L157 T(96+40n) R56 B(134+40n). */
    static Rect statButtonRect(int sw, int sh, int n) {
        return anchoredRight(screen0bRect(sw, sh), 157, 96 + 40 * n, 56, 134 + 40 * n);
    }
    /** CC0_Stat<n>_Display_Button L54 T(96+40n) R22 B(129+40n). */
    static Rect statDisplayRect(int sw, int sh, int n) {
        return anchoredRight(screen0bRect(sw, sh), 54, 96 + 40 * n, 22, 129 + 40 * n);
    }
    /** CC0_Stat<n>_Up_Button L19 T(96+40n) R5 B(112+40n). */
    static Rect statUpRect(int sw, int sh, int n) {
        return anchoredRight(screen0bRect(sw, sh), 19, 96 + 40 * n, 5, 112 + 40 * n);
    }
    /** CC0_Stat<n>_Down_Button L19 T(114+40n) R5 B(130+40n). */
    static Rect statDownRect(int sw, int sh, int n) {
        return anchoredRight(screen0bRect(sw, sh), 19, 114 + 40 * n, 5, 130 + 40 * n);
    }
    /** CC0_Help / CC1_Help STMLbox: anchors L2 T4 R2 B4 inside its screen. */
    static Rect helpTextRect(const Rect& pane) {
        return Rect(pane.UpperLeftCorner.X + kClientInset + 2, pane.UpperLeftCorner.Y + kClientInset + 4,
                    pane.LowerRightCorner.X - kClientInset - 2, pane.LowerRightCorner.Y - kClientInset - 4);
    }

    // --- page 1 ---
    static Rect screen1aRect(int, int) { return make(0, 0, kScreen1aW, kScreen1aH); }
    static Rect screen1bRect(int, int sh) { return make(0, (sh - kScreen1bH) / 2, kScreen1bW, kScreen1bH); }
    static Rect screen1cRect(int sw, int sh) {
        return make(sw - kScreen1cW, (sh - kScreen1cH) / 2, kScreen1cW, kScreen1cH);
    }
    static Rect screen1dRect(int sw, int sh) {
        return make((sw - kScreen1dW) / 2, sh - kScreen1dH, kScreen1dW, kScreen1dH);
    }
    /** CC1_NameLabelBorder (54,5) 100x25, CC1_NameInput (6,32) 120x20,
     *  CC1_Get_Name_Button (131,32) 73x22. CC1_StartTutorial_Button and the
     *  three CCT_*TutorialPiece animations are OUT (Will: no tutorial). */
    static Rect nameLabelRect(int sw, int sh) { return child(screen1aRect(sw, sh), 54, 5, 100, 25); }
    static Rect nameInputRect(int sw, int sh) { return child(screen1aRect(sw, sh), 6, 32, 120, 20); }
    static Rect getNameButtonRect(int sw, int sh) { return child(screen1aRect(sw, sh), 131, 32, 73, 22); }
    /** CC1_DeityLabelBorder (53,10) 100x25; CC1_Deity<n> (5, 42 + 22n) 195x22. */
    static Rect deityTitleRect(int sw, int sh) { return child(screen1bRect(sw, sh), 53, 10, 100, 25); }
    static Rect deitySlotRect(int sw, int sh, int n) {
        return child(screen1bRect(sw, sh), 5, 42 + 22 * n, 195, 22);
    }
    /** CC1_CityLabelBorder (10,8) 100x25; CC1_City<n> (8, 42 + 22n) 105x22. */
    static Rect cityTitleRect(int sw, int sh) { return child(screen1cRect(sw, sh), 10, 8, 100, 25); }
    static Rect citySlotRect(int sw, int sh, int n) {
        return child(screen1cRect(sw, sh), 8, 42 + 22 * n, 105, 22);
    }
};

}  // namespace Graphics
}  // namespace EQT
