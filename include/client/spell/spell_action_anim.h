#pragma once

// B1174 (#493): the official client's OP_Action spell handler plays the
// spell's CastingAnim on the SOURCE at the packet -- the only thing a real
// /disc draws (refactor/B1174/README.md: record 325's Target stage is empty,
// and the observer's view shows the caster's t06 swing, no sprite).
//
// eqgame FUN_0047f4af (0x47f4af; review/B1141/ghidra/out/eqgame.exe.c
// 101892-101961), reached for an OP_Action of type 0xe7 (0x47f614). The
// spell id is the packet's word at +0x1b (movsx) and its record is the
// client's OWN spell table (FUN_0040cbd6), so the server's name for the id
// does not matter. The handler RETURNS before anything else unless
//   0x47f4c2..0x47f4ca  FUN_0044d0d0(id): the id is in 1..9999
//                       (0x44d0d0: cmp 1 / cmp 0x2710);
//   0x47f4d7..0x47f4e0  FUN_0040cbd6(id) != 0: the client has a record;
//   0x47f4e6..0x47f4ed  record byte +0x180 != 0: spells_us.txt field 98,
//                       the target type (the parser's param_1+0x60 store,
//                       exe.c 69217).
// Then, before the Target stage (0x47f5e1), for the source spawn (packet
// word +2, non-null at 0x47f50e..0x47f517):
//   0x47f519..0x47f525  the source's stand byte +0x251 is 'd' (100,
//                       standing) or 'f' (102, frozen/stunned);
//   0x47f527..0x47f530  FUN_0044ced9(spell) == 0, i.e. the spell's field 123
//                       (record +0x230, the spells.eff index) is NOT 61..64;
//   0x47f53c..0x47f54c  the source's class byte +0x252 is not 8 (Bard), or the
//                       packet's instrument_mod (int at +6) is above 10 --
//                       a SIGNED compare (0x47f548 cmp dword [eax+6],0xa;
//                       jle);
//   0x47f54e..         the source's animation manager (FUN_0047c08e) is asked
//                       for the spell's CastingAnim (record +0x186, field 120)
//                       with (1, 0, 1.0f, 1) -- the same call OP_BeginCast
//                       makes (spell_cast_animation.h).
// A source still in its BeginCast state already plays that id (the client's
// same-id no-op, FUN_00450c2f), so the rule only shows where no BeginCast
// started the track: a /disc (OP_CastSpell slot 10 -> OP_Action only).
//
// The stand byte (an APPROXIMATION, refactor/B1174/README.md sec. 5): the
// client keeps +0x251 per spawn and writes it in many places (eqgame.dis has
// 143 references to +0x251; FUN_00483556 is one setter, with local
// transitions such as 0x69 = looting at 0x484de7). fable does not port that
// state machine. It keeps, in Entity::stand_state, the stand value the server
// last sent for the spawn in OP_SpawnAppearance AT_Anim (and, for the local
// player, the pose fable itself sets: MovementComponent::ApplyPositionState),
// default 100 (SpawnStandStateAfterAppearance below). So a spawn the server
// puts in 105 (looting), 110 (sitting), 111, 115 ... is not asked, and a
// client-internal transition fable does not model is not in the byte.

#include <cstdint>

namespace EQ {

constexpr int kActionAnimBardClass = 8;            // the class byte FUN_0047f4af excludes
constexpr uint8_t kStandStateStanding = 100;       // 'd', eq_enums.h ANIM_STANDING
constexpr uint8_t kStandStateFrozen = 102;         // 'f', eq_enums.h ANIM_FREEZE

// FUN_0047f4af's inputs, each named after the byte it reads.
struct ActionCastAnimInputs {
    uint32_t spellId = 0;            // packet +0x1b
    bool recordExists = false;       // FUN_0040cbd6(id) != 0
    uint8_t targetType = 0;          // record +0x180, spells_us.txt field 98
    uint8_t sourceStandState = 0;    // source spawn +0x251
    int spellAffectIndex = -1;       // record +0x230, field 123 (-1 = none)
    int sourceClass = 0;             // source spawn +0x252 (0 = unknown)
    uint32_t instrumentMod = 0;      // packet +6, read as a signed int
};

inline constexpr bool ActionSpellIdInRange(uint32_t spellId) {  // FUN_0044d0d0
    return spellId >= 1 && spellId < 10000;
}

inline bool ActionPlaysSourceCastAnim(const ActionCastAnimInputs& in) {
    if (!ActionSpellIdInRange(in.spellId) || !in.recordExists || in.targetType == 0) return false;
    if (in.sourceStandState != kStandStateStanding && in.sourceStandState != kStandStateFrozen) return false;
    if (in.spellAffectIndex > 60 && in.spellAffectIndex < 65) return false;  // FUN_0044ced9 == 1
    return in.sourceClass != kActionAnimBardClass || static_cast<int32_t>(in.instrumentMod) > 10;
}

// The stand byte fable keeps per spawn (Entity::stand_state) after an
// OP_SpawnAppearance AT_Anim with `animParam`: the server's stand values
// (100 standing, 102 frozen, 105 looting, 110 sitting, 111 crouching,
// 115 lying, ...) are taken as sent; ANIM_STAND (0), the value eqemu also
// uses for standing, is 100; any other value (an emote-style action
// animation) leaves the byte as it was.
inline uint8_t SpawnStandStateAfterAppearance(uint8_t current, uint32_t animParam) {
    if (animParam == 0) return kStandStateStanding;
    if (animParam >= 100 && animParam <= 255) return static_cast<uint8_t>(animParam);
    return current;
}

}  // namespace EQ
