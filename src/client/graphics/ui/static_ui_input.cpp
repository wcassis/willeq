/*
 * StaticUIInput implementation.
 * U06j: Standalone input handler for the new static layout UI.
 */

#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/hotbar_panel.h"   // B1025-H2: hitTestHotbar
#include "client/graphics/ui/spell_gem_layout.h"  // B1077: the oracle's gem/book rects
#include "client/hotbar/hotbar_model.h"        // B1025-H2: PAGE_COUNT
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/combat_ability_layout.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/item_display_layout.h"
// T436 (board B429).
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "common/packets/merchant.h"   // B442b: EQT::COINSLOT_BANK
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace EQT {
namespace Graphics {

namespace {

/**
 * The panel's cache for inventory slot `i` (the flat 0..TOTAL_SLOTS-1 index
 * the layout uses), or null. The twin of renderInventoryPopup()'s own split
 * between the equipment array and the general array -- same bound, same
 * indexing.
 */
const SlotDisplayInfo* slotInfo(const InventoryPanelState& inv, int i) {
    if (i < 0) return nullptr;
    if (i < InventoryPanelState::EQUIP_SLOTS) {
        return &inv.equipSlots[i];
    }
    const int g = i - InventoryPanelState::EQUIP_SLOTS;
    if (g < InventoryPanelState::GENERAL_SLOTS) {
        return &inv.generalSlots[g];
    }
    return nullptr;
}

}  // namespace

bool StaticUIInput::processInput(int mouseX, int mouseY, bool leftClicked,
                                  const UILayout& layout,
                                  HotbarPanelState& hotbar,
                                  SpellGemPanelState& spellGems,
                                  InventoryPanelState& inventory,
                                  SpellbookPopupState& spellbook,
                                  SkillsPopupState& skills,
                                  const BuffBarState& buffs,
                                  LootPanelState& loot,
                                  bool shift) {
    bool consumed = false;

    // Reset hover states
    hotbar.hoveredSlot = -1;
    spellGems.hoveredGem = -1;
    spellGems.bookClicked = false;   // B1077: consumed once per frame by FrameComponent
    spellGems.bookHovered = false;   // B1077
    inventory.hoveredSlot = -1;
    spellbook.hoveredSlot = -1;
    skills.hoveredRow = -1;
    loot.hoveredRow = -1;

    // B1075 (#395): the drawn-last rule. The centre popups are drawn AFTER
    // the HUD (renderStaticUI()'s order), so where an open popup covers a HUD
    // window the popup owns the point. Before B1075 no popup touched the
    // hotbar or the gems; under the layout table the side-by-side pair fills
    // an 800-wide screen and the inventory covers part of the gem column.
    const irr::core::vector2di at(mouseX, mouseY);
    const bool overOpenPopup =
        (inventory.activePopup == PopupType::Inventory &&
         layout.inventoryPopup.isPointInside(at)) ||
        ((spellbook.isOpen || skills.isOpen) && layout.centerPopup.isPointInside(at)) ||
        (loot.isOpen && layout.partnerPopup.isPointInside(at));

    // --- Hotbar window (B1025-H2): the stock HotButtonWnd, one window ---
    // The renderer and this test share HotbarLayout's rects, so a click lands
    // on the drawn control. The old horizontal-row hitTestSlotRow() is gone
    // with the old geometry.
    if (!overOpenPopup) {
        const HotbarHit hbHit = hitTestHotbar(layout, hotbar, mouseX, mouseY);
        if (hbHit != HotbarHit::None) {
            consumed = true;
            if (leftClicked && bridge_) {
                switch (hbHit) {
                case HotbarHit::Slot: {
                    // T156: fill all four fields from the cached slot state so
                    // the consumer switches on the model's enumerators.
                    const int slot = hotbar.hoveredSlot;
                    const auto& cached = hotbar.slots[slot];
                    if (cached.type != eqt::HotbarSlotType::Empty) {
                        bridge_->pushIntent(eqt::events::HotbarActivateIntent{
                            slot, cached.type, cached.id, cached.emoteText});
                    }
                    break;
                }
                case HotbarHit::PageLeft:
                    if (hotbar.currentPage > 0) {
                        bridge_->pushIntent(eqt::events::HotbarPageChangedIntent{
                            hotbar.currentPage - 1});
                    }
                    break;
                case HotbarHit::PageRight:
                    if (hotbar.currentPage < eqt::HotbarModel::PAGE_COUNT - 1) {
                        bridge_->pushIntent(eqt::events::HotbarPageChangedIntent{
                            hotbar.currentPage + 1});
                    }
                    break;
                case HotbarHit::Close:
                    // In-session toggle; nothing user-changeable is persisted
                    // (T432), so the flag lives on the panel state.
                    hotbar.isOpen = false;
                    break;
                case HotbarHit::None:
                    break;
                }
            }
        }
    }

    // --- Spell gem hit test: the oracle's own cell rects (B1077) ---
    if (!overOpenPopup) {
        for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) {
            const auto gr = SpellGemLayout::gemRect(i);
            const int gx = layout.spellGems.UpperLeftCorner.X + gr.UpperLeftCorner.X;
            const int gy = layout.spellGems.UpperLeftCorner.Y + gr.UpperLeftCorner.Y;
            if (mouseX >= gx && mouseX < gx + gr.getWidth() &&
                mouseY >= gy && mouseY < gy + gr.getHeight()) {
                spellGems.hoveredGem = i;
                consumed = true;
                if (leftClicked && shift) {
                    // T425 (board B298, spell half): shift+click a MEMORISED
                    // gem asks for the spell display. The gem's own spellId
                    // is already cached here (SpellGemState), so the request
                    // carries the spell, not the slot -- the same id the
                    // spellbook arm below sends.
                    if (bridge_ && spellGems.gems[i].spellId != 0) {
                        bridge_->pushIntent(eqt::events::SpellDisplayRequestIntent{
                            spellGems.gems[i].spellId});
                    }
                } else if (leftClicked) {
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

        // B1077: the CSPW_SpellBook button -- the oracle's 22x22 toggle at
        // (10,252) inside the gem window. A LEFT click lands bookClicked;
        // FrameComponent consumes it and runs the SAME toggle body Ctrl+B
        // and /book use (the /newbook body in renderer_slash_commands.cpp).
        const auto br = SpellGemLayout::bookRect();
        const int bx = layout.spellGems.UpperLeftCorner.X + br.UpperLeftCorner.X;
        const int by = layout.spellGems.UpperLeftCorner.Y + br.UpperLeftCorner.Y;
        if (mouseX >= bx && mouseX < bx + br.getWidth() &&
            mouseY >= by && mouseY < by + br.getHeight()) {
            spellGems.bookHovered = true;
            consumed = true;
            if (leftClicked) spellGems.bookClicked = true;
        }
    }

    // --- Inventory popup hit test ---
    // Geometry comes entirely from InventoryLayout::slotRect() — the single
    // shared source also used by renderInventoryPopup() (T57). No
    // slot-position arithmetic here.
    if (inventory.activePopup == PopupType::Inventory) {
        if (mouseX >= layout.inventoryPopup.UpperLeftCorner.X &&
            mouseX < layout.inventoryPopup.LowerRightCorner.X &&
            mouseY >= layout.inventoryPopup.UpperLeftCorner.Y &&
            mouseY < layout.inventoryPopup.LowerRightCorner.Y) {
            consumed = true;
            for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
                irr::core::rect<irr::s32> r = InventoryLayout::slotRect(layout, i);
                if (mouseX >= r.UpperLeftCorner.X && mouseX < r.LowerRightCorner.X &&
                    mouseY >= r.UpperLeftCorner.Y && mouseY < r.LowerRightCorner.Y) {
                    inventory.hoveredSlot = i;
                    // B442 (#151): the click-to-cursor gesture. LEFT picks
                    // up / places / swaps / merges, RIGHT is the item display
                    // -- the oracle's own map, which replaces T421's
                    // left-click-displays deviation now that the static UI
                    // latches a right click. The flat index IS the Titanium
                    // slot id (0-21 worn, 22-29 general).
                    if (leftClicked || rightClicked_) {
                        const SlotDisplayInfo* info = slotInfo(inventory, i);
                        eqt::inventory::SlotSnapshot snap;
                        snap.occupied = info && info->hasItem;
                        snap.quantity = info ? info->quantity : 0;
                        slotClick(static_cast<int16_t>(i), leftClicked, snap,
                                  info ? info->itemName : std::string(),
                                  info ? info->iconId : 0u,
                                  info && info->hasItem ? info->bagSlots : uint8_t{0});
                    }
                    break;
                }
            }
            // B442b (#151 rows 34-35): IW_Destroy. With an item held it asks
            // the game thread for the eqstr 2043 prompt; the dialog's Yes is
            // the destroy. With nothing held it does nothing (unmeasured).
            inventory.destroyHovered = false;
            if (inventory.hoveredSlot < 0) {
                const auto d = InventoryLayout::destroyButtonRect(layout);
                if (mouseX >= d.UpperLeftCorner.X && mouseX < d.LowerRightCorner.X &&
                    mouseY >= d.UpperLeftCorner.Y && mouseY < d.LowerRightCorner.Y) {
                    inventory.destroyHovered = true;
                    if (leftClicked && bridge_ && cursorHoldsItem()) {
                        eqt::events::CursorGestureIntent ask;
                        ask.op = eqt::events::CursorGestureOp::RequestDestroy;
                        bridge_->pushIntent(ask);
                    }
                }
            }
            // B442: a purse row (IW_Money0..3) -- coin onto the cursor, or
            // SHIFT for the QuantityWnd. Row 0 is platinum, the wire's 3.
            if (inventory.hoveredSlot < 0 && !inventory.destroyHovered && leftClicked) {
                for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
                    const auto r = InventoryLayout::coinRect(layout, d);
                    if (mouseX >= r.UpperLeftCorner.X && mouseX < r.LowerRightCorner.X &&
                        mouseY >= r.UpperLeftCorner.Y && mouseY < r.LowerRightCorner.Y) {
                        coinRowClick(d, inventory.coins[d]);
                        break;
                    }
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
                    if (leftClicked && shift) {
                        // T425 (board B298, spell half): shift+click a
                        // spellbook row asks for the spell display instead of
                        // memorising it.
                        int idx = spellbook.currentPage * SpellbookPopupState::SPELLS_PER_PAGE + i;
                        if (idx < static_cast<int>(spellbook.spells.size()) && bridge_) {
                            bridge_->pushIntent(eqt::events::SpellDisplayRequestIntent{
                                spellbook.spells[idx].spellId});
                        }
                    } else if (leftClicked) {
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
        if (mouseX >= layout.partnerPopup.UpperLeftCorner.X &&
            mouseX < layout.partnerPopup.LowerRightCorner.X &&
            mouseY >= layout.partnerPopup.UpperLeftCorner.Y &&
            mouseY < layout.partnerPopup.LowerRightCorner.Y) {
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
                    // B442 (#151): the loot window takes the shared button
                    // map. LEFT is the cursor gesture, and for a corpse slot
                    // that is LootItemIntent with auto_loot 0 -- eqemu's
                    // Corpse::LootCorpseItem puts the item ON THE CURSOR
                    // (PutLootInInventory(slotCursor)), which is the oracle's
                    // pick-up. A corpse stack cannot be split, so SHIFT does
                    // the same. RIGHT is MEASURED (UIGAP-live2,
                    // review/UIGAP-ORACLE/151/gestures.tsv row 46c): the
                    // official client sends OP_LootItem auto_loot=1 and the
                    // item goes straight into the inventory, the cursor
                    // untouched (row 46a is the LEFT, auto_loot=0).
                    const auto action = eqt::inventory::classifyClick(
                        leftClicked ? eqt::inventory::ClickButton::Left
                                    : eqt::inventory::ClickButton::Right,
                        mods_, eqt::inventory::SlotSnapshot{true, 1},
                        eqt::inventory::CursorSnapshot{cursorHoldsItem()});
                    if (leftClicked && bridge_ &&
                        action == eqt::inventory::GestureAction::CursorClick) {
                        LOG_INFO(MOD_GRAPHICS,
                            "Loot window: taking '{}' (corpse slot {}) from corpse {}",
                            loot.items[idx].itemName, loot.items[idx].slot,
                            loot.corpseId);
                        bridge_->pushIntent(eqt::events::LootItemIntent{
                            loot.corpseId, loot.items[idx].slot, /*autoLoot=*/false});
                    } else if (!leftClicked && rightClicked_ && bridge_) {
                        LOG_INFO(MOD_GRAPHICS,
                            "Loot window: auto-looting '{}' (corpse slot {}) from corpse {}",
                            loot.items[idx].itemName, loot.items[idx].slot,
                            loot.corpseId);
                        bridge_->pushIntent(eqt::events::LootItemIntent{
                            loot.corpseId, loot.items[idx].slot, /*autoLoot=*/true});
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
            // T313: LINE_H/PAD were a second copy of renderSkillsPopup()'s
            // own constants and the loop bound was a hard-coded 20 -- more
            // rows than the popup ever DRAWS (15 at 800x600), so a click
            // low in the popup could activate a skill that was not on
            // screen. Both now come from the shared source.
            constexpr int LINE_H = SkillsPopupState::LINE_H;
            constexpr int PAD = SkillsPopupState::ROW_PAD;
            const int visibleRows = skillsVisibleRows(layout);
            for (int i = 0; i < visibleRows; ++i) {
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

// T415 (boards B321/B320) -- see the declaration for why this is its own
// entry point rather than two more parameters on processInput().
bool StaticUIInput::processPanelInput(int mouseX, int mouseY, bool leftClicked,
                                      const UILayout& layout,
                                      TradeskillPanelState& tradeskill,
                                      CombatAbilityPanelState& combatAbilities) {
    bool consumed = false;

    tradeskill.hoveredSlot = -1;
    tradeskill.hoveredRecipe = -1;
    combatAbilities.hoveredButton = -1;

    // --- The tradeskill container window (B321) ---
    if (tradeskill.isOpen) {
        const int slot = hitTestTradeskillSlot(layout, tradeskill, mouseX, mouseY);
        if (slot >= 0) {
            tradeskill.hoveredSlot = slot;
            consumed = true;
        }
        // COMBW_RecipeList (T419, board B335). A row click selects the recipe
        // AND asks the server for its components -- the same OP_RecipeDetails
        // /recipedetails sends, so the row is a second producer for an
        // existing sender, not a second packet.
        const int recipe = hitTestTradeskillRecipe(layout, tradeskill, mouseX, mouseY);
        if (recipe >= 0) {
            tradeskill.hoveredRecipe = recipe;
            consumed = true;
            if (leftClicked) {
                tradeskill.selectedRecipe = recipe;
                if (bridge_) {
                    bridge_->pushIntent(eqt::events::RecipeDetailsRequestIntent{
                        tradeskill.recipes[static_cast<size_t>(recipe)].recipeId});
                }
            }
        }
        // T421 (board B365): the three Editboxes. A click focuses one; a
        // click anywhere else inside the window blurs them, which is how the
        // keyboard gate is released (FrameComponent watches focusedField).
        const TradeskillField field =
            hitTestTradeskillField(layout, tradeskill, mouseX, mouseY);
        if (field != TradeskillField::None) {
            consumed = true;
            if (leftClicked) {
                if (auto* prev = tradeskill.focusedEntry()) {
                    prev->inputActive = false;
                }
                tradeskill.focusedField = field;
                if (auto* now = tradeskill.focusedEntry()) {
                    now->inputActive = true;
                    now->cursorPos = static_cast<int>(now->inputText.size());
                }
            }
        } else if (leftClicked &&
                   layout.centerPopup.isPointInside(
                       irr::core::vector2di(mouseX, mouseY))) {
            // The tradeskill window IS layout.centerPopup (TradeskillLayout
            // maps the oracle's 400x525 onto it), so this is "clicked in the
            // window but not in an editbox".
            if (auto* prev = tradeskill.focusedEntry()) {
                prev->inputActive = false;
            }
            tradeskill.focusedField = TradeskillField::None;
        }

        const TradeskillButton button =
            hitTestTradeskillButton(layout, tradeskill, mouseX, mouseY);
        if (button != TradeskillButton::None) {
            consumed = true;
            if (leftClicked && bridge_) {
                if (button == TradeskillButton::Search) {
                    // T421: COMBW_SearchButton. The same request Enter in any
                    // of the three editboxes makes, and the same
                    // OP_RecipesSearch `/recipesearch` sends.
                    bridge_->pushIntent(eqt::events::RecipeSearchIntent{
                        tradeskill.searchEntry.inputText,
                        tradeskill.typedMinTrivial(),
                        tradeskill.typedMaxTrivial()});
                } else if (button == TradeskillButton::AddFavorite ||
                           button == TradeskillButton::RemoveFavorite ||
                           button == TradeskillButton::ShowFavorites) {
                    // T421: the favourites row. Add/Remove act on the
                    // SELECTED row (0 for none, which the game side refuses
                    // rather than sending an empty edit); Show re-fetches.
                    const auto op =
                        button == TradeskillButton::AddFavorite
                            ? eqt::events::RecipeFavoriteOp::Add
                            : (button == TradeskillButton::RemoveFavorite
                                   ? eqt::events::RecipeFavoriteOp::Remove
                                   : eqt::events::RecipeFavoriteOp::Show);
                    const uint32_t id =
                        (tradeskill.selectedRecipe >= 0 &&
                         tradeskill.selectedRecipe <
                             static_cast<int>(tradeskill.recipes.size()))
                            ? tradeskill.recipes[static_cast<size_t>(
                                  tradeskill.selectedRecipe)].recipeId
                            : 0;
                    bridge_->pushIntent(eqt::events::RecipeFavoriteIntent{op, id});
                } else if (button == TradeskillButton::Combine) {
                    // The container slot the server expects. A WORLD container
                    // combines through SLOT_TRADESKILL_EXPERIMENT_COMBINE (the
                    // same value `/combine` with no argument sends); an
                    // inventory container combines through its own slot.
                    const int16_t containerSlot =
                        tradeskill.isWorldObject
                            ? eqt::inventory::SLOT_TRADESKILL_EXPERIMENT_COMBINE
                            : tradeskill.inventorySlot;
                    bridge_->pushIntent(
                        eqt::events::TradeskillCombineIntent{containerSlot});
                } else {
                    // The UI never closes itself -- T290's rule. The panel
                    // stays up until TradeskillContainerClosed comes back, so
                    // an optimistic local close cannot desync the server's
                    // "this container is open" state.
                    bridge_->pushIntent(
                        eqt::events::TradeskillCloseIntent{tradeskill.objectId});
                }
            }
        }
    }

    // --- The combat abilities window (B320) ---
    if (combatAbilityWindowIsVisible(combatAbilities)) {
        if (CombatAbilityLayout::windowRect(layout).isPointInside(
                irr::core::vector2di(mouseX, mouseY))) {
            consumed = true;
        }
        const int row = hitTestCombatAbility(layout, combatAbilities, mouseX, mouseY);
        if (row >= 0) {
            combatAbilities.hoveredButton = row;
            if (leftClicked && bridge_) {
                bridge_->pushIntent(eqt::events::SkillActivateIntent{
                    combatAbilities.abilities[row].skillId});
            }
        }
    }

    return consumed;
}

// T434 -- the Options window. See the doc comment on the declaration for why
// it is its own entry point and why it resolves a click into a Setting rather
// than acting on it here.
bool StaticUIInput::processOptionsInput(int mouseX, int mouseY, bool leftClicked,
                                        const UILayout& layout,
                                        OptionsPanelState& options,
                                        OptionsLayout::Setting& changed) {
    changed = OptionsLayout::Setting::None;
    if (!optionsWindowIsVisible(options)) {
        options.hoveredTab = -1;
        options.hoveredControl = -1;
        return false;
    }
    // B993: the colour picker is drawn above the window, so it is asked first.
    if (colorPickerContains(layout, options.colorPicker, mouseX, mouseY)) {
        options.hoveredTab = -1;
        options.hoveredControl = -1;
        if (leftClicked &&
            clickColorPicker(layout, options.colorPicker, options.textColors, mouseX, mouseY)) {
            changed = OptionsLayout::Setting::TextColors;
        }
        return true;
    }
    if (!optionsWindowContains(layout, options, mouseX, mouseY)) {
        options.hoveredTab = -1;
        options.hoveredControl = -1;
        return false;
    }

    options.hoveredTab = hitTestOptionsTab(layout, options, mouseX, mouseY);
    options.hoveredControl = hitTestOptionsControl(layout, options, mouseX, mouseY);

    if (leftClicked && options.hoveredTab >= 0) {
        options.activeTab = options.hoveredTab;
        // A tab switch changes which page is hit-testable, so the control
        // hover is stale the instant it lands.
        options.hoveredControl = -1;
    } else if (leftClicked && options.hoveredControl >= 0) {
        // B430: every wired kind (slider, checkbox, combobox, list) resolves
        // through the one body in options_lists.cpp.
        changed = resolveOptionsClick(layout, options, options.hoveredControl,
                                      mouseX, mouseY);
    }

    // The whole window consumes the position either way, so a click on a
    // greyed control does not fall through to the 3D scene behind it.
    return true;
}

bool StaticUIInput::processKey(irr::EKEY_CODE key, bool pressed, bool /*shift*/, bool /*ctrl*/,
                                InventoryPanelState& inventory,
                                SpellbookPopupState& spellbook,
                                SkillsPopupState& skills,
                                LootPanelState& loot,
                                TradeskillPanelState& tradeskill,
                                EscWindow only) {
    if (!pressed) return false;
    // B460: with `only` set, exactly that popup is addressed.
    const bool any = only == EscWindow::Count;
    auto pick = [&](EscWindow w) { return any || only == w; };

    // ESC closes any open popup
    if (key == irr::KEY_ESCAPE) {
        // T443 (board B454): the tradeskill container goes first -- it is
        // the LAST centre popup drawn (renderer_frame_render.cpp draws it
        // after the loot popup), so it is the topmost one here. Like the
        // loot window it is SERVER-owned: it pushes TradeskillCloseIntent
        // and does NOT clear isOpen; applyTradeskillContainerClosed() does,
        // off the server's TradeskillContainerClosed event. That is T290's
        // rule and the same body the window's Close button already used --
        // ESC is a second producer for one existing intent, not a new
        // packet.
        if (tradeskill.isOpen && pick(EscWindow::Tradeskill)) {
            if (bridge_) {
                bridge_->pushIntent(
                    eqt::events::TradeskillCloseIntent{tradeskill.objectId});
            }
            return true;
        }
        // T290: the loot popup goes next -- it is the only OTHER popup whose
        // close has to reach the server. It does NOT clear isOpen here;
        // LootWindowClosed does.
        if (loot.isOpen && pick(EscWindow::Loot)) {
            if (bridge_) {
                bridge_->pushIntent(eqt::events::CloseLootIntent{loot.corpseId});
            }
            return true;
        }
        if (spellbook.isOpen && pick(EscWindow::Spellbook)) { spellbook.isOpen = false; return true; }
        if (skills.isOpen && pick(EscWindow::Skills)) { skills.isOpen = false; return true; }
        if (inventory.activePopup != PopupType::None && pick(EscWindow::Inventory)) {
            inventory.activePopup = PopupType::None;
            return true;
        }
    }

    return false;
}

// ===========================================================================
// T313 -- the scroll input path.
//
// One router. scrollTargetAt() picks the panel, scrollPanel() moves it, and
// the wheel / PageUp-PageDown / `/scroll` entry points differ only in the
// step they pass. Nothing below invents a clamp: chat's upper bound is
// renderChatPanel()'s (it is the only code that knows the wrapped-line
// count), skills' is SkillsPopupState::maxScrollOffset(), loot's is
// LootPanelState::maxScrollOffset().
// ===========================================================================

ScrollTarget StaticUIInput::scrollTargetAt(int mouseX, int mouseY,
                                           const UILayout& layout,
                                           const SkillsPopupState& skills,
                                           const LootPanelState& loot,
                                           const ItemDisplayPanelState& itemDisplay) {
    const irr::core::vector2di p(mouseX, mouseY);

    // T421: the item display window is TOPMOST -- it has its own absolute
    // placement and is drawn after every centre popup -- so it is asked
    // first, and it is the one target that is not in layout.centerPopup.
    if (itemDisplayContains(layout, itemDisplay, mouseX, mouseY)) {
        return ScrollTarget::ItemDisplay;
    }

    // Popups first, topmost first -- the same precedence processKey()'s ESC
    // handler uses (loot is drawn after skills). B1075 (#395): each popup is
    // asked over ITS OWN layout-table anchor -- the loot window is the
    // PairLeft popup (layout.partnerPopup), the skills window the CentreSlot
    // (layout.centerPopup) -- and an open popup, drawn on top, wins over
    // the chat underneath it. A zero-area panel (an uninitialised layout,
    // whose rects are all (0,0,0,0) -- isPointInside answers true for (0,0)
    // there) owns no pixels and is not a target.
    if (loot.isOpen && layout.partnerPopup.getArea() > 0 &&
        layout.partnerPopup.isPointInside(p)) {
        return ScrollTarget::Loot;
    }
    if (skills.isOpen && layout.centerPopup.getArea() > 0 &&
        layout.centerPopup.isPointInside(p)) {
        return ScrollTarget::Skills;
    }

    // The whole chat panel scrolls, not just the message area: the tab strip
    // and the input field are part of the box the user is pointing at, and
    // the alternative is a dead 16px band at the top and bottom of it.
    if (layout.chatPanel.getArea() > 0 && layout.chatPanel.isPointInside(p))
        return ScrollTarget::Chat;

    return ScrollTarget::None;
}

int StaticUIInput::pageSize(ScrollTarget target, const UILayout& layout) {
    switch (target) {
        case ScrollTarget::Chat:
            return chatVisibleLines(layout);
        case ScrollTarget::Skills:
            return skillsVisibleRows(layout);
        case ScrollTarget::Loot:
            return LootPanelState::VISIBLE_ROWS;
        case ScrollTarget::ItemDisplay:
            return ItemDisplayLayout::visibleDescriptionLines(layout);
        case ScrollTarget::None:
        default:
            return 0;
    }
}

bool StaticUIInput::scrollPanel(ScrollTarget target, int lines,
                                const UILayout& layout,
                                ChatPanelState& chat,
                                SkillsPopupState& skills,
                                LootPanelState& loot,
                                ItemDisplayPanelState& itemDisplay) {
    switch (target) {
        case ScrollTarget::Chat: {
            // From the BOTTOM: 0 is pinned to the newest line, and scrolling
            // UP (lines > 0) means a LARGER offset. The top end is clamped by
            // renderChatPanel(), which is the only place the wrapped-line
            // total exists; overshooting here is corrected on the next drawn
            // frame rather than silently discarded.
            int next = chat.scrollOffset + lines;
            chat.scrollOffset = next > 0 ? next : 0;
            return true;
        }
        case ScrollTarget::Skills: {
            // From the TOP: scrolling UP means a SMALLER offset.
            int maxOff = skills.maxScrollOffset(skillsVisibleRows(layout));
            int next = skills.scrollOffset - lines;
            skills.scrollOffset = std::max(0, std::min(next, maxOff));
            return true;
        }
        case ScrollTarget::Loot: {
            int maxOff = loot.maxScrollOffset();
            int next = loot.scrollOffset - lines;
            loot.scrollOffset = std::max(0, std::min(next, maxOff));
            // The hovered row is an ON-SCREEN row index; the row under the
            // unmoved cursor is a different item now. Recomputed by the next
            // processInput().
            loot.hoveredRow = -1;
            return true;
        }
        case ScrollTarget::ItemDisplay: {
            // T421: from the TOP, the skills/loot convention.
            // `Style_VScroll true` on IDW_ItemDescription is why this exists:
            // a loaded item composes more lines than the oracle's 190px
            // window shows.
            int maxOff = itemDisplay.maxScrollOffset(layout);
            int next = itemDisplay.scrollOffset - lines;
            itemDisplay.scrollOffset = std::max(0, std::min(next, maxOff));
            return true;
        }
        case ScrollTarget::None:
        default:
            return false;
    }
}

bool StaticUIInput::processWheel(int mouseX, int mouseY, float wheelDelta,
                                 const UILayout& layout,
                                 ChatPanelState& chat,
                                 SkillsPopupState& skills,
                                 LootPanelState& loot,
                                 ItemDisplayPanelState& itemDisplay) {
    if (wheelDelta == 0.0f) return false;
    ScrollTarget target = scrollTargetAt(mouseX, mouseY, layout, skills, loot, itemDisplay);
    if (target == ScrollTarget::None) return false;

    // Irrlicht's Wheel is +1 per notch away from the user. Round rather than
    // truncate so a fractional accumulation (trackpads, the RDP wheel
    // translation in rdp_input_handler.cpp) still moves a line.
    int notches = static_cast<int>(std::lround(wheelDelta));
    if (notches == 0) notches = (wheelDelta > 0.0f) ? 1 : -1;
    return scrollPanel(target, notches * WHEEL_LINES, layout, chat, skills, loot,
                       itemDisplay);
}

bool StaticUIInput::processScrollKey(bool up, int mouseX, int mouseY,
                                     const UILayout& layout,
                                     ChatPanelState& chat,
                                     SkillsPopupState& skills,
                                     LootPanelState& loot,
                                     ItemDisplayPanelState& itemDisplay) {
    ScrollTarget target = scrollTargetAt(mouseX, mouseY, layout, skills, loot, itemDisplay);
    if (target == ScrollTarget::None) {
        // A page key with the cursor out in the world still has an obvious
        // subject when a scrollable popup is up -- you opened it, it is the
        // only thing on screen, and requiring the cursor to be inside it as
        // well would make the keyboard path useless to a --script run, which
        // never moves the pointer off (0,0).
        if (itemDisplay.isOpen) target = ScrollTarget::ItemDisplay;
        else if (loot.isOpen) target = ScrollTarget::Loot;
        else if (skills.isOpen) target = ScrollTarget::Skills;
        else target = ScrollTarget::Chat;   // the always-present panel
    }
    int page = pageSize(target, layout);
    if (page <= 0) return false;
    return scrollPanel(target, up ? page : -page, layout, chat, skills, loot,
                       itemDisplay);
}

std::string StaticUIInput::runScrollCommand(const std::string& args,
                                            int mouseX, int mouseY,
                                            const UILayout& layout,
                                            ChatPanelState& chat,
                                            SkillsPopupState& skills,
                                            LootPanelState& loot,
                                            ItemDisplayPanelState& itemDisplay) {
    std::vector<std::string> tok;
    {
        std::istringstream iss(args);
        std::string w;
        while (iss >> w) tok.push_back(w);
    }

    ScrollTarget target = ScrollTarget::None;
    size_t dirIdx = 0;
    if (!tok.empty()) {
        if (tok[0] == "chat") { target = ScrollTarget::Chat; dirIdx = 1; }
        else if (tok[0] == "skills") { target = ScrollTarget::Skills; dirIdx = 1; }
        else if (tok[0] == "loot") { target = ScrollTarget::Loot; dirIdx = 1; }
        else if (tok[0] == "item") { target = ScrollTarget::ItemDisplay; dirIdx = 1; }
    }
    if (target == ScrollTarget::None) {
        target = scrollTargetAt(mouseX, mouseY, layout, skills, loot, itemDisplay);
    }
    if (target == ScrollTarget::None) {
        return "Scroll: no scrollable panel under the cursor "
               "(usage: /scroll [chat|skills|loot|item] [up|down|top|bottom] [lines])";
    }

    const std::string which = tok.size() > dirIdx ? tok[dirIdx] : std::string("up");
    int lines = pageSize(target, layout);
    if (tok.size() > dirIdx + 1) {
        int n = std::atoi(tok[dirIdx + 1].c_str());
        if (n > 0) lines = n;
    }
    // "top"/"bottom" ride the same clamps rather than reaching into the
    // panels: a step larger than any list saturates at the end of travel.
    if (which == "top") lines = 1000000;
    else if (which == "bottom") lines = -1000000;
    else if (which == "down") lines = -lines;

    scrollPanel(target, lines, layout, chat, skills, loot, itemDisplay);

    static const char* kNames[] = {"none", "chat", "skills", "loot", "item"};
    return std::string("Scroll: ") + kNames[static_cast<int>(target)] + " " + which;
}

// T421 (board B298, item half) -- see the declaration for why this is its
// own entry point.
bool StaticUIInput::processItemDisplayInput(int mouseX, int mouseY,
                                            bool leftClicked,
                                            const UILayout& layout,
                                            ItemDisplayPanelState& itemDisplay) {
    itemDisplay.closeHovered = false;
    if (!itemDisplay.isOpen) return false;

    if (hitTestItemDisplayCloseBox(layout, itemDisplay, mouseX, mouseY)) {
        itemDisplay.closeHovered = true;
        if (leftClicked) {
            // Unlike the loot popup, this window has no server-side state:
            // the item display is a pure client view of an item the client
            // already holds, so closing it locally cannot desync anything.
            itemDisplay.isOpen = false;
        }
        return true;
    }
    return itemDisplayContains(layout, itemDisplay, mouseX, mouseY);
}

// T436 (board B429) -- see the declaration for why the merchant and the bank
// share one entry point and why the cursor is const here.
bool StaticUIInput::processCommerceInput(int mouseX, int mouseY,
                                         bool leftClicked,
                                         const UILayout& layout,
                                         MerchantPanelState& merchant,
                                         BankPanelState& bank,
                                         const CursorAttachmentState& cursor) {
    merchant.hoveredRow = -1;
    merchant.tradeHovered = false;
    merchant.doneHovered = false;
    bank.hoveredSlot = -1;
    bank.doneHovered = false;
    bank.closeHovered = false;
    bank.changeHovered = false;

    // --- the bank window, asked first (it draws above the centre popups) ---
    if (bank.isOpen) {
        const bool onDone = hitTestBankDoneButton(layout, bank, mouseX, mouseY);
        const bool onClose = hitTestBankCloseBox(layout, bank, mouseX, mouseY);
        if (onDone || onClose) {
            bank.doneHovered = onDone;
            bank.closeHovered = onClose;
            if (leftClicked && bridge_) {
                // The session is the SERVER's; the panel closes when
                // BankWindowClosed comes back (T290's rule).
                bridge_->pushIntent(eqt::events::CloseBankIntent{});
            }
            return true;
        }
        // B441 (#26): BW_ChangeButton -- the game thread converts both
        // purses from its own counters and sends the OP_MoveCoin steps.
        if (hitTestBankChangeButton(layout, bank, mouseX, mouseY)) {
            bank.changeHovered = true;
            if (leftClicked && bridge_) {
                bridge_->pushIntent(eqt::events::BankMakeChangeIntent{});
            }
            return true;
        }
        const int slot = hitTestBankSlot(layout, bank, mouseX, mouseY);
        if (slot >= 0) {
            bank.hoveredSlot = slot;
            // B442 (#151): BW_BankSlot<n> is Titanium slot 2000+n, and a click
            // is the same gesture as an inventory slot's -- into and out of
            // the bank is a MoveItem between the slot and the cursor.
            if (leftClicked || rightClicked_) {
                const auto& cell = bank.slots[slot];
                eqt::inventory::SlotSnapshot snap;
                snap.occupied = cell.hasItem;
                snap.quantity = cell.quantity;
                slotClick(static_cast<int16_t>(eqt::inventory::BANK_BEGIN + slot),
                          leftClicked, snap, cell.itemName, cell.iconId);
            }
            return true;
        }
        // B442b (#151 rows 43-44): BW_Money0..3, the bank's coin -- the same
        // coin gesture as the purse on OP_MoveCoin slot 2.
        if (leftClicked) {
            const irr::core::vector2di p(mouseX, mouseY);
            for (int denom = 0; denom < BankPanelState::COIN_COUNT; ++denom) {
                if (BankLayout::coinRect(layout, denom).isPointInside(p)) {
                    coinStoreClick(denom, bank.coins[denom], EQT::COINSLOT_BANK);
                    return true;
                }
            }
        }
        if (bankContains(layout, bank, mouseX, mouseY)) return true;
    }

    // --- the merchant window ---
    if (!merchant.isOpen) return false;

    if (hitTestMerchantDoneButton(layout, merchant, mouseX, mouseY)) {
        merchant.doneHovered = true;
        if (leftClicked && bridge_) {
            // VendorToggleIntent's handler closes an open vendor session --
            // no new intent is needed and no new packet is sent.
            bridge_->pushIntent(eqt::events::VendorToggleIntent{});
        }
        return true;
    }

    if (hitTestMerchantTradeButton(layout, merchant, mouseX, mouseY)) {
        merchant.tradeHovered = true;
        // B442b (#152): Buy or Sell by the side of the LAST selection, never
        // by the cursor (T436's default was wrong). static_ui_merchant.cpp.
        (void)cursor;
        if (leftClicked) merchantTradeClick(merchant);
        return true;
    }

    const int row = hitTestMerchantRow(layout, merchant, mouseX, mouseY);
    if (row >= 0) {
        merchant.hoveredRow = row;
        if (leftClicked) {
            // MW_SelectedItem / MW_SelectedItemLabel / MW_SelectedPriceLabel
            // all describe this row, and the shared button reads Buy
            // (merchant.tsv rows 2, 4, 9). Selection is purely client side --
            // the server is told nothing until Buy.
            merchant.selectRow(row);
        }
        return true;
    }

    return merchantContains(layout, merchant, mouseX, mouseY);
}

} // namespace Graphics
} // namespace EQT
