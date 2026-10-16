/*
 * TrainLayout -- the skill trainer window's geometry, and the only place it
 * is computed (B898, GitLab #214).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_TrainWindow.xml (stock
 * Titanium, Will 2026-09-23):
 *
 *   <Screen item = "TrainWindow">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>120</X><Y>20</Y></Location>
 *     <Size><CX>580</CX><CY>280</CY></Size>
 *     <Text>Training Window</Text>
 *     <DrawTemplate>WDT_Rounded</DrawTemplate>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Minimizebox>true</Style_Minimizebox>
 *     <Style_Sizable>false</Style_Sizable>
 *     <Pieces> TRNW_TrainButton, TRNW_CoinCount0..3, TRNW_SkillList,
 *              TRNW_ExpLabel, TRNW_ExpGauge, TRNW_PracticeLabel,
 *              TRNW_PracticeCount, TRNW_MoneyLabel, TRNW_Purse0..3,
 *              TW_DoneButton
 *
 * UNSCALED, and the reason is the oracle's own: `Style_Sizable false` -- the
 * condition under which T415 drew EQUI_CombatAbilityWnd.xml and T436 drew
 * EQUI_BankWnd.xml at their own offsets. 580x280 fits inside 640x480, so
 * every interior rect keeps the oracle's offset and nothing can round.
 *
 * THE ANCHOR IS A DEVIATION, flagged rather than absorbed: the window is
 * CENTRED, not placed at its absolute <Location> (120,20). At 640x480 that
 * Location puts the right edge at 700, off the screen; T436 made the same
 * call for the bank window (and T434 / T415 before it).
 *
 * Every child is <RelativePosition>true</RelativePosition> (the Listbox
 * omits the tag; its <Location> is relative too) with a plain
 * <Location>/<Size>, so the numbers below are TRANSCRIBED, not derived:
 *
 *   <Listbox item="TRNW_SkillList">     (2,2)     450x245  WDT_Inner
 *       <Style_Border>true</Style_Border> <Style_VScroll>true</Style_VScroll>
 *       <Columns>125 "Skill Name"
 *       <Columns> 95 "Rank"
 *       <Columns> 50 "Value"
 *       <Columns> 40 Header_PlatCoin
 *       <Columns> 40 Header_GoldCoin
 *       <Columns> 40 Header_SilvCoin
 *       <Columns> 40 Header_CoppCoin
 *   <Label  item="TRNW_ExpLabel">       (456,2)   108x20   "Experience", AlignCenter
 *   <Gauge  item="TRNW_ExpGauge">       (456,24)  108x16   EQType 4, FillTint (240,240,0)
 *   <Label  item="TRNW_PracticeLabel">  (456,48)   70x20   "Practices:", AlignRight
 *   <Label  item="TRNW_PracticeCount">  (530,48)   24x20   AlignCenter
 *   <Button item="TRNW_TrainButton">    (460,78)  100x24   "Train"
 *   <Label  item="TRNW_MoneyLabel">     (456,108) 120x24   "Money", AlignCenter
 *   <StaticAnimation TRNW_Purse0..3>    (470,136+20i) 18x20  A_PlatinumCoin ..
 *                                                           A_CopperCoin
 *   <Label  item="TRNW_CoinCount0..3">  (494,136+20i) 50x20
 *   <Button item="TW_DoneButton">       (460,226) 100x24   "Done"
 *
 * The seven column widths sum to 430 of the list's 450. The 20 left over is
 * where `Style_VScroll true` puts the scroll bar; its width comes from the
 * WDT_Inner art, which is not a number in a shipped file, so VSCROLL_W is
 * DERIVED (450 - 430), not quoted.
 *
 * NOT IN THIS XML AT ALL: languages (no column, no tab), AA training (that
 * is EQUI_AAWindow.xml) -- nothing post-Velious to exclude here.
 *
 * TWO NUMBERS ARE NOT THE ORACLE'S, the same two T436 flagged:
 *  - TITLE_H, the title band. `Style_Titlebar true` / `Style_Closebox true`
 *    draw a bar whose height comes from the window font and the
 *    `WDT_Rounded` art. 18 is fable's own (BankLayout's), and every child
 *    rect is measured from the bottom of that bar.
 *  - ROW_H, the list's row height. A <Listbox> row is measured from the
 *    window font at runtime; 20 is the flagged choice MerchantLayout and
 *    TradeskillLayout made for theirs. The heading strip is one row.
 *
 * `Style_Minimizebox true` is NOT drawn: fable has no minimised-window state
 * for any window, so the box would be a control with nothing behind it (the
 * T363 rule, made mechanical by T434).
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace TrainLayout {

// <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 580;
constexpr irr::s32 WINDOW_H = 280;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;

// TRNW_SkillList.
constexpr irr::s32 LIST_X = 2;
constexpr irr::s32 LIST_Y = 2;
constexpr irr::s32 LIST_W = 450;
constexpr irr::s32 LIST_H = 245;

// The seven <Columns> widths, in element order.
constexpr int COLUMN_COUNT = 7;
constexpr irr::s32 COL_W[COLUMN_COUNT] = {125, 95, 50, 40, 40, 40, 40};
enum Column {
    COL_NAME = 0, COL_RANK = 1, COL_VALUE = 2,
    COL_PLAT = 3, COL_GOLD = 4, COL_SILVER = 5, COL_COPPER = 6
};
constexpr irr::s32 columnsWidth() {
    irr::s32 w = 0;
    for (int c = 0; c < COLUMN_COUNT; ++c) w += COL_W[c];
    return w;
}
// DERIVED: what the columns leave of the list's width (Style_VScroll).
constexpr irr::s32 VSCROLL_W = LIST_W - columnsWidth();  // 20

// NOT the oracle's -- see the header comment.
constexpr irr::s32 ROW_H = 20;
// The heading strip is one row; the rest of the list is rows.
constexpr int ROWS_VISIBLE = (LIST_H - ROW_H) / ROW_H;  // 11

// TRNW_ExpLabel / TRNW_ExpGauge.
constexpr irr::s32 EXP_LABEL_X = 456, EXP_LABEL_Y = 2, EXP_LABEL_W = 108, EXP_LABEL_H = 20;
constexpr irr::s32 EXP_GAUGE_X = 456, EXP_GAUGE_Y = 24, EXP_GAUGE_W = 108, EXP_GAUGE_H = 16;

// TRNW_PracticeLabel / TRNW_PracticeCount.
constexpr irr::s32 PRACTICE_LABEL_X = 456, PRACTICE_LABEL_Y = 48;
constexpr irr::s32 PRACTICE_LABEL_W = 70, PRACTICE_LABEL_H = 20;
constexpr irr::s32 PRACTICE_COUNT_X = 530, PRACTICE_COUNT_Y = 48;
constexpr irr::s32 PRACTICE_COUNT_W = 24, PRACTICE_COUNT_H = 20;

// TRNW_TrainButton.
constexpr irr::s32 TRAIN_X = 460, TRAIN_Y = 78, TRAIN_W = 100, TRAIN_H = 24;

// TRNW_MoneyLabel.
constexpr irr::s32 MONEY_LABEL_X = 456, MONEY_LABEL_Y = 108;
constexpr irr::s32 MONEY_LABEL_W = 120, MONEY_LABEL_H = 24;

// TRNW_Purse0..3 and TRNW_CoinCount0..3: platinum, gold, silver, copper in
// element order -- the order InventoryLayout and BankLayout use, so the decal
// is UISprite::CoinPlatinum + denom.
constexpr int COIN_COUNT = 4;
constexpr irr::s32 PURSE_X = 470;
constexpr irr::s32 PURSE_W = 18, PURSE_H = 20;
constexpr irr::s32 COIN_COUNT_X = 494;
constexpr irr::s32 COIN_COUNT_W = 50, COIN_COUNT_H = 20;
constexpr irr::s32 COIN_Y[COIN_COUNT] = {136, 156, 176, 196};

// TW_DoneButton.
constexpr irr::s32 DONE_X = 460, DONE_Y = 226, DONE_W = 100, DONE_H = 24;

/**
 * The whole window, in screen coordinates: the oracle's own 580x280,
 * CENTRED (the flagged deviation), clamped so it never starts off screen.
 */
// B1075 (#395): the layout table's ScreenCentre anchor.
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    return layout.placeScreenCentre(WINDOW_W, WINDOW_H);
}

/** Style_Titlebar: the row holding <Text>Training Window</Text>. */
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
 * from the client area's origin, so the draw and the hit tests cannot drift
 * (T57's rule).
 */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x,
                                    irr::s32 y, irr::s32 w, irr::s32 h) {
    const auto c = clientRect(layout);
    const irr::s32 sx = c.UpperLeftCorner.X + x;
    const irr::s32 sy = c.UpperLeftCorner.Y + y;
    return irr::core::rect<irr::s32>(sx, sy, sx + w, sy + h);
}

/** TRNW_SkillList, the whole box (border, headings, rows, scroll bar). */
inline irr::core::rect<irr::s32> listRect(const UILayout& layout) {
    return at(layout, LIST_X, LIST_Y, LIST_W, LIST_H);
}

/** Left edge of column `c`, in client coordinates. */
constexpr irr::s32 columnX(int c) {
    irr::s32 x = LIST_X;
    for (int i = 0; i < c && i < COLUMN_COUNT; ++i) x += COL_W[i];
    return x;
}

/** The heading cell of column `c` (the strip above the rows). */
inline irr::core::rect<irr::s32> headerCellRect(const UILayout& layout, int c) {
    if (c < 0 || c >= COLUMN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, columnX(c), LIST_Y, COL_W[c], ROW_H);
}

/** On-screen row `row` (0..ROWS_VISIBLE-1), the columns' full width. */
inline irr::core::rect<irr::s32> rowRect(const UILayout& layout, int row) {
    if (row < 0 || row >= ROWS_VISIBLE) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, LIST_X, LIST_Y + ROW_H + row * ROW_H, columnsWidth(),
              ROW_H);
}

/** One cell of on-screen row `row`. */
inline irr::core::rect<irr::s32> cellRect(const UILayout& layout, int row, int c) {
    if (row < 0 || row >= ROWS_VISIBLE || c < 0 || c >= COLUMN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return at(layout, columnX(c), LIST_Y + ROW_H + row * ROW_H, COL_W[c], ROW_H);
}

/** Style_VScroll: the bar down the list's right-hand VSCROLL_W pixels. */
inline irr::core::rect<irr::s32> scrollBarRect(const UILayout& layout) {
    return at(layout, LIST_X + columnsWidth(), LIST_Y, VSCROLL_W, LIST_H);
}

/** The bar's up arrow (its top VSCROLL_W square). */
inline irr::core::rect<irr::s32> scrollUpRect(const UILayout& layout) {
    return at(layout, LIST_X + columnsWidth(), LIST_Y, VSCROLL_W, VSCROLL_W);
}

/** The bar's down arrow (its bottom VSCROLL_W square). */
inline irr::core::rect<irr::s32> scrollDownRect(const UILayout& layout) {
    return at(layout, LIST_X + columnsWidth(), LIST_Y + LIST_H - VSCROLL_W,
              VSCROLL_W, VSCROLL_W);
}

inline irr::core::rect<irr::s32> expLabelRect(const UILayout& layout) {
    return at(layout, EXP_LABEL_X, EXP_LABEL_Y, EXP_LABEL_W, EXP_LABEL_H);
}
inline irr::core::rect<irr::s32> expGaugeRect(const UILayout& layout) {
    return at(layout, EXP_GAUGE_X, EXP_GAUGE_Y, EXP_GAUGE_W, EXP_GAUGE_H);
}
inline irr::core::rect<irr::s32> practiceLabelRect(const UILayout& layout) {
    return at(layout, PRACTICE_LABEL_X, PRACTICE_LABEL_Y, PRACTICE_LABEL_W,
              PRACTICE_LABEL_H);
}
inline irr::core::rect<irr::s32> practiceCountRect(const UILayout& layout) {
    return at(layout, PRACTICE_COUNT_X, PRACTICE_COUNT_Y, PRACTICE_COUNT_W,
              PRACTICE_COUNT_H);
}
inline irr::core::rect<irr::s32> trainButtonRect(const UILayout& layout) {
    return at(layout, TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H);
}
inline irr::core::rect<irr::s32> moneyLabelRect(const UILayout& layout) {
    return at(layout, MONEY_LABEL_X, MONEY_LABEL_Y, MONEY_LABEL_W,
              MONEY_LABEL_H);
}

/** TRNW_Purse<denom>, 0=platinum .. 3=copper. */
inline irr::core::rect<irr::s32> purseRect(const UILayout& layout, int denom) {
    if (denom < 0 || denom >= COIN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, PURSE_X, COIN_Y[denom], PURSE_W, PURSE_H);
}

/** TRNW_CoinCount<denom>, 0=platinum .. 3=copper. */
inline irr::core::rect<irr::s32> coinCountRect(const UILayout& layout, int denom) {
    if (denom < 0 || denom >= COIN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, COIN_COUNT_X, COIN_Y[denom], COIN_COUNT_W, COIN_COUNT_H);
}

inline irr::core::rect<irr::s32> doneButtonRect(const UILayout& layout) {
    return at(layout, DONE_X, DONE_Y, DONE_W, DONE_H);
}

}  // namespace TrainLayout
}  // namespace Graphics
}  // namespace EQT
