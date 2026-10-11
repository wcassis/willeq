/**
 * B733 (issue #70): the race model builder selects parts by NAME
 * ("{CODE}_DMSPRITEDEF" / "{CODE}HE00_DMSPRITEDEF", model_loading_chr.cpp),
 * so a character whose skeleton's 0x200 mesh list references a part under
 * ANOTHER name loses it.
 *
 * Proof subject: race 146 (Spectral Sarnak), code SSN, archive
 * cabeast_chr.s3d. In that archive the skeleton SSN_HS_DEF references, via
 * its 0x200 mesh list (T476's SkeletonTrack::meshRefs), exactly two meshes:
 *   SSN_DMSPRITEDEF    body, 3359 verts  -- bone-attached, the loader stores it
 *   SNNHE00_DMSPRITEDEF  head, 974 verts  -- ONLY in the 0x200 list (the "SNN"
 *                       spelling differs from the race code "SSN")
 * The loader's flattenSkeleton walks the BONES, so the head never reaches the
 * CharacterModel, and the name-based selection (SSN / SSNHE00) cannot select
 * what was never stored. Result: the built model holds 3359 verts, not the
 * 4333 (3359 + 974) the skeleton references.
 *
 * The expected total is DERIVED here from the archive itself: the same
 * PfsArchive parse the production loader runs (loadCharacters reuses
 * S3DLoader::mainWldName_), the same no-filter options it passes, the
 * skeleton track named by the race's stored character, the union of the
 * bone-attached meshes and the 0x200 meshRefs, each resolved through the
 * loader's own modelRefs table. Nothing is hand-typed.
 *
 * Offline: opens the shipped data/willeq.db as the race-row source and the
 * real client tree (data/eq_client -> EverQuestP1999); skips if either is
 * absent. EDT_NULL Irrlicht device, no server, no DISPLAY.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/race_model_loader.h"
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

class B733Race146Test : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
    }
    void TearDown() override {
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }
};

}  // namespace

#ifdef EQT_HAS_GRAPHICS

class B733Race146LoaderTest : public B733Race146Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;
    std::string eq_ = clientPath();

    void SetUp() override {
        B733Race146Test::SetUp();
        if (IsSkipped()) return;
        if (!fileExists(eq_ + "cabeast_chr.s3d")) {
            GTEST_SKIP() << "EQ client tree not at " << eq_;
        }
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        loader_->setClientPath(eq_);
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
        B733Race146Test::TearDown();
    }

    // Every mesh the race's skeleton references -- the bone-attached ones
    // plus the 0x200 meshRefs (T476) -- resolved through the same WldLoader
    // state the production loader builds, so the expectation is data-derived,
    // never hand-typed. Returns the total vertex count (0 on any failure, with
    // a reason). No assertions here: this is a non-void helper, and gtest's
    // ASSERT_* expands to `return`, which is ill-formed in a returning method.
    struct Expected {
        bool ok = false;
        std::string reason;
        size_t total = 0;
        std::string listing;
    };
    Expected probeSkeleton() {
        Expected e;
        EQT::Graphics::S3DLoader probe;
        if (!probe.loadZone(eq_ + "cabeast_chr.s3d")) {
            e.reason = "cabeast_chr.s3d failed to load";
            return e;
        }
        auto zone = probe.getZone();
        if (!zone || !zone->wldLoader) {
            e.reason = "no WLD data";
            return e;
        }
        const auto& wld = *zone->wldLoader;
        const auto& modelRefs = wld.getModelRefs();
        const auto& tracks = wld.getSkeletonTracks();

        const std::string charName = "SSN_HS_DEF";   // the loader's own name for the skeleton
        std::shared_ptr<EQT::Graphics::SkeletonTrack> track;
        for (const auto& [idx, t] : tracks)
            if (t->name == charName) { track = t; break; }
        if (!track) {
            e.reason = "skeleton " + charName + " absent from " + std::to_string(tracks.size()) + " tracks";
            return e;
        }

        std::set<const EQT::Graphics::ZoneGeometry*> seen;   // each geometry counted once
        const auto nameOf = [&](uint32_t modelRef) -> std::string {
            auto it = modelRefs.find(modelRef);
            if (it == modelRefs.end() || it->second.geometryFragRef == 0) return "";
            auto g = wld.getGeometryByFragmentIndex(it->second.geometryFragRef);
            if (!g) return "";
            seen.insert(g.get());
            return g->name;
        };
        // Bone-attached meshes, walked iteratively (a recursive auto lambda
        // self-references before deduction, which is ill-formed).
        {
            std::vector<std::shared_ptr<EQT::Graphics::SkeletonBone>> frontier = track->bones;
            while (!frontier.empty()) {
                std::vector<std::shared_ptr<EQT::Graphics::SkeletonBone>> next;
                for (const auto& b : frontier) {
                    if (!b) continue;
                    if (b->modelRef > 0) nameOf(b->modelRef);
                    next.insert(next.end(), b->children.begin(), b->children.end());
                }
                frontier = std::move(next);
            }
        }
        // The 0x200 mesh list -- the part the name selection cannot reach.
        for (uint32_t ref : track->meshRefs)
            nameOf(ref);
        if (seen.empty()) {
            e.reason = "skeleton " + charName + " references no meshes";
            return e;
        }
        // report the derived list (RED output names the dropped parts)
        for (const auto* g : seen) {
            if (!e.listing.empty()) e.listing += ", ";
            e.listing += g->name + "(" + std::to_string(g->vertices.size()) + ")";
            e.total += g->vertices.size();
        }
        e.ok = true;
        return e;
    }
};

// The built model must hold every mesh the skeleton references. On origin/fable
// this is RED: the 0x200-listed head SNNHE00_DMSPRITEDEF (974 verts) is never
// stored (the loader walks the bones) and the name-based selection (SSN /
// SSNHE00) cannot reach it, so the combined geometry holds the 3359-vert body
// alone -- 974 verts of the referenced 4333 are missing.
TEST_F(B733Race146LoaderTest, BuiltModelHoldsEveryMeshTheSkeletonReferences) {
    ASSERT_EQ(EQT::Graphics::getRaceCode(146), "SSN") << "db race row moved";

    const auto exp = probeSkeleton();
    ASSERT_TRUE(exp.ok) << exp.reason;
    std::cout << "B733: SSN_HS_DEF references [" << exp.listing
              << "] total=" << exp.total << std::endl;
    const size_t expected = exp.total;

    // The production path: the T504 resolver reaches cabeast_chr.s3d through
    // the archive walk (see ensureModelDataLoaded), landing in
    // loadModelFromCachedChr -- the path race 146's db row drives.
    auto model = loader_->ensureModelDataLoaded(146, 0);
    ASSERT_NE(model, nullptr) << "race 146 did not resolve a model";
    ASSERT_NE(model->combinedGeometry, nullptr) << "no combined geometry";

    const size_t actual = model->combinedGeometry->vertices.size();
    ASSERT_EQ(actual, expected)
        << "built model holds " << actual << " of the " << expected
        << " verts the skeleton references; the referenced meshes it dropped are the "
           "0x200-listed ones whose names the name-based selection (SSN/SSNHE00) does not match";
}

#endif  // EQT_HAS_GRAPHICS
