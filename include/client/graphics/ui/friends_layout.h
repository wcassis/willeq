/*
 * FriendsLayout -- the Friends window's geometry, and the only place it is
 * computed (B897, GitLab #213).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_FriendsWnd.xml:
 *
 *   <Screen item="FriendsWindow">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>196</X><Y>50</Y></Location>
 *     <Size><CX>168</CX><CY>288</CY></Size>
 *     <Text>Friends Window</Text>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Minimizebox>true</Style_Minimizebox>
 *     <Style_Sizable>false</Style_Sizable>
 *     <Pieces>FW_Subwindows</Pieces>
 *
 *   <TabBox item="FW_Subwindows">  RelativePosition true, (2,0) 158x264
 *     <TabBorderTemplate>FT_DefTabBorder</TabBorderTemplate>
 *     <Pages>FW_FriendsPage</Pages>   TabText "Friends"
 *     <Pages>FW_IgnorePage</Pages>    TabText "Ignored"
 *
 *   FW_FriendsPage <Pieces> (each RelativePosition true, offsets from the page):
 *     FW_ContactButton  (3,5)    70x20   Text "Contact"
 *     FW_WhoButton      (78,5)   70x20   Text "Who"
 *     FW_FriendsList    (3,30)  144x150  Listbox, one 150px column, VScroll
 *     FW_NameInput      (3,184)  90x24   Editbox
 *     FW_AddButton      (104,186) 40x20  Text "Add"
 *     FW_DeleteButton   (3,214) 144x20   Text "Delete"
 *   FW_IgnorePage <Pieces>:
 *     FW_IgnoreList        (3,30)  144x150
 *     FW_IgnoreNameInput   (3,184)  90x24
 *     FW_IgnoreAddButton   (104,186) 40x20
 *     FW_IgnoreDeleteButton (3,214) 144x20
 *   -- the Ignored page has NO Contact and NO Who button; the shipped file
 *   declares none for it, so none is drawn.
 *
 * UNSCALED, the oracle's own reason: `Style_Sizable false`, T415's condition
 * (the bank and the combat-abilities windows use it too), and 168x288 fits
 * 640x480 with room to spare.
 *
 * WHERE ON SCREEN: the oracle's own per-resolution default, quoted from
 * /mnt/projects/EverQuestP1999/defaults.ini `[FriendsWindow]`
 * (`XPos800x600=259 YPos800x600=125`, `XPos1024x768=429 YPos1024x768=170`,
 * ... -- kDefaultPositions below), and the XML's own <Location> (196,50) at
 * any resolution that file does not list (640x480 among them). Clamped so
 * the window is always wholly on screen. Unlike the bank and the Options
 * window this needs no flagged centring deviation: (196,50) clears fable's
 * player status panel, which ends at x=164.
 *
 * TWO NUMBERS ARE NOT IN THE XML, and both are DERIVED, not picked:
 *   - TITLE_H = 24 = 288 (the Screen) - 264 (the TabBox, at Y 0 of the
 *     client area). The window's own arithmetic leaves exactly 24px above
 *     the TabBox for the title bar.
 *   - TAB_H = 22: the strip FT_DefTabBorder draws. This window's pages carry
 *     no <Location>, but EQUI_OptionsWindow.xml's TabBox uses the SAME
 *     FT_DefTabBorder / FT_DefPageBorder pair and every one of its pages sits
 *     at <Location>(0,22) -- options_layout.h's TAB_H. The page below it is
 *     264 - 22 = 242 tall, and the deepest piece (FW_DeleteButton, 214+20 =
 *     234) fits inside it, which is the check that the strip is not taller.
 * The two tabs split the 158px strip evenly (79 each): a layout header has
 * no font metrics, the same deviation options_layout.h flags for its seven.
 *
 * THE LIST ROWS ARE fable's: the Listbox carries no row height (it comes from
 * the font), so ROW_H is fable's own 14 (the height its text batch draws a
 * line at) -- ten rows in the 150px box -- and the VScroll strip is
 * fable's own 12px down the box's right edge. Flagged, not absorbed.
 *
 * NOT DRAWN: `Style_Minimizebox` -- fable's static UI has no minimised state
 * for any window; the close box is drawn.
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>

#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace FriendsLayout {

// <Screen> <Size>, verbatim.
constexpr irr::s32 WINDOW_W = 168;
constexpr irr::s32 WINDOW_H = 288;
// <Screen> <Location>, verbatim -- used where defaults.ini lists nothing.
constexpr irr::s32 XML_X = 196;
constexpr irr::s32 XML_Y = 50;

// FW_Subwindows: (2,0) 158x264 in the client area.
constexpr irr::s32 TABBOX_X = 2;
constexpr irr::s32 TABBOX_Y = 0;
constexpr irr::s32 TABBOX_W = 158;
constexpr irr::s32 TABBOX_H = 264;

// Derived -- see the header comment.
constexpr irr::s32 TITLE_H = WINDOW_H - TABBOX_H;   // 24
constexpr irr::s32 TAB_H = 22;
constexpr irr::s32 PAGE_H = TABBOX_H - TAB_H;       // 242

constexpr int PAGE_COUNT = 2;

/** The TabBox's <Pages>, in XML order. */
enum class Page : uint8_t {
    Friends = 0,   // FW_FriendsPage
    Ignored = 1,   // FW_IgnorePage
};

/** Each page's own <TabText>. */
inline const char* tabText(int page) {
    return page == 0 ? "Friends" : (page == 1 ? "Ignored" : "");
}

// --- the page pieces, transcribed (identical offsets on both pages) ---
constexpr irr::s32 LIST_X = 3, LIST_Y = 30, LIST_W = 144, LIST_H = 150;
constexpr irr::s32 INPUT_X = 3, INPUT_Y = 184, INPUT_W = 90, INPUT_H = 24;
constexpr irr::s32 ADD_X = 104, ADD_Y = 186, ADD_W = 40, ADD_H = 20;
constexpr irr::s32 DELETE_X = 3, DELETE_Y = 214, DELETE_W = 144, DELETE_H = 20;
// FW_FriendsPage only.
constexpr irr::s32 CONTACT_X = 3, CONTACT_Y = 5, CONTACT_W = 70, CONTACT_H = 20;
constexpr irr::s32 WHO_X = 78, WHO_Y = 5, WHO_W = 70, WHO_H = 20;

// fable's own -- see the header comment.
constexpr irr::s32 ROW_H = 14;
constexpr int VISIBLE_ROWS = LIST_H / ROW_H;   // 10
constexpr irr::s32 SCROLL_W = 12;

/**
 * /mnt/projects/EverQuestP1999/defaults.ini [FriendsWindow], verbatim: the
 * oracle's default top-left per resolution.
 */
struct DefaultPosition {
    irr::s32 screenW, screenH, x, y;
};
constexpr DefaultPosition kDefaultPositions[] = {
    {1024, 768, 429, 170},
    {800, 600, 259, 125},
    {1280, 1024, 259, 125},
    {1600, 1200, 349, 637},
    {1152, 864, 223, 317},
    {1280, 720, 429, 170},
    {1600, 900, 429, 170},
};

/** The whole window in screen coordinates (see WHERE ON SCREEN). */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    irr::s32 x = XML_X;
    irr::s32 y = XML_Y;
    for (const auto& d : kDefaultPositions) {
        if (d.screenW == layout.screenW && d.screenH == layout.screenH) {
            x = d.x;
            y = d.y;
            break;
        }
    }
    if (x + WINDOW_W > layout.screenW) x = layout.screenW - WINDOW_W;
    if (y + WINDOW_H > layout.screenH) y = layout.screenH - WINDOW_H;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return irr::core::rect<irr::s32>(x, y, x + WINDOW_W, y + WINDOW_H);
}

/** Style_Titlebar: the row holding <Text>Friends Window</Text>. */
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

/** FW_Subwindows. */
inline irr::core::rect<irr::s32> tabBoxRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    const irr::s32 x = w.UpperLeftCorner.X + TABBOX_X;
    const irr::s32 y = w.UpperLeftCorner.Y + TITLE_H + TABBOX_Y;
    return irr::core::rect<irr::s32>(x, y, x + TABBOX_W, y + TABBOX_H);
}

/** Tab `page` (0..1) of the strip; degenerate out of range. */
inline irr::core::rect<irr::s32> tabRect(const UILayout& layout, int page) {
    if (page < 0 || page >= PAGE_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto b = tabBoxRect(layout);
    const irr::s32 x0 = b.UpperLeftCorner.X + (TABBOX_W * page) / PAGE_COUNT;
    const irr::s32 x1 = b.UpperLeftCorner.X + (TABBOX_W * (page + 1)) / PAGE_COUNT;
    return irr::core::rect<irr::s32>(x0, b.UpperLeftCorner.Y, x1,
                                     b.UpperLeftCorner.Y + TAB_H);
}

/** The page area under the strip. */
inline irr::core::rect<irr::s32> pageRect(const UILayout& layout) {
    const auto b = tabBoxRect(layout);
    return irr::core::rect<irr::s32>(b.UpperLeftCorner.X, b.UpperLeftCorner.Y + TAB_H,
                                     b.LowerRightCorner.X, b.LowerRightCorner.Y);
}

/** THE one placement body: a piece's <Location>/<Size> inside the page. */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 x, irr::s32 y,
                                    irr::s32 w, irr::s32 h) {
    const auto p = pageRect(layout);
    const irr::s32 sx = p.UpperLeftCorner.X + x;
    const irr::s32 sy = p.UpperLeftCorner.Y + y;
    return irr::core::rect<irr::s32>(sx, sy, sx + w, sy + h);
}

inline irr::core::rect<irr::s32> listRect(const UILayout& l) {
    return at(l, LIST_X, LIST_Y, LIST_W, LIST_H);
}
inline irr::core::rect<irr::s32> nameInputRect(const UILayout& l) {
    return at(l, INPUT_X, INPUT_Y, INPUT_W, INPUT_H);
}
inline irr::core::rect<irr::s32> addButtonRect(const UILayout& l) {
    return at(l, ADD_X, ADD_Y, ADD_W, ADD_H);
}
inline irr::core::rect<irr::s32> deleteButtonRect(const UILayout& l) {
    return at(l, DELETE_X, DELETE_Y, DELETE_W, DELETE_H);
}
/** FW_ContactButton -- the Friends page only. */
inline irr::core::rect<irr::s32> contactButtonRect(const UILayout& l) {
    return at(l, CONTACT_X, CONTACT_Y, CONTACT_W, CONTACT_H);
}
/** FW_WhoButton -- the Friends page only. */
inline irr::core::rect<irr::s32> whoButtonRect(const UILayout& l) {
    return at(l, WHO_X, WHO_Y, WHO_W, WHO_H);
}

/** The VScroll strip down the list's right edge (fable's width). */
inline irr::core::rect<irr::s32> scrollStripRect(const UILayout& l) {
    const auto r = listRect(l);
    return irr::core::rect<irr::s32>(r.LowerRightCorner.X - SCROLL_W, r.UpperLeftCorner.Y,
                                     r.LowerRightCorner.X, r.LowerRightCorner.Y);
}

/** Visible row `i` (0..VISIBLE_ROWS-1) of the list, left of the strip. */
inline irr::core::rect<irr::s32> rowRect(const UILayout& l, int i) {
    if (i < 0 || i >= VISIBLE_ROWS) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto r = listRect(l);
    const irr::s32 y = r.UpperLeftCorner.Y + i * ROW_H;
    return irr::core::rect<irr::s32>(r.UpperLeftCorner.X, y,
                                     r.LowerRightCorner.X - SCROLL_W, y + ROW_H);
}

}  // namespace FriendsLayout
}  // namespace Graphics
}  // namespace EQT
