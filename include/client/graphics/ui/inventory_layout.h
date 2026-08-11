/*
 * InventoryLayout — the single shared source of inventory-popup slot
 * geometry.
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
 * right: wrists/range/hands/rings/chest/legs) flank an empty center
 * (no paperdoll model — see T54's recorded gap), a 6-slot weapon row
 * (primary/secondary/feet/waist/ammo/charm) sits below both columns, and
 * the 8-slot general inventory row sits below that. All 30 rects are
 * proven to lie wholly inside the fixed 400x350 UILayout::centerPopup —
 * see the containment test in tests/test_static_ui_panels.cpp and the
 * arithmetic in the plan's Review.
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

} // namespace detail

/**
 * Returns the screen-space rect for a global inventory slot index
 * (0-21 equipment, 22-29 general), positioned within `layout.centerPopup`.
 * Returns a degenerate (all-zero) rect for an out-of-range index.
 *
 * This is the ONLY place inventory-popup slot geometry is computed —
 * renderInventoryPopup() and StaticUIInput::processInput() both call it.
 */
inline irr::core::rect<irr::s32> slotRect(const UILayout& layout, int globalIndex) {
    using namespace detail;
    const auto& popup = layout.centerPopup;
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

} // namespace InventoryLayout
} // namespace Graphics
} // namespace EQT
