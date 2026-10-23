// B1144 (#464): SpellVisualFX draws a spellsnew.eff record slot by slot, by
// the rules the official client's code applies (eqgame.exe 0x495cc3, B1141's
// docs/spellsnew_format.md):
//   - a slot is drawn only if its minLevel <= the caster's level (an NPC or
//     unknown caster counts as level 1);
//   - its anchor (slot +8) picks the actor: 1 the caster, 3 the target;
//   - its attach point (slot +12) picks the point on that actor (LHAND /
//     RHAND / HEAD here; DEFAULT, CHEST and the rest = the entity);
//   - a Target stage with no slot drawn draws no landing (record 326).
// The measured rows (B1131/B1132) still give the shape, sizes and timing; each
// slot carries 1/N of its row's population, N = the stage's emitter slots (the
// set the `#level 60` oracle captures drew), and its own emitter's art.
//
// The real SpellVisualFX -> ParticleManager seam, on the shipped client files
// (EQ_CLIENT_PATH, default /mnt/projects/EverQuestP1999); skips without them.
// RED on fb6d6481: every slot was drawn whatever the level, the landing of a
// record with no Target stage was drawn, the anchor was the row's (93's Source
// on the two hands only, 290's landing on the target).

#include <gtest/gtest.h>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

#include <glm/glm.hpp>
#include <irrlicht.h>

#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/spell_visual_fx.h"
#include "spell_pool.h"

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
const Spell kFrostBolt{54, 82, 23, EQ::SpellSchool::Evocation};        // record 82, B1122 override shapes
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

// The shares: at the measured level the slots add up to the row's population.
TEST_F(SlotRules, AtTheMeasuredLevelTheSlotsCarryTheWholeRow) {
    Note(kBurstOfFlame);
    fx_->createCastGlow(kCaster, 93, 1500, 60);
    float sum = 0.0f;
    for (const auto& c : Created())
        for (const auto& e : c.spellDef.emitters) sum += e.config.spawnRate;
    const int row = fx_->sourceRecordOf(93);
    float whole = 0.0f;
    for (const auto& e : Presets::SourceForSpell(glm::vec4(1.0f), 93, row, false, 3.6f, 1.0f).emitters)
        whole += e.config.spawnRate;
    ASSERT_GT(whole, 0.0f);
    EXPECT_NEAR(sum, whole, 1e-3f * whole);
    // ...and a level-1 caster's two slots carry half of it.
    fx_->createCastGlow(kCaster, 93, 1500, 1);
    float one = 0.0f;
    for (const auto& c : Created())
        for (const auto& e : c.spellDef.emitters) one += e.config.spawnRate;
    EXPECT_NEAR(one, 0.5f * whole, 1e-3f * whole);
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
    // The spellComplete half (even entries) takes the Source slots' art at the
    // same level (T441): spelcb only at level 1 -- spelce is 278, a level-24
    // Source slot.
    for (size_t i = 0; i < one.size(); i += 2) {
        const auto& c = one[i].spellDef.emitters[0].config;
        EXPECT_EQ(c.textureRegionCount, 2) << i;  // the two hand slots, both emitter 276
        for (uint8_t k = 0; k < c.textureRegionCount; ++k) EXPECT_EQ(c.textureRegions[k], Tile("spelcb.png")) << i;
    }
    EXPECT_TRUE(hasTile(all, "spelce.png"));
    EXPECT_FALSE(hasTile(one, "spelce.png"));
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

// An override that gives the Source and the landing their own measured
// geometry (B1122: 54 Frost Bolt) keeps it on the entity for every slot; the
// level gate and the per-slot art still apply.
TEST_F(SlotRules, AnOverridesOwnGeometryStaysOnTheEntity) {
    Note(kFrostBolt);
    fx_->createCastGlow(kCaster, 54, 1500, 1);  // record 82: hands at level 0 only
    EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kCaster, kCaster}));
    fx_->createLanding(kCaster, kOther, 54, 60);
    for (const auto& c : Created()) EXPECT_EQ(On(c), kOther);  // slot 3 is HEAD: still the entity
}

// A spell with no spellsnew.eff record keeps the row's own anchor (B1131).
TEST_F(SlotRules, ASpellWithoutARecordKeepsTheRowsAnchor) {
    Note(kNoRecord);
    const auto slots = fx_->stageSlots(93, EQ::kSpellsNewEffSource, 1);
    EXPECT_FALSE(slots.fromFile);
    fx_->createCastGlow(kCaster, 93, 1500, 1);
    for (const auto& c : Created()) EXPECT_NE(On(c), kHead);
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2
