// T426 Leg C (refactor/T426_overcast_fog_heading_caster_landing.md, board
// B400): a spell landing must survive the SECOND OP_Action the caster -- and
// only the caster -- receives for its own cast.
//
// MEASURED BY T422 (review/T422/README.md, board B400): the same landing drew
// a 139-pixel excursion on an observing client and nothing above the noise
// floor on the caster's own capture, while `SimWorker: Created spell effect
// 'Impact'` was present in the caster's log every time -- so the effect
// reached the worker and was lost downstream.
//
// THE MECHANISM, found by reading the two packets against the removal.
// eqemu sends TWO OP_Action packets for one cast (T395, Mob::SpellOnTarget):
// effect_flag 0 to everyone in range, effect_flag 4 to the target and the
// caster only. irrlicht_bridge.cpp's SpellCastVisualComplete arm therefore
// runs TWICE on the caster and ONCE on an observer, and that arm calls
// SpellVisualFX::removeCastGlow(casterId) unconditionally before the
// isCastAnimation gate.
//
// removeCastGlow() issued ParticleManager::removeSpellEffectsForEntity(),
// whose worker arm removes every effect whose CASTER **or** TARGET is that
// entity. A self-target cast (`#castspell`, and any self-buff) puts the
// caster's own spawn id in both fields, so the second packet's teardown
// destroyed the impact burst and the spell-complete burst the first packet
// had just created. The observer never receives the second packet, so its
// copy lived -- exactly the asymmetry that was measured.
//
// THE SEAM is the real one on both sides: a real SpellVisualFX (EDT_NULL
// Irrlicht device, as tests/test_spell_visual_vocabulary.cpp does) driving a
// real ParticleManager, whose drained commands are fed to a real
// SimulationWorker. Nothing here is a hand-built command.
//
// The readout is SimulationOutput::particleOutput.positionRequestEntities,
// rebuilt every frame from the worker's live particleSpellEffects_ set -- the
// same public readout tests/test_spell_effect_removal.cpp uses.

#include <gtest/gtest.h>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>
#include <irrlicht.h>

#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace {

// T422's own session: Testcaster casting Burst of Flame on itself, so the
// caster spawn id is BOTH the caster and the target of the landing.
constexpr uint16_t kSelfCaster = 288;
constexpr uint32_t kBurstOfFlame = 93;
constexpr uint32_t kCastTimeMs = 2000;

class CasterOwnLandingTest : public ::testing::Test {
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
        // initUnifiedRenderer() needs a GL context; createSpellEffect() refuses
        // to enqueue without the flag it sets. Everything else in the chain is
        // the real thing.
        pm_->Test_SetUnifiedRendererInitializedForSeam(true);
        pm_->setEntityPositionCallback(
            [](uint16_t, glm::vec3& out) -> bool {
                out = glm::vec3(10.0f, 5.0f, 20.0f);
                return true;
            });
        fx_ = std::make_unique<EQ::SpellVisualFX>(device_->getSceneManager(),
                                                  device_->getVideoDriver(),
                                                  std::string());
        fx_->setParticleManager(pm_.get());
        // Every entity resolves, including the hand bones, so createCastGlow
        // takes its normal two-hand path.
        fx_->setEntityPositionCallback(
            [](uint16_t, irr::core::vector3df& out) -> bool {
                out = irr::core::vector3df(10.0f, 5.0f, 20.0f);
                return true;
            });
    }

    void TearDown() override {
        fx_.reset();
        pm_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // Hand whatever the ParticleManager has queued to a real worker frame.
    void PumpFrame() {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.poolSize = 1024;
        pi.commands = pm_->drainCommands();
        worker_.computeOnce(input, output_);
    }

    const std::unordered_set<uint16_t>& LiveEffectEntities() const {
        return output_.particleOutput.positionRequestEntities;
    }

    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::Environment::ParticleManager> pm_;
    std::unique_ptr<EQ::SpellVisualFX> fx_;
    EQT::Graphics::SimulationWorker worker_;
    EQT::Graphics::SimulationOutput output_;
};

// Sanity: the landing the first OP_Action creates is live. If this goes red
// the assertion below means nothing.
TEST_F(CasterOwnLandingTest, TheLandingIsLiveAfterTheCastAnimationPacket) {
    fx_->createImpact(kSelfCaster, kBurstOfFlame);
    PumpFrame();
    ASSERT_TRUE(output_.particleOutput.valid);
    EXPECT_EQ(LiveEffectEntities().count(kSelfCaster), 1u)
        << "createImpact() did not reach the worker at all";
}

// PRE-FIX RED, and the whole of B400.
//
// The caster's SECOND SpellCastVisualComplete (effect_flag 4) runs the same
// bridge arm again: removeCastGlow(casterId), then the isCastAnimation gate
// stops it re-creating anything. Pre-fix that removal was unfiltered, so it
// matched the impact by its TARGET field and the landing vanished.
TEST_F(CasterOwnLandingTest, SelfCastLandingSurvivesTheSuccessRepeatTeardown) {
    // Packet 1, effect_flag 0: glow down (nothing up yet), then the landing.
    fx_->removeCastGlow(kSelfCaster);
    fx_->createImpact(kSelfCaster, kBurstOfFlame);
    PumpFrame();
    ASSERT_EQ(LiveEffectEntities().count(kSelfCaster), 1u);

    // Packet 2, effect_flag 4 -- reaches the caster and the target only.
    fx_->removeCastGlow(kSelfCaster);
    PumpFrame();

    EXPECT_EQ(LiveEffectEntities().count(kSelfCaster), 1u)
        << "the caster's own landing was destroyed by the success repeat's "
           "cast-glow teardown -- this is B400, and an observing client (which "
           "never receives the second packet) keeps its copy";
}

// The teardown must still do its job: a cast glow really is removed.
TEST_F(CasterOwnLandingTest, TheCastGlowItselfIsStillRemoved) {
    fx_->createCastGlow(kSelfCaster, kBurstOfFlame, kCastTimeMs);
    PumpFrame();
    // The glow is created on the hand-bone pseudo-entities when the position
    // callback resolves them, so assert on the population rather than on one id.
    ASSERT_FALSE(LiveEffectEntities().empty())
        << "createCastGlow() did not reach the worker at all";

    fx_->removeCastGlow(kSelfCaster);
    PumpFrame();

    EXPECT_TRUE(LiveEffectEntities().empty())
        << "removeCastGlow() stopped removing the cast glow";
}

// And the other styles on the same entity are untouched by a glow teardown,
// which is the property the style filter adds.
TEST_F(CasterOwnLandingTest, AGlowTeardownRemovesTheGlowAndNothingElse) {
    fx_->createCastGlow(kSelfCaster, kBurstOfFlame, kCastTimeMs);
    fx_->createImpact(kSelfCaster, kBurstOfFlame);
    fx_->createSpellComplete(kSelfCaster, kBurstOfFlame);
    PumpFrame();
    const std::size_t before = LiveEffectEntities().size();
    ASSERT_GE(before, 1u);

    fx_->removeCastGlow(kSelfCaster);
    PumpFrame();

    EXPECT_EQ(LiveEffectEntities().count(kSelfCaster), 1u)
        << "the impact and/or the spell-complete burst on the caster went with "
           "the glow";
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
