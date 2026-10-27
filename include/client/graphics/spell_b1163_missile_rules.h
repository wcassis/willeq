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
 *  (A) The missile's [missile+0x28] is a THRUST word, not a speed. The
 *      constructor FUN_00476f34 writes it (not a spellsnew.edd / .eff field
 *      and not a spells_us.txt field):
 *          004774c2  fld1                         ; [+0x62]==0: 1.0
 *          004774f4  mov [esi+0x28], 0x40800000   ; types 0x1b..0x1c: 4.0
 *          00477504  mov [esi+0x28], 0x40200000   ; other types: 2.5
 *          0047750d  cmp [esi+0x5c], 0x35b        ; type 9 (spell): spell 859?
 *          00477514  mov [esi+0x28], 0x3fc00000   ; type 9: 1.5
 *          0047751d  mov [esi+0x28], 0x3f4ccccd   ; type 9 + spell 0x35b: 0.8
 *      but that is the constructor's LOCAL branch, which no call reaches: its
 *      only call site, 00460737 call 0x476f34 (the 0x4033 message arm of
 *      FUN_0045b8f0), passes the message buffer, so every missile takes the
 *      received branch and copies the word from the message
 *      (004770eb / 004770ee).
 *      FUN_004bfe0f (called with the missile, 0044558f) moves it as a damped
 *      velocity, per pass:
 *          thrust = cos/sin(heading) x word        (004c0292 .. 004c02c8)
 *          v      = 0.625 x v + 0.8 x thrust       (004c04c1 [0x6431d8] = 0.8,
 *                                                   004c04cd [0x643f7c] = 0.625,
 *                                                   004c0694, 004c0834 .. 004c083a)
 *          pos   += dt x v,  dt = 0.02 x elapsed   (004c0033, clamp 3.0 at 004c0049)
 *      The damping is 0.85 instead when [missile+0x62] == 0 (004c075c /
 *      004c0762). The gravity branch needs actor byte +0x9c == 1
 *      (004c0362); the constructor clears it (00477203, 00477532). A pass
 *      runs when at least 33 time() units elapsed (00445580 cmp ecx, 0x21 /
 *      00445583 jb), so the per-pass damping makes the flight depend on the
 *      pass rate. The missile is removed when the XY distance stored at the
 *      PREVIOUS pass is below 2.0 (00476743 fcomp [0x6410b4], 00476769).
 *      MissilePassesToHit() in tests/test_b1163_missile_rules.cpp is that model. It does NOT reproduce B1152's
 *      measurement, for a reason in the captures: the B1152 and B1159 oracle
 *      captures hold no 0x4033 message, so in those legs the observer's client
 *      built no missile object at all. kMissileSpeed = 30 u/s therefore stays
 *      the B1152 record-80 CALIBRATION (refactor/B1163/README.md (A)).
 *      Spell 859 never flies: FUN_00476b12 zeroes the word and the velocity
 *      for target types 8 / 0x15..0x17 / 0x1c and puts the missile on its
 *      [+0x50] entity (00476be6 .. 00476d6b); 859's target type is 8.
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
 *      Four early-outs guard that fire (all pass for 859):
 *        00477947 cmp al, bl / 00477949 jne 0x477aab: the stage-2 fires (54
 *          and 859) run only when [[missile+0x48]] != 0 (al = 1 when it is 0,
 *          00477927 .. 0047792d);
 *        0047f4c2 call 0x44d0d0 / 0047f4ca je 0x47f5f5: the spell id must be
 *          in 1..9999 (0044d0d3 / 0044d0d9 cmp ..., 0x2710);
 *        0047f4de test edi, edi / 0047f4e0 je 0x47f5f5: the spell record must
 *          exist;
 *        0047f4e6 cmp byte ptr [edi + 0x180], 0 / 0047f4ed je 0x47f5f5: no
 *          fire when the record's +0x180 is 0 -- spells_us.txt field 98 (target
 *          type), stored by FUN_0044dbf0 as the byte at param_1 + 0x60 (int*),
 *          i.e. +0x180. 859's is 8.
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

// (A) The thrust word [missile+0x28] the constructor's local branch writes
// for a type-9 (spell) missile. Facts of the code, not speeds. The update
// model and the no-flight target types are pinned in
// tests/test_b1163_missile_rules.cpp (and refactor/B1163/scripts/
// missile_model.py); fable does not run them (README (A)).
inline constexpr float kMissileThrustWordSpell = 1.5f;  // 00477514 0x3fc00000
inline constexpr float kMissileThrustWord859   = 0.8f;  // 0047751d 0x3f4ccccd

// (A) The FUN_004bfe0f update for a missile (no gravity branch).
inline constexpr float kMissileThrustScale   = 0.8f;    // [0x6431d8], 004c04c1
inline constexpr float kMissileDamping       = 0.625f;  // [0x643f7c], 004c04cd
inline constexpr float kMissileDampingNo62   = 0.85f;   // 0x3f59999a, 004c0762
inline constexpr float kMissileDtPerUnit     = 0.02f;   // [0x6410b8], 004c0033
inline constexpr int   kMissileMinPassUnits  = 33;      // 00445580 cmp ecx, 0x21
inline constexpr float kMissileHitDistance   = 2.0f;    // [0x6410b4], 00476743
inline constexpr float kMissileInitialStored = 2000.0f; // 00476eef 0x44fa0000

// The record-80 CALIBRATION (B1152, refactor/B1152/TRACE.md sec. 8, spell 38):
// 9.05 units in 0.30 s. NOT derived from the word: see (A) above.
inline constexpr float kMissileSpeed = 30.0f;  // units / second

// The calibrated flight geometry and clock (refactor/B1152/TRACE.md sec. 8,
// record 80): the oracle's caster->target gap, the flight end before the
// OP_Action, and the Target stage after the flight end. BLESS-ONLY.
inline constexpr float kMissileFlightDistance  = 9.05f;  // units
inline constexpr float kMissileFlightEndOffset = 0.184f; // s before OP_Action
inline constexpr float kMissileLandingDelay    = 0.30f;  // s after the flight end

// Flight time of every missile fable builds: the calibrated gap over the
// calibrated speed, 9.05 / 30 = 0.302 s.
inline float MissileFlightTime() {
    return kMissileFlightDistance / kMissileSpeed;
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

// The whole stage-2-at-player decision with the four early-outs of (C):
// the missile's actor object exists (00477947 / 00477949), the spell id is in
// 1..9999 (0047f4c2 call 0x44d0d0), its record exists (0047f4de), and the
// record's target type (spell +0x180, spells_us.txt field 98) is not 0
// (0047f4e6 / 0047f4ed).
struct Spell859RemovalInputs {
    bool actorObject = false;   // [[missile+0x48]] != 0
    uint32_t spellId = 0;
    bool recordExists = false;
    uint8_t targetType = 0;     // spell +0x180
    float distXY = 0.0f;        // missile -> local player, XY
};
inline constexpr bool SpellIdInRange(uint32_t spellId) {  // FUN_0044d0d0
    return spellId >= 1 && spellId < 10000;
}
// Spell 859's target type, spells_us.txt field 98 (README (A) table).
// SpellVisualFX carries no spell target type; this is the value its one
// caller passes, for the one spell the gate fires for.
inline constexpr uint8_t kSpell859TargetType = 8;
inline bool Spell859RemovalFires(const Spell859RemovalInputs& in) {
    return in.actorObject && Spell859RemovalFiresAtPlayer(in.spellId, in.distXY) &&
           SpellIdInRange(in.spellId) && in.recordExists && in.targetType != 0;
}

} // namespace SpellMissileRules
} // namespace EQ

#endif
