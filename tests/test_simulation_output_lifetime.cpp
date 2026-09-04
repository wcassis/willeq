// T116 -- coverage for the SimulationOutput dangling-pointer class made
// impossible by construction: applySimulationOutput() split by lifetime
// contract from bindFrameSimulationBuffers(), so the pointer-storing half is
// only reachable from the caller that holds a persistent buffer
// (applySimulationResults(), the runtime path). See
// refactor/T116_simulationoutput_lifetime.md for the full history: T111 and
// T115 each fixed one instance of this exact class (regionPvsDepthMap_/
// particleRenderBuffer_, then portalVisibleRegions_) by nulling the member
// again on the loading thread after applySimulationOutput() ran -- and the
// third instance was added directly beneath T111's comment warning about the
// first two. A comment did not prevent recurrence.
//
// L07 (Batch L) moved both bodies onto EQT::Graphics::SimBridgeComponent, so
// the two production calls now go through renderer.SimBridge(). Nothing else
// here changed: all four pointer members and usePvsCulling_/particleManager_
// stayed on IrrlichtRenderer, so the FRIEND_TEST lines below still grant the
// access every assertion needs, and every assertion observes exactly what it
// observed before.
//
// These tests call the REAL production functions (applySimulationOutput()
// and the new bindFrameSimulationBuffers()) via FRIEND_TEST access -- not a
// reimplementation or a hand-built fake of their logic. This codebase's
// dominant defect class is implemented-but-unreached code, so the point is
// to prove the actual function bodies behave as claimed under the actual
// conditions that gate each pointer store (usePvsCulling_ + non-empty
// regionVisible; particleOutput.valid + particleManager_), not merely that
// a test compiles.
//
// No Irrlicht device needed: same reasoning as test_irrlicht_camera_command
// (T66) and test_progressive_load_rearm (T111) -- a default-constructed,
// never-Init()'d IrrlichtRenderer is safe to construct/destroy, and neither
// applySimulationOutput() nor bindFrameSimulationBuffers() dereference
// smgr_/driver_ under the empty-container inputs these tests use
// (regionMeshNodes_, objectNodes_, objectLights_, entityRenderer_'s map are
// all empty on a fresh instance; particleManager_/detailManager_/
// spellVisualFX_/weatherSystem_/skyRenderer_/zoneShader_/playerLightNode_
// are all null and every call site null-guards them).

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/simulation_worker.h"

// FRIEND_TEST requires the test fixture/TEST_F-generated classes to live in
// the SAME namespace as the class under test (gtest_prod.h's own doc
// comment: "putting MyClassTest in an anonymous namespace will not work") --
// IrrlichtRenderer's FRIEND_TEST declarations name these classes
// unqualified, so they resolve against EQT::Graphics at that point, not
// ::(anonymous). Hence this whole file lives inside EQT::Graphics rather
// than the more common test-file anonymous-namespace idiom.
namespace EQT {
namespace Graphics {

class SimulationOutputLifetimeTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
};

// --- The load path's exact call: applySimulationOutput() alone, with a
//     SimulationOutput local, under the conditions that used to trigger
//     T111's/T115's dangling-pointer bugs (usePvsCulling_ on, non-empty
//     regionVisible, valid particle output). This is Step12b's real call
//     shape (renderer_zone_load.cpp), minus the surrounding zone-load
//     plumbing that isn't relevant to the pointer-lifetime question. ---

TEST_F(SimulationOutputLifetimeTest, ApplyOutputAloneLeavesAllFourPointersUntouched) {
    renderer.usePvsCulling_ = true;

    // Sentinels: distinct, recognizable, non-null addresses that do NOT
    // belong to `output` below. If applySimulationOutput() ever points a
    // member at `output`'s storage again, these assertions fail even though
    // `output` is still alive here (this test doesn't need it to go out of
    // scope to catch the regression -- unlike the real bug, which only
    // faulted after the loading thread's stack was reused).
    std::unordered_map<size_t, uint8_t> sentinelDepthMap{{1, 2}};
    std::unordered_set<size_t> sentinelPortalSet{7, 8};
    std::vector<EQT::Graphics::Environment::UnifiedParticle> sentinelParticles(3);
    const SimulationOutput* sentinelFrameResults =
        reinterpret_cast<const SimulationOutput*>(0x1);

    renderer.regionPvsDepthMap_ = &sentinelDepthMap;
    renderer.portalVisibleRegions_ = &sentinelPortalSet;
    renderer.particleRenderBuffer_ = &sentinelParticles;
    renderer.currentFrameSimResults_ = sentinelFrameResults;

    {
        // Mirrors loadZoneSequential() Step12b exactly: SimulationOutput as
        // a plain local, populated enough to force every gated branch that
        // used to store a pointer.
        SimulationOutput output;
        output.regionVisible = {1, 1};
        output.currentPvsRegion = 0;
        output.portalVisibleRegions = {5, 6};
        output.regionPvsDepth = {{5, 0}, {6, 1}};
        output.particleOutput.valid = true;
        output.particleOutput.renderBuffer.resize(2);

        renderer.SimBridge().applySimulationOutput(output);
        // Still inside output's scope: even here, immediately after the
        // call, none of the four members should have moved.
        EXPECT_EQ(renderer.regionPvsDepthMap_, &sentinelDepthMap);
        EXPECT_EQ(renderer.portalVisibleRegions_, &sentinelPortalSet);
        EXPECT_EQ(renderer.particleRenderBuffer_, &sentinelParticles);
        EXPECT_EQ(renderer.currentFrameSimResults_, sentinelFrameResults);
    }
    // After output's scope ends (the real hazard window: T115's crash read
    // portalVisibleRegions_ on the NEXT frame, after the loading thread's
    // stack had already been reused) -- still unmoved, because
    // applySimulationOutput() never wrote them in the first place.
    EXPECT_EQ(renderer.regionPvsDepthMap_, &sentinelDepthMap);
    EXPECT_EQ(renderer.portalVisibleRegions_, &sentinelPortalSet);
    EXPECT_EQ(renderer.particleRenderBuffer_, &sentinelParticles);
    EXPECT_EQ(renderer.currentFrameSimResults_, sentinelFrameResults);
}

// --- Same, but with usePvsCulling_ off and empty particle output -- the
//     "nothing gated ever fires" path. Confirms the split isn't hiding a
//     second code path that still stores a pointer under different
//     conditions. ---

TEST_F(SimulationOutputLifetimeTest, ApplyOutputAloneLeavesAllFourPointersUntouched_PvsCullingOff) {
    renderer.usePvsCulling_ = false;

    std::unordered_map<size_t, uint8_t> sentinelDepthMap{{9, 9}};
    std::unordered_set<size_t> sentinelPortalSet{9};
    renderer.regionPvsDepthMap_ = &sentinelDepthMap;
    renderer.portalVisibleRegions_ = &sentinelPortalSet;

    SimulationOutput output;
    output.regionVisible = {1};  // non-empty, but usePvsCulling_ is false
    output.particleOutput.valid = false;

    renderer.SimBridge().applySimulationOutput(output);

    EXPECT_EQ(renderer.regionPvsDepthMap_, &sentinelDepthMap);
    EXPECT_EQ(renderer.portalVisibleRegions_, &sentinelPortalSet);
    EXPECT_EQ(renderer.particleRenderBuffer_, nullptr);
}

// --- The runtime path's exact call: bindFrameSimulationBuffers(), given a
//     pointer that (per its own contract) outlives the frame -- proving it
//     actually performs the binds applySimulationOutput() no longer does. ---

TEST_F(SimulationOutputLifetimeTest, BindBindsAllFourFromThePassedPersistentPointer) {
    renderer.usePvsCulling_ = true;
    // Real ParticleManager, not a fake -- its constructor only stores the
    // (here null) smgr_/driver_ pointers, does no GL/device work (verified
    // by reading particle_manager.cpp), so it's safe to construct headlessly
    // and lets this test exercise bind's true particle-buffer branch for
    // real instead of only ever hitting the "no particleManager_" early-out.
    renderer.particleManager_ = std::make_unique<Environment::ParticleManager>(nullptr, nullptr);

    SimulationOutput persistent;
    persistent.regionVisible = {1};
    persistent.currentPvsRegion = 3;
    persistent.portalVisibleRegions = {10, 11};
    persistent.regionPvsDepth = {{10, 0}};
    persistent.particleOutput.valid = true;
    persistent.particleOutput.renderBuffer.resize(5);

    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);

    EXPECT_EQ(renderer.currentFrameSimResults_, &persistent);
    EXPECT_EQ(renderer.regionPvsDepthMap_, &persistent.regionPvsDepth);
    EXPECT_EQ(renderer.portalVisibleRegions_, &persistent.portalVisibleRegions);
    EXPECT_EQ(renderer.particleRenderBuffer_, &persistent.particleOutput.renderBuffer);
}

// --- bind must NOT populate the PVS pointers when usePvsCulling_ is off or
//     regionVisible is empty -- mirrors applySimulationOutput()'s own value
//     gate exactly, so a member never binds under conditions
//     applySimulationOutput() itself wouldn't have populated it under. ---

TEST_F(SimulationOutputLifetimeTest, BindOnlyBindsPvsMembersWhenPvsCullingConditionsHold) {
    std::unordered_map<size_t, uint8_t> sentinelDepthMap{{1, 1}};
    std::unordered_set<size_t> sentinelPortalSet{1};

    SimulationOutput persistent;
    persistent.regionVisible.clear();  // empty -- gate fails even with culling on

    renderer.usePvsCulling_ = true;
    renderer.regionPvsDepthMap_ = &sentinelDepthMap;
    renderer.portalVisibleRegions_ = &sentinelPortalSet;
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);
    EXPECT_EQ(renderer.regionPvsDepthMap_, &sentinelDepthMap)
        << "bind must not touch the PVS pointers when regionVisible is empty";
    EXPECT_EQ(renderer.portalVisibleRegions_, &sentinelPortalSet);

    persistent.regionVisible = {1};  // non-empty now, but culling off
    renderer.usePvsCulling_ = false;
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);
    EXPECT_EQ(renderer.regionPvsDepthMap_, &sentinelDepthMap)
        << "bind must not touch the PVS pointers when usePvsCulling_ is false";
    EXPECT_EQ(renderer.portalVisibleRegions_, &sentinelPortalSet);
}

// --- bind must null particleRenderBuffer_ the instant particle output stops
//     being valid -- preserves the pre-split else-branch behavior (stale
//     particle data must never be read past the frame it was produced). ---

TEST_F(SimulationOutputLifetimeTest, BindOnlyBindsParticleBufferWhenParticleOutputValid) {
    SimulationOutput persistent;
    persistent.particleOutput.valid = false;

    std::vector<Environment::UnifiedParticle> sentinelParticles(1);
    renderer.particleRenderBuffer_ = &sentinelParticles;

    // particleManager_ is null on a fresh instance -- both the "invalid
    // output" and "no particleManager_" reasons converge on the same
    // null-out, matching applySimulationOutput()'s original
    // `results->particleOutput.valid && particleManager_` gate.
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);
    EXPECT_EQ(renderer.particleRenderBuffer_, nullptr);

    persistent.particleOutput.valid = true;
    persistent.particleOutput.renderBuffer.resize(4);
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);
    // particleManager_ still null -- must still be null, not bound, proving
    // bind requires BOTH conditions like the original code did (the
    // both-true, actually-bound case is covered by
    // BindBindsAllFourFromThePassedPersistentPointer with a real
    // ParticleManager).
    EXPECT_EQ(renderer.particleRenderBuffer_, nullptr);

    // Now add a real particleManager_ but drop validity back to false --
    // proves the null-out isn't just "particleManager_ was never set".
    renderer.particleManager_ = std::make_unique<Environment::ParticleManager>(nullptr, nullptr);
    persistent.particleOutput.valid = false;
    renderer.particleRenderBuffer_ = &sentinelParticles;
    renderer.SimBridge().bindFrameSimulationBuffers(&persistent);
    EXPECT_EQ(renderer.particleRenderBuffer_, nullptr);
}

// --- The load path's real failure shape, reproduced directly: TWO separate
//     locals, each going out of scope, each passed through the real
//     applySimulationOutput() in turn (mirrors Step12b running once per
//     zone load, with a fresh stack local every time). If any pointer ever
//     aliased the first local's storage, reusing that stack slot for the
//     second local's fields would (in the pre-split code, given ASAN or bad
//     luck) manifest as reading the second local's data through a pointer
//     that thinks it's the first's -- this asserts the sentinel from before
//     the FIRST call is still exactly what's there after the SECOND. ---

TEST_F(SimulationOutputLifetimeTest, RepeatedApplyOutputWithFreshLocalsNeverDangles) {
    renderer.usePvsCulling_ = true;
    std::unordered_map<size_t, uint8_t> sentinelDepthMap{{42, 42}};
    renderer.regionPvsDepthMap_ = &sentinelDepthMap;

    for (int i = 0; i < 2; ++i) {
        SimulationOutput output;
        output.regionVisible = {1};
        output.regionPvsDepth = {{static_cast<size_t>(i), static_cast<uint8_t>(i)}};
        renderer.SimBridge().applySimulationOutput(output);
    }

    EXPECT_EQ(renderer.regionPvsDepthMap_, &sentinelDepthMap);
}

}  // namespace Graphics
}  // namespace EQT
