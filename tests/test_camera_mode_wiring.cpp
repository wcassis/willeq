/**
 * T461 -- the camera keys and the rig, wired end to end on a device-less
 * renderer. T471 -- the official client's measurements applied to them.
 *
 * MEASURED before T461's fix: RendererEventReceiver::OnEvent queued
 * RendererAction::ToggleCameraMode for the camera key (then Ctrl+F5) and
 * FrameComponent::processCommonInput() had NO arm for it -- the key fell
 * through `default: break` and did nothing, the T330/T343 unwired-handler
 * shape again. The board's triage ("bound Ctrl+F5") described a key that had
 * never reached cycleCameraMode(); only `/camera cycle` ever did.
 *
 * What this file pins:
 *   - Behavioural, through the exact production consumer
 *     (FrameComponent::processCommonInput, the idiom of
 *     tests/test_autorun_arm.cpp): the F9 action walks the official cycle
 *     from the official login camera; Pitch up / down move the ACTIVE user
 *     camera, and Pitch up tilts the VIEW up (T466's oracle: Alt+PgUp x10
 *     swung User 1 up to the sky); Tether-slide (CameraOrbitLeft/Right) and
 *     Raise / Lower move the Tether camera only.
 *   - Behavioural (T471, board B580): User Defined 1/2 and Tether hold a WORLD
 *     direction when the player turns, including on re-entry after the turn --
 *     the oracle's own protocol; Overhead turns with the player.
 *   - Behavioural (T471, board B581): entering Chase from any other camera
 *     places the Chase camera at once, on a real (EDT_NULL) camera node --
 *     since T474 at T466's oracle fit (tests/test_chase_camera_fit.cpp pins
 *     the fit itself).
 *   - Behavioural: zoom edits the active rig's distance; a player move leaves
 *     every rig alone.
 *   - Source-pinned: the receiver queues the camera actions on actionQueue_,
 *     so the arms cannot be orphaned from the other end; and the frame routes
 *     a UI-unconsumed wheel notch into zoomCamera().
 */
#include <gtest/gtest.h>

#include <irrlicht.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "client/graphics/camera_controller.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/player/camera_rig.h"
#include "client/graphics/player/player_camera_component.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;
constexpr float kPi = 3.14159265358979f;

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

class CameraModeWiringTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
    const CameraRig& Rig(CameraMode m) { return *renderer.PlayerCamera().cameraRigFor(m); }

    // `/camera get`'s "ideal camPos=... target=..." line for the active rig
    // mode: the pose the rig resolves to around the player right now.
    std::string IdealPose() {
        const auto lines = renderer.PlayerCamera().buildCameraGetLines();
        for (const auto& l : lines) {
            if (l.find("ideal camPos=") != std::string::npos) return l;
        }
        ADD_FAILURE() << "no ideal pose line for " << renderer.getCameraModeString();
        return {};
    }
};

TEST_F(CameraModeWiringTest, TheF9ActionWalksTheOfficialCycle) {
    // T471: starts on the official login camera (T466), then T466's measured
    // successor order, which is fable's.
    ASSERT_EQ(renderer.getCameraMode(), CameraMode::FirstPerson);
    const CameraMode order[] = {CameraMode::Overhead, CameraMode::Chase, CameraMode::User1,
                                CameraMode::User2, CameraMode::Tether,
                                CameraMode::FirstPerson};
    for (CameraMode expected : order) {
        renderer.Frame().processCommonInput(One(RendererAction::ToggleCameraMode));
        EXPECT_EQ(renderer.getCameraMode(), expected)
            << "the camera key reaches nothing that cycles the camera";
    }
}

TEST_F(CameraModeWiringTest, ThePitchActionsMoveTheActiveUserCamera) {
    renderer.PlayerCamera().setCameraMode(CameraMode::User2);
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchDown));
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).pitchDeg, kCameraOrbitKeyStepDeg);
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchUp));
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchUp));
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).pitchDeg, -kCameraOrbitKeyStepDeg);
    EXPECT_EQ(Rig(CameraMode::User1), kUser1DefaultRig) << "the inactive user camera moved";
}

// T471: the SIGN. T466 recorded that the official Pitch up tilts the view UP
// (review/T466/logs/oracle_adjust_keys_image_shift.tsv: User 1 after Alt+PgUp
// x10, the scene moved DOWN the screen by the whole +60 px search window) and
// boarded fable's CameraPitchUp as the opposite. The view direction is the
// look-at point minus the camera; Pitch up must make it point more upward.
TEST_F(CameraModeWiringTest, PitchUpTiltsTheViewUpAsTheOfficialKeyDoes) {
    renderer.PlayerCamera().setCameraMode(CameraMode::User1);
    auto viewUp = [this]() {
        const CameraRigPose p = computeCameraRigPose(Rig(CameraMode::User1), 0.0f, 0.0f, 0.0f, 0.0f);
        const float dx = p.targetX - p.camX, dy = p.targetY - p.camY, dz = p.targetZ - p.camZ;
        return dz / std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const float before = viewUp();
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchUp));
    EXPECT_GT(viewUp(), before + 0.01f) << "Pitch up tilted the view down";
    const float afterUp = viewUp();
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchDown));
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchDown));
    EXPECT_LT(viewUp(), afterUp - 0.01f) << "Pitch down tilted the view up";
}

// T471: CameraOrbitLeft/Right ARE the official LEFT_CAM/RIGHT_CAM, whose Options
// > Keys > Camera labels are "Tether-slide camera left/right" (eqstr 6694/6695,
// T466's screenshot). They move the TETHER camera. "Right" is the viewer's
// right: Irrlicht is left-handed and the renderer maps EQ (x, y, z) to
// (x, z, y), so for a horizontal view direction (fx, fy) the screen's right is
// (fy, -fx) in EQ client x/y.
TEST_F(CameraModeWiringTest, TetherSlideRightMovesTheTetherCameraToTheViewersRight) {
    renderer.PlayerCamera().setCameraMode(CameraMode::Tether);
    auto camXY = [this](float& x, float& y, float& fx, float& fy) {
        const CameraRigPose p = computeCameraRigPose(Rig(CameraMode::Tether), 0.0f, 0.0f, 0.0f, 0.0f);
        x = p.camX;
        y = p.camY;
        fx = p.targetX - p.camX;
        fy = p.targetY - p.camY;
    };
    float x0, y0, fx, fy;
    camXY(x0, y0, fx, fy);
    renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitRight));
    float x1, y1, unusedX, unusedY;
    camXY(x1, y1, unusedX, unusedY);
    const float right = (x1 - x0) * fy + (y1 - y0) * (-fx);
    EXPECT_GT(right, 0.1f) << "Tether-slide right did not move the Tether camera to the right";

    renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitLeft));
    renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitLeft));
    float x2, y2;
    camXY(x2, y2, unusedX, unusedY);
    const float left = (x2 - x1) * fy + (y2 - y1) * (-fx);
    EXPECT_LT(left, -0.1f) << "Tether-slide left did not move the Tether camera to the left";
}

// T471: each key reaches only the camera the oracle names it for. T466 pressed
// Tether-slide right x10 in User 1 and scored no change (0.047, re-grab noise).
TEST_F(CameraModeWiringTest, EachAdjustKeyMovesOnlyItsOwnCamera) {
    for (CameraMode m : {CameraMode::Chase, CameraMode::FirstPerson, CameraMode::Overhead,
                         CameraMode::Tether}) {
        renderer.PlayerCamera().setCameraMode(m);
        renderer.Frame().processCommonInput(One(RendererAction::CameraPitchDown));
        EXPECT_EQ(renderer.getCameraMode(), m);
    }
    for (CameraMode m : {CameraMode::Chase, CameraMode::FirstPerson, CameraMode::Overhead,
                         CameraMode::User1, CameraMode::User2}) {
        renderer.PlayerCamera().setCameraMode(m);
        renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitRight));
        EXPECT_EQ(renderer.getCameraMode(), m);
    }
    EXPECT_EQ(Rig(CameraMode::User1), kUser1DefaultRig);
    EXPECT_EQ(Rig(CameraMode::User2), kUser2DefaultRig);
    EXPECT_EQ(Rig(CameraMode::Overhead), kOverheadRig);
    EXPECT_EQ(Rig(CameraMode::Tether), kTetherPlaceholderRig);
    EXPECT_EQ(Rig(CameraMode::Chase), kChaseRig) << "T474: Chase is a rig; no adjust key moves it";
}

// T471: Raise / Lower camera move the Tether camera's look-at height and nothing
// else. T466 pressed Raise x10 in User 1 and scored no change (0.022).
TEST_F(CameraModeWiringTest, RaiseAndLowerMoveOnlyTheTetherLookAtHeight) {
    renderer.PlayerCamera().setCameraMode(CameraMode::Tether);
    renderer.Frame().processCommonInput(One(RendererAction::CameraRaise));
    EXPECT_FLOAT_EQ(Rig(CameraMode::Tether).height,
                    kTetherPlaceholderRig.height + kCameraRaiseKeyStepUnits);
    renderer.Frame().processCommonInput(One(RendererAction::CameraLower));
    renderer.Frame().processCommonInput(One(RendererAction::CameraLower));
    EXPECT_FLOAT_EQ(Rig(CameraMode::Tether).height,
                    kTetherPlaceholderRig.height - kCameraRaiseKeyStepUnits);

    for (CameraMode m : {CameraMode::User1, CameraMode::User2, CameraMode::Overhead,
                         CameraMode::Chase, CameraMode::FirstPerson}) {
        renderer.PlayerCamera().setCameraMode(m);
        renderer.Frame().processCommonInput(One(RendererAction::CameraRaise));
        EXPECT_EQ(renderer.getCameraMode(), m);
    }
    EXPECT_EQ(Rig(CameraMode::User1), kUser1DefaultRig);
    EXPECT_EQ(Rig(CameraMode::User2), kUser2DefaultRig);
    EXPECT_EQ(Rig(CameraMode::Overhead), kOverheadRig);
}

// T471 (board B580). MEASURED on the official client (T466,
// review/T466/logs/oracle_heading_relative_test.tsv and oracle_marks_session.tsv):
// the character was put at heading 160, each direct camera entered (Ctrl+F7,
// F3, F6, F2, F5, F4, each from a different camera) and captured; then
// `#goto` to heading 288 (+90 degrees) and each camera ENTERED again. Edge
// score h160 vs h288: User 1 0.084, User 2 0.120, Tether 0.193 -- a re-grab --
// while First Person 0.837, Overhead 0.689 and Chase 0.680 turned with the
// player. So the three hold a WORLD direction, and it is not captured from the
// heading at entry: an entry after the turn gives the same view.
TEST_F(CameraModeWiringTest, UserAndTetherCamerasHoldAWorldDirectionWhenThePlayerTurns) {
    for (CameraMode m : {CameraMode::User1, CameraMode::User2, CameraMode::Tether}) {
        renderer.setPlayerPosition(50.0f, -20.0f, 4.0f, 160.0f);
        renderer.PlayerCamera().setCameraMode(m);
        const std::string atH160 = IdealPose();

        // Turning while in the camera.
        renderer.Context().playerHeading = 288.0f;
        renderer.PlayerCamera().updateRigCamera();
        EXPECT_EQ(IdealPose(), atH160) << renderer.getCameraModeString()
                                       << " swung with the player's heading";

        // The oracle's protocol: leave, turn, ENTER again.
        renderer.PlayerCamera().setCameraMode(CameraMode::Overhead);
        renderer.setPlayerPosition(50.0f, -20.0f, 4.0f, 160.0f);
        renderer.setPlayerPosition(50.0f, -20.0f, 4.0f, 288.0f);
        renderer.PlayerCamera().setCameraMode(m);
        EXPECT_EQ(IdealPose(), atH160) << renderer.getCameraModeString()
                                       << " re-entered after a turn at a different world direction";
    }
}

TEST_F(CameraModeWiringTest, OverheadStillTurnsWithThePlayer) {
    renderer.setPlayerPosition(50.0f, -20.0f, 4.0f, 160.0f);
    renderer.PlayerCamera().setCameraMode(CameraMode::Overhead);
    const std::string atH160 = IdealPose();
    renderer.Context().playerHeading = 288.0f;
    renderer.PlayerCamera().updateRigCamera();
    EXPECT_NE(IdealPose(), atH160) << "Overhead is heading-relative on the oracle (0.689)";
}

TEST_F(CameraModeWiringTest, ZoomEditsTheActiveRigAndIsSafeEverywhere) {
    renderer.PlayerCamera().setCameraMode(CameraMode::User1);
    renderer.PlayerCamera().zoomCamera(kCameraZoomStepUnits, 0.016f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).distance,
                    kUser1DefaultRig.distance + kCameraZoomStepUnits);
    renderer.PlayerCamera().zoomCamera(-1000.0f, 0.016f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).distance, kCameraRigMinDistance);

    // First person does not zoom; Chase with no camera controller must not
    // dereference it.
    renderer.PlayerCamera().setCameraMode(CameraMode::FirstPerson);
    EXPECT_NO_FATAL_FAILURE(renderer.PlayerCamera().zoomCamera(2.0f, 0.016f));
    renderer.PlayerCamera().setCameraMode(CameraMode::Chase);
    EXPECT_NO_FATAL_FAILURE(renderer.PlayerCamera().zoomCamera(2.0f, 0.016f));
}

TEST_F(CameraModeWiringTest, APlayerMoveLeavesEveryRigAlone) {
    renderer.PlayerCamera().setCameraMode(CameraMode::User1);
    renderer.PlayerCamera().orbitCamera(45.0f, 12.0f);
    const CameraRig user1 = Rig(CameraMode::User1);
    for (CameraMode m : {CameraMode::User1, CameraMode::Overhead, CameraMode::Tether}) {
        renderer.PlayerCamera().setCameraMode(m);
        renderer.setPlayerPosition(310.0f, -42.0f, 7.0f, 200.0f);
        renderer.Context().playerHeading = 12.0f;
        renderer.PlayerCamera().updateRigCamera();
    }
    EXPECT_EQ(Rig(CameraMode::User1), user1);
    EXPECT_EQ(Rig(CameraMode::Overhead), kOverheadRig);
    EXPECT_EQ(Rig(CameraMode::Tether), kTetherPlaceholderRig);
    EXPECT_EQ(Rig(CameraMode::Chase), kChaseRig);
}

TEST_F(CameraModeWiringTest, PersistedUserCamerasAreRangeCheckedOnTheWayIn) {
    renderer.PlayerCamera().setUserCameraRigs(CameraRig{400.0f, 100.0f, 55.0f, 2.0f},
                                             CameraRig{10.0f, 0.0f, 9999.0f, 0.0f});
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).azimuthDeg, 40.0f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).pitchDeg, kCameraRigMaxPitchDeg);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).distance, kCameraRigMaxDistance);
}

// --- T471 (board B581): Chase places its camera on entry ---
//
// MEASURED by T466 in a live fable session
// (review/T466/fable/sessions/baseline_h160/camera_replies.txt): after
// `/camera overhead` then `/camera chase`, `/camera get` printed the Overhead
// node pose byte for byte, (438.471,-99.279,78.175). setCameraMode() placed
// first person (T74) and the rig modes (T461) on entry, but Chase moved only
// when setFollowPosition() next ran -- a player move -- so a stationary player
// switching to Chase kept looking through the previous camera.
TEST(CameraChaseEntryTest, EnteringChaseFromAnyOtherCameraPlacesTheChaseCameraAtOnce) {
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.LoggingLevel = irr::ELL_NONE;
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) GTEST_SKIP() << "no EDT_NULL device";
    irr::scene::ICameraSceneNode* cam = device->getSceneManager()->addCameraSceneNode();

    {
        IrrlichtRenderer renderer;
        renderer.Context().camera = cam;
        renderer.Context().cameraController = std::make_unique<CameraController>(cam);

        // T466's baseline pose: qeynos2 (428, -74, 3.119) in the client frame, heading 480.
        const float x = 428.0f, y = -74.0f, z = 3.119f, heading = 480.0f;
        renderer.PlayerCamera().setCameraMode(CameraMode::Overhead);
        renderer.setPlayerPosition(x, y, z, heading);

        // T474 (board B650): the Chase camera is T466's fit as a rig, around
        // the feet (no entity renderer here, so the feet are the node origin and
        // the eye scale is the fitted character's). Before T474 this was
        // setFollowPosition(): 20 behind, 10 above the node origin.
        const CameraRigPose fit = computeCameraRigPose(kChaseRig, x, y, z, heading);
        const irr::core::vector3df chase(fit.camX, fit.camZ, fit.camY);

        for (CameraMode from : {CameraMode::Overhead, CameraMode::FirstPerson, CameraMode::User1,
                                CameraMode::User2, CameraMode::Tether}) {
            renderer.PlayerCamera().setCameraMode(from);
            const irr::core::vector3df before = cam->getPosition();
            ASSERT_GT(before.getDistanceFrom(chase), 1.0f)
                << cameraModeName(from) << " already sits at the Chase pose; the case proves nothing";
            renderer.PlayerCamera().setCameraMode(CameraMode::Chase);
            const irr::core::vector3df after = cam->getPosition();
            EXPECT_NEAR(after.X, chase.X, 1e-3f) << "B581: Chase kept the " << cameraModeName(from) << " pose";
            EXPECT_NEAR(after.Y, chase.Y, 1e-3f) << "B581: Chase kept the " << cameraModeName(from) << " pose";
            EXPECT_NEAR(after.Z, chase.Z, 1e-3f) << "B581: Chase kept the " << cameraModeName(from) << " pose";
        }

        renderer.Context().cameraController.reset();
        renderer.Context().camera = nullptr;
    }
    device->drop();
}

// --- the other end of each arm ---

TEST(CameraModeWiringSource, TheReceiverQueuesTheCameraActionsForTheFrame) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/renderer/renderer_event_receiver.cpp");
    for (const char* line :
         {"case HA::ToggleCameraMode: actionQueue_.push_back({RA::ToggleCameraMode}); break;",
          "case HA::CameraOrbitLeft: actionQueue_.push_back({RA::CameraOrbitLeft}); break;",
          "case HA::CameraOrbitRight: actionQueue_.push_back({RA::CameraOrbitRight}); break;",
          "case HA::CameraPitchUp: actionQueue_.push_back({RA::CameraPitchUp}); break;",
          "case HA::CameraPitchDown: actionQueue_.push_back({RA::CameraPitchDown}); break;",
          "case HA::CameraRaise: actionQueue_.push_back({RA::CameraRaise}); break;",
          "case HA::CameraLower: actionQueue_.push_back({RA::CameraLower}); break;"}) {
        EXPECT_NE(src.find(line), std::string::npos) << "receiver no longer queues: " << line;
    }
}

// T486 (2026-09-14 audit, item 5): the zoom notch has ONE owner,
// kCameraZoomStepUnits (camera_rig.h). The receiver's key arms hard-coded
// +/-2.0 beside it, so a change to the constant would have moved the wheel
// and not the keys.
TEST(CameraModeWiringSource, TheZoomKeysUseTheOneZoomStepConstant) {
    const std::string src =
        readFile(kRoot / "src/client/graphics/renderer/renderer_event_receiver.cpp");
    EXPECT_NE(src.find("case HA::CameraZoomIn: cameraZoomDelta_ = -kCameraZoomStepUnits; break;"),
              std::string::npos) << "CameraZoomIn does not use kCameraZoomStepUnits";
    EXPECT_NE(src.find("case HA::CameraZoomOut: cameraZoomDelta_ = kCameraZoomStepUnits; break;"),
              std::string::npos) << "CameraZoomOut does not use kCameraZoomStepUnits";
    EXPECT_EQ(src.find("cameraZoomDelta_ = -2.0f"), std::string::npos) << "a hard-coded zoom notch is back";
    EXPECT_EQ(src.find("cameraZoomDelta_ = 2.0f"), std::string::npos) << "a hard-coded zoom notch is back";
}

// T486 (2026-09-14 audit, item 6): the rig camera is placed ONCE per frame,
// after the lift carry -- the last thing that moves the player in a frame.
// Before this unit FrameComponent called updateRigCamera() twice per frame
// (right after updatePlayerMovement() and again after carryPlayerOnLifts()):
// two BSP lookups and a PVS ray per frame on the Pi, and the ceiling log's
// counter counted every frame twice. The second call was also inside
// `if (ctx_.doorManager)`, so without doors the camera was placed BEFORE the
// lift carry, not after it.
TEST(CameraModeWiringSource, TheRigCameraIsPlacedOncePerFrameAfterTheLiftCarry) {
    const std::string frame =
        readFile(kRoot / "src/client/graphics/frame/renderer_frame_input.cpp");
    const std::string call = "ctx_.PlayerCamera().updateRigCamera();";
    size_t count = 0;
    for (size_t pos = frame.find(call); pos != std::string::npos; pos = frame.find(call, pos + 1)) {
        ++count;
    }
    EXPECT_EQ(count, 1u) << "updateRigCamera() is called " << count
                         << " times per frame in renderer_frame_input.cpp; it must be exactly one";
    const size_t carry = frame.find("ctx_.PlayerCamera().carryPlayerOnLifts(deltaTime);");
    const size_t place = frame.find(call);
    ASSERT_NE(carry, std::string::npos);
    ASSERT_NE(place, std::string::npos);
    EXPECT_LT(carry, place) << "the rig camera is placed before the lift carry moves the player";
    // And not gated on the door manager: the closing brace of the door block
    // comes before the placement.
    const size_t doorBlock = frame.find("if (ctx_.doorManager) {");
    ASSERT_NE(doorBlock, std::string::npos);
    const size_t doorClose = frame.find("\n    }\n", carry);
    ASSERT_NE(doorClose, std::string::npos);
    EXPECT_LT(doorClose, place) << "updateRigCamera() is inside `if (ctx_.doorManager)`";
}

TEST(CameraModeWiringSource, TheFrameZoomsWithAWheelNotchTheUIDidNotConsume) {
    const std::string frame =
        readFile(kRoot / "src/client/graphics/frame/renderer_frame_input.cpp");
    // B433: the Options window's Mouse Wheel Zoom checkbox gates the zoom half.
    EXPECT_NE(frame.find("if (wheel != 0.0f && !wheelConsumedByUI && !chatInputFocused_ &&\n"
                         "            ctx_.PlayerCamera().mouseOptions().wheelZoom) {"),
              std::string::npos);
    EXPECT_NE(frame.find("ctx_.PlayerCamera().zoomCamera(-wheel * kCameraZoomStepUnits, deltaTime);"),
              std::string::npos);
    EXPECT_NE(frame.find("ctx_.PlayerCamera().zoomCamera(zoomDelta, deltaTime);"),
              std::string::npos) << "the +/- keys no longer reach zoomCamera()";
}

}  // namespace
