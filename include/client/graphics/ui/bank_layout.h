/*
 * BankLayout -- the bank window's geometry, and the only place it is computed
 * (T436, board B429).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_BankWnd.xml:
 *
 *   <Screen item="BankWnd">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>0</X><Y>25</Y></Location>
 *     <Size><CX>108</CX><CY>347</CY></Size>
 *     <Text>Bank</Text>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Sizable>false</Style_Sizable>
 *
 * UNSCALED, and the reason is the oracle's own: `Style_Sizable false`. That
 * is the exact condition under which T415 used EQUI_CombatAbilityWnd.xml's
 * offsets unscaled, and 347px fits inside 640x480 with room to spare, so
 * every interior rect keeps the oracle's own offset and nothing can round.
 *
 * THE ONE DEVIATION is the ANCHOR: the window is CENTRED rather than placed
 * at the oracle's absolute <Location> (0,25), because fable's top-left is the
 * player status panel and the two would overlap for 43px. T434 flagged the
 * identical deviation for the Options window and T415 for the combat
 * abilities window; flagged here, not silently absorbed.
 *
 * Every child of this window is <RelativePosition>true</RelativePosition>
 * with a plain <Location>/<Size>, so there is no anchor arithmetic to do --
 * the numbers below are transcribed, not derived.
 *
 *   <Label  item="BW_BankerName">   (0,1)     100x40   AlignCenter true
 *   <InvSlot item="BW_BankSlot0">   (10,32)    40x40   EQType 2000
 *   <InvSlot item="BW_BankSlot1">   (10,72)    40x40   EQType 2001
 *   <InvSlot item="BW_BankSlot2">   (10,112)   40x40   EQType 2002
 *   <InvSlot item="BW_BankSlot3">   (11,152)   40x40   EQType 2003
 *   <InvSlot item="BW_BankSlot4">   (50,32)    40x40   EQType 2004
 *   <InvSlot item="BW_BankSlot5">   (50,72)    40x40   EQType 2005
 *   <InvSlot item="BW_BankSlot6">   (50,112)   40x40   EQType 2006
 *   <InvSlot item="BW_BankSlot7">   (50,152)   40x40   EQType 2007
 *     each <Background>A_RecessedBox</Background> with
 *     <ItemOffsetX>2</ItemOffsetX> <ItemOffsetY>2</ItemOffsetY>
 *     THE GRID IS COLUMN-MAJOR: 0..3 run down the left column, 4..7 down the
 *     right. Slot 3's x is 11 and not 10 -- an off-by-one in the shipped
 *     file, carried verbatim rather than tidied, because the point of this
 *     header is that it says what the oracle says.
 *   <Button item="BW_Money0">       (11,194)   80x24   A_PlatinumCoin decal
 *   <Button item="BW_Money1">       (11,218)   80x24   A_GoldCoin
 *   <Button item="BW_Money2">       (11,242)   80x24   A_SilverCoin
 *   <Button item="BW_Money3">       (11,266)   80x24   A_CopperCoin
 *     each <DecalOffset>(2,2)</DecalOffset> <DecalSize>18x18</DecalSize>
 *   <Button item="BW_ChangeButton"> (2,297)    50x24   Text "Change"
 *   <Button item="BW_DoneButton">   (55,297)   40x24   Text "Done"
 *
 * EIGHT SLOTS, NOT SIXTEEN. `include/client/inventory/inventory_constants.h`
 * declares BANK_COUNT = 16, which is a POST-Velious bank; the era XML shipped
 * with this client declares BW_BankSlot0..7 and nothing more, and the era
 * rule (launch through Velious) governs what is drawn. The extra eight slot
 * ids stay in the constants because the wire range is theirs; the window
 * shows the eight the oracle shows.
 *
 * THE SHARED BANK IS DECLARED AND EMPTY. `BW_SharedBankLabel`,
 * `BW_SharedBankSlot0`, `BW_SharedBankSlot1`, `BW_SharedMoney0` and
 * `BW_AltStorageButton` appear in this file with a <ScreenID> AND NOTHING
 * ELSE -- no <Location>, no <Size>, no draw template -- and they ARE listed
 * in the Screen's <Pieces>. So the era file itself declares them as stubs
 * with no geometry. They are not drawn, not hit-tested, and there is no rect
 * for them here, because the oracle supplies none. That is the answer to the
 * plan's "(+ shared? quote the era XML)" question, quoted rather than
 * guessed.
 *
 * THE ONE NUMBER THAT IS NOT THE ORACLE'S is the title band, exactly as in
 * ItemDisplayLayout: `Style_Titlebar true` / `Style_Closebox true` give a bar
 * whose height comes from the window font and the `WDT_Rounded` art, neither
 * of which is a number in a shipped file. TITLE_H is fable's own 18, the
 * height its text batch draws a title row at, and every child rect below is
 * measured from the bottom of that bar -- which is what the official client's
 * client-area inset does.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace BankLayout {

// <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 108;
constexpr irr::s32 WINDOW_H = 347;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;

// BW_BankerName.
constexpr irr::s32 NAME_X = 0;
constexpr irr::s32 NAME_Y = 1;
constexpr irr::s32 NAME_W = 100;
constexpr irr::s32 NAME_H = 40;

// BW_BankSlot0..7, transcribed above (note slot 3's x of 11).
constexpr int SLOT_COUNT = 8;
constexpr irr::s32 SLOT_SIZE = 40;
constexpr irr::s32 SLOT_X[SLOT_COUNT] = {10, 10, 10, 11, 50, 50, 50, 50};
constexpr irr::s32 SLOT_Y[SLOT_COUNT] = {32, 72, 112, 152, 32, 72, 112, 152};
// <ItemOffsetX>/<ItemOffsetY>: where the item art sits inside the recess.
constexpr irr::s32 ITEM_OFFSET = 2;
constexpr irr::s32 ITEM_SIZE = SLOT_SIZE - ITEM_OFFSET * 2;  // 36

// The wire slot id each cell shows: <EQType>2000</EQType> .. 2007, which is
// inventory_constants.h's BANK_BEGIN .. BANK_BEGIN+7.
constexpr int16_t EQTYPE_BEGIN = 2000;

// BW_Money0..3, platinum/gold/silver/copper, in element order -- the same
// order InventoryLayout::Coin uses, so coinSprite(denom) is CoinPlatinum+denom.
constexpr int COIN_COUNT = 4;
constexpr irr::s32 COIN_X = 11;
constexpr irr::s32 COIN_Y[COIN_COUNT] = {194, 218, 242, 266};
constexpr irr::s32 COIN_W = 80;
constexpr irr::s32 COIN_H = 24;
constexpr irr::s32 DECAL_OFFSET = 2;
constexpr irr::s32 DECAL_SIZE = 18;

// BW_ChangeButton and BW_DoneButton.
constexpr irr::s32 CHANGE_X = 2;
constexpr irr::s32 CHANGE_Y = 297;
constexpr irr::s32 CHANGE_W = 50;
constexpr irr::s32 CHANGE_H = 24;
constexpr irr::s32 DONE_X = 55;
constexpr irr::s32 DONE_Y = 297;
constexpr irr::s32 DONE_W = 40;
constexpr irr::s32 DONE_H = 24;

/**
 * The whole window, in screen coordinates: the oracle's own 108x347,
 * CENTRED (see the flagged deviation), clamped so it is always wholly on
 * screen.
 */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    irr::s32 x = (layout.screenW - WINDOW_W) / 2;
    irr::s32 y = (layout.screenH - WINDOW_H) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return irr::core::rect<irr::s32>(x, y, x + WINDOW_W, y + WINDOW_H);
}

/** Style_Titlebar: the row holding <Text>Bank</Text>. */
inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
                                     w.LowerRightCorner.X,
                                     w.UpperLeftCorner.Y + TITLE_H);
}

/** Style_Closebox: the square at the right-hand end of the title bar. */
inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return irr::core::rect<irr::s32>(t.LowerRightCorner.X - TITLE_H,
                                     t.UpperLeftCorner.Y, t.LowerRightCorner.X,
                                     t.LowerRightCorner.Y);
}

/** The client area -- everything below the title bar. */
inline irr::core::rect<irr::s32> clientRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X,
                                     w.UpperLeftCorner.Y + TITLE_H,
                                     w.LowerRightCorner.X, w.LowerRightCorner.Y);
}

/**
 * THE one placement body: every rect below is a <Location>/<Size> offset
 * from the client area's origin, so the draw and both hit tests cannot
 * drift (T57's rule).
 */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x,
                                    irr::s32 y, irr::s32 w, irr::s32 h) {
    const auto c = clientRect(layout);
    const irr::s32 sx = c.UpperLeftCorner.X + x;
    const irr::s32 sy = c.UpperLeftCorner.Y + y;
    return irr::core::rect<irr::s32>(sx, sy, sx + w, sy + h);
}

/** BW_BankerName. */
inline irr::core::rect<irr::s32> bankerNameRect(const UILayout& layout) {
    return at(layout, NAME_X, NAME_Y, NAME_W, NAME_H);
}

/** BW_BankSlot<i>, 0..7. Degenerate rect for an out-of-range index. */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOT_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, SLOT_X[i], SLOT_Y[i], SLOT_SIZE, SLOT_SIZE);
}

/** The item art inside slot `i`, at <ItemOffsetX>/<ItemOffsetY>. */
inline irr::core::rect<irr::s32> slotItemRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOT_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, SLOT_X[i] + ITEM_OFFSET, SLOT_Y[i] + ITEM_OFFSET,
              ITEM_SIZE, ITEM_SIZE);
}

/** BW_Money<denom>, 0=platinum .. 3=copper. */
inline irr::core::rect<irr::s32> coinRect(const UILayout& layout, int denom) {
    if (denom < 0 || denom >= COIN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return at(layout, COIN_X, COIN_Y[denom], COIN_W, COIN_H);
}

/** The coin decal inside it, at <DecalOffset>/<DecalSize>. */
inline irr::core::rect<irr::s32> coinDecalRect(const UILayout& layout,
                                               int denom) {
    if (denom < 0 || denom >= COIN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return at(layout, COIN_X + DECAL_OFFSET, COIN_Y[denom] + DECAL_OFFSET,
              DECAL_SIZE, DECAL_SIZE);
}

/** BW_ChangeButton. */
inline irr::core::rect<irr::s32> changeButtonRect(const UILayout& layout) {
    return at(layout, CHANGE_X, CHANGE_Y, CHANGE_W, CHANGE_H);
}

/** BW_DoneButton. */
inline irr::core::rect<irr::s32> doneButtonRect(const UILayout& layout) {
    return at(layout, DONE_X, DONE_Y, DONE_W, DONE_H);
}

}  // namespace BankLayout
}  // namespace Graphics
}  // namespace EQT
