/*
 * QuantityLayout -- the stack-split / coin-amount dialog's geometry, and the
 * only place it is computed (B442, GitLab #151).
 *
 * THE PLACEMENT IS THE ORACLE'S. Source:
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_QuantityWnd.xml
 *
 *   <Screen item="QuantityWnd">
 *       <RelativePosition>false</RelativePosition>
 *       <Location><X>300</X><Y>10</Y></Location>
 *       <Size><CX>150</CX><CY>90</CY></Size>
 *       <Text>Quantity</Text>
 *       <Style_Titlebar>true</Style_Titlebar>
 *       <Style_Closebox>true</Style_Closebox>
 *       <Style_Sizable>false</Style_Sizable>
 *       <Pieces>QTYW_Slider</Pieces>
 *       <Pieces>QTYW_SliderInput</Pieces>
 *       <Pieces>QTYW_Accept_Button</Pieces>
 *
 *   <Slider item="QTYW_Slider">          Location (20,8)  Size 100x20
 *   <Editbox item="QTYW_SliderInput">    Location (5,40)  Size 70x20
 *   <Button item="QTYW_Accept_Button">   Location (80,40) Size 54x20  "Accept"
 *
 * `RelativePosition false`: screen coordinates, unscaled, the T421/T434 rule
 * (150x90 at (300,10) fits 640x480). The three children are
 * `RelativePosition true`, i.e. relative to the client area.
 *
 * THE ONE NUMBER THAT IS NOT THE ORACLE'S is TITLE_H, for the reason
 * item_display_layout.h writes out: the bar's height comes from the font and
 * the WDT_Rounded art, not from a number in a shipped file. 18, the same
 * value the item display uses.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace QuantityLayout {

// <Location> and <Size>, verbatim.
constexpr irr::s32 WINDOW_X = 300;
constexpr irr::s32 WINDOW_Y = 10;
constexpr irr::s32 WINDOW_W = 150;
constexpr irr::s32 WINDOW_H = 90;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;

// The three pieces, verbatim, relative to the client area.
constexpr irr::s32 SLIDER_X = 20, SLIDER_Y = 8, SLIDER_W = 100, SLIDER_H = 20;
constexpr irr::s32 INPUT_X = 5, INPUT_Y = 40, INPUT_W = 70, INPUT_H = 20;
constexpr irr::s32 ACCEPT_X = 80, ACCEPT_Y = 40, ACCEPT_W = 54, ACCEPT_H = 20;

inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    // Clamped like ItemDisplayLayout::windowRect, for a screen narrower than
    // the oracle's Location + Size.
    const irr::s32 x = (layout.screenW >= WINDOW_X + WINDOW_W)
        ? WINDOW_X : layout.screenW - WINDOW_W;
    return {x, WINDOW_Y, x + WINDOW_W, WINDOW_Y + WINDOW_H};
}

inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
            w.LowerRightCorner.X, w.UpperLeftCorner.Y + TITLE_H};
}

inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return {t.LowerRightCorner.X - TITLE_H, t.UpperLeftCorner.Y,
            t.LowerRightCorner.X, t.LowerRightCorner.Y};
}

inline irr::core::rect<irr::s32> clientRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y + TITLE_H,
            w.LowerRightCorner.X, w.LowerRightCorner.Y};
}

inline irr::core::rect<irr::s32> childRect(const UILayout& layout, irr::s32 x, irr::s32 y,
                                           irr::s32 w, irr::s32 h) {
    const auto c = clientRect(layout);
    const irr::s32 x0 = c.UpperLeftCorner.X + x;
    const irr::s32 y0 = c.UpperLeftCorner.Y + y;
    return {x0, y0, x0 + w, y0 + h};
}

inline irr::core::rect<irr::s32> sliderRect(const UILayout& layout) {
    return childRect(layout, SLIDER_X, SLIDER_Y, SLIDER_W, SLIDER_H);
}
inline irr::core::rect<irr::s32> inputRect(const UILayout& layout) {
    return childRect(layout, INPUT_X, INPUT_Y, INPUT_W, INPUT_H);
}
inline irr::core::rect<irr::s32> acceptRect(const UILayout& layout) {
    return childRect(layout, ACCEPT_X, ACCEPT_Y, ACCEPT_W, ACCEPT_H);
}

/**
 * The slider value under `mouseX`, 1..maxValue: the slider's width mapped
 * linearly, the ends inclusive, clamped. The one place the mapping lives, so
 * the click and the drawn thumb cannot disagree.
 */
inline int valueAt(const UILayout& layout, int maxValue, int mouseX) {
    if (maxValue <= 1) return maxValue < 1 ? 0 : 1;
    const auto s = sliderRect(layout);
    const int span = s.getWidth() - 1;
    int dx = mouseX - s.UpperLeftCorner.X;
    if (dx < 0) dx = 0;
    if (dx > span) dx = span;
    // 1 at the left edge, maxValue at the right edge, rounded to nearest.
    return 1 + (dx * (maxValue - 1) + span / 2) / span;
}

/** The thumb's x for `value` -- valueAt()'s inverse. */
inline irr::s32 thumbX(const UILayout& layout, int maxValue, int value) {
    const auto s = sliderRect(layout);
    if (maxValue <= 1) return s.UpperLeftCorner.X;
    const int span = s.getWidth() - 1;
    return s.UpperLeftCorner.X + ((value - 1) * span) / (maxValue - 1);
}

} // namespace QuantityLayout
} // namespace Graphics
} // namespace EQT
