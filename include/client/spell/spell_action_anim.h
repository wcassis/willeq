#pragma once

// B1174 (#493): the official client's OP_Action spell handler plays the
// spell's CastingAnim on the SOURCE at the packet -- the only thing a real
// /disc draws (refactor/B1174/README.md: record 325's Target stage is empty,
// and the observer's view shows the caster's t06 swing, no sprite).
//
// eqgame FUN_0047f4af (0x47f4af; review/B1141/ghidra/out/eqgame.exe.c
// 101892-101961), reached for an OP_Action of type 0xe7 (0x47f614): the
// spell id is the packet's word at +0x1b and its record is the client's OWN
// spell table (FUN_0040cbd6), so the server's name for the id does not
// matter. Then, before the Target stage (0x47f5e1), for the source spawn
// (packet word +2):
//   0x47f519..0x47f525  the source's stand byte +0x251 is 'd' (100,
//                       standing) or 'f' (102, frozen/stunned);
//   0x47f527..0x47f530  FUN_0044ced9(spell) == 0, i.e. the spell's field 123
//                       (record +0x230, the spells.eff index) is NOT 61..64;
//   0x47f53c..0x47f54c  the source's class byte +0x252 is not 8 (Bard), or the
//                       packet's instrument_mod (int at +6) is above 10;
//   0x47f54e..         the source's animation manager (FUN_0047c08e) is asked
//                       for the spell's CastingAnim (record +0x186, field 120)
//                       with (1, 0, 1.0f, 1) -- the same call OP_BeginCast
//                       makes (spell_cast_animation.h).
// A source still in its BeginCast state already plays that id (the client's
// same-id no-op, FUN_00450c2f), so the rule only shows where no BeginCast
// started the track: a /disc (OP_CastSpell slot 10 -> OP_Action only).

#include <cstdint>

namespace EQ {

constexpr int kActionAnimBardClass = 8;  // the class byte FUN_0047f4af excludes

inline bool ActionPlaysSourceCastAnim(bool sourceStanding, int spellAffectIndex, int sourceClass,
                                      uint32_t instrumentMod) {
    if (!sourceStanding) return false;
    if (spellAffectIndex > 60 && spellAffectIndex < 65) return false;  // FUN_0044ced9 == 1
    return sourceClass != kActionAnimBardClass || instrumentMod > 10;
}

}  // namespace EQ
