// B1163 (#482): device-free tests for the two missile follow-ups from B1152.
//
//  (A) The per-record "run" speed is a RUNTIME quantity (elapsed-since-stage x
//      the live target velocity, eqgame 00476c12-00476d2f) -- not a file field
//      -- so the actor flies at one CALIBRATED speed. This pins that the speed
//      is the constant 30 u/s and that the timing formula reproduces the
//      record-80 oracle flight (9.05 units over 0.30 s).
//
//  (C) Spell 859's distance-gated 0x47f614 branch (eqgame 00477962 cmp eax,
//      0x35b -> 004779cb fcomp [0x645ec0]=40.0 -> 00477a4e call 0x47f614) is a
//      no-draw network + velocity step, so fable implements no draw for it.
//      This pins the traced gate (the branch fires iff spell==859 AND
//      dist<40.0) so the rule survives and the contrast with spell 54's
//      unconditional stage-2 fire (0x49b260) is documented.
//
// Device-free: includes only the pure rules header
// (client/graphics/spell_b1163_missile_rules.h, no <irrlicht.h>), so it builds
// and runs in both the graphics and headless trees with no display.

#include <gtest/gtest.h>

#include "client/graphics/spell_b1163_missile_rules.h"

namespace {

// (A) The calibrated missile speed is a constant, and the timing formula
// reproduces the record-80 oracle (refactor/B1152/missile_oracle.tsv, spell 38:
// flight 472.333-472.633 s = 0.30 s over 9.05 units = 30 u/s, TRACE sec. 3/8).
TEST(B1163MissileRules, MissileSpeedIsCalibratedConstantNotPerRecord) {
    const float speed = EQ::SpellMissileRules::kMissileSpeed;
    const float dist  = EQ::SpellMissileRules::kMissileFlightDistance;
    EXPECT_FLOAT_EQ(speed, 30.0f);
    EXPECT_FLOAT_EQ(dist, 9.05f);

    // The flight time is distance / speed -- a constant, NOT a per-record value
    // (the runtime-only proof lives in the rules header) -- and lands in the
    // record-80 oracle's 0.30 s flight band.
    const float flightTime = EQ::SpellMissileRules::MissileFlightTime();
    EXPECT_NEAR(flightTime, 9.05f / 30.0f, 1e-6);
    EXPECT_NEAR(flightTime, 0.30, 0.02);

    // The reach/land offsets reproduce the record-80 oracle clock (OP_Action at
    // 472.817 s; flight ends at 472.633 s; the Target stage fires at 472.933 s).
    const double hitAt   = 472.817;
    const double reachAt = EQ::SpellMissileRules::MissileReachAt(hitAt);
    const double landAt  = EQ::SpellMissileRules::MissileLandAt(reachAt);
    EXPECT_NEAR(reachAt, 472.633, 0.005);
    EXPECT_NEAR(landAt, 472.933, 0.005);
    // The flight starts reachAt - flightTime ~= the oracle's 472.333 s.
    EXPECT_NEAR(reachAt - flightTime, 472.333, 0.02);
}

// (C) The oracle's spell-859 gate: the 0x47f614 branch fires iff the spell is
// 859 (0x35b) AND the effect is within 40.0 units of the player ([0x645ec0]).
// The callee is a no-draw network + velocity step, so fable draws nothing for
// it; the traced rule is encoded here (against the production constants in
// spell_b1163_missile_rules.h) and pinned, with the contrast to spell 54
// (0x36), whose stage-2 fire (0x49b260) has no distance gate.
static bool b1163_859GatedBranch(uint32_t spellId, float distToPlayer) {
    return spellId == EQ::SpellMissileRules::kSpell859
        && distToPlayer < EQ::SpellMissileRules::kSpell859GateDistance;
}

TEST(B1163MissileRules, Spell859GatedBranch) {
    EXPECT_EQ(EQ::SpellMissileRules::kSpell859, 0x35b);
    EXPECT_FLOAT_EQ(EQ::SpellMissileRules::kSpell859GateDistance, 40.0f);

    // 859 within 40 units -> the oracle fires the gated branch.
    EXPECT_TRUE(b1163_859GatedBranch(0x35b, 39.9f));
    EXPECT_TRUE(b1163_859GatedBranch(0x35b, 10.0f));
    // 859 at/beyond 40 units -> it does not.
    EXPECT_FALSE(b1163_859GatedBranch(0x35b, 40.0f));
    EXPECT_FALSE(b1163_859GatedBranch(0x35b, 55.0f));
    // A different spell -> no, even when close: spell 54 (0x36) fires
    // unconditionally (no distance gate), and an absent spell (0) never does.
    EXPECT_FALSE(b1163_859GatedBranch(0x36, 10.0f));
    EXPECT_FALSE(b1163_859GatedBranch(0, 10.0f));
}

} // namespace
