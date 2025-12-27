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

    // Common animation codes
    static constexpr const char* ANIM_IDLE = "p01";
    static constexpr const char* ANIM_CAST_STANDARD = "c01";
    static constexpr const char* ANIM_CAST_CHANNEL = "c02";
    static constexpr const char* ANIM_CAST_COMBAT = "c03";
    static constexpr const char* ANIM_CAST_PRAYER = "c04";
    static constexpr const char* ANIM_CAST_SUMMON = "c05";
    static constexpr const char* ANIM_HIT_LIGHT = "h01";
    static constexpr const char* ANIM_HIT_HEAVY = "h02";
    static constexpr const char* ANIM_FLINCH = "d01";

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
