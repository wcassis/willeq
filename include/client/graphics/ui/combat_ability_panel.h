/*
 * The combat abilities window — renderer-side state and draw (T415,
 * board B320).
 *
 * `SkillActivationFeedbackData::cooldownMs` is computed on the game thread
 * (`EverQuest`'s `setOnSkillActivated` handler applies equipment + buff
 * haste to the skill's base recast) and published on EVERY successful skill
 * activation. `IrrlichtBridge`'s arm was a `LOG_TRACE` that discarded the
 * whole payload, and no panel had anywhere to put it — B320 is that missing
 * panel, not a missing producer.
 *
 * The oracle draws it as `CAW_CombatEffectTimeRemainingGauge`, EQType 26,
 * with `CAW_CombatEffectLabel` above it and eight ability buttons below —
 * see include/client/graphics/ui/combat_ability_layout.h for the quoted XML.
 *
 * The ability rows come from the skills the character HAS: the same
 * `SkillsPopupState::skills` list `/skills` (T363) draws, filtered to the
 * skills that are both `EQ::SkillCategory::Combat` and
 * `EQ::isActivatableSkill()` and whose value is non-zero, in skill-id order,
 * capped at the oracle's eight buttons. No second source of skill data.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state { struct SkillActivationFeedbackData; } }

namespace EQT {
namespace Graphics {

class UIRenderer;
struct SkillDisplayItem;

/** One CAW_Button row. */
struct CombatAbilityEntry {
    uint8_t skillId = 0;
    std::string name;
    uint32_t value = 0;
};

struct CombatAbilityPanelState {
    /**
     * The Alt+C toggle. MEASURED at the official client's Options > Keys >
     * UI page (T377, review/T377/oracle/T377_oracle_options_keys_UI_page3.png
     * and page4): "Toggle Combat Ability Window   Alt C", Alternate column
     * empty.
     *
     * Default CLOSED. Unlike the buff and group windows (T368/B101) there is
     * no shipped default to read: /mnt/projects/EverQuestP1999/defaults.ini
     * has no `[CombatAbilityWnd]` section at all, so there is no `Show=`
     * either way, and the T377 in-world captures show no such window open.
     * A window fable cannot prove ships open, ships closed.
     */
    bool isOpen = false;

    static constexpr int BUTTON_COUNT = 8;  // CAW_Button1..8
    std::vector<CombatAbilityEntry> abilities;
    int hoveredButton = -1;

    // --- CAW_CombatEffectLabel + CAW_CombatEffectTimeRemainingGauge ---
    bool hasEffect = false;
    uint8_t effectSkillId = 0;
    uint32_t cooldownMs = 0;
    std::chrono::steady_clock::time_point cooldownStart;

    /**
     * The gauge's fill, 1.0 at the instant of activation falling to 0.0 when
     * the cooldown expires — a TIME REMAINING gauge, which is what the
     * oracle element is called.
     */
    float gaugeProgress() const {
        if (!hasEffect || cooldownMs == 0) return 0.0f;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cooldownStart).count();
        if (elapsed <= 0) return 1.0f;
        if (static_cast<uint32_t>(elapsed) >= cooldownMs) return 0.0f;
        return 1.0f - static_cast<float>(elapsed) / static_cast<float>(cooldownMs);
    }

    /**
     * CAW_CombatEffectLabel's text: the skill's name, or the oracle's own
     * shipped placeholder `<Text>No Effect</Text>` when nothing is running.
     */
    std::string effectLabelText() const;
};

/** Does the window draw this frame? The toggle alone — there is no content gate. */
bool combatAbilityWindowIsVisible(const CombatAbilityPanelState& state);

/**
 * The SkillActivationFeedback bridge arm, as a free function. A failed
 * activation, or one with no cooldown, is not an effect and leaves the
 * gauge alone.
 */
void applyCombatAbilityFeedback(
    CombatAbilityPanelState& state,
    const eqt::state::SkillActivationFeedbackData& data);

/**
 * Rebuild the eight ability rows from the character's skill list — called
 * from the SkillsSnapshot and SkillValueChanged bridge arms, which already
 * own that list.
 */
void rebuildCombatAbilities(CombatAbilityPanelState& state,
                            const std::vector<SkillDisplayItem>& skills);

/** Render the window. Only when combatAbilityWindowIsVisible(). */
void renderCombatAbilityPanel(UIRenderer& ui, const UILayout& layout,
                              const CombatAbilityPanelState& state);

/**
 * Hit-test the ability buttons; returns an index into `state.abilities` or
 * -1. -1 for a closed window and for a button row with no ability behind it.
 */
int hitTestCombatAbility(const UILayout& layout,
                         const CombatAbilityPanelState& state,
                         int mouseX, int mouseY);

} // namespace Graphics
} // namespace EQT
