/*
 * TrackingLayout -- the Tracking Window's geometry, and the only place it is
 * computed (B895, GitLab #211). The T436 recipe: a header that QUOTES the
 * oracle XML, a panel TU that computes no rect, and tests that pin both.
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_TrackingWnd.xml:
 *
 *   <Screen item="TrackingWnd">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>0</X><Y>20</Y></Location>
 *     <Size><CX>150</CX><CY>460</CY></Size>
 *     <Text>Tracking Window</Text>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Minimizebox>true</Style_Minimizebox>
 *     <Style_Sizable>true</Style_Sizable>
 *
 * SIZE: the oracle's own 150x460, unscaled -- it fits 640x480. The window is
 * Style_Sizable, and every child below is AutoStretch-anchored to the
 * window's edges, so the anchor arithmetic gives the right rects for ANY
 * window height; on a screen shorter than 460 + 2 the window is clamped to
 * the screen and the bottom-anchored controls ride up with it, exactly as
 * they would when the player sizes the oracle's window down.
 *
 * THE ONE DEVIATION is the ANCHOR, flagged as T434/T436 flagged theirs: the
 * window is CENTRED rather than at the oracle's absolute (0,20), because
 * fable's top-left holds the player status, group and pet panels. Not
 * absorbed silently.
 *
 * THE TITLE BAND is fable's own TITLE_H = 18 (BankLayout's rule: the bar's
 * height comes from the font and the WDT art, not from a number in a shipped
 * file); every child rect is measured inside the client area below it.
 *
 * ANCHORS, verbatim (offsets in the client area, cw x ch). An offset whose
 * <...AnchorToTop>/<...AnchorToLeft> is false is measured from the FAR edge;
 * absent tags default to the near edge (true).
 *
 *   TRW_FiltersLabel      L3 T0 R3(far) B20            Text "Filters", centred
 *   TRW_Filter*Button     <Location>(3|25|47|69|91|113, 15) <Size>20x20
 *                         Style_Checkbox true, Text R Y W B L G with
 *                         TextColor (240,0,0) (240,240,0) (240,240,240)
 *                         (0,0,240) (0,240,240) (0,240,0)
 *   TRW_TrackingList      L0 T36 R0(far) B141(far)     one 150px column, VScroll
 *   TRW_TrackSortLabel    L3 T136(far) R3(far) B122(far)  "Track Sort:"
 *   TRW_TrackSortCombobox L3 T120(far) R3(far) B96(far)   ListHeight 160
 *   TRW_TrackPlayersLabel L3 T94(far) R3(far) B80(far)    "Track Players:"
 *   TRW_TrackPlayersCombobox L3 T78(far) R3(far) B54(far) ListHeight 160
 *   TRW_TrackButton       L3 T45(far) R3(far) B25(far)    "Track"
 *   TRW_DoneButton        L3 T22(far) R3(far) B2(far)     "Cancel" (ScreenID DoneButton)
 *
 * NOT DRAWN: Style_Minimizebox -- fable's static UI has no minimised-window
 * state for any panel (the same omission every T415/T434/T436 window makes).
 * Nothing in this file is post-Velious; the era question the issue raises
 * about the filter/sort/players controls is open for Will, and the default
 * there (keep them) is what is drawn.
 *
 * NOT THE ORACLE'S: ROW_H (14, the height fable's text batch draws a list row
 * at -- T419's ORACLE_ROW_H rule) and the open combobox list's height, which
 * is min(ListHeight 160, items * ROW_H).
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace TrackingLayout {

// <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 150;
constexpr irr::s32 WINDOW_H = 460;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;
constexpr irr::s32 ROW_H = 14;

// <Text> of each control, verbatim.
constexpr const char* TITLE_TEXT = "Tracking Window";
constexpr const char* FILTERS_TEXT = "Filters";
constexpr const char* SORT_LABEL_TEXT = "Track Sort:";
constexpr const char* PLAYERS_LABEL_TEXT = "Track Players:";
constexpr const char* TRACK_TEXT = "Track";
constexpr const char* CANCEL_TEXT = "Cancel";

// TRW_Filter*Button, in element order: Red Yellow White Blue LightBlue Green.
constexpr int FILTER_COUNT = 6;
constexpr irr::s32 FILTER_X[FILTER_COUNT] = {3, 25, 47, 69, 91, 113};
constexpr irr::s32 FILTER_Y = 15;
constexpr irr::s32 FILTER_SIZE = 20;
constexpr const char* FILTER_TEXT[FILTER_COUNT] = {"R", "Y", "W", "B", "L", "G"};
struct Rgb { unsigned char r, g, b; };
constexpr Rgb FILTER_TEXT_COLOR[FILTER_COUNT] = {
    {240, 0, 0}, {240, 240, 0}, {240, 240, 240},
    {0, 0, 240}, {0, 240, 240}, {0, 240, 0},
};

// <ListHeight> of both comboboxes.
constexpr irr::s32 COMBO_LIST_HEIGHT = 160;

/** An AutoStretch anchor set; `*Far` = that offset is from the far edge. */
struct Anchor {
    irr::s32 left;
    irr::s32 top;
    bool topFar;
    irr::s32 right;
    irr::s32 bottom;
    bool bottomFar;
};

constexpr Anchor FILTERS_LABEL = {3, 0, false, 3, 20, false};
constexpr Anchor TRACKING_LIST = {0, 36, false, 0, 141, true};
constexpr Anchor SORT_LABEL = {3, 136, true, 3, 122, true};
constexpr Anchor SORT_COMBO = {3, 120, true, 3, 96, true};
constexpr Anchor PLAYERS_LABEL = {3, 94, true, 3, 80, true};
constexpr Anchor PLAYERS_COMBO = {3, 78, true, 3, 54, true};
constexpr Anchor TRACK_BUTTON = {3, 45, true, 3, 25, true};
constexpr Anchor DONE_BUTTON = {3, 22, true, 3, 2, true};

/** The whole window: 150x460 (height clamped to the screen), CENTRED. */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    // B1075 (#395): the layout table's ScreenCentre anchor.
    const irr::s32 h = layout.screenH < WINDOW_H ? layout.screenH : WINDOW_H;
    return layout.placeScreenCentre(WINDOW_W, h);
}

/** Style_Titlebar. */
inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
                                     w.LowerRightCorner.X, w.UpperLeftCorner.Y + TITLE_H);
}

/** Style_Closebox: the square at the right-hand end of the title bar. */
inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return irr::core::rect<irr::s32>(t.LowerRightCorner.X - TITLE_H, t.UpperLeftCorner.Y,
                                     t.LowerRightCorner.X, t.LowerRightCorner.Y);
}

/** The client area -- everything below the title bar. */
inline irr::core::rect<irr::s32> clientRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, w.UpperLeftCorner.Y + TITLE_H,
                                     w.LowerRightCorner.X, w.LowerRightCorner.Y);
}

/** THE one anchor body: every anchored control goes through it. */
inline irr::core::rect<irr::s32> anchored(const UILayout& layout, const Anchor& a) {
    const auto c = clientRect(layout);
    const irr::s32 cx = c.UpperLeftCorner.X;
    const irr::s32 cy = c.UpperLeftCorner.Y;
    const irr::s32 cw = c.getWidth();
    const irr::s32 ch = c.getHeight();
    const irr::s32 top = a.topFar ? ch - a.top : a.top;
    const irr::s32 bottom = a.bottomFar ? ch - a.bottom : a.bottom;
    return irr::core::rect<irr::s32>(cx + a.left, cy + top, cx + cw - a.right, cy + bottom);
}

/** TRW_Filter<i>Button, 0..5 (R Y W B L G). Degenerate when out of range. */
inline irr::core::rect<irr::s32> filterButtonRect(const UILayout& layout, int i) {
    if (i < 0 || i >= FILTER_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto c = clientRect(layout);
    const irr::s32 x = c.UpperLeftCorner.X + FILTER_X[i];
    const irr::s32 y = c.UpperLeftCorner.Y + FILTER_Y;
    return irr::core::rect<irr::s32>(x, y, x + FILTER_SIZE, y + FILTER_SIZE);
}

inline irr::core::rect<irr::s32> filtersLabelRect(const UILayout& l) { return anchored(l, FILTERS_LABEL); }
inline irr::core::rect<irr::s32> listRect(const UILayout& l) { return anchored(l, TRACKING_LIST); }
inline irr::core::rect<irr::s32> sortLabelRect(const UILayout& l) { return anchored(l, SORT_LABEL); }
inline irr::core::rect<irr::s32> sortComboRect(const UILayout& l) { return anchored(l, SORT_COMBO); }
inline irr::core::rect<irr::s32> playersLabelRect(const UILayout& l) { return anchored(l, PLAYERS_LABEL); }
inline irr::core::rect<irr::s32> playersComboRect(const UILayout& l) { return anchored(l, PLAYERS_COMBO); }
inline irr::core::rect<irr::s32> trackButtonRect(const UILayout& l) { return anchored(l, TRACK_BUTTON); }
inline irr::core::rect<irr::s32> doneButtonRect(const UILayout& l) { return anchored(l, DONE_BUTTON); }

/** How many whole rows TRW_TrackingList shows. */
inline int visibleRows(const UILayout& layout) {
    const irr::s32 h = listRect(layout).getHeight();
    return h > 0 ? h / ROW_H : 0;
}

/** Row `visibleIndex` (0 = top of the list's viewport). */
inline irr::core::rect<irr::s32> rowRect(const UILayout& layout, int visibleIndex) {
    const auto l = listRect(layout);
    const irr::s32 y = l.UpperLeftCorner.Y + visibleIndex * ROW_H;
    return irr::core::rect<irr::s32>(l.UpperLeftCorner.X, y, l.LowerRightCorner.X, y + ROW_H);
}

/**
 * An OPEN combobox's item list, directly below its box: min(ListHeight,
 * items * ROW_H) tall, flipped above the box if it would leave the screen.
 */
inline irr::core::rect<irr::s32> comboListRect(const UILayout& layout,
                                               const irr::core::rect<irr::s32>& combo,
                                               int items) {
    irr::s32 h = items * ROW_H;
    if (h > COMBO_LIST_HEIGHT) h = COMBO_LIST_HEIGHT;
    irr::s32 y = combo.LowerRightCorner.Y;
    if (y + h > layout.screenH) y = combo.UpperLeftCorner.Y - h;
    return irr::core::rect<irr::s32>(combo.UpperLeftCorner.X, y, combo.LowerRightCorner.X, y + h);
}

}  // namespace TrackingLayout
}  // namespace Graphics
}  // namespace EQT
