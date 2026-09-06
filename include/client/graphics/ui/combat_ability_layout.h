/*
 * CombatAbilityLayout — the combat abilities window's geometry (T415,
 * board B320).
 *
 * THE PLACEMENT IS THE ORACLE'S, quoted verbatim from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_CombatAbilityWnd.xml:
 *
 *   <Screen item="CombatAbilityWnd">
 *     <Location><X>50</X><Y>50</Y></Location>
 *     <Size><CX>174</CX><CY>150</CY></Size>
 *     <Text>Combat Abilities</Text>
 *     <Style_Sizable>false</Style_Sizable>
 *
 *   <Button item="CAW_CombatEffectButton">  (5,4)    20x20
 *   <Label  item="CAW_CombatEffectLabel">   (28,5)  129x20  <Text>No Effect</Text>
 *   <Gauge  item="CAW_CombatEffectTimeRemainingGauge">
 *                                           (11,30) 116x10  <EQType>26</EQType>
 *   <Button item="CAW_OpenCombatSkillSelectWnd">
 *                                           (124,28) 37x14  <Text>S</Text>
 *   <Button item="CAW_Button1">   (2,45)  40x40      CAW_Button5   (2,85)  40x40
 *   <Button item="CAW_Button2">  (42,45)  40x40      CAW_Button6  (42,85)  40x40
 *   <Button item="CAW_Button3">  (82,45)  40x40      CAW_Button7  (82,85)  40x40
 *   <Button item="CAW_Button4"> (122,45)  40x40      CAW_Button8 (122,85)  40x40
 *
 * UNLIKE the tradeskill window there is NO SCALING here. That window is
 * `Style_Sizable true` and fable maps it onto the fixed 400x350
 * centerPopup; this one is `Style_Sizable false` — a small fixed HUD window
 * — so it is drawn at the oracle's own 174x150 and every interior rect is
 * the oracle's own offset, unmodified. Nothing is proportional, so nothing
 * can round.
 *
 * TITLE BAND. The piece offsets are relative to the window's CLIENT area,
 * under its titlebar (`Style_Titlebar true`). The arithmetic confirms it:
 * the lowest piece, CAW_Button5..8, ends at 85+40 = 125, and the window is
 * 150 tall — the 25px difference is the titlebar. fable's static panels draw
 * their own title inside the panel rect (renderSkillsPopup, renderLootPopup),
 * so TITLE_H below is that band and the piece origin is the window origin
 * plus (0, TITLE_H). 20 + 125 = 145 <= 150, so the oracle's grid fits under
 * fable's title band with the same 5px bottom margin the official window has.
 *
 * THE DEVIATION, FLAGGED: the oracle's own `<Location>` is (50,50), an
 * absolute screen position in the top-left. fable's top-left is the player
 * status panel (UILayout::playerStatus), so the window is ANCHORED instead —
 * top-right, under `layout.targetInfo`, which is where the official client's
 * own default layout keeps the Actions window it belongs to
 * (review/T377/oracle/T377_oracle_options_keys_UI_page1.png, right edge).
 * The SIZE and every INTERIOR rect are the oracle's; only the window's
 * screen anchor is fable's.
 */

#pragma once

#include <irrlicht.h>
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace CombatAbilityLayout {

constexpr irr::s32 ORACLE_WINDOW_W = 174;
constexpr irr::s32 ORACLE_WINDOW_H = 150;

// fable's title band, inside the window rect. See the header comment.
constexpr irr::s32 TITLE_H = 20;

// CAW_CombatEffectButton / CAW_CombatEffectLabel.
constexpr irr::s32 ORACLE_EFFECT_ICON_X = 5;
constexpr irr::s32 ORACLE_EFFECT_ICON_Y = 4;
constexpr irr::s32 ORACLE_EFFECT_ICON_W = 20;
constexpr irr::s32 ORACLE_EFFECT_ICON_H = 20;
constexpr irr::s32 ORACLE_EFFECT_LABEL_X = 28;
constexpr irr::s32 ORACLE_EFFECT_LABEL_Y = 5;
constexpr irr::s32 ORACLE_EFFECT_LABEL_W = 129;
constexpr irr::s32 ORACLE_EFFECT_LABEL_H = 20;

// CAW_CombatEffectTimeRemainingGauge — EQType 26, the field B320 named.
constexpr irr::s32 ORACLE_GAUGE_X = 11;
constexpr irr::s32 ORACLE_GAUGE_Y = 30;
constexpr irr::s32 ORACLE_GAUGE_W = 116;
constexpr irr::s32 ORACLE_GAUGE_H = 10;

// CAW_OpenCombatSkillSelectWnd.
constexpr irr::s32 ORACLE_SELECT_X = 124;
constexpr irr::s32 ORACLE_SELECT_Y = 28;
constexpr irr::s32 ORACLE_SELECT_W = 37;
constexpr irr::s32 ORACLE_SELECT_H = 14;

// CAW_Button1..8.
constexpr int      BUTTON_COUNT  = 8;
constexpr int      BUTTON_COLS   = 4;
constexpr irr::s32 ORACLE_BTN_X0 = 2;
constexpr irr::s32 ORACLE_BTN_Y0 = 45;
constexpr irr::s32 ORACLE_BTN    = 40;

/**
 * The window rect. Anchored top-right at the oracle's own size — UNDER
 * `layout.targetInfo` and LEFT OF the spell-gem column — see the flagged
 * deviation in the header comment. Clamped so the window is always wholly
 * on screen.
 *
 * The gem column, not the screen edge, is the right anchor: at 640x480 the
 * gems reach up to y=148 and a screen-edge anchor put the window's bottom
 * 44px inside them (caught by
 * CombatAbilityLayoutT415.TheWindowStaysOnScreenAndClearOfItsNeighbours,
 * which checks all five resolutions).
 */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    irr::s32 x = layout.spellGems.UpperLeftCorner.X - UILayout::MARGIN
                 - ORACLE_WINDOW_W;
    irr::s32 y = layout.targetInfo.LowerRightCorner.Y + UILayout::MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (y + ORACLE_WINDOW_H > layout.screenH) {
        y = layout.screenH - ORACLE_WINDOW_H;
        if (y < 0) y = 0;
    }
    return irr::core::rect<irr::s32>(x, y, x + ORACLE_WINDOW_W,
                                     y + ORACLE_WINDOW_H);
}

/**
 * An oracle client-area offset placed in the window. The one placement
 * body; every rect below goes through it.
 */
inline irr::core::rect<irr::s32> piece(const UILayout& layout,
                                       irr::s32 x, irr::s32 y,
                                       irr::s32 w, irr::s32 h) {
    const auto win = windowRect(layout);
    const irr::s32 px = win.UpperLeftCorner.X + x;
    const irr::s32 py = win.UpperLeftCorner.Y + TITLE_H + y;
    return irr::core::rect<irr::s32>(px, py, px + w, py + h);
}

/** CAW_CombatEffectButton. */
inline irr::core::rect<irr::s32> effectIconRect(const UILayout& layout) {
    return piece(layout, ORACLE_EFFECT_ICON_X, ORACLE_EFFECT_ICON_Y,
                 ORACLE_EFFECT_ICON_W, ORACLE_EFFECT_ICON_H);
}

/** CAW_CombatEffectLabel. */
inline irr::core::rect<irr::s32> effectLabelRect(const UILayout& layout) {
    return piece(layout, ORACLE_EFFECT_LABEL_X, ORACLE_EFFECT_LABEL_Y,
                 ORACLE_EFFECT_LABEL_W, ORACLE_EFFECT_LABEL_H);
}

/** CAW_CombatEffectTimeRemainingGauge (EQType 26). */
inline irr::core::rect<irr::s32> gaugeRect(const UILayout& layout) {
    return piece(layout, ORACLE_GAUGE_X, ORACLE_GAUGE_Y,
                 ORACLE_GAUGE_W, ORACLE_GAUGE_H);
}

/** CAW_OpenCombatSkillSelectWnd — the "S" button. */
inline irr::core::rect<irr::s32> skillSelectRect(const UILayout& layout) {
    return piece(layout, ORACLE_SELECT_X, ORACLE_SELECT_Y,
                 ORACLE_SELECT_W, ORACLE_SELECT_H);
}

/** CAW_Button<i+1>, i in 0..7. Degenerate rect out of range. */
inline irr::core::rect<irr::s32> buttonRect(const UILayout& layout, int i) {
    if (i < 0 || i >= BUTTON_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return piece(layout,
                 ORACLE_BTN_X0 + (i % BUTTON_COLS) * ORACLE_BTN,
                 ORACLE_BTN_Y0 + (i / BUTTON_COLS) * ORACLE_BTN,
                 ORACLE_BTN, ORACLE_BTN);
}

} // namespace CombatAbilityLayout
} // namespace Graphics
} // namespace EQT
