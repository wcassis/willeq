/*
 * StaticUIInput implementation.
 * U06j: Standalone input handler for the new static layout UI.
 */

#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

bool StaticUIInput::processInput(int mouseX, int mouseY, bool leftClicked,
                                  const UILayout& layout,
                                  HotbarPanelState& hotbar,
                                  SpellGemPanelState& spellGems,
                                  InventoryPanelState& inventory,
                                  SpellbookPopupState& spellbook,
                                  SkillsPopupState& skills) {
    bool consumed = false;

    // Reset hover states
    hotbar.hoveredSlot = -1;
    spellGems.hoveredGem = -1;
    inventory.hoveredSlot = -1;
    spellbook.hoveredSlot = -1;
    skills.hoveredRow = -1;

    // --- Hotbar hit test (10 horizontal slots) ---
    {
        int slot = hitTestSlotRow(mouseX, mouseY,
            layout.hotbar.UpperLeftCorner.X, layout.hotbar.UpperLeftCorner.Y,
            UILayout::SLOT_SIZE, UILayout::MARGIN, HotbarPanelState::SLOT_COUNT);
        if (slot >= 0) {
            hotbar.hoveredSlot = slot;
            consumed = true;
            if (leftClicked && bridge_) {
                bridge_->pushIntent(eqt::events::HotbarActivateIntent{slot});
            }
        }
    }

    // --- Spell gem hit test (8 vertical slots) ---
    {
        int sx = layout.spellGems.UpperLeftCorner.X;
        int sy = layout.spellGems.UpperLeftCorner.Y;
        for (int i = 0; i < SpellGemPanelState::GEM_COUNT; ++i) {
            int gy = sy + i * (UILayout::SLOT_SIZE + UILayout::MARGIN);
            if (mouseX >= sx && mouseX < sx + UILayout::SLOT_SIZE &&
                mouseY >= gy && mouseY < gy + UILayout::SLOT_SIZE) {
                spellGems.hoveredGem = i;
                consumed = true;
                if (leftClicked && bridge_) {
                    bridge_->pushIntent(eqt::events::CastSpellIntent{static_cast<uint8_t>(i)});
                }
                break;
            }
        }
    }

    // --- Inventory popup hit test ---
    // Geometry comes entirely from InventoryLayout::slotRect() — the single
    // shared source also used by renderInventoryPopup() (T57). No
    // slot-position arithmetic here.
    if (inventory.activePopup == PopupType::Inventory) {
        if (mouseX >= layout.centerPopup.UpperLeftCorner.X &&
            mouseX < layout.centerPopup.LowerRightCorner.X &&
            mouseY >= layout.centerPopup.UpperLeftCorner.Y &&
            mouseY < layout.centerPopup.LowerRightCorner.Y) {
            consumed = true;
            for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
                irr::core::rect<irr::s32> r = InventoryLayout::slotRect(layout, i);
                if (mouseX >= r.UpperLeftCorner.X && mouseX < r.LowerRightCorner.X &&
                    mouseY >= r.UpperLeftCorner.Y && mouseY < r.LowerRightCorner.Y) {
                    inventory.hoveredSlot = i;
                    break;
                }
            }
        }
    }

    // --- Spellbook popup hit test ---
    if (spellbook.isOpen) {
        if (mouseX >= layout.centerPopup.UpperLeftCorner.X &&
            mouseX < layout.centerPopup.LowerRightCorner.X &&
            mouseY >= layout.centerPopup.UpperLeftCorner.Y &&
            mouseY < layout.centerPopup.LowerRightCorner.Y) {
            consumed = true;
            // Spell rows
            int startY = layout.centerPopup.UpperLeftCorner.Y + 24;
            constexpr int LINE_H = 28;
            constexpr int PAD = UILayout::MARGIN;
            for (int i = 0; i < SpellbookPopupState::SPELLS_PER_PAGE; ++i) {
                int ry = startY + i * (LINE_H + PAD);
                if (mouseY >= ry && mouseY < ry + LINE_H) {
                    spellbook.hoveredSlot = i;
                    if (leftClicked) {
                        // Memorize spell — would need MemorizeSpellIntent
                        // For now just log
                        int idx = spellbook.currentPage * SpellbookPopupState::SPELLS_PER_PAGE + i;
                        if (idx < static_cast<int>(spellbook.spells.size())) {
                            LOG_INFO(MOD_GRAPHICS, "Spellbook: clicked spell '{}'",
                                spellbook.spells[idx].name);
                        }
                    }
                    break;
                }
            }
            // Page navigation
            int navY = layout.centerPopup.LowerRightCorner.Y - 20;
            if (leftClicked && mouseY >= navY && mouseY < navY + 14) {
                int midX = (layout.centerPopup.UpperLeftCorner.X + layout.centerPopup.LowerRightCorner.X) / 2;
                if (mouseX < midX && spellbook.currentPage > 0) {
                    spellbook.currentPage--;
                } else if (mouseX >= midX && spellbook.currentPage < spellbook.pageCount() - 1) {
                    spellbook.currentPage++;
                }
            }
        }
    }

    // --- Skills popup scroll area ---
    if (skills.isOpen) {
        if (mouseX >= layout.centerPopup.UpperLeftCorner.X &&
            mouseX < layout.centerPopup.LowerRightCorner.X &&
            mouseY >= layout.centerPopup.UpperLeftCorner.Y &&
            mouseY < layout.centerPopup.LowerRightCorner.Y) {
            consumed = true;
            int startY = layout.centerPopup.UpperLeftCorner.Y + 24;
            constexpr int LINE_H = 18;
            constexpr int PAD = 2;
            for (int i = 0; i < 20; ++i) {  // max visible
                int ry = startY + i * (LINE_H + PAD);
                if (mouseY >= ry && mouseY < ry + LINE_H) {
                    skills.hoveredRow = i;
                    break;
                }
            }
        }
    }

    return consumed;
}

bool StaticUIInput::processKey(irr::EKEY_CODE key, bool pressed, bool /*shift*/, bool /*ctrl*/,
                                InventoryPanelState& inventory,
                                SpellbookPopupState& spellbook,
                                SkillsPopupState& skills) {
    if (!pressed) return false;

    // ESC closes any open popup
    if (key == irr::KEY_ESCAPE) {
        if (spellbook.isOpen) { spellbook.isOpen = false; return true; }
        if (skills.isOpen) { skills.isOpen = false; return true; }
        if (inventory.activePopup != PopupType::None) {
            inventory.activePopup = PopupType::None;
            return true;
        }
    }

    return false;
}

int StaticUIInput::hitTestSlotRow(int mx, int my, int startX, int startY,
                                   int slotSize, int padding, int count) const {
    if (my < startY || my >= startY + slotSize) return -1;
    for (int i = 0; i < count; ++i) {
        int sx = startX + i * (slotSize + padding);
        if (mx >= sx && mx < sx + slotSize) return i;
    }
    return -1;
}

} // namespace Graphics
} // namespace EQT
