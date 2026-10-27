/*
 * B1163 (#482): the missile path follow-ups from B1152, pinned as pure rules.
 *
 * This header is deliberately device-free (no <irrlicht.h>): it names the
 * two calibrated quantities the missile actor uses and the oracle's
 * spell-859 gate, so test_b1163_missile_rules can pin them without a display,
 * and so SpellVisualFX's private constants have exactly one source of truth.
 *
 * Two facts were traced from the oracle's disassembly
 * (review/B1141/ghidra/out/eqgame.dis) and are the content of B1163:
 *
 *  (A) The per-record "run" speed is a RUNTIME quantity, not a file field.
 *      The oracle's missile spawner (FUN_00476b12) scales the stage's elapsed
 *      time -- time() minus the stage start word (+0x50) -- by 0.02 and
 *      multiplies it by the LIVE target's per-axis velocity vector:
 *          00476c18  call 0x46d655          ; time()
 *          00476c23  sub  eax, [ecx+0x50]   ; elapsed since stage start
 *          00476c36  fmul [0x6410b8]        ; x 0.02 (the "run value" scale)
 *          00476d11  fld  [ebp-4]           ; stageScalar (the elapsed x scale)
 *          00476d14  fmul [esi+0x1c]        ; x per-axis velocity (x)
 *          00476d17  fadd [esi+0x10]        ; pos.x += stageScalar*vel.x
 *          ... 00476d1d/00476d20/00476d29 for y/z ...
 *      The per-axis velocity the effect carries (effect+0x1c/+0x20/+0x24) is
 *      copied from the live entity the same tick (004770f1-00477100), so the
 *      "run value" is elapsed-since-stage x live-velocity -- neither word is
 *      read from spellsnew.edd or spellsnew.eff. The spell record is
 *      zero-initialised at construction (0044d932-0044db05: the whole record
 *      is cleared, +0x0c at 0044d948, +0x180 at 0044da15, +0x188 at 0044da45)
 *      and never has a run-speed word filled from the file. So there is no
 *      per-record run speed to read; fable's actor flies at one calibrated
 *      speed (kMissileSpeed) that reproduces the record-80 oracle flight
 *      (9.05 units over 0.30 s = 30 u/s, refactor/B1152/TRACE.md sec. 3/8).
 *
 *  (C) Spell 859's distance-gated branch (eqgame 00477962 cmp eax, 0x35b) is a
 *      NETWORK + VELOCITY step, not a local draw. Its callee (0x47f614) sends
 *      six zone messages and adds (cos, sin, sin-pitch) x speed to the
 *      in-memory record's velocity vector; it spawns no local particle. So the
 *      859 branch draws nothing the visual ledger would show, and fable
 *      intentionally implements no draw for it (859's visible projectile, if
 *      any, comes from the ordinary Middle-emitter / createLanding path). The
 *      gate below pins the traced rule: the branch fires iff the spell is 859
 *      (0x35b) AND the effect is within 40.0 units of the player
 *      (004779cb fcomp [0x645ec0] = 40.0 -> 00477a4e call 0x47f614). Spell 54
 *      (0x36) is the contrast: it fires its stage-2 emitter unconditionally
 *      (00477959 cmp eax, 0x36 -> 00477aa3 call 0x49b260), no distance gate.
 *
 * The +0x260 spawn-count key B1152 noted (20.5 vs 1.5) is the live creature
 * type written by the entity type setter (0047a74a mov [ecx+0x260], eax),
 * also a runtime quantity -- not a file field either.
 */

#ifndef EQT_GRAPHICS_SPELL_B1163_MISSILE_RULES_H
#define EQT_GRAPHICS_SPELL_B1163_MISSILE_RULES_H

#include <cstdint>

namespace EQ {
namespace SpellMissileRules {

// (A) The calibrated missile timing (refactor/B1152/missile_oracle.tsv record
// 80, spell 38; refactor/B1152/TRACE.md sec. 3/8). The per-record "run" speed
// is runtime-only, so these constants -- not a per-record value -- drive the
// actor. BLESS-ONLY: changing them moves the missile timing out of the B1152
// oracle bands (test_b1163_missile_rules + the B1152 timing gate).
inline constexpr float kMissileSpeed           = 30.0f;  // u/s (record 80: 9.05u/0.30s)
inline constexpr float kMissileFlightDistance  = 9.05f;  // units, caster -> target
inline constexpr float kMissileFlightEndOffset = 0.184f; // s before OP_Action the flight ends
inline constexpr float kMissileLandingDelay    = 0.30f;  // s from flight end to Target stage

// (C) The oracle's spell-859 gate (eqgame 00477962 cmp eax, 0x35b; 004779cb
// fcomp [0x645ec0] = 40.0; 00477a4e call 0x47f614). The callee is a
// network + velocity step -- it draws no local particle -- so fable
// implements no draw for it. The traced gate (branch fires iff spell==859 AND
// dist<40.0) is pinned by test_b1163_missile_rules, which encodes the rule
// against these two words (kept here so the values have one home).
inline constexpr uint32_t kSpell859           = 0x35b;  // spell 859
inline constexpr float    kSpell859GateDistance = 40.0f; // [0x645ec0]

// Flight time the actor uses: distance / calibrated speed (a constant --
// NOT a per-record value, per the runtime-only proof above).
inline float MissileFlightTime() { return kMissileFlightDistance / kMissileSpeed; }

// FX-time (s) the missile reaches the target: the flight ends
// kMissileFlightEndOffset before the hit (the OP_Action).
inline double MissileReachAt(double hitAt) { return hitAt - kMissileFlightEndOffset; }

// FX-time (s) the Target stage fires: kMissileLandingDelay after the flight ends.
inline double MissileLandAt(double reachAt) { return reachAt + kMissileLandingDelay; }

} // namespace SpellMissileRules
} // namespace EQ

#endif
