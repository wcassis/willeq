/*
 * InventoryLayout — the single shared source of inventory-window slot
 * geometry.
 *
 * B1075 (#395) split the old 400x350 `centerPopup` into the pair: this
 * window takes the RIGHT slot (`UILayout::inventoryPopup`), the window it is
 * used with (merchant/loot/bank/trade/give) the LEFT slot (`partnerPopup`).
 *
 * B1078 (#398) grew the frame to the oracle's own size and made its scale a
 * ONE uniform factor. The window is the oracle's 428x460
 * (`<Screen item="InventoryWindow"><Size>CX=428 CY=460`) at a single scale
 * that fits it into the middle band (see windowRect/scalePct), anchored in
 * its pair slot. Every interior rect — the 30 slots, the paperdoll, the
 * four coin rows, the destroy button — is expressed in the 428x460 window
 * space and mapped by THAT SAME factor, so the frame and its contents can
 * never drift apart and nothing is stretched anisotropically (the pre-B1078
 * header mapped x by windowW/428 and y by windowH/460, squashing the oracle
 * doll 85x168 into 80x161).
 *
 * The 30-slot BODY ARRANGEMENT is fable's T57 layout (Will's choice,
 * 2026-08-11): two 8-slot equipment columns flank the paperdoll, a 6-slot
 * weapon row, then the 8-slot general row. The era's own `EQUI_Inventory.xml`
 * carries NO slot geometry (empty), so the arrangement is fable's and only
 * the WINDOW size is the oracle's — see the header of the T57/T60 plans.
 *
 * T57: static_panels.cpp's renderInventoryPopup() and
 * static_ui_input.cpp's processInput() used to independently compute these
 * rectangles and drifted apart identically wrong. Both now call the
 * functions below; neither file may contain slot-position arithmetic of its
 * own.
 */

#pragma once

#include <irrlicht.h>
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/inventory_constants.h"

namespace EQT {
namespace Graphics {
namespace InventoryLayout {

// Global inventory slot index space used throughout the static UI:
//   0-21  -> equipment slot (index == eqt::inventory::EquipmentSlot id)
//   22-29 -> general inventory slot (index - EQUIP_COUNT)
constexpr int EQUIP_COUNT = 22;
constexpr int GENERAL_COUNT = 8;
constexpr int TOTAL_SLOTS = EQUIP_COUNT + GENERAL_COUNT;  // 30

namespace detail {

// --- The window (B1078): the oracle's own size. ---------------------------
//   <Screen item="InventoryWindow">  Size CX=428 CY=460
constexpr irr::s32 ORACLE_WINDOW_W = 428;
constexpr irr::s32 ORACLE_WINDOW_H = 460;

// --- The 30-slot T57 body, in the 428x460 window space. --------------------
// The slot size and the margins are fable's T57 choices (32px slots), kept
// as-is; only the window they live in grew. 428 is 28px wider than the old
// 400, so the centred rows gain a little margin on each side.
constexpr irr::s32 SLOT = UILayout::SLOT_SIZE;  // 32
constexpr irr::s32 PAD = 2;
constexpr irr::s32 CONTENT_TOP = 20;      // below the title
constexpr int COL_ROWS = 8;               // slots per side column
constexpr irr::s32 GAP_COL_WEAPON = 2;    // columns bottom -> weapon row
constexpr irr::s32 GAP_WEAPON_GENERAL = 6;  // weapon row -> general row
constexpr irr::s32 LEFT_COL_X = 8;
constexpr int WEAPON_COUNT = 6;
constexpr int GEN_COUNT = 8;

// The right equipment column's right edge in window space — the destroy
// button right-aligns to it.
constexpr irr::s32 RIGHT_COL_RIGHT = ORACLE_WINDOW_W - LEFT_COL_X;  // 420

using eqt::inventory::EquipmentSlot;

// Left column, top to bottom.
constexpr int16_t kLeftColumn[COL_ROWS] = {
    EquipmentSlot::SLOT_EAR1, EquipmentSlot::SLOT_HEAD,
    EquipmentSlot::SLOT_FACE, EquipmentSlot::SLOT_EAR2,
    EquipmentSlot::SLOT_NECK, EquipmentSlot::SLOT_SHOULDERS,
    EquipmentSlot::SLOT_ARMS, EquipmentSlot::SLOT_BACK,
};

// Right column, top to bottom.
constexpr int16_t kRightColumn[COL_ROWS] = {
    EquipmentSlot::SLOT_WRIST1, EquipmentSlot::SLOT_WRIST2,
    EquipmentSlot::SLOT_RANGE,  EquipmentSlot::SLOT_HANDS,
    EquipmentSlot::SLOT_RING1,  EquipmentSlot::SLOT_RING2,
    EquipmentSlot::SLOT_CHEST,  EquipmentSlot::SLOT_LEGS,
};

// Bottom weapon row, left to right.
constexpr int16_t kWeaponRow[WEAPON_COUNT] = {
    EquipmentSlot::SLOT_PRIMARY, EquipmentSlot::SLOT_SECONDARY,
    EquipmentSlot::SLOT_FEET,    EquipmentSlot::SLOT_WAIST,
    EquipmentSlot::SLOT_AMMO,    EquipmentSlot::SLOT_CHARM,
};

// --- The purse (T393, board B201) -------------------------------------
// THE PLACEMENT IS THE ORACLE'S, NOT OURS. Every number is quoted verbatim
// from the official Titanium/P1999 default UI,
// /mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml:
//   <Button item="IW_Money0..3">  Location X=303 Y={121,147,171,197}
//                                 Size CX=70 CY=24  DecalOffset 1,3  DecalSize 18
// so the oracle draws the four denominations as a single column of 70x24
// boxes, top to bottom platinum/gold/silver/copper, each with a coin decal
// on its left and the count as its text. Mapped into the window by the ONE
// factor (B1078), exactly as the doll is.
constexpr irr::s32 ORACLE_COIN_X = 303;
constexpr irr::s32 ORACLE_COIN_W = 70;
constexpr irr::s32 ORACLE_COIN_H = 24;
constexpr irr::s32 ORACLE_COIN_Y[4] = {121, 147, 171, 197};
constexpr irr::s32 ORACLE_DECAL_X = 1;
constexpr irr::s32 ORACLE_DECAL_Y = 3;
constexpr irr::s32 ORACLE_DECAL_SIZE = 18;

// --- The character view (T409, board B219) ----------------------------
// Same file, same window:  <Screen item="IW_CharacterView">
//   Location X=166 Y=57  Size CX=85 CY=168  -- a TALL box at 20% of the
// window width, which is what leaves its coin column at X=303 clear of it.
// Mapped into the window by the ONE factor (B1078), so it keeps the oracle's
// 85x168 aspect at every resolution.
constexpr irr::s32 ORACLE_DOLL_X = 166;
constexpr irr::s32 ORACLE_DOLL_Y = 57;
constexpr irr::s32 ORACLE_DOLL_W = 85;
constexpr irr::s32 ORACLE_DOLL_H = 168;

// The doll is rendered to an offscreen target and blitted into
// paperdollRect(). Supersample factor: the target is this many times the
// drawn rect on each axis, so the blit only ever downscales.
constexpr irr::s32 PAPERDOLL_RT_SUPERSAMPLE = 2;

// The destroy button's oracle size (IW_Destroy, 50x20) -- its PLACE is
// fable's, see destroyButtonRect().
constexpr irr::s32 ORACLE_DESTROY_W = 50;
constexpr irr::s32 ORACLE_DESTROY_H = 20;

} // namespace detail

// Denomination order is the oracle's element order, IW_Money0..3.
constexpr int COIN_COUNT = 4;

enum Coin { COIN_PLATINUM = 0, COIN_GOLD = 1, COIN_SILVER = 2, COIN_COPPER = 3 };

/**
 * B1078 (#398): the ONE uniform factor in per-cent (100 = unscaled) that
 * fits the oracle's 428x460 window into the space its pair slot leaves it --
 * the min of the available width (from its anchor to the band's right edge)
 * and the band's height ratios, capped at 1. At 1024x768 and up it is 1 and
 * every rect is the XML's own number to the pixel; at 800x600 the band is
 * only 384 tall and the slot's width is only 360, so the whole window -- its
 * 30 slots and the doll -- shrinks uniformly. One factor, never two: the
 * pre-B1078 header mapped x by windowW/428 and y by windowH/460, squashing
 * the 85x168 oracle doll into 80x161 and the 70x24 coin boxes into 58x23 --
 * the anisotropy this ruling kills.
 */
inline irr::s32 scalePct(const UILayout& layout) {
    const auto& b = layout.middleBand;
    const irr::s32 availW =
        b.LowerRightCorner.X - layout.inventoryPopup.UpperLeftCorner.X;
    irr::s32 f = std::min(availW * 100 / detail::ORACLE_WINDOW_W,
                          b.getHeight() * 100 / detail::ORACLE_WINDOW_H);
    return f < 100 ? f : 100;
}

/**
 * The window, in screen coordinates (B1078): the oracle's own 428x460 at the
 * ONE uniform factor (`scalePct`), anchored in the pair's RIGHT slot
 * (`inventoryPopup.UpperLeftCorner.X`) and top-anchored at the middle band's
 * top, clamped wholly on screen. The merchant takes the pair's LEFT slot
 * (`partnerPopup`), so the two are neighbours. Every interior rect below is
 * expressed against THIS rect (via `at`), so the frame and its contents can
 * never drift apart (T57's rule).
 */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    const irr::s32 f = scalePct(layout);
    const irr::s32 w = detail::ORACLE_WINDOW_W * f / 100;
    const irr::s32 h = detail::ORACLE_WINDOW_H * f / 100;
    const irr::s32 x = layout.inventoryPopup.UpperLeftCorner.X;
    const irr::s32 y = layout.middleBand.UpperLeftCorner.Y;
    return layout.clampOnScreen(x, y, w, h);
}

/**
 * Map a 428x460 window-space rect to screen by the ONE uniform factor,
 * offset by `windowRect()`'s origin. The single scaling body -- every interior
 * rect below goes through it, so there is no second copy of the mapping to
 * drift (T57's rule).
 */
inline irr::core::rect<irr::s32> at(const UILayout& layout, irr::s32 ox,
                                    irr::s32 oy, irr::s32 ow, irr::s32 oh) {
    const auto win = windowRect(layout);
    const irr::s32 f = scalePct(layout);
    const irr::s32 x = win.UpperLeftCorner.X + ox * f / 100;
    const irr::s32 y = win.UpperLeftCorner.Y + oy * f / 100;
    return irr::core::rect<irr::s32>(x, y, x + ow * f / 100, y + oh * f / 100);
}

/**
 * Returns the screen-space rect for a global inventory slot index
 * (0-21 equipment, 22-29 general), positioned within `windowRect()`.
 * Returns a degenerate (all-zero) rect for an out-of-range index.
 *
 * This is the ONLY place inventory slot geometry is computed --
 * renderInventoryPopup() and StaticUIInput::processInput() both call it.
 */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int globalIndex) {
    using namespace detail;
    const irr::s32 rightColX = ORACLE_WINDOW_W - LEFT_COL_X - SLOT;
    constexpr irr::s32 weaponRowW = WEAPON_COUNT * SLOT + (WEAPON_COUNT - 1) * PAD;
    const irr::s32 weaponRowX = (ORACLE_WINDOW_W - weaponRowW) / 2;
    constexpr irr::s32 generalRowW = GEN_COUNT * SLOT + (GEN_COUNT - 1) * PAD;
    const irr::s32 generalRowX = (ORACLE_WINDOW_W - generalRowW) / 2;
    const irr::s32 weaponY = CONTENT_TOP + COL_ROWS * SLOT + GAP_COL_WEAPON;
    const irr::s32 generalY = weaponY + SLOT + GAP_WEAPON_GENERAL;

    irr::s32 ox = 0, oy = 0;
    bool found = false;

    if (globalIndex >= 0 && globalIndex < EQUIP_COUNT) {
        for (int row = 0; row < COL_ROWS && !found; ++row) {
            if (kLeftColumn[row] == globalIndex) {
                ox = LEFT_COL_X;
                oy = CONTENT_TOP + row * SLOT;
                found = true;
            } else if (kRightColumn[row] == globalIndex) {
                ox = rightColX;
                oy = CONTENT_TOP + row * SLOT;
                found = true;
            }
        }
        for (int col = 0; col < WEAPON_COUNT && !found; ++col) {
            if (kWeaponRow[col] == globalIndex) {
                ox = weaponRowX + col * (SLOT + PAD);
                oy = weaponY;
                found = true;
            }
        }
    } else if (globalIndex >= EQUIP_COUNT && globalIndex < TOTAL_SLOTS) {
        int i = globalIndex - EQUIP_COUNT;
        ox = generalRowX + i * (SLOT + PAD);
        oy = generalY;
        found = true;
    }

    if (!found) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return at(layout, ox, oy, SLOT, SLOT);
}

/**
 * Returns the screen-space rect for the character paperdoll (T60), placed
 * where the OFFICIAL client puts its character view (T409, board B219) -- the
 * `IW_CharacterView` rect (166,57 85x168) mapped into `windowRect()` by the
 * ONE factor, so it keeps the oracle's 85x168 aspect at every resolution. It
 * lies between the two equipment columns and above the weapon row, so the
 * T60 containment/no-overlap invariants hold as before.
 */
inline irr::core::rect<irr::s32> paperdollRect(const UILayout& layout) {
    using namespace detail;
    return at(layout, ORACLE_DOLL_X, ORACLE_DOLL_Y, ORACLE_DOLL_W, ORACLE_DOLL_H);
}

/**
 * The pixel size of the offscreen render target the character model is
 * drawn into before it is blitted into paperdollRect() (T409, board B219).
 *
 * It lives here, beside the rect it feeds, because the two must share a
 * proportion: a target whose aspect differs from the rect's squashes the
 * model in the blit. B1078 keeps the doll's aspect exact at every resolution
 * (windowRect() scales 428x460 by ONE uniform factor, so the doll rect keeps
 * the oracle's 85x168 aspect), so this may read the rect at a single fixed
 * probe resolution (1024x768, the factor there is 1) and the aspect is
 * correct at every other resolution for free.
 */
inline irr::core::dimension2d<irr::u32> paperdollRenderTargetSize() {
    UILayout probe;
    probe.computeLayout(1024, 768);
    const auto r = paperdollRect(probe);
    return irr::core::dimension2d<irr::u32>(
        static_cast<irr::u32>(r.getWidth() * detail::PAPERDOLL_RT_SUPERSAMPLE),
        static_cast<irr::u32>(r.getHeight() * detail::PAPERDOLL_RT_SUPERSAMPLE));
}

/**
 * Returns the screen-space rect for one coin row (0=platinum .. 3=copper),
 * positioned within `windowRect()`. Degenerate (all-zero) rect for an
 * out-of-range denomination. As with slotRect(), this is the ONLY place
 * purse geometry is computed -- renderInventoryPopup() contains no
 * coin-position arithmetic of its own.
 */
inline irr::core::rect<irr::s32> coinRect(const UILayout& layout, int denom) {
    using namespace detail;
    if (denom < 0 || denom >= COIN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return at(layout, ORACLE_COIN_X, ORACLE_COIN_Y[denom],
              ORACLE_COIN_W, ORACLE_COIN_H);
}

/**
 * The coin decal box inside one coin row -- where the oracle blits
 * A_PlatinumCoin / A_GoldCoin / A_SilverCoin / A_CopperCoin. fable's UI
 * atlas has no coin sprite yet, so renderInventoryPopup() puts the
 * denomination letter here; the rect is the oracle's either way.
 */
inline irr::core::rect<irr::s32> coinDecalRect(const UILayout& layout, int denom) {
    using namespace detail;
    if (denom < 0 || denom >= COIN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return at(layout, ORACLE_COIN_X + ORACLE_DECAL_X,
              ORACLE_COIN_Y[denom] + ORACLE_DECAL_Y,
              ORACLE_DECAL_SIZE, ORACLE_DECAL_SIZE);
}

/**
 * B442b (#151 rows 34-35): `IW_Destroy`, the button that destroys the item on
 * the cursor after a confirmation.
 *
 *   <Button item="IW_Destroy">  Location X=202 Y=384  Size CX=50 CY=20
 *                               <Text>Destroy</Text>
 *
 * THE SIZE IS THE ORACLE'S, THE PLACE IS NOT. Its oracle size is 50x20,
 * mapped into `windowRect()` by the ONE factor like coinRect(). Its oracle
 * position, mapped the same way, lands on fable's own WEAPON row (the slot
 * grid is fable's T57 layout, not the oracle's, which is why only the purse
 * and the doll carry oracle positions). So it sits in the free space to the
 * right of that row, right-aligned with the right equipment column and
 * centred on the row: a flagged anchor deviation, the kind T434/T436 made
 * for their windows. Pinned disjoint from every slot rect by
 * tests/test_cursor_gesture_completion.cpp.
 */
inline irr::core::rect<irr::s32> destroyButtonRect(const UILayout& layout) {
    using namespace detail;
    const irr::s32 weaponY = CONTENT_TOP + COL_ROWS * SLOT + GAP_COL_WEAPON;
    const irr::s32 oy = weaponY + (SLOT - ORACLE_DESTROY_H) / 2;
    const irr::s32 ox = RIGHT_COL_RIGHT - ORACLE_DESTROY_W;
    return at(layout, ox, oy, ORACLE_DESTROY_W, ORACLE_DESTROY_H);
}

} // namespace InventoryLayout
} // namespace Graphics
} // namespace EQT
