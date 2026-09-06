/*
 * StaticUIInput implementation.
 * U06j: Standalone input handler for the new static layout UI.
 */

#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/combat_ability_layout.h"
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
    combatAbilities.hoveredButton = -1;

    // --- The tradeskill container window (B321) ---
    if (tradeskill.isOpen) {
        const int slot = hitTestTradeskillSlot(layout, tradeskill, mouseX, mouseY);
        if (slot >= 0) {
            tradeskill.hoveredSlot = slot;
            consumed = true;
        }
        const TradeskillButton button =
            hitTestTradeskillButton(layout, tradeskill, mouseX, mouseY);
        if (button != TradeskillButton::None) {
            consumed = true;
            if (leftClicked && bridge_) {
                if (button == TradeskillButton::Combine) {
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
                                           const LootPanelState& loot) {
    const irr::core::vector2di p(mouseX, mouseY);

    // Popups first, topmost first -- the same precedence processKey()'s ESC
    // handler uses. Both draw into layout.centerPopup.
    if (layout.centerPopup.isPointInside(p)) {
        if (loot.isOpen) return ScrollTarget::Loot;
        if (skills.isOpen) return ScrollTarget::Skills;
        return ScrollTarget::None;
    }

    // The whole chat panel scrolls, not just the message area: the tab strip
    // and the input field are part of the box the user is pointing at, and
    // the alternative is a dead 16px band at the top and bottom of it.
    if (layout.chatPanel.isPointInside(p)) return ScrollTarget::Chat;

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
        case ScrollTarget::None:
        default:
            return 0;
    }
}

bool StaticUIInput::scrollPanel(ScrollTarget target, int lines,
                                const UILayout& layout,
                                ChatPanelState& chat,
                                SkillsPopupState& skills,
                                LootPanelState& loot) {
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
        case ScrollTarget::None:
        default:
            return false;
    }
}

bool StaticUIInput::processWheel(int mouseX, int mouseY, float wheelDelta,
                                 const UILayout& layout,
                                 ChatPanelState& chat,
                                 SkillsPopupState& skills,
                                 LootPanelState& loot) {
    if (wheelDelta == 0.0f) return false;
    ScrollTarget target = scrollTargetAt(mouseX, mouseY, layout, skills, loot);
    if (target == ScrollTarget::None) return false;

    // Irrlicht's Wheel is +1 per notch away from the user. Round rather than
    // truncate so a fractional accumulation (trackpads, the RDP wheel
    // translation in rdp_input_handler.cpp) still moves a line.
    int notches = static_cast<int>(std::lround(wheelDelta));
    if (notches == 0) notches = (wheelDelta > 0.0f) ? 1 : -1;
    return scrollPanel(target, notches * WHEEL_LINES, layout, chat, skills, loot);
}

bool StaticUIInput::processScrollKey(bool up, int mouseX, int mouseY,
                                     const UILayout& layout,
                                     ChatPanelState& chat,
                                     SkillsPopupState& skills,
                                     LootPanelState& loot) {
    ScrollTarget target = scrollTargetAt(mouseX, mouseY, layout, skills, loot);
    if (target == ScrollTarget::None) {
        // A page key with the cursor out in the world still has an obvious
        // subject when a scrollable popup is up -- you opened it, it is the
        // only thing on screen, and requiring the cursor to be inside it as
        // well would make the keyboard path useless to a --script run, which
        // never moves the pointer off (0,0).
        if (loot.isOpen) target = ScrollTarget::Loot;
        else if (skills.isOpen) target = ScrollTarget::Skills;
        else target = ScrollTarget::Chat;   // the always-present panel
    }
    int page = pageSize(target, layout);
    if (page <= 0) return false;
    return scrollPanel(target, up ? page : -page, layout, chat, skills, loot);
}

std::string StaticUIInput::runScrollCommand(const std::string& args,
                                            int mouseX, int mouseY,
                                            const UILayout& layout,
                                            ChatPanelState& chat,
                                            SkillsPopupState& skills,
                                            LootPanelState& loot) {
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
    }
    if (target == ScrollTarget::None) {
        target = scrollTargetAt(mouseX, mouseY, layout, skills, loot);
    }
    if (target == ScrollTarget::None) {
        return "Scroll: no scrollable panel under the cursor "
               "(usage: /scroll [chat|skills|loot] [up|down|top|bottom] [lines])";
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

    scrollPanel(target, lines, layout, chat, skills, loot);

    static const char* kNames[] = {"none", "chat", "skills", "loot"};
    return std::string("Scroll: ") + kNames[static_cast<int>(target)] + " " + which;
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
