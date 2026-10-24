/**
 * B1148 (#468) -- the official client's corpse-fade list, and the per-caster
 * cast-animation term of the Source duration.
 *
 * THE ORACLE IS THE CLIENT'S CODE (refactor/B1148/README.md sec. 1; the
 * decompiles are review/B1141/ghidra/out/):
 *   - OP_DeleteSpawn 0x4581b7 -> 0x4849b2: a corpse (spawn +0x244 > 1) whose
 *     race is not 216/472 (0x47bfd0) goes on the fade list (0x48f4a4 ->
 *     0x401351, start stamped 0x401100); any other spawn's actor is deleted at
 *     the packet.
 *   - 0x401127, per frame: alpha = clamp(1 - (t - 1300) * 0.0005, 0, 1) for
 *     t > 1300 ms (constants 0x63f4d0 1.0, 0x63f4d4 0.0005, 0x63f4cc 0.0);
 *     0x4012bb deletes the actor when t > 0xdab (3499) ms.
 *   - 0x47ddd4(spell+0x186): the caster actor's track for the CastingAnim id,
 *     named by eqgame's id -> code table at 0x8ffbc8 + id * 20
 *     (review/B1148/anim_code_table.tsv, scripts/anim_code_table.py).
 *
 * The device-free half pins those numbers; the EDT_NULL half drives a real
 * EntityRenderer corpse through beginCorpseFade() and the per-frame walk, and
 * reads the cast tracks off real client models (skips without the db or the
 * client tree). The per-race track lengths are compared with B1145's
 * measured term (review/B1145/cast_anim/cast_anim_fit_decline.tsv: the 20
 * unsaturated B1130 captures, all cast by char 67, a Dark Elf female).
 */

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <string>

#include "client/graphics/corpse_fade.h"
#include "client/spell/spell_fx_timing.h"

using namespace EQT::Graphics;

TEST(CorpseFade, TheModelIsUntouchedForTheFirst1300Ms) {
    for (uint32_t t : {0u, 500u, 1000u, 1001u, 1150u, 1299u, 1300u})
        EXPECT_FLOAT_EQ(CorpseFadeAlpha(t), 1.0f) << t;
}

TEST(CorpseFade, AlphaFallsLinearlyAt0Point0005PerMsFrom1300) {
    EXPECT_NEAR(CorpseFadeAlpha(1301), 0.9995f, 1e-6f);
    EXPECT_NEAR(CorpseFadeAlpha(1800), 0.75f, 1e-6f);
    EXPECT_NEAR(CorpseFadeAlpha(2300), 0.5f, 1e-6f);
    EXPECT_NEAR(CorpseFadeAlpha(3000), 0.15f, 1e-5f);
    EXPECT_FLOAT_EQ(CorpseFadeAlpha(3300), 0.0f) << "transparent from 3300 ms";
    EXPECT_FLOAT_EQ(CorpseFadeAlpha(3499), 0.0f) << "clamped at 0 (0x63f4cc)";
}

TEST(CorpseFade, TheActorIsDeletedOnceMoreThan3499MsHavePassed) {
    EXPECT_FALSE(CorpseFadeExpired(3300));
    EXPECT_FALSE(CorpseFadeExpired(3499)) << "0x4012bb: 0xdab < now - start";
    EXPECT_TRUE(CorpseFadeExpired(3500));
}

TEST(CorpseFade, OnlyACorpseOfARaceOtherThan216Or472GoesOnTheList) {
    EXPECT_TRUE(CorpseUsesFadeList(true, 1));
    EXPECT_TRUE(CorpseUsesFadeList(true, 92)) << "the tour's TrollF";
    EXPECT_FALSE(CorpseUsesFadeList(true, 216)) << "0x47bfd0";
    EXPECT_FALSE(CorpseUsesFadeList(true, 472)) << "0x47bfd0";
    EXPECT_FALSE(CorpseUsesFadeList(false, 1)) << "a live spawn's actor goes at the packet";
}

TEST(CorpseFade, Effect0x5eIsActorEmittersEntry94) {
    EXPECT_EQ(kCorpseDeathEffectEmitter, 94);
}

// eqgame's id -> code table (0x8ffbc8 + id * 20), as anim_code_table.py read
// it from the binary.
TEST(CastAnimationTerm, TheIdNamesTheTrackThroughEqgamesTable) {
    EXPECT_STREQ(EQ::SpellCastAnimationCode(1), "c01");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(12), "d01");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(40), "t02");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(42), "t04");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(43), "t05");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(44), "t06");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(48), "s06");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(71), "p08");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(78), "l10");
    EXPECT_EQ(EQ::SpellCastAnimationCode(0), nullptr);
    EXPECT_EQ(EQ::SpellCastAnimationCode(79), nullptr);
}

TEST(CastAnimationTerm, TheSourceRunsTheCastPlusTheCastersTrack) {
    using EQ::SpellFxCallerDurationMs;
    using EQ::SpellFxStage;
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Source, 1500, 0, 4100), 1500 + 4100);
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Source, 1500, 0, 1700), 1500 + 1700);
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Source, 1500, 0, -1), 1500 + 4350)
        << "a caster whose track does not resolve keeps B1145's measured term";
    EXPECT_EQ(SpellFxCallerDurationMs(SpellFxStage::Target, 1500, 0, 4100), 0);
}

#ifdef EQT_HAS_GRAPHICS

#include <irrlicht.h>
#include "client/willeq_database.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/spell_fx_bone_anchor.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"

namespace {

class CorpseFadeEntity : public ::testing::Test {
protected:
    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EntityRenderer> ents_;
    std::string eqClientPath_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') eqClientPath_ += '/';
        if (!fileExists(eqClientPath_ + "global_chr.s3d")) GTEST_SKIP() << "no client data at " << eqClientPath_;
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        ents_ = std::make_unique<EntityRenderer>(device_->getSceneManager(), device_->getVideoDriver(),
                                                 device_->getFileSystem());
        ents_->setClientPath(eqClientPath_);
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        ents_->setConstrainedTextureCache(textureCache_.get());
        if (!ents_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
    }

    void TearDown() override {
        ents_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    const EntityVisual* build(uint16_t spawn, uint16_t race, uint8_t gender, bool corpse, bool npc = true) {
        if (!ents_->registerEntity(spawn, race, corpse ? "Fadeoracle's corpse" : "Fadeoracle", 0.0f, 0.0f, 0.0f,
                                   0.0f, /*isPlayer=*/false, gender, EntityAppearance(), npc, corpse,
                                   /*serverSize=*/6.0f, /*level=*/10))
            return nullptr;
        if (!ents_->buildEntityMesh(spawn)) return nullptr;
        const auto it = ents_->getEntities().find(spawn);
        return it == ents_->getEntities().end() ? nullptr : &it->second;
    }

    const EntityVisual* find(uint16_t spawn) const {
        const auto it = ents_->getEntities().find(spawn);
        return it == ents_->getEntities().end() ? nullptr : &it->second;
    }

    // Every vertex alpha byte of the node's per-instance mesh: {min, max}.
    static std::pair<int, int> vertexAlphaRange(EQAnimatedMeshSceneNode* node) {
        int lo = 256, hi = -1;
        irr::scene::SMesh* mesh = node->getInstanceMesh();
        for (irr::u32 b = 0; mesh && b < mesh->getMeshBufferCount(); ++b) {
            auto* mb = mesh->getMeshBuffer(b);
            if (mb->getVertexType() != irr::video::EVT_STANDARD) continue;
            auto* v = static_cast<irr::video::S3DVertex*>(mb->getVertices());
            for (irr::u32 k = 0; k < mb->getVertexCount(); ++k) {
                lo = std::min<int>(lo, v[k].Color.getAlpha());
                hi = std::max<int>(hi, v[k].Color.getAlpha());
            }
        }
        return {lo, hi};
    }

    // Steps the renderer's per-frame walk to `seconds` after the packet in
    // 50 ms frames from `from`.
    void walk(float from, float seconds) {
        for (float t = from; t + 1e-4f < seconds; t += 0.05f) ents_->updateMainThreadEntityState(0.05f);
    }
};

constexpr uint16_t kHumanMale = 1;

TEST_F(CorpseFadeEntity, ACorpseFadesFrom1300MsAndIsRemovedPast3499Ms) {
    constexpr uint16_t kSpawn = 4301;
    const auto* v = build(kSpawn, kHumanMale, 0, /*corpse=*/true);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(v->animatedNode, nullptr);
    ASSERT_TRUE(ents_->beginCorpseFade(kSpawn)) << "OP_DeleteSpawn of a race-1 corpse: the fade list";
    EXPECT_TRUE(ents_->beginCorpseFade(kSpawn)) << "idempotent (CorpseDecayStarted + EntityDespawned)";
    EXPECT_FALSE(ents_->nameTagKindVisible(*find(kSpawn))) << "the spawn is gone: no name";

    walk(0.0f, 1.25f);
    ASSERT_NE(find(kSpawn), nullptr);
    EXPECT_EQ(vertexAlphaRange(find(kSpawn)->animatedNode), std::make_pair(255, 255)) << "untouched before 1.3 s";

    walk(1.25f, 2.30f);
    ASSERT_NE(find(kSpawn), nullptr);
    const auto mid = vertexAlphaRange(find(kSpawn)->animatedNode);
    EXPECT_EQ(mid.first, mid.second);
    EXPECT_NEAR(mid.first, 128, 4) << "alpha 0.5 at 2.3 s";
    auto* node = find(kSpawn)->animatedNode;
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i)
        EXPECT_EQ(node->getMaterial(i).MaterialType, irr::video::EMT_TRANSPARENT_VERTEX_ALPHA)
            << "blended (no shader twins on EDT_NULL: the built-in path)";

    walk(2.30f, 3.45f);
    ASSERT_NE(find(kSpawn), nullptr) << "the actor is still on the list at 3.45 s";
    EXPECT_EQ(vertexAlphaRange(find(kSpawn)->animatedNode).second, 0) << "transparent from 3.3 s";

    walk(3.45f, 3.55f);
    EXPECT_EQ(find(kSpawn), nullptr) << "deleted on the first walk past 3499 ms";
}

TEST_F(CorpseFadeEntity, ALiveSpawnAndARace216CorpseAreNotListed) {
    const auto* live = build(4302, kHumanMale, 0, /*corpse=*/false);
    ASSERT_NE(live, nullptr);
    EXPECT_FALSE(ents_->beginCorpseFade(4302)) << "a live spawn's model goes at the packet";
    // Race 216 needs no model here: the predicate decides before the node.
    EXPECT_FALSE(CorpseUsesFadeList(true, 216));
}

TEST_F(CorpseFadeEntity, ANewSpawnOfTheSameIdEndsTheOldActorsFade) {
    constexpr uint16_t kSpawn = 4303;
    ASSERT_NE(build(kSpawn, kHumanMale, 0, /*corpse=*/true), nullptr);
    ASSERT_TRUE(ents_->beginCorpseFade(kSpawn));
    const auto* fresh = build(kSpawn, kHumanMale, 0, /*corpse=*/false);
    ASSERT_NE(fresh, nullptr) << "the id is free for the new spawn";
    EXPECT_FALSE(fresh->isFading);
}

// The tracks 0x47ddd4 reads, off fable's own model data (donor tracks
// included), per race. Only the Dark Elf female has an oracle measurement:
// every B1130 capture was cast by char 67 (race 6), and B1145's decline fit
// gives per CastingAnim 42 (t04) 4.077 s (n 2), 43 (t05) 4.349 s (n 3), 44
// (t06) 4.287 s (n 14); all 20 median 4.35 s, IQR 4.09..4.75.
TEST_F(CorpseFadeEntity, TheDarkElfFemaleCastTracksMatchTheMeasuredTerm) {
    constexpr uint16_t kDarkElf = 6;
    const auto* v = build(4304, kDarkElf, /*female*/ 1, /*corpse=*/false);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(v->animatedNode, nullptr);
    struct Row { int castAnim; float measured; };
    for (const Row r : {Row{42, 4.077f}, Row{43, 4.349f}, Row{44, 4.287f}}) {
        const int32_t ms = CastAnimationTrackMs(v->animatedNode, EQ::SpellCastAnimationCode(r.castAnim));
        ASSERT_GT(ms, 0) << r.castAnim;
        EXPECT_NEAR(ms / 1000.0f, r.measured, 0.30f)
            << "CastingAnim " << r.castAnim << ": track " << ms << " ms vs the oracle's " << r.measured << " s";
    }
}

// Other races: the model data differs per race (what makes the term per
// caster); no oracle capture exists for them yet (FOLLOW-UP in the README).
TEST_F(CorpseFadeEntity, TheCastTrackIsPerRace) {
    const auto* daf = build(4305, 6, 1, false);
    const auto* hum = build(4306, kHumanMale, 0, false);
    ASSERT_NE(daf, nullptr);
    ASSERT_NE(hum, nullptr);
    const int32_t a = CastAnimationTrackMs(daf->animatedNode, "t05");
    const int32_t b = CastAnimationTrackMs(hum->animatedNode, "t05");
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);
    EXPECT_NE(a, b) << "Dark Elf female (ELF donor) and Human male (ELM donor) t05";
    EXPECT_EQ(CastAnimationTrackMs(daf->animatedNode, "zzz"), -1);
    EXPECT_EQ(CastAnimationTrackMs(nullptr, "t05"), -1);
}

// Evidence (review/B1148/cast_tracks.tsv): with WILLEQ_CAST_TRACK_TSV set,
// the t02/t04/t05/t06 tracks of the playable races' models, both genders.
TEST_F(CorpseFadeEntity, WritesTheCastTrackTable) {
    const char* out = std::getenv("WILLEQ_CAST_TRACK_TSV");
    if (!out) GTEST_SKIP() << "WILLEQ_CAST_TRACK_TSV not set";
    std::ofstream tsv(out);
    tsv << "race\tgender\tt02_ms\tt04_ms\tt05_ms\tt06_ms\n";
    uint16_t spawn = 4400;
    for (uint16_t race : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 128, 130, 330}) {
        for (uint8_t gender : {0, 1}) {
            const auto* v = build(spawn++, race, gender, false);
            if (!v || !v->animatedNode) continue;
            tsv << race << '\t' << int(gender);
            for (const char* c : {"t02", "t04", "t05", "t06"}) tsv << '\t' << CastAnimationTrackMs(v->animatedNode, c);
            tsv << '\n';
        }
    }
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS
