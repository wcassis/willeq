/**
 * B1071 (GitLab #391) -- the current target is never culled by the sim worker.
 *
 * Pre-fix: `SimulationWorker::computeEntityVisibility` (sim_entity_sync.cpp)
 * exempted only the player from distance / PVS / frustum / full-model-cap
 * culling. A targeted entity whose region went PVS-invisible (or was beyond
 * the render distance, or fell off the full-model cap) got
 * `shouldBeVisible=false`, which `renderer_sim_bridge.cpp` applies as
 * `sceneNode->remove()` + `inSceneGraph=false` -- the node leaves the scene
 * graph and is invisible on screen. The impostor pass's pin
 * (`entity_impostors.cpp`, `c.pinned = id == impostorTargetId_`) only keeps a
 * *candidate* full; an out-of-graph entity is *ineligible* for candidacy
 * (`eligible()` requires `inSceneGraph`), so the pin never reaches it and its
 * `setVisible(true)` cannot re-parent a removed node. The target outline
 * (`OverlaysComponent::drawTargetOutline`, which only checks `isVisible()`,
 * true on a removed node) keeps painting the hull over the missing mesh:
 * B1064's "solid silhouette, mesh not in the depth buffer" still.
 *
 * The fix threads `SimulationInput::targetId` (the same id the impostor pass
 * pins on, read from `Overlays().currentTargetId()` in
 * `postSimulationInput()`) into the worker, and `computeEntityVisibility`
 * exempts it exactly like the player.
 *
 * These tests drive the real `SimulationWorker::computeOnce()` -- no device,
 * no thread, no assets -- the same seam as test_pvs_render_distance_trigger.
 */

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "client/graphics/simulation_worker.h"
#include "client/zone_bsp.h"

namespace EQT {
namespace Graphics {
namespace {

// One worker pass with the given entity snapshots and target id, camera at
// the origin, no frustum, no PVS, no occlusion -- so distance and the
// full-model cap are the ONLY culling arms that can fire.
struct CullCase {
    std::vector<uint16_t> spawnIds;  // default: all at 100 units on +X
    std::vector<std::array<float, 3>> spawnPositions;  // empty = all at (100,0,0)
    uint16_t targetId = 0;           // 0 = nothing targeted
    float renderDistance = 300.0f;
    int maxVisible = 50;
};

SimulationOutput run(const CullCase& c, SimulationWorker& worker, uint32_t frame) {
    SimulationInput input;
    input.loadingActive = false;   // the culling branch, not the loading one
    input.renderDistance = c.renderDistance;
    input.deltaTime = 0.1f;
    input.frustumValid = false;    // isolate the distance/cap arms
    input.camEqX = 0.0f; input.camEqY = 0.0f; input.camEqZ = 0.0f;
    input.playerX = 0.0f; input.playerY = 0.0f; input.playerZ = 0.0f;
    input.frameNumber = frame;
    input.entityCullingEnabled = true;
    input.entityRenderDistance = c.renderDistance;
    input.maxVisibleEntities = c.maxVisible;
    input.targetId = c.targetId;

    for (size_t i = 0; i < c.spawnIds.size(); ++i) {
        uint16_t id = c.spawnIds[i];
        SimulationInput::EntitySnapshot s;
        s.spawnId = id;
        // default 100 units on +X (EQ Z-up: X=100, Y=height, Z=0)
        float x = (i < c.spawnPositions.size()) ? c.spawnPositions[i][0] : 100.0f;
        float y = (i < c.spawnPositions.size()) ? c.spawnPositions[i][1] : 0.0f;
        float z = (i < c.spawnPositions.size()) ? c.spawnPositions[i][2] : 0.0f;
        s.lastX = x; s.lastY = y; s.lastZ = z;
        s.velocityX = 0.0f; s.velocityY = 0.0f; s.velocityZ = 0.0f;
        s.serverX = x; s.serverY = y; s.serverZ = z;
        s.serverHeading = 0.0f;
        s.timeSinceUpdate = 0.0f; s.lastUpdateInterval = 0.0f;
        s.collisionZOffset = 0.0f; s.modelYOffset = 0.0f;
        s.serverAnimation = 0; s.lastNonZeroAnimation = 0;
        s.cachedBspRegion = 0; s.bspRegionDirty = false;
        s.isNPC = true; s.isPlayer = false; s.isCorpse = false; s.isFading = false;
        s.inSceneGraph = true; s.hasVelocity = false;
        input.entitySnapshots.push_back(s);
    }

    SimulationOutput output;
    worker.computeOnce(input, output);
    return output;
}

bool resultVisible(const SimulationOutput& out, uint16_t id) {
    for (const auto& er : out.entityResults)
        if (er.spawnId == id) return er.shouldBeVisible;
    return false;  // not in results at all = not visible
}

class TargetCullExemptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        SimulationZoneData data;
        // No PVS: a trivial one-leaf tree (calculateRegions returns early
        // with no region geometry, so distance/cap are the only arms).
        auto bsp = std::make_shared<BspTree>();
        BspNode leaf; leaf.regionId = 1; leaf.left = -1; leaf.right = -1;
        bsp->nodes = {leaf};
        bsp->regions.resize(1);
        bsp->regions[0] = std::make_shared<BspRegion>();
        bsp->regions[0]->containsPolygons = true;
        bsp->regions[0]->setPvs(std::vector<bool>(1, true));
        data.bspTree = bsp;
        data.usePvsCulling = false;
        data.regionBounds.resize(1);
        data.regionBounds[0] = {0, -5.0f, -5.0f, -5.0f, 105.0f, 5.0f, 5.0f};
        data.allBoundsIndices = {0};
        worker_.setZoneData(data);
    }

    SimulationWorker worker_;
    uint32_t frame_ = 0;
};

// A target beyond the render distance is culled like any other entity when
// nothing is targeted ...
TEST_F(TargetCullExemptionTest, BeyondRenderDistanceIsCulledWhenNotTargeted) {
    CullCase c;
    c.spawnIds = {1, 2};
    c.renderDistance = 50.0f;  // both at 100 units are beyond it
    c.targetId = 0;
    auto out = run(c, worker_, ++frame_);
    EXPECT_FALSE(resultVisible(out, 1));
    EXPECT_FALSE(resultVisible(out, 2));
}

// ... but the SAME entity, once it is the target, stays visible for the same
// reason the player is exempt: the worker must not remove a node the pin is
// about to draw (renderer_sim_bridge.cpp: sceneNode->remove() -> invisible
// -> ineligible -> pin never reaches it -> B1064's solid-hull still).
TEST_F(TargetCullExemptionTest, TargetBeyondRenderDistanceStaysVisible) {
    CullCase c;
    c.spawnIds = {1, 2};
    c.renderDistance = 50.0f;  // both at 100 units are beyond it
    c.targetId = 1;
    auto out = run(c, worker_, ++frame_);
    EXPECT_TRUE(resultVisible(out, 1))
        << "the current target must be exempt from distance culling -- a "
           "culled target leaves the scene graph and is invisible while the "
           "outline keeps painting its hull (B1064)";
    EXPECT_FALSE(resultVisible(out, 2))
        << "the non-target is still culled: the exemption is one entity, not a widening";
}

// The full-model cap arm: with maxVisibleEntities=1 the nearest entity
// survives and the rest are culled by the cap. Targeting the far one pulls it
// back into the visible set (it would otherwise be capped), while the
// non-target middle one stays culled. Without the exemption the target here
// is culled by the cap exactly like the middle one -- the pre-fix failure.
TEST_F(TargetCullExemptionTest, TargetBeyondTheFullModelCapStaysVisible) {
    CullCase c;
    c.spawnIds = {1, 2, 3};
    c.spawnPositions = {{10.0f, 0.0f, 0.0f},   // near: the one the cap keeps
                        {50.0f, 0.0f, 0.0f},   // mid: capped
                        {100.0f, 0.0f, 0.0f}}; // far: the target
    c.renderDistance = 300.0f;
    c.maxVisible = 1;
    c.targetId = 3;
    auto out = run(c, worker_, ++frame_);
    EXPECT_TRUE(resultVisible(out, 1)) << "the nearest entity survives the cap";
    EXPECT_TRUE(resultVisible(out, 3))
        << "a target the full-model cap would cull must stay visible -- "
           "renderer_sim_bridge removes its node, which is exactly the "
           "B1064 invisible-target / solid-hull defect";
    EXPECT_FALSE(resultVisible(out, 2))
        << "the non-target is still capped: the exemption is one entity, not a widening";
}

// The exemption is keyed on the id the impostor pass pins on, so a target
// change moves the exemption with it: the old target is culled again, the
// new one is not.
TEST_F(TargetCullExemptionTest, ExemptionFollowsTargetChanges) {
    CullCase c;
    c.spawnIds = {1, 2};
    c.renderDistance = 50.0f;
    c.targetId = 1;
    auto first = run(c, worker_, ++frame_);
    EXPECT_TRUE(resultVisible(first, 1));
    EXPECT_FALSE(resultVisible(first, 2));

    c.targetId = 2;
    auto second = run(c, worker_, ++frame_);
    EXPECT_FALSE(resultVisible(second, 1))
        << "the old target goes back to normal culling once it is no longer targeted";
    EXPECT_TRUE(resultVisible(second, 2));
}

// The exemption does not leak into the player's own path, and a 0 target id
// (nothing targeted) changes nothing.
TEST_F(TargetCullExemptionTest, ZeroTargetIdCullsEverythingBeyondDistance) {
    CullCase c;
    c.spawnIds = {1, 2, 3};
    c.renderDistance = 50.0f;
    c.targetId = 0;
    auto out = run(c, worker_, ++frame_);
    EXPECT_FALSE(resultVisible(out, 1));
    EXPECT_FALSE(resultVisible(out, 2));
    EXPECT_FALSE(resultVisible(out, 3));
}

}  // namespace
}  // namespace Graphics
}  // namespace EQT
