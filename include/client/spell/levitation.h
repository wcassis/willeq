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
// [measured] by the lane-1 oracle capture (refactor/B892/README.md,
// "The two motion numbers — [measured]", evidence in refactor/B892/evidence/):
// the official client in-world as Testchar, GM-cast Levitate, the C->S
// OP_ClientUpdate wire (eqemu packet logging, categories 83/84) decoded at
// z_pos@+26. Standing floor at the pre-cast standing spot (x=20, y=5.0) =
// -38.2157 (593 constant samples); the buff landed and the client's own
// computed z bobbed ~4.4 s about the hover line: feet 1.458 (low) .. 4.556
// (high) above the floor, mean 2.989. Nothing else names these two numbers.

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
// [measured] -- the lane-1 oracle capture: the classic client's self-
// levitation BOBS about the hover line -- the C->S z bobbed feet
// 1.458 (low) .. 4.556 (high) above the floor (period ~4.4 s) -- and the LOW of
// that bob is where the feet rest between cycles. fable's renderer does not
// animate the bob (it pins the feet at the line and descends to it), so the
// line it should hold is the bob's LOW, 1.458 -- the point the classic client
// re-anchors the feet at every cycle, not the bob's mean (2.989).
constexpr float kLevitationHoverHeight = 1.5f;

// Descent rate in units/sec when the feet are above the hover line (the
// "descend slowly when walking off a ledge" of the issue). [measured] -- the
// lane-1 oracle capture's 33 high->low levitation legs (z_pos@+26 on the C->S
// OP_ClientUpdate wire, ~1 Hz) descend at a median 1.294 u/s (range 0.697-1.487;
// the spread is 1 Hz sampling jitter on a ~2.6 u drop), so the classic client
// descends slowly and roughly constantly -- far slower than the old assumed
// 10.0 u/s (which was a guess against the classic fall acceleration).
constexpr float kLevitationDescentRate = 1.3f;

// The predicate: any active buff whose spell carries SPA 57, OR the
// player's own flymode 2 (levitate) / 5 (levitate while running) -- the
// server-sent appearance (a GM #levitate-style effect reaches self even
// though the cast path does not). `db` may be nullptr (a client before the
// spell database is up); then only the flymode half can fire.
//
// `exclude_spell_id` drops one buff from the buff half. The fade hooks fire
// while the fading buff is STILL in the list (buff_manager.cpp:
// removeBuffBySlot and checkBuffExpiration both notify before erasing), so
// the fade-side recompute must exclude the spell that is
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
