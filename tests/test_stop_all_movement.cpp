/**
 * T343 (board B032) -- what /stop stops.
 *
 * B032 read "the keyboard movement START half has zero callers while the STOP
 * half is called ... the switch has no on position". T343 measured the other
 * end and REFUTED the premise behind keeping the cluster: the renderer's
 * per-frame phase 1 (readMovementInput(), renderer_player_physics.cpp) polls
 * HotkeyManager for MoveForward / MoveBackward / TurnLeft / TurnRight /
 * StrafeLeft / StrafeRight / Jump / SwimUp / SwimDown every frame and writes
 * the whole of PlayerMovementState from them -- forward/back/turn are driven
 * by the poll exactly as swim is (T334, board B046). MovementComponent's
 * StartMove, StartTurn, StopMove and StopTurn family was a SECOND, parallel
 * route whose only possible producer would have been key-down/key-up pairs from the
 * graphical client, which does not use it; the console handler is line-based
 * and has no continuous movement keys. So the cluster was deleted, along with
 * UpdateKeyboardMovement() and the four PlayerState flags nothing else wrote.
 *
 * Its one reachable call site, EqActionHandler::stopAllMovement() (the /stop
 * command, and InputActionBridge's MoveCommand::Type::Stop), was the thing
 * that had to change rather than disappear -- and it was BROKEN, which is what
 * this file pins. It called the four Stop* methods, whose only real effect was
 * to clear m_isMoving and send a position update; NONE of them touched the
 * movement target or the path. So /stop during a MoveTo left targetX/Y/Z set
 * and the very next UpdateMovement() tick resumed walking to it. It now calls
 * StopMovement() -- public, despite the deleted comment that claimed otherwise
 * -- which clears the path AND the target.
 *
 * PRE-FIX: StopsAnActiveMovementTarget fails on the target assertions (the
 * target survives stopAllMovement()).
 */
#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/movement/movement_component.h"
#include "zone_stream_test_utils.h"

using namespace zonetest;
using eqt::EqActionHandler;

namespace {

struct Harness {
    EverQuest eq = MakeEq();
    TestBridge bridge;
    EqActionHandler handler{eq};
    Harness() { eq.setBridge(&bridge); }
    ~Harness() { eq.setBridge(nullptr); }
    eqt::state::PlayerState& player() { return eq.GetGameState().player(); }
};

TEST(StopAllMovement, StopsAnActiveMovementTarget) {
    Harness h;
    h.player().setMovementTarget(100.0f, 200.0f, 5.0f);
    h.player().setMoving(true);
    ASSERT_FLOAT_EQ(h.player().targetX(), 100.0f);

    h.handler.stopAllMovement();

    EXPECT_FALSE(h.eq.IsMoving());
    EXPECT_FLOAT_EQ(h.player().targetX(), 0.0f)
        << "/stop left the movement target set, so the next UpdateMovement() tick walks\n"
           "straight back to it -- the four Stop* calls never cleared it (B032).";
    EXPECT_FLOAT_EQ(h.player().targetY(), 0.0f);
    EXPECT_FLOAT_EQ(h.player().targetZ(), 0.0f);
}

// Idempotent and safe with nothing in flight: /stop on a standing player is a
// no-op, not a spurious position update.
TEST(StopAllMovement, IsANoOpWhenNothingIsMoving) {
    Harness h;
    ASSERT_FALSE(h.eq.IsMoving());
    h.handler.stopAllMovement();
    h.handler.stopAllMovement();
    EXPECT_FALSE(h.eq.IsMoving());
    EXPECT_FLOAT_EQ(h.player().targetX(), 0.0f);
}

}  // namespace
