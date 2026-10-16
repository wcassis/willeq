// spell_gem_layout.h -- the gem bar window's geometry, quoted from the
// official client's own uifiles/default/EQUI_CastSpellWnd.xml (stock
// Titanium). B1077 (GitLab #397).
//
// The window (CastSpellWnd) is 48x290, Style_Sizable false, so it is drawn
// unscaled. The eight on-screen gems (CSPW_Spell0..7) are 36x28 SpellGem
// pieces at X=3, Y=10+30i (30 px pitch, 2 px vertical gap). CSPW_Spell8 is
// parked at (-5000,-5000) -- the oracle's way of saying "nine gems exist in
// the data, eight are shown" (T483). The spellbook toggle button
// (CSPW_SpellBook) is 22x22 at (10,252).
//
// The gem art is the A_SpellGemHolder piece (window_pieces02.tga (208,216)
// 39x32, spell_icon_atlas.h) drawn behind the gem icon (the Gems family of
// the spell-icon atlas). The holder is 39x32; the gem cell is 36x28, so the
// holder overflows the cell by 3 px right and 4 px bottom. The oracle draws
// the holder at the cell's origin (no centering), so the overflow clips
// against the window border. fable draws the holder at the cell's origin
// too, in a 40x40 atlas cell, minified into the caller's dest rect.
//
// PLACEMENT is the layout table's (B1075 ruling): the anchor is
// UIAnchor::RightEdgeAboveHotbar; the SIZE is the oracle's 48x290.

#pragma once

#include <irrlicht.h>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace SpellGemLayout {

// CastSpellWnd: 48x290, Style_Sizable false.
constexpr int kWindowW = 48;
constexpr int kWindowH = 290;

// CSPW_Spell0..7: 36x28 at (3, 10+30i).
constexpr int kGemW = 36;
constexpr int kGemH = 28;
constexpr int kGemX = 3;
constexpr int kGemFirstY = 10;
constexpr int kGemPitchY = 30;   // 28 + 2 px gap
constexpr int kGemCount = 8;     // CSPW_Spell0..7; Spell8 is at -5000

// CSPW_SpellBook: 22x22 at (10, 252).
constexpr int kBookW = 22;
constexpr int kBookH = 22;
constexpr int kBookX = 10;
constexpr int kBookY = 252;

// The rect of gem `gemIndex` (0..7) inside the window.
inline irr::core::rect<irr::s32> gemRect(int gemIndex) {
    const irr::s32 x = kGemX;
    const irr::s32 y = kGemFirstY + gemIndex * kGemPitchY;
    return irr::core::rect<irr::s32>(x, y, x + kGemW, y + kGemH);
}

// The spellbook toggle button, inside the window.
inline irr::core::rect<irr::s32> bookRect() {
    return irr::core::rect<irr::s32>(kBookX, kBookY,
                                      kBookX + kBookW, kBookY + kBookH);
}

}  // namespace SpellGemLayout
}  // namespace Graphics
}  // namespace EQT
