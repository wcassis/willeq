// T314: the one body-type rule for spell targeting. See the header for why
// this exists and where the accept sets come from.

#include "client/spell/spell_body_type_target.h"

namespace EQ {

namespace {

namespace BT = EQT::BodyType;

// eqemu folds two Velious-era body types into their base type before the
// comparison (zone/spells.cpp, "check for special case body types").
uint8_t FoldSpecialCase(uint8_t bodytype)
{
    if (bodytype == BT::RaidGiant) {
        return BT::Giant;
    }
    if (bodytype == BT::VeliousDragon) {
        return BT::Dragon;
    }
    return bodytype;
}

}  // namespace

bool TargetTypeRestrictsCreature(SpellTargetType target_type)
{
    switch (target_type) {
        case SpellTargetType::Animal:
        case SpellTargetType::Undead:
        case SpellTargetType::UndeadAE:
        case SpellTargetType::Summoned:
        case SpellTargetType::SummonedAE:
        case SpellTargetType::Corpse:
        case SpellTargetType::Plant:
        case SpellTargetType::Giant:
        case SpellTargetType::Dragon:
            return true;
        default:
            return false;
    }
}

bool BodyTypeSatisfiesTargetType(SpellTargetType target_type, uint8_t bodytype,
                                 bool is_corpse)
{
    const uint8_t body = FoldSpecialCase(bodytype);

    switch (target_type) {
        case SpellTargetType::Animal:
            // eqemu ST_Animal wants Animal and nothing else. Insect(22) is a
            // separate body type and is NOT an animal target -- the old
            // SpellManager copy accepted it (as "21"), which was its own
            // Animal value shifted by one.
            return body == BT::Animal;

        case SpellTargetType::Undead:
        case SpellTargetType::UndeadAE:
            return body == BT::Undead || body == BT::SummonedUndead ||
                   body == BT::Vampire;

        case SpellTargetType::Summoned:
        case SpellTargetType::SummonedAE:
            return body == BT::Summoned || body == BT::Summoned2 ||
                   body == BT::Summoned3;

        case SpellTargetType::Corpse:
            return is_corpse;

        case SpellTargetType::Plant:
            return body == BT::Plant;

        case SpellTargetType::Giant:
            // RaidGiant(9) already folded into Giant(4) above.
            return body == BT::Giant;

        case SpellTargetType::Dragon:
            // VeliousDragon(30) already folded into Dragon(26) above.
            // Dragon2(29)/Dragon3(32) are NOT folded by the server, so they
            // are not dragon targets here either.
            return body == BT::Dragon;

        default:
            return true;
    }
}

}  // namespace EQ
