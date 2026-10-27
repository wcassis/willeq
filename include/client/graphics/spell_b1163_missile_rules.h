/*
 * B1163 (#482): the missile path follow-ups from B1152, pinned as pure rules.
 *
 * This header is deliberately device-free (no <irrlicht.h>): it holds the
 * missile speed word, the calibrated flight timing and the oracle's spell-859
 * removal gate, so test_b1163_missile_rules can pin them without a display,
 * and so SpellVisualFX has exactly one source of truth for them. Every
 * address below is an instruction start in review/B1141/ghidra/out/eqgame.dis;
 * line numbers are in review/B1141/ghidra/out/eqgame.exe.c.
 *
 *  (A) The missile speed is the missile object's speed WORD [missile+0x28],
 *      written by the missile constructor FUN_00476f34 (not a spellsnew.edd /
 *      .eff field and not a spells_us.txt field):
 *          004774c2  fld1                         ; [+0x62]==0: 1.0
 *          004774f4  mov [esi+0x28], 0x40800000   ; types 0x1b..0x1c: 4.0
 *          00477504  mov [esi+0x28], 0x40200000   ; other types: 2.5
 *          0047750d  cmp [esi+0x5c], 0x35b        ; type 9 (spell): spell 859?
 *          00477514  mov [esi+0x28], 0x3fc00000   ; type 9: 1.5
 *          0047751d  mov [esi+0x28], 0x3f4ccccd   ; type 9 + spell 0x35b: 0.8
 *      The physics step FUN_004bfe0f advances a missile by that word along its
 *      heading (exe.c 146632 pfVar9 = missile+0x10, so pfVar9[6] = +0x28;
 *      146774-146781 local_18/local_1c += cos/sin(heading) x pfVar9[6]),
 *      with dt = elapsed x [0x6410b8] (= 0.02) clamped at 3.0 (exe.c
 *      146620-146627): ticks of 50 time() units. Reading time() as ms (its
 *      divisor is a runtime value, exe.c 161138-161148), one tick is 50 ms,
 *      20 ticks/s, so 1.5 u/tick = 30 u/s -- exactly the speed B1152 measured
 *      for record 80 (9.05 units over 0.30 s, refactor/B1152/TRACE.md sec. 8),
 *      which is the corroboration of the ms reading. Spell 859 flies at
 *      0.8 u/tick = 16 u/s.
 *      NOT a speed: FUN_00476b12's 0.02 x elapsed term (00476c18-00476d2c) is
 *      time() minus [[caster+0x1a8]+0x50], the CASTER's last physics tick
 *      (written by FUN_004bfe0f, exe.c 146708), times the missile velocity
 *      +0x1c/+0x20/+0x24 which the constructor copies from the caster
 *      (exe.c 95330-95332). FUN_00476b12 runs once, before the actor exists
 *      (exe.c 60443-60445): a one-shot spawn-position catch-up.
 *
 *  (C) Spell 859's removal gate (FUN_0047789d, the missile removal): for a
 *      type-9 missile (0047793d cmp byte [esi+0x63], 9) whose spell is 0x35b
 *      (00477962 cmp eax, 0x35b), the XY distance from the missile to the
 *      local player (004779c6 call 0x439c41, 7th arg 0.0 = 2-D) is compared
 *      to [0x645ec0] = 40.0 (004779cb fcomp); 004779d3 test ah, 0x41 /
 *      004779d6 jp 0x477aab skips only when neither C0 nor C3 is set, so the
 *      branch is taken when dist <= 40.0. It builds an action packet whose
 *      type byte +0x16 is [missile+0x64] (00477a0e / 00477a1b), and the
 *      constructor set [missile+0x64] = FUN_00479d99([missile+0x63])
 *      (004772dd call 0x479d99 / 004772e2), which maps 9 to 0xe7
 *      (00479dd3 sub eax, 7 .. 00479dda je 0x479dec; 00479dec mov al, 0xe7).
 *      So in the callee (00477a4e call 0x47f614, ecx = the local player)
 *      0047f625 cmp byte [esi+0x16], 0xe7 is TRUE and 0047f630 call 0x47f4af
 *      runs: it looks up the spell from packet +0x1b and ends in
 *      0047f5eb push 2 / 0047f5ed call 0x49b260 -- spell 859's Target stage
 *      (stage 2) fired AT THE LOCAL PLAYER. (The six SPA ids
 *      0x0c/0x1c/0x1d/0x13a/0x13b/0x13c and the velocity push are on the
 *      NOT-0xe7 branch, 0047f65a onward, which 859 never takes.)
 *      Spell 54 (00477959 cmp eax, 0x36) is the contrast: its stage 2 fires
 *      unconditionally (00477aa1 push 2 / 00477aa3 call 0x49b260).
 */

#ifndef EQT_GRAPHICS_SPELL_B1163_MISSILE_RULES_H
#define EQT_GRAPHICS_SPELL_B1163_MISSILE_RULES_H

#include <cmath>
#include <cstdint>

namespace EQ {
namespace SpellMissileRules {

// The one spell id the client compares on the missile path (0047750d and
// 00477962 both read cmp ..., 0x35b).
inline constexpr uint32_t kSpell859 = 0x35b;

// (A) The speed word [missile+0x28], units per physics tick.
inline constexpr float kMissileSpeedWordSpell = 1.5f;  // 00477514 0x3fc00000
inline constexpr float kMissileSpeedWord859   = 0.8f;  // 0047751d 0x3f4ccccd
// FUN_004bfe0f: dt = elapsed x 0.02 ([0x6410b8]) -> one tick = 50 ms, 20/s.
inline constexpr float kMissileTicksPerSecond = 20.0f;

// The speed word a spell missile (type 9) carries.
inline constexpr float MissileSpeedWord(uint32_t spellId) {
    return spellId == kSpell859 ? kMissileSpeedWord859 : kMissileSpeedWordSpell;
}
// The same in units per second (word x 20 ticks/s).
inline constexpr float MissileSpeed(uint32_t spellId) {
    return MissileSpeedWord(spellId) * kMissileTicksPerSecond;
}
// The spell-missile speed every spell but 859 flies at: 1.5 x 20 = 30 u/s.
inline constexpr float kMissileSpeed = kMissileSpeedWordSpell * kMissileTicksPerSecond;

// The calibrated flight geometry and clock (refactor/B1152/TRACE.md sec. 8,
// record 80): the oracle's caster->target gap, the flight end before the
// OP_Action, and the Target stage after the flight end. BLESS-ONLY.
inline constexpr float kMissileFlightDistance  = 9.05f;  // units
inline constexpr float kMissileFlightEndOffset = 0.184f; // s before OP_Action
inline constexpr float kMissileLandingDelay    = 0.30f;  // s after the flight end

// Flight time: the calibrated gap over the spell's speed word (0.302 s for a
// 1.5-word missile, 0.566 s for 859's 0.8 word).
inline float MissileFlightTime(uint32_t spellId) {
    return kMissileFlightDistance / MissileSpeed(spellId);
}

// FX-time (s) the missile reaches the target: kMissileFlightEndOffset before
// the hit (the OP_Action).
inline double MissileReachAt(double hitAt) { return hitAt - kMissileFlightEndOffset; }

// FX-time (s) the Target stage fires: kMissileLandingDelay after the flight end.
inline double MissileLandAt(double reachAt) { return reachAt + kMissileLandingDelay; }

// (C) The 859 removal gate.
inline constexpr float kSpell859GateDistance = 40.0f;  // [0x645ec0]

// FUN_00439c41 with a 0.0 7th argument: the 2-D (x, y) distance.
inline float GateDistanceXY(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

// True when the oracle fires the spell's Target stage (stage 2) at the LOCAL
// PLAYER as the missile is removed: spell 0x35b and the missile within
// 40.0 XY units of the player (dist <= 40.0; a NaN distance sets C0 and C3
// together, even parity, and skips like dist > 40).
inline bool Spell859RemovalFiresAtPlayer(uint32_t spellId, float distXY) {
    return spellId == kSpell859 && distXY <= kSpell859GateDistance;
}

} // namespace SpellMissileRules
} // namespace EQ

#endif
