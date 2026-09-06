/**
 * T412 Leg C (board B294) -- what actually forces a PVS recompute.
 *
 * `IrrlichtRenderer::forcePvsUpdate_` was set true in FOUR places
 * (setRenderDistance() in the header, hideLoadingScreen(),
 * PlayerCameraComponent's zone-in reposition, and the occlusion-diagnostic
 * debug key) and read NOWHERE -- T407's write-only field census found it. Its
 * comment said "set when render distance changes", so on its face changing the
 * render distance never forced the recompute it asked for.
 *
 * The flag is not a missing consumer, it is a leftover. Its one reader lived in
 * `IrrlichtRenderer::updatePvsVisibility()`, which cached the camera's BSP
 * region behind a 5-unit movement threshold in function-local statics; the flag
 * reset those statics. Commit 6914d99b ("Move unified particle physics to
 * SimulationWorker background thread") deleted that whole function when PVS
 * moved to the worker, and `portalCacheDirty_`, the other thing it reset, is
 * gone too. Since then `SimBridgeComponent::buildSimulationInput()` republishes
 * `input.renderDistance` and the camera position on EVERY frame and
 * `SimulationWorker::computeAll()` runs `calculateRegions()` +
 * `computeVisibility()` over them unconditionally: no threshold, no cache, and
 * nothing left for a flag to invalidate. So the four writes were deleted rather
 * than given an invented consumer.
 *
 * That argument is only as good as its evidence, so these tests pin the
 * replacement trigger itself through the worker's public computeOnce() seam --
 * no Irrlicht device, no thread, no assets -- plus a source guard that the
 * deleted flag stays deleted.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "client/graphics/simulation_worker.h"
#include "client/zone_bsp.h"

namespace EQT {
namespace Graphics {
namespace {

// Two regions on the +X axis: near at 100 units, far at 400. The camera sits at
// the origin and never moves, so render distance is the ONLY thing that decides
// what is visible.
constexpr float kNearRegionX = 100.0f;
constexpr float kFarRegionX = 400.0f;

class PvsRenderDistanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        SimulationZoneData data;
        // No BSP PVS: the distance arm is what this pins. calculateRegions()
        // returns early with no bspTree, so a trivial one-leaf tree is supplied.
        auto bsp = std::make_shared<BspTree>();
        BspNode leaf; leaf.regionId = 1; leaf.left = -1; leaf.right = -1;
        bsp->nodes = {leaf};
        bsp->regions.resize(1);
        bsp->regions[0] = std::make_shared<BspRegion>();
        bsp->regions[0]->containsPolygons = true;
        bsp->regions[0]->visibleRegions = std::vector<bool>(1, true);
        data.bspTree = bsp;
        data.usePvsCulling = false;

        data.regionBounds.resize(2);
        data.regionBounds[0] = {0, kNearRegionX - 5.0f, -5.0f, -5.0f,
                                   kNearRegionX + 5.0f,  5.0f,  5.0f};
        data.regionBounds[1] = {1, kFarRegionX - 5.0f, -5.0f, -5.0f,
                                   kFarRegionX + 5.0f,  5.0f,  5.0f};
        data.allBoundsIndices = {0, 1};

        worker_.setZoneData(data);
    }

    // One worker pass at the given render distance, camera fixed at the origin.
    SimulationOutput runAt(float renderDistance) {
        SimulationInput input;
        input.loadingActive = false;   // the culling branch, not the loading one
        input.renderDistance = renderDistance;
        input.deltaTime = 0.1f;
        input.frustumValid = false;    // isolate the distance arm
        input.camEqX = 0.0f; input.camEqY = 0.0f; input.camEqZ = 0.0f;
        input.playerX = 0.0f; input.playerY = 0.0f; input.playerZ = 0.0f;
        input.frameNumber = ++frame_;
        SimulationOutput output;
        worker_.computeOnce(input, output);
        return output;
    }

    SimulationWorker worker_;
    uint64_t frame_ = 0;
};

// The trigger that replaced the flag: a render-distance change is honoured by
// the very next compute, with the camera unmoved and nothing invalidated.
TEST_F(PvsRenderDistanceTest, RenderDistanceChangeIsHonouredOnTheNextCompute) {
    SimulationOutput tight = runAt(200.0f);
    ASSERT_EQ(2u, tight.regionVisible.size());
    EXPECT_EQ(1, tight.regionVisible[0]) << "the near region is inside 200 units";
    EXPECT_EQ(0, tight.regionVisible[1]) << "the far region is not";

    // Exactly what IrrlichtRenderer::setRenderDistance() changes -- and the only
    // thing that differs between these two passes.
    SimulationOutput wide = runAt(500.0f);
    ASSERT_EQ(2u, wide.regionVisible.size());
    EXPECT_EQ(1, wide.regionVisible[0]);
    EXPECT_EQ(1, wide.regionVisible[1])
        << "raising the render distance must reveal the far region on the NEXT "
           "pass -- this is the recompute forcePvsUpdate_ claimed to ask for";

    // And back down again, so this is a live read of renderDistance and not a
    // one-way widening.
    SimulationOutput tightAgain = runAt(200.0f);
    EXPECT_EQ(1, tightAgain.regionVisible[0]);
    EXPECT_EQ(0, tightAgain.regionVisible[1]);
}

// The camera-position half of the same argument: the region lookup is redone
// every pass, with no movement threshold to defeat.
TEST_F(PvsRenderDistanceTest, EveryPassRecomputesFromScratch) {
    SimulationOutput a = runAt(200.0f);
    EXPECT_EQ(1u, a.sortedRegions.size());

    SimulationOutput b = runAt(50.0f);
    EXPECT_TRUE(b.sortedRegions.empty())
        << "nothing is within 50 units; a cached result would still list the "
           "near region";
}

// The flag is gone and must stay gone. A member nothing reads has no runtime
// observable -- this is the SpellLandHookIsGone (T357) shape.
TEST(ForcePvsUpdateFlagIsGone, NoMemberAndNoWritesRemain) {
    auto slurp = [](const std::string& path) {
        std::ifstream in(path);
        EXPECT_TRUE(in.is_open()) << "cannot open: " << path;
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const std::string root = WILLEQ_REPO_ROOT;

    const std::string header = slurp(root + "/include/client/graphics/irrlicht_renderer.h");
    EXPECT_EQ(header.find("bool forcePvsUpdate_"), std::string::npos)
        << "the member came back";

    // All four writers named in B294.
    for (const char* tu : {"/include/client/graphics/irrlicht_renderer.h",
                           "/src/client/graphics/renderer/renderer_init.cpp",
                           "/src/client/graphics/player/renderer_camera.cpp",
                           "/src/client/graphics/frame/renderer_frame_input.cpp"}) {
        const std::string text = slurp(root + tu);
        EXPECT_EQ(text.find("forcePvsUpdate_ = true"), std::string::npos)
            << "a write to the deleted flag came back in " << tu;
    }

    // The trigger that replaced it is still wired: renderDistance is republished
    // to the worker every frame.
    const std::string simBridge = slurp(root + "/src/client/graphics/sim/renderer_sim_bridge.cpp");
    EXPECT_NE(simBridge.find("input.renderDistance = ctx_.renderDistance"), std::string::npos)
        << "SimulationWorker stopped being fed the live render distance -- the "
           "argument for deleting forcePvsUpdate_ no longer holds";
}

}  // namespace
}  // namespace Graphics
}  // namespace EQT
