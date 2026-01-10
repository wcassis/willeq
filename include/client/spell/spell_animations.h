/*
 * WillEQ Spell Animation Mapping
 *
 * Maps spell animation IDs to animation codes used by the skeletal animator.
 * Covers Classic through Velious spell animations.
 */

#ifndef EQT_SPELL_ANIMATIONS_H
#define EQT_SPELL_ANIMATIONS_H

#include "spell_constants.h"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace EQ {

// Animation categories for spells
enum class SpellAnimCategory : uint8_t {
    None = 0,       // No animation
    Cast,           // Casting pose
    Impact,         // Hit by spell
    Buff,           // Receiving beneficial spell
    Debuff,         // Receiving harmful spell
    Channel,        // Channeled spell
};

class SpellAnimations {
public:
    SpellAnimations();
    ~SpellAnimations() = default;

    // Get animation code for spell casting
    // cast_anim_id: from SpellData::cast_anim
    // Returns animation code like "c01", "c02", etc.
    std::string getCastAnimation(uint16_t cast_anim_id) const;

    // Get animation code for spell impact on target
    // target_anim_id: from SpellData::target_anim
    std::string getTargetAnimation(uint16_t target_anim_id) const;

    // Get default cast animation for a spell school
    std::string getSchoolCastAnimation(SpellSchool school) const;

    // Get animation duration estimate in milliseconds
    // Returns 0 if animation is looping/unknown
    uint32_t getAnimationDuration(const std::string& anim_code) const;

    // Check if an animation code exists
    bool isValidAnimation(const std::string& anim_code) const;

    // Common animation codes - using correct EQ pre-Luclin animation codes
    static constexpr const char* ANIM_IDLE = "p01";
    // Casting animations (t04-t06 sequence)
    static constexpr const char* ANIM_CAST_START = "t04";      // Cast Pull Back (start)
    static constexpr const char* ANIM_CAST_LOOP = "t05";       // Cast Arms Loop (channeling)
    static constexpr const char* ANIM_CAST_END = "t06";        // Cast Push Forward (complete)
    // Aliases for different casting styles (all use same animation code)
    static constexpr const char* ANIM_CAST_STANDARD = "t04";   // Standard cast (pullback)
    static constexpr const char* ANIM_CAST_CHANNEL = "t05";    // Channeled spell (loop)
    static constexpr const char* ANIM_CAST_COMBAT = "t04";     // Combat cast (same as standard)
    static constexpr const char* ANIM_CAST_PRAYER = "t04";     // Prayer cast (same as standard)
    static constexpr const char* ANIM_CAST_SUMMON = "t04";     // Summoning (same as standard)
    // Damage reaction animations
    static constexpr const char* ANIM_HIT_LIGHT = "d01";       // Minor damage
    static constexpr const char* ANIM_HIT_HEAVY = "d02";       // Heavy damage
    static constexpr const char* ANIM_FLINCH = "d01";          // Flinch (same as minor damage)
    // Bard instrument animations
    static constexpr const char* ANIM_STRINGED_INSTRUMENT = "t02";  // Stringed instrument
    static constexpr const char* ANIM_WIND_INSTRUMENT = "t03";      // Wind/brass instrument

private:
    void initAnimationMaps();

    // Maps cast_anim_id to animation code
    std::unordered_map<uint16_t, std::string> m_cast_anims;

    // Maps target_anim_id to animation code
    std::unordered_map<uint16_t, std::string> m_target_anims;

    // Animation durations in milliseconds
    std::unordered_map<std::string, uint32_t> m_anim_durations;
};

} // namespace EQ

#endif // EQT_SPELL_ANIMATIONS_H
