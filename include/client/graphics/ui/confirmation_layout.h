/*
 * ConfirmationLayout -- the Yes/No (or Ok) confirmation dialog's geometry, and
 * the only place it is computed (B442b, GitLab #151 rows 31-35).
 *
 * THE SIZE AND THE PIECES ARE THE ORACLE'S. Source:
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_ConfirmationDialog.xml
 *
 *   <Screen item="ConfirmationDialogBox">
 *       <RelativePosition>false</RelativePosition>
 *       <Location><X>550</X><Y>350</Y></Location>
 *       <Size><CX>274</CX><CY>200</CY></Size>
 *       <Style_Titlebar>true</Style_Titlebar>
 *       <Style_Closebox>false</Style_Closebox>
 *       <Style_Sizable>true</Style_Sizable>
 *       <Pieces>CD_Yes_Button</Pieces> <Pieces>CD_No_Button</Pieces>
 *       <Pieces>CD_OK_Button</Pieces>  <Pieces>CD_TextOutput</Pieces>
 *
 *   CD_Yes_Button  AutoStretch: left 2 (to left), right 66 (to LEFT),
 *                  top 22 / bottom 2 (both to BOTTOM)          -> x 2..66
 *   CD_No_Button   left 66 / right 2 (both to RIGHT), same rows -> x W-66..W-2
 *   CD_OK_Button   left 105 / right 169 (both to LEFT), same rows
 *   CD_TextOutput  left 2, top 2 (to top), right 2 (to right),
 *                  bottom 24 (to bottom): the STMLbox, WDT_Inner, VScroll
 *
 * All pieces are `RelativePosition true`, i.e. anchored in the client area
 * under the title bar. Measured on the official client
 * (review/UIGAP-ORACLE/151/run1.G41_plainclick_floor_with_item_held.png): a
 * 274-wide window, an EMPTY title bar, the text box filling it above two
 * buttons in its bottom corners -- the anchors above.
 *
 * TWO NUMBERS ARE NOT THE ORACLE'S:
 *   - TITLE_H, for the reason item_display_layout.h writes out (the bar is
 *     sized by the font and the WDT_Rounded art, not by a shipped number).
 *   - the PLACE. The XML's <Location> (550,350) is off a 640x480 screen and
 *     the official run drew the box at (258,170) on 1024x768 -- a saved
 *     position, not the XML's. The box is CENTRED, the same flagged anchor
 *     deviation the bank window makes (T436).
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace ConfirmationLayout {

constexpr irr::s32 WINDOW_W = 274;
constexpr irr::s32 WINDOW_H = 200;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;

// The anchors, verbatim.
constexpr irr::s32 BUTTON_TOP_FROM_BOTTOM = 22;
constexpr irr::s32 BUTTON_BOTTOM_FROM_BOTTOM = 2;
constexpr irr::s32 YES_LEFT = 2, YES_RIGHT = 66;          // both to the left edge
constexpr irr::s32 NO_LEFT_FROM_RIGHT = 66, NO_RIGHT_FROM_RIGHT = 2;
constexpr irr::s32 OK_LEFT = 105, OK_RIGHT = 169;         // both to the left edge
constexpr irr::s32 TEXT_LEFT = 2, TEXT_TOP = 2, TEXT_RIGHT = 2, TEXT_BOTTOM = 24;

inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    irr::s32 x = (layout.screenW - WINDOW_W) / 2;
    irr::s32 y = (layout.screenH - WINDOW_H) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return {x, y, x + WINDOW_W, y + WINDOW_H};
}

inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
            w.LowerRightCorner.X, w.UpperLeftCorner.Y + TITLE_H};
}

inline irr::core::rect<irr::s32> clientRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y + TITLE_H,
            w.LowerRightCorner.X, w.LowerRightCorner.Y};
}

inline irr::core::rect<irr::s32> buttonRow(const UILayout& layout, irr::s32 left,
                                           irr::s32 right) {
    const auto c = clientRect(layout);
    return {left, c.LowerRightCorner.Y - BUTTON_TOP_FROM_BOTTOM,
            right, c.LowerRightCorner.Y - BUTTON_BOTTOM_FROM_BOTTOM};
}

inline irr::core::rect<irr::s32> yesRect(const UILayout& layout) {
    const auto c = clientRect(layout);
    return buttonRow(layout, c.UpperLeftCorner.X + YES_LEFT, c.UpperLeftCorner.X + YES_RIGHT);
}

inline irr::core::rect<irr::s32> noRect(const UILayout& layout) {
    const auto c = clientRect(layout);
    return buttonRow(layout, c.LowerRightCorner.X - NO_LEFT_FROM_RIGHT,
                     c.LowerRightCorner.X - NO_RIGHT_FROM_RIGHT);
}

inline irr::core::rect<irr::s32> okRect(const UILayout& layout) {
    const auto c = clientRect(layout);
    return buttonRow(layout, c.UpperLeftCorner.X + OK_LEFT, c.UpperLeftCorner.X + OK_RIGHT);
}

inline irr::core::rect<irr::s32> textRect(const UILayout& layout) {
    const auto c = clientRect(layout);
    return {c.UpperLeftCorner.X + TEXT_LEFT, c.UpperLeftCorner.Y + TEXT_TOP,
            c.LowerRightCorner.X - TEXT_RIGHT, c.LowerRightCorner.Y - TEXT_BOTTOM};
}

}  // namespace ConfirmationLayout
}  // namespace Graphics
}  // namespace EQT
