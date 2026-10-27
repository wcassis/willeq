// T397 Leg B (refactor/T397_spell_effects_config_path.md, board B220):
// removing a spell effect by entity.
//
// B220 was filed as hygiene -- ParticleManager::removeSpellEffect(id) writes
// cmd.preAssignedEffectID while the worker's arm matches cmd.effectID, so it
// can never hit, and it has zero callers. That reading is correct, and T397
// deleted it: no caller anywhere keeps the id createSpellEffect() returns, so
// by-id removal has no consumer to wire.
//
// TRACING IT TURNED UP THE LIVE ONE, WHICH IS WHAT THIS FILE PINS.
// ParticleManager::removeSpellEffectsForEntity() -- the removal every real
// caller uses (SpellVisualFX::removeCastGlow; B1139 deleted the zero-caller
// removeBuffAura / removeAllForEntity) -- had the SAME wrong-field bug, and there it was not
// harmless. It wrote cmd.targetID, leaving cmd.entityID at its default 0,
// while SimulationWorker's RemoveSpellEffectsEntity arm matches
//
//     casterEntityID == cmd.entityID || targetEntityID == cmd.entityID
//
// Every cast glow is created as createSpellEffect(def, caster_id, 0) and
// every impact as createSpellEffect(def, 0, target_id), so one of the two
// fields is ALWAYS 0. A cmd.entityID of 0 therefore matched every spell
// effect alive in the zone. The call behaved as clearAllSpellEffects() while
// removing nothing it had been asked to remove -- and
// SpellVisualFX::createCastGlow() issues three of them before every single
// cast, so any player beginning to cast wiped every other player's impacts
// and glows.
//
// THE SEAM. SimulationOutput::particleOutput.positionRequestEntities is
// rebuilt every frame from the worker's live particleSpellEffects_ set (the
// caster and target of each surviving effect), so it is a direct, public
// readout of which effects the worker still holds. The removal command is not
// hand-built: it comes out of the real ParticleManager via drainCommands(),
// which is the half that was wrong.

#include <gtest/gtest.h>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>

#include "client/graphics/simulation_worker.h"
#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "particle_test_seed.h"

#include <string>
#include <unordered_set>

namespace {

namespace SpellPresets = EQT::Graphics::Environment::SpellPresets;

constexpr uint16_t kImpactTarget = 38;   // impact: casterID 0, targetID 38
constexpr uint16_t kGlowCaster   = 99;   // cast glow: casterID 99, targetID 0

class SpellEffectRemovalTest : public ::testing::Test {
protected:
    void SetUp() override {
        // The presets must be the shipped ones; T397 Leg A is why this one
        // line is enough from any working directory.
        ASSERT_TRUE(EQT::Graphics::Environment::SpellEffectsConfig::instance().load(
            std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json"));
    }

    // The impact command SpellVisualFX::createImpact() produces:
    // createSpellEffect(def, 0, target_id) -- no caster.
    static EQT::Graphics::ParticleCommandData Impact(uint16_t target, uint32_t id) {
        EQT::Graphics::ParticleCommandData cmd;
        cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
        cmd.spellDef = SpellPresets::Impact(glm::vec4(1.0f, 0.47f, 0.2f, 1.0f), 93);
        cmd.casterID = 0;
        cmd.targetID = target;
        cmd.preAssignedEffectID = id;
        return cmd;
    }

    // The cast-glow command SpellVisualFX::createCastGlow() produces:
    // createSpellEffect(def, caster_id, 0, durSec, spray) -- no target.
    static EQT::Graphics::ParticleCommandData CastGlow(uint16_t caster, uint32_t id) {
        EQT::Graphics::ParticleCommandData cmd;
        cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
        cmd.spellDef = SpellPresets::CastGlow(glm::vec4(0.59f, 0.71f, 1.0f, 1.0f), 93);
        cmd.casterID = caster;
        cmd.targetID = 0;
        cmd.duration = 0.0f;
        cmd.preAssignedEffectID = id;
        return cmd;
    }

    void RunFrame(std::vector<EQT::Graphics::ParticleCommandData> cmds) {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 1024;
        pi.commands = std::move(cmds);
        worker_.computeOnce(input, output_);
    }

    const std::unordered_set<uint16_t>& LiveEffectEntities() const {
        return output_.particleOutput.positionRequestEntities;
    }

    EQT::Graphics::SimulationWorker worker_;
    EQT::Graphics::SimulationOutput output_;
};

// Sanity: both effects exist and the worker reports both entities. If this
// ever goes red the removal assertions below mean nothing.
TEST_F(SpellEffectRemovalTest, BothEffectsAreLiveBeforeAnyRemoval) {
    RunFrame({Impact(kImpactTarget, 1), CastGlow(kGlowCaster, 2)});

    ASSERT_TRUE(output_.particleOutput.valid);
    EXPECT_EQ(LiveEffectEntities().count(kImpactTarget), 1u);
    EXPECT_EQ(LiveEffectEntities().count(kGlowCaster), 1u);
}

// PRE-FIX RED, and the whole point of the unit.
//
// The removal command is drained out of a real ParticleManager, so the field
// the manager writes is under test. Pre-fix it wrote targetID and left
// entityID at 0; 0 matches the impact (casterEntityID 0) as well as the glow,
// so BOTH effects vanish and LiveEffectEntities() is empty.
TEST_F(SpellEffectRemovalTest, RemovingOneEntityLeavesEveryOtherEffectAlone) {
    RunFrame({Impact(kImpactTarget, 1), CastGlow(kGlowCaster, 2)});
    ASSERT_EQ(LiveEffectEntities().size(), 2u);

    EQT::Graphics::Environment::ParticleManager mgr(nullptr, nullptr);
    mgr.removeSpellEffectsForEntity(kGlowCaster);
    auto cmds = mgr.drainCommands();
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].entityID, kGlowCaster)
        << "the manager did not put the entity id in the field the worker matches on";

    RunFrame(std::move(cmds));

    EXPECT_EQ(LiveEffectEntities().count(kGlowCaster), 0u)
        << "the effect that was asked for is still there";
    EXPECT_EQ(LiveEffectEntities().count(kImpactTarget), 1u)
        << "an unrelated entity's effect was removed too -- the removal matched "
           "entityID 0, which every glow and every impact carries in one of its "
           "two entity fields";
}

// The mirror case: removing an entity that owns nothing must remove nothing.
// Pre-fix this wiped the board as well, because the id never reached the
// command at all.
TEST_F(SpellEffectRemovalTest, RemovingAnEntityWithNoEffectsRemovesNothing) {
    RunFrame({Impact(kImpactTarget, 1), CastGlow(kGlowCaster, 2)});
    ASSERT_EQ(LiveEffectEntities().size(), 2u);

    EQT::Graphics::Environment::ParticleManager mgr(nullptr, nullptr);
    mgr.removeSpellEffectsForEntity(4242);
    RunFrame(mgr.drainCommands());

    EXPECT_EQ(LiveEffectEntities().size(), 2u)
        << "removing an uninvolved entity destroyed live effects";
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
