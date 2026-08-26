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
#include <algorithm>

namespace EQT {
namespace Graphics {

bool StaticUIInput::processInput(int mouseX, int mouseY, bool leftClicked,
                                  const UILayout& layout,
                                  HotbarPanelState& hotbar,
                                  SpellGemPanelState& spellGems,
                                  InventoryPanelState& inventory,
                                  SpellbookPopupState& spellbook,
                                  SkillsPopupState& skills,
                                  const BuffBarState& buffs,
                                  LootPanelState& loot) {
    bool consumed = false;

    // Reset hover states
    hotbar.hoveredSlot = -1;
    spellGems.hoveredGem = -1;
    inventory.hoveredSlot = -1;
    spellbook.hoveredSlot = -1;
    skills.hoveredRow = -1;
    loot.hoveredRow = -1;

    // --- Hotbar hit test (10 horizontal slots) ---
    {
        int slot = hitTestSlotRow(mouseX, mouseY,
            layout.hotbar.UpperLeftCorner.X, layout.hotbar.UpperLeftCorner.Y,
            UILayout::SLOT_SIZE, UILayout::MARGIN, HotbarPanelState::SLOT_COUNT);
        if (slot >= 0) {
            hotbar.hoveredSlot = slot;
            consumed = true;
            if (leftClicked && bridge_) {
                // T156: was `HotbarActivateIntent{slot}` — a brace-init with
                // the index alone, which value-initialised buttonType/id/
                // emoteText and made every click look like "Spell 0". Fill all
                // four fields from the cached slot state, carrying the model's
                // own HotbarSlotType so the consumer switches on enumerators.
                const auto& cached = hotbar.slots[slot];
                if (cached.type != eqt::HotbarSlotType::Empty) {
                    bridge_->pushIntent(eqt::events::HotbarActivateIntent{
                        slot, cached.type, cached.id, cached.emoteText});
                }
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
                if (leftClicked) {
                    if (spellbook.isOpen) {
                        // T272: with the spellbook open, a gem click PICKS the
                        // destination for the next memorize instead of casting
                        // -- you cannot cast out of your spellbook anyway, and
                        // this is the only way to say "overwrite THIS gem"
                        // when all eight are full. Clicking the already-chosen
                        // gem clears the choice.
                        spellGems.memorizeTargetGem =
                            (spellGems.memorizeTargetGem == i) ? -1 : i;
                    } else if (bridge_) {
                        bridge_->pushIntent(eqt::events::CastSpellIntent{static_cast<uint8_t>(i)});
                    }
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

    // --- Buff bar hit test (T277: click a buff off) ---
    // Geometry comes entirely from hitTestBuffBar(), the twin of
    // renderBuffBar()'s own loop -- no icon arithmetic here. The bar is
    // only clickable while it is drawn (at least one active buff), and it
    // consumes clicks anywhere inside itself so a miss between icons does
    // not fall through to the 3D scene.
    if (mouseX >= layout.buffBar.UpperLeftCorner.X &&
        mouseX < layout.buffBar.LowerRightCorner.X &&
        mouseY >= layout.buffBar.UpperLeftCorner.Y &&
        mouseY < layout.buffBar.LowerRightCorner.Y) {
        int buffSlot = hitTestBuffBar(layout, buffs, mouseX, mouseY);
        if (buffSlot >= 0) {
            consumed = true;
            if (leftClicked && bridge_) {
                // Ask, never assume: the intent's consumer sends OP_Buff and
                // waits for the server's bufffade == 1 before the buff
                // leaves the bar. A detrimental buff will simply stay.
                LOG_INFO(MOD_GRAPHICS, "Buff bar: requesting cancel of slot {}", buffSlot);
                bridge_->pushIntent(eqt::events::BuffCancelIntent{
                    static_cast<uint8_t>(buffSlot)});
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
                        // T272: this was a LOG stub naming MemorizeSpellIntent
                        // as the missing piece (T264 kept the intent for
                        // exactly this). Row index is relative to the current
                        // page, exactly as renderSpellbookPopup() draws it;
                        // the destination gem comes from the single shared
                        // resolver so the click and the gem-panel highlight
                        // can never disagree.
                        int idx = spellbook.currentPage * SpellbookPopupState::SPELLS_PER_PAGE + i;
                        if (idx < static_cast<int>(spellbook.spells.size()) && bridge_) {
                            int gem = resolveMemorizeGem(spellGems);
                            if (gem < 0) {
                                LOG_INFO(MOD_GRAPHICS,
                                    "Spellbook: all gems full -- click a gem to choose "
                                    "which to overwrite, then click '{}'",
                                    spellbook.spells[idx].name);
                            } else {
                                LOG_INFO(MOD_GRAPHICS, "Spellbook: memorizing '{}' into gem {}",
                                    spellbook.spells[idx].name, gem + 1);
                                bridge_->pushIntent(eqt::events::MemorizeSpellIntent{
                                    static_cast<uint8_t>(gem),
                                    spellbook.spells[idx].spellId});
                                // The explicit pick is consumed by the click
                                // it targeted; the next one falls back to
                                // "first empty gem".
                                spellGems.memorizeTargetGem = -1;
                            }
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

    // --- Loot popup hit test (T290) ---
    // Geometry comes entirely from hitTestLootRow()/hitTestLootButton(),
    // the twins of renderLootPopup()'s own loop -- no row arithmetic here.
    // The three intents pushed below are the UI-GESTURE leg of the same
    // feature /loot, /lootall and /lootclose drive; they land on the very
    // same handler bodies in eq_bridge.cpp (LootItemFromCorpse,
    // CombatManager::LootAll, CloseLootWindow), so there is one body per
    // action, not two (T230/T239).
    if (loot.isOpen) {
        if (mouseX >= layout.centerPopup.UpperLeftCorner.X &&
            mouseX < layout.centerPopup.LowerRightCorner.X &&
            mouseY >= layout.centerPopup.UpperLeftCorner.Y &&
            mouseY < layout.centerPopup.LowerRightCorner.Y) {
            consumed = true;

            LootButton btn = hitTestLootButton(layout, loot, mouseX, mouseY);
            if (btn != LootButton::None) {
                if (leftClicked && bridge_) {
                    if (btn == LootButton::LootAll && !loot.items.empty()) {
                        LOG_INFO(MOD_GRAPHICS, "Loot window: Loot All on corpse {}",
                            loot.corpseId);
                        bridge_->pushIntent(eqt::events::LootAllIntent{loot.corpseId});
                    } else if (btn == LootButton::Close) {
                        // Ask, never assume: the panel stays up until the
                        // game publishes LootWindowClosed. Closing it here
                        // would desync the server's looting state (T277's
                        // rule, and the old window's callback-driven close).
                        LOG_INFO(MOD_GRAPHICS, "Loot window: closing corpse {}",
                            loot.corpseId);
                        bridge_->pushIntent(eqt::events::CloseLootIntent{loot.corpseId});
                    }
                }
            } else {
                int idx = hitTestLootRow(layout, loot, mouseX, mouseY);
                if (idx >= 0 && idx < static_cast<int>(loot.items.size())) {
                    // hoveredRow is the ON-SCREEN row; idx is the index into
                    // the packed list. They differ whenever the list is
                    // scrolled -- and the CORPSE SLOT differs from both,
                    // which is why the intent carries items[idx].slot.
                    int first = std::min(loot.scrollOffset,
                        std::max(0, static_cast<int>(loot.items.size()) - 1));
                    loot.hoveredRow = idx - first;
                    if (leftClicked && bridge_) {
                        LOG_INFO(MOD_GRAPHICS,
                            "Loot window: taking '{}' (corpse slot {}) from corpse {}",
                            loot.items[idx].itemName, loot.items[idx].slot,
                            loot.corpseId);
                        bridge_->pushIntent(eqt::events::LootItemIntent{
                            loot.corpseId, loot.items[idx].slot});
                    }
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
                    // T156: SkillActivateIntent had a consumer in eq_bridge.cpp
                    // and no producer anywhere (T147's trap class). This is it:
                    // clicking a row in the skills popup activates that skill.
                    // Row index is relative to the scroll window, exactly as
                    // renderSkillsPopup() draws it.
                    if (leftClicked && bridge_) {
                        int idx = skills.scrollOffset + i;
                        if (idx >= 0 && idx < static_cast<int>(skills.skills.size())) {
                            bridge_->pushIntent(eqt::events::SkillActivateIntent{
                                skills.skills[idx].skillId});
                        }
                    }
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
                                SkillsPopupState& skills,
                                LootPanelState& loot) {
    if (!pressed) return false;

    // ESC closes any open popup
    if (key == irr::KEY_ESCAPE) {
        // T290: the loot popup goes first -- it is drawn on top, and it is
        // the only popup whose close has to reach the server. It does NOT
        // clear isOpen here; LootWindowClosed does.
        if (loot.isOpen) {
            if (bridge_) {
                bridge_->pushIntent(eqt::events::CloseLootIntent{loot.corpseId});
            }
            return true;
        }
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
