// B893: ONE jump model. The defect: two independent jump arcs lived in the
// client --
//
//   1. the headless game thread: MovementComponent::UpdateJump() ran
//      `const float jump_duration_ms = 1000.0f; const float jump_height = 25.0f;`
//      (src/client/movement/movement_state.cpp, "increased for visibility",
//      B463) and its arc's Z reached the wire through SendPositionUpdate();
//
//   2. the render thread: PlayerCameraComponent integrated
//      `jumpVelocity 40` / `gravity 80` (include/client/graphics/
//      renderer_types.h) -- a 10-unit peak, 1 s arc -- whose Z reached the
//      wire through the PlayerPositionChanged intent (renderer_player_
//      physics.cpp: sendPositionUpdateToServer).
//
// In Player Mode BOTH ran at once (Space is consumed by the game thread via
// InputActionBridge -> ActionDispatcher::jump AND polled by the render
// thread in readMovementInput), so the on-screen arc and the wire Z were two
// different parabolas from the same keypress. This test pins the collapse:
// every arc constant lives in include/client/jump_fall_physics.h and neither
// integrator may carry its own copy. It also pins the fall-damage table
// (shape now, values from the lane-2 oracle when it lands) and the
// EnvDamage encoder, which is byte-exact to the pcap.

#include <gtest/gtest.h>

#include "client/jump_fall_physics.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

using eqt::jumpfall::Arc;
using eqt::jumpfall::Fall;
using eqt::jumpfall::FallTracker;

namespace {

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in) {
        return lines;
    }
    std::string l;
    while (std::getline(in, l)) {
        lines.push_back(l);
    }
    return lines;
}

// Strip the code out of a comment line; a number mentioned in a comment does
// not count as a writer.
std::string CodePart(const std::string& line) {
    // These scans are line-based and single-line-comment-free by construction
    // in the two files they cover (checked: the arc constants are whole-line
    // `const float` declarations).
    return line;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

namespace {

// =====================================================================
// The curve itself
// =====================================================================

TEST(JumpFallArc, TheCurvePeaksAtHalfDurationAndReturnsToZero) {
    const Arc a{10.0f, 1.0f};
    EXPECT_NEAR(a.jumpHeightAt(0.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(a.jumpHeightAt(a.durationS), 0.0f, 1e-6f);
    EXPECT_NEAR(a.jumpHeightAt(a.durationS * 0.5f), a.peakHeight, 1e-4f);
    // Out of range clamps to 0 (landing), not to a negative arc.
    EXPECT_NEAR(a.jumpHeightAt(a.durationS + 5.0f), 0.0f, 1e-6f);
    EXPECT_TRUE(a.elapsed(a.durationS));
    EXPECT_FALSE(a.elapsed(a.durationS * 0.5f));
}

TEST(JumpFallArc, PeakHeightAndDurationAreOnePairOfConstants) {
    const Arc& a = eqt::jumpfall::arc();
    // The table, not the integrators: both must read these two numbers.
    // (Leg C replaces the [ASSUMED] values with the oracle's; the SHAPE
    // -- one peak, one duration -- is the invariant.)
    EXPECT_GT(a.peakHeight, 0.0f);
    EXPECT_GT(a.durationS, 0.0f);
}

TEST(JumpFallArc, DerivedPerFrameConstantsMatchTheTable) {
    // The two derived per-frame constants the renderer's velocity integrator
    // needs are functions of the ONE (peakHeight, durationS) pair:
    //   initialVelocity = 4*peak/duration,  gravity = 8*peak/duration^2
    // Pinning them against the table (not a bare pair of literals) is what
    // keeps the two integrators honest: if renderer_types.h ever seeded one
    // from a literal that drifted, this would catch it. At the measured
    // peak (3.05, lane-2 capture) and 1.0 s duration these resolve to
    // 4*3.05 = 12.2 and 8*3.05 = 24.4. (The pre-B893 renderer ran 40/80 from
    // its own hardcoded pair -- the two models disagreed because each owned its
    // own (peak, duration); the collapse makes them one pair.)
    const Arc& a = eqt::jumpfall::arc();
    EXPECT_FLOAT_EQ(a.initialVelocity(), 4.0f * a.peakHeight / a.durationS);
    EXPECT_FLOAT_EQ(a.gravity(), 8.0f * a.peakHeight / (a.durationS * a.durationS));
    // And the concrete measured values, so a change to either integrator's
    // derivation (not just the table) fails here:
    EXPECT_FLOAT_EQ(a.initialVelocity(), 12.2f);
    EXPECT_FLOAT_EQ(a.gravity(), 24.4f);
}

// =====================================================================
// The collapse: no second writer of an arc constant
// =====================================================================

// The two integrators' files must no longer DECLARE their own arc constants.
// The pre-fix literals (the 25-unit "increased for visibility" height and the
// 1000 ms duration in movement_state.cpp; the 40/80 velocity/gravity pair in
// renderer_types.h) are searched for here as whole-line const declarations,
// so a comment quoting them does not trip the scan.
TEST(JumpFallArc, NoSecondWriterOfTheArcConstants) {
    const std::filesystem::path root = WILLEQ_REPO_ROOT;

    // (1) The headless integrator: the 25.0f / 1000.0f literals.
    const auto movement = ReadLines((root / "src/client/movement/movement_state.cpp").string());
    for (const auto& l : movement) {
        const auto t = Trim(CodePart(l));
        EXPECT_FALSE(t.find("const float jump_duration_ms") != std::string::npos)
            << "B893: movement_state.cpp declares its own jump duration: " << t
            << " -- use eqt::jumpfall::arc().durationS";
        EXPECT_FALSE(t.find("const float jump_height") != std::string::npos)
            << "B893: movement_state.cpp declares its own jump height: " << t
            << " -- use eqt::jumpfall::arc().peakHeight";
    }

    // (2) The renderer's movement struct: jumpVelocity/gravity may be
    //     initialized in the header ONLY from the shared table (an
    //     `= eqt::jumpfall::arc().initialVelocity()`-style derivation is the
    //     single writer, still reading the one (peak, duration) pair). A BARE
    //     numeric literal -- `= 40.0f` / `= 80.0f` -- is the second writer:
    //     it would drift from the table if only one changed.
    const auto types = ReadLines((root / "include/client/graphics/renderer_types.h").string());
    for (const auto& l : types) {
        const auto t = Trim(CodePart(l));
        if (t.rfind("float jumpVelocity", 0) == 0 || t.rfind("float gravity", 0) == 0) {
            const bool hasEq = t.find("= ") != std::string::npos;
            const bool fromTable = t.find("eqt::jumpfall::") != std::string::npos;
            EXPECT_FALSE(hasEq && !fromTable)
                << "B893: renderer_types.h initializes an arc constant with a "
                   "bare literal: "
                << t << " -- seed it from eqt::jumpfall::arc() instead";
        }
    }
}

// =====================================================================
// The fall: the client owns height -> damage
// =====================================================================

TEST(JumpFallFall, NoDamageWithinTheSafeHeight) {
    const Fall f = eqt::jumpfall::fall();
    EXPECT_EQ(eqt::jumpfall::damageForFallHeight(0.0f), 0u);
    EXPECT_EQ(eqt::jumpfall::damageForFallHeight(f.safeHeight), 0u);
    EXPECT_EQ(eqt::jumpfall::damageForFallHeight(f.safeHeight - 0.5f), 0u);
}

TEST(JumpFallFall, DamageGrowsWithTheHeightAboveSafe) {
    const Fall f = eqt::jumpfall::fall();
    EXPECT_GT(eqt::jumpfall::damageForFallHeight(f.safeHeight + 1.0f), 0u);
    EXPECT_GT(eqt::jumpfall::damageForFallHeight(f.safeHeight + 50.0f),
              eqt::jumpfall::damageForFallHeight(f.safeHeight + 1.0f));
    EXPECT_GT(eqt::jumpfall::damageForFallHeight(200.0f),
              eqt::jumpfall::damageForFallHeight(50.0f));
}

// The takeoff tracker: begin on the airborne rising edge, end on the landing.
// A jump landing back at its takeoff deals 0; a jump off a ledge deals the drop.
TEST(JumpFallFall, FallTrackerReportsTheDropAndClears) {
    FallTracker t;
    // Not airborne: end() deals nothing.
    EXPECT_FLOAT_EQ(t.end(0.0f), 0.0f);

    // A normal standing jump: takeoff and landing at the same height -> 0.
    t.begin(100.0f);
    EXPECT_FLOAT_EQ(t.end(100.0f), 0.0f);
    EXPECT_FALSE(t.airborne);

    // A jump off a 12-unit ledge: the drop is the takeoff minus the landing.
    t.begin(100.0f);
    EXPECT_FLOAT_EQ(t.end(88.0f), 12.0f);
    EXPECT_FALSE(t.airborne);

    // Landing ABOVE the takeoff (a step up) clamps to 0, not negative.
    t.begin(100.0f);
    EXPECT_FLOAT_EQ(t.end(103.0f), 0.0f);
}

// =====================================================================
// The encoder: byte-exact to the pcap (envdamage packet, 0x31b3 C->S)
// =====================================================================

TEST(JumpFallFall, TheEncoderIsByteExact) {
    unsigned char buf[31];
    eqt::jumpfall::encodeEnvDamage(buf, 1234, eqt::jumpfall::kEnvDamageFalling);

    EXPECT_EQ(buf[0], 0x00);  // id = 0 (self)
    EXPECT_EQ(buf[4], 0x00);  // unknown4 low
    EXPECT_EQ(buf[5], 0x00);  // unknown4 high
    // damage = 1234 = 0x04D2 little-endian
    EXPECT_EQ(buf[6], 0xD2);
    EXPECT_EQ(buf[7], 0x04);
    EXPECT_EQ(buf[8], 0x00);
    EXPECT_EQ(buf[9], 0x00);
    for (int i = 10; i < 22; ++i) EXPECT_EQ(buf[i], 0x00);  // unknown10[12]
    EXPECT_EQ(buf[22], 252);  // dmgtype = Falling
    for (int i = 23; i < 27; ++i) EXPECT_EQ(buf[i], 0x00);  // unknown2[4]
    EXPECT_EQ(buf[27], 0xFF);  // constant 0xFFFF (the pcap's +27)
    EXPECT_EQ(buf[28], 0xFF);
    EXPECT_EQ(buf[29], 0x00);  // unknown29
    EXPECT_EQ(buf[30], 0x00);
}

TEST(JumpFallFall, DmgTypeFallingIs252) {
    // eqemu EQ::constants::EnvironmentalDamage::Falling
    // (code/common/emu_constants.h:308).
    EXPECT_EQ(eqt::jumpfall::kEnvDamageFalling, 252u);
}

}  // namespace
