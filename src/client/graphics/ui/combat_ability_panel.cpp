/*
 * T415 (board B320) — the combat abilities window.
 *
 * A separate TU, for the same reason tradeskill_panel.cpp and loot_panel.cpp
 * are: substantial new code goes in new TUs, not into static_panels.cpp.
 *
 * The geometry is in include/client/graphics/ui/combat_ability_layout.h,
 * which quotes EQUI_CombatAbilityWnd.xml; nothing here computes a rect.
 */

#include "client/graphics/ui/combat_ability_panel.h"

#include "client/graphics/ui/combat_ability_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/text_batch.h"
#include "client/skill/skill_constants.h"
#include "client/state/event_bus.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

std::string CombatAbilityPanelState::effectLabelText() const {
    if (!hasEffect) {
        // The oracle's own shipped <Text> for CAW_CombatEffectLabel.
        return "No Effect";
    }
    return EQ::getSkillName(effectSkillId);
}

bool combatAbilityWindowIsVisible(const CombatAbilityPanelState& state) {
    return state.isOpen;
}

void applyCombatAbilityFeedback(
    CombatAbilityPanelState& state,
    const eqt::state::SkillActivationFeedbackData& data) {
    // A refusal is a chat line, not a cooldown; and the producer only
    // publishes a non-zero cooldown, so a zero here means "nothing to time".
    if (!data.success || data.cooldownMs == 0) {
        return;
    }
    state.hasEffect = true;
    state.effectSkillId = data.skillId;
    state.cooldownMs = data.cooldownMs;
    state.cooldownStart = std::chrono::steady_clock::now();
}

void rebuildCombatAbilities(CombatAbilityPanelState& state,
                            const std::vector<SkillDisplayItem>& skills) {
    state.abilities.clear();
    state.hoveredButton = -1;

    std::vector<const SkillDisplayItem*> picked;
    for (const auto& s : skills) {
        if (s.value == 0) continue;  // a skill the character does not have
        if (!EQ::isActivatableSkill(s.skillId)) continue;
        if (EQ::getSkillCategory(s.skillId) != EQ::SkillCategory::Combat) continue;
        picked.push_back(&s);
    }
    std::sort(picked.begin(), picked.end(),
              [](const SkillDisplayItem* a, const SkillDisplayItem* b) {
                  return a->skillId < b->skillId;
              });
    if (static_cast<int>(picked.size()) > CombatAbilityPanelState::BUTTON_COUNT) {
        picked.resize(CombatAbilityPanelState::BUTTON_COUNT);
    }
    for (const auto* s : picked) {
        CombatAbilityEntry e;
        e.skillId = s->skillId;
        e.name = s->name;
        e.value = s->value;
        state.abilities.push_back(std::move(e));
    }
}

void renderCombatAbilityPanel(UIRenderer& ui, const UILayout& layout,
                              const CombatAbilityPanelState& state) {
    if (!combatAbilityWindowIsVisible(state)) return;

    auto* tb = ui.getTextBatch();
    const auto win = CombatAbilityLayout::windowRect(layout);

    ui.drawPanel(win);

    // The window title — the oracle's <Text>Combat Abilities</Text>.
    if (tb) {
        tb->addText("Combat Abilities", win.UpperLeftCorner.X + 6,
                    win.UpperLeftCorner.Y + 4,
                    irr::video::SColor(255, 255, 215, 0));
    }

    // CAW_CombatEffectButton — the oracle blits a BuffIcons decal here.
    // fable's atlas has no buff-icon sheet, so the box is drawn and left
    // empty rather than filled with an invented glyph.
    ui.drawSprite(CombatAbilityLayout::effectIconRect(layout),
                  static_cast<uint8_t>(UISprite::SlotBackground));

    // CAW_CombatEffectLabel.
    if (tb) {
        tb->addText(state.effectLabelText(),
                    CombatAbilityLayout::effectLabelRect(layout).UpperLeftCorner.X,
                    CombatAbilityLayout::effectLabelRect(layout).UpperLeftCorner.Y + 3,
                    state.hasEffect ? irr::video::SColor(255, 230, 230, 200)
                                    : irr::video::SColor(255, 140, 140, 140));
    }

    // CAW_CombatEffectTimeRemainingGauge (EQType 26) -- THE consumer of
    // SkillActivationFeedbackData::cooldownMs (board B320).
    ui.drawBar(CombatAbilityLayout::gaugeRect(layout), state.gaugeProgress(),
               irr::video::SColor(255, 200, 120, 60),
               irr::video::SColor(200, 20, 20, 25));

    // CAW_OpenCombatSkillSelectWnd -- the oracle's "S". fable has no combat
    // skill select window; the box is drawn so the layout matches, and it
    // is not hit-tested.
    if (tb) {
        tb->addTextCentered("S", CombatAbilityLayout::skillSelectRect(layout),
                            irr::video::SColor(255, 120, 120, 120));
    }

    // CAW_Button1..8.
    for (int i = 0; i < CombatAbilityPanelState::BUTTON_COUNT; ++i) {
        const auto r = CombatAbilityLayout::buttonRect(layout, i);
        const bool filled = i < static_cast<int>(state.abilities.size());
        ui.drawSprite(r, static_cast<uint8_t>(UISprite::SlotBackground));
        if (filled && state.hoveredButton == i) {
            ui.drawRect(r, irr::video::SColor(60, 255, 255, 255));
        }
        if (!filled || !tb) continue;
        tb->addTextCentered(state.abilities[i].name, r,
                            irr::video::SColor(255, 225, 225, 225));
    }
}

int hitTestCombatAbility(const UILayout& layout,
                         const CombatAbilityPanelState& state,
                         int mouseX, int mouseY) {
    if (!combatAbilityWindowIsVisible(state)) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    const int count = std::min(static_cast<int>(state.abilities.size()),
                               CombatAbilityPanelState::BUTTON_COUNT);
    for (int i = 0; i < count; ++i) {
        if (CombatAbilityLayout::buttonRect(layout, i).isPointInside(p)) {
            return i;
        }
    }
    return -1;
}

} // namespace Graphics
} // namespace EQT
