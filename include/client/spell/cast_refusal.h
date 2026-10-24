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
 *
 * T251: castRefusalMessage() is what both sites now call -- it renders the
 * id through the string DB. (T251 also named a short component with 433;
 * B1140 removed the local reagent refusal -- the official client sends the
 * cast and the SERVER says 272 + 433 -- so nothing local produces
 * ComponentMissing any more.)
 *
 * T253: a THIRD site -- the mouse hotbar Spell arm (HotbarActivateIntent in
 * eq_bridge.cpp) -- had kept three literals and stayed silent for every
 * other refusal; it now calls castRefusalMessage() too. Grep for a fourth
 * (review/T253/fourth_site_grep.txt) found none.
 */
#pragma once

#include "client/spell/spell_constants.h"

#include <cstdint>
#include <string>

namespace eqt { struct ClientContext; }

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
    STR_MISSING_COMPONENTS      = 272,    // You are missing some required components.
    STR_MISSING_NAMED_COMPONENT = 433,    // You are missing %1.   (T251)
    STR_INSUFFICIENT_ENDURANCE  = 5805,   // Insufficient Endurance to activate this ability!  (T355)
    STR_ALREADY_CASTING         = 12442,  // You are already casting a spell!
};

// T251: the chat line for a refusal, or "" when the result has no official
// string (see castRefusalStringId()). Defined in
// src/client/spell/cast_refusal.cpp.
//
// T366 (B038 cluster 4): takes the ClientContext, not EverQuest&. The three
// things it reads -- the spell manager, the inventory manager and the eqstr
// lookup -- are all context members, so passing the whole client was the last
// reason eqt::bridge::BridgeComponent carried an EverQuest& handle.
std::string castRefusalMessage(eqt::ClientContext& ctx, CastResult result);

inline uint32_t castRefusalStringId(CastResult result) {
    switch (result) {
        case CastResult::Fizzle:            return STR_SPELL_FIZZLES;
        case CastResult::NotEnoughMana:     return STR_INSUFFICIENT_MANA;
        case CastResult::NotEnoughEndurance: return STR_INSUFFICIENT_ENDURANCE;
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
