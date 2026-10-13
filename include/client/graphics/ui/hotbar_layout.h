/*
 * HotbarLayout -- stock Titanium EQUI_HotButtonWnd.xml geometry.
 * Source: /mnt/projects/EverQuestP1999/uifiles/default/EQUI_HotButtonWnd.xml.
 *
 * HotButtonWnd: Location (0,230), Size 96x260, rounded/no title,
 * close box enabled. HB_PageLeftButton: (8,3) 22x12;
 * HB_CurrentPageLabel: (30,3) 25x16; HB_PageRightButton: (58,3) 22x12.
 * HB_Button1..10, HB_InvSlot1..10, and HB_SpellGem1..10 all occupy the
 * same 40x40 cells: columns x=2,46; rows y=20,64,108,152,196.
 * The spell-gem holder is a separate 40x40 atlas frame at (39,117), with
 * icon offsets (2,6). HotButtonWnd2..4 each have Location (0,0) and Size
 * 0x0; the stock UI cannot show them (B1025-H0 oracle).
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
inline Rect slotRect(const Rect& window, int index) {
    if (index < 0 || index >= kSlotCount) return {0, 0, 0, 0};
    const int x = window.UpperLeftCorner.X + kSlotFirstX +
                  (index % 2) * kSlotStepX;
    const int y = window.UpperLeftCorner.Y + kSlotFirstY +
                  (index / 2) * kSlotStepY;
    return {x, y, x + kSlotW, y + kSlotH};
}

}}} // namespace EQT::Graphics::HotbarLayout
