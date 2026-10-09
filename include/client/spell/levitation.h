#ifndef EQT_SPELL_LEVITATION_H
#define EQT_SPELL_LEVITATION_H

// B892 (GitLab #208): the player's own levitation.
//
// eqemu sends the caster the levitate appearance with ignore_self
// (akk-stack/code/zone/spell_effects.cpp:1452-1455,
// SendAppearancePacket(FlyMode, Levitating, true, true)), so the caster's
// own client never receives AT_FLYMODE 2 and the player never floats. The
// official client derives ITS OWN levitation from its buff list: any active
// buff whose spell carries SpellEffect::Levitate (SPA 57,
// spell_constants.h). This file is that predicate -- the first reader of SPA
// 57 in willeq -- and the ONE table of its two motion numbers.
//
// [assumed] until the lane-1 oracle capture lands (refactor/B892/README.md):
// the numbers below are the classic client's behaviour as observed, marked
// assumed because no Z trace has been recorded yet. When measured, the
// values are updated HERE and the mark changes to [measured] in the README;
// nothing else names them.

#include "client/spell/buff_manager.h"
#include "client/spell/spell_database.h"

#include <cstdint>
#include <vector>

namespace EQ {

enum class LevitationState {
    NotLevitating,
    Levitating,
};

// The hover line above the floor, in EQ world units at the FEET.
// [assumed] -- classic client hovers roughly a foot off the ground.
constexpr float kLevitationHoverHeight = 1.5f;

// Descent rate in units/sec when the feet are above the hover line (the
// "descend slowly when walking off a ledge" of the issue). [assumed] --
// the classic fall acceleration is ~80 units/s^2 (PlayerMovementState::
// gravity); a slow, roughly constant descent is the observed behaviour.
constexpr float kLevitationDescentRate = 10.0f;

// The predicate: any active buff whose spell carries SPA 57, OR the
// player's own flymode 2 (levitate) / 5 (levitate while running) -- the
// server-sent appearance (a GM #levitate-style effect reaches self even
// though the cast path does not). `db` may be nullptr (a client before the
// spell database is up); then only the flymode half can fire.
//
// `exclude_spell_id` drops one buff from the buff half. The fade hooks fire
// while the fading buff is STILL in the list (buff_manager.cpp: removeBuff
// and removeBuffBySlot both notify before erasing, and checkBuffExpiration
// does the same), so the fade-side recompute must exclude the spell that is
// going away -- the same shape T339's computeVisionType() needs. The apply
// hooks pass NO_EXCLUDED_SPELL (nothing excluded: the new buff IS in the
// list already, and it counts).
inline LevitationState PlayerIsLevitating(const std::vector<ActiveBuff>& buffs,
                                   SpellDatabase* db,
                                   uint8_t selfFlymode,
                                   uint32_t exclude_spell_id = SPELL_UNKNOWN) {
    if (selfFlymode == 2 || selfFlymode == 5) {
        return LevitationState::Levitating;
    }
    if (!db) {
        return LevitationState::NotLevitating;
    }
    for (const ActiveBuff& buff : buffs) {
        if (buff.spell_id == 0 || buff.spell_id == SPELL_UNKNOWN) continue;
        if (buff.spell_id == exclude_spell_id) continue;
        const SpellData* spell = db->getSpell(buff.spell_id);
        if (spell && spell->hasEffect(SpellEffect::Levitate)) {
            return LevitationState::Levitating;
        }
    }
    return LevitationState::NotLevitating;
}

}  // namespace EQ

#endif  // EQT_SPELL_LEVITATION_H
