/*
 * TradeLayout -- the player trade window's geometry, and the only place it is
 * computed (B896, GitLab #212).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeWnd.xml:
 *
 *   <Screen item="TradeWnd">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>0</X><Y>25</Y></Location>
 *     <Size><CX>236</CX><CY>365</CY></Size>
 *     <Text>Trade</Text>
 *     <DrawTemplate>WDT_Def</DrawTemplate>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Sizable>true</Style_Sizable>
 *
 * UNSCALED. The window is Style_Sizable true, which is T415's condition for
 * scaling a window onto centerPopup only when it does NOT fit; this one does
 * (25 + 365 = 390 < 480), so it keeps the oracle's own size and <Location>.
 *
 * Every child is <RelativePosition>true</RelativePosition>. The slots, the
 * names and the coin rows carry a plain <Location>/<Size> and are
 * TRANSCRIBED:
 *
 *   <Label  item="TRDW_HisName">     (0,1)    95x40  AlignCenter
 *   <InvSlot TRDW_TradeSlot8..11>    (5, 32/72/112/152)   40x40  EQType 3008-3011
 *   <InvSlot TRDW_TradeSlot12..15>   (50,32/72/112/152)   40x40  EQType 3012-3015
 *   <Button TRDW_HisMoney0..3>       (5, 197/221/245/269) 85x24  PP/GP/SP/CP decal
 *   <Label  item="TRDW_MyName">      (125,1)  95x40  AlignCenter
 *   <InvSlot TRDW_TradeSlot0..3>     (130,32/72/112/152)  40x40  EQType 3000-3003
 *   <InvSlot TRDW_TradeSlot4..7>     (175,32/72/112/152)  40x40  EQType 3004-3007
 *   <Button TRDW_MyMoney0..3>        (130,197/221/245/269) 85x24 "Drop coins here"
 *     every InvSlot: <Background>A_RecessedBox</Background>,
 *     <ItemOffsetX>2</ItemOffsetX> <ItemOffsetY>2</ItemOffsetY>;
 *     every money Button: A_RecessedBox + an 18x18 coin decal at (2,2).
 *
 * THE PARTNER IS THE LEFT COLUMN AND OUR OWN SIDE THE RIGHT, and the slots
 * are COLUMN-MAJOR (3000-3003 down the left half of our column, 3004-3007 down
 * the right): both are in the file and both were MEASURED on the official
 * client (review/UIGAP-ORACLE/212/trade.tsv rows 1, 3 and 6 -- B's second item
 * went to 3004, the top of the right-hand column).
 *
 * THE TWO BUTTONS ARE ANCHORED, NOT LOCATED, and the arithmetic is written out:
 *
 *   <Button TRDW_Trade_Button>  LeftAnchorOffset 5   RightAnchorOffset 90
 *                               TopAnchorOffset 37   BottomAnchorOffset 5
 *                               TopAnchorToTop false  BottomAnchorToTop false
 *   <Button TRDW_Cancel_Button> LeftAnchorOffset 130 RightAnchorOffset 215
 *                               (same vertical anchors)
 *
 * Left/right anchor to the LEFT edge (the default, no *ToLeft override), so
 * the x range is [left, right]; top and bottom anchor to the BOTTOM edge, so
 * the y range is [clientH - 37, clientH - 5]: 85x32 buttons on the bottom row.
 *
 * THE ONE NUMBER THAT IS NOT THE ORACLE'S: TITLE_H, fable's own 18 -- the
 * identical reasoning BankLayout and ContainerLayout give (the title bar's
 * height comes from the window font and the WDT_Def art, neither of which is a
 * number in a shipped file). The client area is the window below that bar.
 *
 * The XML draws both names in white (<TextColor> 255,255,255); the official
 * client draws them RED and turns the accepting side's name GREEN (MEASURED,
 * trade.tsv rows 1, 4, 6). That colour rule lives in trade_panel.h.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace TradeLayout {

// <Screen> <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 236;
constexpr irr::s32 WINDOW_H = 365;

// <Screen> <Location>, verbatim (RelativePosition false: screen pixels).
constexpr irr::s32 ORACLE_X = 0;
constexpr irr::s32 ORACLE_Y = 25;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;
constexpr irr::s32 CLIENT_H = WINDOW_H - TITLE_H;

// TRDW_HisName / TRDW_MyName.
constexpr irr::s32 HIS_NAME_X = 0;
constexpr irr::s32 MY_NAME_X = 125;
constexpr irr::s32 NAME_Y = 1;
constexpr irr::s32 NAME_W = 95;
constexpr irr::s32 NAME_H = 40;

// The InvSlots. Index i of a side is its i-th slot in EQType order.
constexpr int SLOTS_PER_SIDE = 8;
constexpr irr::s32 SLOT_SIZE = 40;
constexpr irr::s32 ITEM_OFFSET = 2;
constexpr irr::s32 ITEM_SIZE = SLOT_SIZE - ITEM_OFFSET * 2;  // 36
// Our own TRDW_TradeSlot0..7 (EQType 3000..3007).
constexpr irr::s32 MY_SLOT_X[SLOTS_PER_SIDE] = {130, 130, 130, 130, 175, 175, 175, 175};
// The partner's TRDW_TradeSlot8..15 (EQType 3008..3015).
constexpr irr::s32 HIS_SLOT_X[SLOTS_PER_SIDE] = {5, 5, 5, 5, 50, 50, 50, 50};
constexpr irr::s32 SLOT_Y[SLOTS_PER_SIDE] = {32, 72, 112, 152, 32, 72, 112, 152};
// <EQType> of TRDW_TradeSlot0 -- inventory_constants.h TRADE_BEGIN.
constexpr int EQTYPE_BEGIN = 3000;
// <EQType> of TRDW_TradeSlot8 -- the partner's half, never a model slot.
constexpr int HIS_EQTYPE_BEGIN = 3008;

// TRDW_HisMoney0..3 / TRDW_MyMoney0..3: platinum, gold, silver, copper.
constexpr int COIN_COUNT = 4;
constexpr irr::s32 HIS_COIN_X = 5;
constexpr irr::s32 MY_COIN_X = 130;
constexpr irr::s32 COIN_Y[COIN_COUNT] = {197, 221, 245, 269};
constexpr irr::s32 COIN_W = 85;
constexpr irr::s32 COIN_H = 24;
constexpr irr::s32 DECAL_OFFSET = 2;
constexpr irr::s32 DECAL_SIZE = 18;

// TRDW_Trade_Button / TRDW_Cancel_Button anchor offsets, verbatim.
constexpr irr::s32 TRADE_LEFT = 5;
constexpr irr::s32 TRADE_RIGHT = 90;
constexpr irr::s32 CANCEL_LEFT = 130;
constexpr irr::s32 CANCEL_RIGHT = 215;
constexpr irr::s32 BUTTON_TOP_FROM_BOTTOM = 37;
constexpr irr::s32 BUTTON_BOTTOM_FROM_BOTTOM = 5;

/** The window's top-left: the oracle's <Location>, clamped on screen. */
// B1075 (#395): the oracle's (ORACLE_X, ORACLE_Y) is content history; WHERE
// the window sits is the layout table's PairLeft anchor -- right-aligned
// against the inventory it is always open with (inventory+trade is a
// required co-open set), at its own oracle size.
inline irr::core::vector2di windowOrigin(const UILayout& layout) {
    return layout.placePairLeft(WINDOW_W, WINDOW_H).UpperLeftCorner;
}

inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    const auto o = windowOrigin(layout);
    return irr::core::rect<irr::s32>(o.X, o.Y, o.X + WINDOW_W, o.Y + WINDOW_H);
}

inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
                                     w.LowerRightCorner.X, w.UpperLeftCorner.Y + TITLE_H);
}

inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return irr::core::rect<irr::s32>(t.LowerRightCorner.X - TITLE_H, t.UpperLeftCorner.Y,
                                     t.LowerRightCorner.X, t.LowerRightCorner.Y);
}

/** THE one placement body (T57's rule): a child's offset from the client origin. */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x, irr::s32 y,
                                    irr::s32 w, irr::s32 h) {
    const auto o = windowOrigin(layout);
    const irr::s32 sx = o.X + x;
    const irr::s32 sy = o.Y + TITLE_H + y;
    return irr::core::rect<irr::s32>(sx, sy, sx + w, sy + h);
}

inline irr::core::rect<irr::s32> hisNameRect(const UILayout& layout) {
    return at(layout, HIS_NAME_X, NAME_Y, NAME_W, NAME_H);
}
inline irr::core::rect<irr::s32> myNameRect(const UILayout& layout) {
    return at(layout, MY_NAME_X, NAME_Y, NAME_W, NAME_H);
}

/** TRDW_TradeSlot<i> (ours, i in 0..7). Degenerate out of range. */
inline irr::core::rect<irr::s32> mySlotRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOTS_PER_SIDE) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, MY_SLOT_X[i], SLOT_Y[i], SLOT_SIZE, SLOT_SIZE);
}
/** TRDW_TradeSlot<8+i> (the partner's, i in 0..7). */
inline irr::core::rect<irr::s32> hisSlotRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOTS_PER_SIDE) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, HIS_SLOT_X[i], SLOT_Y[i], SLOT_SIZE, SLOT_SIZE);
}
/** The item art inside a slot: <ItemOffsetX/Y> 2, so 36x36. */
inline irr::core::rect<irr::s32> itemArtRect(const irr::core::rect<irr::s32>& cell) {
    return irr::core::rect<irr::s32>(cell.UpperLeftCorner.X + ITEM_OFFSET,
                                     cell.UpperLeftCorner.Y + ITEM_OFFSET,
                                     cell.UpperLeftCorner.X + ITEM_OFFSET + ITEM_SIZE,
                                     cell.UpperLeftCorner.Y + ITEM_OFFSET + ITEM_SIZE);
}

/** TRDW_MyMoney<denom> / TRDW_HisMoney<denom>, 0=platinum .. 3=copper. */
inline irr::core::rect<irr::s32> myCoinRect(const UILayout& layout, int denom) {
    if (denom < 0 || denom >= COIN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, MY_COIN_X, COIN_Y[denom], COIN_W, COIN_H);
}
inline irr::core::rect<irr::s32> hisCoinRect(const UILayout& layout, int denom) {
    if (denom < 0 || denom >= COIN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, HIS_COIN_X, COIN_Y[denom], COIN_W, COIN_H);
}
/** A coin row's 18x18 decal at <DecalOffset> (2,2). */
inline irr::core::rect<irr::s32> coinDecalRect(const irr::core::rect<irr::s32>& row) {
    return irr::core::rect<irr::s32>(row.UpperLeftCorner.X + DECAL_OFFSET,
                                     row.UpperLeftCorner.Y + DECAL_OFFSET,
                                     row.UpperLeftCorner.X + DECAL_OFFSET + DECAL_SIZE,
                                     row.UpperLeftCorner.Y + DECAL_OFFSET + DECAL_SIZE);
}

/** TRDW_Trade_Button: x [5, 90], y [clientH - 37, clientH - 5]. */
inline irr::core::rect<irr::s32> tradeButtonRect(const UILayout& layout) {
    return at(layout, TRADE_LEFT, CLIENT_H - BUTTON_TOP_FROM_BOTTOM,
              TRADE_RIGHT - TRADE_LEFT, BUTTON_TOP_FROM_BOTTOM - BUTTON_BOTTOM_FROM_BOTTOM);
}
/** TRDW_Cancel_Button: x [130, 215], same row. */
inline irr::core::rect<irr::s32> cancelButtonRect(const UILayout& layout) {
    return at(layout, CANCEL_LEFT, CLIENT_H - BUTTON_TOP_FROM_BOTTOM,
              CANCEL_RIGHT - CANCEL_LEFT, BUTTON_TOP_FROM_BOTTOM - BUTTON_BOTTOM_FROM_BOTTOM);
}

}  // namespace TradeLayout
}  // namespace Graphics
}  // namespace EQT
