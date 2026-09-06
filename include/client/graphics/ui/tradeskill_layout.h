/*
 * TradeskillLayout — the tradeskill container window's geometry, and the
 * only place it is computed (T415, board B321).
 *
 * THE PLACEMENT IS THE ORACLE'S, NOT OURS — the same rule and the same
 * arithmetic InventoryLayout::coinRect() (T393) and
 * InventoryLayout::paperdollRect() (T409) follow: quote the official
 * client's own element, map it proportionally from the oracle window's size
 * onto fable's fixed 400x350 `UILayout::centerPopup`, integer division, no
 * rounding policy to drift.
 *
 * Source: /mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeskillWnd.xml
 *
 *   <Screen item="TradeskillWnd">   <Size><CX>400</CX><CY>525</CY></Size>
 *
 * The container half of that window is anchor-positioned rather than
 * absolute, so the absolute rects below are DERIVED; the derivation is
 * written out here so a reader can check it against the XML without redoing
 * it. `AnchorToLeft/ToTop == false` means the offset is measured from the
 * parent's RIGHT / BOTTOM edge.
 *
 *   <Screen item="COMBW_RightSideArea">   parent = TradeskillWnd (400x525)
 *       LeftAnchorOffset 160, LeftAnchorToLeft  false -> x0 = 400-160 = 240
 *       TopAnchorOffset    0, TopAnchorToTop    true  -> y0 = 0
 *       RightAnchorOffset  0, RightAnchorToLeft false -> x1 = 400
 *       BottomAnchorOffset 0, BottomAnchorToTop false -> y1 = 525
 *     => (240,0)-(400,525), i.e. 160 x 525
 *
 *   <Screen item="COMBW_ContainerArea">   parent = RightSideArea (160x525)
 *       0 / 0 / 0(from right) / 330(from top)  => (0,0)-(160,330)
 *     => absolute (240,0)-(400,330)
 *
 *   <Screen item="COMBW_ComponentArea">   parent = ContainerArea (160x330)
 *       LeftAnchorOffset  39, ToLeft true  -> x0 = 39
 *       TopAnchorOffset  120, ToTop true   -> y0 = 120
 *       RightAnchorOffset123, ToLeft true  -> x1 = 123
 *       BottomAnchorOffset330, ToTop true  -> y1 = 330
 *     => (39,120)-(123,330), i.e. 84 x 210  == 2 columns x 5 rows of 42x42
 *     => absolute origin (279,120)
 *
 *   <Button item="COMBW_RequiredIcon0..9">  the ten container slots
 *       Location ((i%2)*42, (i/2)*42)   Size 42x42
 *       DecalOffset (2,2)               DecalSize 40x40
 *     => absolute (279 + (i%2)*42, 120 + (i/2)*42) 42x42
 *
 *   <Label item="COMBW_ContainerName">    parent = ContainerArea (160x330)
 *       2 / 70(from top) / 2(from right) / 90(from top)
 *     => (2,70)-(158,90)  => absolute (242,70)-(398,90), 156 x 20
 *
 *   <Screen item="COMBW_CombineArea">     parent = RightSideArea (160x525)
 *       0 / 334 / 0(from right) / 388     => absolute (240,334)-(400,388)
 *   <Button item="COMBW_CombineButton">   parent = CombineArea (160x54)
 *       20 / 0 / 20(from right) / 30      => (20,0)-(140,30)
 *     => absolute (260,334)-(380,364), 120 x 30
 *
 *   <Screen item="COMBW_FlagsArea">       parent = RightSideArea (160x525)
 *       0 / 392 / 0(from right) / 0(from bottom)
 *                                         => absolute (240,392)-(400,525)
 *   <Button item="COMBW_CloseButton">     parent = FlagsArea (160x133)
 *       20 / 24(from bottom) / 20(from right) / 2(from bottom)
 *                                         => (20,109)-(140,131)
 *     => absolute (260,501)-(380,523), 120 x 22
 *
 * What fable does NOT draw from this window is its whole LEFT side — the
 * recipe search box, the known-recipe list and the favourites buttons
 * (COMBW_LeftSideArea) — because fable has no recipe database to fill them
 * from. That is boarded, not forgotten; the window's frame is still the
 * oracle's 400x525 so the container half lands where the official client
 * puts it.
 */

#pragma once

#include <irrlicht.h>
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace TradeskillLayout {

// The oracle window the rects below are expressed in.
constexpr irr::s32 ORACLE_WINDOW_W = 400;
constexpr irr::s32 ORACLE_WINDOW_H = 525;

// COMBW_ComponentArea's absolute origin, and the RequiredIcon grid.
constexpr irr::s32 ORACLE_GRID_X = 279;
constexpr irr::s32 ORACLE_GRID_Y = 120;
constexpr irr::s32 ORACLE_SLOT   = 42;
constexpr int      GRID_COLS     = 2;
constexpr int      SLOT_COUNT    = 10;  // COMBW_RequiredIcon0..9
constexpr irr::s32 ORACLE_DECAL_OFFSET = 2;
constexpr irr::s32 ORACLE_DECAL_SIZE   = 40;

// COMBW_ContainerName.
constexpr irr::s32 ORACLE_TITLE_X = 242;
constexpr irr::s32 ORACLE_TITLE_Y = 70;
constexpr irr::s32 ORACLE_TITLE_W = 156;
constexpr irr::s32 ORACLE_TITLE_H = 20;

// COMBW_CombineButton.
constexpr irr::s32 ORACLE_COMBINE_X = 260;
constexpr irr::s32 ORACLE_COMBINE_Y = 334;
constexpr irr::s32 ORACLE_COMBINE_W = 120;
constexpr irr::s32 ORACLE_COMBINE_H = 30;

// COMBW_CloseButton.
constexpr irr::s32 ORACLE_CLOSE_X = 260;
constexpr irr::s32 ORACLE_CLOSE_Y = 501;
constexpr irr::s32 ORACLE_CLOSE_W = 120;
constexpr irr::s32 ORACLE_CLOSE_H = 22;

/**
 * Map an oracle-window rect into `layout.centerPopup`, proportionally.
 * The one scaling body — every rect below goes through it, so there is no
 * second copy of the mapping to drift (T57's rule).
 */
inline irr::core::rect<irr::s32> scaled(const UILayout& layout,
                                        irr::s32 x, irr::s32 y,
                                        irr::s32 w, irr::s32 h) {
    const auto& popup = layout.centerPopup;
    const irr::s32 pw = popup.getWidth();
    const irr::s32 ph = popup.getHeight();
    const irr::s32 sx = popup.UpperLeftCorner.X + x * pw / ORACLE_WINDOW_W;
    const irr::s32 sy = popup.UpperLeftCorner.Y + y * ph / ORACLE_WINDOW_H;
    const irr::s32 sw = w * pw / ORACLE_WINDOW_W;
    const irr::s32 sh = h * ph / ORACLE_WINDOW_H;
    return irr::core::rect<irr::s32>(sx, sy, sx + sw, sy + sh);
}

/** COMBW_RequiredIcon<i>, 0..9. Degenerate rect for an out-of-range index. */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOT_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return scaled(layout,
                  ORACLE_GRID_X + (i % GRID_COLS) * ORACLE_SLOT,
                  ORACLE_GRID_Y + (i / GRID_COLS) * ORACLE_SLOT,
                  ORACLE_SLOT, ORACLE_SLOT);
}

/** COMBW_ContainerName — the window's title, i.e. `containerName`. */
inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    return scaled(layout, ORACLE_TITLE_X, ORACLE_TITLE_Y,
                  ORACLE_TITLE_W, ORACLE_TITLE_H);
}

/** COMBW_CombineButton. */
inline irr::core::rect<irr::s32> combineRect(const UILayout& layout) {
    return scaled(layout, ORACLE_COMBINE_X, ORACLE_COMBINE_Y,
                  ORACLE_COMBINE_W, ORACLE_COMBINE_H);
}

/** COMBW_CloseButton. */
inline irr::core::rect<irr::s32> closeRect(const UILayout& layout) {
    return scaled(layout, ORACLE_CLOSE_X, ORACLE_CLOSE_Y,
                  ORACLE_CLOSE_W, ORACLE_CLOSE_H);
}

} // namespace TradeskillLayout
} // namespace Graphics
} // namespace EQT
