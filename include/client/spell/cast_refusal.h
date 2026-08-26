/*
 * T244: one mapping from a local cast refusal (EQ::CastResult) to the
 * official client string that names the reason -- eqstr_us.txt ids, looked
 * up through EverQuest::GetStringMessage() (string DB first, its built-in
 * fallback table second). Both refusal-message sites -- the action layer's
 * EqActionHandler::beginCastFromGem() and the UI's CastSpellIntent arm in
 * eq_bridge.cpp -- read this table, so they cannot drift apart again (T240
 * found the action layer collapsing OutOfRange/NoLineOfSight/InvalidTarget
 * into "Cannot cast spell" while the intent arm had per-case literals).
 *
 * Returns 0 for results that have no official refusal string (Success,
 * Interrupted, NotMemorized, SpellNotReady, InvalidSpell, Feared); callers
 * keep their own wording for those.
 */
#pragma once

#include "client/spell/spell_constants.h"

#include <cstdint>

namespace EQ {

// eqstr_us.txt ids (verified 2026-08-26 against
// /mnt/projects/EverQuestP1999/eqstr_us.txt).
enum CastRefusalStringId : uint32_t {
    STR_TARGET_OUT_OF_RANGE     = 100,    // Your target is out of range, get closer!
    STR_CANNOT_SEE_TARGET       = 108,    // You cannot see your target.
    STR_SPELL_FIZZLES           = 173,    // Your spell fizzles!
    STR_CANNOT_CAST_STUNNED     = 175,    // You can't cast spells while stunned!
    STR_INSUFFICIENT_MANA       = 199,    // Insufficient Mana to cast this spell!
    STR_SILENCED                = 207,    // You *CANNOT* cast spells, you have been silenced!
    STR_SPELL_RECAST_NOT_MET    = 236,    // Spell recast time not yet met.
    STR_CANNOT_AFFECT_NPC       = 251,    // That spell can not affect this target NPC.
    STR_NO_PET                  = 255,    // You do not have a pet.
    STR_MISSING_COMPONENTS      = 272,    // You are missing some required spell components.
    STR_ALREADY_CASTING         = 12442,  // You are already casting a spell!
};

inline uint32_t castRefusalStringId(CastResult result) {
    switch (result) {
        case CastResult::Fizzle:            return STR_SPELL_FIZZLES;
        case CastResult::NotEnoughMana:     return STR_INSUFFICIENT_MANA;
        case CastResult::OutOfRange:        return STR_TARGET_OUT_OF_RANGE;
        case CastResult::InvalidTarget:     return STR_CANNOT_AFFECT_NPC;
        case CastResult::ComponentMissing:  return STR_MISSING_COMPONENTS;
        case CastResult::NoLineOfSight:     return STR_CANNOT_SEE_TARGET;
        case CastResult::Stunned:           return STR_CANNOT_CAST_STUNNED;
        case CastResult::Silenced:          return STR_SILENCED;
        case CastResult::GemCooldown:       return STR_SPELL_RECAST_NOT_MET;
        case CastResult::AlreadyCasting:    return STR_ALREADY_CASTING;
        case CastResult::NoPet:             return STR_NO_PET;
        case CastResult::Success:
        case CastResult::Interrupted:
        case CastResult::Feared:
        case CastResult::NotMemorized:
        case CastResult::SpellNotReady:
        case CastResult::InvalidSpell:
            return 0;
    }
    return 0;
}

} // namespace EQ
