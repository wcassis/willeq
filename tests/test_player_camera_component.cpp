// L05 (Batch L, refactor/L05_player_camera_component.md): PlayerCameraComponent,
// extracted from IrrlichtRenderer. Same bare, never-Init()'d IrrlichtRenderer
// idiom as test_diagnostics_component / test_irrlicht_camera_command: the
// constructor is empty apart from the component wiring, and shutdown() (called
// from the destructor) null-guards every member it touches, so no Irrlicht
// device and no GL context are needed.
//
// L01 correction 6: the context binding is pinned by MUTATION through public
// accessors, never by address equality on private members. Every assertion here
// is a round trip -- write on one side of a binding, read it back on the other.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/renderer_types.h"

using namespace EQT::Graphics;

namespace {

class PlayerCameraComponentTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
};

// --- The component exists and is reachable -------------------------------

TEST_F(PlayerCameraComponentTest, AccessorReachesAConstructedComponent) {
    // If the ctor body in renderer_init.cpp ever stops constructing it, every
    // forwarder below is a null dereference in production, not a test failure.
    PlayerCameraComponent& pc = renderer.PlayerCamera();
    EXPECT_EQ(&pc, &renderer.PlayerCamera());
}

// --- The owner_ transition-handle path -----------------------------------
//
// L13 moved cameraMode_ onto this component (it is the only writer). It had
// stayed on IrrlichtRenderer (it is read by renderer_entities.cpp
// and renderer_frame_input.cpp too), so setCameraMode() is the component
// WRITING through owner_ and getCameraMode() is the renderer reading its own
// member. One round trip across the handle.

TEST_F(PlayerCameraComponentTest, SetCameraModeWritesTheRenderersOwnMemberThroughTheOwnerHandle) {
    // Declared default: First Person since T471 (the official login camera, T466).
    ASSERT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::FirstPerson);

    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Chase);
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Chase);
    EXPECT_FALSE(renderer.isFirstPersonMode());

    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::FirstPerson);
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::FirstPerson);
    EXPECT_TRUE(renderer.isFirstPersonMode());

    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Overhead);
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Overhead);
    EXPECT_FALSE(renderer.isFirstPersonMode());
}

TEST_F(PlayerCameraComponentTest, CycleCameraModeWalksTheOfficialOrderThroughTheOwnerHandle) {
    // cycleCameraMode() has NO forwarder on IrrlichtRenderer -- L05 measured
    // zero callers on that receiver. Its routes are FrameComponent's F9 arm
    // (T461) and `/camera cycle`, both through PlayerCamera().
    ASSERT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::FirstPerson);
    renderer.PlayerCamera().cycleCameraMode();
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::Overhead);
    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Chase);
    renderer.PlayerCamera().cycleCameraMode();
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::User1);
    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Tether);
    renderer.PlayerCamera().cycleCameraMode();
    EXPECT_EQ(renderer.getCameraMode(), IrrlichtRenderer::CameraMode::FirstPerson);
}

TEST_F(PlayerCameraComponentTest, CameraModeStringReadsTheOwnersMemberBackThroughTheForwarder) {
    // Renderer forwarder -> component -> its own cameraMode_ -> string (L13).
    EXPECT_EQ(renderer.getCameraModeString(), "First Person Camera");  // T471 default
    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Chase);
    EXPECT_EQ(renderer.getCameraModeString(), "Chase Camera");
    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Overhead);
    EXPECT_EQ(renderer.getCameraModeString(), "Overhead Camera");
}

// --- The ctx_ (RendererContext) path -------------------------------------
//
// playerX_/playerY_/playerZ_ are core members bound into RendererContext.
// setPlayerPosition() is the component writing them as ctx_.playerX etc.;
// getPlayerZ() is IrrlichtRenderer reading its own member back. Context write
// -> renderer read, which is the binding L01 correction 6 asks for.

TEST_F(PlayerCameraComponentTest, SetPlayerPositionWritesTheCoreMembersThroughTheContext) {
    // Overhead on purpose (T461: this used Free, which is gone). Until T474 the
    // Chase branch's next act was ctx_.cameraController->enableZoneInLogging(),
    // an UNGUARDED dereference of a unique_ptr that only Init() fills; T474
    // (B650) deleted that branch, and every rig mode -- Chase included -- takes
    // updateRigCamera(), which returns without a camera controller, so the
    // member writes above it are what this test observes. (K09 correction z
    // read forward: an unguarded dereference in an otherwise-guarded body tells
    // you which harness paths are reachable.)
    renderer.PlayerCamera().setCameraMode(IrrlichtRenderer::CameraMode::Overhead);
    renderer.setPlayerPosition(11.0f, 22.0f, 33.0f, 64.0f);
    EXPECT_FLOAT_EQ(renderer.getPlayerZ(), 33.0f);
    EXPECT_FLOAT_EQ(renderer.Context().playerX, 11.0f);
    EXPECT_FLOAT_EQ(renderer.Context().playerY, 22.0f);
    EXPECT_FLOAT_EQ(renderer.Context().playerZ, 33.0f);
}

TEST_F(PlayerCameraComponentTest, ContextWriteIsSeenByTheComponentsReader) {
    // The other direction: mutate through Context(), read back through the
    // renderer. If ctx_ ever stopped being a reference bundle this fails.
    renderer.Context().playerZ = 77.5f;
    EXPECT_FLOAT_EQ(renderer.getPlayerZ(), 77.5f);
}

// --- Members that MOVED into the component -------------------------------

TEST_F(PlayerCameraComponentTest, SwimmingStateRoundTripsOnTheComponentsOwnMember) {
    // playerMovement_ moved; setSwimmingState() is a renderer forwarder
    // (irrlicht_bridge.cpp:629) and isSwimming() is reachable only through the
    // component, because it had zero callers and got no forwarder.
    EXPECT_FALSE(renderer.PlayerCamera().isSwimming());
    renderer.setSwimmingState(true, 20.0f);
    EXPECT_TRUE(renderer.PlayerCamera().isSwimming());
    renderer.setSwimmingState(false, 20.0f);
    EXPECT_FALSE(renderer.PlayerCamera().isSwimming());
}

TEST_F(PlayerCameraComponentTest, VisionAppliesTheBuffValueOverTheRacialBase) {
    // baseVision_ moved into the component; currentVision_ stayed on the
    // renderer (renderer_sim_bridge.cpp reads it). setVisionType() is the
    // bridge forwarder for the VisionChanged arm.
    //
    // T334 (B046): the value it carries is the buff set RECOMPUTED by
    // EverQuest's fade callback, so it is applied absolutely -- over the
    // racial base, not over whatever vision happens to be current. The old
    // "only upgrade, never downgrade" rule made the fade publish a no-op;
    // see tests/test_vision_buff_fade_wire.cpp for the wire itself.
    // resetVisionToBase() is no longer a zero-caller knob: setVisionType()
    // calls it, and it is still reachable here on its own.
    EXPECT_EQ(renderer.getVisionType(), VisionType::Normal);
    EXPECT_EQ(renderer.PlayerCamera().getBaseVision(), VisionType::Normal);

    renderer.setVisionType(VisionType::Ultravision);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Ultravision);

    // A lesser buff value now takes effect -- it is the whole recomputed
    // answer, not a second buff landing on top of the first.
    renderer.setVisionType(VisionType::Infravision);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);

    // ...but never below the race's own base.
    renderer.PlayerCamera().setPlayerRace(8);  // Dwarf -- base Infravision
    renderer.setVisionType(VisionType::Normal);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);

    renderer.PlayerCamera().resetVisionToBase();
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);
}

// --- The forwarders that stay, each reaching a live component -------------

TEST_F(PlayerCameraComponentTest, BridgeAndApplicationForwardersReachTheComponent) {
    // setPlayerSpawnId writes this component's playerSpawnId_ (L13; shared with the entities
    // family, so it stayed on the renderer). No public reader exists on
    // IrrlichtRenderer, so this pins reachability rather than a value: it must
    // not crash and must leave the camera state it also touches consistent.
    renderer.setPlayerSpawnId(4321);
    renderer.setPlayerSpawnId(0);

    // setCameraMode + getCameraModeString are covered above; this asserts the
    // remaining two forwarders dispatch into a non-null component.
    renderer.setSwimmingState(true, 15.0f);
    EXPECT_TRUE(renderer.PlayerCamera().isSwimming());
    renderer.setVisionType(VisionType::Infravision);
    EXPECT_EQ(renderer.getVisionType(), VisionType::Infravision);
}

}  // namespace
