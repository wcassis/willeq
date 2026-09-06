/*
 * PetLayout — the pet window's command-button geometry, and the only place it
 * is computed (T419, board B352).
 *
 * WHAT WAS BROKEN. `renderPetPanel()` drew the ten pet command indicators as
 * a single run of text starting at `petPanel.top + 34` in a panel that is
 * `BAR_HEIGHT * 2 + MARGIN * 3` = 40px tall, so the labels hung ~8px BELOW
 * the frame (visible in all three columns of `review/T416/crops/pet_3way.png`,
 * which is how it was found), and the run's `bx > right - 10` guard dropped
 * the tenth label ("SH", SpellHold) without saying so. Two defects, one row.
 *
 * Source: /mnt/projects/EverQuestP1999/uifiles/default/EQUI_PetInfoWindow.xml
 *
 *   <Screen item="PetInfoWindow">  <Size><CX>154</CX><CY>142</CY></Size>
 *                                 <Style_Sizable>true</Style_Sizable>
 *
 * Every child of that window is `Location`/`Size`, not anchors:
 *
 *   <Gauge  item="PIW_PetHPGauge">     Location (2,0)   Size 108x26
 *                                      <Text>No Pet</Text>
 *   <Button item="PIW_AttackButton">   Location (2,28)  Size 108x20  "Attack"
 *   <Button item="PIW_FollowButton">   Location (2,50)  Size  54x20  "Follow"
 *   <Button item="PIW_TauntButton">    Location (56,50) Size  54x20  "Taunt"
 *   <Button item="PIW_GuardButton">    Location (2,74)  Size  54x20  "Guard"
 *   <Button item="PIW_SitButton">      Location (56,74) Size  54x20  "Sit"
 *   <Button item="PIW_StandButton">    Location (56,74) Size  54x20  "Stand"
 *   <Button item="PIW_BackButton">     Location (2,96)  Size  54x20  "Back"
 *   <Button item="PIW_LostButton">     Location (56,96) Size  54x20  "Go Away"
 *
 * So the oracle's shape is: a full-width gauge, a full-width Attack button,
 * then a TWO-COLUMN grid of 54x20 cells at x = 2 and x = 56, rows at y = 50,
 * 74 and 96 (Sit and Stand share one cell — they are the two faces of one
 * toggle). Seven distinct command cells.
 *
 * TWO DELIBERATE DEVIATIONS, both flagged rather than absorbed:
 *
 * 1. THE BUTTON SET IS NOT THE ORACLE'S, because fable's is not Titanium's.
 *    `PetButton` (include/client/pet_constants.h) is eqemu's ten-entry set —
 *    Sit, Stop, Regroup, Follow, Guard, Taunt, Hold, GHold, Focus, SpellHold
 *    — of which only Sit, Follow, Guard, Taunt and (as "Back") Stop have a
 *    cell in this XML at all; Regroup, Hold, GHold, Focus and SpellHold
 *    postdate the Titanium UI file. Ten cells cannot be laid on a grid drawn
 *    for seven, so the ORDER and the CELL SHAPE are the oracle's and the
 *    COLUMN COUNT is not — see 2.
 *
 * 2. THE GRID IS 5x2, NOT THE ORACLE'S 2x5. Measured, not assumed: fable
 *    stacks its HUD down the left column, and at the smallest supported
 *    resolution (640x480) `petPanel` starts at y=172, under a 94px group
 *    panel under a 70px player-status block, with the buff bar's top edge at
 *    y=228 — SIXTEEN pixels of slack. The oracle's two-column grid needs five
 *    rows for ten cells; at the oracle's own 20px row that is 100px, and even
 *    scaled proportionally into 16px it would put a 3px row under a 3px row.
 *    Five columns of two rows fits the ten cells in 20px with 4px to spare
 *    and keeps every rect inside the frame, which is what B352 asked for.
 *    Restoring the oracle's 2x5 needs a pet window that is not stacked in
 *    that column — a resolution-aware HUD or a floating window — which is
 *    board B366, not this unit.
 *
 * What IS the oracle's here: the row-major order, the cell aspect being wider
 * than tall, the 2px inset from the frame, and the rule that the whole grid
 * lives INSIDE the window.
 */

#pragma once

#include <irrlicht.h>
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace PetLayout {

// The oracle window the quotes above are expressed in.
constexpr irr::s32 ORACLE_WINDOW_W = 154;
constexpr irr::s32 ORACLE_WINDOW_H = 142;

// PIW_PetHPGauge / PIW_AttackButton / the 54x20 command cell.
constexpr irr::s32 ORACLE_GAUGE_W  = 108;
constexpr irr::s32 ORACLE_GAUGE_H  = 26;
constexpr irr::s32 ORACLE_CELL_W   = 54;
constexpr irr::s32 ORACLE_CELL_H   = 20;

// fable's grid — see deviation 2 in the header comment.
constexpr int      BUTTON_COLS  = 5;
constexpr int      BUTTON_ROWS  = 2;
constexpr int      BUTTON_COUNT = BUTTON_COLS * BUTTON_ROWS;  // == PET_BUTTON_COUNT
constexpr irr::s32 FRAME_INSET  = 2;   // the oracle's own Location.X for column 0

/**
 * The top of the button grid inside `layout.petPanel`: under the name row and
 * the HP bar, which keep the geometry they had. `UILayout::computeLayout()`
 * sizes the panel from the same two constants, so the grid cannot fall out of
 * the frame the way the old text run did.
 */
inline irr::s32 buttonAreaTop(const UILayout& layout) {
    return layout.petPanel.UpperLeftCorner.Y +
           UILayout::BAR_HEIGHT * 2 + UILayout::MARGIN;
}

/**
 * One pet command indicator, 0..BUTTON_COUNT-1, in `PetButton` order (Sit,
 * Stop, Regroup, Follow, Guard, Taunt, Hold, GHold, Focus, SpellHold) laid
 * out row-major the way the oracle lays PIW_FollowButton..PIW_LostButton.
 * Degenerate rect for an out-of-range index, the TradeskillLayout contract.
 */
inline irr::core::rect<irr::s32> buttonRect(const UILayout& layout, int i) {
    if (i < 0 || i >= BUTTON_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    const auto& p = layout.petPanel;
    const irr::s32 usableW = p.getWidth() - FRAME_INSET * 2;
    const irr::s32 cellW = usableW / BUTTON_COLS;
    const irr::s32 cellH = UILayout::PET_BUTTON_H;
    const irr::s32 col = i % BUTTON_COLS;
    const irr::s32 row = i / BUTTON_COLS;
    const irr::s32 x0 = p.UpperLeftCorner.X + FRAME_INSET + col * cellW;
    const irr::s32 y0 = buttonAreaTop(layout) + row * cellH;
    // One pixel of gutter on the right and bottom, so adjacent cells never
    // share an edge pixel and the hit tests cannot both claim it.
    return irr::core::rect<irr::s32>(x0, y0, x0 + cellW - 1, y0 + cellH - 1);
}

} // namespace PetLayout
} // namespace Graphics
} // namespace EQT
