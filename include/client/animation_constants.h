/*
 * WillEQ Animation Constants
 *
 * Defines animation code mappings for EverQuest pre-Luclin models.
 * Based on eq_old_model_animations.md reference document.
 */

#pragma once

#include <cstdint>

namespace EQ {

// ============================================================================
// Combat Animation Codes
// ============================================================================

// Primary combat animations (c01-c11)
constexpr const char* ANIM_KICK         = "c01";  // Kick
constexpr const char* ANIM_1H_PIERCE    = "c02";  // 1H Piercing
constexpr const char* ANIM_2H_SLASH     = "c03";  // 2H Slash
constexpr const char* ANIM_2H_BLUNT     = "c04";  // 2H Blunt
constexpr const char* ANIM_THROWING     = "c05";  // Throwing
constexpr const char* ANIM_1H_SLASH     = "c06";  // 1H Slash (also used for offhand)
constexpr const char* ANIM_BASH         = "c07";  // Bash (Shield)
constexpr const char* ANIM_HAND_TO_HAND = "c08";  // Hand to Hand
constexpr const char* ANIM_ARCHERY      = "c09";  // Archery
constexpr const char* ANIM_SWIM_ATTACK  = "c10";  // Swim Attack
constexpr const char* ANIM_ROUND_KICK   = "c11";  // Round Kick

// ============================================================================
// Damage Reaction Animation Codes
// ============================================================================

constexpr const char* ANIM_DAMAGE_MINOR   = "d01";  // Minor Damage
constexpr const char* ANIM_DAMAGE_HEAVY   = "d02";  // Heavy Damage
constexpr const char* ANIM_DAMAGE_TRAP    = "d03";  // Damage from Trap
constexpr const char* ANIM_DAMAGE_DOT     = "d04";  // Drowning/Burning (DoT)
constexpr const char* ANIM_DEATH          = "d05";  // Death/Dying

// ============================================================================
// Special/Skill Animation Codes
// ============================================================================

constexpr const char* ANIM_STRINGED_INST  = "t02";  // Stringed Instrument
constexpr const char* ANIM_WIND_INST      = "t03";  // Wind Instrument
constexpr const char* ANIM_CAST_PULLBACK  = "t04";  // Cast Pull Back (start)
constexpr const char* ANIM_CAST_LOOP      = "t05";  // Cast Arms Loop (channeling)
constexpr const char* ANIM_CAST_FORWARD   = "t06";  // Cast Push Forward (finish)
constexpr const char* ANIM_FLYING_KICK    = "t07";  // Flying Kick
constexpr const char* ANIM_TIGER_STRIKE   = "t08";  // Tiger Strike / Rapid Punches
constexpr const char* ANIM_DRAGON_PUNCH   = "t09";  // Dragon Punch / Large Punch

// ============================================================================
// Locomotion Animation Codes
// ============================================================================

constexpr const char* ANIM_WALK           = "l01";  // Walk
constexpr const char* ANIM_RUN            = "l02";  // Run
constexpr const char* ANIM_JUMP_RUNNING   = "l03";  // Jump (Running)
constexpr const char* ANIM_JUMP_STANDING  = "l04";  // Jump (Standing)
constexpr const char* ANIM_FALLING        = "l05";  // Falling
constexpr const char* ANIM_CROUCH_WALK    = "l06";  // Crouch Walk
constexpr const char* ANIM_CLIMBING       = "l07";  // Climbing
constexpr const char* ANIM_CROUCHING      = "l08";  // Crouching
constexpr const char* ANIM_SWIM_TREADING  = "l09";  // Swim Treading

// ============================================================================
// Idle/Pose Animation Codes
// ============================================================================

constexpr const char* ANIM_IDLE           = "o01";  // Idle
constexpr const char* ANIM_IDLE_ARMS_DOWN = "o02";  // Idle (Arms at Sides)
constexpr const char* ANIM_IDLE_SITTING   = "o03";  // Idle (Sitting)
constexpr const char* ANIM_POSE_STAND     = "p01";  // Stand
constexpr const char* ANIM_POSE_SIT       = "p02";  // Sit Down / Stand Up
constexpr const char* ANIM_POSE_SHUFFLE   = "p03";  // Shuffle Feet (Rotating)
constexpr const char* ANIM_POSE_FLOAT     = "p04";  // Float/Walk/Strafe
constexpr const char* ANIM_POSE_KNEEL     = "p05";  // Kneel (Loot)
constexpr const char* ANIM_POSE_SWIM_IDLE = "p06";  // Swimming Idle
constexpr const char* ANIM_POSE_SITTING   = "p07";  // Sitting
constexpr const char* ANIM_POSE_STAND_ALT = "p08";  // Stand (Arms at Sides)

// ============================================================================
// Emote/Social Animation Codes
// ============================================================================

constexpr const char* ANIM_EMOTE_CHEER       = "s01";  // Cheer
constexpr const char* ANIM_EMOTE_MOURN       = "s02";  // Mourn/Disappointed
constexpr const char* ANIM_EMOTE_WAVE        = "s03";  // Wave
constexpr const char* ANIM_EMOTE_RUDE        = "s04";  // Rude
constexpr const char* ANIM_EMOTE_YAWN        = "s05";  // Yawn
constexpr const char* ANIM_EMOTE_NOD         = "s06";  // Nod
constexpr const char* ANIM_EMOTE_AMAZED      = "s07";  // Amazed
constexpr const char* ANIM_EMOTE_PLEAD       = "s08";  // Plead
constexpr const char* ANIM_EMOTE_CLAP        = "s09";  // Clap
constexpr const char* ANIM_EMOTE_DISTRESS    = "s10";  // Distress/Hungry
constexpr const char* ANIM_EMOTE_BLUSH       = "s11";  // Blush
constexpr const char* ANIM_EMOTE_CHUCKLE     = "s12";  // Chuckle
constexpr const char* ANIM_EMOTE_BURP        = "s13";  // Burp/Cough
constexpr const char* ANIM_EMOTE_DUCK        = "s14";  // Duck
constexpr const char* ANIM_EMOTE_PUZZLE      = "s15";  // Look Around/Puzzle
constexpr const char* ANIM_EMOTE_DANCE       = "s16";  // Dance
constexpr const char* ANIM_EMOTE_BLINK       = "s17";  // Blink
constexpr const char* ANIM_EMOTE_GLARE       = "s18";  // Glare
constexpr const char* ANIM_EMOTE_DROOL       = "s19";  // Drool
constexpr const char* ANIM_EMOTE_KNEEL       = "s20";  // Kneel
constexpr const char* ANIM_EMOTE_LAUGH       = "s21";  // Laugh
constexpr const char* ANIM_EMOTE_POINT       = "s22";  // Point
constexpr const char* ANIM_EMOTE_SHRUG       = "s23";  // Ponder/Shrug
constexpr const char* ANIM_EMOTE_READY       = "s24";  // Ready
constexpr const char* ANIM_EMOTE_SALUTE      = "s25";  // Salute
constexpr const char* ANIM_EMOTE_SHIVER      = "s26";  // Shiver
constexpr const char* ANIM_EMOTE_TAP_FOOT    = "s27";  // Tap Foot
constexpr const char* ANIM_EMOTE_BOW         = "s28";  // Bow

// ============================================================================
// Weapon Skill Type to Animation Mapping
// ============================================================================

// Weapon skill types (matches WeaponSkillType enum from inventory_constants.h)
enum WeaponSkillTypeAnim : uint8_t {
    WEAPON_1H_SLASHING = 0,
    WEAPON_2H_SLASHING = 1,
    WEAPON_PIERCING = 2,
    WEAPON_1H_BLUNT = 3,
    WEAPON_2H_BLUNT = 4,
    WEAPON_ARCHERY = 5,
    WEAPON_THROWING = 6,
    WEAPON_HAND_TO_HAND = 7,
    WEAPON_2H_PIERCING = 35,
    WEAPON_NONE = 255
};

/**
 * Get the attack animation code for a given weapon skill type.
 *
 * @param weaponSkillType The weapon skill type (from item data or WeaponSkillType enum)
 * @param isOffhand True if this is an offhand attack
 * @param isSwimming True if the entity is underwater
 * @return The animation code string (e.g., "c02" for piercing)
 */
inline const char* getWeaponAttackAnimation(uint8_t weaponSkillType, bool isOffhand = false, bool isSwimming = false) {
    // Swimming uses special swim attack animation
    if (isSwimming) {
        return ANIM_SWIM_ATTACK;  // c10
    }

    switch (weaponSkillType) {
        case WEAPON_1H_SLASHING:
            return ANIM_1H_SLASH;       // c06 - 1H Slash
        case WEAPON_2H_SLASHING:
            return ANIM_2H_SLASH;       // c03 - 2H Slash
        case WEAPON_PIERCING:
        case WEAPON_2H_PIERCING:
            return ANIM_1H_PIERCE;      // c02 - Piercing
        case WEAPON_1H_BLUNT:
            return ANIM_1H_SLASH;       // c06 - Uses same as 1H slash
        case WEAPON_2H_BLUNT:
            return ANIM_2H_BLUNT;       // c04 - 2H Blunt
        case WEAPON_ARCHERY:
            return ANIM_ARCHERY;        // c09 - Archery
        case WEAPON_THROWING:
            return ANIM_THROWING;       // c05 - Throwing
        case WEAPON_HAND_TO_HAND:
            return ANIM_HAND_TO_HAND;   // c08 - Hand to Hand
        case WEAPON_NONE:
        default:
            // No weapon or unknown - use hand to hand
            return ANIM_HAND_TO_HAND;   // c08
    }
}

/**
 * Get the animation code for a combat skill.
 *
 * @param skillId The skill ID (from skill_constants.h)
 * @return The animation code string, or nullptr if no animation
 */
inline const char* getCombatSkillAnimation(uint8_t skillId) {
    switch (skillId) {
        case 8:   // Backstab
            return ANIM_1H_PIERCE;      // c02 - Piercing attack
        case 10:  // Bash
            return ANIM_BASH;           // c07 - Shield bash
        case 21:  // DragonPunch
            return ANIM_DRAGON_PUNCH;   // t09 - Dragon punch
        case 23:  // EagleStrike
            return ANIM_TIGER_STRIKE;   // t08 - Similar rapid strike
        case 25:  // FeignDeath
            return ANIM_DEATH;          // d05 - Death/lying animation
        case 26:  // FlyingKick
            return ANIM_FLYING_KICK;    // t07 - Flying kick
        case 30:  // Kick
            return ANIM_KICK;           // c01 - Basic kick
        case 38:  // RoundKick
            return ANIM_ROUND_KICK;     // c11 - Round kick
        case 52:  // TigerClaw
            return ANIM_TIGER_STRIKE;   // t08 - Tiger strike / rapid punches
        case 73:  // Taunt
            return ANIM_KICK;           // c01 - Combat gesture
        default:
            return nullptr;             // No animation for this skill
    }
}

/**
 * Get the damage reaction animation based on damage percentage.
 *
 * @param damagePercent Damage as percentage of max HP (0-100)
 * @param isDrowning True if drowning damage
 * @param isTrap True if trap damage
 * @return The animation code string
 */
inline const char* getDamageAnimation(float damagePercent, bool isDrowning = false, bool isTrap = false) {
    if (isDrowning) {
        return ANIM_DAMAGE_DOT;         // d04 - Drowning/burning
    }
    if (isTrap) {
        return ANIM_DAMAGE_TRAP;        // d03 - Trap damage
    }
    if (damagePercent >= 10.0f) {
        return ANIM_DAMAGE_HEAVY;       // d02 - Heavy damage (10%+ of max HP)
    }
    return ANIM_DAMAGE_MINOR;           // d01 - Minor damage
}

} // namespace EQ
