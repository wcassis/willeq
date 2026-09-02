#ifndef EQT_SPELL_BODY_TYPE_TARGET_H
#define EQT_SPELL_BODY_TYPE_TARGET_H

// T314: THE single body-type rule for spell targeting in this client.
//
// Before this unit two hand-rolled copies of EverQuest's body-type numbering
// were live here and both were wrong:
//   * SpellManager::isValidCreatureTarget()'s local BT_* constants -- off by
//     one for EVERY value (BT_HUMANOID = 0, BT_UNDEAD = 2, BT_GIANT = 3,
//     BT_ANIMAL = 20, BT_PLANT = 24, BT_DRAGON = 25);
//   * SpellTypeProcessor::isValidTarget() -- a third, differently-wrong set
//     (21 for Plant, which is really Animal; 22 for Giant, which is really
//     Insect), with Undead/Summoned/Animal not checked at all.
// Both now call this one predicate, which spells its constants out of
// client/eq_body_types.h -- the values transcribed from the server that
// stamps them (eqemu common/bodytypes.h).
//
// The accept sets are transcribed from eqemu's own dispatcher,
// zone/spells.cpp Mob::DetermineSpellTargets():
//   ST_Undead / ST_UndeadAE : Undead, SummonedUndead, Vampire   (line ~1936)
//   ST_Summoned / ST_SummonedAE : Summoned, Summoned2, Summoned3 (line ~1956)
//   ST_Plant / ST_Dragon / ST_Giant / ST_Animal : exactly that one body type
//     (lines ~1985-1995), with two special cases folded in first:
//       RaidGiant(9)     counts as Giant(4)
//       VeliousDragon(30) counts as Dragon(26)
// Anything else is unrestricted.

#include "client/eq_body_types.h"
#include "spell_constants.h"

#include <cstdint>

namespace EQ {

// True if `bodytype` satisfies the creature restriction `target_type` imposes.
// `is_corpse` answers SpellTargetType::Corpse, the one restriction that is not
// a body type. Unrestricted target types return true.
bool BodyTypeSatisfiesTargetType(SpellTargetType target_type, uint8_t bodytype,
                                 bool is_corpse);

// True if `target_type` restricts targets by body type (or corpse state) at
// all -- i.e. if BodyTypeSatisfiesTargetType() can ever return false for it.
bool TargetTypeRestrictsCreature(SpellTargetType target_type);

}  // namespace EQ

#endif  // EQT_SPELL_BODY_TYPE_TARGET_H
