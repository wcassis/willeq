#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "client/core/memory_ledger.h"
#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/graphics_archive_index.h"
#endif

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string clientPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    std::string p = envPath ? envPath : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

// T500: the LOGICAL bytes of the category's live blocks. `liveBytes` is the
// whole glibc chunk and carries per-block overhead (glibc's size word, its
// 16-byte rounding, the ledger's own 16-byte header) that no logical tally can
// reproduce -- 15.84 B per block, measured. The split is asserted against the
// logical side; the overhead is reported, never buried in the residual.
uint64_t ledgerLive() {
    return eqt::memory::stats(eqt::memory::Category::EntityModels).liveRequestedBytes;
}

}  // namespace

#ifdef EQT_HAS_GRAPHICS

class EntityModelSharingTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    // The playable races that all live in global_chr.s3d, so every one of them
    // resolves against the SAME merged texture map — which is the duplication
    // under test. (1 human, 2 barbarian, 3 erudite, 4 wood elf, 5 high elf,
    // 6 dark elf, 7 half elf, 8 dwarf, 9 troll, 10 ogre, 11 halfling, 12 gnome.)
    const std::vector<uint16_t> races_{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

    // B843: the race the ARCHIVE-INDEX path must serve. Its s3d_file is a
    // NUMBERED global (config/race_models.json) with no global_chr.s3d
    // alternative, so with graphicsArchiveIndex_ installed it resolves only
    // through the index — exactly the way the orangepi preset (which sets
    // deferredAssetLoading) drives every model load on the Pi.
    static constexpr uint16_t kArchiveIndexRace = 42;  // Wolf (WOF) -> global6_chr.s3d

    std::unique_ptr<EQT::Graphics::GraphicsArchiveIndex> archiveIndex_;

    void SetUp() override {
        ASSERT_TRUE(eqt::memory::enabled())
            << "the ledger must be compiled in (-DEQT_MEMORY_LEDGER=ON, the default)";

        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        const std::string eq = clientPath();
        if (!fileExists(eq + "global_chr.s3d")) GTEST_SKIP() << "EQ client tree not at " << eq;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        loader_->setClientPath(eq);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    size_t loadAll() {
        size_t loaded = 0;
        for (uint16_t race : races_)
            if (loader_->ensureModelDataLoaded(race, 0)) ++loaded;
        return loaded;
    }
};

// ---------------------------------------------------------------------------
// Leg A — the split closes.
//
// DELTAS, never absolutes: this binary's own allocations (gtest, the database,
// the Irrlicht device) are charged to whatever scope their thread is in, and
// only the delta across the loads is attributable to the models. The bound is
// the plan's STOP condition 4: the split must close within ~2 %.
// ---------------------------------------------------------------------------
TEST_F(EntityModelSharingTest, TheSplitClosesAgainstTheLedger) {
    const uint64_t ledgerBefore = ledgerLive();
    const size_t totalBefore =
        EQT::Graphics::RaceModelLoader::modelMemTotal(loader_->getMemoryStats());

    ASSERT_GT(loadAll(), 0u) << "no race model loaded — nothing to split";

    const auto stats = loader_->getMemoryStats();
    const uint64_t ledgerAfter = ledgerLive();
    const size_t totalAfter = EQT::Graphics::RaceModelLoader::modelMemTotal(stats);

    ASSERT_GT(ledgerAfter, ledgerBefore) << "the ledger charged nothing for the loads";
    const double ledgerDelta = static_cast<double>(ledgerAfter - ledgerBefore);
    const double statsDelta = static_cast<double>(totalAfter) - static_cast<double>(totalBefore);
    const double residualPct = 100.0 * (ledgerDelta - statsDelta) / ledgerDelta;

    // Printed on every run, pass or fail — the number IS the deliverable.
    std::cout << loader_->modelMemLogLine("test_split") << "\n"
              << "[modelmem-delta] ledger=" << ledgerDelta << " stats=" << statsDelta
              << " residual_pct=" << residualPct << "\n";

    EXPECT_LT(std::abs(residualPct), 2.0)
        << "the split does not close: the loader accounts for " << statsDelta
        << " of the " << ledgerDelta << " bytes the ledger charged";
}

// ---------------------------------------------------------------------------
// B843 — a shared TextureInfo block is reported ONCE, with every claimant
// named, and is NEVER added into a category's total that does not own it.
//
// T500 Leg C [measured on the Pi, qeynos2 + befallen]: with
// graphicsArchiveIndex_ installed (the orangepi preset builds it in
// loadZoneStep5_Assets and installs it via setGraphicsArchiveIndex), the
// `[modelmem]` tally at zone_load_complete read 49,778,752 against
// 22,146,989 live requested bytes in the category — a 125 % over-count —
// while the same split closes to -0.9 % on x86. The difference is the
// texture pixels: the x86 offline path loads every model under
// ensureModelDataLoaded()'s EntityModels scope, so every TextureInfo it
// holds is entity-owned; on the Pi the same loader maps hold pixels that
// were ALLOCATED under other scopes (tex_global 6.95 MB + tex_zone 2.35 MB +
// tex_otherchr 9.37 MB, [measured, T500 Leg C]). The ledger's own total was
// right in both cases — the TALLY claimed bytes it did not own. Will's ruling
// (docs/decisions.md 2026-09-17, memory rulings, item 3): a shared block is
// reported ONCE with every claimant listed (`X: category1, category2, ...`),
// never added into each claimant's total.
//
// The scenario, reproduced offline the way production drives it:
//   1. build + install the archive index (loadZoneStep5_Assets does exactly
//      this for the orangepi preset: buildIndex, then setGraphicsArchiveIndex)
//      — with it set, getMergedTextures() skips its eager numbered-global
//      load and loadModelFromGlobalChr() refuses, so model loads are
//      archive-index-driven, like on the Pi;
//   2. loadNumberedGlobalModels() under a ZoneParse scope: T500 Leg B left
//      this call retaining ONLY the numbered-global TextureInfo pixels
//      (the characters go out of scope), so numberedGlobalTextures_ holds
//      pixels the EntityModels scope never allocated — the Pi's foreign-scope
//      tex_* maps, same shape;
//   3. ensureModelDataLoaded() for a numbered-global race (Wolf, 42,
//      global6_chr.s3d per config/race_models.json): its own allocations
//      charge the EntityModels scope (loadZoneStep8a_BaseModelPrep's scope),
//      and its texture map holds shared views of the step-2 pixels.
//
// RED at the pre-fix tip [measured this run]: the tally's entity_models
// total (15,261,253, incl. the 9,781,900 B tex_numbered the ZoneParse scope
// allocated) exceeded the ledger's live requested (5,193,855) by 193.83 %.
// GREEN: those pixels are left out of modelMemTotal() and reported ONCE in
// sharedTextureBytes with the owner and every claimant named.
// ---------------------------------------------------------------------------
TEST_F(EntityModelSharingTest, B843SharedPixelsAreNotClaimedByEntityModels) {
    const uint64_t ledgerBefore = ledgerLive();
    const size_t totalBefore =
        EQT::Graphics::RaceModelLoader::modelMemTotal(loader_->getMemoryStats());

    // Step 1: the archive index, built and installed the way
    // loadZoneStep5_Assets does it for the orangepi preset.
    archiveIndex_ = std::make_unique<EQT::Graphics::GraphicsArchiveIndex>();
    ASSERT_TRUE(archiveIndex_->buildIndex(clientPath()))
        << "the archive index must build from the real client tree";
    loader_->setGraphicsArchiveIndex(archiveIndex_.get());

    // Step 1b: the numbered-global characters, parsed under a scope that is
    // NOT EntityModels (the Pi's equivalent: the render thread's ZoneParse
    // scope, loadZoneStep2 — the renderer parses the zone Chr files and the
    // numbered globals they name before the entity-prep thread ever runs).
    // T500 Leg B left this call retaining ONLY the pixels — the characters go
    // out of scope — so numberedGlobalTextures_ holds pixels the EntityModels
    // scope never allocated: the Pi's foreign-scope tex_* maps, same shape.
    {
        eqt::memory::MemoryScope zoneScope(eqt::memory::Category::ZoneParse);
        ASSERT_TRUE(loader_->loadNumberedGlobalModels())
            << "global2-7_chr.s3d must yield texture pixels";
    }
    {
        const auto s1b = loader_->getMemoryStats();
        // Which ledger category owns step 1b's bytes?
        std::cout << "[diag ledger all-categories after 1b]\n";
        for (size_t i = 0; i < eqt::memory::kCategoryCount; ++i) {
            const auto& c = eqt::memory::stats(eqt::memory::Category(i));
            if (c.liveRequestedBytes > 0)
                std::cout << "   " << eqt::memory::kCategoryNames[i]
                          << " live_req=" << c.liveRequestedBytes
                          << " blocks=" << c.liveBlocks << "\n";
        }
        std::cout << "[diag after 1b] shared_blocks=" << s1b.sharedTextureBytes.size()
                  << " numberedHeld=" << s1b.numberedTextureBytesHeld
                  << " ledgerLive=" << ledgerLive()
                  << " ledgerDelta1b=" << (ledgerLive() - ledgerBefore)
                  << " totalDelta1b="
                  << (EQT::Graphics::RaceModelLoader::modelMemTotal(s1b) - totalBefore)
                  << "\n";
        for (const auto& e : s1b.sharedTextureBytes)
            std::cout << "   shared " << e.bytes << " owner=" << e.owner
                      << " claimants=[" << e.claimants << "]\n";
    }

    // Step 2: the entity base — the base races and the player's own model,
    // under the EntityModels scope (loadZoneStep8a_BaseModelPrep). The base
    // races resolve through global_chr.s3d (they are not in
    // config/race_models.json, so the index never claims them); the numbered
    // globals the index does claim were already parsed in step 1b, so their
    // shared pixel views stay owned by ZoneParse.
    // The delta is measured ACROSS THIS STEP ONLY: step 1b's allocations
    // (their map nodes, the shared_ptr control blocks) were charged to
    // ZoneParse and must not land in the EntityModels delta.
    const uint64_t ledgerMid = ledgerLive();
    const auto statsMid = loader_->getMemoryStats();
    const size_t totalMid = EQT::Graphics::RaceModelLoader::modelMemTotal(statsMid);
    {
        eqt::memory::MemoryScope entityScope(eqt::memory::Category::EntityModels);
        ASSERT_GT(loadAll(), 0u) << "the base races must load";
        // B843 bisect: base races only (global_chr, EntityModels) -- how far
        // off the ledger are they, before the archive-index race is added?
        {
            const uint64_t baseLedger = ledgerLive();
            const auto baseStats = loader_->getMemoryStats();
            std::cout << "[diag base-races only] ledgerDelta="
                      << (baseLedger - ledgerMid) << " statsDelta="
                      << (EQT::Graphics::RaceModelLoader::modelMemTotal(baseStats) - totalMid)
                      << " loaderTexMaps=" << baseStats.loaderTextureMapBytes
                      << " texObjects=" << baseStats.textureObjectBytes
                      << " mergedMap=" << baseStats.mergedTextureMapBytes << "\n";
        }
        ASSERT_TRUE(loader_->ensureModelDataLoaded(kArchiveIndexRace, 0))
            << "race " << kArchiveIndexRace << " must load";
    }
    {
        const auto s2 = loader_->getMemoryStats();
        struct T { const char* name; size_t v; };
        const T termsMid[] = {
            {"tex_global", statsMid.globalTextureBytes},
            {"tex_numbered", statsMid.numberedTextureBytes},
            {"tex_armor", statsMid.armorTextureBytes},
            {"tex_zone", statsMid.zoneTextureBytes},
            {"tex_otherchr", statsMid.otherChrTextureBytes},
            {"model_geometry", statsMid.modelGeometryBytes},
            {"model_skeleton", statsMid.modelSkeletonBytes},
            {"char_models", statsMid.characterModelBytes},
            {"merged_tex_map", statsMid.mergedTextureMapBytes},
            {"src_char_global", statsMid.sourceCharacterGlobalBytes},
            {"src_char_numbered", statsMid.sourceCharacterNumberedBytes},
            {"src_char_zone", statsMid.sourceCharacterZoneBytes},
            {"src_char_other", statsMid.sourceCharacterOtherBytes},
            {"src_skel_global", statsMid.sourceSkeletonGlobalBytes},
            {"src_skel_numbered", statsMid.sourceSkeletonNumberedBytes},
            {"src_skel_zone", statsMid.sourceSkeletonZoneBytes},
            {"src_skel_other", statsMid.sourceSkeletonOtherBytes},
            {"material_lists", statsMid.materialListBytes},
            {"tex_objects", statsMid.textureObjectBytes},
            {"loader_tex_maps", statsMid.loaderTextureMapBytes},
            {"skel_containers", statsMid.skeletonContainerBytes},
            {"dup_geometry", statsMid.duplicateGeometryBytes},
        };
        const T termsAfter[] = {
            {"tex_global", s2.globalTextureBytes},
            {"tex_numbered", s2.numberedTextureBytes},
            {"tex_armor", s2.armorTextureBytes},
            {"tex_zone", s2.zoneTextureBytes},
            {"tex_otherchr", s2.otherChrTextureBytes},
            {"model_geometry", s2.modelGeometryBytes},
            {"model_skeleton", s2.modelSkeletonBytes},
            {"char_models", s2.characterModelBytes},
            {"merged_tex_map", s2.mergedTextureMapBytes},
            {"src_char_global", s2.sourceCharacterGlobalBytes},
            {"src_char_numbered", s2.sourceCharacterNumberedBytes},
            {"src_char_zone", s2.sourceCharacterZoneBytes},
            {"src_char_other", s2.sourceCharacterOtherBytes},
            {"src_skel_global", s2.sourceSkeletonGlobalBytes},
            {"src_skel_numbered", s2.sourceSkeletonNumberedBytes},
            {"src_skel_zone", s2.sourceSkeletonZoneBytes},
            {"src_skel_other", s2.sourceSkeletonOtherBytes},
            {"material_lists", s2.materialListBytes},
            {"tex_objects", s2.textureObjectBytes},
            {"loader_tex_maps", s2.loaderTextureMapBytes},
            {"skel_containers", s2.skeletonContainerBytes},
            {"dup_geometry", s2.duplicateGeometryBytes},
        };
        std::cout << "[diag terms mid->after2]\n";
        for (size_t i = 0; i < sizeof(termsMid) / sizeof(termsMid[0]); ++i) {
            const long d =
                static_cast<long>(termsAfter[i].v) - static_cast<long>(termsMid[i].v);
            if (d != 0)
                std::cout << "   " << termsAfter[i].name << " delta=" << d
                          << " after2=" << termsAfter[i].v << "\n";
        }
        std::cout << "[diag after 2] shared_blocks=" << s2.sharedTextureBytes.size()
                  << " numberedHeld=" << s2.numberedTextureBytesHeld
                  << " numberedOwned=" << s2.numberedTextureBytes
                  << " loaderTexMaps=" << s2.loaderTextureMapBytes
                  << " texObjects=" << s2.textureObjectBytes
                  << " mergedMap=" << s2.mergedTextureMapBytes
                  << " ledgerLive=" << ledgerLive()
                  << " ledgerDeltaStep2=" << (ledgerLive() - ledgerMid)
                  << " totalDeltaStep2="
                  << (EQT::Graphics::RaceModelLoader::modelMemTotal(s2) - totalMid)
                  << " total=" << EQT::Graphics::RaceModelLoader::modelMemTotal(s2) << "\n";
        for (const auto& e : s2.sharedTextureBytes)
            std::cout << "   shared " << e.bytes << " owner=" << e.owner
                      << " claimants=[" << e.claimants << "]\n";
    }
    const auto s = loader_->getMemoryStats();
    const uint64_t ledgerAfter = ledgerLive();
    const size_t totalAfter = EQT::Graphics::RaceModelLoader::modelMemTotal(s);

    // (g) invariant: each owned tex_* term is a SUBDIVISION of the raw held
    // total of its map (owned + shared, per map), never more. This is what
    // the (g) fields are for -- a test asserts the split against the bytes
    // actually held rather than re-summing the maps.
    EXPECT_LE(s.numberedTextureBytes, s.numberedTextureBytesHeld)
        << "the owned numbered term (" << s.numberedTextureBytes
        << ") exceeds the numbered map's held total ("
        << s.numberedTextureBytesHeld << "): not a subdivision";
    EXPECT_LE(s.zoneTextureBytes, s.zoneCharacterTexturesHeld)
        << "the owned zone term (" << s.zoneTextureBytes
        << ") exceeds the zone map's held total (" << s.zoneCharacterTexturesHeld
        << "): not a subdivision";

    // The evidence, printed on every run.
    std::cout << loader_->modelMemLogLine("test_b843") << "\n";

    // The defect's subject must be in the picture: a shared block the
    // EntityModels scope does NOT own, named once with its owner and every
    // claimant (Will's ruling, docs/decisions.md 2026-09-17 item 3).
    bool sawForeignOwnedBlock = false;
    uint64_t sharedTotal = 0;
    for (const auto& e : s.sharedTextureBytes) {
        sharedTotal += e.bytes;
        if (e.bytes > 0 && std::string(e.owner) != "entity_models") {
            sawForeignOwnedBlock = true;
        }
    }
    EXPECT_TRUE(sawForeignOwnedBlock)
        << "the ZoneParse-owned numbered-global pixels were not reported as a "
           "shared block: the scenario did not reproduce the Pi condition "
           "(shared_total=" << sharedTotal << ")";

    // The acceptance (AC1): the entity_models total the tally claims must not
    // exceed the ledger's live REQUESTED bytes for Category::EntityModels by
    // more than the existing test's 2 % tolerance -- the SAME comparison the
    // [modelmem] line makes (its `residual_pct`), and the SAME one T500 Leg C
    // measured on the Pi (tally 49,778,752 vs 22,146,989 live requested,
    // +125 %). Pre-fix, on x86, this scenario's tally claimed the whole
    // ZoneParse-owned block map (pixels AND their map nodes / TextureInfo
    // objects) as entity_models: the absolute tally read
    // 31,610,137 against 30,761,219 live requested, +2.76 % [measured, WIP-tip
    // 0862527c]. Post-fix the block's pixels AND its storage sit in
    // sharedTextureBytes, out of the total, and the split closes like it does
    // on the Pi after the fix (+1.88 %; see the green log below).
    const double ledgerAbs = static_cast<double>(ledgerAfter);
    const double statsAbs = static_cast<double>(totalAfter);
    const double overPct =
        ledgerAbs ? 100.0 * (statsAbs - ledgerAbs) / ledgerAbs : 0.0;
    std::cout << "[modelmem-b843] tally_abs=" << totalAfter
              << " ledger_abs=" << ledgerAfter
              << " over_pct=" << overPct << " shared_pixels=" << sharedTotal << "\n";
    ASSERT_GT(ledgerAbs, 0.0)
        << "the ledger holds no live entity_models bytes for the archive-index load";
    EXPECT_LT(overPct, 2.0)
        << "the entity_models tally over-claims the archive-index load: it "
           "claims " << totalAfter << " against " << ledgerAfter
           << " live requested ledger bytes (+" << overPct
           << "%) — shared blocks report " << sharedTotal << " B of pixels";
}

// The three new terms are real, not zero-valued placeholders.
TEST_F(EntityModelSharingTest, TheThreeNewTermsAreMeasured) {
    ASSERT_GT(loadAll(), 0u);
    const auto s = loader_->getMemoryStats();

    EXPECT_GT(s.mergedTextureMapBytes, 0u);
    EXPECT_GT(s.mergedTextureEntryCount, 0u);
    EXPECT_GT(s.mergedTextureMapCount, 0u);
    EXPECT_GT(s.sourceCharacterGlobalBytes, 0u) << "global_chr characters are retained";

    // The per-owner split is a SUBDIVISION of characterModelBytes and must sum
    // to it exactly — not approximately.
    EXPECT_EQ(s.sourceCharacterGlobalBytes + s.sourceCharacterNumberedBytes
              + s.sourceCharacterZoneBytes + s.sourceCharacterOtherBytes,
              s.characterModelBytes);
}

// ---------------------------------------------------------------------------
// Leg B — the root fix, RED FIRST, on ledger bytes.
//
// Leg A measured which of the plan's two duplications was larger, and it was
// not close: the per-model merged-texture-map copies are 2,376,336 B, while
// the retained SOURCE data is 2,049,649 + 2,856,632 (characters) +
// 1,772,372 + 12,629,571 (skeletons) + 4,863,104 (the rawParts deep copies).
// Inside that, one term stands out as pure waste rather than a trade:
// `numberedGlobalNoModelCameFromThere` below.
//
// `getMergedTextures()` calls `loadNumberedGlobalModels()` because it wants
// global2-7_chr.s3d's TEXTURES. That call also retained every CharacterModel in
// all six archives -- parts, partsWithTransforms, rawParts, the original
// SkeletonTrack and a full animated skeleton per race -- for the whole life of
// the process, 15,486,203 B of it, for races the client's default (old models)
// path never asks those archives for. Nothing DROPS here: the characters are
// loaded per archive the moment `loadModelFromNumberedGlobal()` actually wants
// one, so the worst case (a race in none of the six) does exactly the work it
// did before, and the common case does none of it. A re-parse is load time,
// which is secondary; a missing model would be a pixel change and is out of
// scope.
// ---------------------------------------------------------------------------
TEST_F(EntityModelSharingTest, NumberedGlobalCharactersAreNotRetainedForTheirTextures) {
    ASSERT_GT(loadAll(), 0u);
    const auto s = loader_->getMemoryStats();

    // The textures ARE still merged -- this must not become a fix that works by
    // not loading the thing at all.
    EXPECT_GT(s.numberedTextureBytes, 0u)
        << "global2-7 textures must still reach the merged map";

    // Every one of these races resolves from global_chr.s3d, so not one model
    // came out of a numbered global. Nothing from those archives should be
    // retained but the textures.
    // MEASURED before the fix: 2,856,632 B of CharacterModel and 12,629,571 B
    // of skeleton, i.e. ALL SIX archives, retained for a texture map. After it,
    // only the archives a model was actually taken from are retained. The
    // bounds below are the post-fix figures with headroom, not round numbers:
    // 1,048,950 and 1,487,013 for this race set.
    EXPECT_LT(s.sourceCharacterNumberedBytes, 1500000u)
        << "numbered-global CharacterModels retained beyond the archives used";
    EXPECT_LT(s.sourceSkeletonNumberedBytes, 2500000u)
        << "numbered-global skeletons retained beyond the archives used";
    // The point of the fix, stated as a ratio rather than a constant: the
    // skeletons retained from the numbered globals are now a small fraction of
    // the global_chr ones, where they used to be seven times larger.
    EXPECT_LT(s.sourceSkeletonNumberedBytes, s.sourceSkeletonGlobalBytes)
        << "global2-7 still costs more skeleton than global_chr itself";
}

// The narrow statement the fix rests on, with nothing else in the picture: the
// TEXTURE path must not pull in a single numbered-global CharacterModel. This
// is the one that fails loudly if someone puts the eager character load back.
TEST_F(EntityModelSharingTest, TheTexturePathAloneRetainsNoNumberedGlobalCharacter) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::EntityModels);
    ASSERT_TRUE(loader_->loadNumberedGlobalModels());
    const auto s = loader_->getMemoryStats();
    EXPECT_GT(s.numberedTextureBytes, 0u) << "the textures are the point of the call";
    EXPECT_EQ(s.sourceCharacterNumberedBytes, 0u);
    EXPECT_EQ(s.sourceSkeletonNumberedBytes, 0u);
}

// The same statement in LEDGER bytes, which is the currency the plan's bound is
// written in: loading K races that all live in one archive must not cost the
// numbered globals' source data on top.
TEST_F(EntityModelSharingTest, EntityModelsLiveStaysUnderTheStatedBound) {
    const uint64_t before = ledgerLive();
    ASSERT_GT(loadAll(), 0u);
    const uint64_t after = ledgerLive();
    const auto s = loader_->getMemoryStats();

    std::cout << loader_->modelMemLogLine("test_bound") << "\n";

    // MEASURED at this tip before the fix: 53,485,626 B of logical live bytes,
    // of which 15,486,203 is numbered-global source data no model came from.
    // The bound is the pre-fix figure minus that, rounded up to a round number
    // so a few hundred KB of ordinary drift does not make it a tripwire.
    constexpr uint64_t kBound = 40ull * 1024 * 1024;
    EXPECT_LT(after - before, kBound)
        << "entity_models logical live bytes for twelve global_chr races";

    // And the split still closes, so the bound is being met by not allocating
    // rather than by the tally losing sight of something.
    const double residualPct =
        100.0 * (static_cast<double>(after - before)
                 - static_cast<double>(EQT::Graphics::RaceModelLoader::modelMemTotal(s)))
        / static_cast<double>(after - before);
    EXPECT_LT(std::abs(residualPct), 2.0) << "split no longer closes: " << residualPct << "%";
}

#endif  // EQT_HAS_GRAPHICS
