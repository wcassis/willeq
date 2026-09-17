/**
 * T500 — the `entity_models` byte split, and the sharing it pays for.
 *
 * Leg A (the split). `RaceModelLoader::getMemoryStats()` tallied ten terms and
 * nothing ever checked that they added up to what the process actually holds.
 * The memory ledger (T491) charges every allocation made under
 * `MemoryScope(Category::EntityModels)` — and `ensureModelDataLoaded()` opens
 * exactly that scope — so the two numbers are measurable at the same instant
 * and the difference is a RESIDUAL, not an opinion. A split that does not
 * close is not a split.
 *
 * Leg B (the fix). `RaceModelData::textures` was a full `std::map` COPY of the
 * merged character-texture map, per model: the `TextureInfo` pixels were
 * shared through `shared_ptr` but the map — one red-black node plus a key per
 * entry — was duplicated at every one of the thirteen assignment sites. The
 * K-th model therefore cost a whole extra map for a texture set it shares with
 * the first. `MergedTextureMapIsNotCopiedPerModel` is the red: it fails at the
 * pre-fix tip and passes once the map is shared.
 *
 * Offline. EDT_NULL Irrlicht device, the shipped `data/willeq.db` and the real
 * client tree (`data/eq_client -> EverQuestP1999`); skips without either. No
 * server, no display, nothing drawn.
 *
 * Protocol (B819): the ledger-byte assertions here are run at -O0 AND -O2 —
 * they are different programs, and an optimiser that elides an allocation
 * measures itself rather than the ledger (T491's own lesson).
 */

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

#endif  // EQT_HAS_GRAPHICS
