// spellbook_layout.h -- the spellbook window's geometry, quoted from the
// official client's own uifiles/default/EQUI_SpellBookWnd.xml (stock
// Titanium). B1076 (GitLab #396).
//
// The window (SpellBookWnd) is 510x360, Style_Sizable false, so it is
// drawn unscaled. It opens from the CastSpellWnd book button (the 22x22
// piece at (10,252), spell_gem_layout.h) and shows the character's
// Scribed spells, 8 per page, two pages per open (16 slots:
// SBW_Spell0..7 the left page, SBW_Spell8..15 the right page).
//
// SLOT GRID -- two 2x4 grids of 44x44 Spell buttons, row-major per page
// (the XML: SBW_Spell0 (80,24), SBW_Spell1 (171,24), SBW_Spell2 (80,97)):
//   left page  columns X=80 and X=171,  right page X=292 and X=383
//   slots 0..7 walk the left grid, 8..15 the right grid, the same way
// The row Ys are IRREGULAR -- 24, 97, 169, 242 (pitches 73, 72, 73) --
// so the row table, not a constant pitch, is the geometry.
// Each button draws a 40x40 decal (BuffIcons template) at DecalOffset
// (2,2), so the spell icon fills the button with a 2 px margin.
//
// NAMES -- each button pairs with a 84x16 centred StaticText (Font 1)
// directly below it: for a slot at (x,y) the name text is at
// (x-20, y+44). The oracle pins both in the XML (e.g. SBW_Spell0 (80,24)
// -> SBW_SpellName0 (60,68)).
//
// PAGE NUMBERS -- two 20x16 centred StaticTexts at the top of each page:
// SBW_LeftPageNum (137,9), SBW_RightPageNum (350,9).
//
// PAGE ARROWS -- 18x32 buttons, one on each outer edge at Y=145 (mid
// page): SBW_PageDownButton (6,145) left / back, SBW_PageUpButton
// (487,145) right / forward.
//
// MEMORIZE GAUGE -- one 100x8 Gauge at (204,312), below the slots,
// centred under the left page. (The XML lists the piece twice; the
// window draws one gauge.)
//
// INVISIBLE PAGE-SELECT BUTTONS -- SBW_MemPage0_Button and
// SBW_MemPage1_Button are 1x1 buttons at (66,285) and (296,285): the
// oracle's whole-page hit targets (memorize a page / flip to a page).
//
// DONE BUTTON -- a 13x13 "X" at (449,5), the window's close control.
//
// BACKDROP -- the two pages are drawn from the book art (256x360 per
// half, SBW_SpellBook1..4 tiled 256x256 + 256x107): left half (0,0),
// right half (256,0); the bottom 107 px carries the gauges/controls.
//
// PLACEMENT is the layout table's (B1075 ruling): the anchor is
// UICentre::CenterScreen; the SIZE is the oracle's 510x360.

#pragma once

#include <irrlicht.h>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace SpellBookLayout {

// SpellBookWnd: 510x360, Style_Sizable false.
constexpr int kWindowW = 510;
constexpr int kWindowH = 360;

// Spell buttons: 44x44, 91 px column gap (171-80 = 383-292).
constexpr int kSlotW = 44;
constexpr int kSlotH = 44;
constexpr int kSlotColDX = 91;
constexpr int kColumnsPerPage = 2;
constexpr int kSlotRows = 4;     // rows per page (kSlotRowY has 4 entries)
constexpr int kSlotsPerPage = 8;
constexpr int kPageCount = 2;
constexpr int kSlotCountTotal = 16;  // SBW_Spell0..15

// Grid origins, window-relative. The row Ys are the XML's own (irregular).
constexpr int kLeftPageX0 = 80;    // SBW_Spell0 column
constexpr int kRightPageX0 = 292;  // SBW_Spell8 column
constexpr int kSlotRowY[4] = {24, 97, 169, 242};

// Spell icons: 40x40 decal at (2,2) inside each 44x44 button.
constexpr int kIconW = 40;
constexpr int kIconH = 40;
constexpr int kIconOffset = 2;

// Name text: 84x16 centred, (x-20, y+44) below the button.
constexpr int kNameW = 84;
constexpr int kNameH = 16;
constexpr int kNameDX = -20;
constexpr int kNameDY = 44;

// Page numbers: 20x16 centred at the top of each page.
constexpr int kPageNumW = 20;
constexpr int kPageNumH = 16;
constexpr int kLeftPageNumX = 137;
constexpr int kLeftPageNumY = 9;
constexpr int kRightPageNumX = 350;
constexpr int kRightPageNumY = 9;

// Page arrows: 18x32, mid-page (Y=145), one per outer edge.
constexpr int kArrowW = 18;
constexpr int kArrowH = 32;
constexpr int kArrowY = 145;
constexpr int kPageDownX = 6;    // SBW_PageDownButton, left edge
constexpr int kPageUpX = 487;    // SBW_PageUpButton, right edge

// Memorize gauge: 100x8 below the slots.
constexpr int kGaugeW = 100;
constexpr int kGaugeH = 8;
constexpr int kGaugeX = 204;
constexpr int kGaugeY = 312;

// Scribe gauge: the XML's SBW_Scribe_Gauge -- the SAME 100x8 piece at the
// same (204,312) place (the oracle lists the gauge twice; the window draws
// one, whichever action is in flight), FillTint (176,64,0) against the
// memorize tint (200,0,200).
constexpr int kScribeGaugeX = 204;
constexpr int kScribeGaugeY = 312;

// Invisible whole-page hit targets: 1x1.
constexpr int kMemPage0X = 66;
constexpr int kMemPage1X = 296;
constexpr int kMemPageY = 285;

// Done ("X") close button: 13x13.
constexpr int kDoneW = 13;
constexpr int kDoneH = 13;
constexpr int kDoneX = 449;
constexpr int kDoneY = 5;

// Half-page art tiles: 256 wide, top 256 px pages + bottom 107 px strip.
constexpr int kHalfPageW = 256;
constexpr int kBackdropStripH = 107;  // the XML's bottom control strips

// --- Window-relative rects ---------------------------------------------

// Which page a slot index (0..15) lives on.
inline bool slotIsLeftPage(int slot) {
    return slot < kSlotsPerPage;
}

// Slot ordering is row-major per page (the XML's own geometry:
// SBW_Spell0 (80,24) and SBW_Spell1 (171,24) are the first row, left
// then right; SBW_Spell2 (80,97) starts the second).

// The rect of slot `slot` (0..15) inside the window.
inline irr::core::rect<irr::s32> slotRect(int slot) {
    const int pageSlot = slotIsLeftPage(slot) ? slot : slot - kSlotsPerPage;
    const irr::s32 col = pageSlot % kColumnsPerPage;
    const irr::s32 row = pageSlot / kColumnsPerPage;
    const irr::s32 x = (slotIsLeftPage(slot) ? kLeftPageX0 : kRightPageX0) +
                       col * kSlotColDX;
    const irr::s32 y = kSlotRowY[row];
    return irr::core::rect<irr::s32>(x, y, x + kSlotW, y + kSlotH);
}


// The 40x40 icon rect of a slot (DecalOffset 2,2).
inline irr::core::rect<irr::s32> slotIconRect(int slot) {
    const irr::core::rect<irr::s32> r = slotRect(slot);
    return irr::core::rect<irr::s32>(r.UpperLeftCorner.X + kIconOffset,
                                     r.UpperLeftCorner.Y + kIconOffset,
                                     r.UpperLeftCorner.X + kIconOffset + kIconW,
                                     r.UpperLeftCorner.Y + kIconOffset + kIconH);
}

// The name-text rect of a slot (centred under its button).
inline irr::core::rect<irr::s32> slotNameRect(int slot) {
    const irr::core::rect<irr::s32> r = slotRect(slot);
    const irr::s32 x = r.UpperLeftCorner.X + kNameDX;
    const irr::s32 y = r.UpperLeftCorner.Y + kNameDY;
    return irr::core::rect<irr::s32>(x, y, x + kNameW, y + kNameH);
}

inline irr::core::rect<irr::s32> leftPageNumRect() {
    return irr::core::rect<irr::s32>(kLeftPageNumX, kLeftPageNumY,
                                     kLeftPageNumX + kPageNumW,
                                     kLeftPageNumY + kPageNumH);
}

inline irr::core::rect<irr::s32> rightPageNumRect() {
    return irr::core::rect<irr::s32>(kRightPageNumX, kRightPageNumY,
                                     kRightPageNumX + kPageNumW,
                                     kRightPageNumY + kPageNumH);
}

inline irr::core::rect<irr::s32> pageDownRect() {
    return irr::core::rect<irr::s32>(kPageDownX, kArrowY,
                                     kPageDownX + kArrowW, kArrowY + kArrowH);
}

inline irr::core::rect<irr::s32> pageUpRect() {
    return irr::core::rect<irr::s32>(kPageUpX, kArrowY,
                                     kPageUpX + kArrowW, kArrowY + kArrowH);
}

inline irr::core::rect<irr::s32> memorizeGaugeRect() {
    return irr::core::rect<irr::s32>(kGaugeX, kGaugeY, kGaugeX + kGaugeW,
                                     kGaugeY + kGaugeH);
}

inline irr::core::rect<irr::s32> doneRect() {
    return irr::core::rect<irr::s32>(kDoneX, kDoneY, kDoneX + kDoneW,
                                     kDoneY + kDoneH);
}

// The backdrop book art: the XML tiles FOUR StaticAnimations, not two
// half-pages -- SBW_SpellBook1 (0,0) and 2 (256,0) are 256x256 page
// halves, SBW_SpellBook3 (0,256) and 4 (256,256) are 256x107 control
// strips; together they fill 512x363, and the window's 510x360 clips the
// right 2 px and the bottom 3 px.
inline irr::core::rect<irr::s32> backdropTopPageRect() {
    return irr::core::rect<irr::s32>(0, 0, kHalfPageW, kHalfPageW);
}

inline irr::core::rect<irr::s32> backdropRightTopRect() {
    return irr::core::rect<irr::s32>(kHalfPageW, 0, kHalfPageW * 2,
                                     kHalfPageW);
}

inline irr::core::rect<irr::s32> backdropBottomStripRect() {
    return irr::core::rect<irr::s32>(0, kHalfPageW, kHalfPageW,
                                     kHalfPageW + kBackdropStripH);
}

inline irr::core::rect<irr::s32> backdropRightBottomRect() {
    return irr::core::rect<irr::s32>(kHalfPageW, kHalfPageW, kHalfPageW * 2,
                                     kHalfPageW + kBackdropStripH);
}

}  // namespace SpellBookLayout
}  // namespace Graphics
}  // namespace EQT
