// T438 (board B421/B422/B423): the door-motion constants, MEASURED.
//
// Every number asserted here came off the official Titanium client under the
// oracle rig on 2026-09-07, not off an argument. B421 was the blocker -- T433
// proved the client's 3D door pick refuses every synthetic click, so nothing
// could be made to animate -- and T438 unblocked it with a temporary quest
// fixture calling eqemu's own `Doors::ForceOpen()`, which emits the same
// OP_MoveDoor a click does. The evidence for each figure is in
// include/client/graphics/door_motion.h and review/T438/README.md.
//
// The point of this file is that the four constants fable used to invent
// cannot drift back: each case asserts the measured value AND rejects the
// value fable shipped before this unit.

#include <gtest/gtest.h>

#include "client/graphics/door_motion.h"

#include <cmath>

using namespace EQT::Graphics;

namespace {

// B606 (#189) deleted the production LiftTravelSeconds() (zero callers); its
// measured arithmetic -- door_param / kLiftUnitsPerSecond -- is what these
// cases pin, so the net computes it locally.
inline float liftTravelSeconds(uint32_t doorParam) {
    return static_cast<float>(doorParam) / DoorMotion::kLiftUnitsPerSecond;
}

// --------------------------------------------------------------------------
// The constants
// --------------------------------------------------------------------------

// Five open/close cycles of gfaydark FAYDOOR 30, 30 fps video stepped frame by
// frame: the open rotation spanned 0.967/0.933/0.967/0.933/0.933 s and the
// close 0.633/0.567/0.567/0.567/0.600 s (review/T438/logs/diff_x5.tsv).
TEST(DoorMotionConstants, HingedRotationIsTheMeasuredPair) {
    EXPECT_NEAR(DoorMotion::kHingedOpenSeconds, 0.95f, 0.05f);
    EXPECT_NEAR(DoorMotion::kHingedCloseSeconds, 0.60f, 0.05f);
    // ...and the two directions are NOT the same duration. fable had one
    // ANIM_SPEED for both.
    EXPECT_GT(DoorMotion::kHingedOpenSeconds, DoorMotion::kHingedCloseSeconds);
}

TEST(DoorMotionConstants, TheInventedHalfSecondRotationCannotComeBack) {
    // DoorManager::ANIM_SPEED was 2.0f -- "complete animation in ~0.5 seconds",
    // written from nothing. Both measured durations are outside it.
    EXPECT_GT(DoorMotion::kHingedOpenSeconds, 0.6f);
    EXPECT_GT(DoorMotion::kHingedCloseSeconds, 0.55f);
}

// eqemu transmits NO close for an ordinary door
// (/mnt/projects/akk-stack/code/zone/doors.cpp:138 sends one only for
// open_type 40 or trigger_type 1), and the official client closed it anyway,
// 11.00/11.07/11.10/11.03/11.00 s after the open began.
TEST(DoorMotionConstants, TheClientClosesTheDoorItself) {
    EXPECT_NEAR(DoorMotion::kHingedAutoCloseSeconds, 11.04f, 0.10f);
    // It must outlast the open rotation, or a door would start closing while
    // it is still swinging open.
    EXPECT_GT(DoorMotion::kHingedAutoCloseSeconds, DoorMotion::kHingedOpenSeconds);
}

// steamfont WMBLADE (opentype 105), 30 fps: the blade's horizontal centroid
// autocorrelates -0.861 at lag 64 frames and +0.822 at 128, so the period is
// 128 frames = 4.267 s for a FULL turn.
TEST(DoorMotionConstants, SpinIsAFullTurnIn4267Milliseconds) {
    const float period = 360.0f / DoorMotion::kSpinDegreesPerSecond;
    EXPECT_NEAR(period, 4.267f, 0.05f);
}

TEST(DoorMotionConstants, TheHalfTurnMisreadingCannotComeBack) {
    // fable's SPIN_SPEED was 42.35 deg/s -- "~180 degrees per 4.25 seconds".
    // The period is right; the arc was half of it. 42.35 * 2 = 84.70, which is
    // the measured rate to 0.4%.
    EXPECT_GT(DoorMotion::kSpinDegreesPerSecond, 60.0f);
    EXPECT_NEAR(DoorMotion::kSpinDegreesPerSecond, 2.0f * 42.35f, 1.0f);
}

// Two lifts of different door_param settle speed and dwell together: door 69
// (param 68) ran open-to-return in 28.33 s, door 77 (param 98) in 31.79 s, so
// 30 extra units cost 3.46 s.
TEST(DoorMotionConstants, LiftSpeedIsConstantAcrossDoorParam) {
    EXPECT_NEAR(DoorMotion::kLiftUnitsPerSecond, 8.67f, 0.20f);
    const float t68 = liftTravelSeconds(68);
    const float t98 = liftTravelSeconds(98);
    EXPECT_NEAR(t98 - t68, 3.46f, 0.15f);
    EXPECT_NEAR(t68, 7.84f, 0.20f);
}

TEST(DoorMotionConstants, LiftDwellDoesNotDependOnTheDistance) {
    EXPECT_NEAR(DoorMotion::kLiftDwellSeconds, 20.5f, 0.5f);
    // Both lifts' measured open->return gaps must reproduce from the two
    // constants alone, which is the whole content of "the dwell is fixed".
    EXPECT_NEAR(liftTravelSeconds(68) + DoorMotion::kLiftDwellSeconds, 28.33f, 0.30f);
    EXPECT_NEAR(liftTravelSeconds(98) + DoorMotion::kLiftDwellSeconds, 31.79f, 0.30f);
}

// --------------------------------------------------------------------------
// Which doors travel
// --------------------------------------------------------------------------

TEST(DoorMotionClassification, OnlyOpentype59WithAParamTravels) {
    EXPECT_TRUE(DoorTravels(59, 68));
    EXPECT_TRUE(DoorTravels(59, 1));
    // T433's census: 21 era opentype-59 rows carry door_param 0 (grobb signs,
    // acrylia torches, the arena tower). They are decorative and must keep
    // standing still.
    EXPECT_FALSE(DoorTravels(59, 0));
    // Nothing else translates, whatever its param.
    EXPECT_FALSE(DoorTravels(0, 68));
    EXPECT_FALSE(DoorTravels(5, 68));
    EXPECT_FALSE(DoorTravels(105, 68));
    EXPECT_FALSE(DoorTravels(54, 68));
}

// --------------------------------------------------------------------------
// The cycle
// --------------------------------------------------------------------------

// Step a cycle at a fixed dt and report when each phase boundary happened.
struct CycleTrace {
    float leftFarEnd = -1.0f;    // time the platform started moving
    float reachedNear = -1.0f;   // time it arrived at the spawn end
    float startedBack = -1.0f;   // time it left again
    float parked = -1.0f;        // time it was home
    float minOffset = 1e9f;
    float maxOffset = -1e9f;
};

CycleTrace runCycle(LiftCycle& c, float dt, float seconds) {
    CycleTrace t;
    LiftPhase prev = c.phase();
    for (float now = 0.0f; now < seconds; now += dt) {
        c.advance(dt);
        t.minOffset = std::min(t.minOffset, c.offset());
        t.maxOffset = std::max(t.maxOffset, c.offset());
        const LiftPhase p = c.phase();
        if (p != prev) {
            if (p == LiftPhase::Dwell) t.reachedNear = now + dt;
            if (p == LiftPhase::Inbound) t.startedBack = now + dt;
            if (p == LiftPhase::Parked) t.parked = now + dt;
            prev = p;
        }
    }
    return t;
}

TEST(LiftCycleTest, AtRestThePlatformSitsAtTheFarEnd) {
    LiftCycle c;
    c.init(68.0f);
    EXPECT_FLOAT_EQ(c.offset(), 68.0f);
    EXPECT_FALSE(c.active());
    EXPECT_FALSE(c.moving());
    // And it stays there with no trigger, however long the frame loop runs.
    c.advance(5.0f);
    EXPECT_FLOAT_EQ(c.offset(), 68.0f);
}

TEST(LiftCycleTest, OneMoveDoorRunsTravelDwellAndReturn) {
    LiftCycle c;
    c.init(68.0f);
    c.trigger();
    EXPECT_TRUE(c.moving());
    const CycleTrace t = runCycle(c, 1.0f / 60.0f, 45.0f);

    // Out in door_param / 8.67 seconds...
    EXPECT_NEAR(t.reachedNear, liftTravelSeconds(68), 0.10f);
    // ...dwell 20.5 s...
    EXPECT_NEAR(t.startedBack - t.reachedNear, DoorMotion::kLiftDwellSeconds, 0.10f);
    // ...and home again by itself, with no second packet.
    EXPECT_NEAR(t.parked - t.startedBack, liftTravelSeconds(68), 0.10f);
    EXPECT_FLOAT_EQ(c.offset(), 68.0f);
    EXPECT_FALSE(c.active());

    // It travels exactly door_param units and never overshoots either end.
    EXPECT_NEAR(t.minOffset, 0.0f, 0.01f);
    EXPECT_NEAR(t.maxOffset, 68.0f, 0.01f);
}

TEST(LiftCycleTest, TravelTimeScalesWithDoorParamAndSpeedDoesNot) {
    LiftCycle a, b;
    a.init(68.0f);
    b.init(98.0f);
    a.trigger();
    b.trigger();
    const CycleTrace ta = runCycle(a, 1.0f / 60.0f, 60.0f);
    const CycleTrace tb = runCycle(b, 1.0f / 60.0f, 60.0f);
    EXPECT_NEAR(tb.reachedNear - ta.reachedNear, 3.46f, 0.15f);
    // Same speed: units per second is the same to within a frame's worth.
    EXPECT_NEAR(68.0f / ta.reachedNear, 98.0f / tb.reachedNear, 0.1f);
}

TEST(LiftCycleTest, ASecondMoveDoorMidCycleIsIgnored) {
    // Measured: eqemu's own close packet arrives 5 s into lift 69's ride and
    // the official client does not react to it -- the second run with a 45 s
    // close timer produced identical motion to the run with a 5 s one.
    LiftCycle c;
    c.init(68.0f);
    c.trigger();
    for (int i = 0; i < 120; ++i) c.advance(1.0f / 60.0f);   // 2 s in
    const float midway = c.offset();
    c.trigger();
    EXPECT_FLOAT_EQ(c.offset(), midway);
    EXPECT_EQ(c.phase(), LiftPhase::Outbound);
}

TEST(LiftCycleTest, ADecorativeLiftNeverMoves) {
    LiftCycle c;
    c.init(0.0f);
    c.trigger();
    c.advance(5.0f);
    EXPECT_FLOAT_EQ(c.offset(), 0.0f);
    EXPECT_FALSE(c.active());
}

TEST(LiftCycleTest, TheOffsetIsFrameRateIndependent) {
    LiftCycle fast, slow;
    fast.init(68.0f);
    slow.init(68.0f);
    fast.trigger();
    slow.trigger();
    for (int i = 0; i < 300; ++i) fast.advance(1.0f / 60.0f);   // 5 s at 60 Hz
    for (int i = 0; i < 25; ++i) slow.advance(0.2f);            // 5 s at 5 Hz
    EXPECT_NEAR(fast.offset(), slow.offset(), 0.05f);
}

// --------------------------------------------------------------------------
// The rider
// --------------------------------------------------------------------------

TEST(LiftRideTest, FeetOnTheSurfaceRide) {
    EXPECT_TRUE(PlayerRidesLift(5.81f, 5.81f));
    EXPECT_TRUE(PlayerRidesLift(5.81f, 5.0f));
    EXPECT_TRUE(PlayerRidesLift(5.81f, 7.0f));   // the platform got ahead of us
}

TEST(LiftRideTest, FeetNowhereNearItDoNot) {
    EXPECT_FALSE(PlayerRidesLift(5.81f, -0.09f));   // standing on the forest floor
    EXPECT_FALSE(PlayerRidesLift(-0.09f, 5.81f));   // the platform is overhead
}

}  // namespace
