/*
 * InventoryLayout — the single shared source of inventory-popup slot
 * geometry.
 *
 * B1075 (#395): the 400x350 box every rect below is mapped into is
 * `UILayout::inventoryPopup` (the layout table's PairRight anchor, so the
 * merchant/loot/bank/trade window can sit beside it). The comments below
 * that say "centerPopup" predate the split and mean that same-size box.
 *
 * T57: static_panels.cpp's renderInventoryPopup() and
 * static_ui_input.cpp's processInput() used to independently compute
 * these same rectangles from the same magic numbers, and drifted apart
 * identically wrong (all 8 general slots + equipment slots 20-21 fell
 * outside centerPopup — see refactor/T57_inventory_paperdoll_layout.md).
 * Both now call slotRect() below; neither file may contain slot-position
 * arithmetic of its own.
 *
 * Layout is an EQ-authentic body arrangement (Will's choice, 2026-08-11):
 * two 8-slot columns (left: ears/head/face/neck/shoulders/arms/back,
 * right: wrists/range/hands/rings/chest/legs) flank the paperdoll rect
 * (T60: character preview — the empty center T54/T57 recorded as a
 * deliberate gap is now filled), a 6-slot weapon row
 * (primary/secondary/feet/waist/ammo/charm) sits below both columns, and
 * the 8-slot general inventory row sits below that. All 30 slot rects
 * plus the paperdoll rect are proven to lie wholly inside the fixed
 * 400x350 UILayout::centerPopup and not overlap each other — see the
 * containment test in tests/test_static_ui_panels.cpp and the arithmetic
 * in the T57/T60 plans' Reviews.
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

constexpr irr::s32 SLOT = UILayout::SLOT_SIZE;  // 32
constexpr irr::s32 PAD = 2;

// Vertical layout, relative to centerPopup's top-left corner.
constexpr irr::s32 CONTENT_TOP = 20;      // below the title
constexpr int COL_ROWS = 8;               // slots per side column
constexpr irr::s32 GAP_COL_WEAPON = 2;    // columns bottom -> weapon row
constexpr irr::s32 GAP_WEAPON_GENERAL = 6;  // weapon row -> general row

// Horizontal layout, relative to centerPopup's top-left corner.
constexpr irr::s32 LEFT_COL_X = 8;

constexpr int WEAPON_COUNT = 6;
constexpr int GEN_COUNT = 8;

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
//
// THE PLACEMENT IS THE ORACLE'S, NOT OURS. Every number below is quoted
// verbatim from the official Titanium/P1999 default UI,
// /mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml:
//
//   <Screen item="InventoryWindow">  Size CX=428 CY=460
//   <Button item="IW_Money0">  Location X=303 Y=121  Size CX=70 CY=24
//                              ButtonDrawTemplate/NormalDecal A_PlatinumCoin
//                              DecalOffset X=1 Y=3  DecalSize CX=18 CY=18
//   <Button item="IW_Money1">  Location X=303 Y=147  Size CX=70 CY=24
//                              NormalDecal A_GoldCoin     (same decal box)
//   <Button item="IW_Money2">  Location X=303 Y=171  Size CX=70 CY=24
//                              NormalDecal A_SilverCoin   (same decal box)
//   <Button item="IW_Money3">  Location X=303 Y=197  Size CX=70 CY=24
//                              NormalDecal A_CopperCoin   (same decal box)
//
// so the oracle draws the four denominations as a single column of 70x24
// boxes, top to bottom platinum/gold/silver/copper, each with a coin decal
// on its left and the count as its text.
//
// The scale rule is the popup's own: fable's centerPopup is a fixed
// 400x350 at every resolution (T351 measured all five) and every rect in
// this header is expressed relative to that frame, so the coin column is
// the oracle's rect mapped proportionally from the 428x460 window onto
// centerPopup's width and height. Integer division, so the mapping is
// exact and reproducible; no rounding policy to drift.
constexpr irr::s32 ORACLE_WINDOW_W = 428;
constexpr irr::s32 ORACLE_WINDOW_H = 460;
constexpr irr::s32 ORACLE_COIN_X = 303;
constexpr irr::s32 ORACLE_COIN_W = 70;
constexpr irr::s32 ORACLE_COIN_H = 24;
constexpr irr::s32 ORACLE_COIN_Y[4] = {121, 147, 171, 197};
constexpr irr::s32 ORACLE_DECAL_X = 1;
constexpr irr::s32 ORACLE_DECAL_Y = 3;
constexpr irr::s32 ORACLE_DECAL_SIZE = 18;

// --- The character view (T409, board B219) ----------------------------
//
// Same file, same window, same scale rule as the purse above:
//
//   <Screen item="IW_CharacterView">  Location X=166 Y=57
//                                     Size CX=85 CY=168
//
// i.e. the oracle's doll is a TALL box occupying 20% of the window width,
// which is what leaves its coin column at X=303 clear of it. fable's rect
// used to fill the whole gap between the equipment columns (76% of the
// popup) and the coin rows landed on it -- board B219, ruled 2026-09-06.
constexpr irr::s32 ORACLE_DOLL_X = 166;
constexpr irr::s32 ORACLE_DOLL_Y = 57;
constexpr irr::s32 ORACLE_DOLL_W = 85;
constexpr irr::s32 ORACLE_DOLL_H = 168;

// The doll is rendered to an offscreen target and blitted into
// paperdollRect(). Supersample factor: the target is this many times the
// drawn rect on each axis, so the blit only ever downscales.
constexpr irr::s32 PAPERDOLL_RT_SUPERSAMPLE = 2;

} // namespace detail

/**
 * Returns the screen-space rect for a global inventory slot index
 * (0-21 equipment, 22-29 general), positioned within `layout.inventoryPopup`.
 * Returns a degenerate (all-zero) rect for an out-of-range index.
 *
 * This is the ONLY place inventory-popup slot geometry is computed —
 * renderInventoryPopup() and StaticUIInput::processInput() both call it.
 */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int globalIndex) {
    using namespace detail;
    const auto& popup = layout.inventoryPopup;
    const irr::s32 popupX = popup.UpperLeftCorner.X;
    const irr::s32 popupY = popup.UpperLeftCorner.Y;
    const irr::s32 popupW = popup.getWidth();

    const irr::s32 rightColX = popupW - LEFT_COL_X - SLOT;

    constexpr irr::s32 weaponRowW = WEAPON_COUNT * SLOT + (WEAPON_COUNT - 1) * PAD;
    const irr::s32 weaponRowX = (popupW - weaponRowW) / 2;

    constexpr irr::s32 generalRowW = GEN_COUNT * SLOT + (GEN_COUNT - 1) * PAD;
    const irr::s32 generalRowX = (popupW - generalRowW) / 2;

    const irr::s32 weaponY = popupY + CONTENT_TOP + COL_ROWS * SLOT + GAP_COL_WEAPON;
    const irr::s32 generalY = weaponY + SLOT + GAP_WEAPON_GENERAL;

    irr::s32 x = 0, y = 0;
    bool found = false;

    if (globalIndex >= 0 && globalIndex < EQUIP_COUNT) {
        for (int row = 0; row < COL_ROWS && !found; ++row) {
            if (kLeftColumn[row] == globalIndex) {
                x = popupX + LEFT_COL_X;
                y = popupY + CONTENT_TOP + row * SLOT;
                found = true;
            } else if (kRightColumn[row] == globalIndex) {
                x = popupX + rightColX;
                y = popupY + CONTENT_TOP + row * SLOT;
                found = true;
            }
        }
        for (int col = 0; col < WEAPON_COUNT && !found; ++col) {
            if (kWeaponRow[col] == globalIndex) {
                x = popupX + weaponRowX + col * (SLOT + PAD);
                y = weaponY;
                found = true;
            }
        }
    } else if (globalIndex >= EQUIP_COUNT && globalIndex < TOTAL_SLOTS) {
        int i = globalIndex - EQUIP_COUNT;
        x = popupX + generalRowX + i * (SLOT + PAD);
        y = generalY;
        found = true;
    }

    if (!found) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    return irr::core::rect<irr::s32>(x, y, x + SLOT, y + SLOT);
}

/**
 * Returns the screen-space rect for the character paperdoll (T60), placed
 * where the OFFICIAL client puts its character view (T409, board B219).
 *
 * THE PLACEMENT IS THE ORACLE'S, NOT OURS, exactly as it is for coinRect():
 * the `IW_CharacterView` rect (166,57 85x168) mapped proportionally from the
 * 428x460 `InventoryWindow` onto centerPopup's fixed 400x350, integer
 * division. It used to fill the entire gap between the two equipment columns
 * — 76% of the popup width against the oracle's 20% — which is why T393's
 * oracle-placed coin column landed on its right-hand margin (B219, seen on
 * screen in T401's live pair). Narrowing it to the oracle's proportion makes
 * the doll and the purse disjoint at every resolution; both facts are pinned
 * in tests/test_static_ui_panels.cpp.
 *
 * It still lies between the two equipment columns and above the weapon row,
 * so the T60 containment/no-overlap invariants hold as before.
 */
inline irr::core::rect<irr::s32> paperdollRect(const UILayout& layout) {
    using namespace detail;
    const auto& popup = layout.inventoryPopup;
    const irr::s32 popupW = popup.getWidth();
    const irr::s32 popupH = popup.getHeight();

    const irr::s32 x = popup.UpperLeftCorner.X + ORACLE_DOLL_X * popupW / ORACLE_WINDOW_W;
    const irr::s32 y = popup.UpperLeftCorner.Y + ORACLE_DOLL_Y * popupH / ORACLE_WINDOW_H;
    const irr::s32 w = ORACLE_DOLL_W * popupW / ORACLE_WINDOW_W;
    const irr::s32 h = ORACLE_DOLL_H * popupH / ORACLE_WINDOW_H;
    return irr::core::rect<irr::s32>(x, y, x + w, y + h);
}

/**
 * The pixel size of the offscreen render target the character model is
 * drawn into before it is blitted into paperdollRect() (T409, board B219).
 *
 * It lives here, beside the rect it feeds, because the two must share a
 * proportion: a target whose aspect differs from the rect's squashes the
 * model in the blit. centerPopup is a fixed 400x350 at every resolution
 * (T351 measured five), so this is a constant, and the layout is computed
 * at an arbitrary resolution to read it.
 */
inline irr::core::dimension2d<irr::u32> paperdollRenderTargetSize() {
    UILayout probe;
    probe.computeLayout(1024, 768);
    const auto r = paperdollRect(probe);
    return irr::core::dimension2d<irr::u32>(
        static_cast<irr::u32>(r.getWidth() * detail::PAPERDOLL_RT_SUPERSAMPLE),
        static_cast<irr::u32>(r.getHeight() * detail::PAPERDOLL_RT_SUPERSAMPLE));
}

// Denomination order is the oracle's element order, IW_Money0..3.
constexpr int COIN_COUNT = 4;
enum Coin { COIN_PLATINUM = 0, COIN_GOLD = 1, COIN_SILVER = 2, COIN_COPPER = 3 };

/**
 * Returns the screen-space rect for one coin row (0=platinum .. 3=copper),
 * positioned within `layout.inventoryPopup`. Degenerate (all-zero) rect for an
 * out-of-range denomination.
 *
 * As with slotRect(), this is the ONLY place purse geometry is computed —
 * renderInventoryPopup() contains no coin-position arithmetic of its own.
 */
inline irr::core::rect<irr::s32> coinRect(const UILayout& layout, int denom) {
    using namespace detail;
    if (denom < 0 || denom >= COIN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    const auto& popup = layout.inventoryPopup;
    const irr::s32 popupW = popup.getWidth();
    const irr::s32 popupH = popup.getHeight();

    const irr::s32 x = popup.UpperLeftCorner.X + ORACLE_COIN_X * popupW / ORACLE_WINDOW_W;
    const irr::s32 y = popup.UpperLeftCorner.Y + ORACLE_COIN_Y[denom] * popupH / ORACLE_WINDOW_H;
    const irr::s32 w = ORACLE_COIN_W * popupW / ORACLE_WINDOW_W;
    const irr::s32 h = ORACLE_COIN_H * popupH / ORACLE_WINDOW_H;
    return irr::core::rect<irr::s32>(x, y, x + w, y + h);
}

/**
 * The coin decal box inside one coin row — where the oracle blits
 * A_PlatinumCoin / A_GoldCoin / A_SilverCoin / A_CopperCoin. fable's UI
 * atlas has no coin sprite yet, so renderInventoryPopup() puts the
 * denomination letter here; the rect is the oracle's either way.
 */
inline irr::core::rect<irr::s32> coinDecalRect(const UILayout& layout, int denom) {
    using namespace detail;
    if (denom < 0 || denom >= COIN_COUNT) {
        return irr::core::rect<irr::s32>(0, 0, 0, 0);
    }
    const auto& popup = layout.inventoryPopup;
    const irr::s32 popupW = popup.getWidth();
    const irr::s32 popupH = popup.getHeight();
    const auto row = coinRect(layout, denom);

    const irr::s32 x = row.UpperLeftCorner.X + ORACLE_DECAL_X * popupW / ORACLE_WINDOW_W;
    const irr::s32 y = row.UpperLeftCorner.Y + ORACLE_DECAL_Y * popupH / ORACLE_WINDOW_H;
    const irr::s32 w = ORACLE_DECAL_SIZE * popupW / ORACLE_WINDOW_W;
    const irr::s32 h = ORACLE_DECAL_SIZE * popupH / ORACLE_WINDOW_H;
    return irr::core::rect<irr::s32>(x, y, x + w, y + h);
}

/**
 * B442b (#151 rows 34-35): `IW_Destroy`, the button that destroys the item on
 * the cursor after a confirmation.
 *
 *   <Button item="IW_Destroy">  Location X=202 Y=382  Size CX=50 CY=20
 *                               <Text>Destroy</Text>
 *
 * THE SIZE IS THE ORACLE'S, THE PLACE IS NOT. The size is the oracle's 50x20
 * mapped onto centerPopup like coinRect() (46x15). Its oracle position,
 * mapped the same way, lands on fable's own WEAPON row (the slot grid is
 * fable's T57 layout, not the oracle's, which is why only the purse and the
 * doll carry oracle positions). So it sits in the free space to the right of
 * that row, right-aligned with the right equipment column and centred on the
 * row: a flagged anchor deviation, the kind T434/T436 made for their windows.
 * Pinned disjoint from every slot rect by tests/test_cursor_gesture_completion.cpp.
 */
inline irr::core::rect<irr::s32> destroyButtonRect(const UILayout& layout) {
    using namespace detail;
    constexpr irr::s32 ORACLE_DESTROY_W = 50;
    constexpr irr::s32 ORACLE_DESTROY_H = 20;
    const auto& popup = layout.inventoryPopup;
    const irr::s32 popupW = popup.getWidth();
    const irr::s32 popupH = popup.getHeight();
    const irr::s32 w = ORACLE_DESTROY_W * popupW / ORACLE_WINDOW_W;
    const irr::s32 h = ORACLE_DESTROY_H * popupH / ORACLE_WINDOW_H;
    const irr::s32 weaponY = popup.UpperLeftCorner.Y + CONTENT_TOP + COL_ROWS * SLOT + GAP_COL_WEAPON;
    const irr::s32 x = popup.UpperLeftCorner.X + popupW - LEFT_COL_X - w;
    const irr::s32 y = weaponY + (SLOT - h) / 2;
    return irr::core::rect<irr::s32>(x, y, x + w, y + h);
}

} // namespace InventoryLayout
} // namespace Graphics
} // namespace EQT
