// T438 Leg C (board B424): a discrete relocation cancels an in-flight jump.
//
// B424 was reported as "fable resolves ground ~71 units too high at the
// gfaydark Kelethin lift" and survived two units under that description.
// T435 refuted the stated mechanism and stopped. T438 instrumented every
// writer of the game-thread player z with its CALL SITE, ran one `#goto`, and
// read the answer off the log: `MovementComponent::UpdateJump()` finishes a
// jump by writing `jumpStartZ` FLAT, and `jumpStartZ` was captured before the
// relocation. The commanded z survived three milliseconds
// (review/T438/logs/fable_session_legc.log; the trace is quoted in full in
// src/client/state/player_state.cpp above the fix).
//
// The fix lives in PlayerState because that is the one store: setPosition() is
// the discrete-relocation path (zone-in, bind, `#goto`, the T332 echo) and
// setPositionNoEvent() is the per-tick integrator, which must NOT cancel
// anything or a jump would end on its first frame.

#include <gtest/gtest.h>

#include "client/state/player_state.h"

using eqt::state::PlayerState;

namespace {

TEST(JumpRelocationTest, ARelocationEndsTheJump) {
    PlayerState p;
    p.setPositionNoEvent(-320.0f, -404.0f, 14.4944f);
    p.setJumping(true);
    p.setJumpStartZ(14.4944f);

    // The `#goto` lands mid-arc.
    p.setPosition(137.0f, 340.0f, 6.0f);

    EXPECT_FALSE(p.isJumping());
    EXPECT_FLOAT_EQ(p.z(), 6.0f);
    // ...and the landing height is rebased, so nothing can restore the old z
    // even if a stale tick runs before the flag is read.
    EXPECT_FLOAT_EQ(p.jumpStartZ(), 6.0f);
}

TEST(JumpRelocationTest, TheStaleLandingHeightIsWhatB424Was) {
    // Pre-fix behaviour, spelled out: with the jump left running, the arc's
    // final write is jumpStartZ -- the pre-goto z -- at the post-goto x/y.
    // That is exactly the `137.0, 340.0, 77.0` /loc B424 reported.
    PlayerState p;
    p.setJumping(true);
    p.setJumpStartZ(77.0f);
    p.setPosition(137.0f, 340.0f, 6.0f);
    // The jump is over, so UpdateJump()'s "return to original height" branch
    // cannot run at all; and if the flag were somehow still set, the height it
    // would write is now the relocation's own z rather than 77.0.
    EXPECT_FALSE(p.isJumping());
    EXPECT_NE(p.jumpStartZ(), 77.0f);
}

TEST(JumpRelocationTest, TheIntegratorPathDoesNotCancelAJump) {
    // setPositionNoEvent() is the per-tick movement write, including the
    // jump's own arc. If it cancelled, a jump would end on its first frame.
    PlayerState p;
    p.setJumping(true);
    p.setJumpStartZ(10.0f);
    p.setPositionNoEvent(0.0f, 0.0f, 12.0f);
    EXPECT_TRUE(p.isJumping());
    EXPECT_FLOAT_EQ(p.jumpStartZ(), 10.0f);
}

TEST(JumpRelocationTest, ARelocationWithNoJumpRunningIsUnchanged) {
    PlayerState p;
    p.setJumpStartZ(3.0f);
    p.setPosition(1.0f, 2.0f, 3.5f);
    EXPECT_FALSE(p.isJumping());
    // Untouched: only an ACTIVE jump gets rebased.
    EXPECT_FLOAT_EQ(p.jumpStartZ(), 3.0f);
}

}  // namespace
