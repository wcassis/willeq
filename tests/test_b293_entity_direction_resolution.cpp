// B293 (#124): the entity-direction callback was installed by two callers
// (renderer_entities.cpp, tools/model_viewer) and invoked by none.
//
// The mechanism, traced at this unit:
//   1. A spray cast is the one production consumer that wants it.
//      SpellVisualFX::createCastGlow() passes isSpray through as
//      ParticleManager::createSpellEffect()'s useDynamicDir
//      (spell_visual_fx.cpp, the hand-bone path).
//   2. The command crosses to the SimulationWorker, where the effect's
//      emitters carry useDynamicDirection (sim_particles.cpp,
//      CreateSpellEffect arm and the trigger arm).
//   3. Every frame the worker REQUIRES directions for its dynamic emitters:
//      SimulationOutput::particleOutput.directionRequestEntities.
//   4. Every frame it CONSUMES directions: pi.entityDirections feeds
//      emitter.dynamicDirection, which spawnSpellParticle() turns into
//      particle velocity (sim_particles.cpp).
//   5. The renderer side (SimBridgeComponent) fills pi.entityDirections from
//      pendingParticleEntityDirections_ each frame.
//
// The ONE missing link was step 5's other half: applySimulationOutput()
// resolved the PREVIOUS frame's positionRequestEntities into
// pendingParticleEntityPositions_ but had no direction arm --
// pendingParticleEntityDirections_ was cleared each frame and refilled with
// nothing. So entityDirCallback_ was stored by two installers and called by
// nobody, and every spray cast spawned with the emitter's static
// velocityBase instead of along the hand's forward vector.
//
// The fix adds the direction arm (a mirror of the position arm) and a
// ParticleManager::resolveEntityDirection() beside resolveEntityPosition().
// This test drives the REAL renderer body (applySimulationOutput(), no
// device, no GL -- the T116 lifetime fixtures prove a never-Init()'d
// renderer is safe to construct/destroy and to feed): install a counting
// direction callback on a real ParticleManager, feed the worker's real
// directionRequestEntities output, and assert the callback FIRED and its
// result landed in the pending map the next frame's worker input reads.
// Red at base (no arm: the callback never fires, the map stays empty);
// green after.

#include <gtest/gtest.h>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>
#include <irrlicht.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "particle_test_seed.h"

#include <memory>
#include <vector>

namespace EQT {
namespace Graphics {

class B293EntityDirectionResolutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // The renderer's ctx_ holds a reference to ITS particleManager_
        // member, so the manager must live inside the renderer for the
        // production body to see it (Test_SetParticleManagerForSeam, a
        // swap: nothing was there on a never-Init()'d renderer).
        injected_ = renderer_.Test_SetParticleManagerForSeam(
            std::make_unique<Environment::ParticleManager>(nullptr, nullptr));
        pm_ = renderer_.getParticleManager();
        ASSERT_NE(pm_, nullptr);
        pm_->Test_SetUnifiedRendererInitializedForSeam(true);
        // The resolver under test: counts its calls and returns a fixed
        // normalized direction for the one entity it knows.
        pm_->setEntityDirectionCallback(
            [this](uint16_t id, glm::vec3& out) -> bool {
                dirCallbackCalls_++;
                if (id != kCaster) return false;
                out = glm::vec3(1.0f, 0.0f, 0.0f);
                return true;
            });
    }

    void TearDown() override {
        // Restore so the renderer's own swap-in member never double-frees.
        renderer_.Test_SetParticleManagerForSeam(std::move(injected_));
        pm_ = nullptr;
    }

    // One real worker frame: hand it the queued commands, get its output.
    void PumpWorker(SimulationOutput& output,
                    const std::vector<ParticleCommandData>& commands) {
        auto input = SimulationInput();
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 1024;
        pi.commands = commands;
        worker_.computeOnce(input, output);
    }

    // Renderer declared FIRST: its destructor (and everything in the
    // T116-safe never-Init path) runs last, after this fixture's other
    // members.
    IrrlichtRenderer renderer_;
    std::unique_ptr<Environment::ParticleManager> injected_;
    Environment::ParticleManager* pm_ = nullptr;
    SimulationWorker worker_;
    int dirCallbackCalls_ = 0;

    static constexpr uint16_t kCaster = 288;
};

// The worker requires the caster's direction (the spray half of
// useDynamicDirection) -- sanity that the fixture reaches the code under
// test.
TEST_F(B293EntityDirectionResolutionTest, WorkerRequiresTheCasterDirectionForASprayEffect) {
    Environment::SpellEffectDef def;
    def.name = "B293TestSpray";
    Environment::SpellEmitterDef em;
    em.config.burstCount = 8;
    em.config.emitterLifetime = 0.5f;
    def.emitters.push_back(em);

    uint32_t id = pm_->createSpellEffect(def, kCaster, 0, 1.0f, /*useDynamicDir=*/true);
    ASSERT_NE(id, 0u) << "createSpellEffect refused to enqueue (seam flag missing?)";
    SimulationOutput output;
    PumpWorker(output, pm_->drainCommands());

    // The worker named the caster in BOTH request sets (cast-glow shape: the
    // attached emitter is the caster itself).
    EXPECT_TRUE(output.particleOutput.positionRequestEntities.count(kCaster) == 1u);
    EXPECT_TRUE(output.particleOutput.directionRequestEntities.count(kCaster) == 1u);
}

// The seam B293 closed: the renderer's output-application resolves the
// requested direction THROUGH the installed callback. Red at base -- no
// direction arm existed, so the callback never fired and the pending map
// stayed empty, which is exactly what made entityDirCallback_ write-only.
TEST_F(B293EntityDirectionResolutionTest, ApplySimulationOutputResolvesRequestedDirectionThroughTheCallback) {
    Environment::SpellEffectDef def;
    def.name = "B293TestSpray";
    Environment::SpellEmitterDef em;
    em.config.burstCount = 8;
    em.config.emitterLifetime = 0.5f;
    def.emitters.push_back(em);

    ASSERT_NE(pm_->createSpellEffect(def, kCaster, 0, 1.0f, /*useDynamicDir=*/true), 0u);
    SimulationOutput output;
    PumpWorker(output, pm_->drainCommands());
    ASSERT_TRUE(output.particleOutput.directionRequestEntities.count(kCaster) == 1u);

    // The real production body, the runtime-path shape (applySimulationResults
    // applies the same function over the worker's persistent buffer).
    renderer_.SimBridge().applySimulationOutput(output);

    EXPECT_EQ(dirCallbackCalls_, 1)
        << "entityDirCallback_ was installed but never invoked (the B293 defect)";
    EXPECT_EQ(renderer_.SimBridge().pendingParticleEntityDirections_.size(), 1u);
    auto it = renderer_.SimBridge().pendingParticleEntityDirections_.find(kCaster);
    ASSERT_NE(it, renderer_.SimBridge().pendingParticleEntityDirections_.end());
    EXPECT_FLOAT_EQ(it->second.x, 1.0f);
    EXPECT_FLOAT_EQ(it->second.y, 0.0f);
    EXPECT_FLOAT_EQ(it->second.z, 0.0f);
}

} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
