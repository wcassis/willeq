// B1144 (#464): SpellVisualFX draws a spellsnew.eff record slot by slot, by
// the rules the official client's code applies (eqgame.exe 0x495cc3, B1141's
// docs/spellsnew_format.md):
//   - a slot is drawn only if its minLevel <= the caster's level (an NPC or
//     unknown caster counts as level 1);
//   - its anchor (slot +8) picks the actor: 1 the caster, 3 the target;
//   - its attach point (slot +12) picks the point on that actor (LHAND /
//     RHAND / HEAD here; DEFAULT, CHEST and the rest = the entity);
//   - a Target stage with no slot drawn draws no landing (record 326).
// The measured rows (B1131/B1132) still give the shape; B1145 the timing and
// B1149 the population and sprite size come from each slot's spellsnew.edd
// emitter, with its own art and colour words on the client's alpha.
//
// The real SpellVisualFX -> ParticleManager seam, on the shipped client files
// (EQ_CLIENT_PATH, default /mnt/projects/EverQuestP1999); skips without them.
// RED on fb6d6481: every slot was drawn whatever the level, the landing of a
// record with no Target stage was drawn, the anchor was the row's (93's Source
// on the two hands only, 290's landing on the target).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>
#include <irrlicht.h>

#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/spell_visual_fx.h"
#include "client/graphics/environment/spell_population_model.h"
#include "spell_pool.h"
#include "particle_test_seed.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using EQ::SpellVisualFX;
using EQT::Graphics::Environment::SpellEffectsConfig;
namespace Presets = EQT::Graphics::Environment::SpellPresets;

constexpr uint16_t kCaster = 300;
constexpr uint16_t kOther = 301;
const glm::vec3 kCenter{10.0f, 5.0f, 20.0f};
const glm::vec3 kOtherCenter{19.0f, 5.0f, 20.0f};
const glm::vec3 kHand{1.5f, -1.0f, 0.0f};
const glm::vec3 kHeadUp{0.0f, 4.0f, 0.0f};

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

struct Spell {
    uint32_t id;
    int16_t anim;    // spells_us.txt field 145
    int16_t affect;  // field 123
    EQ::SpellSchool school;
};
const Spell kBurstOfFlame{93, 80, 0, EQ::SpellSchool::Evocation};      // record 80 Ignite80ct V2
const Spell kFlashOfLight{201, 112, 22, EQ::SpellSchool::Evocation};   // record 112 (B1131 head row)
const Spell kColorFlux{290, 113, 27, EQ::SpellSchool::Evocation};      // record 113, lands on the caster
const Spell kDefensive{4499, 326, 51, EQ::SpellSchool::Evocation};     // record 326, no Target stage
const Spell kFrostBolt{54, 82, 23, EQ::SpellSchool::Evocation};        // record 82, ElemMastery V2 (the B1122 override shapes are retired, B1150)
const Spell kNoRecord{93, 0, 0, EQ::SpellSchool::Evocation};           // field 145 = 0

class SlotRules : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::ifstream(clientDir() + "/spellsnew.eff").good())
            GTEST_SKIP() << "spellsnew.eff not under " << clientDir();
        const int pool = spell_pool::Shipped(spell_pool::ThisBuildPlatform());
        ASSERT_GT(pool, 0);
        if (!spell_pool::LoadAt(pool)) GTEST_SKIP() << "no config/spell_effects.json";
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        pm_ = std::make_unique<EQT::Graphics::Environment::ParticleManager>(
            device_->getSceneManager(), device_->getVideoDriver());
        pm_->Test_SetUnifiedRendererInitializedForSeam(true);
        pm_->setEntityPositionCallback([](uint16_t id, glm::vec3& out) -> bool {
            const uint16_t real = SpellVisualFX::realEntityId(id);
            const glm::vec3 c = real == kOther ? kOtherCenter : kCenter;
            if (real != kCaster && real != kOther) return false;
            if (SpellVisualFX::isHandBoneId(id)) {
                out = c + (SpellVisualFX::isRightHand(id) ? kHand : glm::vec3(-kHand.x, kHand.y, kHand.z));
                return true;
            }
            out = SpellVisualFX::isHeadBoneId(id) ? c + kHeadUp : c;
            return true;
        });
        fx_ = std::make_unique<SpellVisualFX>(device_->getSceneManager(), device_->getVideoDriver(),
                                              clientDir());
        fx_->setParticleManager(pm_.get());
    }
    void TearDown() override {
        fx_.reset();
        pm_.reset();
        if (device_) device_->drop();
        device_ = nullptr;
    }
    void Note(const Spell& s) {
        EQ::SpellVisualVocabulary v;
        v.known = true;
        v.school = s.school;
        v.spellAffectIndex = s.affect;
        v.spellAnim = s.anim;
        fx_->noteSpellVocabulary(s.id, v);
    }
    // The CreateSpellEffect commands queued since the last call.
    std::vector<EQT::Graphics::ParticleCommandData> Created() {
        std::vector<EQT::Graphics::ParticleCommandData> out;
        for (auto& c : pm_->drainCommands())
            if (c.type == EQT::Graphics::ParticleCommand::CreateSpellEffect) out.push_back(c);
        return out;
    }
    static uint16_t On(const EQT::Graphics::ParticleCommandData& c) {
        return c.casterID != 0 ? c.casterID : c.targetID;
    }
    static std::vector<uint16_t> Points(const std::vector<EQT::Graphics::ParticleCommandData>& v) {
        std::vector<uint16_t> out;
        for (const auto& c : v) out.push_back(On(c));
        return out;
    }
    static uint8_t Tile(const std::string& tex) { return SpellEffectsConfig::instance().getTextureIndex(tex); }

    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::Environment::ParticleManager> pm_;
    std::unique_ptr<SpellVisualFX> fx_;
};

const uint16_t kL = SpellVisualFX::leftHandEntityId(kCaster);
const uint16_t kR = SpellVisualFX::rightHandEntityId(kCaster);
const uint16_t kHead = SpellVisualFX::headEntityId(kCaster);

}  // namespace

TEST(SlotRulesPoints, AttachPointsMapToFablesBoneAnchors) {
    using P = EQ::SpellsNewEffAttach;
    auto at = [](P p) { return SpellVisualFX::attachPointEntityId(kCaster, static_cast<int32_t>(p)); };
    EXPECT_EQ(at(P::LeftHand), kL);
    EXPECT_EQ(at(P::RightHand), kR);
    for (P p : {P::Head, P::LeftEye, P::RightEye, P::Mouth}) EXPECT_EQ(at(p), kHead);
    for (P p : {P::None, P::Default, P::Chest, P::LeftFoot, P::RightFoot, P::Origin, P::FloorUnderChest})
        EXPECT_EQ(at(p), kCaster);
}

// Record 80 (93 Burst of Flame): Source LHAND/RHAND at level 0, DEFAULT at 39
// and 24. A level-1 caster draws the two hands only.
TEST_F(SlotRules, TheSourceIsTheSlotsTheCastersLevelPasses) {
    Note(kBurstOfFlame);
    fx_->createCastGlow(kCaster, 93, 1500, 1);
    EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kL, kR}));
    fx_->createCastGlow(kCaster, 93, 1500, 24);
    EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kL, kR, kCaster}));
    fx_->createCastGlow(kCaster, 93, 1500, 60);
    const auto all = Created();
    EXPECT_EQ(Points(all), (std::vector<uint16_t>{kL, kR, kCaster, kCaster}));
    // Each slot draws its own emitter's art: 276 spelcb x2, 277 spelcb, 278 spelce.
    ASSERT_EQ(all.size(), 4u);
    const char* tex[4] = {"spelcb.png", "spelcb.png", "spelcb.png", "spelce.png"};
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_FALSE(all[i].spellDef.emitters.empty());
        const auto& c = all[i].spellDef.emitters[0].config;
        EXPECT_EQ(c.textureRegionCount, 1) << i;
        EXPECT_EQ(c.textureRegions[0], Tile(tex[i])) << i;
    }
}

// B1149 (#469): each drawn slot emits its emitter's FILE population
// (spellsnew.edd +0x80 burst, then +0x84 per tick at +0x88 ticks/s;
// CParticleSystem::Render 0x10060ea4..0x10060ffd) and draws its sprite size
// (+0xCC/+0x184 x +0x17C/+0x180 x +0x16C, world units). Record 80's Source:
// 276 (2 + 1 x 32/s, 0.875) on each hand, 277 (5 + 5 x 10/s, 1.25), 278
// (33 + 33 x 18/s, 1.0). No share of a measured row any more.
TEST_F(SlotRules, EachSourceSlotEmitsItsFilesPopulationAndSize) {
    Note(kBurstOfFlame);
    fx_->createCastGlow(kCaster, 93, 1500, 60);
    const auto all = Created();
    ASSERT_EQ(all.size(), 4u);
    const int burst[4] = {2, 2, 5, 33};
    const float perTick[4] = {1.0f, 1.0f, 5.0f, 33.0f};
    const float tps[4] = {32.0f, 32.0f, 10.0f, 18.0f};
    const float width[4] = {0.875f, 0.875f, 1.25f, 1.0f};
    for (size_t i = 0; i < all.size(); ++i) {
        int b = 0;
        float t = 0.0f;
        for (const auto& ed : all[i].spellDef.emitters) {
            b += ed.config.burstCount;
            t += ed.config.tickParticles;
            EXPECT_FLOAT_EQ(ed.config.ticksPerSecond, tps[i]) << i;
            EXPECT_FLOAT_EQ(ed.config.spawnRate, 0.0f) << i << ": no rate path";
            const auto& es = ed.config.eddSprite;
            EXPECT_TRUE(es.active) << i;
            EXPECT_FLOAT_EQ(es.widthMin, width[i]) << i;
            EXPECT_FLOAT_EQ(es.widthMax, width[i]) << i;
            EXPECT_FLOAT_EQ(es.heightMin, width[i]) << i;
            EXPECT_FLOAT_EQ(es.scale, 1.0f) << i << ": +0x16C 1, attach scale 1";
        }
        EXPECT_EQ(b, burst[i]) << i;
        EXPECT_NEAR(t, perTick[i], 1e-5f) << i;
    }
    // A level-1 caster draws the two level-0 hand slots, each its full count.
    fx_->createCastGlow(kCaster, 93, 1500, 1);
    const auto one = Created();
    ASSERT_EQ(one.size(), 2u);
    for (const auto& c : one) EXPECT_EQ(c.spellDef.emitters.at(0).config.burstCount, 2);
}

// B1149: a landing slot's two halves (the row's spellComplete + impact
// geometry) are ONE emitter: their counts add to the file's, both draw the
// slot's own texture and sprite size. Record 80 at level 60: 280 (50 + 50 x
// 30/s, 0.5), 281 (1, 16), 282 (2 + 2 x 28/s, 6), 9 (3 + 3 x 10/s, 1.25).
TEST_F(SlotRules, EachLandingSlotIsOneEmitterOfItsFilesPopulation) {
    Note(kBurstOfFlame);
    fx_->createLanding(kCaster, kOther, 93, 60);
    const auto all = Created();
    ASSERT_EQ(all.size(), 8u);
    const int burst[4] = {50, 1, 2, 3};
    const float perTick[4] = {50.0f, 0.0f, 2.0f, 3.0f};
    const float width[4] = {0.5f, 16.0f, 6.0f, 1.25f};
    const char* tex[4] = {"spelcc.png", "spelcm.png", "spelcf.png", "spelcb.png"};
    for (size_t slot = 0; slot < 4; ++slot) {
        int b = 0;
        float t = 0.0f;
        for (size_t h = 0; h < 2; ++h)
            for (const auto& ed : all[2 * slot + h].spellDef.emitters) {
                b += ed.config.burstCount;
                t += ed.config.tickParticles;
                EXPECT_FLOAT_EQ(ed.config.eddSprite.widthMin, width[slot]) << slot;
                EXPECT_EQ(ed.config.textureRegionCount, 1) << slot;
                EXPECT_EQ(ed.config.textureRegions[0], Tile(tex[slot])) << slot;
                EXPECT_TRUE(ed.config.regionColours[0].active) << slot << ": white, on the client's alpha";
                EXPECT_TRUE(ed.config.regionColours[0].clientAlpha) << slot;
            }
        EXPECT_EQ(b, burst[slot]) << slot;
        EXPECT_NEAR(t, perTick[slot], 1e-4f) << slot;
    }
}

// B1149: the simulation emits by the client's ticks -- the burst on the first
// frame, then tickParticles for every 1/ticksPerSecond of emitter age -- and
// sizes the sprite from the file's range, as a world-unit quad.
TEST(SlotRulesSim, TheEmitterEmitsTheBurstThenTicksAndDrawsTheFilesSize) {
    EQT::Graphics::Environment::SpellEffectDef def;
    EQT::Graphics::Environment::SpellEmitterDef ed;
    auto& c = ed.config;
    c.spawnRate = 0.0f;
    c.burstCount = 5;
    c.ticksPerSecond = 10.0f;
    c.tickParticles = 2.5f;
    c.emitterLifetime = 0.5f;
    c.lifetimeMin = c.lifetimeMax = 5.0f;
    c.velocityBase = glm::vec3(0.0f);
    c.eddSprite.active = true;
    c.eddSprite.widthMin = c.eddSprite.widthMax = 1.25f;
    c.eddSprite.heightMin = 2.0f;
    c.eddSprite.heightMax = 3.0f;
    c.eddSprite.scale = 2.0f;
    def.emitters.push_back(ed);
    EQT::Graphics::SimulationWorker worker;
    EQT::Graphics::SimulationOutput output;
    std::vector<EQT::Graphics::Environment::UnifiedParticle> alive;
    for (int f = 0; f < 60; ++f) {  // 1 s at 60 Hz: the 0.5 s window and beyond
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 256;
        if (f == 0) {
            EQT::Graphics::ParticleCommandData cmd;
            cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
            cmd.spellDef = def;
            cmd.preAssignedEffectID = 1;
            pi.commands.push_back(cmd);
        }
        worker.computeOnce(input, output);
        if (output.particleOutput.valid) alive = output.particleOutput.renderBuffer;
    }
    // 5 + ticks 1..4 (age < 0.5) x 2.5 = 15 sprites.
    EXPECT_EQ(alive.size(), 15u);
    for (const auto& p : alive) {
        EXPECT_TRUE(p.hasEddSprite());
        EXPECT_FLOAT_EQ(p.size, 2.5f);
        EXPECT_GE(p.height, 4.0f);
        EXPECT_LE(p.height, 6.0f);
    }
}

// Record 112 (201 Flash of Light, B1131's head row): HEAD at 24 and 0, DEFAULT
// at 39 and 24. Level 1 draws one halo on the head bone.
TEST_F(SlotRules, AHeadSlotFollowsTheHeadBone) {
    Note(kFlashOfLight);
    fx_->createCastGlow(kCaster, 201, 1500, 1);
    EXPECT_EQ(Points(Created()), std::vector<uint16_t>{kHead});
    fx_->createCastGlow(kCaster, 201, 1500, 60);
    EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kHead, kHead, kCaster, kCaster}));
}

// Record 80's landing: four DEFAULT slots on the TARGET, two at level 0.
TEST_F(SlotRules, TheLandingIsTheTargetSlotsTheCastersLevelPasses) {
    Note(kBurstOfFlame);
    EXPECT_EQ(fx_->createLanding(kCaster, kOther, 93, 1), kOther);
    const auto one = Created();
    EXPECT_EQ(one.size(), 4u);  // two slots x (spellComplete + impact)
    for (const auto& c : one) EXPECT_EQ(On(c), kOther);
    EXPECT_EQ(fx_->createLanding(kCaster, kOther, 93, 60), kOther);
    const auto all = Created();
    EXPECT_EQ(all.size(), 8u);
    // Slot 2 (282, level 39) draws spelcf; it is absent at level 1.
    auto hasTile = [&](const std::vector<EQT::Graphics::ParticleCommandData>& v, const char* t) {
        for (const auto& c : v) {
            const auto& cfg = c.spellDef.emitters[0].config;
            for (uint8_t i = 0; i < cfg.textureRegionCount; ++i)
                if (cfg.textureRegions[i] == Tile(t)) return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTile(all, "spelcf.png"));
    EXPECT_FALSE(hasTile(one, "spelcf.png"));
    EXPECT_TRUE(hasTile(one, "spelcc.png"));  // slot 0, emitter 280
    // B1149: both halves of a slot draw that slot's emitter's art (B1144 gave
    // the spellComplete half the Source slots' art): no Source texture here.
    EXPECT_FALSE(hasTile(all, "spelce.png"));
    for (size_t i = 0; i < one.size(); i += 2) {
        const auto& a = one[i].spellDef.emitters[0].config;
        const auto& b = one[i + 1].spellDef.emitters[0].config;
        EXPECT_EQ(a.textureRegionCount, 1) << i;
        EXPECT_EQ(a.textureRegions[0], b.textureRegions[0]) << i;
    }
}

// B1145 (#465): each landing slot carries its emitter's TIMING from
// spellsnew.edd (eqgame passes a Target stage no duration): record 80 at
// level 1 is 280 (a 1 s fixed run, 0.25 s delay, 1.5 s life: 50 + 50 x 30/s)
// and 281 (a one-sprite burst living 1 s), from the landing's measured
// stage latency (the styles' triggerDelay). The level-1 landing therefore ends
// 2.5 s after the stage starts -- where B1144's observer saw it (Act+2.61..2.65)
// -- and level 60 adds 282's 5 s run (its 6.5 s end; B1130's capture 6.67).
TEST_F(SlotRules, EachLandingSlotRunsItsEmittersTiming) {
    Note(kBurstOfFlame);
    const float latency = SpellEffectsConfig::instance().targetStageLatency();
    EXPECT_FLOAT_EQ(latency, 0.33f);
    fx_->createLanding(kCaster, kOther, 93, 1);
    const auto one = Created();
    ASSERT_EQ(one.size(), 4u);
    for (size_t i = 0; i < one.size(); ++i) {
        const auto& ed = one[i].spellDef.emitters.at(0);
        if (i < 2) {  // slot 0, emitter 280
            EXPECT_NEAR(ed.triggerDelay, latency + 0.25f, 1e-5f) << i;
            EXPECT_NEAR(ed.config.emitterLifetime, 0.75f, 1e-5f) << i;
            EXPECT_FLOAT_EQ(ed.config.lifetimeMin, 1.5f) << i;
            EXPECT_FLOAT_EQ(ed.config.lifetimeMax, 1.5f) << i;
            EXPECT_FLOAT_EQ(ed.config.ticksPerSecond, 30.0f) << i << ": the file's 30 ticks/s";
        } else {      // slot 1, emitter 281: the burst alone
            EXPECT_NEAR(ed.triggerDelay, latency, 1e-5f) << i;
            EXPECT_FLOAT_EQ(ed.config.spawnRate, 0.0f) << i;
            EXPECT_FLOAT_EQ(ed.config.tickParticles, 0.0f) << i;
            EXPECT_FLOAT_EQ(ed.config.lifetimeMax, 1.0f) << i;
        }
        EXPECT_FLOAT_EQ(ed.config.sustainRate, 0.0f) << i << ": the B1121 two-phase schedule is gone";
        float end = ed.triggerDelay + std::max(ed.config.emitterLifetime, 0.0f) + ed.config.lifetimeMax;
        if (i >= 2) end = ed.triggerDelay + ed.config.lifetimeMax;  // a burst dies with its sprites
        EXPECT_LE(end, latency + 2.5f + 1e-4f) << i;
    }
    fx_->createLanding(kCaster, kOther, 93, 60);
    float latest = 0.0f;
    for (const auto& c : Created())
        for (const auto& ed : c.spellDef.emitters)
            latest = std::max(latest, ed.triggerDelay + ed.config.emitterLifetime + ed.config.lifetimeMax);
    EXPECT_NEAR(latest, latency + 6.5f, 1e-4f) << "282: 0.25 s delay + 4.75 s window + 1.5 s life";
}

// B1145: a spellsnew.eff Source slot runs the duration eqgame passed at
// OP_BeginCast (0x49b977: the cast time + the cast-animation term for a
// +0x78 >= 0 emitter), from the Source's measured stage latency, with its
// emitter's lifetime -- and OP_Action does not release it.
TEST_F(SlotRules, ASpellsNewSourceRunsItsSlotsDurations) {
    Note(kBurstOfFlame);
    const float latency = SpellEffectsConfig::instance().sourceStageLatency();
    fx_->createCastGlow(kCaster, 93, 1500, 60);
    const auto all = Created();
    ASSERT_EQ(all.size(), 4u);
    const float lifes[4] = {1.0f, 1.0f, 1.0f, 0.25f};  // 276, 276, 277, 278
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& ed = all[i].spellDef.emitters.at(0);
        EXPECT_NEAR(ed.triggerDelay, latency, 1e-5f) << i;
        // The measured term is the emission's end after OP_Action: the run
        // from fable's start (BC + latency) ends at BC + 1.5 + the term.
        EXPECT_NEAR(ed.config.emitterLifetime, 1.5f - latency + EQ::kSpellFxCastAnimationSeconds, 1e-3f) << i;
        EXPECT_NEAR(ed.triggerDelay + ed.config.emitterLifetime, 1.5f + EQ::kSpellFxCastAnimationSeconds, 1e-3f) << i;
        EXPECT_FLOAT_EQ(ed.config.lifetimeMax, lifes[i]) << i;
        EXPECT_NEAR(all[i].duration, 1.5f + EQ::kSpellFxCastAnimationSeconds + lifes[i] + 0.1f, 1e-3f) << i;
    }
    fx_->releaseCastGlow(kCaster, 93, kOther);
    EXPECT_TRUE(pm_->drainCommands().empty()) << "OP_Action released a spellsnew.eff Source";
}

// B1148 (#468): the cast-animation term is the CASTER MODEL's track for the
// spell's CastingAnim (0x47ddd4): 93's CastingAnim 44 names t06 (eqgame's
// code table); a caster whose t06 is 1500 ms (fable's Dark Elf / Human male,
// the ELM donor) runs its Source 1.5 s past the cast, a 4100 ms one (the
// Dark Elf female B1130 cast with) 4.1 s; an unresolved track keeps 4.35 s.
TEST_F(SlotRules, TheSourceRunsTheCastersOwnCastAnimationTrack) {
    Note(kBurstOfFlame);
    const float latency = SpellEffectsConfig::instance().sourceStageLatency();
    std::string asked;
    fx_->setCastAnimationLengthCallback([&](uint16_t id, const char* code) -> int32_t {
        asked = code;
        return id == kCaster ? 1500 : -1;
    });
    fx_->createCastGlow(kCaster, 93, 1500, 60, /*castAnim=*/44);
    EXPECT_EQ(asked, "t06");
    auto all = Created();
    ASSERT_EQ(all.size(), 4u);
    for (const auto& c : all)
        EXPECT_NEAR(c.spellDef.emitters.at(0).config.emitterLifetime, 1.5f - latency + 1.5f, 1e-3f);
    fx_->removeCastGlow(kCaster);
    pm_->drainCommands();
    fx_->setCastAnimationLengthCallback([](uint16_t, const char*) -> int32_t { return -1; });
    fx_->createCastGlow(kCaster, 93, 1500, 60, 44);
    all = Created();
    ASSERT_EQ(all.size(), 4u);
    for (const auto& c : all)
        EXPECT_NEAR(c.spellDef.emitters.at(0).config.emitterLifetime,
                    1.5f - latency + EQ::kSpellFxCastAnimationSeconds, 1e-3f) << "unresolved: the measured 4.35 s";
}

// B1145 (#465): OP_DeleteSpawn deletes the spawn's actor and every spell
// emitter attached to it with its particles (eqgame 0x4581b7 -> 0x4849b2 ->
// 0x48f4a4; EQGraphicsDX9 actor destructor 0x10040150 -> particle manager
// vtbl +0x30 = 0x1005e760 -> 0x1005e550). A corpse's actor is deleted 3.5 s
// later (the corpse-fade list: 0x401351 queues it at the packet, 0x4012bb
// deletes it once 0xdab ms have passed) -- so 93's landing on a target that
// died at OP_Action and was despawned at +1.036 s ends at ~Act+4.54, the
// oracle tour's cliff (4.55..4.71).
TEST_F(SlotRules, ADeletedSpawnTakesItsAttachedEffectsWithIt) {
    auto removals = [&]() {
        std::vector<uint16_t> ids;
        for (auto& c : pm_->drainCommands())
            if (c.type == EQT::Graphics::ParticleCommand::RemoveSpellEffectsEntity) ids.push_back(c.entityID);
        return ids;
    };
    const std::vector<uint16_t> attached = {kOther, SpellVisualFX::rightHandEntityId(kOther),
                                            SpellVisualFX::leftHandEntityId(kOther),
                                            SpellVisualFX::headEntityId(kOther)};
    Note(kBurstOfFlame);
    fx_->createLanding(kCaster, kOther, 93, 60);
    pm_->drainCommands();
    // A live spawn's actor goes at the packet.
    fx_->onSpawnDeleted(kOther, false, 1);
    EXPECT_EQ(removals(), attached);
    // A corpse's actor goes 3.5 s later; until then its landing runs on.
    fx_->onSpawnDeleted(kOther, true, 1);
    EXPECT_TRUE(removals().empty());
    fx_->update(3.4f);
    EXPECT_TRUE(removals().empty()) << "the corpse actor is deleted only past 3499 ms";
    fx_->update(0.15f);
    EXPECT_EQ(removals(), attached);
    fx_->update(5.0f);
    EXPECT_TRUE(removals().empty()) << "a corpse's actor is deleted once";
    // Races 216 and 472 skip the fade list (0x47bfd0): at the packet.
    fx_->onSpawnDeleted(kOther, true, 216);
    EXPECT_EQ(removals(), attached);
}

// Record 113 (290 Color Flux): every Target slot is anchor 1 -- the landing is
// round the CASTER of an other-target cast, from the file (B1132 measured it).
TEST_F(SlotRules, AnAnchorOneLandingIsOnTheCaster) {
    Note(kColorFlux);
    fx_->createLanding(kCaster, kOther, 290, 60);
    const auto v = Created();
    ASSERT_EQ(v.size(), 6u);
    for (const auto& c : v) EXPECT_EQ(On(c), kCaster);
}

// Record 326 (4499 Defensive Discipline): no Target stage = no landing; its
// Source (a 0-ms BeginCast's, B1134) is four level-0 slots, hands and chest.
TEST_F(SlotRules, NoTargetStageMeansNoLanding) {
    Note(kDefensive);
    EXPECT_EQ(fx_->createLanding(kCaster, kCaster, 4499, 60), 0);
    EXPECT_TRUE(Created().empty());
    fx_->createCastGlow(kCaster, 4499, 0, 1);
    EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kCaster, kCaster, kL, kR}));
}

// 54 (Frost Bolt)'s B1122 override gave the Source and the landing their own
// measured geometry, kept on the entity. B1150 (#470) retired those shape
// words (their measurements are the kOverrideShape bands in
// tests/spell_fx_shape_oracle.h), so the slots draw where the file puts them
// (eqgame 0x495cc3, docs/spellsnew_format.md): record 82 (ElemMastery V2)
// draws its Source at the two hands at minLevel 0 (slots 1-2, em292; slots
// 0/3 need levels 39/24, so a level-1 caster gate draws the hands only), and
// a level-60 caster's landing draws the Target's chest (slot 0, em294),
// chest (slot 1, em295, 39), default (slot 2, em296, 24) and head (slot 3,
// em297, 24) -- three slots on the target's entity, one on its head bone.
// The record's words: python3 refactor/B1150/scripts/eff_record.py 82.
TEST_F(SlotRules, ARetiredOverrideSpellDrawsItsFileSlotsOnTheirAttachPoints) {
    Note(kFrostBolt);
    fx_->createCastGlow(kCaster, 54, 1500, 1);  // the level-1 gate: the two minLevel-0 hands
    EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kL, kR}));
    fx_->createLanding(kCaster, kOther, 54, 60);
    // Two commands (the spellComplete and impact halves) per drawn slot:
    // slots 0-2 on the target's entity, slot 3 on its head bone.
    EXPECT_EQ(Points(Created()),
              (std::vector<uint16_t>{kOther, kOther, kOther, kOther, kOther, kOther,
                                     SpellVisualFX::headEntityId(kOther),
                                     SpellVisualFX::headEntityId(kOther)}));
}

// A spell with no spellsnew.eff record keeps the row's own anchor (B1131).
TEST_F(SlotRules, ASpellWithoutARecordKeepsTheRowsAnchor) {
    Note(kNoRecord);
    const auto slots = fx_->stageSlots(93, EQ::kSpellsNewEffSource, 1);
    EXPECT_FALSE(slots.fromFile);
    fx_->createCastGlow(kCaster, 93, 1500, 1);
    for (const auto& c : Created()) EXPECT_NE(On(c), kHead);
}

// ===========================================================================
// B1165 (#484): the Options > Particles > Spell page on the record path
// ===========================================================================

namespace {

// One created command's (or a landing pair's) counts: burst, per-tick and
// tick rate summed over its emitters, and whether the density is already in.
struct Counts {
    int burst = 0;
    float perTick = 0.0f;
    float tps = 0.0f;
    bool atCreate = true;
};
Counts Sum(const std::vector<EQT::Graphics::ParticleCommandData>& v, size_t from, size_t n) {
    Counts c;
    for (size_t i = from; i < from + n && i < v.size(); ++i)
        for (const auto& ed : v[i].spellDef.emitters) {
            c.burst += ed.config.burstCount;
            c.perTick += ed.config.tickParticles;
            c.tps = std::max(c.tps, ed.config.ticksPerSecond);
            c.atCreate = c.atCreate && ed.config.densityAtCreate;
        }
    return c;
}
// What CreateEmitter makes of the density-1 counts at `d` (0x1005a463..0x1005a4f5).
Counts AtDensity(const Counts& one, float d) {
    EQ::SpellFxSlotTiming t;
    t.run = 1.0f;
    t.lifetime = 1.0f;
    t.burst = one.burst;
    t.perTick = static_cast<int32_t>(std::lround(one.perTick));
    t.ticksPerSecond = one.tps;
    t = EQ::SpellFxSlotTimingAtDensity(t, d);
    Counts c;
    c.burst = t.burst;
    c.perTick = static_cast<float>(t.perTick);
    c.tps = t.perTick > 0 ? t.ticksPerSecond : 0.0f;
    return c;
}
EQ::SpellFxOptions Density(float d) {
    EQ::SpellFxOptions o;
    o.density = d;
    return o;
}

}  // namespace

// Each stage's emitters are created with the Spell page's density applied to
// the record's counts as CreateEmitter applies it -- Source (createCastGlow),
// Middle (the missile's launch) and Target (createLanding) of 93's record 80
// at level 60 -- and the worker does not scale them again (densityAtCreate).
TEST_F(SlotRules, TheSpellPagesDensityScalesEveryStageAtCreation) {
    Note(kBurstOfFlame);
    auto stages = [&](float d) {
        fx_->setSpellFxOptions(Density(d));
        std::vector<std::vector<Counts>> out(3);
        fx_->createCastGlow(kCaster, 93, 1500, 60);
        const auto src = Created();
        for (size_t i = 0; i < src.size(); ++i) out[0].push_back(Sum(src, i, 1));
        // A missile kOther -> kCaster (keyed on its own caster, so kCaster's
        // createLanding below is not deferred to it).
        fx_->createCastGlow(kOther, 93, 1500, 60, 0, kCaster);
        Created();  // its Source
        for (int f = 0; f < 40 && fx_->missileCount() > 0; ++f) {
            fx_->update(0.05f);
            for (const auto& c : Created())
                if (c.spellDef.name == "MissileMiddle") out[1].push_back(Sum({c}, 0, 1));
        }
        fx_->clearAllEffects();
        Created();
        fx_->createLanding(kCaster, kOther, 93, 60);
        const auto land = Created();
        for (size_t i = 0; i + 1 < land.size(); i += 2) out[2].push_back(Sum(land, i, 2));
        return out;
    };
    const auto one = stages(1.0f);
    ASSERT_EQ(one[0].size(), 4u) << "record 80's four Source slots at level 60";
    ASSERT_FALSE(one[1].empty()) << "record 80 flies a Middle";
    ASSERT_EQ(one[2].size(), 4u) << "and lands four Target slots";
    EXPECT_EQ(one[0][3].burst, 33);
    EXPECT_EQ(one[2][0].burst, 50);
    const char* const name[3] = {"Source", "Middle", "Target"};
    for (float d : {0.5f, 0.25f}) {
        const auto got = stages(d);
        for (int st = 0; st < 3; ++st) {
            ASSERT_EQ(got[st].size(), one[st].size()) << name[st] << " at " << d;
            for (size_t i = 0; i < got[st].size(); ++i) {
                const Counts want = AtDensity(one[st][i], d);
                EXPECT_EQ(got[st][i].burst, want.burst) << name[st] << " slot " << i << " at " << d;
                EXPECT_NEAR(got[st][i].perTick, want.perTick, 1e-4f) << name[st] << " slot " << i << " at " << d;
                if (want.perTick > 0.0f)
                    EXPECT_FLOAT_EQ(got[st][i].tps, want.tps) << name[st] << " slot " << i << " at " << d;
                EXPECT_TRUE(got[st][i].atCreate) << name[st] << " slot " << i;
            }
        }
    }
    // Spot values (record 80, level 60) at 0.5: the Source's 33 -> ftol(16.5)
    // = 16, its 2s keep min(2, 16) = 2 and their 32 ticks/s halve; the
    // Target's 50 + 50 x 30/s -> 25 + 25 x 30/s.
    const auto half = stages(0.5f);
    EXPECT_EQ(half[0][3].burst, 16);
    EXPECT_EQ(half[0][0].burst, 2);
    EXPECT_FLOAT_EQ(half[0][0].tps, 16.0f);
    EXPECT_EQ(half[2][0].burst, 25);
    EXPECT_NEAR(half[2][0].perTick, 25.0f, 1e-4f);
    EXPECT_FLOAT_EQ(half[2][0].tps, 30.0f);
    // Density 0: FUN_0049b260 draws no record stage at all (0x49b30b).
    const auto off = stages(0.0f);
    for (int st = 0; st < 3; ++st) EXPECT_TRUE(off[st].empty()) << name[st] << " at density 0";
}

// "On For:" per stage: the Source on (caster, caster), the Middle on
// (caster, none), the missile's arrival and createLanding on (caster, target).
TEST_F(SlotRules, TheOnForFilterGatesEachStage) {
    Note(kBurstOfFlame);
    EQ::SpellFxActor other;
    other.present = true;  // a PC, not me, not grouped
    EQ::SpellFxActor me;
    me.present = true;
    me.isSelf = true;
    EQ::SpellFxOptions mine;
    mine.castFilter = 1;  // My spells
    fx_->setSpellFxOptions(mine);
    // Another PC's bolt at me: no Source, no Middle -- the landing on me draws.
    fx_->createCastGlow(kOther, 93, 1500, 60, 0, kCaster, other, me);
    EXPECT_TRUE(Created().empty()) << "My spells: another PC's Source is hidden";
    ASSERT_EQ(fx_->missileCount(), 1u) << "the missile still flies (its landing has its own gate)";
    size_t middle = 0, landing = 0;
    for (int f = 0; f < 60 && fx_->missileCount() > 0; ++f) {
        fx_->update(0.05f);
        for (const auto& c : Created()) (c.spellDef.name == "MissileMiddle" ? middle : landing)++;
    }
    EXPECT_EQ(middle, 0u) << "the Middle is (caster, none): hidden";
    EXPECT_EQ(landing, 8u) << "the arrival on me is shown: record 80's four Target slots x 2 halves";
    // Another PC's spell on another PC: nothing; on me: the landing.
    fx_->createLanding(kCaster, kOther, 93, 60, other, other);
    EXPECT_TRUE(Created().empty());
    fx_->createLanding(kCaster, kOther, 93, 60, other, me);
    EXPECT_EQ(Created().size(), 8u);
    // All spells (the default and every capture's 24): everything draws.
    fx_->setSpellFxOptions(EQ::SpellFxOptions{});
    fx_->createCastGlow(kCaster, 93, 1500, 60, 0, 0, other, other);
    EXPECT_EQ(Created().size(), 4u);
    // ShowSpellEffects 0: no record stage draws (0x49b331).
    EQ::SpellFxOptions hidden;
    hidden.showSpellEffects = false;
    fx_->setSpellFxOptions(hidden);
    fx_->createCastGlow(kCaster, 93, 1500, 60, 0, 0, me, me);
    fx_->createLanding(kCaster, kOther, 93, 60, me, me);
    EXPECT_TRUE(Created().empty()) << "/showspelleffects off draws no spell particles";
}

// The worker leaves a created-at-density emitter alone and still scales a
// non-record one by the family density (B995).
TEST(SlotRulesSim, ACreatedAtDensityEmitterIsNotScaledAgain) {
    auto run = [](bool atCreate) {
        EQT::Graphics::Environment::SpellEffectDef def;
        EQT::Graphics::Environment::SpellEmitterDef ed;
        auto& c = ed.config;
        c.spawnRate = 0.0f;
        c.burstCount = 5;
        c.ticksPerSecond = 10.0f;
        c.tickParticles = 2.5f;
        c.emitterLifetime = 0.5f;
        c.lifetimeMin = c.lifetimeMax = 5.0f;
        c.velocityBase = glm::vec3(0.0f);
        c.densityAtCreate = atCreate;
        def.emitters.push_back(ed);
        EQT::Graphics::SimulationWorker worker;
        EQT::Graphics::SimulationOutput output;
        size_t alive = 0;
        for (int f = 0; f < 60; ++f) {
            EQT::Graphics::SimulationInput input;
            auto& pi = input.particleInput;
            pi.deltaTime = 1.0f / 60.0f;
            pi.unifiedRendererInitialized = true;
            pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
            pi.poolSize = 256;
            for (float& d : pi.density) d = 0.5f;
            if (f == 0) {
                EQT::Graphics::ParticleCommandData cmd;
                cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
                cmd.spellDef = def;
                cmd.preAssignedEffectID = 1;
                pi.commands.push_back(cmd);
            }
            worker.computeOnce(input, output);
            if (output.particleOutput.valid) alive = output.particleOutput.renderBuffer.size();
        }
        return alive;
    };
    EXPECT_EQ(run(true), 15u) << "5 + 4 ticks x 2.5: the density was applied at creation";
    EXPECT_EQ(run(false), 8u) << "round(5 x 0.5) = 3 + 4 ticks x 1.25 = 5";
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
