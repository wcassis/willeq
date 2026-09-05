// T367 (board B080) -- a lamp the player is standing next to must reach the
// zone shader.
//
// MEASURED LIVE FIRST (review/T367/logs/fable_session_legA.log, qeynos2 at
// forced 22:00): fable logged `ZoneLightVis: 0/218 visible` on EVERY frame of
// a whole session. Standing 6 units from zone light 63 (radius 40) the census
// read `dist=164, pvs=44, frustum=10` -- 54 of the 218 lights were inside the
// render distance and every one of them was thrown away by the PVS or the
// frustum test. `computeLightSelection` therefore had no candidate but the
// player light, `uLightPos[1..7]` were never fed, and the lamp lit nothing.
// The official Titanium client at the same coordinates is x1.81 brighter at
// the lamp spot than at a lamp-free spot at the same hour; fable was x0.67.
//
// The rule the fix installs: a light whose own influence sphere CONTAINS the
// camera survives both remaining culls. PVS asks "can I see the lamp's own BSP
// region" and the frustum asks "is the lamp on screen"; neither question bears
// on whether the lamp lights the wall in front of the player.
//
// These tests drive the REAL production functions through the public
// computeOnce() seam -- computeLightVisibility, computeObjectLightVisibility
// and computeLightSelection -- with a two-region BSP whose PVS says the two
// regions cannot see each other, and a frustum that faces away from the lamp.
// No Irrlicht device, no server, no assets.

#include <gtest/gtest.h>

#include "client/graphics/simulation_worker.h"
#include "client/zone_bsp.h"

namespace EQT {
namespace Graphics {
namespace {

// EQ coords. The camera stands at the origin; the lamp is 6 units away on +X,
// exactly the live geometry (6 units from a radius-40 lamp).
constexpr float kLampDist = 6.0f;
constexpr float kLampRadius = 40.0f;

// A second lamp far outside its own radius, to pin that the fix does NOT
// disable PVS culling in general.
constexpr float kFarLampDist = 200.0f;
constexpr float kFarLampRadius = 40.0f;

class ZoneLightCullingTest : public ::testing::Test {
protected:
    // Two-region BSP: one split plane at x = 3. Region 1 (x < 3) holds the
    // camera, region 2 (x > 3) holds both lamps. The PVS says neither region
    // can see the other, which is the live qeynos2 case: the lamp sits in a
    // region the camera's region does not list.
    static std::shared_ptr<BspTree> makeSplitBsp() {
        auto bsp = std::make_shared<BspTree>();
        BspNode split;
        split.normalX = 1.0f; split.normalY = 0.0f; split.normalZ = 0.0f;
        split.splitDistance = 3.0f;
        split.regionId = 0;
        split.left = 1;    // in front of the plane (x > 3) -> region 2
        split.right = 2;   // behind (x < 3)                -> region 1
        BspNode leafFront; leafFront.regionId = 2; leafFront.left = -1; leafFront.right = -1;
        BspNode leafBack;  leafBack.regionId  = 1; leafBack.left  = -1; leafBack.right  = -1;
        bsp->nodes = {split, leafFront, leafBack};

        bsp->regions.resize(2);
        for (int i = 0; i < 2; ++i) {
            bsp->regions[i] = std::make_shared<BspRegion>();
            bsp->regions[i]->containsPolygons = true;
            // Nothing sees anything, including itself -- the harshest PVS.
            bsp->regions[i]->visibleRegions = std::vector<bool>(2, false);
        }
        return bsp;
    }

    void SetUp() override {
        SimulationZoneData data;
        data.bspTree = makeSplitBsp();
        data.usePvsCulling = true;
        // calculateRegions() returns early on empty regionBounds.
        data.regionBounds.resize(2);
        data.allBoundsIndices = {0, 1};

        // Irrlicht Y-up positions: (X, Z_eq, Y_eq).
        data.zoneLights.resize(2);
        data.zoneLights[0].position = irr::core::vector3df(kLampDist, 0.0f, 0.0f);
        data.zoneLights[0].bspRegion = 1;   // the far side of the split
        data.zoneLights[1].position = irr::core::vector3df(kFarLampDist, 0.0f, 0.0f);
        data.zoneLights[1].bspRegion = 1;

        data.zoneLightNodes.resize(2);
        data.zoneLightNodes[0].position = data.zoneLights[0].position;
        data.zoneLightNodes[0].diffuseColor = irr::video::SColorf(0.51f, 0.51f, 0.12f, 1.0f);
        data.zoneLightNodes[0].radius = kLampRadius;
        data.zoneLightNodes[0].attConstant = 1.0f;
        data.zoneLightNodes[0].attLinear = 1.0f / kLampRadius;
        data.zoneLightNodes[0].attQuadratic = 1.0f / (kLampRadius * kLampRadius);
        data.zoneLightNodes[1] = data.zoneLightNodes[0];
        data.zoneLightNodes[1].position = data.zoneLights[1].position;
        data.zoneLightNodes[1].radius = kFarLampRadius;

        // One object light, also next to the camera and also in the unseen
        // region, to pin the same rule on computeObjectLightVisibility.
        data.objectLights.resize(1);
        data.objectLights[0].position = irr::core::vector3df(0.0f, kLampDist, 0.0f);
        data.objectLights[0].bspRegion = 1;
        data.objectLights[0].radius = kLampRadius;
        data.objectLights[0].originalColor = irr::video::SColorf(1.0f, 0.6f, 0.2f, 1.0f);
        data.objectLights[0].attConstant = 1.0f;
        data.objectLights[0].attLinear = 1.0f / kLampRadius;
        data.objectLights[0].attQuadratic = 1.0f / (kLampRadius * kLampRadius);

        worker_.setZoneData(data);
    }

    // A frustum facing -X, i.e. AWAY from both lamps, so the frustum arm
    // would also throw them away on its own.
    static void setFrustumFacingAwayFromLamps(SimulationInput& input) {
        input.frustumValid = true;
        // Six half-spaces of a box on the -X side of the camera, in EQ coords
        // (n.p + d >= 0 means inside).
        const float planes[6][4] = {
            {-1.0f, 0.0f, 0.0f, -1.0f},   // x <= -1 (strictly behind the camera)
            { 1.0f, 0.0f, 0.0f, 1000.0f},
            { 0.0f, 1.0f, 0.0f, 1000.0f},
            { 0.0f,-1.0f, 0.0f, 1000.0f},
            { 0.0f, 0.0f, 1.0f, 1000.0f},
            { 0.0f, 0.0f,-1.0f, 1000.0f},
        };
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 4; ++j)
                input.frustumPlanes[i][j] = planes[i][j];
    }

    SimulationOutput run() {
        SimulationInput input;
        input.loadingActive = false;   // the CULLING branch, not the loading one
        input.renderDistance = 300.0f;
        input.deltaTime = 0.1f;
        input.visionType = 0;
        input.weatherAmbientModifier = 1.0f;
        input.cameraPos = irr::core::vector3df(0.0f, 0.0f, 0.0f);
        input.cameraTarget = irr::core::vector3df(-1.0f, 0.0f, 0.0f);
        input.camEqX = 0.0f; input.camEqY = 0.0f; input.camEqZ = 0.0f;
        input.playerX = 0.0f; input.playerY = 0.0f; input.playerZ = 0.0f;
        setFrustumFacingAwayFromLamps(input);
        SimulationOutput output;
        input.frameNumber = 1;
        worker_.computeOnce(input, output);
        return output;
    }

    static int selectedZoneLightCount(const SimulationOutput& output) {
        int n = 0;
        for (int i = 0; i < output.activeLightCount; ++i)
            if (output.selectedLights[i].valid && output.selectedLights[i].isZoneLight) n++;
        return n;
    }

    SimulationWorker worker_;
};

// The camera stands INSIDE the near lamp's radius. Pre-T367 this was 0.
TEST_F(ZoneLightCullingTest, LampContainingTheCameraSurvivesPvsAndFrustum) {
    SimulationOutput output = run();
    ASSERT_EQ(output.lightVisible.size(), 2u);
    EXPECT_EQ(output.lightVisible[0], 1)
        << "a lamp " << kLampDist << " units away with radius " << kLampRadius
        << " was culled -- this is the live qeynos2 defect (B080)";
}

// ...and the rule is not "disable PVS for lights": a lamp outside its own
// radius is still culled.
TEST_F(ZoneLightCullingTest, LampOutsideItsOwnRadiusIsStillPvsCulled) {
    SimulationOutput output = run();
    ASSERT_EQ(output.lightVisible.size(), 2u);
    EXPECT_EQ(output.lightVisible[1], 0)
        << "PVS/frustum culling of distant lights must be unchanged";
}

// The whole point: the surviving lamp has to reach the shader, which reads
// output.selectedLights.
TEST_F(ZoneLightCullingTest, TheSurvivingLampReachesLightSelection) {
    SimulationOutput output = run();
    EXPECT_EQ(selectedZoneLightCount(output), 1)
        << "computeLightSelection saw no zone light, so uLightPos[1..7] stay dark";
}

// The same rule on the object-light path (torch models placed in the zone).
TEST_F(ZoneLightCullingTest, ObjectLightContainingTheCameraSurvivesToo) {
    SimulationOutput output = run();
    ASSERT_EQ(output.objectLightVisible.size(), 1u);
    EXPECT_EQ(output.objectLightVisible[0], 1);
}

}  // namespace
}  // namespace Graphics
}  // namespace EQT
