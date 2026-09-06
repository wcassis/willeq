/*
 * ItemDisplayLayout -- the item display window's geometry, and the only
 * place it is computed (T421, board B298 item half).
 *
 * THE PLACEMENT IS THE ORACLE'S. Source:
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_ItemDisplay.xml
 *
 *   <Screen item="ItemDisplayWindow">
 *       <RelativePosition>false</RelativePosition>
 *       <Location><X>0</X><Y>80</Y></Location>
 *       <Size><CX>400</CX><CY>190</CY></Size>
 *       <Style_Titlebar>true</Style_Titlebar>
 *       <Style_Closebox>true</Style_Closebox>
 *       <Pieces>IDW_ItemDescription</Pieces>
 *       <Pieces>IDW_IconButton</Pieces>
 *
 * `RelativePosition false` means the Location is SCREEN coordinates, not a
 * fraction of the parent -- this window is not stacked in fable's centre
 * popup and is not scaled into it. It is drawn at the oracle's own (0,80)
 * at the oracle's own 400x190, which fits inside 640x480, the smallest
 * resolution fable supports. That is why there is no `scaled()` here and no
 * ORACLE_WINDOW_W/H pair: unlike TradeskillLayout, nothing needs mapping.
 *
 * The two children are anchor-positioned inside the window.
 * `AnchorToLeft/ToTop == false` means the offset is measured from the
 * parent's RIGHT / BOTTOM edge, the same rule TradeskillLayout writes out.
 *
 *   <Button item="IDW_IconButton">
 *       LeftAnchorOffset   70, LeftAnchorToLeft   false -> x0 = 400-70 = 330
 *       TopAnchorOffset    12, TopAnchorToTop     true  -> y0 = 12
 *       RightAnchorOffset  26, RightAnchorToLeft  false -> x1 = 400-26 = 374
 *       BottomAnchorOffset 56, BottomAnchorToTop  true  -> y1 = 56
 *     => (330,12)-(374,56), i.e. 44 x 44
 *       <DecalOffset><X>2</X><Y>2</Y></DecalOffset>
 *       <DecalSize><CX>40</CX><CY>40</CY></DecalSize>
 *     => the icon itself is (332,14) 40x40
 *
 *   <STMLbox item="IDW_ItemDescription">
 *       0 / 0 / 0(from right) / 3(from bottom)
 *     => (0,0)-(400,187), the whole client area less three pixels at the foot
 *
 * THE ONE NUMBER THAT IS NOT THE ORACLE'S is the TITLE BAR HEIGHT. The XML
 * says `Style_Titlebar true` and `Style_Closebox true` and nothing more --
 * the bar's height comes from the window font and the `WDT_Filigree` draw
 * template's art, neither of which is a number in a shipped file. TITLE_H
 * below is FABLE'S choice (18, the height its own text batch draws a title
 * row at), flagged here rather than dressed up as a quotation. Every child
 * rect is measured from the bottom of that bar, which is what the official
 * client's client-area inset does.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace ItemDisplayLayout {

// <Location> and <Size>, verbatim.
constexpr irr::s32 WINDOW_X = 0;
constexpr irr::s32 WINDOW_Y = 80;
constexpr irr::s32 WINDOW_W = 400;
constexpr irr::s32 WINDOW_H = 190;

// NOT the oracle's -- see the header comment.
constexpr irr::s32 TITLE_H = 18;

// IDW_IconButton, resolved above.
constexpr irr::s32 ICON_BTN_X = 330;
constexpr irr::s32 ICON_BTN_Y = 12;
constexpr irr::s32 ICON_BTN_W = 44;
constexpr irr::s32 ICON_BTN_H = 44;
constexpr irr::s32 DECAL_OFFSET = 2;
constexpr irr::s32 DECAL_SIZE   = 40;

// IDW_ItemDescription: the client area less 3px at the bottom.
constexpr irr::s32 DESC_BOTTOM_INSET = 3;

// Fable's own: how tall a text line is in the description box, and the left
// inset the text sits at inside the sunken field. Neither is in the XML.
constexpr irr::s32 LINE_H = 12;
constexpr irr::s32 TEXT_INSET = 3;

/** The whole window, in screen coordinates. */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    // Clamped to the screen so a resolution narrower than the oracle's own
    // window still draws it whole rather than off the right edge.
    const irr::s32 x = (layout.screenW >= WINDOW_W)
        ? WINDOW_X : layout.screenW - WINDOW_W;
    return {x, WINDOW_Y, x + WINDOW_W, WINDOW_Y + WINDOW_H};
}

/** The title bar: the row Style_Titlebar draws, holding the item's name. */
inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
            w.LowerRightCorner.X, w.UpperLeftCorner.Y + TITLE_H};
}

/** Style_Closebox: the square at the right-hand end of the title bar. */
inline irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto t = titleRect(layout);
    return {t.LowerRightCorner.X - TITLE_H, t.UpperLeftCorner.Y,
            t.LowerRightCorner.X, t.LowerRightCorner.Y};
}

/** The client area -- everything below the title bar. */
inline irr::core::rect<irr::s32> clientRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return {w.UpperLeftCorner.X, w.UpperLeftCorner.Y + TITLE_H,
            w.LowerRightCorner.X, w.LowerRightCorner.Y};
}

/** IDW_IconButton's 44x44 button. */
inline irr::core::rect<irr::s32> iconButtonRect(const UILayout& layout) {
    const auto c = clientRect(layout);
    const irr::s32 x = c.UpperLeftCorner.X + ICON_BTN_X;
    const irr::s32 y = c.UpperLeftCorner.Y + ICON_BTN_Y;
    return {x, y, x + ICON_BTN_W, y + ICON_BTN_H};
}

/** The 40x40 decal inside it, at DecalOffset. */
inline irr::core::rect<irr::s32> iconDecalRect(const UILayout& layout) {
    const auto b = iconButtonRect(layout);
    const irr::s32 x = b.UpperLeftCorner.X + DECAL_OFFSET;
    const irr::s32 y = b.UpperLeftCorner.Y + DECAL_OFFSET;
    return {x, y, x + DECAL_SIZE, y + DECAL_SIZE};
}

/** IDW_ItemDescription, the scrolling free-text box. */
inline irr::core::rect<irr::s32> descriptionRect(const UILayout& layout) {
    const auto c = clientRect(layout);
    return {c.UpperLeftCorner.X, c.UpperLeftCorner.Y,
            c.LowerRightCorner.X, c.LowerRightCorner.Y - DESC_BOTTOM_INSET};
}

/**
 * Row `i` of the description text. The twin of the render loop's row
 * arithmetic and of the visible-row count below, so what is drawn and what
 * is counted cannot disagree (T277's rule).
 */
inline irr::core::rect<irr::s32> descriptionLineRect(const UILayout& layout, int i) {
    const auto d = descriptionRect(layout);
    const irr::s32 y = d.UpperLeftCorner.Y + TEXT_INSET + i * LINE_H;
    return {d.UpperLeftCorner.X + TEXT_INSET, y,
            d.LowerRightCorner.X - TEXT_INSET, y + LINE_H};
}

/** How many description lines fit. `Style_VScroll true` is why the rest scroll. */
inline int visibleDescriptionLines(const UILayout& layout) {
    const auto d = descriptionRect(layout);
    const int usable = d.getHeight() - TEXT_INSET * 2;
    return usable > 0 ? usable / LINE_H : 0;
}

} // namespace ItemDisplayLayout
} // namespace Graphics
} // namespace EQT
