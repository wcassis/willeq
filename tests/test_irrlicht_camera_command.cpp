// T66 -- coverage for /camera (irrlicht_renderer.cpp's processSlashCommand),
// instance 7 of refactor/deferred.md's "PATTERN: real features reachable
// only through a UI gesture, invisible to automation". Prior to this unit,
// IrrlichtRenderer::cycleCameraMode()/setCameraMode()/getCameraModeString()
// were fully implemented but the ONLY producer was HA::ToggleCameraMode, a
// raw Ctrl+F5 key event -- a scripted or piped session could never enter
// first person, which is why T63 could not produce any first-person
// evidence at all.
//
// This is a genuinely headless, end-to-end test: it exercises the REAL
// processSlashCommand() dispatch (the same function the render-thread side
// of the RendererCommand bridge queue calls -- see irrlicht_bridge.cpp) down
// into the real setCameraMode()/cycleCameraMode()/getCameraModeString(), no
// mocks or seams in that chain. It needs no Irrlicht device at all: reading
// setCameraMode()/cycleCameraMode()/getCameraModeString() (irrlicht_renderer.cpp)
// shows they only touch cameraMode_ and playerConfig_.eyeHeight (L13 moved both
// to PlayerCameraComponent), the (null-guarded) entityRenderer_, -- none of which require Init()/a GL context. A
// default-constructed, never-Init()'d IrrlichtRenderer is safe to construct
// and destroy (constructor is empty; shutdown(), called from the destructor,
// null-guards every member it touches).
//
// Chat replies are captured via the same "minimal fake bridge" idiom
// test_static_ui_panels.cpp (T54) uses: a concrete GameStateBridge subclass
// with pushEvent()/drainEvents() both non-virtual on the base class.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
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

class CameraCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        renderer.setBridge(&bridge);
    }

    IrrlichtRenderer renderer;
    TestBridge bridge;
};

// --- Default state ---

TEST_F(CameraCommandTest, DefaultModeIsFollow) {
    // player_camera_component.h (L13; it was irrlicht_renderer.h):
    // cameraMode_ = CameraMode::Follow is the declared
    // default -- asserted here so the round-trip tests below aren't the
    // first thing to notice if that default ever changes.
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Follow);
    EXPECT_EQ(renderer.getCameraModeString(), "Follow");
}

// --- No-arg report ---

TEST_F(CameraCommandTest, NoArgReportsCurrentModeViaChat) {
    renderer.processSlashCommand("/camera");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Camera mode: Follow");
}

// --- Set-then-read round trip, every mode, via the exact strings
// getCameraModeString() produces (the plan's explicit bar: "/camera <mode>
// should set a named mode explicitly... accept the mode names
// getCameraModeString() already produces, so set-then-read round-trips"). ---

struct CameraModeCase {
    std::string setArg;
    IrrlichtRenderer::CameraMode expectedMode;
    std::string expectedString;
};

class CameraCommandRoundTripTest : public CameraCommandTest,
                                    public ::testing::WithParamInterface<CameraModeCase> {};

TEST_P(CameraCommandRoundTripTest, SetThenReadRoundTrips) {
    const auto& tc = GetParam();

    // Set.
    renderer.processSlashCommand("/camera " + tc.setArg);
    {
        auto msgs = DrainSystemMessages(bridge);
        ASSERT_EQ(msgs.size(), 1u);
        EXPECT_EQ(msgs[0], "Camera mode: " + tc.expectedString);
    }
    EXPECT_EQ(renderer.getCameraMode(), tc.expectedMode);
    EXPECT_EQ(renderer.getCameraModeString(), tc.expectedString);

    // Read back with a separate, later no-arg call -- proves the mode
    // genuinely persisted in renderer state, not just in the immediately
    // preceding chat reply.
    renderer.processSlashCommand("/camera");
    auto msgs2 = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs2.size(), 1u);
    EXPECT_EQ(msgs2[0], "Camera mode: " + tc.expectedString);
}

INSTANTIATE_TEST_SUITE_P(
    ExactGetterStrings, CameraCommandRoundTripTest,
    ::testing::Values(
        CameraModeCase{"Free", IrrlichtRenderer::CameraMode::Free, "Free"},
        CameraModeCase{"Follow", IrrlichtRenderer::CameraMode::Follow, "Follow"},
        CameraModeCase{"First Person", IrrlichtRenderer::CameraMode::FirstPerson, "First Person"}));

// Compact/case-insensitive spellings a script might prefer over quoting a
// two-word argument -- still round-trips against the canonical getter string.
INSTANTIATE_TEST_SUITE_P(
    CompactAliases, CameraCommandRoundTripTest,
    ::testing::Values(
        CameraModeCase{"free", IrrlichtRenderer::CameraMode::Free, "Free"},
        CameraModeCase{"FOLLOW", IrrlichtRenderer::CameraMode::Follow, "Follow"},
        CameraModeCase{"firstperson", IrrlichtRenderer::CameraMode::FirstPerson, "First Person"},
        CameraModeCase{"fp", IrrlichtRenderer::CameraMode::FirstPerson, "First Person"},
        CameraModeCase{"first-person", IrrlichtRenderer::CameraMode::FirstPerson, "First Person"}));

// --- Cycling (additionally exposed since it was free, per the plan) ---

TEST_F(CameraCommandTest, CycleAdvancesFreeFollowFirstPersonFree) {
    ASSERT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Follow);  // default

    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::FirstPerson);
    DrainSystemMessages(bridge);

    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Free);
    DrainSystemMessages(bridge);

    renderer.processSlashCommand("/camera cycle");
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Follow);
}

// --- Unrecognized mode name ---

TEST_F(CameraCommandTest, UnknownModeName_PrintsErrorMessage_DoesNotChangeMode) {
    ASSERT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Follow);

    renderer.processSlashCommand("/camera sidescroll");

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_NE(msgs[0].find("Unknown camera mode"), std::string::npos);
    EXPECT_NE(msgs[0].find("sidescroll"), std::string::npos);
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Follow);  // unchanged
}

}  // namespace
