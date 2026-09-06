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
 * ---------------------------------------------------------------------------
 * T419 (board B335) added the LEFT side. Same derivation, same anchors:
 *
 *   <Screen item="COMBW_LeftSideArea">   parent = TradeskillWnd (400x525)
 *       LeftAnchorOffset   2, LeftAnchorToLeft   true  -> x0 = 2
 *       TopAnchorOffset    0, TopAnchorToTop     true  -> y0 = 0
 *       RightAnchorOffset160, RightAnchorToLeft  false -> x1 = 400-160 = 240
 *       BottomAnchorOffset 0, BottomAnchorToTop  false -> y1 = 525
 *     => (2,0)-(240,525), i.e. 238 x 525
 *
 *   <Screen item="COMBW_SearchArea">     parent = LeftSideArea (238x525)
 *       0 / 0 / 0(from right) / 124(from top)  => absolute (2,0)-(240,124)
 *     Its children are Location/Size, not anchors (RelativePosition):
 *       <Label   item="COMBW_MinTrivialText">  Location (4,4)    Size 100x24
 *       <Editbox item="COMBW_MinTrivialEdit">  Location (106,4)  Size  50x24
 *       <Label   item="COMBW_MaxTrivialText">  Location (4,30)   Size 100x24
 *       <Editbox item="COMBW_MaxTrivialEdit">  Location (106,30) Size  50x24
 *       <Label   item="COMBW_SearchTextText">  Location (4,56)   Size 100x24
 *       <Editbox item="COMBW_SearchTextEdit">  Location (106,56) Size  80x24
 *       <Button  item="COMBW_SearchButton">    Location (50,84)  Size 100x20
 *     => absolute = the Location + (2,0)
 *
 *   <Screen item="COMBW_RecipeListArea"> parent = LeftSideArea (238x525)
 *       0 / 124(from top) / 0(from right) / 50(from BOTTOM)
 *     => (0,124)-(238,475)  => absolute (2,124)-(240,475), 238 x 351
 *   <Label item="COMBW_KnownRecipeText">  2 / 2 / 2(from right) / 22(from top)
 *     => (2,2)-(236,22)   => absolute (4,126)-(238,146), 234 x 20
 *   <Listbox item="COMBW_RecipeList">     2 / 24 / 2(from right) / 2(from BOTTOM)
 *     => (2,24)-(236,349) => absolute (4,148)-(238,473), 234 x 325
 *       <Columns><Width>140</Width><Heading>Recipe Name</Heading></Columns>
 *       <Columns><Width> 50</Width><Heading>Trivial</Heading></Columns>
 *
 *   <Screen item="COMBW_FavoritesArea">  parent = LeftSideArea (238x525)
 *       0 / 50(from BOTTOM) / 0(from right) / 0(from bottom)
 *     => (0,475)-(238,525) => absolute (2,475)-(240,525), 238 x 50
 *   <Label item="COMBW_FavoritesText">   2 / 0 / 0(from right) / 22(from top)
 *     => absolute (4,475)-(240,497), 236 x 22
 *   <Button item="COMBW_AddFavoriteButton">     Location (2,24)   Size 60x24
 *   <Button item="COMBW_RemoveFavoriteButton">  Location (64,24)  Size 60x24
 *   <Button item="COMBW_ShowFavoritesButton">   Location (124,24) Size 60x24
 *     => absolute = the Location + (2,475)
 *
 * THE ONE NUMBER THAT IS NOT THE ORACLE'S is the recipe list's ROW HEIGHT.
 * `COMBW_RecipeList` is a `<Listbox>`; the XML gives its rect and its two
 * column widths and says nothing about how tall a row is — the official
 * client measures that from the window font at runtime, and there is no
 * shipped number to quote. ORACLE_ROW_H below is therefore FABLE'S choice
 * (20, matching the 20px the same file uses for every Label and Editbox in
 * this window), flagged here rather than dressed up as a quotation.
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

// --- The left side (T419, board B335) ---

// COMBW_SearchArea's children, absolute in the 400x525 window.
constexpr irr::s32 ORACLE_MIN_TRIVIAL_LABEL_X = 6;
constexpr irr::s32 ORACLE_MIN_TRIVIAL_LABEL_Y = 4;
constexpr irr::s32 ORACLE_MAX_TRIVIAL_LABEL_Y = 30;
constexpr irr::s32 ORACLE_SEARCH_LABEL_Y      = 56;
constexpr irr::s32 ORACLE_SEARCH_LABEL_W      = 100;
constexpr irr::s32 ORACLE_SEARCH_FIELD_H      = 24;

constexpr irr::s32 ORACLE_TRIVIAL_EDIT_X = 108;
constexpr irr::s32 ORACLE_TRIVIAL_EDIT_W = 50;
constexpr irr::s32 ORACLE_SEARCH_EDIT_W  = 80;

constexpr irr::s32 ORACLE_SEARCH_BTN_X = 52;
constexpr irr::s32 ORACLE_SEARCH_BTN_Y = 84;
constexpr irr::s32 ORACLE_SEARCH_BTN_W = 100;
constexpr irr::s32 ORACLE_SEARCH_BTN_H = 20;

// COMBW_KnownRecipeText and COMBW_RecipeList.
constexpr irr::s32 ORACLE_LIST_LABEL_X = 4;
constexpr irr::s32 ORACLE_LIST_LABEL_Y = 126;
constexpr irr::s32 ORACLE_LIST_LABEL_W = 234;
constexpr irr::s32 ORACLE_LIST_LABEL_H = 20;

constexpr irr::s32 ORACLE_LIST_X = 4;
constexpr irr::s32 ORACLE_LIST_Y = 148;
constexpr irr::s32 ORACLE_LIST_W = 234;
constexpr irr::s32 ORACLE_LIST_H = 325;

// <Columns>: "Recipe Name" 140 wide, then "Trivial" 50 wide.
constexpr irr::s32 ORACLE_COL_NAME_W    = 140;
constexpr irr::s32 ORACLE_COL_TRIVIAL_W = 50;

// NOT the oracle's — see the header comment. 20px per row in oracle space.
constexpr irr::s32 ORACLE_ROW_H = 20;
constexpr int      RECIPE_ROWS_VISIBLE = ORACLE_LIST_H / ORACLE_ROW_H;  // 16

// COMBW_FavoritesText and the three favourite buttons.
constexpr irr::s32 ORACLE_FAV_LABEL_X = 4;
constexpr irr::s32 ORACLE_FAV_LABEL_Y = 475;
constexpr irr::s32 ORACLE_FAV_LABEL_W = 236;
constexpr irr::s32 ORACLE_FAV_LABEL_H = 22;

constexpr irr::s32 ORACLE_FAV_BTN_Y     = 499;
constexpr irr::s32 ORACLE_FAV_BTN_W     = 60;
constexpr irr::s32 ORACLE_FAV_BTN_H     = 24;
constexpr irr::s32 ORACLE_FAV_ADD_X     = 4;
constexpr irr::s32 ORACLE_FAV_REMOVE_X  = 66;
constexpr irr::s32 ORACLE_FAV_SHOW_X    = 126;

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

// --- The left side's rects (T419) ---

/** COMBW_MinTrivialText / COMBW_MaxTrivialText / COMBW_SearchTextText. */
inline irr::core::rect<irr::s32> minTrivialLabelRect(const UILayout& layout) {
    return scaled(layout, ORACLE_MIN_TRIVIAL_LABEL_X, ORACLE_MIN_TRIVIAL_LABEL_Y,
                  ORACLE_SEARCH_LABEL_W, ORACLE_SEARCH_FIELD_H);
}
inline irr::core::rect<irr::s32> maxTrivialLabelRect(const UILayout& layout) {
    return scaled(layout, ORACLE_MIN_TRIVIAL_LABEL_X, ORACLE_MAX_TRIVIAL_LABEL_Y,
                  ORACLE_SEARCH_LABEL_W, ORACLE_SEARCH_FIELD_H);
}
inline irr::core::rect<irr::s32> searchLabelRect(const UILayout& layout) {
    return scaled(layout, ORACLE_MIN_TRIVIAL_LABEL_X, ORACLE_SEARCH_LABEL_Y,
                  ORACLE_SEARCH_LABEL_W, ORACLE_SEARCH_FIELD_H);
}

/** COMBW_MinTrivialEdit / COMBW_MaxTrivialEdit / COMBW_SearchTextEdit. */
inline irr::core::rect<irr::s32> minTrivialEditRect(const UILayout& layout) {
    return scaled(layout, ORACLE_TRIVIAL_EDIT_X, ORACLE_MIN_TRIVIAL_LABEL_Y,
                  ORACLE_TRIVIAL_EDIT_W, ORACLE_SEARCH_FIELD_H);
}
inline irr::core::rect<irr::s32> maxTrivialEditRect(const UILayout& layout) {
    return scaled(layout, ORACLE_TRIVIAL_EDIT_X, ORACLE_MAX_TRIVIAL_LABEL_Y,
                  ORACLE_TRIVIAL_EDIT_W, ORACLE_SEARCH_FIELD_H);
}
inline irr::core::rect<irr::s32> searchEditRect(const UILayout& layout) {
    return scaled(layout, ORACLE_TRIVIAL_EDIT_X, ORACLE_SEARCH_LABEL_Y,
                  ORACLE_SEARCH_EDIT_W, ORACLE_SEARCH_FIELD_H);
}

/** COMBW_SearchButton. */
inline irr::core::rect<irr::s32> searchButtonRect(const UILayout& layout) {
    return scaled(layout, ORACLE_SEARCH_BTN_X, ORACLE_SEARCH_BTN_Y,
                  ORACLE_SEARCH_BTN_W, ORACLE_SEARCH_BTN_H);
}

/** COMBW_KnownRecipeText — the list's heading label. */
inline irr::core::rect<irr::s32> recipeListLabelRect(const UILayout& layout) {
    return scaled(layout, ORACLE_LIST_LABEL_X, ORACLE_LIST_LABEL_Y,
                  ORACLE_LIST_LABEL_W, ORACLE_LIST_LABEL_H);
}

/** COMBW_RecipeList — the listbox frame. */
inline irr::core::rect<irr::s32> recipeListRect(const UILayout& layout) {
    return scaled(layout, ORACLE_LIST_X, ORACLE_LIST_Y,
                  ORACLE_LIST_W, ORACLE_LIST_H);
}

/**
 * Map an oracle-space X / Y to fable's, WITHOUT deriving a size from it.
 *
 * `scaled()` scales an origin and a size independently, which is right for an
 * isolated rect and WRONG for a run of adjacent ones: 20 oracle rows of 20px
 * scaled that way gap or overlap by a pixel wherever the two integer
 * divisions disagree. The recipe rows are a run, so each row's bottom is the
 * NEXT row's scaled top and the list tiles exactly.
 */
inline irr::s32 scaledX(const UILayout& layout, irr::s32 x) {
    return layout.centerPopup.UpperLeftCorner.X +
           x * layout.centerPopup.getWidth() / ORACLE_WINDOW_W;
}
inline irr::s32 scaledY(const UILayout& layout, irr::s32 y) {
    return layout.centerPopup.UpperLeftCorner.Y +
           y * layout.centerPopup.getHeight() / ORACLE_WINDOW_H;
}

/**
 * One visible row of COMBW_RecipeList, 0..RECIPE_ROWS_VISIBLE-1. Degenerate
 * for an out-of-range index, the same contract slotRect() has.
 */
inline irr::core::rect<irr::s32> recipeRowRect(const UILayout& layout, int row) {
    if (row < 0 || row >= RECIPE_ROWS_VISIBLE) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return irr::core::rect<irr::s32>(
        scaledX(layout, ORACLE_LIST_X),
        scaledY(layout, ORACLE_LIST_Y + row * ORACLE_ROW_H),
        scaledX(layout, ORACLE_LIST_X + ORACLE_LIST_W),
        scaledY(layout, ORACLE_LIST_Y + (row + 1) * ORACLE_ROW_H));
}

/** The "Recipe Name" column of one row (<Columns><Width>140). */
inline irr::core::rect<irr::s32> recipeNameCellRect(const UILayout& layout, int row) {
    auto r = recipeRowRect(layout, row);
    if (r.getWidth() == 0) return r;
    r.LowerRightCorner.X = scaledX(layout, ORACLE_LIST_X + ORACLE_COL_NAME_W);
    return r;
}

/** The "Trivial" column of one row (<Columns><Width>50), right of the name. */
inline irr::core::rect<irr::s32> recipeTrivialCellRect(const UILayout& layout, int row) {
    auto r = recipeRowRect(layout, row);
    if (r.getWidth() == 0) return r;
    r.UpperLeftCorner.X = scaledX(layout, ORACLE_LIST_X + ORACLE_COL_NAME_W);
    r.LowerRightCorner.X =
        scaledX(layout, ORACLE_LIST_X + ORACLE_COL_NAME_W + ORACLE_COL_TRIVIAL_W);
    return r;
}

/** COMBW_FavoritesText. */
inline irr::core::rect<irr::s32> favoritesLabelRect(const UILayout& layout) {
    return scaled(layout, ORACLE_FAV_LABEL_X, ORACLE_FAV_LABEL_Y,
                  ORACLE_FAV_LABEL_W, ORACLE_FAV_LABEL_H);
}

/** COMBW_AddFavoriteButton / RemoveFavoriteButton / ShowFavoritesButton. */
inline irr::core::rect<irr::s32> addFavoriteRect(const UILayout& layout) {
    return scaled(layout, ORACLE_FAV_ADD_X, ORACLE_FAV_BTN_Y,
                  ORACLE_FAV_BTN_W, ORACLE_FAV_BTN_H);
}
inline irr::core::rect<irr::s32> removeFavoriteRect(const UILayout& layout) {
    return scaled(layout, ORACLE_FAV_REMOVE_X, ORACLE_FAV_BTN_Y,
                  ORACLE_FAV_BTN_W, ORACLE_FAV_BTN_H);
}
inline irr::core::rect<irr::s32> showFavoritesRect(const UILayout& layout) {
    return scaled(layout, ORACLE_FAV_SHOW_X, ORACLE_FAV_BTN_Y,
                  ORACLE_FAV_BTN_W, ORACLE_FAV_BTN_H);
}

} // namespace TradeskillLayout
} // namespace Graphics
} // namespace EQT
