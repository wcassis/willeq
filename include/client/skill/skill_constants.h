/*
 * WillEQ Skill Constants
 *
 * Defines skill categories, skill metadata, and helper functions
 * for the player skill system.
 */

#pragma once

#include <cstdint>
#include <string>
#include "client/spell/spell_constants.h"  // For CastingSkill enum

namespace EQ {

// Maximum number of skills in the player profile
static constexpr uint32_t MAX_SKILL_ID = 75;

// ============================================================================
// Skill Categories
// ============================================================================

enum class SkillCategory : uint8_t {
    Combat,     // Bash, Kick, Backstab, etc.
    Defense,    // Block, Dodge, Parry, Riposte
    Weapon,     // 1H Blunt, 2H Slash, Archery, etc.
    Magic,      // Specializations, Channeling, Meditation
    Utility,    // Forage, Hide, Sneak, Tracking, etc.
    Trade,      // Blacksmithing, Tailoring, etc.
    Unknown
};

// ============================================================================
// Skill Category Mapping
// ============================================================================

inline SkillCategory getSkillCategory(uint8_t skill_id) {
    switch (skill_id) {
        // Combat skills
        case 6:  // ApplyPoison
        case 8:  // Backstab
        case 10: // Bash
        case 16: // Disarm
        case 20: // DoubleAttack
        case 21: // DragonPunch
        case 22: // DualWield
        case 23: // EagleStrike
        case 26: // FlyingKick
        case 30: // Kick
        case 38: // RoundKick
        case 52: // TigerClaw
        case 71: // Intimidation
        case 72: // Berserking
        case 73: // Taunt
            return SkillCategory::Combat;

        // Defense skills
        case 11: // Block
        case 15: // Defense
        case 19: // Dodge
        case 34: // Parry
        case 37: // Riposte
        case 39: // SafeFall
            return SkillCategory::Defense;

        // Weapon skills
        case 0:  // OneHandBlunt
        case 1:  // OneHandSlash
        case 2:  // TwoHandBlunt
        case 3:  // TwoHandSlash
        case 7:  // Archery
        case 28: // HandToHand
        case 36: // Piercing
        case 51: // Throwing
            return SkillCategory::Weapon;

        // Magic skills
        case 4:  // Abjuration
        case 5:  // Alteration
        case 13: // Channeling
        case 14: // Conjuration
        case 18: // Divination
        case 24: // Evocation
        case 31: // Meditate
        case 43: // SpecializeAbjure
        case 44: // SpecializeAlteration
        case 45: // SpecializeConjuration
        case 46: // SpecializeDivination
        case 47: // SpecializeEvocation
        case 58: // Research
            return SkillCategory::Magic;

        // Utility skills
        case 9:  // BindWound
        case 17: // DisarmTraps
        case 25: // FeignDeath
        case 27: // Forage
        case 29: // Hide
        case 32: // Mend
        case 33: // Offense
        case 35: // PickLock
        case 40: // SenseHeading
        case 42: // Sneak
        case 48: // PickPocket
        case 50: // Swimming
        case 53: // Tracking
        case 55: // Fishing
        case 62: // SenseTraps
        case 66: // AlcoholTolerance
        case 67: // Begging
            return SkillCategory::Utility;

        // Trade skills
        case 56: // MakePoison
        case 57: // Tinkering
        case 59: // Alchemy
        case 60: // Baking
        case 61: // Tailoring
        case 63: // Blacksmithing
        case 64: // Fletching
        case 65: // Brewing
        case 68: // JewelryMaking
        case 69: // Pottery
            return SkillCategory::Trade;

        // Instrument skills (Bard - could be Magic or Utility)
        case 12: // BrassInst
        case 41: // Singing
        case 49: // StringedInst
        case 54: // WindInst
        case 70: // PercussionInst
        case 74: // SingingSkill
            return SkillCategory::Magic;

        default:
            return SkillCategory::Unknown;
    }
}

// ============================================================================
// Skill Category Names
// ============================================================================

inline const char* getSkillCategoryName(SkillCategory category) {
    switch (category) {
        case SkillCategory::Combat:  return "Combat";
        case SkillCategory::Defense: return "Defense";
        case SkillCategory::Weapon:  return "Weapon";
        case SkillCategory::Magic:   return "Magic";
        case SkillCategory::Utility: return "Utility";
        case SkillCategory::Trade:   return "Trade";
        default:                     return "Unknown";
    }
}

// ============================================================================
// Skill Names
// ============================================================================

inline const char* getSkillName(uint8_t skill_id) {
    switch (skill_id) {
        case 0:  return "1H Blunt";
        case 1:  return "1H Slash";
        case 2:  return "2H Blunt";
        case 3:  return "2H Slash";
        case 4:  return "Abjuration";
        case 5:  return "Alteration";
        case 6:  return "Apply Poison";
        case 7:  return "Archery";
        case 8:  return "Backstab";
        case 9:  return "Bind Wound";
        case 10: return "Bash";
        case 11: return "Block";
        case 12: return "Brass Instruments";
        case 13: return "Channeling";
        case 14: return "Conjuration";
        case 15: return "Defense";
        case 16: return "Disarm";
        case 17: return "Disarm Traps";
        case 18: return "Divination";
        case 19: return "Dodge";
        case 20: return "Double Attack";
        case 21: return "Dragon Punch";
        case 22: return "Dual Wield";
        case 23: return "Eagle Strike";
        case 24: return "Evocation";
        case 25: return "Feign Death";
        case 26: return "Flying Kick";
        case 27: return "Forage";
        case 28: return "Hand to Hand";
        case 29: return "Hide";
        case 30: return "Kick";
        case 31: return "Meditate";
        case 32: return "Mend";
        case 33: return "Offense";
        case 34: return "Parry";
        case 35: return "Pick Lock";
        case 36: return "Piercing";
        case 37: return "Riposte";
        case 38: return "Round Kick";
        case 39: return "Safe Fall";
        case 40: return "Sense Heading";
        case 41: return "Singing";
        case 42: return "Sneak";
        case 43: return "Specialize Abjure";
        case 44: return "Specialize Alteration";
        case 45: return "Specialize Conjuration";
        case 46: return "Specialize Divination";
        case 47: return "Specialize Evocation";
        case 48: return "Pick Pocket";
        case 49: return "Stringed Instruments";
        case 50: return "Swimming";
        case 51: return "Throwing";
        case 52: return "Tiger Claw";
        case 53: return "Tracking";
        case 54: return "Wind Instruments";
        case 55: return "Fishing";
        case 56: return "Make Poison";
        case 57: return "Tinkering";
        case 58: return "Research";
        case 59: return "Alchemy";
        case 60: return "Baking";
        case 61: return "Tailoring";
        case 62: return "Sense Traps";
        case 63: return "Blacksmithing";
        case 64: return "Fletching";
        case 65: return "Brewing";
        case 66: return "Alcohol Tolerance";
        case 67: return "Begging";
        case 68: return "Jewelry Making";
        case 69: return "Pottery";
        case 70: return "Percussion Instruments";
        case 71: return "Intimidation";
        case 72: return "Berserking";
        case 73: return "Taunt";
        case 74: return "Singing";
        default: return "Unknown";
    }
}

// ============================================================================
// Activatable Skills
// ============================================================================

// Returns true if the skill can be manually activated by the player
inline bool isActivatableSkill(uint8_t skill_id) {
    switch (skill_id) {
        case 6:  // ApplyPoison
        case 8:  // Backstab
        case 9:  // BindWound
        case 10: // Bash
        case 16: // Disarm
        case 17: // DisarmTraps
        case 21: // DragonPunch
        case 23: // EagleStrike
        case 25: // FeignDeath
        case 26: // FlyingKick
        case 27: // Forage
        case 29: // Hide
        case 30: // Kick
        case 31: // Meditate
        case 32: // Mend
        case 35: // PickLock
        case 38: // RoundKick
        case 40: // SenseHeading
        case 42: // Sneak
        case 48: // PickPocket
        case 52: // TigerClaw
        case 53: // Tracking
        case 55: // Fishing
        case 62: // SenseTraps
        case 67: // Begging
        case 71: // Intimidation
        case 72: // Berserking
        case 73: // Taunt
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Skill Animation IDs
// ============================================================================

// Animation IDs used by the server emote system
// These map to animation codes in ZoneProcessEmote
enum SkillAnimationId : uint8_t {
    ANIM_ID_ATTACK_PRIMARY = 1,   // Primary attack (weapon-based)
    ANIM_ID_ATTACK_SECONDARY = 2, // Secondary attack (offhand)
    ANIM_ID_DAMAGE_FRONT = 3,     // Damage from front
    ANIM_ID_DAMAGE_BACK = 4,      // Damage from back
    ANIM_ID_ATTACK = 5,           // Generic attack
    ANIM_ID_ATTACK2 = 6,          // Generic attack 2
    ANIM_ID_ROUND_KICK = 10,      // Round kick → c11
    ANIM_ID_KICK = 11,            // Kick → c01
    ANIM_ID_BASH = 12,            // Bash → c07
    ANIM_ID_FLYING_KICK = 14,     // Flying kick → t07
    ANIM_ID_DEATH = 16,           // Death → d05
    ANIM_ID_LOOT = 105,           // Looting → p05
};

// Returns the animation ID to play when activating a skill
// Returns 0 if no animation should be played
inline uint8_t getSkillAnimationId(uint8_t skill_id) {
    switch (skill_id) {
        // Monk special attacks
        case 21: // DragonPunch - uses custom animation (t09)
        case 23: // EagleStrike - uses custom animation (t08)
        case 52: // TigerClaw - uses custom animation (t08)
            return ANIM_ID_ATTACK;  // Server sends attack, client overrides with skill-specific anim

        // Kick skills
        case 30: // Kick
            return ANIM_ID_KICK;     // 11 → c01
        case 26: // FlyingKick
            return ANIM_ID_FLYING_KICK;  // 14 → t07
        case 38: // RoundKick
            return ANIM_ID_ROUND_KICK;   // 10 → c11

        // Weapon skills
        case 8:  // Backstab
            return ANIM_ID_ATTACK;  // Uses weapon attack animation (c02 piercing)
        case 10: // Bash
            return ANIM_ID_BASH;    // 12 → c07

        // Other combat skills
        case 73: // Taunt
            return ANIM_ID_KICK;    // Uses kick animation as taunt gesture

        // Death/Feign
        case 25: // FeignDeath
            return ANIM_ID_DEATH;   // 16 → d05

        default:
            return 0;   // No animation
    }
}

// Returns the skeletal animation code for a skill
// Animation codes are based on eq_old_model_animations.md reference
inline const char* getSkillAnimationCode(uint8_t skill_id) {
    switch (skill_id) {
        // Combat skills - Monk special attacks
        case 21: // DragonPunch
            return "t09";  // Dragon Punch / Large Punch
        case 23: // EagleStrike
            return "t08";  // Tiger Strike / Rapid Punches (similar rapid strike)
        case 26: // FlyingKick
            return "t07";  // Flying Kick
        case 52: // TigerClaw
            return "t08";  // Tiger Strike / Rapid Punches

        // Combat skills - Basic kicks
        case 30: // Kick
            return "c01";  // Kick (c01 is specifically the kick animation)
        case 38: // RoundKick
            return "c11";  // Round Kick

        // Combat skills - Weapon-based
        case 8:  // Backstab
            return "c02";  // 1H Piercing attack
        case 10: // Bash
            return "c07";  // Bash (Shield)

        // Combat skills - Other
        case 73: // Taunt
            return "c01";  // Combat gesture (kick motion works for taunt)

        // Death/Feign
        case 25: // FeignDeath
            return "d05";  // Death/lying animation

        // Bard instrument animations
        case 12: // BrassInstruments
            return "t03";  // Wind Instrument (brass uses same animation)
        case 49: // StringedInstruments
            return "t02";  // Stringed Instrument
        case 54: // WindInstruments
            return "t03";  // Wind Instrument
        case 70: // PercussionInstruments
            return "t02";  // Stringed Instrument (closest available)
        case 41: // Singing
        case 74: // SingingSkill
            return "t02";  // Stringed Instrument (vocal performance pose)

        // Social/utility skill animations
        case 67: // Begging
            return "s08";  // Plead
        case 71: // Intimidation
            return "s18";  // Glare

        // Utility skills with animations
        case 29: // Hide
            return "l08";  // Crouching
        case 42: // Sneak
            return "l06";  // Crouch Walk
        case 31: // Meditate
            return "p07";  // Sitting (meditation pose)

        default:
            return nullptr;  // No animation
    }
}

// ============================================================================
// Skill Recast Times (in milliseconds)
// Base values from EQEmu server (features.h) - server applies -1 second and
// haste modifier, which is handled in getAdjustedSkillRecastTime()
// ============================================================================

inline uint32_t getSkillBaseRecastTime(uint8_t skill_id) {
    switch (skill_id) {
        case 8:  // Backstab
            return 9000;  // 9 seconds (BackstabReuseTime)
        case 9:  // BindWound
            return 1000;  // 1 second
        case 10: // Bash
            return 5000;  // 5 seconds (BashReuseTime)
        case 21: // DragonPunch
            return 6000;  // 6 seconds (TailRakeReuseTime)
        case 23: // EagleStrike
            return 5000;  // 5 seconds (EagleStrikeReuseTime)
        case 25: // FeignDeath
            return 9000;  // 9 seconds (FeignDeathReuseTime)
        case 26: // FlyingKick
            return 7000;  // 7 seconds (FlyingKickReuseTime)
        case 27: // Forage
            return 10000; // 10 seconds
        case 29: // Hide
            return 8000;  // 8 seconds (HideReuseTime)
        case 30: // Kick
            return 5000;  // 5 seconds (KickReuseTime)
        case 31: // Meditate
            return 0;     // Toggle
        case 32: // Mend
            return 360000; // 6 minutes (MendReuseTime)
        case 35: // PickLock
            return 1000;  // 1 second
        case 38: // RoundKick
            return 9000;  // 9 seconds (RoundKickReuseTime)
        case 42: // Sneak
            return 7000;  // 7 seconds (SneakReuseTime)
        case 48: // PickPocket
            return 8000;  // 8 seconds
        case 52: // TigerClaw
            return 6000;  // 6 seconds (TigerClawReuseTime)
        case 53: // Tracking
            return 0;     // Opens window
        case 55: // Fishing
            return 0;     // Channel-based
        case 67: // Begging
            return 10000; // 10 seconds
        case 73: // Taunt
            return 5000;  // 5 seconds (TauntReuseTime)
        case 74: // Frenzy
            return 10000; // 10 seconds (FrenzyReuseTime)
        default:
            return 0;
    }
}

// Calculate adjusted recast time with haste modifier
// Server formula: reuse_time = (BaseTime - 1) * haste_modifier / 100
// haste_modifier = 10000 / (100 + haste) for positive haste
// haste_modifier = (100 - haste) for negative haste (slow)
// skillReduction is from items/AAs that reduce skill recast times (not currently tracked)
inline uint32_t getAdjustedSkillRecastTime(uint8_t skill_id, int32_t haste, int32_t skillReduction = 0) {
    uint32_t baseTime = getSkillBaseRecastTime(skill_id);
    if (baseTime == 0) {
        return 0;
    }

    // Server subtracts 1 second from base time before applying modifiers
    int32_t recastTime = static_cast<int32_t>(baseTime) - 1000 - (skillReduction * 1000);
    if (recastTime < 0) {
        recastTime = 0;
    }

    // Apply haste modifier
    int32_t hasteModifier;
    if (haste >= 0) {
        // Positive haste reduces cooldown
        hasteModifier = 10000 / (100 + haste);
    } else {
        // Negative haste (slow) increases cooldown
        hasteModifier = 100 - haste;
    }

    recastTime = (recastTime * hasteModifier) / 100;

    // Minimum 1 second cooldown for skills that have a base cooldown
    if (recastTime < 1000 && baseTime > 0) {
        recastTime = 1000;
    }

    return static_cast<uint32_t>(recastTime);
}

// Legacy function for backward compatibility - returns base time without modifiers
inline uint32_t getSkillRecastTime(uint8_t skill_id) {
    return getSkillBaseRecastTime(skill_id);
}

} // namespace EQ
