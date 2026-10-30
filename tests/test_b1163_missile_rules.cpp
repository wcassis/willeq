// B1163 (#482): device-free tests for the two missile follow-ups from B1152.
// The addresses are instruction starts in review/B1141/ghidra/out/eqgame.dis;
// the full trace is in client/graphics/spell_b1163_missile_rules.h and
// refactor/B1163/README.md.
//
//  (A) [missile+0x28] is a THRUST word (1.5 for a spell missile, 00477514;
//      0.8 for spell 0x35b, 0047750d cmp / 0047751d mov) in a damped velocity
//      (FUN_004bfe0f: v = 0.625 v + 0.8 x word, 004c04c1 / 004c04cd; pos +=
//      0.02 x elapsed x v, 004c0033), not a distance per tick. The flight it
//      gives depends on the pass rate. The B1152 record-80 calibration
//      (30 u/s) was removed by B1170 (#489): the client draws a Middle stage
//      only on a 0x4033 missile object. Spell 859's target type 8 zeroes its
//      word (FUN_00476b12, 00476bec .. 00476d6b): no flight.
//
//  (C) Spell 859's removal gate (FUN_0047789d): 00477962 cmp eax, 0x35b, the
//      XY distance to the local player (004779c6 call 0x439c41) against
//      [0x645ec0] = 40.0 (004779cb fcomp / 004779d3 test ah, 0x41 /
//      004779d6 jp 0x477aab: taken when dist <= 40.0) -> 00477a4e call
//      0x47f614 with packet type 0xe7 -> 0047f630 call 0x47f4af ->
//      0047f5eb push 2 / 0047f5ed call 0x49b260: spell 859's Target stage
//      fired at the local player, behind four early-outs: the actor object
//      (00477947 / 00477949), spell id 1..9999 (0047f4c2 call 0x44d0d0), the
//      record (0047f4de) and target type != 0 (0047f4e6 / 0047f4ed).
//
// Device-free: includes only the pure rules header (no <irrlicht.h>), so it
// builds and runs in both the graphics and headless trees with no display.

#include <gtest/gtest.h>

#include <limits>

#include "client/graphics/spell_b1163_missile_rules.h"

namespace R = EQ::SpellMissileRules;

namespace {

// (A) The FUN_004bfe0f / FUN_004766aa model, test-local because fable does not
// run it (refactor/B1163/README.md (A); refactor/B1163/scripts/missile_model.py
// is the same loop). A missile at rest `gap` XY units from its target, homing
// straight at it, one pass every `passUnits` time() units: at pass k (k >= 2)
// it is removed when the distance stored at pass k-1 is below 2.0, else the
// current distance is stored; then v = damping x v + 0.8 x word and
// s += 0.02 x passUnits x v. Returns the removal pass, 0 for none.
int MissilePassesToHit(float gap, float word, float damping, int passUnits,
                       int maxPasses = 1000) {
    float v = 0.0f, s = 0.0f, stored = R::kMissileInitialStored;
    const float dt = R::kMissileDtPerUnit * static_cast<float>(passUnits);
    for (int k = 1; k < maxPasses; ++k) {
        if (k >= 2 && stored < R::kMissileHitDistance) return k;
        stored = gap - s;
        v = damping * v + R::kMissileThrustScale * word;
        s = s + dt * v;
    }
    return 0;
}

// (A) FUN_00476b12 zeroes the word and the velocity (no flight) for these
// target types (spells_us.txt field 98, spell +0x180): 00476bec cmp al, 8 /
// 00476bf4 cmp al, 0x14 / 00476bf8 cmp al, 0x17 / 00476c00 cmp al, 0x1c ->
// 00476d6b. Not wired into fable (README (A) FOLLOW-UP).
bool TargetTypeHasNoFlight(uint8_t t) {
    return t == 8 || (t > 0x14 && t <= 0x17) || t == 0x1c;
}

// (A) The thrust word and the damped update are the code's.
TEST(B1163MissileRules, ThrustWordIsNotASpeed) {
    EXPECT_EQ(R::kSpell859, 0x35bu);
    EXPECT_FLOAT_EQ(R::kMissileThrustWordSpell, 1.5f);  // 0x3fc00000
    EXPECT_FLOAT_EQ(R::kMissileThrustWord859, 0.8f);    // 0x3f4ccccd

    EXPECT_FLOAT_EQ(R::kMissileThrustScale, 0.8f);    // [0x6431d8]
    EXPECT_FLOAT_EQ(R::kMissileDamping, 0.625f);      // [0x643f7c]
    EXPECT_FLOAT_EQ(R::kMissileDampingNo62, 0.85f);   // 0x3f59999a
    EXPECT_FLOAT_EQ(R::kMissileDtPerUnit, 0.02f);     // [0x6410b8]
    EXPECT_EQ(R::kMissileMinPassUnits, 33);           // cmp ecx, 0x21 / jb
    EXPECT_FLOAT_EQ(R::kMissileHitDistance, 2.0f);    // [0x6410b4]
    // Terminal velocity 0.8 x 1.5 / 0.375 = 3.2 per dt unit (64 u/s at
    // 0.02 x ms), not 1.5.
    EXPECT_FLOAT_EQ(R::kMissileThrustScale * R::kMissileThrustWordSpell /
                        (1.0f - R::kMissileDamping), 3.2f);

}

// (A) The model's closing pass over the 9.05-unit gap depends on the pass
// rate (refactor/B1163/scripts/missile_model.py prints the same table).
TEST(B1163MissileRules, ClosingTimeDependsOnThePassRate) {
    EXPECT_EQ(MissilePassesToHit(9.05f, 1.5f, R::kMissileDamping, 50), 6);   // 0.300 s
    EXPECT_EQ(MissilePassesToHit(9.05f, 1.5f, R::kMissileDamping, 33), 7);   // 0.231 s
    EXPECT_EQ(MissilePassesToHit(9.05f, 1.5f, R::kMissileDamping, 100), 5);  // 0.500 s
    // The local branch's 1.5-unit spawn offset ([0x6410d0]) shortens the gap.
    EXPECT_EQ(MissilePassesToHit(7.55f, 1.5f, R::kMissileDamping, 50), 5);   // 0.250 s
    EXPECT_EQ(MissilePassesToHit(9.05f, 1.0f, R::kMissileDampingNo62, 50), 7);
    // A zero word never closes the gap.
    EXPECT_EQ(MissilePassesToHit(9.05f, 0.0f, R::kMissileDamping, 50, 200), 0);
}

// (A) FUN_00476b12 zeroes the word for target types 8, 0x15..0x17, 0x1c.
TEST(B1163MissileRules, TargetTypesWithNoFlight) {
    EXPECT_TRUE(TargetTypeHasNoFlight(8));     // spell 859's
    EXPECT_TRUE(TargetTypeHasNoFlight(0x15));
    EXPECT_TRUE(TargetTypeHasNoFlight(0x16));
    EXPECT_TRUE(TargetTypeHasNoFlight(0x17));
    EXPECT_TRUE(TargetTypeHasNoFlight(0x1c));
    EXPECT_FALSE(TargetTypeHasNoFlight(1));    // spell 38's
    EXPECT_FALSE(TargetTypeHasNoFlight(5));    // spell 93's
    EXPECT_FALSE(TargetTypeHasNoFlight(0x14));
    EXPECT_FALSE(TargetTypeHasNoFlight(0x18));
}

// (C) The 859 removal gate: spell 0x35b AND XY distance <= 40.0.
TEST(B1163MissileRules, Spell859RemovalFiresAtPlayer) {
    EXPECT_FLOAT_EQ(R::kSpell859GateDistance, 40.0f);  // [0x645ec0]

    EXPECT_TRUE(R::Spell859RemovalFiresAtPlayer(0x35b, 0.0f));
    EXPECT_TRUE(R::Spell859RemovalFiresAtPlayer(0x35b, 39.9f));
    // The boundary fires: fcomp sets C3 alone at equality (odd parity, no jp).
    EXPECT_TRUE(R::Spell859RemovalFiresAtPlayer(0x35b, 40.0f));
    EXPECT_FALSE(R::Spell859RemovalFiresAtPlayer(0x35b, 40.01f));
    EXPECT_FALSE(R::Spell859RemovalFiresAtPlayer(0x35b, 55.0f));
    // Unordered (NaN) sets C0 and C3 together: even parity, the jp skips.
    EXPECT_FALSE(R::Spell859RemovalFiresAtPlayer(
        0x35b, std::numeric_limits<float>::quiet_NaN()));
    // Other spells never take this branch: 38 / 93 (ordinary missiles) and
    // 54 (0x36), whose stage 2 fires unconditionally on its own branch.
    EXPECT_FALSE(R::Spell859RemovalFiresAtPlayer(38, 0.0f));
    EXPECT_FALSE(R::Spell859RemovalFiresAtPlayer(0x36, 0.0f));
    EXPECT_FALSE(R::Spell859RemovalFiresAtPlayer(0, 0.0f));
}

// (C) The four early-outs before the at-player fire.
TEST(B1163MissileRules, Spell859RemovalEarlyOuts) {
    R::Spell859RemovalInputs in;
    in.actorObject = true;
    in.spellId = 0x35b;
    in.recordExists = true;
    in.targetType = R::kSpell859TargetType;  // spells_us.txt field 98
    EXPECT_EQ(R::kSpell859TargetType, 8);
    in.distXY = 10.0f;
    EXPECT_TRUE(R::Spell859RemovalFires(in));

    auto off = in; off.actorObject = false;   // 00477947 / 00477949
    EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.recordExists = false;       // 0047f4de / 0047f4e0
    EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.targetType = 0;             // 0047f4e6 / 0047f4ed
    EXPECT_FALSE(R::Spell859RemovalFires(off));
    off = in; off.distXY = 40.5f;             // the 40.0 gate
    EXPECT_FALSE(R::Spell859RemovalFires(off));

    // FUN_0044d0d0: 1 <= id < 10000.
    EXPECT_FALSE(R::SpellIdInRange(0));
    EXPECT_TRUE(R::SpellIdInRange(1));
    EXPECT_TRUE(R::SpellIdInRange(9999));
    EXPECT_FALSE(R::SpellIdInRange(10000));
}

// (C) The gate distance is 2-D: FUN_00439c41 with a 0.0 7th argument drops z.
TEST(B1163MissileRules, GateDistanceIsXY) {
    EXPECT_FLOAT_EQ(R::GateDistanceXY(0.0f, 0.0f, 30.0f, 40.0f), 50.0f);
    EXPECT_FLOAT_EQ(R::GateDistanceXY(10.0f, 10.0f, 10.0f, 10.0f), 0.0f);
    // 40 units apart in x: exactly on the boundary, so 859 fires.
    EXPECT_TRUE(R::Spell859RemovalFiresAtPlayer(
        0x35b, R::GateDistanceXY(0.0f, 0.0f, 40.0f, 0.0f)));
}

} // namespace
