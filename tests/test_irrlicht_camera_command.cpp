// T66 -- coverage for /camera, instance 7 of the "reachable only through a
// UI gesture, invisible to automation" pattern: before T66 the only producer
// of a camera mode change was a raw key event, so a scripted or piped session
// could never enter first person.
//
// T461 rewrote the camera set to the official client's six cameras
// (client/graphics/player/camera_mode.h), retired Free/Follow (no alias), and
// added exact placement -- `/camera set|nudge|get` -- for the oracle rig. The
// grammar's every bad input is pinned device-free in tests/test_camera_modes.cpp;
// this file drives the REAL processSlashCommand() dispatch end to end.
//
// Genuinely headless: a default-constructed, never-Init()'d IrrlichtRenderer.
// setCameraMode()/cycleCameraMode()/runCameraCommand() touch only the
// component's own state, the (null-guarded) entity renderer and the
// (null-guarded) camera controller -- no Init(), no GL context.
//
// Chat replies are captured via the "minimal fake bridge" idiom
// test_static_ui_panels.cpp (T54) uses.

#include <gtest/gtest.h>

#include <cctype>
#include <cmath>
#include <sstream>
#include <string>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/camera_rig.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"

using namespace EQT::Graphics;

namespace {

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

bool Contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

class CameraCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        renderer.setBridge(&bridge);
    }

    std::vector<std::string> Run(const std::string& command) {
        DrainSystemMessages(bridge);
        renderer.processSlashCommand(command);
        return DrainSystemMessages(bridge);
    }

    const CameraRig& Rig(CameraMode mode) {
        return *renderer.PlayerCamera().cameraRigFor(mode);
    }

    IrrlichtRenderer renderer;
    TestBridge bridge;
};

// --- Default state ---

TEST_F(CameraCommandTest, DefaultModeIsChase) {
    // player_camera_component.h: cameraMode_ = kDefaultCameraMode. Chase is
    // the camera fable called Follow; asserted first so the round trips below
    // are not the first thing to notice a changed default.
    EXPECT_EQ(renderer.getCameraMode(), CameraMode::Chase);
    EXPECT_EQ(renderer.getCameraModeString(), "Chase Camera");
}

TEST_F(CameraCommandTest, NoArgReportsCurrentModeViaChat) {
    const auto msgs = Run("/camera");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Camera mode: Chase Camera");
}

// --- Set-then-read round trip, every mode ---

struct CameraModeCase {
    std::string setArg;
    CameraMode expectedMode;
    std::string expectedString;
};

// Fixes the value gtest prints beside the test name (see T356).
inline void PrintTo(const CameraModeCase& c, std::ostream* os) {
    *os << "{set=\"" << c.setArg << "\", expects=\"" << c.expectedString << "\"}";
}

class CameraCommandRoundTripTest : public CameraCommandTest,
                                   public ::testing::WithParamInterface<CameraModeCase> {};

// T356 (B010): stable, self-describing case names derived from setArg.
struct CameraModeCaseName {
    std::string operator()(const ::testing::TestParamInfo<CameraModeCase>& info) const {
        std::string name;
        for (char c : info.param.setArg) {
            name += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        }
        return name.empty() ? std::string("empty") : name;
    }
};

TEST_P(CameraCommandRoundTripTest, SetThenReadRoundTrips) {
    const auto& tc = GetParam();
    {
        const auto msgs = Run("/camera " + tc.setArg);
        ASSERT_EQ(msgs.size(), 1u);
        EXPECT_EQ(msgs[0], "Camera mode: " + tc.expectedString);
    }
    EXPECT_EQ(renderer.getCameraMode(), tc.expectedMode);
    EXPECT_EQ(renderer.getCameraModeString(), tc.expectedString);

    // Read back with a separate, later no-arg call.
    const auto msgs2 = Run("/camera");
    ASSERT_EQ(msgs2.size(), 1u);
    EXPECT_EQ(msgs2[0], "Camera mode: " + tc.expectedString);
}

// The official names, verbatim from eqstr_us.txt 9500..9505.
INSTANTIATE_TEST_SUITE_P(
    OfficialNames, CameraCommandRoundTripTest,
    ::testing::Values(
        CameraModeCase{"First Person Camera", CameraMode::FirstPerson, "First Person Camera"},
        CameraModeCase{"Overhead Camera", CameraMode::Overhead, "Overhead Camera"},
        CameraModeCase{"Chase Camera", CameraMode::Chase, "Chase Camera"},
        CameraModeCase{"User Defined Camera 1", CameraMode::User1, "User Defined Camera 1"},
        CameraModeCase{"User Defined Camera 2", CameraMode::User2, "User Defined Camera 2"},
        CameraModeCase{"Tether Camera", CameraMode::Tether, "Tether Camera"}),
    CameraModeCaseName());

// The short forms a script would rather type.
INSTANTIATE_TEST_SUITE_P(
    ShortForms, CameraCommandRoundTripTest,
    ::testing::Values(
        CameraModeCase{"fp", CameraMode::FirstPerson, "First Person Camera"},
        CameraModeCase{"first person", CameraMode::FirstPerson, "First Person Camera"},
        CameraModeCase{"OVERHEAD", CameraMode::Overhead, "Overhead Camera"},
        CameraModeCase{"chase", CameraMode::Chase, "Chase Camera"},
        CameraModeCase{"user1", CameraMode::User1, "User Defined Camera 1"},
        CameraModeCase{"user_2", CameraMode::User2, "User Defined Camera 2"},
        CameraModeCase{"tether", CameraMode::Tether, "Tether Camera"}),
    CameraModeCaseName());

// --- Cycling: the F9 order ---

TEST_F(CameraCommandTest, CycleWalksTheOfficialOrderAndWraps) {
    ASSERT_EQ(renderer.getCameraMode(), CameraMode::Chase);
    const CameraMode expected[] = {CameraMode::User1, CameraMode::User2, CameraMode::Tether,
                                   CameraMode::FirstPerson, CameraMode::Overhead,
                                   CameraMode::Chase};
    for (CameraMode m : expected) {
        Run("/camera cycle");
        EXPECT_EQ(renderer.getCameraMode(), m);
    }
}

// --- The retired names: no alias ---

TEST_F(CameraCommandTest, FreeAndFollowAreGoneWithNoAlias) {
    for (const char* retired : {"free", "Follow"}) {
        const auto msgs = Run(std::string("/camera ") + retired);
        ASSERT_EQ(msgs.size(), 1u);
        EXPECT_TRUE(Contains(msgs[0], "Unknown camera mode")) << msgs[0];
        EXPECT_EQ(renderer.getCameraMode(), CameraMode::Chase);
    }
}

TEST_F(CameraCommandTest, UnknownModeName_PrintsErrorMessage_DoesNotChangeMode) {
    const auto msgs = Run("/camera sidescroll");
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_TRUE(Contains(msgs[0], "Unknown camera mode"));
    EXPECT_TRUE(Contains(msgs[0], "sidescroll"));
    EXPECT_EQ(renderer.getCameraMode(), CameraMode::Chase);
}

// --- /camera set / nudge / get ---

TEST_F(CameraCommandTest, SetFromChaseSwitchesToUserCamera1AndSaysSo) {
    const auto msgs = Run("/camera set az 90 pitch 20 dist 40 height 6");
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_TRUE(Contains(msgs[0], "Switched to User Defined Camera 1")) << msgs[0];
    EXPECT_EQ(msgs[1],
              "User Defined Camera 1: az=90.000 pitch=20.000 dist=40.000 height=6.000");
    EXPECT_EQ(renderer.getCameraMode(), CameraMode::User1);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).azimuthDeg, 90.0f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).pitchDeg, 20.0f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).distance, 40.0f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).height, 6.0f);
}

TEST_F(CameraCommandTest, SetInUserCamera2EditsUserCamera2Only) {
    Run("/camera user2");
    const auto msgs = Run("/camera set dist 12");
    ASSERT_EQ(msgs.size(), 1u) << "no switch message when already in a user camera";
    EXPECT_EQ(renderer.getCameraMode(), CameraMode::User2);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).distance, 12.0f);
    // A subset leaves the rest alone; User 1 is untouched.
    EXPECT_FLOAT_EQ(Rig(CameraMode::User2).height, kUser2DefaultRig.height);
    EXPECT_EQ(Rig(CameraMode::User1), kUser1DefaultRig);
}

TEST_F(CameraCommandTest, NudgeAddsExactSteps) {
    Run("/camera set az 350 pitch 10 dist 30 height 5");
    Run("/camera nudge az 20 pitch -5 dist 2.5 height 1");
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).azimuthDeg, 10.0f);  // wraps past 360
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).pitchDeg, 5.0f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).distance, 32.5f);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).height, 6.0f);
}

TEST_F(CameraCommandTest, OutOfRangeValuesAreClampedAndTheReplySaysSo) {
    const auto msgs = Run("/camera set pitch 120 dist 0");
    ASSERT_FALSE(msgs.empty());
    EXPECT_TRUE(Contains(msgs.back(), "(clamped to range)")) << msgs.back();
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).pitchDeg, kCameraRigMaxPitchDeg);
    EXPECT_FLOAT_EQ(Rig(CameraMode::User1).distance, kCameraRigMinDistance);
}

TEST_F(CameraCommandTest, BadInputChangesNothing) {
    Run("/camera user1");
    const CameraRig before = Rig(CameraMode::User1);
    for (const char* bad : {"/camera set", "/camera set az", "/camera set az abc",
                            "/camera set yaw 5", "/camera set az 1 az 2",
                            "/camera nudge dist nan", "/camera get now"}) {
        const auto msgs = Run(bad);
        ASSERT_EQ(msgs.size(), 1u) << bad;
        EXPECT_TRUE(Contains(msgs[0], "/camera help")) << bad << " -> " << msgs[0];
        EXPECT_EQ(Rig(CameraMode::User1), before) << bad;
        EXPECT_EQ(renderer.getCameraMode(), CameraMode::User1) << bad;
    }
}

TEST_F(CameraCommandTest, GetPrintsTheModeTheFourValuesAndThePose) {
    // The user camera FIRST: in the default Chase mode setPlayerPosition()
    // dereferences the camera controller, which a device-less renderer lacks.
    Run("/camera set az 0 pitch 0 dist 10 height 5");
    renderer.setPlayerPosition(100.0f, 200.0f, 10.0f, 0.0f);
    const auto lines = Run("/camera get");
    ASSERT_GE(lines.size(), 5u);
    EXPECT_EQ(lines[0], "[CAMERA-GET] mode=\"User Defined Camera 1\" view=4");
    EXPECT_EQ(lines[1], "[CAMERA-GET] az=0.000 pitch=0.000 dist=10.000 height=5.000 "
                        "units=deg,deg,eq_units,eq_units");
    // No entity renderer: collisionZOffset reads 0, so the feet are the node origin.
    EXPECT_EQ(lines[2], "[CAMERA-GET] player=(100.000,200.000,10.000) feetZ=10.000 "
                        "heading=0.000 frame=eq_client");
    // Heading 0 faces +Y; the camera sits 10 behind it, level with the target.
    EXPECT_EQ(lines[3], "[CAMERA-GET] ideal camPos=(100.000,190.000,15.000) "
                        "target=(100.000,200.000,15.000) frame=eq_client");
    EXPECT_EQ(lines[4], "[CAMERA-GET] node=absent (no camera scene node)");
}

TEST_F(CameraCommandTest, GetInANonOrbitModeSaysSo) {
    const auto lines = Run("/camera get");
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[0], "[CAMERA-GET] mode=\"Chase Camera\" view=3");
    EXPECT_TRUE(Contains(lines[1], "az=n/a")) << lines[1];
}

TEST_F(CameraCommandTest, HelpStatesTheUnits) {
    const auto lines = Run("/camera help");
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    EXPECT_TRUE(Contains(all, "degrees")) << all;
    EXPECT_TRUE(Contains(all, "EQ world units")) << all;
    EXPECT_TRUE(Contains(all, "F9")) << all;
}

// --- The rig is not overwritten by moving the player ---

TEST_F(CameraCommandTest, APlayerMoveDoesNotOverwriteTheOrbit) {
    Run("/camera set az 90 pitch 20 dist 40 height 6");
    const CameraRig set = Rig(CameraMode::User1);

    // A server relocation (the setPlayerPosition path) and a local move (the
    // context the movement integrator writes) both move the PLAYER only.
    renderer.setPlayerPosition(-50.0f, 75.0f, 3.0f, 128.0f);
    EXPECT_EQ(Rig(CameraMode::User1), set);
    renderer.Context().playerX = 12.0f;
    renderer.Context().playerHeading = 300.0f;
    renderer.PlayerCamera().updateRigCamera();
    EXPECT_EQ(Rig(CameraMode::User1), set);

    // And the pose follows the player, expressed around its new heading.
    const CameraRigPose expected = computeCameraRigPose(set, 12.0f, 75.0f, 3.0f, 300.0f);
    const auto lines = Run("/camera get");
    ASSERT_GE(lines.size(), 4u);
    std::ostringstream want;
    want.setf(std::ios::fixed);
    want.precision(3);
    want << "[CAMERA-GET] ideal camPos=(" << expected.camX << "," << expected.camY << ","
         << expected.camZ << ") target=(" << expected.targetX << "," << expected.targetY
         << "," << expected.targetZ << ") frame=eq_client";
    EXPECT_EQ(lines[3], want.str());
}

}  // namespace
