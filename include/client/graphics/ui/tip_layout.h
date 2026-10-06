#pragma once
/*
 * B834 (GitLab #202, UIGAP-small) -- the tip window's geometry, quoted from
 * the official client's uifiles/default/EQUI_TipWnd.xml.
 *
 *   <Screen item="TipWindow"> RelativePosition false, <Location> (280,0),
 *     <Size> 360x300, Style_Titlebar, Style_Closebox, Style_Sizable true,
 *     <Text> "Tip of the Day". Pieces, by their anchors (an offset from the
 *     client area's BOTTOM where <TopAnchorToTop>/<BottomAnchorToTop> are
 *     false, from its RIGHT where <RightAnchorToLeft>/<LeftAnchorToLeft> are
 *     false):
 *   TIP_Display (STMLbox)      L2  T2      R2(right)   B74(bottom)  VScroll
 *   TIP_History (Combobox)     L2  T72(b)  R2(right)   B48(bottom)
 *   TIP_NextTipButton          L42 T72(b)  R42(right)  B48(bottom)  "Show Another Tip"
 *   TIP_NoShowThisButton       L4  T40(b)  R16         B28(bottom)  checkbox
 *   TIP_NoShowThisLabel        L20 T42(b)  R100(right) B22(bottom)  "Don't show this tip again"
 *   TIP_NoShowButton           L4  T18(b)  R16         B6(bottom)   checkbox
 *   TIP_NoShowLabel            L20 T20(b)  R100(right) B0(bottom)   "Don't show any tips"
 *   TIP_CloseButton            L86(r) T34(b) R6(right) B10(bottom)  "Close"
 *
 * WHICH pieces show is MEASURED, not in the file (review/UIGAP-ORACLE/202/
 * tip_of_the_day_on_zone_in.png, inventory_tip_on_first_inventory_open.png):
 * the Tip of the Day shows Display, Show Another Tip, "Don't show any tips"
 * and Close; a context tip shows Display, the History combobox (reading the
 * tip's own title), BOTH checkboxes and Close.
 *
 * PLACEMENT, MEASURED: at 1024x768 the window sits at (664,234), flush with
 * the right edge (B834's T499 sweep; the same grabs). Neither the XML's
 * (280,0) nor defaults.ini's [TipWindow] XPos1024x768=505 / YPos=120 is
 * where it appeared, so fable anchors it to the right edge and scales the
 * measured y with the screen height -- ASSUMPTION for other resolutions.
 * Drawn unscaled. The title bar height is fable's own (the T434/B897 one).
 */

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace TipLayout {

constexpr irr::s32 WINDOW_W = 360;
constexpr irr::s32 WINDOW_H = 300;
constexpr irr::s32 TITLE_H = 18;          // fable's own title strip

constexpr irr::s32 MEASURED_Y_AT_768 = 234;

inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    const irr::s32 x = layout.screenW - WINDOW_W;
    irr::s32 y = MEASURED_Y_AT_768 * layout.screenH / 768;
    if (y + WINDOW_H > layout.screenH) y = layout.screenH - WINDOW_H;
    if (y < 0) y = 0;
    return {x, y, x + WINDOW_W, y + WINDOW_H};
}

inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y, w.LowerRightCorner.X,
            w.UpperLeftCorner.Y + TITLE_H};
}

inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return {t.LowerRightCorner.X - TITLE_H, t.UpperLeftCorner.Y, t.LowerRightCorner.X,
            t.LowerRightCorner.Y};
}

/** The client area every anchor is measured against. */
inline irr::core::rect<irr::s32> clientRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y + TITLE_H, w.LowerRightCorner.X,
            w.LowerRightCorner.Y};
}

// One anchored piece: left/right from the client's left (fromRight = false)
// or right edge; top/bottom from its top or bottom edge.
inline irr::core::rect<irr::s32> anchored(const UILayout& layout, irr::s32 l, bool lFromRight,
                                          irr::s32 t, bool tFromBottom, irr::s32 r,
                                          bool rFromRight, irr::s32 b, bool bFromBottom) {
    const auto c = clientRect(layout);
    const irr::s32 x0 = lFromRight ? c.LowerRightCorner.X - l : c.UpperLeftCorner.X + l;
    const irr::s32 x1 = rFromRight ? c.LowerRightCorner.X - r : c.UpperLeftCorner.X + r;
    const irr::s32 y0 = tFromBottom ? c.LowerRightCorner.Y - t : c.UpperLeftCorner.Y + t;
    const irr::s32 y1 = bFromBottom ? c.LowerRightCorner.Y - b : c.UpperLeftCorner.Y + b;
    return {x0, y0, x1, y1};
}

inline irr::core::rect<irr::s32> displayRect(const UILayout& l) {
    return anchored(l, 2, false, 2, false, 2, true, 74, true);
}
inline irr::core::rect<irr::s32> historyRect(const UILayout& l) {
    return anchored(l, 2, false, 72, true, 2, true, 48, true);
}
inline irr::core::rect<irr::s32> nextTipRect(const UILayout& l) {
    return anchored(l, 42, false, 72, true, 42, true, 48, true);
}
inline irr::core::rect<irr::s32> noShowThisBoxRect(const UILayout& l) {
    return anchored(l, 4, false, 40, true, 16, false, 28, true);
}
inline irr::core::rect<irr::s32> noShowThisLabelRect(const UILayout& l) {
    return anchored(l, 20, false, 42, true, 100, true, 22, true);
}
inline irr::core::rect<irr::s32> noShowBoxRect(const UILayout& l) {
    return anchored(l, 4, false, 18, true, 16, false, 6, true);
}
inline irr::core::rect<irr::s32> noShowLabelRect(const UILayout& l) {
    return anchored(l, 20, false, 20, true, 100, true, 0, true);
}
inline irr::core::rect<irr::s32> closeButtonRect(const UILayout& l) {
    return anchored(l, 86, true, 34, true, 6, true, 10, true);
}

/** Body text rows the display shows at once (fable's 12px line). */
constexpr irr::s32 LINE_H = 12;
inline int visibleLines(const UILayout& l) {
    const auto d = displayRect(l);
    return (d.getHeight() - 4) / LINE_H;
}

}  // namespace TipLayout
}  // namespace Graphics
}  // namespace EQT
