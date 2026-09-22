/**
 * T364 Leg B (B065) — the `_chr.s3d` cache's LRU accounting.
 *
 * `RaceModelLoader` caps `otherChrCaches_` at `maxChrCacheEntries_` and
 * evicts the least-recently-loaded entry through `chrCacheLruOrder_`. T91
 * made `storeOtherChrCacheLocked()` the only correct way to insert, because
 * a raw `otherChrCaches_[key] = ...` produces an entry that
 *
 *   - never enters `chrCacheLruOrder_`, so it can never be evicted, and
 *   - still counts against `maxChrCacheEntries_`,
 *
 * which pushes the cache permanently over its cap and forces the entries
 * that ARE tracked (the global_chr / global2-7_chr fallback chain) to be
 * evicted and reloaded from disk far more often than the cap allows. B065
 * found the bypass surviving at `loadModelFromCachedChr`
 * (`model_loading_chr.cpp`) — the JSON-specified-S3D path, which is exactly
 * the path a per-zone race row takes.
 *
 * The user-reachable entry point is `ensureModelDataLoaded(race, gender)`:
 * in old-models mode a race whose `race_models.s3d_file` is a plain zone
 * archive (not `global_chr` / `global<N>_chr`) routes straight into
 * `loadModelFromCachedChr`. This test drives that with a cap of 2 over
 * several races living in DIFFERENT archives and asserts the two invariants
 * the bypass breaks.
 *
 * Offline: the shipped `data/willeq.db` plus the real client tree, an
 * EDT_NULL Irrlicht device. No server, no DISPLAY. Skips without either.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
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

}  // namespace

#ifdef EQT_HAS_GRAPHICS

class ChrCacheLruTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    // Races whose race_models row names a plain zone _chr.s3d, each a
    // DIFFERENT archive, so every one of them reaches
    // loadModelFromCachedChr with a cache key not already present.
    // (13 airplane, 15 butcher, 16 northkarana, 17 befallen, 22 beholder.)
    const std::vector<uint16_t> races_{13, 15, 16, 17, 22};
    const std::vector<std::string> archives_{
        "airplane_chr.s3d", "butcher_chr.s3d", "northkarana_chr.s3d",
        "befallen_chr.s3d", "beholder_chr.s3d"};

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        const std::string eq = clientPath();
        for (const auto& a : archives_) {
            if (!fileExists(eq + a)) GTEST_SKIP() << "EQ client tree not at " << eq;
        }

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
};

// INVARIANT 1 — every cached _chr.s3d is registered in the LRU order.
// A raw otherChrCaches_[key] = ... in loadModelFromCachedChr leaves an entry
// that is in the map and not in the list: untracked > 0.
TEST_F(ChrCacheLruTest, EveryCachedChrEntryIsRegisteredInTheLruOrder) {
    ASSERT_TRUE(loader_->isUsingOldModels())
        << "the JSON-specified-S3D path is the old-models branch";
    loader_->setMaxChrCacheEntries(2);

    for (uint16_t race : races_) {
        loader_->ensureModelDataLoaded(race, 0);
    }

    auto acc = loader_->getChrCacheAccountingForTest();
    EXPECT_GT(acc.entries, 0u) << "no _chr.s3d was cached at all -- the "
                                  "loadModelFromCachedChr path was never reached";
    EXPECT_EQ(acc.untracked, 0u)
        << acc.untracked << " of " << acc.entries << " cached _chr.s3d entries never "
        << "entered chrCacheLruOrder_ (LRU-accounting bypass; "
        << "lruOrder=" << acc.lruOrder << ")";
}

// INVARIANT 2 — the cap is actually enforced. Untracked entries cannot be
// evicted, so the map grows past maxChrCacheEntries_ and stays there.
TEST_F(ChrCacheLruTest, CachedChrEntriesAreEvictedOnceTheCapIsExceeded) {
    ASSERT_TRUE(loader_->isUsingOldModels());
    const size_t cap = 2;
    loader_->setMaxChrCacheEntries(cap);

    for (uint16_t race : races_) {
        loader_->ensureModelDataLoaded(race, 0);
    }

    auto acc = loader_->getChrCacheAccountingForTest();
    EXPECT_EQ(acc.maxEntries, cap);
    EXPECT_GT(acc.entries, 0u);
    EXPECT_LE(acc.entries, cap)
        << "otherChrCaches_ holds " << acc.entries << " entries with a cap of " << cap
        << " (" << acc.untracked << " of them unevictable, lruOrder=" << acc.lruOrder << ")";
    EXPECT_EQ(acc.entries, acc.lruOrder)
        << "map and LRU order disagree: " << acc.entries << " vs " << acc.lruOrder;
}

// With the cap OFF (0 = unlimited) nothing is evicted, but the entries are
// still registered -- the store helper's LRU push is gated on the cap, so
// this pins that the unlimited configuration is unaffected by the fix.
TEST_F(ChrCacheLruTest, UnlimitedCapKeepsEveryEntry) {
    ASSERT_TRUE(loader_->isUsingOldModels());
    loader_->setMaxChrCacheEntries(0);

    for (uint16_t race : races_) {
        loader_->ensureModelDataLoaded(race, 0);
    }

    auto acc = loader_->getChrCacheAccountingForTest();
    EXPECT_EQ(acc.maxEntries, 0u);
    // T504 resolver: races resolve from the original client's ordered
    // sources (zone archive, the zone list, GlobalLoad), so several races
    // share one archive and the entry count is the number of DISTINCT
    // archives consulted, not the number of races (it used to be one
    // db-specified archive per race). The invariant is: nothing evicted,
    // every entry registered.
    // (With the cap OFF the store helper does not push to chrCacheLruOrder_ at
    // all, so lruOrder/untracked say nothing here.) Measured under the
    // resolver: these five races consult three distinct archives; the capped
    // case above holds at most two, so an unlimited cap must hold more.
    EXPECT_GT(acc.entries, 2u) << "with no cap every distinct archive should stay cached";
}


// ---------------------------------------------------------------------------
// T366 (B095) — the two OTHER functions that write otherChrCaches_:
//   RaceModelLoader::loadModelFromZoneChr        (the animation-source insert)
//   RaceModelLoader::searchZoneChrFilesForModel  (two negative-cache inserts
//                                                 plus one inline copy of the
//                                                 store helper's push/evict)
//
// Both are private, and both are reached from ensureModelDataLoaded() in
// NEW-models mode. searchZoneChrFilesForModel scans the WHOLE client
// directory for `*_chr.s3d`, so pointing it at the real tree would load 378
// archives; instead the fixture builds a private client tree of symlinks
// holding a controlled handful, plus two archives engineered to take the two
// negative-cache branches:
//
//   zzbroken_chr.s3d  — 64 bytes of junk, S3DLoader::loadZone() fails
//   zznochars_chr.s3d — a real ZONE archive (arena.s3d), loads clean with
//                       zero characters
//
// Neither can ever be evicted while its insert bypasses
// storeOtherChrCacheLocked(), so they pin the cache permanently over its cap.
// ---------------------------------------------------------------------------

class ChrCacheScanTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;
    std::filesystem::path tree_;

    // Archives symlinked into the private tree. beholder holds PUM (race 76,
    // animation source LIM); arena holds LIM (race 50) and is what
    // getRaceS3DFileByCode("LIM") names, i.e. exactly the file
    // loadModelFromZoneChr's animation-source branch loads.
    const std::vector<std::string> archives_{
        "beholder_chr.s3d", "arena_chr.s3d", "akanon_chr.s3d",
        "befallen_chr.s3d", "butcher_chr.s3d"};

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        const std::string eq = clientPath();
        for (const auto& a : archives_) {
            if (!fileExists(eq + a)) GTEST_SKIP() << "EQ client tree not at " << eq;
        }
        if (!fileExists(eq + "arena.s3d")) GTEST_SKIP() << "no arena.s3d at " << eq;

        std::error_code ec;
        tree_ = std::filesystem::temp_directory_path() /
                ("willeq_scantree_" + std::to_string(::getpid()));
        std::filesystem::remove_all(tree_, ec);
        ASSERT_TRUE(std::filesystem::create_directories(tree_, ec)) << ec.message();
        for (const auto& a : archives_) {
            std::filesystem::create_symlink(eq + a, tree_ / a, ec);
            ASSERT_FALSE(ec) << a << ": " << ec.message();
        }
        // Loads clean, holds zero characters (it is a zone-geometry archive).
        std::filesystem::create_symlink(eq + "arena.s3d", tree_ / "zznochars_chr.s3d", ec);
        ASSERT_FALSE(ec) << ec.message();
        // Fails to load at all.
        {
            std::ofstream junk((tree_ / "zzbroken_chr.s3d").string(), std::ios::binary);
            junk << "not an s3d archive, not even close --------------------------";
        }

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        loader_->setClientPath(tree_.string());
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
        // Both target functions sit on the new-models branch of
        // ensureModelDataLoaded().
        loader_->setUseOldModels(false);
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
        std::error_code ec;
        if (!tree_.empty()) std::filesystem::remove_all(tree_, ec);
    }
};

// loadModelFromZoneChr's animation-source branch: race 76 (PUM) is in
// beholder_chr.s3d with too few animations of its own, its animation source
// is LIM, LIM is not in beholder, so the branch loads arena_chr.s3d (LIM's
// own archive) into otherChrCaches_. Pre-fix that insert is a raw
// otherChrCaches_[key] = ..., so the entry never enters chrCacheLruOrder_.
TEST_F(ChrCacheScanTest, ZoneChrAnimationSourceInsertIsRegisteredInTheLruOrder) {
    loader_->setMaxChrCacheEntries(2);
    loader_->setCurrentZone("beholder");

    loader_->ensureModelDataLoaded(76, 0);

    auto acc = loader_->getChrCacheAccountingForTest();
    EXPECT_GT(acc.entries, 0u)
        << "loadModelFromZoneChr's animation-source branch cached nothing -- "
           "the site under test was not reached";
    EXPECT_EQ(acc.untracked, 0u)
        << acc.untracked << " of " << acc.entries << " entries never entered "
        << "chrCacheLruOrder_ (lruOrder=" << acc.lruOrder << ")";
}

// searchZoneChrFilesForModel scans the client directory. Race 330 (FRM) lives
// only in globalpcfroglok_chr.s3d, which is NOT in the private tree, so the
// scan runs to completion and every archive in the tree is cached -- including
// the two that take the negative-cache branches.
TEST_F(ChrCacheScanTest, ScannedChrFilesAreAllRegisteredInTheLruOrder) {
    loader_->setMaxChrCacheEntries(2);

    loader_->ensureModelDataLoaded(330, 0);

    auto acc = loader_->getChrCacheAccountingForTest();
    EXPECT_GT(acc.entries, 0u)
        << "searchZoneChrFilesForModel cached nothing -- the scan was not reached";
    EXPECT_EQ(acc.untracked, 0u)
        << acc.untracked << " of " << acc.entries << " scanned _chr.s3d entries never "
        << "entered chrCacheLruOrder_ (lruOrder=" << acc.lruOrder << ")";
}

// ... and because untracked entries cannot be evicted, the same scan also
// breaks the cap.
TEST_F(ChrCacheScanTest, ScannedChrFilesRespectTheCap) {
    const size_t cap = 2;
    loader_->setMaxChrCacheEntries(cap);

    loader_->ensureModelDataLoaded(330, 0);

    auto acc = loader_->getChrCacheAccountingForTest();
    EXPECT_EQ(acc.maxEntries, cap);
    EXPECT_GT(acc.entries, 0u);
    EXPECT_LE(acc.entries, cap)
        << "otherChrCaches_ holds " << acc.entries << " entries with a cap of " << cap
        << " (" << acc.untracked << " of them unevictable, lruOrder=" << acc.lruOrder << ")";
    EXPECT_EQ(acc.entries, acc.lruOrder)
        << "map and LRU order disagree: " << acc.entries << " vs " << acc.lruOrder;
}

#endif  // EQT_HAS_GRAPHICS
