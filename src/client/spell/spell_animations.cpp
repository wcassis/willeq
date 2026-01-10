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
    // Based on EQ pre-Luclin animation reference
    // Correct sequence: t04 (start) -> t05 (channel/loop) -> t06 (finish)
    // ========================================================================

    // Standard casting animations - use t04 (cast pullback/start)
    m_cast_anims[0] = ANIM_CAST_START;      // Default cast -> t04
    m_cast_anims[44] = ANIM_CAST_START;     // Standard cast -> t04
    m_cast_anims[45] = ANIM_CAST_LOOP;      // Channel cast (sustained) -> t05
    m_cast_anims[46] = ANIM_CAST_START;     // Combat cast -> t04
    m_cast_anims[47] = ANIM_CAST_START;     // Alternative standard -> t04
    m_cast_anims[48] = ANIM_CAST_LOOP;      // Alternative channel -> t05
    m_cast_anims[49] = ANIM_CAST_START;     // Prayer/meditation cast -> t04
    m_cast_anims[50] = ANIM_CAST_START;     // Summoning animation -> t04

    // Bard song animations - use correct instrument animations
    m_cast_anims[70] = ANIM_STRINGED_INSTRUMENT;  // Singing/playing stringed -> t02
    m_cast_anims[71] = ANIM_WIND_INSTRUMENT;      // Wind/brass instrument -> t03
    m_cast_anims[72] = ANIM_STRINGED_INSTRUMENT;  // Percussion (uses stringed) -> t02

    // Special casting animations
    m_cast_anims[100] = ANIM_CAST_START;    // Offensive spell -> t04
    m_cast_anims[101] = ANIM_CAST_START;    // Defensive spell -> t04
    m_cast_anims[102] = ANIM_CAST_START;    // Summoning spell -> t04

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

    // Casting animations - t04/t05/t06 sequence
    m_anim_durations[ANIM_CAST_START] = 500;     // t04 - quick pullback
    m_anim_durations[ANIM_CAST_LOOP] = 0;        // t05 - looping/variable duration
    m_anim_durations[ANIM_CAST_END] = 400;       // t06 - quick finish

    // Impact animations - short
    m_anim_durations[ANIM_HIT_LIGHT] = 300;      // d01
    m_anim_durations[ANIM_HIT_HEAVY] = 500;      // d02

    // Bard instrument animations - looping
    m_anim_durations[ANIM_STRINGED_INSTRUMENT] = 0;  // t02 - looping
    m_anim_durations[ANIM_WIND_INSTRUMENT] = 0;      // t03 - looping
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
