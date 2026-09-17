/**
 * T464 (boards B507, B508) -- the Chase camera's BSP ceiling clamp.
 *
 * MEASURED before the fix (T459 x86 run x2, willeq.log frame 3539; this file's
 * nro case re-measures it from the zone's own BSP):
 *   - CameraController::setFollowPosition() clamped the camera height to
 *     regionBoundsMap_[region].MaxEdge.Y. The map is in the EQ Z-UP frame
 *     (ZoneLoadComponent::loadZoneStep2_BSP() builds it from EQ vertices), so
 *     MaxEdge.Y is a HORIZONTAL extent. nro region 1217 -- the region of the
 *     player's feet, eye and camera there -- has max (2761.75, -171.47, 10.81);
 *     the log's "ceiling at -172.0" is -171.47 - 0.5. The player stood at
 *     y -181.57, z -9.27, so every frame in that region clamped to
 *     eyeHeight*0.5 = 3.0: the latch. The vertical top, 10.81, is no ceiling
 *     at all for a camera 10 over those feet.
 *   - The region was the SimulationWorker's currentPvsRegion: the region of the
 *     CAMERA, whose height the clamp sets, fed back a frame late and only on a
 *     change. Where a split plane with a vertical component lies between the
 *     clamped and unclamped camera heights, and the two regions' boxes straddle
 *     the threshold, the height alternates between two states (B508's shape).
 *     T459's nro path has no such plane (0 of 201 steps, measured below).
 *
 * Before the fix the two constructed cases failed (camera height 3.0 instead
 * of 10.0; heights 3,10,3,10,3,10 over six frames through the camera-region
 * feedback). The controller now reads MaxEdge.Z of the player's EYE region,
 * which it looks up itself, and no longer has a region setter.
 *
 * T474 (board B650): setFollowPosition() is DELETED -- Chase is a rig at
 * T466's oracle fit (camera_rig.h kChaseRig) -- and the clamp moved with the
 * same two rules into CameraController::placeChaseCamera(). Every case below
 * is the T464 case re-expressed on the fit: the camera sits kChaseRig.height +
 * dist * sin(pitch) = 7.418 above the feet (the controller is driven directly,
 * so the feet are the node origin), the eye is the fitted character's
 * (kChaseFitEyeAboveFeet, 6.023), and the constructed boxes are moved to
 * straddle THOSE heights. The nro measurements of the pre-T474 geometry are
 * kept as they were taken, and say so.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <irrlicht.h>

#include "client/graphics/camera_controller.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/player/camera_rig.h"
#include "client/zone_bsp.h"

using namespace EQT::Graphics;

namespace {

// T459 x86 x2, willeq.log frame 3539.
constexpr float kPlayerX = 2617.87f;
constexpr float kPlayerY = -181.57f;
constexpr float kPlayerZ = -9.27f;
constexpr float kHeading = 37.01f;
constexpr float kMeasuredMaxEdgeY = -171.47f;  // nro region 1217, measured below
constexpr float kOldEyeHeight = 6.0f;           // the pre-T474 follow camera's eye, T459's logs
constexpr float kCollisionOffset = 0.5f;        // CAMERA_COLLISION_OFFSET
const float kUnclampedHeight =
    kChaseRig.height + kChaseRig.distance * std::sin(kChaseRig.pitchDeg * 3.14159265f / 180.0f);

class CameraBspCeilingTest : public ::testing::Test {
protected:
    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.LoggingLevel = irr::ELL_NONE;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        camera_ = device_->getSceneManager()->addCameraSceneNode();
    }
    void TearDown() override {
        if (device_) device_->drop();
    }

    float CameraHeightAbovePlayer() const { return camera_->getPosition().Y - kPlayerZ; }

    // The Chase camera at T459's pose: the unscaled fit, the fitted eye.
    static float PlaceChase(CameraController& cc) {
        const CameraRigPose pose =
            computeCameraRigPose(kChaseRig, kPlayerX, kPlayerY, kPlayerZ, kHeading);
        return cc.placeChaseCamera(pose, kPlayerZ + kChaseFitEyeAboveFeet);
    }

    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ICameraSceneNode* camera_ = nullptr;
};

std::shared_ptr<BspRegion> Region(std::vector<bool> pvs) {
    auto r = std::make_shared<BspRegion>();
    r->setPvs(std::move(pvs));
    return r;
}

// One leaf: region 0 everywhere.
BspTree OneRegion() {
    BspTree bsp;
    bsp.nodes.push_back(BspNode{});
    bsp.nodes[0].regionId = 1;
    bsp.regions.push_back(Region({true}));
    return bsp;
}

// A horizontal split at z = splitZ: region 1 above, region 0 below.
BspTree SplitAtZ(float splitZ, std::vector<bool> pvs0, std::vector<bool> pvs1) {
    BspTree bsp;
    bsp.nodes.resize(3);
    bsp.nodes[0].normalZ = 1.0f;
    bsp.nodes[0].splitDistance = -splitZ;
    bsp.nodes[0].left = 1;   // dot >= 0: above
    bsp.nodes[0].right = 2;  // below
    bsp.nodes[1].regionId = 2;
    bsp.nodes[2].regionId = 1;
    bsp.regions.push_back(Region(std::move(pvs0)));
    bsp.regions.push_back(Region(std::move(pvs1)));
    return bsp;
}

irr::core::aabbox3df EqBox(float maxY, float maxZ) {
    return irr::core::aabbox3df(irr::core::vector3df(2500.0f, -400.0f, -60.0f),
                                irr::core::vector3df(2700.0f, maxY, maxZ));
}

}  // namespace

// B507, the measured case: a region whose Y (horizontal) top is just north of
// the player and whose Z (vertical) top is above the camera.
TEST_F(CameraBspCeilingTest, AHorizontalExtentIsNotACeiling) {
    BspTree bsp = OneRegion();
    std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBox(kMeasuredMaxEdgeY, 120.0f)}};

    CameraController cc(camera_);
    cc.setBspCollision(&bsp, &bounds);
    PlaceChase(cc);

    EXPECT_NEAR(CameraHeightAbovePlayer(), kUnclampedHeight, 1e-3f)
        << "the clamp read the region's horizontal extent as a ceiling";
}

// The clamp still does its job, on the vertical axis.
TEST_F(CameraBspCeilingTest, TheVerticalTopOverTheEyeIsTheCeiling) {
    BspTree bsp = OneRegion();
    const float top = kPlayerZ + 7.2f;  // above the eye (+6.023), below the camera (+7.418)
    std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBox(500.0f, top)}};

    CameraController cc(camera_);
    cc.setBspCollision(&bsp, &bounds);
    PlaceChase(cc);

    EXPECT_NEAR(CameraHeightAbovePlayer(), 7.2f - kCollisionOffset, 1e-3f);
    EXPECT_LE(camera_->getTarget().Y - kPlayerZ, 7.2f - kCollisionOffset + 1e-3f);
}

// A box top at or below the eye is not over the player's head (a hilltop, a
// sloped outdoor region): with the axis fixed and no such guard, a player
// standing on a region's highest vertex would latch again.
TEST_F(CameraBspCeilingTest, ABoxTopBelowTheEyeIsNoCeiling) {
    BspTree bsp = OneRegion();
    std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBox(500.0f, kPlayerZ + 0.2f)}};

    CameraController cc(camera_);
    cc.setBspCollision(&bsp, &bounds);
    PlaceChase(cc);

    EXPECT_NEAR(CameraHeightAbovePlayer(), kUnclampedHeight, 1e-3f);
}

// B508's mechanism removed: the region above the eye (where the unclamped
// camera sits) has a box that WOULD clamp; the eye's region does not. The
// clamp now reads only the eye region, so six frames place the camera
// identically. Before the fix, feeding the camera's own region back (what
// SimBridgeComponent did) alternated 3,10,3,10,3,10.
TEST_F(CameraBspCeilingTest, TheClampReadsTheEyeRegionNotTheCameraRegion) {
    // The split lies between the eye (+6.023) and the look-at (+7.0).
    BspTree bsp = SplitAtZ(kPlayerZ + 6.5f, {true, true}, {true, true});
    std::map<size_t, irr::core::aabbox3df> bounds{
        {0, EqBox(500.0f, 120.0f)},
        {1, EqBox(500.0f, kPlayerZ + 7.0f)},
    };

    CameraController cc(camera_);
    cc.setBspCollision(&bsp, &bounds);
    std::vector<float> heights;
    for (int frame = 0; frame < 6; ++frame) {
        PlaceChase(cc);
        heights.push_back(CameraHeightAbovePlayer());
    }
    EXPECT_NEAR(heights[0], kUnclampedHeight, 1e-3f) << "the camera region's box was read";
    for (size_t i = 1; i < heights.size(); ++i) {
        EXPECT_NEAR(heights[i], heights[0], 1e-3f) << "frame " << i << " flipped";
    }
}

// The rig cameras' wall test now works without anyone handing the controller a
// region: before the fix bspPlayerRegion_ stayed SIZE_MAX until the
// SimulationWorker reported a region CHANGE, and BSP collision was off.
TEST_F(CameraBspCeilingTest, TheRigWallTestUsesTheLookAtRegion) {
    // Region 0 (below z=5) cannot see region 1 (above).
    BspTree bsp = SplitAtZ(5.0f, {true, false}, {false, true});
    std::map<size_t, irr::core::aabbox3df> bounds{{0, EqBox(500.0f, 120.0f)},
                                                  {1, EqBox(500.0f, 120.0f)}};
    CameraController cc(camera_);
    cc.setBspCollision(&bsp, &bounds);

    CameraRigPose pose;
    pose.targetX = 0.0f;
    pose.targetY = 0.0f;
    pose.targetZ = 0.0f;
    pose.camX = 0.0f;
    pose.camY = 0.0f;
    pose.camZ = 20.0f;
    const float used = cc.placeRigCamera(pose);
    EXPECT_LT(used, 6.0f) << "the camera was not pulled in front of the split";
    EXPECT_GE(used, 1.0f);
    EXPECT_FLOAT_EQ(cc.getLastPlacedDistance(), used);
}

// ---------------------------------------------------------------------------
// nro's real BSP
// ---------------------------------------------------------------------------
namespace {

std::string EqClientPath() {
    if (const char* p = std::getenv("EQ_CLIENT_PATH")) return p;
    return "/mnt/projects/EverQuestP1999";
}

// ZoneLoadComponent::loadZoneStep2_BSP()'s region box, statement for
// statement: the world position of every vertex of the region's geometry, in
// the EQ frame, keyed by region index.
std::map<size_t, irr::core::aabbox3df> RegionBoxes(const WldLoader& wld, const BspTree& bsp) {
    std::map<size_t, irr::core::aabbox3df> out;
    for (size_t i = 0; i < bsp.regions.size(); ++i) {
        auto geom = wld.getGeometryForRegion(i);
        if (!geom || geom->vertices.empty()) continue;
        float mn[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
        float mx[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                       std::numeric_limits<float>::lowest()};
        for (const auto& v : geom->vertices) {
            const float w[3] = {geom->centerX + v.x, geom->centerY + v.y, geom->centerZ + v.z};
            for (int a = 0; a < 3; ++a) {
                mn[a] = std::min(mn[a], w[a]);
                mx[a] = std::max(mx[a], w[a]);
            }
        }
        out[i] = irr::core::aabbox3df(irr::core::vector3df(mn[0], mn[1], mn[2]),
                                      irr::core::vector3df(mx[0], mx[1], mx[2]));
    }
    return out;
}

class CameraBspCeilingNroTest : public CameraBspCeilingTest {
protected:
    void SetUp() override {
        CameraBspCeilingTest::SetUp();
        if (IsSkipped()) return;
        const std::string s3d = EqClientPath() + "/nro.s3d";
        if (!std::ifstream(s3d).good()) GTEST_SKIP() << "no " << s3d;
        ASSERT_TRUE(wld_.parseFromArchive(s3d, "nro.wld"));
        bsp_ = wld_.getBspTree();
        ASSERT_TRUE(bsp_);
        boxes_ = RegionBoxes(wld_, *bsp_);
    }
    WldLoader wld_;
    std::shared_ptr<BspTree> bsp_;
    std::map<size_t, irr::core::aabbox3df> boxes_;
};

}  // namespace

// The numbers T459 logged, from the zone file itself (the pre-T474 geometry:
// eye 6 above the node origin).
TEST_F(CameraBspCeilingNroTest, Region1217IsWhereT459LatchedAndItsYTopIsTheLoggedCeiling) {
    const size_t feet = bsp_->findRegionIndexForPoint(kPlayerX, kPlayerY, kPlayerZ);
    const size_t eye = bsp_->findRegionIndexForPoint(kPlayerX, kPlayerY, kPlayerZ + kOldEyeHeight);
    // Frame 3538's camera, Irrlicht (2608.60, 0.74, -200.20) = EQ (x, z, y); the
    // log's "SimWorker apply: PVS=1217".
    const size_t cam = bsp_->findRegionIndexForPoint(2608.60f, -200.20f, 0.74f);
    EXPECT_EQ(feet, 1217u);
    EXPECT_EQ(eye, 1217u);
    EXPECT_EQ(cam, 1217u);
    auto it = boxes_.find(1217);
    ASSERT_NE(it, boxes_.end());
    // "BSP camera ceiling: limiting height to 3.0 (ceiling at -172.0)"
    EXPECT_NEAR(it->second.MaxEdge.Y - kCollisionOffset, -172.0f, 0.05f);
    EXPECT_GT(it->second.MaxEdge.Z, kPlayerZ + 10.0f + kCollisionOffset)
        << "the vertical top is above the unclamped camera: no ceiling applies";
}

// T474: the Chase fit at T459's pose, on nro's real BSP -- not latched, and
// not pulled in (the fitted camera spot is in a region the look-at can see).
TEST_F(CameraBspCeilingNroTest, TheCameraIsNotLatchedAtT459sPose) {
    CameraController cc(camera_);
    cc.setBspCollision(bsp_.get(), &boxes_);
    const float used = PlaceChase(cc);
    EXPECT_NEAR(CameraHeightAbovePlayer(), kUnclampedHeight, 1e-3f);
    EXPECT_NEAR(used, kChaseRig.distance, 1e-3f);
}

// B508's precondition on T459's own path (x86_camera.script's /move from
// (2611.1,-190.1,-11.2) to (2723.25,-41.75,-24.75)): no step has the camera
// in a different region at height 10 than at height 3, so the pre-fix
// two-state flip could not happen there. Recorded as a measurement OF THE
// PRE-T474 GEOMETRY (20 behind, heights 10 and 3 above the node origin) --
// the geometry T459 ran with -- and kept as it was taken.
TEST_F(CameraBspCeilingNroTest, T459sPathHasNoHeightDependentCameraRegion) {
    int heightDependent = 0;
    const float h = kHeading / 512.0f * 2.0f * 3.14159265f;
    for (int i = 0; i <= 200; ++i) {
        const float t = i / 200.0f;
        const float px = 2611.1f + (2723.25f - 2611.1f) * t;
        const float py = -190.1f + (-41.75f + 190.1f) * t;
        const float pz = -11.2f + (-24.75f + 11.2f) * t;
        const float cx = px - std::sin(h) * 20.0f;
        const float cy = py - std::cos(h) * 20.0f;
        if (bsp_->findRegionIndexForPoint(cx, cy, pz + 10.0f) !=
            bsp_->findRegionIndexForPoint(cx, cy, pz + 3.0f)) {
            ++heightDependent;
        }
    }
    std::printf("T459 path: %d of 201 steps have a height-dependent camera region\n",
                heightDependent);
    EXPECT_EQ(heightDependent, 0);
}
