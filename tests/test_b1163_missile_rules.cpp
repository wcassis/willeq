// B1163 (#482): device-free tests for the two missile follow-ups from B1152.
// The addresses are instruction starts in review/B1141/ghidra/out/eqgame.dis;
// the full trace is in client/graphics/spell_b1163_missile_rules.h and
// refactor/B1163/README.md.
//
//  (A) The missile speed is the missile's speed word [missile+0x28]: 1.5 per
//      physics tick for a spell missile (00477514), 0.8 for spell 0x35b
//      (0047750d cmp / 0047751d mov). A tick is 50 ms (FUN_004bfe0f:
//      elapsed x 0.02), so 1.5 x 20 = 30 u/s -- the speed B1152 measured for
//      record 80 -- and spell 859 flies at 16 u/s.
//
//  (C) Spell 859's removal gate (FUN_0047789d): 00477962 cmp eax, 0x35b, the
//      XY distance to the local player (004779c6 call 0x439c41) against
//      [0x645ec0] = 40.0 (004779cb fcomp / 004779d3 test ah, 0x41 /
//      004779d6 jp 0x477aab: taken when dist <= 40.0) -> 00477a4e call
//      0x47f614 with packet type 0xe7 -> 0047f630 call 0x47f4af ->
//      0047f5eb push 2 / 0047f5ed call 0x49b260: spell 859's Target stage
//      fired at the local player.
//
// Device-free: includes only the pure rules header (no <irrlicht.h>), so it
// builds and runs in both the graphics and headless trees with no display.

#include <gtest/gtest.h>

#include <limits>

#include "client/graphics/spell_b1163_missile_rules.h"

namespace R = EQ::SpellMissileRules;

namespace {

// (A) The speed comes from the speed word, per spell missile.
TEST(B1163MissileRules, MissileSpeedFromTheSpeedWord) {
    EXPECT_EQ(R::kSpell859, 0x35bu);
    EXPECT_FLOAT_EQ(R::kMissileSpeedWordSpell, 1.5f);   // 0x3fc00000
    EXPECT_FLOAT_EQ(R::kMissileSpeedWord859, 0.8f);     // 0x3f4ccccd
    EXPECT_FLOAT_EQ(R::kMissileTicksPerSecond, 20.0f);  // 1 / (50 ms)

    // Every spell but 859 carries the 1.5 word; 859 carries 0.8.
    EXPECT_FLOAT_EQ(R::MissileSpeedWord(38), 1.5f);
    EXPECT_FLOAT_EQ(R::MissileSpeedWord(93), 1.5f);
    EXPECT_FLOAT_EQ(R::MissileSpeedWord(0x35b), 0.8f);

    // kMissileSpeed is DERIVED: 1.5 u/tick x 20 ticks/s = 30 u/s exactly,
    // the record-80 speed B1152 measured (9.05 units over 0.30 s).
    EXPECT_FLOAT_EQ(R::kMissileSpeed, 30.0f);
    EXPECT_FLOAT_EQ(R::MissileSpeed(38), 30.0f);
    EXPECT_FLOAT_EQ(R::MissileSpeed(0x35b), 16.0f);

    // The flight over the calibrated gap: 0.302 s at 1.5, 0.566 s at 0.8.
    EXPECT_FLOAT_EQ(R::kMissileFlightDistance, 9.05f);
    EXPECT_NEAR(R::MissileFlightTime(38), 9.05f / 30.0f, 1e-6);
    EXPECT_NEAR(R::MissileFlightTime(0x35b), 9.05f / 16.0f, 1e-6);
    EXPECT_GT(R::MissileFlightTime(0x35b), R::MissileFlightTime(38));
}

// (A) The record-80 clock is unchanged by the derivation (missile_oracle.tsv
// M1_38: OP_Action 472.817 s, flight 472.333-472.633 s, arrival 472.933 s).
TEST(B1163MissileRules, Record80ClockUnchanged) {
    const double hitAt   = 472.817;
    const double reachAt = R::MissileReachAt(hitAt);
    const double landAt  = R::MissileLandAt(reachAt);
    EXPECT_NEAR(reachAt, 472.633, 0.005);
    EXPECT_NEAR(landAt, 472.933, 0.005);
    EXPECT_NEAR(reachAt - R::MissileFlightTime(38), 472.333, 0.02);
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

// (C) The gate distance is 2-D: FUN_00439c41 with a 0.0 7th argument drops z.
TEST(B1163MissileRules, GateDistanceIsXY) {
    EXPECT_FLOAT_EQ(R::GateDistanceXY(0.0f, 0.0f, 30.0f, 40.0f), 50.0f);
    EXPECT_FLOAT_EQ(R::GateDistanceXY(10.0f, 10.0f, 10.0f, 10.0f), 0.0f);
    // 40 units apart in x: exactly on the boundary, so 859 fires.
    EXPECT_TRUE(R::Spell859RemovalFiresAtPlayer(
        0x35b, R::GateDistanceXY(0.0f, 0.0f, 40.0f, 0.0f)));
}

} // namespace
