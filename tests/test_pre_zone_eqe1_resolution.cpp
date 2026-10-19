// B1096 (#416) -- the behavioral net for the pre-zone EQE1 resolution.
//
// Two tests, both skipped when the ETC1 tree is not mounted (asset
// dependency, same idiom as test_eqe1_archive_component.cpp):
//
//   PreZoneMissWithNoResolverExits (death test) -- the branch the XU4 died
//   in: willeq.eqe1 open (the ONE loader the play test had), no resolver
//   registered (Step0 has not run), a variant name (daffa0001) no open pack
//   holds. getOrLoad()'s all-or-nothing rule -- gated on ANY loader being
//   open, constrained_texture_cache.cpp:622 -- LOG_FATALs and exits(1)
//   (constrained_texture_cache.cpp:651).
//
//   EarlyResolutionResolvesANameNoOpenPackHolds -- the fix: the SAME
//   single-open-pack configuration, but initializeEQE1EarlyResolution() has
//   run. The index (daffa0001 -> global_chr, globaldaf_chr) resolves the
//   miss, openArchiveContaining() mmaps global_chr.eqe1, and the lookup is
//   served by exactly that pack. No exit. It also shows why the card's
//   "keep the S3D bytes" option cannot fix it: the fatal gate never looks at
//   the data, so the name is requested with and without bytes and resolves
//   either way.
//
// This TU names initializeEQE1EarlyResolution, so it builds only at the head
// (with the fix) -- the parent's red is carried by
// test_pre_zone_eqe1_resolution_order.cpp, which compiles everywhere.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1/eqe1_archive_component.h"
#include "client/graphics/eqe1/eqe1_early_resolution.h"
#include "client/graphics/eqe1_loader.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

namespace {

using namespace EQT::Graphics;

const char* kEqe1Tree = "/mnt/projects/EverQuest_ETC1";  // data/eqe1_client target

// ctest runs the binary with CWD <build>/tests (T36), so the index -- a
// shipped-data load, resolved through ResolveDataPath() -- would miss: its
// checkout-relative candidate does not exist under the build tree. Point the
// $WILLEQ_DATA_ROOT candidate at the checkout (WILLEQ_REPO_ROOT from the
// force-included willeq_paths.h, resolved at run time) instead. A test that
// sets its own environment is independent of ctest properties, and this one
// needs no properties at all -- which matters: under PRE_TEST discovery the
// tests do not exist at configure time, so a configure-time
// SET_TESTS_PROPERTIES on them would fail the whole configure.
void pinDataRootToCheckout() {
    const char* already = std::getenv("WILLEQ_DATA_ROOT");
    if (already && *already) return;
    ::setenv("WILLEQ_DATA_ROOT", WILLEQ_REPO_ROOT, 1);
}

bool filePresent(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

bool assetsPresent() {
    pinDataRootToCheckout();
    return filePresent(std::string(kEqe1Tree) + "/willeq.eqe1") &&
           filePresent(std::string(kEqe1Tree) + "/global_chr.eqe1") &&
           filePresent(std::string(WILLEQ_SOURCE_DIR) + "/data/eqe1_texture_index.json");
}

// The exact pre-zone configuration: ONE pack open (willeq.eqe1), no resolver.
// MEMBER ORDER IS LOAD-BEARING: destruction is reverse, and the cache's
// clear() (the destructor's zone-unload path) calls into the driver, so the
// driver (the renderer) must outlive the cache.
struct PreZoneFixture {
    IrrlichtRenderer renderer;
    std::unique_ptr<ConstrainedTextureCache> cache;
    EQE1Loader willeq;

    bool ready() {
        cache = std::make_unique<ConstrainedTextureCache>(ConstrainedRendererConfig());
        renderer.Context().constrainedTextureCache = cache.get();
        renderer.Context().config.constrainedConfig.eqe1Path = kEqe1Tree;
        if (!willeq.open(std::string(kEqe1Tree) + "/willeq.eqe1")) return false;
        cache->setDriver(renderer.getDriver());
        cache->addEQE1Loader(&willeq);
        return true;
    }
};

}  // namespace

// The branch the XU4 died in: willeq.eqe1 open, no resolver, a name no open
// pack holds -> exit(1). EXPECT_EXIT forks, so the child's exit never takes
// this test binary with it. The name must be absent from the open pack for
// this to be a miss -- if the shipped willeq pack ever gains it, pick
// another.
TEST(PreZoneEqe1Resolution, PreZoneMissWithNoResolverExits) {
    if (!filePresent(std::string(kEqe1Tree) + "/willeq.eqe1"))
        GTEST_SKIP() << "willeq.eqe1 not mounted at " << kEqe1Tree;
    const std::string kName = "daffa0001.bmp";
    {
        EQE1Loader probe;
        if (probe.open(std::string(kEqe1Tree) + "/willeq.eqe1") && probe.lookup(kName))
            GTEST_SKIP() << "willeq.eqe1 now holds " << kName << "; pick another name";
    }

    EXPECT_EXIT(
        {
            PreZoneFixture fx;
            if (!fx.ready()) std::_Exit(77);  // asset gap, not the branch under test
            const std::vector<char> noBytes;
            fx.cache->getOrLoad(kName, noBytes);
            std::_Exit(0);  // not reached: the miss above is exit(1)
        },
        ::testing::ExitedWithCode(1), "");
}

// The fix, behaviorally: the SAME single-open-pack configuration, but the
// early resolution installed. The index resolves the miss to its owner, the
// owner's pack is opened on demand, and the name is served by it. No exit.
TEST(PreZoneEqe1Resolution, EarlyResolutionResolvesANameNoOpenPackHolds) {
    if (!assetsPresent())
        GTEST_SKIP() << "ETC1 tree / willeq.eqe1 / index not mounted at " << kEqe1Tree;
    const std::string kName = "daffa0001.bmp";

    PreZoneFixture fx;
    ASSERT_TRUE(fx.ready()) << "could not stand up the pre-zone configuration";

    initializeEQE1EarlyResolution(fx.renderer, fx.cache.get());

    // The name must resolve (not exit) either way: the FATAL gate is the open
    // loader set, never the data -- that is the point of the fix.
    const std::vector<char> noBytes;  // the pack supplies the bytes
    const std::vector<char> withBytes{'B', 'M'};  // B1051 shape: bytes in hand
    // The index maps the name to global_chr (and globaldaf_chr); the resolver
    // opens the owner on demand and the pack serves the name through the
    // cache. The request log keys on the extension-stripped name (that is
    // what the EQE1 packs themselves name their entries).
    const std::string kStem = "daffa0001";
    fx.cache->getOrLoad(kName, noBytes);
    ASSERT_EQ(fx.cache->eqe1ServedByStem(kStem), "global_chr")
        << "the index maps daffa0001 to global_chr (and globaldaf_chr); the "
           "resolver must open the owner and the pack must hold the name";
    fx.cache->getOrLoad(kName, withBytes);
    EXPECT_EQ(fx.cache->eqe1ServedByStem(kStem), "global_chr");

    SUCCEED() << "getOrLoad('" << kName
              << "') with only willeq.eqe1 open and the early resolver did not "
                 "take the fatal branch; global_chr was opened to serve it";

    // TEARDOWN ORDER: the renderer's shutdown (a fixture member, destroyed
    // after the cache) runs unloadZone() -> clearEQE1Loaders() and the
    // loader-re-registration loop, both of which reach the cache through
    // Context().constrainedTextureCache. Drop the cache first and NULL the
    // context pointer, so shutdown sees no cache instead of a dead one
    // (the same shape as test_eqe1_source_archive.cpp's TearDown).
    fx.renderer.Context().constrainedTextureCache = nullptr;
    fx.cache.reset();
}
