/**
 * T343 (board B002) -- autorun, the toggle that had no writer.
 *
 * MEASURED before the fix. readMovementInput() (renderer_player_physics.cpp,
 * G06 phase 1) has always computed
 *   playerMovement_.moveForward = forward || playerMovement_.autorun;
 * and NOTHING in the tree ever assigned playerMovement_.autorun. The three
 * bound keys (grave, numpad +, Num Lock -- hotkey_manager.cpp and
 * config/hotkeys.json) reached RendererEventReceiver::OnEvent, which pushed
 * RendererAction::ToggleAutorun onto the BRIDGE queue; GraphicsInputHandler
 * turned that into InputAction::ToggleAutorun; InputActionBridge reported it
 * as a success through ActionDispatcher; and EqActionHandler::toggleAutorun()
 * was a LOG_DEBUG stub. Three keys, a whole action chain, a "success", and a
 * player who never moved.
 *
 * The flag is renderer-owned for the reason T334 recorded for swimUp/swimDown
 * (board B046): phase 1 polls the movement bindings every frame, so a
 * game-thread writer is overwritten on the next frame and a key-DOWN action
 * has no key-up to clear it. So the fix routes the hotkey to the renderer's
 * own action queue and gives processCommonInput() an arm, the T330 shape.
 *
 * What this file pins:
 *   - Behavioural, through the exact production consumer
 *     (FrameComponent::processCommonInput on a device-less renderer, the
 *     idiom of tests/test_debug_hotkey_arms.cpp): the action flips
 *     PlayerCameraComponent's autorun flag, on and back off, and disturbs
 *     nothing beside it.
 *   - Source-pinned: the receiver queues ToggleAutorun on actionQueue_ (NOT
 *     the bridge queue), so the arm cannot be orphaned from the other end.
 *   - Source-pinned: phase 1 still reads the flag into moveForward, and still
 *     carries the cancel-on-movement-key half that gives autorun its
 *     semantics (forward held until any movement key or the toggle again).
 *     That half is a behaviour statement in
 *     tests/goldens/movement_order_g06.txt as well, at statements 27-28.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<RendererEvent> One(RendererAction a) {
    return std::vector<RendererEvent>{RendererEvent(a)};
}

class AutorunArmTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
};

TEST_F(AutorunArmTest, ToggleAutorunFlipsTheAutorunFlag) {
    ASSERT_FALSE(renderer.PlayerCamera().autorun());
    renderer.Frame().processCommonInput(One(RendererAction::ToggleAutorun));
    EXPECT_TRUE(renderer.PlayerCamera().autorun())
        << "the autorun hotkey still reaches nothing that can write the flag (B002)";
    renderer.Frame().processCommonInput(One(RendererAction::ToggleAutorun));
    EXPECT_FALSE(renderer.PlayerCamera().autorun())
        << "autorun must toggle OFF on a second press, not latch on";
}

// The arm is its own case, not a fallthrough onto a neighbour.
TEST_F(AutorunArmTest, ANeighbouringActionDoesNotTurnAutorunOn) {
    renderer.Frame().processCommonInput(One(RendererAction::ToggleWireframe));
    EXPECT_FALSE(renderer.PlayerCamera().autorun());
    EXPECT_TRUE(renderer.Context().wireframeMode);
}

// --- Source-pinned: the two ends the behavioural test cannot reach ---

TEST(AutorunArmSourceInvariants, TheReceiverQueuesAutorunOnTheRendererActionQueue) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/renderer/renderer_event_receiver.cpp");
    EXPECT_NE(src.find("case HA::ToggleAutorun: actionQueue_.push_back({RA::ToggleAutorun});"),
              std::string::npos)
        << "the autorun key no longer reaches the renderer action queue, so the arm in\n"
           "processCommonInput() is orphaned and B002 is back.";
    EXPECT_EQ(src.find("case HA::ToggleAutorun: pushBridgeAction("), std::string::npos)
        << "autorun is back on the bridge queue, which routes it to the game thread --\n"
           "the thread that cannot write PlayerMovementState::autorun at all (B002).";
}

TEST(AutorunArmSourceInvariants, PhaseOneStillConsumesAndCancelsTheFlag) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/player/renderer_player_physics.cpp");
    EXPECT_NE(src.find("playerMovement_.moveForward = forward || playerMovement_.autorun;"),
              std::string::npos)
        << "phase 1 no longer folds autorun into moveForward; the toggle would write a\n"
           "flag nothing reads, which is B002 with the halves swapped.";
    EXPECT_NE(src.find("if (playerMovement_.autorun && (forward || backward || strafeLeft "
                       "|| strafeRight)) {"),
              std::string::npos)
        << "the cancel-on-movement-key half is gone: autorun would latch on until the\n"
           "toggle was pressed again, which is not what a movement key does in EQ.";
}

}  // namespace
