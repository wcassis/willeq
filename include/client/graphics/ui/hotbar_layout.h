/*
 * HotbarLayout -- stock Titanium EQUI_HotButtonWnd.xml geometry.
 * Source: /mnt/projects/EverQuestP1999/uifiles/default/EQUI_HotButtonWnd.xml.
 *
 * HotButtonWnd: Location (0,230), Size 96x260, rounded/no title,
 * HB_PageLeftButton: (8,3) 22x12;
 * HB_CurrentPageLabel: (30,3) 25x16; HB_PageRightButton: (58,3) 22x12.
 * HB_Button1..10, HB_InvSlot1..10, and HB_SpellGem1..10 all occupy the
 * same 40x40 cells: columns x=2,46; rows y=20,64,108,152,196.
 * The spell-gem holder is a separate 40x40 atlas frame at (39,117), with
 * icon offsets (2,6). HotButtonWnd2..4 each have Location (0,0) and Size
 * 0x0; the stock UI cannot show them (B1025-H0 oracle), so the model is
 * ONE window x 10 pages x 10 buttons and there is no geometry for bars 2-4.
 *
 * B1025-H2: the oracle's <Location> (0,230) is RelativePosition false, i.e.
 * screen pixels, but it is the 640x480 default only. defaults.ini's
 * [HotButtonWnd] XPos/YPos table (the stock client's own positions) is
 * bottom-anchored: every one of its eight resolutions puts the window's top at
 * YPos = screenHeight - 30 - 260 (800x600: 310; 1024x768: 478; 1280x720: 430;
 * 1600x900: 610; 1600x1200: 910; ...), i.e. a 30px gap from the bottom edge,
 * so windowRect() uses that rule and the X keeps its measured 0. The gem bar
 * must fit entirely above the window (test_ninth_spell_gem pins the eight 32px
 * rows to end at layout.hotbar.UpperLeftCorner.Y): with a 284px bar that holds
 * at 800x600 (340px headroom) and 1280x720 (460px), which is all the test
 * checks.
 *
 * B1075 (#395, Will 2026-10-15 ruling item 3): the ARRANGEMENT of the ten
 * buttons is layout, not content. `columns` is a table value
 * (ui_layout_table.h, kHotbarOrientations): 2 is the oracle's 2x5 window
 * above, 10 is the 1x10 bar the layout table places bottom-centre. Both are
 * the SAME arithmetic -- the oracle's first-cell offset, 44px step, 40px cell
 * and its own right (10px) and bottom (24px) padding -- so windowSize(2) is the
 * oracle's 96x260 exactly and no second code path exists. The windowRect()
 * overloads with no `columns` keep the oracle's 2x5 for the XML test.
 */

#pragma once

#include <irrlicht.h>

namespace EQT { namespace Graphics { namespace HotbarLayout {

using Rect = irr::core::rect<irr::s32>;

constexpr int kWindowX = 0;
constexpr int kWindowY = 230;
constexpr int kWindowW = 96;
constexpr int kWindowH = 260;
constexpr int kSlotCount = 10;
constexpr int kSlotW = 40;
constexpr int kSlotH = 40;
constexpr int kSlotFirstX = 2;
constexpr int kSlotFirstY = 20;
constexpr int kSlotStepX = 44;
constexpr int kSlotStepY = 44;
constexpr int kSpellIconOffsetX = 2;
constexpr int kSpellIconOffsetY = 6;
// B1102 (Will 2026-10-20): despite Style_Closebox true on the oracle's
// title-less template, this hotbar must have no close box. Keep the existing
// 12px strip height for the page row's 12px-tall left/right buttons.
constexpr int kPageRowH = 12;

// defaults.ini's [HotButtonWnd] XPos/YPos rule: the window's top sits at
// screenHeight - 30 - 260 (the stock client's bottom-anchored gap), X at the
// measured 0, clamped to stay on screen.
// The oracle window's padding past the last cell, derived from its own
// 96x260: 96 - (46 + 40) = 10 on the right, 260 - (196 + 40) = 24 below.
constexpr int kPadRight = kWindowW - (kSlotFirstX + kSlotStepX + kSlotW);
constexpr int kPadBottom = kWindowH - (kSlotFirstY + 4 * kSlotStepY + kSlotH);
constexpr int kStockColumns = 2;

constexpr int rowsFor(int columns) {
    return columns <= 0 ? kSlotCount : (kSlotCount + columns - 1) / columns;
}
constexpr int windowW(int columns) {
    return kSlotFirstX + (columns - 1) * kSlotStepX + kSlotW + kPadRight;
}
constexpr int windowH(int columns) {
    return kSlotFirstY + (rowsFor(columns) - 1) * kSlotStepY + kSlotH + kPadBottom;
}
static_assert(windowW(kStockColumns) == kWindowW && windowH(kStockColumns) == kWindowH,
              "the 2x5 arrangement must reproduce the oracle's 96x260");

inline Rect windowRect(irr::s32 screenW, irr::s32 screenH) {
    irr::s32 x = kWindowX;
    irr::s32 y = screenH - 30 - kWindowH;
    if (x + kWindowW > screenW) x = screenW - kWindowW;
    if (y + kWindowH > screenH) y = screenH - kWindowH;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return {x, y, x + kWindowW, y + kWindowH};
}

// The 640x480 oracle position, for the XML rect test.
inline Rect windowRect() {
    return {kWindowX, kWindowY, kWindowX + kWindowW, kWindowY + kWindowH};
}

inline Rect pageLeftRect(const Rect& window) {
    return {window.UpperLeftCorner.X + 8, window.UpperLeftCorner.Y + 3,
            window.UpperLeftCorner.X + 30, window.UpperLeftCorner.Y + 15};
}

inline Rect pageLabelRect(const Rect& window) {
    return {window.UpperLeftCorner.X + 30, window.UpperLeftCorner.Y + 3,
            window.UpperLeftCorner.X + 55, window.UpperLeftCorner.Y + 19};
}

inline Rect pageRightRect(const Rect& window) {
    return {window.UpperLeftCorner.X + 58, window.UpperLeftCorner.Y + 3,
            window.UpperLeftCorner.X + 80, window.UpperLeftCorner.Y + 15};
}

// The Button, InvSlot and SpellGem pieces for one index share this rect.
// Row-major in `columns` columns: 2 is the oracle's 2x5 (HB_Button1 and 2
// share the first row), 10 is the 1x10 bar.
inline Rect slotRect(const Rect& window, int index, int columns = kStockColumns) {
    if (index < 0 || index >= kSlotCount || columns <= 0) return {0, 0, 0, 0};
    const int x = window.UpperLeftCorner.X + kSlotFirstX +
                  (index % columns) * kSlotStepX;
    const int y = window.UpperLeftCorner.Y + kSlotFirstY +
                  (index / columns) * kSlotStepY;
    return {x, y, x + kSlotW, y + kSlotH};
}

}}} // namespace EQT::Graphics::HotbarLayout
