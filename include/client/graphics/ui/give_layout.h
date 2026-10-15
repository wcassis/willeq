/*
 * GiveLayout -- the NPC give window's geometry, and the only place it is
 * computed (B896, GitLab #212).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_GiveWnd.xml:
 *
 *   <Screen item="GiveWnd">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>0</X><Y>25</Y></Location>
 *     <Size><CX>108</CX><CY>375</CY></Size>
 *     <Text>Give</Text>
 *     <DrawTemplate>WDT_Rounded</DrawTemplate>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Sizable>false</Style_Sizable>
 *
 * UNSCALED, for the oracle's own reason: Style_Sizable false (T415's rule, the
 * bank's and the container's). 25 + 375 = 400 fits inside 640x480.
 *
 * Every child is <RelativePosition>true</RelativePosition> with a plain
 * <Location>/<Size>, so every number below is TRANSCRIBED:
 *
 *   <Label  item="GVW_NPCName">       (0,1)    95x40  AlignCenter
 *   <InvSlot GVW_MyItemSlot0..3>      (30, 32/72/112/152)  40x40  EQType 3000-3003,
 *                                     A_RecessedBox, ItemOffset 2,2 -- ONE column
 *   <Button GVW_MyMoney0..3>          (5, 197/221/245/269) 85x24  PP/GP/SP/CP decal
 *   <Button GVW_Give_Button>          (5,300)  85x20  "Give"
 *   <Button GVW_Cancel_Button>        (5,324)  85x20  "Cancel"
 *
 * The four slots are the same wire slots as the trade window's first four
 * (EQType 3000-3003): the NPC hand-in IS a trade with an NPC partner
 * (OP_TradeRequest to the NPC, OP_TradeRequestAck back, OP_MoveItem 30->3000;
 * MEASURED, review/UIGAP-ORACLE/212/trade.tsv row 9), and Give is the trade's
 * accept (OP_TradeAcceptClick, row 10).
 *
 * TITLE_H is fable's own 18, as in TradeLayout and ContainerLayout:
 * 18 + 324 + 20 = 362 < 375, so Cancel still sits inside the oracle's window.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace GiveLayout {

constexpr irr::s32 WINDOW_W = 108;
constexpr irr::s32 WINDOW_H = 375;
constexpr irr::s32 ORACLE_X = 0;
constexpr irr::s32 ORACLE_Y = 25;

// NOT the oracle's -- see TradeLayout.
constexpr irr::s32 TITLE_H = 18;

// GVW_NPCName.
constexpr irr::s32 NAME_X = 0;
constexpr irr::s32 NAME_Y = 1;
constexpr irr::s32 NAME_W = 95;
constexpr irr::s32 NAME_H = 40;

// GVW_MyItemSlot0..3.
constexpr int SLOT_COUNT = 4;
constexpr irr::s32 SLOT_SIZE = 40;
constexpr irr::s32 SLOT_X = 30;
constexpr irr::s32 SLOT_Y[SLOT_COUNT] = {32, 72, 112, 152};
constexpr int EQTYPE_BEGIN = 3000;

// GVW_MyMoney0..3.
constexpr int COIN_COUNT = 4;
constexpr irr::s32 COIN_X = 5;
constexpr irr::s32 COIN_Y[COIN_COUNT] = {197, 221, 245, 269};
constexpr irr::s32 COIN_W = 85;
constexpr irr::s32 COIN_H = 24;

// GVW_Give_Button / GVW_Cancel_Button.
constexpr irr::s32 BUTTON_X = 5;
constexpr irr::s32 BUTTON_W = 85;
constexpr irr::s32 BUTTON_H = 20;
constexpr irr::s32 GIVE_Y = 300;
constexpr irr::s32 CANCEL_Y = 324;

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

inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x, irr::s32 y,
                                    irr::s32 w, irr::s32 h) {
    const auto o = windowOrigin(layout);
    const irr::s32 sx = o.X + x;
    const irr::s32 sy = o.Y + TITLE_H + y;
    return irr::core::rect<irr::s32>(sx, sy, sx + w, sy + h);
}

inline irr::core::rect<irr::s32> nameRect(const UILayout& layout) {
    return at(layout, NAME_X, NAME_Y, NAME_W, NAME_H);
}

/** GVW_MyItemSlot<i>, i in 0..3. Degenerate out of range. */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int i) {
    if (i < 0 || i >= SLOT_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, SLOT_X, SLOT_Y[i], SLOT_SIZE, SLOT_SIZE);
}

/** GVW_MyMoney<denom>, 0=platinum .. 3=copper. */
inline irr::core::rect<irr::s32> coinRect(const UILayout& layout, int denom) {
    if (denom < 0 || denom >= COIN_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return at(layout, COIN_X, COIN_Y[denom], COIN_W, COIN_H);
}

inline irr::core::rect<irr::s32> giveButtonRect(const UILayout& layout) {
    return at(layout, BUTTON_X, GIVE_Y, BUTTON_W, BUTTON_H);
}

inline irr::core::rect<irr::s32> cancelButtonRect(const UILayout& layout) {
    return at(layout, BUTTON_X, CANCEL_Y, BUTTON_W, BUTTON_H);
}

}  // namespace GiveLayout
}  // namespace Graphics
}  // namespace EQT
