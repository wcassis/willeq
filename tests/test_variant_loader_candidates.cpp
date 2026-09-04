/**
 * T340 Leg A (B052) -- a behaviour test for the variant/animation loader,
 * RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation.
 *
 * G07 (refactor/G07_variant_loader.md) stopped on this 815-line method and
 * recorded that NOTHING drives it: zero hits across tests/, and the EQE1
 * crash matrix never reaches it. It is not a pipeline -- it is ONE candidate
 * loop ("for (codeToTry : codesToTry)") over the race row's own gendered code
 * and then the row's fallback_code, searching an ordered source list (the
 * row's s3d_file, the current zone, then global_chr.s3d). Its six "continue"s
 * mean "try the next candidate", NOT "fail": returning false makes the caller
 * (variant_loading.cpp:273-277) write a PERMANENT negative cache entry, so a
 * race whose primary code misses but whose fallback_code hits must LOAD --
 * Will's EQE1 all-or-nothing rule (docs/decisions.md).
 *
 * Each test is named for the exit of that method it drives. Line numbers are
 * as measured on this branch's src/client/graphics/eq/variant_loading_animation.cpp
 * (the G07 Review census, re-measured at fccb2a45, plus the T340 trace block):
 *
 *   PrimaryCodeHitsInTheRowsOwnArchive       -> the depth-5 "return true"
 *   FallbackCodeLoadsAfterThePrimaryMisses   -> the outer "continue" (globals
 *                                               unavailable) then the depth-5
 *                                               "return true"
 *   MatchedCharacterWithNoUsableMeshIsSkipped-> the "variant meshes not found"
 *                                               "continue", then the depth-4
 *                                               "return true" on the global path
 *   AllCandidatesExhaustedCachesNullAndDoesNotRescan -> the trailing
 *                                               "return false" + the caller's
 *                                               negative cache
 *   NoRaceCodeAtAllAbandonsImmediately       -> the "codesToTry empty" "return false"
 *   MatchedGlobalCharacterWithNoUsableMeshIsSkipped -> T340 Leg B: the
 *                                               GLOBAL-path twin of case 3's
 *                                               skip (G07 exit 640), the exit
 *                                               that sits inside one of the two
 *                                               near-duplicate character blocks
 *                                               tryBuildVariantFrom() absorbs
 *
 * Observation seam: RaceModelLoader::variantLoadTrace() (T340, read-only --
 * see race_model_loader.h). variantModels_ alone cannot say WHICH candidate
 * won, nor tell "ran and failed" from "never ran because the caller's negative
 * cache short-circuited"; the trace records both. No fixture is a stub: every
 * one is the real, shipped P99 S3D archive, reached through a temporary client
 * directory that symlinks ONLY the archives a case is allowed to see. That is
 * how the candidate source is controlled -- setClientPath() + loadZoneModels()
 * are the loader's own public seam, so no production change was needed beyond
 * the read-only trace.
 *
 * Offline: opens the shipped data/willeq.db as the race-row source and the
 * real client tree (data/eq_client -> EverQuestP1999); skips if either is
 * absent. EDT_NULL Irrlicht device, no server, no DISPLAY.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#endif

namespace {

namespace fs = std::filesystem;

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

// The archives each case is allowed to see. Everything else the loader looks
// for (the race row's own s3d_file, global_chr.s3d) is simply absent from the
// fixture directory, which is what makes the miss paths real rather than
// simulated.
const char* kRace71Archive  = "commons_chr.s3d";      // race 71 QCM lives here
const char* kGlobalArchive  = "global_chr.s3d";       // HUM, SKE (with SKE_DMSPRITEDEF)
const char* kGlobal4Archive = "global4_chr.s3d";      // IKM/IKF/BAM/IKS/SPE, no ICM
const char* kGlobal6Archive = "global6_chr.s3d";      // SKE_HS_DEF whose only mesh is SKEMESH_DMSPRITEDEF

}  // namespace

#ifdef EQT_HAS_GRAPHICS

using EQT::Graphics::RaceModelLoader;

class VariantLoaderCandidatesTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<RaceModelLoader> loader_;
    fs::path fixtureRoot_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        const std::string eq = clientPath();
        for (const char* a : {kRace71Archive, kGlobalArchive, kGlobal4Archive, kGlobal6Archive}) {
            if (!fileExists(eq + a)) GTEST_SKIP() << "EQ client tree not at " << eq << " (missing " << a << ")";
        }
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());

        fixtureRoot_ = fs::temp_directory_path() /
                       ("t340_variant_loader_" + std::to_string(::getpid()) + "_" +
                        ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::error_code ec;
        fs::remove_all(fixtureRoot_, ec);
        fs::create_directories(fixtureRoot_, ec);
        ASSERT_FALSE(ec) << "cannot create " << fixtureRoot_;
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
        if (!fixtureRoot_.empty()) {
            std::error_code ec;
            fs::remove_all(fixtureRoot_, ec);
        }
    }

    // A client directory holding ONLY the named archives, symlinked to the
    // real shipped ones. Nothing is fabricated: the loader parses the same
    // bytes it parses in the game.
    std::string fixtureClientDir(const std::vector<const char*>& archives) {
        const std::string eq = clientPath();
        for (const char* a : archives) {
            std::error_code ec;
            fs::create_symlink(fs::path(eq + a), fixtureRoot_ / a, ec);
            EXPECT_FALSE(ec) << "symlink " << a << ": " << ec.message();
        }
        return fixtureRoot_.string() + "/";
    }

    // The one public door to the method under test: getAnimatedMeshWithAppearance
    // delegates to getAnimatedMeshForRace when head, body and texture variant are
    // all 0, so a non-zero texture variant is what makes it take the variant path.
    // compositeAllowed=false keeps the P08d composite branch out of the way.
    void driveLoader(uint16_t raceId, uint8_t gender, uint8_t head = 0, uint8_t body = 0) {
        loader_->getAnimatedMeshWithAppearance(raceId, gender, head, body,
                                               /*textureVariant=*/1, /*compositeAllowed=*/false);
    }
};

// Case 1 -- the depth-5 "return true": the race row's own code matches the
// first character that carries it, in the archive the row itself names.
// Race 71 (Qeynos citizen, QCM) -> commons_chr.s3d, which holds QCM_HS_DEF
// with QCM_DMSPRITEDEF and QCMHE00_DMSPRITEDEF. Fixture: the whole real client
// tree, because this case must NOT miss anything.
TEST_F(VariantLoaderCandidatesTest, PrimaryCodeHitsInTheRowsOwnArchive) {
    ASSERT_EQ(EQT::Graphics::getRaceCode(71), "QCM");
    loader_->setClientPath(clientPath());

    driveLoader(71, 0);

    auto trace = loader_->variantLoadTrace();
    EXPECT_EQ(trace.attempts, 1u);
    EXPECT_TRUE(trace.succeeded) << "race 71 did not load from its own row archive";
    EXPECT_EQ(trace.resolvedCode, "QCM");
    EXPECT_EQ(trace.resolvedCharacter, "QCM_HS_DEF");
    EXPECT_EQ(trace.resolvedSource, "zone");

    auto model = loader_->getVariantModelData(71, 0, 0, 0);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->raceName, "QCM_HS_DEF");
    ASSERT_NE(model->combinedGeometry, nullptr);
    EXPECT_GT(model->combinedGeometry->vertices.size(), 0u);
}

// Case 2 -- the outer "continue", then success. THE case a bool list would
// have broken, and the one Will's all-or-nothing rule is about.
// Race 139 (Iksar citizen) is code ICM with fallback_code IKM, and its row
// names cabeast_chr.s3d. The fixture directory holds ONLY global4_chr.s3d
// (IKS, SPE, BAF, BAM, IKF, IKM -- no ICM), loaded as the current zone, so:
//   candidate ICM -> no zone match -> loadGlobalModels() fails, there is no
//                    global_chr.s3d here -> "continue", i.e. NEXT CANDIDATE
//   candidate IKM -> IKM_HS_DEF in the zone source -> loads.
// Rewriting that "continue" as "return false" turns this LOAD into a permanent
// null. See the Review in refactor/T340_variant_loader_behaviour_test.md.
TEST_F(VariantLoaderCandidatesTest, FallbackCodeLoadsAfterThePrimaryMisses) {
    ASSERT_EQ(EQT::Graphics::getRaceCode(139), "ICM");
    ASSERT_EQ(EQT::Graphics::getFallbackRaceCode(139, 0), "IKM");

    loader_->setClientPath(fixtureClientDir({kGlobal4Archive}));
    ASSERT_TRUE(loader_->loadZoneModels("global4"));

    driveLoader(139, 0);

    auto trace = loader_->variantLoadTrace();
    EXPECT_EQ(trace.attempts, 1u);
    ASSERT_TRUE(trace.succeeded)
        << "a race whose primary code misses but whose fallback_code hits must LOAD "
           "(EQE1 all-or-nothing) -- got a null instead";
    EXPECT_EQ(trace.resolvedCode, "IKM") << "the FALLBACK candidate must be the one that won";
    EXPECT_EQ(trace.resolvedCharacter, "IKM_HS_DEF");

    auto model = loader_->getVariantModelData(139, 0, 0, 0);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->raceName, "IKM_HS_DEF");
    ASSERT_NE(model->combinedGeometry, nullptr);
    EXPECT_GT(model->combinedGeometry->vertices.size(), 0u);
}

// Case 3 -- "this character matched the code but carries nothing usable"
// -> "continue" to the next character, then success further along the search.
// global6_chr.s3d contains SKE_HS_DEF whose ONLY mesh is SKEMESH_DMSPRITEDEF:
// it matches code SKE by name but has neither SKE_DMSPRITEDEF nor
// SKEHE00_DMSPRITEDEF, so the variant-mesh selection comes up empty. The
// remaining global6 characters (TPN, TIG, WOL, WOE) do not match SKE, so the
// search falls through to global_chr.s3d, whose SKE_HS_DEF does carry both --
// the depth-4 "return true" on the global path.
TEST_F(VariantLoaderCandidatesTest, MatchedCharacterWithNoUsableMeshIsSkipped) {
    ASSERT_EQ(EQT::Graphics::getRaceCode(60), "SKE");
    ASSERT_TRUE(EQT::Graphics::getFallbackRaceCode(60, 0).empty())
        << "case 3 must turn on the character loop, not on a second candidate";

    loader_->setClientPath(fixtureClientDir({kGlobal6Archive, kGlobalArchive}));
    ASSERT_TRUE(loader_->loadZoneModels("global6"));

    driveLoader(60, 0);

    auto trace = loader_->variantLoadTrace();
    EXPECT_EQ(trace.attempts, 1u);
    ASSERT_TRUE(trace.succeeded) << "the unusable global6 SKE must not abort the search";
    EXPECT_EQ(trace.resolvedCode, "SKE");
    EXPECT_EQ(trace.resolvedCharacter, "SKE_HS_DEF");
    EXPECT_EQ(trace.resolvedSource, "global")
        << "the zone copy of SKE_HS_DEF has no usable mesh; the global one must win";
    EXPECT_EQ(trace.zoneSkips, 1u)
        << "the global6 SKE_HS_DEF must have been matched by name and then skipped";
    EXPECT_EQ(trace.globalSkips, 0u)
        << "the global_chr.s3d SKE_HS_DEF is usable; nothing should be skipped there";

    auto model = loader_->getVariantModelData(60, 0, 0, 0);
    ASSERT_NE(model, nullptr);
    ASSERT_NE(model->combinedGeometry, nullptr);
    EXPECT_GT(model->combinedGeometry->vertices.size(), 0u);
}

// Case 4 -- every candidate exhausted: the trailing "return false", the
// caller's permanent negative cache entry, and no re-scan on the second call.
// Race 35 (Eel) is code EEL with no fallback_code and its row names
// eel_chr.s3d; the fixture holds only global_chr.s3d, which has no EEL.
TEST_F(VariantLoaderCandidatesTest, AllCandidatesExhaustedCachesNullAndDoesNotRescan) {
    ASSERT_EQ(EQT::Graphics::getRaceCode(35), "EEL");
    ASSERT_TRUE(EQT::Graphics::getFallbackRaceCode(35, 0).empty());

    loader_->setClientPath(fixtureClientDir({kGlobalArchive}));

    driveLoader(35, 0);

    auto first = loader_->variantLoadTrace();
    EXPECT_EQ(first.attempts, 1u);
    EXPECT_FALSE(first.succeeded);
    EXPECT_TRUE(first.resolvedCode.empty());
    EXPECT_EQ(loader_->getVariantModelData(35, 0, 0, 0), nullptr);

    // The caller cached a null for this variant key, so the second call must
    // not reach the loader at all -- the attempt count is what proves it.
    driveLoader(35, 0);
    EXPECT_EQ(loader_->variantLoadTrace().attempts, 1u)
        << "the negative cache did not short-circuit the second call";
}

// Case 5 -- the one exit that really is a plain abandon: no race row at all,
// so the candidate list is empty before any archive is touched. Nothing is
// even opened here, which is why the fixture directory stays empty.
TEST_F(VariantLoaderCandidatesTest, NoRaceCodeAtAllAbandonsImmediately) {
    const uint16_t kNoSuchRace = 60000;
    ASSERT_TRUE(EQT::Graphics::getRaceCode(kNoSuchRace).empty());
    ASSERT_TRUE(EQT::Graphics::getFallbackRaceCode(kNoSuchRace, 0).empty());

    loader_->setClientPath(fixtureClientDir({}));

    driveLoader(kNoSuchRace, 0);

    auto trace = loader_->variantLoadTrace();
    EXPECT_EQ(trace.attempts, 1u);
    EXPECT_FALSE(trace.succeeded);
    EXPECT_TRUE(trace.resolvedCode.empty());
    EXPECT_TRUE(trace.resolvedCharacter.empty());
    EXPECT_EQ(loader_->getVariantModelData(kNoSuchRace, 0, 0, 0), nullptr);
}

// Case 6 (T340 Leg B) -- G07 exit 640, the GLOBAL-path twin of case 3's exit
// 280: a character in global_chr.s3d matches the race code by name but carries
// no usable variant meshes, so the character loop skips it and keeps scanning.
// Leg A left this exit untested and flagged it: 640 lives inside the second of
// the two 0.906-similar character blocks that Leg B merges into one
// tryBuildVariantFrom(), so merging with only the zone block under test would
// have been under-netted.
//
// Fixture: the real global6_chr.s3d symlinked INTO the fixture directory UNDER
// THE NAME global_chr.s3d, which is the only name loadGlobalModels() opens. Its
// SKE_HS_DEF has one mesh, SKEMESH_DMSPRITEDEF -- neither SKE_DMSPRITEDEF nor
// SKEHE00_DMSPRITEDEF -- so it is matched and then skipped. No zone is loaded,
// so the global path is the only path. Race 60's own row names global_chr.s3d,
// whose zone name starts with "global", so the JSON-s3d_file source branch is
// skipped by the loader's own guard and does not muddy the case.
//
// The outcome is a null, exactly as in case 4 -- what separates the two is
// trace.globalSkips: case 4 never matches a character at all (0), this case
// matches one and skips it (1). "640 then a success" is NOT reachable with
// shipped data -- see the Review in refactor/T340_variant_loader_behaviour_test.md
// (all 2,609 character models in the 378 shipped *_chr.s3d archives were
// enumerated: no archive holds an unusable-then-usable pair for any race code,
// and no fallback_code race's primary code is ever unusable), which is why the
// skip count, not the return value, is what pins this exit.
TEST_F(VariantLoaderCandidatesTest, MatchedGlobalCharacterWithNoUsableMeshIsSkipped) {
    ASSERT_EQ(EQT::Graphics::getRaceCode(60), "SKE");
    ASSERT_TRUE(EQT::Graphics::getFallbackRaceCode(60, 0).empty())
        << "this case must turn on the global character loop, not on a second candidate";

    // global6_chr.s3d masquerading as global_chr.s3d: the only archive name
    // loadGlobalModels() will open.
    {
        std::error_code ec;
        fs::create_symlink(fs::path(clientPath() + kGlobal6Archive),
                           fixtureRoot_ / kGlobalArchive, ec);
        ASSERT_FALSE(ec) << "symlink global_chr.s3d -> global6: " << ec.message();
    }
    loader_->setClientPath(fixtureRoot_.string() + "/");

    driveLoader(60, 0);

    auto trace = loader_->variantLoadTrace();
    EXPECT_EQ(trace.attempts, 1u);
    EXPECT_FALSE(trace.succeeded) << "there is no usable SKE anywhere in this fixture";
    EXPECT_EQ(trace.zoneSkips, 0u) << "no zone source is loaded in this case";
    EXPECT_EQ(trace.globalSkips, 1u)
        << "the global SKE_HS_DEF must be matched by name and then SKIPPED -- if this "
           "is 0 the global character loop never reached the missing-variant-parts "
           "guard (G07 exit 640)";
    EXPECT_EQ(loader_->getVariantModelData(60, 0, 0, 0), nullptr);
}

#endif  // EQT_HAS_GRAPHICS
