// B1106 (issue #426, from B1104 #424 MR !268 refactor/B1104/README.md F2):
// a spell landing's command must not be DROPPED because the frame it rides
// on was slow.
//
// THE MECHANISM. Every ParticleCommand is drained from
// ParticleManager::pendingCommands_ into the NEXT postSimulationInput()
// (renderer_sim_bridge.cpp:447), so a CreateSpellEffect enqueued by the
// bridge's OP_Action arm (irrlicht_bridge.cpp:1137-1139) rides the frame
// AFTER the cast. SimulationWorker::computeParticles() begins with
//
//     if (deltaTime <= 0.0f || deltaTime > 1.0f) { ...; return; }
//
// BEFORE the command loop (sim_particles.cpp:45-49), so on a frame whose
// delta exceeds 1.0 s the drained command is discarded, never deferred.
// The render loop passes its raw wall-clock frame time down
// (irrlicht_renderer.cpp:841, renderer_frame.cpp:151) with no cap.
//
// MEASURED. B1104's fable surfaceless capture
// (`--dump-frames-dir --dump-every 2`, the rig that measured "no FX") ran
// 119 frames whose own mtimes show 56 of 118 inter-frame gaps > 1.0 s
// (max 14 s, median 1.0 s) and the client's log carried 89
// "PERF: Previous frame took 1400-1544 ms (slow!)" lines (one per frame,
// the renderer_frame.cpp:85 warning). The cast packets landed mid-tour, so
// the frame that drained each cast's CreateSpellEffect was, in that rig,
// almost certainly a >1.0 s frame. This unit proves that end to end on lane
// 1 (refactor/B1106/README.md); this test pins the mechanism device-free.
//
// THE SEAM. The same one tests/test_caster_own_landing.cpp (B400) and
// tests/test_spell_impact_visual.cpp (T395) use: a real SpellVisualFX +
// ParticleManager whose drained commands feed a real
// SimulationWorker::computeOnce(). Nothing hand-built.
//
// THE PROTOCOL. The impact is enqueued by createImpact() and rides the NEXT
// frame, exactly like the bridge's. That next frame is the slow one --
// deltaTime 2.0 s, the band the early-return fires on. The frame after it
// (0.016 s) is a normal one. PRE-FIX the 2.0 s tick returns before the
// command loop: the effect never reaches the worker, and the 0.016 s tick
// has an empty queue. The effect never comes alive. POST-FIX the command is
// executed on the slow frame (physics stepped with a clamped delta) and is
// live by the next tick.

#include <gtest/gtest.h>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>
#include <irrlicht.h>

#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "particle_test_seed.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace {

constexpr uint16_t kTarget = 257;   // B1104's lane run: the local spawn
constexpr uint32_t kBurstOfFlame = 93;
constexpr uint32_t kFrostBolt = 54;

class SlowFrameCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(EQT::Graphics::Environment::SpellEffectsConfig::instance().load(
            std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json"));

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        pm_ = std::make_unique<EQT::Graphics::Environment::ParticleManager>(
            device_->getSceneManager(), device_->getVideoDriver());
        // initUnifiedRenderer() needs a GL context; createSpellEffect()
        // refuses to enqueue without the flag it sets. Everything else in
        // the chain is the real thing.
        pm_->Test_SetUnifiedRendererInitializedForSeam(true);
        fx_ = std::make_unique<EQ::SpellVisualFX>(device_->getSceneManager(),
                                                  device_->getVideoDriver(),
                                                  std::string());
        fx_->setParticleManager(pm_.get());
    }

    void TearDown() override {
        fx_.reset();
        pm_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // One worker frame: everything the ParticleManager has queued, at the
    // given frame delta -- the exact shape of postSimulationInput()'s
    // drain at renderer_sim_bridge.cpp:447.
    void PumpFrame(float deltaTime) {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = deltaTime;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 1024;
        pi.commands = pm_->drainCommands();
        drainedCommands_ = pi.commands.size();
        worker_.computeOnce(input, output_);
    }

    // How many commands the previous PumpFrame actually drained from the
    // facade. The T72 gate in SpellVisualFX::createImpact() returns early
    // without enqueuing when the spell's spells.eff record has no Target
    // stage, so a 0 here would make the effect assertions vacuous.
    size_t DrainedCommands() const { return drainedCommands_; }

    // The worker's live spell effects, the same public readout
    // test_caster_own_landing.cpp uses: rebuilt from
    // particleSpellEffects_ every frame.
    const std::unordered_set<uint16_t>& LiveEffectEntities() const {
        return output_.particleOutput.positionRequestEntities;
    }

    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::Environment::ParticleManager> pm_;
    std::unique_ptr<EQ::SpellVisualFX> fx_;
    EQT::Graphics::SimulationWorker worker_;
    EQT::Graphics::SimulationOutput output_;
    size_t drainedCommands_ = 0;
};

// B1106 itself, for the spell B1104 measured: Burst of Flame (93).
//
// The cast animation's landing is enqueued, and the frame that carries it
// is the slow one -- 2.0 s, the surfaceless-dump rig's own frame time.
// PRE-FIX: that tick early-returns before the command loop
// (sim_particles.cpp:45-49) and the command is discarded; the following
// normal frame has an empty queue, so the effect never exists and never
// spawns. POST-FIX: the slow frame still executes the command (physics
// stepped with a clamped delta) and the effect is live by the next tick,
// with particles in the render buffer.
TEST_F(SlowFrameCommandTest, ImpactCommandOnASlowFrameIsNotDropped) {
    fx_->createImpact(kTarget, kBurstOfFlame);

    // The frame the command rides on: > 1.0 s, the band the worker's
    // frame-time guard fires on.
    PumpFrame(2.0f);
    ASSERT_EQ(DrainedCommands(), 1u)
        << "createImpact() enqueued no command (the T72 Target-stage gate "
           "suppressed it) -- the test would be vacuous";
    ASSERT_TRUE(output_.particleOutput.valid);
    EXPECT_EQ(LiveEffectEntities().count(kTarget), 1u)
        << "the CreateSpellEffect drained on the 2.0 s frame was discarded by "
           "the worker's frame-time early-return instead of being executed -- "
           "the effect never came alive";

    // The next frame is a normal one: the effect is live and has spawned.
    PumpFrame(0.016f);
    ASSERT_TRUE(output_.particleOutput.valid);
    EXPECT_EQ(LiveEffectEntities().count(kTarget), 1u)
        << "the effect did not survive into the following normal frame";
    EXPECT_FALSE(output_.particleOutput.renderBuffer.empty())
        << "the impact emitter spawned no particles at all";
}

// The other measured spell (54) rides the same path, so the same command
// must not be dropped for it.
TEST_F(SlowFrameCommandTest, SpellCompleteCommandOnASlowFrameIsNotDropped) {
    fx_->createSpellComplete(kTarget, kFrostBolt);

    PumpFrame(2.0f);
    EXPECT_EQ(LiveEffectEntities().count(kTarget), 1u)
        << "the caster-side completion burst was discarded with the slow frame";

    PumpFrame(0.016f);
    EXPECT_EQ(LiveEffectEntities().count(kTarget), 1u);
    EXPECT_FALSE(output_.particleOutput.renderBuffer.empty());
}

// The guard must still guard: a NON-FINITE or NEGATIVE frame time must not
// be stepped through the physics as-is. What changed is that such a frame
// no longer throws the command queue away either.
TEST_F(SlowFrameCommandTest, AZeroDeltaFrameStillExecutesTheCommand) {
    fx_->createImpact(kTarget, kBurstOfFlame);
    PumpFrame(0.0f);  // the <= 0 arm of the same guard

    // B1120 (#440): the impact style now waits its `triggerDelay` (the
    // oracle's landing onset, ~0.33 s) before emitting, so pump normal frames
    // through it; one 16 ms frame no longer reaches the first spawn.
    for (int i = 0; i < 30; ++i) PumpFrame(0.016f);
    ASSERT_TRUE(output_.particleOutput.valid);
    EXPECT_EQ(LiveEffectEntities().count(kTarget), 1u)
        << "a zero-delta frame discarded the queued command";
    EXPECT_FALSE(output_.particleOutput.renderBuffer.empty());
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
