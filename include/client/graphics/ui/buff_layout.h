// buff_layout.h -- the buff window's icon geometry, quoted from the
// official client's own uifiles/default/EQUI_BuffWindow.xml (stock
// Titanium). B1077 (GitLab #397).
//
// The window (BuffWindow) is 104x395 and holds twenty-five
// BW_BuffN_Button pieces, 34x34. Fifteen of them (Buff0..14) are laid out
// on a 3-column by 5-row grid: columns at X = 3, 48, 93 (pitch 45, a 7 px
// gap) and rows at Y = 3, 49, 95, 141, 187 (pitch 46, a 12 px gap). The
// other ten (Buff15..24) are all parked at (3,3) -- the oracle's way of
// saying "fifteen are shown". Every button carries
// <ButtonDrawTemplate><Normal>BlueIconBackground</Normal>
// <NormalDecal>BuffIcons</NormalDecal> and a DecalOffset (2,2) with
// DecalSize 30x30: the icon art is a 30x30 decal INSIDE the 34x34 button,
// the button is a BlueIconBackground frame around it.
//
// fable's buff bar is the table's single strip (ui_layout.h,
// BesidePlayerTop), not the 104x395 window, so PLACEMENT stays the table's
// (B1075 ruling, docs/decisions.md 2026-10-15); only the per-buff CONTENT
// uses these numbers: a 34x34 button carrying a 30x30 decal at its (2,2)
// corner. The bar's height (SLOT_SIZE + MARGIN*2 = 40, ui_layout.h) fits
// exactly one button with 3 px of vertical padding.

#pragma once

namespace EQT {
namespace Graphics {
namespace BuffLayout {

// BW_BuffN_Button: 34x34, a BlueIconBackground frame.
constexpr int kButtonSize = 34;

// The fifteen shown buttons: 3 columns x 5 rows.
constexpr int kCols = 3;
constexpr int kRows = 5;
constexpr int kColumnX[3] = {3, 48, 93};   // pitch 45
constexpr int kRowY[5] = {3, 49, 95, 141, 187};  // pitch 46
constexpr int kShownCount = kCols * kRows;  // 15 (Buff15..24 parked at 3,3)

// The decal: 30x30 at the button's (2,2) corner.
constexpr int kDecalOffset = 2;
constexpr int kDecalSize = 30;

}  // namespace BuffLayout
}  // namespace Graphics
}  // namespace EQT
