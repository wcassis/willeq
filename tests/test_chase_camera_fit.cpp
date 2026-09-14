/**
 * T474 (board B650) -- the Chase camera at the official client's geometry.
 *
 * MEASURED on the official Titanium client (T466, review/T466/README.md, the
 * Chase row; fable/sessions/fit2_* and fit4_*): fable's rig swept with
 * `/camera set` at qeynos2 (-74, 428, 3.751) heading 160, 1024x768, Human male
 * Warrior, against the official Chase frame at the same pose (T310's gradient
 * metric). Minimum edge 0.7379 at az 0, pitch 6, dist 4.0, height 7.0 above the
 * feet -- reproduced to four decimals in two sessions -- and bracketed:
 *   dist 3.5 -> 0.767, dist 4.5 -> 0.839   (pitch 6, height 7)
 *   height 6.5 -> 0.779, 7.5 -> 0.922      (pitch 6, dist 4)
 *   pitch 3 -> 0.937, pitch 9 -> 0.880     (dist 4, height 7)
 * The official Chase turns with the player (h160 vs h288 edge 0.680).
 *
 * BEFORE T474 fable's Chase was CameraController::setFollowPosition(): 20
 * behind, 10 above the node origin, looking 10 AHEAD of the player (T471
 * computed it: camera 16.02 farther back, 5.70 higher, 1.60 degrees more
 * pitch -- board B650). Every case below failed on that body.
 *
 * What this file pins, on a bare never-Init()'d IrrlichtRenderer with a real
 * EDT_NULL camera node and CameraController (no entity renderer, so the feet
 * are the node origin and the race scale is the fitted character's):
 *   - the Chase camera node sits at the fit, with the look-at ON the player;
 *   - it turns with the player (heading-relative);
 *   - zoom acts on the Chase distance and leaves the look-at alone;
 *   - a wall between the look-at point and the fitted camera pulls it in;
 *   - the T464 eye-region ceiling clamp still holds it under a low ceiling,
 *     and still ignores a box top below the eye or above the camera;
 *   - `/camera set` in Chase edits the Chase camera (Leg B's A/B lever), and
 *     `/camera get` reports its four values in the player-heading frame.
 */
#include <gtest/gtest.h>

#include <irrlicht.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/camera_controller.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/state/event_bus.h"
#include "client/zone_bsp.h"

using namespace EQT::Graphics;

namespace {

constexpr float kPi = 3.14159265358979f;
// T466's fit (see the file header).
constexpr float kFitPitchDeg = 6.0f;
constexpr float kFitDistance = 4.0f;
constexpr float kFitHeight = 7.0f;
// T431: the fitted character's (Human male) eye, solved on the official
// client, above its own feet. The ceiling clamp's ray starts at the eye.
constexpr float kFitEyeAboveFeet = 6.023f;
constexpr float kCollisionOffset = 0.5f;  // CameraController's CAMERA_COLLISION_OFFSET

struct Pose {
    irr::core::vector3df cam;
    irr::core::vector3df target;
};

// The fit around a player at EQ client (x, y) with feet at z, heading h
// (0..512), in Irrlicht Y-up space: EQ (x, y, z) -> (x, z, y).
Pose FitPose(float x, float y, float feetZ, float heading, float distance = kFitDistance) {
    const float h = heading / 512.0f * 2.0f * kPi;
    const float p = kFitPitchDeg * kPi / 180.0f;
    const float horizontal = distance * std::cos(p);
    Pose out;
    out.target = irr::core::vector3df(x, feetZ + kFitHeight, y);
    out.cam = irr::core::vector3df(x - std::sin(h) * horizontal,
                                   feetZ + kFitHeight + distance * std::sin(p),
                                   y - std::cos(h) * horizontal);
    return out;
}

void ExpectNear(const irr::core::vector3df& got, const irr::core::vector3df& want,
                const char* what) {
    EXPECT_NEAR(got.X, want.X, 1e-3f) << what;
    EXPECT_NEAR(got.Y, want.Y, 1e-3f) << what;
    EXPECT_NEAR(got.Z, want.Z, 1e-3f) << what;
}

std::shared_ptr<BspRegion> Region(std::vector<bool> pvs) {
    auto r = std::make_shared<BspRegion>();
    r->visibleRegions = std::move(pvs);
    return r;
}

BspTree OneRegion() {
    BspTree bsp;
    bsp.nodes.push_back(BspNode{});
    bsp.nodes[0].regionId = 1;
    bsp.regions.push_back(Region({true}));
    return bsp;
}

// A vertical wall at EQ y = wallY: region 1 on the +Y side, region 0 on the
// -Y side, and neither sees the other.
BspTree WallAtY(float wallY) {
    BspTree bsp;
    bsp.nodes.resize(3);
    bsp.nodes[0].normalY = 1.0f;
    bsp.nodes[0].splitDistance = -wallY;
    bsp.nodes[0].left = 1;   // y >= wallY
    bsp.nodes[0].right = 2;  // y <  wallY
    bsp.nodes[1].regionId = 2;
    bsp.nodes[2].regionId = 1;
    bsp.regions.push_back(Region({true, false}));
    bsp.regions.push_back(Region({false, true}));
    return bsp;
}

// An EQ Z-up region box with its vertical top at maxZ.
irr::core::aabbox3df EqBoxTop(float maxZ) {
    return irr::core::aabbox3df(irr::core::vector3df(-500.0f, -500.0f, -500.0f),
                                irr::core::vector3df(500.0f, 500.0f, maxZ));
}

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class ChaseCameraFitTest : public ::testing::Test {
protected:
    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.LoggingLevel = irr::ELL_NONE;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        cam_ = device_->getSceneManager()->addCameraSceneNode();
        renderer_ = std::make_unique<IrrlichtRenderer>();
        renderer_->setBridge(&bridge_);
        renderer_->Context().camera = cam_;
        renderer_->Context().cameraController = std::make_unique<CameraController>(cam_);
    }
    void TearDown() override {
        if (renderer_) {
            renderer_->Context().cameraController.reset();
            renderer_->Context().camera = nullptr;
            renderer_.reset();
        }
        if (device_) device_->drop();
    }

    CameraController& Controller() { return *renderer_->Context().cameraController; }

    Pose Node() const { return Pose{cam_->getPosition(), cam_->getTarget()}; }

    std::vector<std::string> Run(const std::string& command) {
        bridge_.drainEvents();
        renderer_->processSlashCommand(command);
        std::vector<std::string> out;
        for (auto& ev : bridge_.drainEvents()) {
            if (ev.type == eqt::state::GameEventType::SystemMessage) {
                out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
            }
        }
        return out;
    }

    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ICameraSceneNode* cam_ = nullptr;
    TestBridge bridge_;
    std::unique_ptr<IrrlichtRenderer> renderer_;
};

// T466's baseline pose in the client frame: qeynos2 (428, -74), heading 480
// (server heading 160 through the 90 - h flip).
constexpr float kX = 428.0f, kY = -74.0f, kZ = 3.119f, kHeading = 480.0f;

TEST_F(ChaseCameraFitTest, ChaseSitsAtTheOracleFitWithTheLookAtOnThePlayer) {
    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    renderer_->PlayerCamera().setCameraMode(CameraMode::Chase);
    const Pose want = FitPose(kX, kY, kZ, kHeading);
    ExpectNear(Node().cam, want.cam, "camera");
    ExpectNear(Node().target, want.target, "look-at");
    // The numbers T471 quoted for the fit: 3.978 behind, 7.418 above the feet.
    const float dx = Node().cam.X - kX, dz = Node().cam.Z - kY;
    EXPECT_NEAR(std::sqrt(dx * dx + dz * dz), 3.978f, 1e-3f);
    EXPECT_NEAR(Node().cam.Y - kZ, 7.418f, 1e-3f);
}

TEST_F(ChaseCameraFitTest, ChaseTurnsWithThePlayer) {
    renderer_->PlayerCamera().setCameraMode(CameraMode::Chase);
    for (float heading : {160.0f, 288.0f, 0.0f, 480.0f}) {
        renderer_->setPlayerPosition(50.0f, -20.0f, 4.0f, heading);
        const Pose want = FitPose(50.0f, -20.0f, 4.0f, heading);
        ExpectNear(Node().cam, want.cam, "camera");
        ExpectNear(Node().target, want.target, "look-at");
    }
}

TEST_F(ChaseCameraFitTest, ZoomActsOnTheChaseDistanceAndLeavesTheLookAtAlone) {
    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    renderer_->PlayerCamera().setCameraMode(CameraMode::Chase);
    renderer_->PlayerCamera().zoomCamera(2.0f, 0.016f);
    Pose want = FitPose(kX, kY, kZ, kHeading, kFitDistance + 2.0f);
    ExpectNear(Node().cam, want.cam, "camera after zoom out");
    ExpectNear(Node().target, want.target, "look-at after zoom out");
    renderer_->PlayerCamera().zoomCamera(-4.0f, 0.016f);
    want = FitPose(kX, kY, kZ, kHeading, kFitDistance - 2.0f);
    ExpectNear(Node().cam, want.cam, "camera after zoom in");
    // Zoom survives a move: it is the Chase distance, not a one-frame nudge.
    renderer_->setPlayerPosition(kX + 5.0f, kY, kZ, kHeading);
    want = FitPose(kX + 5.0f, kY, kZ, kHeading, kFitDistance - 2.0f);
    ExpectNear(Node().cam, want.cam, "camera after a move");
}

TEST_F(ChaseCameraFitTest, AWallBetweenTheLookAtAndTheCameraPullsTheChaseCameraIn) {
    // Heading 0 faces +Y, so the fitted camera sits at y = -3.978, behind a
    // wall at y = -2 that the player's side cannot see through.
    BspTree bsp = WallAtY(-2.0f);
    std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBoxTop(120.0f)}, {1, EqBoxTop(120.0f)}};
    Controller().setBspCollision(&bsp, &bounds);
    renderer_->setPlayerPosition(0.0f, 0.0f, 0.0f, 0.0f);
    renderer_->PlayerCamera().setCameraMode(CameraMode::Chase);

    const Pose want = FitPose(0.0f, 0.0f, 0.0f, 0.0f);
    const Pose got = Node();
    ExpectNear(got.target, want.target, "look-at");
    const float used = got.cam.getDistanceFrom(got.target);
    EXPECT_LT(used, 2.1f) << "the camera was not pulled in front of the wall";
    EXPECT_GE(used, 1.0f);
    EXPECT_GT(got.cam.Z, -2.0f) << "the camera is still behind the wall";
    // Pulled in ALONG the look-at -> camera ray: the direction is the fit's.
    irr::core::vector3df dirGot = got.cam - got.target;
    irr::core::vector3df dirWant = want.cam - want.target;
    dirGot.normalize();
    dirWant.normalize();
    EXPECT_GT(dirGot.dotProduct(dirWant), 0.9999f);
}

TEST_F(ChaseCameraFitTest, TheEyeRegionCeilingHoldsTheChaseCameraUnderALowCeiling) {
    // Above the eye (6.023) and below the look-at (7.0): the T464 clamp.
    BspTree bsp = OneRegion();
    const float top = 7.2f;
    std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBoxTop(top)}};
    Controller().setBspCollision(&bsp, &bounds);
    renderer_->setPlayerPosition(0.0f, 0.0f, 0.0f, 0.0f);
    renderer_->PlayerCamera().setCameraMode(CameraMode::Chase);
    EXPECT_NEAR(Node().cam.Y, top - kCollisionOffset, 1e-3f) << "the camera is above the ceiling";
    EXPECT_LE(Node().target.Y, top - kCollisionOffset + 1e-3f) << "the look-at is above the ceiling";
    ASSERT_GT(top - kCollisionOffset, kFitEyeAboveFeet) << "the case must sit above the eye";
    // The clamp lowers the camera; it does not move it off the fit horizontally,
    // and the look-at stays on the player's axis.
    const Pose want = FitPose(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(Node().cam.X, want.cam.X, 1e-3f);
    EXPECT_NEAR(Node().cam.Z, want.cam.Z, 1e-3f) << "the camera is not at the fit's distance behind";
    EXPECT_NEAR(Node().target.X, 0.0f, 1e-3f);
    EXPECT_NEAR(Node().target.Z, 0.0f, 1e-3f) << "the look-at is not on the player";
}

TEST_F(ChaseCameraFitTest, ABoxTopBelowTheEyeOrAboveTheCameraIsNoCeiling) {
    for (float top : {kFitEyeAboveFeet - 1.0f, 50.0f}) {
        BspTree bsp = OneRegion();
        std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBoxTop(top)}};
        Controller().setBspCollision(&bsp, &bounds);
        renderer_->setPlayerPosition(0.0f, 0.0f, 0.0f, 0.0f);
        renderer_->PlayerCamera().setCameraMode(CameraMode::Overhead);
        renderer_->PlayerCamera().setCameraMode(CameraMode::Chase);
        const Pose want = FitPose(0.0f, 0.0f, 0.0f, 0.0f);
        ExpectNear(Node().cam, want.cam, top < 10.0f ? "top below the eye" : "top above the camera");
        ExpectNear(Node().target, want.target, "look-at");
    }
}

TEST_F(ChaseCameraFitTest, CameraSetInChaseEditsTheChaseCamera) {
    renderer_->setPlayerPosition(kX, kY, kZ, kHeading);
    Run("/camera chase");
    const auto msgs = Run("/camera set pitch 8 dist 5");
    ASSERT_EQ(msgs.size(), 1u) << "no switch message: Chase is edited in place";
    EXPECT_EQ(msgs[0], "Chase Camera: az=0.000 pitch=8.000 dist=5.000 height=7.000");
    EXPECT_EQ(renderer_->getCameraMode(), CameraMode::Chase);

    const auto lines = Run("/camera get");
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[1], "[CAMERA-GET] az=0.000 pitch=8.000 dist=5.000 height=7.000 "
                        "units=deg,deg,eq_units,eq_units az_frame=player_heading");
    // And the node went there.
    const float h = kHeading / 512.0f * 2.0f * kPi;
    const float p = 8.0f * kPi / 180.0f;
    EXPECT_NEAR(Node().cam.X, kX - std::sin(h) * 5.0f * std::cos(p), 1e-3f);
    EXPECT_NEAR(Node().cam.Y, kZ + kFitHeight + 5.0f * std::sin(p), 1e-3f);
}

}  // namespace
