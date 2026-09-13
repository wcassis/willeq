/**
 * T461 -- the camera keys and the rig, wired end to end on a device-less
 * renderer.
 *
 * MEASURED before the fix: RendererEventReceiver::OnEvent queued
 * RendererAction::ToggleCameraMode for the camera key (then Ctrl+F5) and
 * FrameComponent::processCommonInput() had NO arm for it -- the key fell
 * through `default: break` and did nothing, the T330/T343 unwired-handler
 * shape again. The board's triage ("bound Ctrl+F5") described a key that had
 * never reached cycleCameraMode(); only `/camera cycle` ever did.
 *
 * What this file pins:
 *   - Behavioural, through the exact production consumer
 *     (FrameComponent::processCommonInput, the idiom of
 *     tests/test_autorun_arm.cpp): the F9 action walks the official cycle,
 *     and the four orbit actions move the ACTIVE user camera and nothing else.
 *   - Behavioural: zoom edits the active rig's distance; a player move leaves
 *     every rig alone.
 *   - Source-pinned: the receiver queues ToggleCameraMode and the four orbit
 *     actions on actionQueue_, so the arms cannot be orphaned from the other
 *     end; and the frame routes a UI-unconsumed wheel notch into zoomCamera().
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
};

TEST_F(CameraModeWiringTest, TheF9ActionWalksTheOfficialCycle) {
    ASSERT_EQ(renderer.getCameraMode(), CameraMode::Chase);
    const CameraMode order[] = {CameraMode::User1, CameraMode::User2, CameraMode::Tether,
                                CameraMode::FirstPerson, CameraMode::Overhead,
                                CameraMode::Chase};
    for (CameraMode expected : order) {
        renderer.Frame().processCommonInput(One(RendererAction::ToggleCameraMode));
        EXPECT_EQ(renderer.getCameraMode(), expected)
            << "the camera key reaches nothing that cycles the camera";
    }
}

TEST_F(CameraModeWiringTest, TheOrbitActionsMoveTheActiveUserCamera) {
    renderer.PlayerCamera().setCameraMode(CameraMode::User2);
    renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitRight));
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).azimuthDeg, kCameraOrbitKeyStepDeg);
    renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitLeft));
    renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitLeft));
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).azimuthDeg, 360.0f - kCameraOrbitKeyStepDeg);
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchDown));
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).pitchDeg, kCameraOrbitKeyStepDeg);
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchUp));
    renderer.Frame().processCommonInput(One(RendererAction::CameraPitchUp));
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).pitchDeg, -kCameraOrbitKeyStepDeg);
    EXPECT_EQ(Rig(CameraMode::User1), kUser1DefaultRig) << "the inactive user camera moved";
}

TEST_F(CameraModeWiringTest, TheOrbitActionsDoNothingOutsideAUserCamera) {
    for (CameraMode m : {CameraMode::Chase, CameraMode::FirstPerson, CameraMode::Overhead,
                         CameraMode::Tether}) {
        renderer.PlayerCamera().setCameraMode(m);
        renderer.Frame().processCommonInput(One(RendererAction::CameraOrbitRight));
        renderer.Frame().processCommonInput(One(RendererAction::CameraPitchDown));
        EXPECT_EQ(renderer.getCameraMode(), m);
    }
    EXPECT_EQ(Rig(CameraMode::User1), kUser1DefaultRig);
    EXPECT_EQ(Rig(CameraMode::User2), kUser2DefaultRig);
    EXPECT_EQ(Rig(CameraMode::Overhead), kOverheadPlaceholderRig);
    EXPECT_EQ(Rig(CameraMode::Tether), kTetherPlaceholderRig);
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
    EXPECT_EQ(Rig(CameraMode::Overhead), kOverheadPlaceholderRig);
    EXPECT_EQ(Rig(CameraMode::Tether), kTetherPlaceholderRig);
}

TEST_F(CameraModeWiringTest, PersistedUserCamerasAreRangeCheckedOnTheWayIn) {
    renderer.PlayerCamera().setUserCameraRigs(CameraRig{400.0f, 100.0f, 55.0f, 2.0f},
                                             CameraRig{10.0f, 0.0f, 9999.0f, 0.0f});
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).azimuthDeg, 40.0f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).pitchDeg, kCameraRigMaxPitchDeg);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).distance, kCameraRigMaxDistance);
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
          "case HA::CameraPitchDown: actionQueue_.push_back({RA::CameraPitchDown}); break;"}) {
        EXPECT_NE(src.find(line), std::string::npos) << "receiver no longer queues: " << line;
    }
}

TEST(CameraModeWiringSource, TheFrameZoomsWithAWheelNotchTheUIDidNotConsume) {
    const std::string frame =
        readFile(kRoot / "src/client/graphics/frame/renderer_frame_input.cpp");
    EXPECT_NE(frame.find("if (wheel != 0.0f && !wheelConsumedByUI && !chatInputFocused_) {"),
              std::string::npos);
    EXPECT_NE(frame.find("ctx_.PlayerCamera().zoomCamera(-wheel * kCameraZoomStepUnits, deltaTime);"),
              std::string::npos);
    EXPECT_NE(frame.find("ctx_.PlayerCamera().zoomCamera(zoomDelta, deltaTime);"),
              std::string::npos) << "the +/- keys no longer reach zoomCamera()";
}

}  // namespace
