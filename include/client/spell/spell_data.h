/*
 * WillEQ Spell Data Structures
 *
 * Data structures representing spell definitions loaded from spells_us.txt.
 * Covers Classic through Velious expansions.
 */

#ifndef EQT_SPELL_DATA_H
#define EQT_SPELL_DATA_H

#include "spell_constants.h"
#include <string>
#include <array>
#include <cstdint>

namespace EQ {

// ============================================================================
// Spell Effect Slot
// ============================================================================

struct SpellEffectSlot {
    SpellEffect effect_id = SpellEffect::InvalidEffect;
    int32_t base_value = 0;      // Primary effect value
    int32_t base2_value = 0;     // Secondary/limit value
    int32_t max_value = 0;       // Maximum/cap value
    int32_t formula = 100;       // Scaling formula (100 = no scaling)

    bool isValid() const {
        return effect_id != SpellEffect::InvalidEffect &&
               effect_id != SpellEffect::UnusedEffect;
    }
};

// ============================================================================
// Spell Data
// ============================================================================

struct SpellData {
    // ========================================================================
    // Identification
    // ========================================================================

    uint32_t id = SPELL_UNKNOWN;
    std::string name;
    std::string player_tag;       // "PLAYER_1", etc.
    std::string teleport_zone;    // For teleport/gate spells

    // ========================================================================
    // Messages
    // ========================================================================

    std::string cast_on_you;      // "You feel stronger"
    std::string cast_on_other;    // "%s looks stronger" (use %s for target name)
    std::string spell_fades;      // "Your strength fades"

    // ========================================================================
    // Timing
    // ========================================================================

    uint32_t cast_time_ms = 0;    // Cast time in milliseconds
    uint32_t recovery_time_ms = 0; // Recovery time before next cast
    uint32_t recast_time_ms = 0;  // Cooldown time

    uint8_t duration_formula = 0; // Duration calculation formula
    uint32_t duration_cap = 0;    // Max duration in ticks
    uint32_t aoe_duration = 0;    // AoE duration

    // ========================================================================
    // Costs
    // ========================================================================

    int32_t mana_cost = 0;        // Mana required (can be negative for mana gain)
    int32_t endurance_cost = 0;   // Endurance required (post-Velious mostly)

    // ========================================================================
    // Targeting
    // ========================================================================

    SpellTargetType target_type = SpellTargetType::Single;
    float range = 0.0f;           // Maximum cast range
    float aoe_range = 0.0f;       // Radius for AE spells
    float push_back = 0.0f;       // Knockback force
    float push_up = 0.0f;         // Vertical knockback

    // ========================================================================
    // Classification
    // ========================================================================

    ResistType resist_type = ResistType::None;
    int16_t resist_diff = 0;      // Resist difficulty modifier

    CastingSkill casting_skill = CastingSkill::Alteration;
    uint8_t zone_type = 0;        // 0=indoor, 1=outdoor, 255=any
    uint8_t environment_type = 0; // Environment restrictions
    uint8_t time_of_day = 0;      // Time restrictions

    // ========================================================================
    // Spell Properties
    // ========================================================================

    bool is_beneficial = false;   // Beneficial to target
    bool activated = false;       // Requires activation
    bool allow_rest = false;      // Can med while active
    bool is_dispellable = true;   // Can be dispelled
    bool no_partial_resist = false; // All or nothing resist

    // ========================================================================
    // Animations & Icons
    // ========================================================================

    uint16_t cast_anim = 0;       // Casting animation ID
    uint16_t target_anim = 0;     // Target impact animation
    uint16_t spell_icon = 0;      // Spellbook icon
    uint16_t gem_icon = 0;        // Spell gem icon

    // ========================================================================
    // Class Requirements
    // Level at which each class can use (255 = can't use)
    // ========================================================================

    std::array<uint8_t, NUM_CLASSES> class_levels = {};

    // ========================================================================
    // Effects (up to 12 slots)
    // ========================================================================

    std::array<SpellEffectSlot, MAX_SPELL_EFFECTS> effects = {};

    // ========================================================================
    // Reagents (up to 4)
    // ========================================================================

    std::array<uint32_t, MAX_SPELL_REAGENTS> reagent_id = {};
    std::array<uint32_t, MAX_SPELL_REAGENTS> reagent_count = {};
    std::array<uint32_t, MAX_SPELL_REAGENTS> no_expend_reagent = {};

    // ========================================================================
    // Stacking
    // ========================================================================

    int16_t spell_group = 0;      // For stacking checks
    int16_t spell_rank = 0;       // Higher rank overwrites lower

    // ========================================================================
    // Helper Methods
    // ========================================================================

    // Check if spell is instant cast
    bool isInstantCast() const {
        return cast_time_ms == 0;
    }

    // Check if spell targets self only
    bool isSelfOnly() const {
        return target_type == SpellTargetType::Self;
    }

    // Check if spell is a group spell
    bool isGroupSpell() const {
        return target_type == SpellTargetType::GroupV1 ||
               target_type == SpellTargetType::GroupV2 ||
               target_type == SpellTargetType::GroupNoPets ||
               target_type == SpellTargetType::GroupedClients ||
               target_type == SpellTargetType::GroupClientsPets;
    }

    // Check if spell is an area effect
    bool isAESpell() const {
        return target_type == SpellTargetType::AECaster ||
               target_type == SpellTargetType::AETarget ||
               target_type == SpellTargetType::TargetAETap ||
               target_type == SpellTargetType::AEClientV1 ||
               target_type == SpellTargetType::AEBard ||
               target_type == SpellTargetType::DirectionalAE ||
               target_type == SpellTargetType::TargetRing ||
               aoe_range > 0;
    }

    // Check if spell is a Point Blank AE
    bool isPBAE() const {
        return target_type == SpellTargetType::AECaster;
    }

    // Check if spell is detrimental
    bool isDetrimental() const {
        return !is_beneficial;
    }

    // Check if spell has a duration (is a buff/debuff)
    bool hasDuration() const {
        return duration_formula > 0 || duration_cap > 0;
    }

    // Check if a class can use this spell at a given level
    bool canClassUse(PlayerClass pc, uint8_t level) const {
        uint8_t class_idx = static_cast<uint8_t>(pc);
        if (class_idx == 0 || class_idx > NUM_CLASSES) {
            return false;
        }
        uint8_t required = class_levels[class_idx - 1];
        return required != 255 && level >= required;
    }

    // Get minimum level required for a class
    uint8_t getClassLevel(PlayerClass pc) const {
        uint8_t class_idx = static_cast<uint8_t>(pc);
        if (class_idx == 0 || class_idx > NUM_CLASSES) {
            return 255;
        }
        return class_levels[class_idx - 1];
    }

    // Count valid effect slots
    int getEffectCount() const {
        int count = 0;
        for (const auto& effect : effects) {
            if (effect.isValid()) {
                count++;
            }
        }
        return count;
    }

    // Check if spell has a specific effect type
    bool hasEffect(SpellEffect effect_type) const {
        for (const auto& effect : effects) {
            if (effect.effect_id == effect_type) {
                return true;
            }
        }
        return false;
    }

    // Get the first effect slot with a specific effect type
    const SpellEffectSlot* getEffect(SpellEffect effect_type) const {
        for (const auto& effect : effects) {
            if (effect.effect_id == effect_type) {
                return &effect;
            }
        }
        return nullptr;
    }

    // Check if spell requires reagents
    bool requiresReagents() const {
        for (size_t i = 0; i < MAX_SPELL_REAGENTS; i++) {
            if (reagent_id[i] != 0 && reagent_count[i] > 0) {
                return true;
            }
        }
        return false;
    }

    // Check if this is likely a damage spell
    bool isDamageSpell() const {
        for (const auto& effect : effects) {
            if ((effect.effect_id == SpellEffect::CurrentHP ||
                 effect.effect_id == SpellEffect::CurrentHPOnce) &&
                effect.base_value > 0) {  // Positive = damage
                return true;
            }
        }
        return false;
    }

    // Check if this is likely a heal spell
    bool isHealSpell() const {
        for (const auto& effect : effects) {
            if ((effect.effect_id == SpellEffect::CurrentHP ||
                 effect.effect_id == SpellEffect::CurrentHPOnce ||
                 effect.effect_id == SpellEffect::HealOverTime ||
                 effect.effect_id == SpellEffect::CompleteHeal) &&
                effect.base_value < 0) {  // Negative = heal
                return true;
            }
        }
        return false;
    }

    // Check if this is a DoT spell
    bool isDoTSpell() const {
        return isDamageSpell() && hasDuration();
    }

    // Check if this is a buff spell (beneficial with duration)
    bool isBuffSpell() const {
        return is_beneficial && hasDuration();
    }

    // Get spell school from casting skill
    SpellSchool getSchool() const {
        switch (casting_skill) {
            case CastingSkill::Abjuration:
            case CastingSkill::SpecializeAbjure:
                return SpellSchool::Abjuration;
            case CastingSkill::Alteration:
            case CastingSkill::SpecializeAlteration:
                return SpellSchool::Alteration;
            case CastingSkill::Conjuration:
            case CastingSkill::SpecializeConjuration:
                return SpellSchool::Conjuration;
            case CastingSkill::Divination:
            case CastingSkill::SpecializeDivination:
                return SpellSchool::Divination;
            case CastingSkill::Evocation:
            case CastingSkill::SpecializeEvocation:
                return SpellSchool::Evocation;
            default:
                return SpellSchool::Alteration;
        }
    }

    // Calculate duration in ticks for a given caster level
    uint32_t calculateDuration(uint8_t caster_level) const;
};

// ============================================================================
// Duration Calculation Implementation
// ============================================================================

inline uint32_t SpellData::calculateDuration(uint8_t caster_level) const
{
    uint32_t duration = 0;

    switch (static_cast<DurationFormula>(duration_formula)) {
        case DurationFormula::None:
            duration = 0;
            break;

        case DurationFormula::LevelDiv2:
            duration = std::max(1u, static_cast<uint32_t>(caster_level / 2));
            break;

        case DurationFormula::LevelDiv2Plus5:
            duration = (caster_level / 2) + 5;
            break;

        case DurationFormula::Level30:
            duration = caster_level * 30;
            break;

        case DurationFormula::Fixed50:
            duration = 50;
            break;

        case DurationFormula::Fixed2:
            duration = 2;
            break;

        case DurationFormula::LevelDiv2Plus2:
            duration = (caster_level / 2) + 2;
            break;

        case DurationFormula::Fixed6:
            duration = 6;
            break;

        case DurationFormula::LevelPlus10:
            duration = caster_level + 10;
            break;

        case DurationFormula::Level2Plus10:
            duration = (caster_level * 2) + 10;
            break;

        case DurationFormula::Level3Plus10:
            duration = (caster_level * 3) + 10;
            break;

        case DurationFormula::Level3Plus30:
            duration = (caster_level + 3) * 30;
            break;

        case DurationFormula::LevelDiv4:
            duration = std::max(1u, static_cast<uint32_t>(caster_level / 4));
            break;

        case DurationFormula::Fixed1:
            duration = 1;
            break;

        case DurationFormula::LevelDiv3Plus5:
            duration = (caster_level / 3) + 5;
            break;

        case DurationFormula::Fixed0:
            duration = 0;
            break;

        case DurationFormula::Permanent:
            duration = 0xFFFFFFFF;  // "Permanent"
            break;

        case DurationFormula::MaxDuration:
            duration = duration_cap;
            break;

        default:
            // Unknown formula, use cap or default
            duration = duration_cap > 0 ? duration_cap : 0;
            break;
    }

    // Apply cap if set and not permanent
    if (duration_cap > 0 && duration != 0xFFFFFFFF && duration > duration_cap) {
        duration = duration_cap;
    }

    return duration;
}

} // namespace EQ

#endif // EQT_SPELL_DATA_H
