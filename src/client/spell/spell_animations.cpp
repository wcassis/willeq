/*
 * WillEQ Spell Animation Mapping Implementation
 */

#include "client/spell/spell_animations.h"

namespace EQ {

SpellAnimations::SpellAnimations()
{
    initAnimationMaps();
}

void SpellAnimations::initAnimationMaps()
{
    // ========================================================================
    // Casting Animations
    // Based on EQ client animation data
    // ========================================================================

    // Standard casting animations (arms raised, magic gestures)
    m_cast_anims[0] = ANIM_CAST_STANDARD;   // Default cast
    m_cast_anims[44] = ANIM_CAST_STANDARD;  // Standard cast (arms raised)
    m_cast_anims[45] = ANIM_CAST_CHANNEL;   // Channel cast (sustained)
    m_cast_anims[46] = ANIM_CAST_COMBAT;    // Combat cast (aggressive)
    m_cast_anims[47] = ANIM_CAST_STANDARD;  // Alternative standard
    m_cast_anims[48] = ANIM_CAST_CHANNEL;   // Alternative channel
    m_cast_anims[49] = ANIM_CAST_PRAYER;    // Prayer/meditation cast
    m_cast_anims[50] = ANIM_CAST_SUMMON;    // Summoning animation

    // Bard song animations
    m_cast_anims[70] = "s01";  // Singing/playing
    m_cast_anims[71] = "s02";  // Instrumental
    m_cast_anims[72] = "s03";  // Drum/percussion

    // Special casting animations
    m_cast_anims[100] = ANIM_CAST_COMBAT;   // Offensive spell
    m_cast_anims[101] = ANIM_CAST_PRAYER;   // Defensive spell
    m_cast_anims[102] = ANIM_CAST_SUMMON;   // Summoning spell

    // ========================================================================
    // Target/Impact Animations
    // ========================================================================

    m_target_anims[0] = "";                 // No animation
    m_target_anims[100] = ANIM_HIT_LIGHT;   // Light hit reaction
    m_target_anims[101] = ANIM_HIT_HEAVY;   // Heavy hit/stagger
    m_target_anims[102] = ANIM_FLINCH;      // Flinch
    m_target_anims[103] = ANIM_HIT_LIGHT;   // Spell impact light
    m_target_anims[104] = ANIM_HIT_HEAVY;   // Spell impact heavy

    // Buff/beneficial animations (subtle)
    m_target_anims[200] = "";               // No visible reaction
    m_target_anims[201] = ANIM_IDLE;        // Return to idle

    // ========================================================================
    // Animation Durations (in milliseconds)
    // ========================================================================

    // Casting animations - typically 1-3 seconds
    m_anim_durations[ANIM_CAST_STANDARD] = 1500;
    m_anim_durations[ANIM_CAST_CHANNEL] = 0;     // Looping/variable
    m_anim_durations[ANIM_CAST_COMBAT] = 1200;
    m_anim_durations[ANIM_CAST_PRAYER] = 2000;
    m_anim_durations[ANIM_CAST_SUMMON] = 2500;

    // Impact animations - short
    m_anim_durations[ANIM_HIT_LIGHT] = 300;
    m_anim_durations[ANIM_HIT_HEAVY] = 500;
    m_anim_durations[ANIM_FLINCH] = 250;

    // Bard songs - looping
    m_anim_durations["s01"] = 0;
    m_anim_durations["s02"] = 0;
    m_anim_durations["s03"] = 0;
}

std::string SpellAnimations::getCastAnimation(uint16_t cast_anim_id) const
{
    auto it = m_cast_anims.find(cast_anim_id);
    if (it != m_cast_anims.end()) {
        return it->second;
    }
    // Default to standard cast
    return ANIM_CAST_STANDARD;
}

std::string SpellAnimations::getTargetAnimation(uint16_t target_anim_id) const
{
    auto it = m_target_anims.find(target_anim_id);
    if (it != m_target_anims.end()) {
        return it->second;
    }
    // Default to light hit for non-zero IDs
    if (target_anim_id != 0) {
        return ANIM_HIT_LIGHT;
    }
    return "";
}

std::string SpellAnimations::getSchoolCastAnimation(SpellSchool school) const
{
    switch (school) {
        case SpellSchool::Abjuration:
            return ANIM_CAST_PRAYER;    // Defensive/protective pose
        case SpellSchool::Alteration:
            return ANIM_CAST_STANDARD;  // Standard casting
        case SpellSchool::Conjuration:
            return ANIM_CAST_SUMMON;    // Summoning/channeling
        case SpellSchool::Divination:
            return ANIM_CAST_PRAYER;    // Meditation pose
        case SpellSchool::Evocation:
            return ANIM_CAST_COMBAT;    // Combat/aggressive
        default:
            return ANIM_CAST_STANDARD;
    }
}

uint32_t SpellAnimations::getAnimationDuration(const std::string& anim_code) const
{
    auto it = m_anim_durations.find(anim_code);
    if (it != m_anim_durations.end()) {
        return it->second;
    }
    // Default duration for unknown animations
    return 1000;
}

bool SpellAnimations::isValidAnimation(const std::string& anim_code) const
{
    // Check if it's in our known animations
    for (const auto& pair : m_cast_anims) {
        if (pair.second == anim_code) return true;
    }
    for (const auto& pair : m_target_anims) {
        if (pair.second == anim_code) return true;
    }
    // Also accept standard animation code format (letter + 2 digits)
    if (anim_code.length() == 3 &&
        std::isalpha(anim_code[0]) &&
        std::isdigit(anim_code[1]) &&
        std::isdigit(anim_code[2])) {
        return true;
    }
    return false;
}

} // namespace EQ
