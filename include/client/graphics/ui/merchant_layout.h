/*
 * MerchantLayout -- the merchant window's geometry, and the only place it is
 * computed (T436, board B429; B1078, GitLab #398).
 *
 * THE PLACEMENT IS THE ORACLE'S, NOT OURS -- B1078 (2026-10-15, Will's play
 * session: "the vendor window item icons are squashed"): the window now
 * draws at the oracle's OWN 374x480, anchored by the layout table's PairLeft
 * anchor, and every interior rect is the XML's own number OFFSET BY THE
 * ANCHOR -- the same unscaled rule bank_layout.h (T436) and options_layout.h
 * (T434) follow. B1075's ruling allows it: "windows keep their size, the
 * oracle's".
 *
 * THE ONE DEVIATION is UNIFORM scaling, only where the screen cannot fit the
 * window: ONE factor (the min of the width and height ratios, capped at 1)
 * is applied to the whole window and the PairLeft anchor keeps it against
 * the inventory (B1078's co-open shape). At 800x600 that factor is 80%
 * (384/480): the pair spans edge to edge, the band is 384 tall, and the
 * merchant is the one that gives the pixels -- uniformly, the min of the
 * width (100%) and height (80%) ratios. At 1024x768 and up the factor is 1
 * and every rect is the XML's own number to the pixel. ONE factor, never two:
 * the old `scaled()` mapped x by 400/374 and y by 350/480 into fable's
 * 400x350 partnerPopup, which is exactly the anisotropy B1078 removes.
 *
 * Source: /mnt/projects/EverQuestP1999/uifiles/default/EQUI_MerchantWnd.xml
 *
 *   <Screen item = "MerchantWnd">
 *       <RelativePosition>false</RelativePosition>
 *       <Location><X>0</X><Y>25</Y></Location>
 *       <Size><CX>374</CX><CY>480</CY></Size>
 *       <Text>Merchant</Text>
 *       <Style_Titlebar>true</Style_Titlebar>
 *       <Style_Closebox>true</Style_Closebox>
 *       <Style_Sizable>true</Style_Sizable>
 *       <Pieces>MW_MerchantName</Pieces>
 *       <Pieces>MW_ItemList</Pieces>
 *       <Pieces>MW_SelectedItemLabel</Pieces>
 *       <Pieces>MW_SelectedPriceLabel</Pieces>
 *       <Pieces>MW_SelectedItem</Pieces>
 *       <Pieces>MW_Buy_Button</Pieces>
 *       <Pieces>MW_Sell_Button</Pieces>
 *       <Pieces>MW_Done_Button</Pieces>
 *
 * Every child is anchor-positioned. `AnchorToLeft/ToTop == false` means the
 * offset is measured from the parent's RIGHT / BOTTOM edge -- the rule T415
 * and T421 both write out. Resolved against the 374x480 window:
 *
 *   <Label item = "MW_MerchantName">
 *       Left   4, ToLeft true  -> x0 = 4
 *       Top    4, ToTop  true  -> y0 = 4
 *       Right  4, ToLeft false -> x1 = 374-4  = 370
 *       Bottom24, ToTop  false -> y1 = 480-24 = 456
 *     => (4,4)-(370,456), i.e. 366 x 452, <AlignCenter>true</AlignCenter>
 *     THAT HEIGHT IS THE ORACLE'S AND IT IS ODD: the name label's box spans
 *     almost the whole window and lies UNDER the item list. An EQ <Label>
 *     draws its text at the TOP of its box and paints no background, so the
 *     visible result is a centred name on the window's first row -- which is
 *     what the official client shows. fable draws the text in the top row
 *     of the quoted rect for the same reason; the box is not filled.
 *
 *   <Listbox item = "MW_ItemList">
 *       4 / 28 / 4(from right) / 50(from bottom)
 *     => (4,28)-(370,430), i.e. 366 x 402
 *       <Style_Border>true</Style_Border> <Style_VScroll>true</Style_VScroll>
 *       <Columns> 24 (no heading -- the icon cell)
 *       <Columns>150 "Item Name"
 *       <Columns> 28 "Qty"
 *       <Columns> 48 Header_PlatCoin
 *       <Columns> 24 Header_GoldCoin
 *       <Columns> 24 Header_SilvCoin
 *       <Columns> 24 Header_CoppCoin
 *
 *   <Button item = "MW_SelectedItem">
 *       Left  84, ToLeft true  -> x0 = 84
 *       Top   46, ToTop  false -> y0 = 480-46 = 434
 *       Right126, ToLeft true  -> x1 = 126
 *       Bottom 4, ToTop  false -> y1 = 480-4  = 476
 *     => (84,434)-(126,476), i.e. 42 x 42
 *       <ButtonDrawTemplate><Normal>A_RecessedBox</Normal>
 *                           <NormalDecal>A_DragItem</NormalDecal>
 *       <DecalOffset>(2,2)</DecalOffset> <DecalSize>40x40</DecalSize>
 *     => the icon itself is (86,436) 40x40
 *
 *   <Label item = "MW_SelectedItemLabel">
 *       130 / 44(from bottom) / 90(from right) / 28(from bottom)
 *     => (130,436)-(284,452), i.e. 154 x 16
 *
 *   <Label item = "MW_SelectedPriceLabel">
 *       130 / 26(from bottom) / 90(from right) / 6(from bottom)
 *     => (130,454)-(284,474), i.e. 154 x 20
 *
 *   <Button item = "MW_Buy_Button">
 *       10 / 44(from bottom) / 74(from LEFT) / 6(from bottom)
 *     => (10,436)-(74,474), i.e. 64 x 38
 *
 *   <Button item = "MW_Sell_Button">
 *       10 / 44(from bottom) / 74(from LEFT) / 6(from bottom)
 *     => (10,436)-(74,474) -- THE SAME RECT AS BUY, byte for byte. This is
 *     not a transcription slip: the two elements carry identical anchors in
 *     the shipped file, so the official client can only ever show one of
 *     them at a time. See merchant_panel.h for which one fable draws when.
 *
 *   <Button item = "MW_Done_Button">
 *       74(from RIGHT) / 44(from bottom) / 10(from right) / 6(from bottom)
 *     => (300,436)-(364,474), i.e. 64 x 38
 *
 * So the bottom strip tiles cleanly: Buy/Sell 10..74, the selected-item icon
 * 84..126, the two labels 130..284, Done 300..364.
 *
 * ONE ELEMENT IN THE FILE IS NOT IN THE WINDOW. `MW_DoneButton`
 * (ScreenID `DoneButton`, note the missing underscores) is declared with a
 * full anchor set and appears in NO <Pieces> list, so the official client
 * never instantiates it. It is not drawn here either, and it is not a gap.
 *
 * TWO NUMBERS ARE NOT THE ORACLE'S, both flagged rather than dressed up:
 *  - ROW_H, the item list's row height. `MW_ItemList` is a <Listbox>; the
 *    XML gives its rect and its seven column widths and says nothing about
 *    how tall a row is. 24 is MEASURED (2026-10-15,
 *    review/UIGAP-ORACLE/152/run1.M02_lclick_merchant_row_Ale.png, 1024x768,
 *    window at native 374x480): the row names sit at y 266/290/314, i.e. a
 *    24 px pitch. The row icon there is a uniformly down-scaled square
 *    (~22 px inside the 24 px column); the column itself IS 24 wide in the
 *    XML, so at the window's own size the icon cell is 24x24 and
 *    `UIRenderer::squareIn()`'s identity.
 *  - The window CHROME. `Style_Titlebar true` / `Style_Closebox true` draw a
 *    bar whose height comes from the font and the `WDT_Def` art, neither of
 *    which is a number in a shipped file. fable's centre popups have no
 *    chrome at all (the tradeskill window is the precedent), so none is
 *    drawn: MW_Done_Button is the close control and ESC also closes.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace MerchantLayout {

// <Size>, verbatim -- the space every rect below is expressed in.
constexpr irr::s32 ORACLE_WINDOW_W = 374;
constexpr irr::s32 ORACLE_WINDOW_H = 480;

// MW_MerchantName.
constexpr irr::s32 ORACLE_NAME_X = 4;
constexpr irr::s32 ORACLE_NAME_Y = 4;
constexpr irr::s32 ORACLE_NAME_W = 366;
constexpr irr::s32 ORACLE_NAME_H = 452;

// MW_ItemList.
constexpr irr::s32 ORACLE_LIST_X = 4;
constexpr irr::s32 ORACLE_LIST_Y = 28;
constexpr irr::s32 ORACLE_LIST_W = 366;
constexpr irr::s32 ORACLE_LIST_H = 402;

// The seven <Columns> widths, in element order.
constexpr int COLUMN_COUNT = 7;
constexpr irr::s32 ORACLE_COL_W[COLUMN_COUNT] = {24, 150, 28, 48, 24, 24, 24};
enum Column {
    COL_ICON = 0, COL_NAME = 1, COL_QTY = 2,
    COL_PLAT = 3, COL_GOLD = 4, COL_SILVER = 5, COL_COPPER = 6
};

// MEASURED 2026-10-15, UIGAP-ORACLE/152 (the header comment).
constexpr irr::s32 ORACLE_ROW_H = 24;
// The heading strip the <Heading>/<Header> entries occupy, one row tall.
constexpr int ROWS_VISIBLE = (ORACLE_LIST_H - ORACLE_ROW_H) / ORACLE_ROW_H;  // 15

// MW_SelectedItem and its A_DragItem decal.
constexpr irr::s32 ORACLE_SELECTED_X = 84;
constexpr irr::s32 ORACLE_SELECTED_Y = 434;
constexpr irr::s32 ORACLE_SELECTED_W = 42;
constexpr irr::s32 ORACLE_SELECTED_H = 42;
constexpr irr::s32 ORACLE_DECAL_OFFSET = 2;
constexpr irr::s32 ORACLE_DECAL_SIZE = 40;

// MW_SelectedItemLabel.
constexpr irr::s32 ORACLE_ITEM_LABEL_X = 130;
constexpr irr::s32 ORACLE_ITEM_LABEL_Y = 436;
constexpr irr::s32 ORACLE_ITEM_LABEL_W = 154;
constexpr irr::s32 ORACLE_ITEM_LABEL_H = 16;

// MW_SelectedPriceLabel.
constexpr irr::s32 ORACLE_PRICE_LABEL_X = 130;
constexpr irr::s32 ORACLE_PRICE_LABEL_Y = 454;
constexpr irr::s32 ORACLE_PRICE_LABEL_W = 154;
constexpr irr::s32 ORACLE_PRICE_LABEL_H = 20;

// MW_Buy_Button AND MW_Sell_Button -- the same rect, see the header comment.
constexpr irr::s32 ORACLE_TRADE_BTN_X = 10;
constexpr irr::s32 ORACLE_TRADE_BTN_Y = 436;
constexpr irr::s32 ORACLE_TRADE_BTN_W = 64;
constexpr irr::s32 ORACLE_TRADE_BTN_H = 38;

// MW_Done_Button.
constexpr irr::s32 ORACLE_DONE_BTN_X = 300;
constexpr irr::s32 ORACLE_DONE_BTN_Y = 436;
constexpr irr::s32 ORACLE_DONE_BTN_W = 64;
constexpr irr::s32 ORACLE_DONE_BTN_H = 38;

// --- THE ONE scaling body (B1078) -----------------------------------------

/**
 * The ONE uniform factor in per-cent (100 = unscaled) that fits the oracle
 * 374x480 window into the middle band: the min of the band's width and height
 * ratios, capped at 1. ONE factor, never two (B1078).
 */
inline irr::s32 scalePct(const UILayout& layout) {
    const auto& b = layout.middleBand;
    irr::s32 f = std::min(b.getWidth() * 100 / ORACLE_WINDOW_W,
                          b.getHeight() * 100 / ORACLE_WINDOW_H);
    return f < 100 ? f : 100;
}

/**
 * The window, in screen coordinates (B1078): the oracle's own 374x480 at a
 * ONE uniform factor (`scalePct`), anchored in the pair's LEFT slot
 * (`partnerPopup`'s left edge) and top-anchored at the middle band's top,
 * clamped wholly on screen. The inventory takes the pair's RIGHT slot
 * (`inventoryPopup`), so the two are neighbours with the pair's own 0-gap.
 * At 1024x768 and up the factor is 1 and every rect is the XML's own number
 * to the pixel; at 800x600 the band is only 384 tall, so the factor is
 * 384/480 and the whole window shrinks uniformly -- never a separate x and y
 * factor, which is the anisotropy B1078 exists to kill. The draw body, every
 * interior rect below and the hit tests all read THIS rect, so what is drawn
 * and what is clicked cannot drift (T57's rule).
 */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    const irr::s32 f = scalePct(layout);
    const irr::s32 w = ORACLE_WINDOW_W * f / 100;
    const irr::s32 h = ORACLE_WINDOW_H * f / 100;
    const irr::s32 x = layout.partnerPopup.UpperLeftCorner.X;
    const irr::s32 y = layout.middleBand.UpperLeftCorner.Y;
    return layout.clampOnScreen(x, y, w, h);
}

/**
 * Map an oracle-window rect onto `windowRect()`, with ONE uniform factor
 * (B1078: never an x factor and a different y factor). Integer arithmetic
 * throughout, no rounding policy to drift.
 */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x,
                                    irr::s32 y, irr::s32 w, irr::s32 h) {
    const auto win = windowRect(layout);
    const irr::s32 f = scalePct(layout);
    const irr::s32 sx = win.UpperLeftCorner.X + x * f / 100;
    const irr::s32 sy = win.UpperLeftCorner.Y + y * f / 100;
    return irr::core::rect<irr::s32>(sx, sy, sx + w * f / 100, sy + h * f / 100);
}

/** The whole MW_MerchantName box (fable fills nothing of it). */
inline irr::core::rect<irr::s32> nameRect(const UILayout& layout) {
    return at(layout, ORACLE_NAME_X, ORACLE_NAME_Y,
              ORACLE_NAME_W, ORACLE_NAME_H);
}

/**
 * The top row of MW_MerchantName's box -- where an EQ <Label> actually
 * paints its text. The quoted rect is 452 tall (see the header comment);
 * the name is one row of it, and this is the band fable centres the text in.
 */
inline irr::core::rect<irr::s32> nameTextRect(const UILayout& layout) {
    return at(layout, ORACLE_NAME_X, ORACLE_NAME_Y, ORACLE_NAME_W,
              ORACLE_ROW_H);
}

/** MW_ItemList, the whole sunken box. */
inline irr::core::rect<irr::s32> listRect(const UILayout& layout) {
    return at(layout, ORACLE_LIST_X, ORACLE_LIST_Y,
              ORACLE_LIST_W, ORACLE_LIST_H);
}

/** The heading strip, one row tall, at the top of the list box. */
inline irr::core::rect<irr::s32> listHeaderRect(const UILayout& layout) {
    return at(layout, ORACLE_LIST_X, ORACLE_LIST_Y,
              ORACLE_LIST_W, ORACLE_ROW_H);
}

/** On-screen row `i` (0 = the first row under the heading strip). */
inline irr::core::rect<irr::s32> listRowRect(const UILayout& layout, int i) {
    if (i < 0 || i >= ROWS_VISIBLE) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, ORACLE_LIST_X,
              ORACLE_LIST_Y + ORACLE_ROW_H + i * ORACLE_ROW_H,
              ORACLE_LIST_W, ORACLE_ROW_H);
}

/**
 * One oracle x mapped to a screen x. The column cells are built from TWO of
 * these rather than from an origin plus a scaled width: `at(x) + at(w)` and
 * `at(x + w)` can differ by a pixel under integer division, which opens a
 * 1px gap between adjacent columns. Both edges of every cell go through
 * this, so the seven columns tile exactly.
 */
inline irr::s32 scaledX(const UILayout& layout, irr::s32 x) {
    const auto win = windowRect(layout);
    const irr::s32 f = scalePct(layout);
    return win.UpperLeftCorner.X + x * f / 100;
}

/** The oracle y of one row's top edge, inside the list box. */
inline irr::s32 rowTopY(const UILayout& layout, int i) {
    const auto win = windowRect(layout);
    const irr::s32 f = scalePct(layout);
    return win.UpperLeftCorner.Y +
           (ORACLE_LIST_Y + ORACLE_ROW_H + i * ORACLE_ROW_H) * f / 100;
}

/** The oracle x of column `c`'s left edge, inside the list box. */
inline irr::s32 columnOracleX(int c) {
    irr::s32 x = ORACLE_LIST_X;
    for (int k = 0; k < c && k < COLUMN_COUNT; ++k) x += ORACLE_COL_W[k];
    return x;
}

/** Column `c` of row `i`. The <Columns> widths tile from the list's left. */
inline irr::core::rect<irr::s32> listCellRect(const UILayout& layout, int i,
                                              int c) {
    if (c < 0 || c >= COLUMN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    if (i < 0 || i >= ROWS_VISIBLE) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const irr::s32 top = rowTopY(layout, i);
    const irr::s32 bottom = rowTopY(layout, i + 1);
    return irr::core::rect<irr::s32>(
        scaledX(layout, columnOracleX(c)), top,
        scaledX(layout, columnOracleX(c) + ORACLE_COL_W[c]), bottom);
}

/** Column `c` of the heading strip. */
inline irr::core::rect<irr::s32> listHeaderCellRect(const UILayout& layout,
                                                    int c) {
    if (c < 0 || c >= COLUMN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto strip = listHeaderRect(layout);
    return irr::core::rect<irr::s32>(
        scaledX(layout, columnOracleX(c)), strip.UpperLeftCorner.Y,
        scaledX(layout, columnOracleX(c) + ORACLE_COL_W[c]),
        strip.LowerRightCorner.Y);
}

/** MW_SelectedItem's 42x42 button. */
inline irr::core::rect<irr::s32> selectedItemRect(const UILayout& layout) {
    return at(layout, ORACLE_SELECTED_X, ORACLE_SELECTED_Y,
              ORACLE_SELECTED_W, ORACLE_SELECTED_H);
}

/** The A_DragItem decal inside it, at DecalOffset/DecalSize. */
inline irr::core::rect<irr::s32> selectedDecalRect(const UILayout& layout) {
    return at(layout, ORACLE_SELECTED_X + ORACLE_DECAL_OFFSET,
              ORACLE_SELECTED_Y + ORACLE_DECAL_OFFSET,
              ORACLE_DECAL_SIZE, ORACLE_DECAL_SIZE);
}

/** MW_SelectedItemLabel. */
inline irr::core::rect<irr::s32> selectedItemLabelRect(const UILayout& layout) {
    return at(layout, ORACLE_ITEM_LABEL_X, ORACLE_ITEM_LABEL_Y,
              ORACLE_ITEM_LABEL_W, ORACLE_ITEM_LABEL_H);
}

/** MW_SelectedPriceLabel. */
inline irr::core::rect<irr::s32> selectedPriceLabelRect(const UILayout& layout) {
    return at(layout, ORACLE_PRICE_LABEL_X, ORACLE_PRICE_LABEL_Y,
              ORACLE_PRICE_LABEL_W, ORACLE_PRICE_LABEL_H);
}

/** MW_Buy_Button and MW_Sell_Button, which share one rect in the XML. */
inline irr::core::rect<irr::s32> tradeButtonRect(const UILayout& layout) {
    return at(layout, ORACLE_TRADE_BTN_X, ORACLE_TRADE_BTN_Y,
              ORACLE_TRADE_BTN_W, ORACLE_TRADE_BTN_H);
}

/** MW_Done_Button. */
inline irr::core::rect<irr::s32> doneButtonRect(const UILayout& layout) {
    return at(layout, ORACLE_DONE_BTN_X, ORACLE_DONE_BTN_Y,
              ORACLE_DONE_BTN_W, ORACLE_DONE_BTN_H);
}

}  // namespace MerchantLayout
}  // namespace Graphics
}  // namespace EQT
