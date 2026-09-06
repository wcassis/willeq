// T413 (board B285) — the software cursor's cache name must be the PACK's name.
//
// createSoftwareCursor() asked ConstrainedTextureCache for "softwareCursor".
// findCompressedEntry() strips a .bmp/.dds extension and lowercases, so that
// name reaches the flat pack as "softwarecursor", and the pack's entry — built
// from data/textures/software_cursor.bmp — is "software_cursor". The two could
// never match.
//
// Nothing failed, for one reason only: ORDERING. createSoftwareCursor() runs
// in renderer_init.cpp fourteen lines before getOrOpenEQE1("willeq"), so
// snapshotEQE1Loaders() is still empty and getOrLoad() falls through to the raw
// BMP decoder, which is keyed by nothing. The moment ANY archive is open first,
// getOrLoad() takes this branch instead:
//
//     if (!snapshotEQE1Loaders().empty()) {
//         LOG_FATAL(... "not found in any EQE1 flat pack" ...);
//         std::exit(1);
//     }
//
// on the DRM/KMS path — the Orange Pi, where the software cursor is the only
// cursor. So the failure mode is a hard exit at startup, gated on an
// initialisation order nobody wrote down.
//
// These tests are the regression net for both halves:
//
//   1. The name the SOURCE passes is looked up in the REAL shipped pack. This
//      is the red: with "softwareCursor" restored, the pack lookup misses.
//   2. The real ConstrainedTextureCache is driven with the pack ALREADY OPEN,
//      the exact configuration the ordering was hiding. A miss there is not an
//      assertion failure — it is exit(1) taking the whole binary with it, which
//      is precisely the production failure this pins.
//
// DRM-path only in production, but nothing here needs DRM, a display or a
// driver: getOrLoad() resolves an EQE1 hit and returns before any GL call.

#include <gtest/gtest.h>

#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

namespace {

const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

const std::string kRendererSrc =
    std::string(WILLEQ_SOURCE_DIR) +
    "/src/client/graphics/renderer/irrlicht_renderer.cpp";

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The name createSoftwareCursor() actually passes to getOrLoad(), read out of
// the production source rather than duplicated here — so this test cannot pass
// by agreeing with itself.
std::string cursorCacheNameFromSource() {
    const std::string src = readFile(kRendererSrc);
    const size_t fn = src.find("void IrrlichtRenderer::createSoftwareCursor()");
    if (fn == std::string::npos) return {};
    const size_t end = src.find("\n}\n", fn);
    const std::string body = src.substr(fn, (end == std::string::npos ? src.size() : end) - fn);

    std::smatch m;
    const std::regex re(R"(getOrLoad\(\"([A-Za-z0-9_]+)\")");
    if (!std::regex_search(body, m, re)) return {};
    return m[1].str();
}

bool packPresent() {
    std::ifstream probe(kWilleqEqe1, std::ios::binary);
    return probe.good();
}

} // namespace

TEST(SoftwareCursorPackName, TheSourceStillNamesTheCursorInCreateSoftwareCursor) {
    ASSERT_FALSE(readFile(kRendererSrc).empty())
        << "cannot read " << kRendererSrc;
    EXPECT_FALSE(cursorCacheNameFromSource().empty())
        << "createSoftwareCursor() no longer has a getOrLoad(\"name\", ...) call; "
           "this test's extraction needs updating with whatever replaced it";
}

// The red/green: the name the source passes must exist in the shipped pack.
TEST(SoftwareCursorPackName, TheCacheNameResolvesInTheShippedPack) {
    if (!packPresent()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;

    const std::string name = cursorCacheNameFromSource();
    ASSERT_FALSE(name.empty());

    EQT::Graphics::EQE1Loader loader;
    ASSERT_TRUE(loader.open(kWilleqEqe1)) << "failed to open " << kWilleqEqe1;

    // findCompressedEntry()'s own normalisation is lowercase-only: the pack is
    // never consulted under any other spelling, so a camelCase cache name for
    // a snake_case entry is unmatchable by construction.
    EXPECT_NE(loader.lookup(name), nullptr)
        << "createSoftwareCursor() asks the cache for '" << name
        << "', and willeq.eqe1 has no such entry. The pack entry built from "
           "data/textures/software_cursor.bmp is named 'software_cursor'. "
           "With any EQE1 archive open, getOrLoad() would LOG_FATAL + exit(1) "
           "on the DRM path (board B285).";
}

// The configuration the initialisation order was hiding: pack ALREADY OPEN,
// then the cursor requested. A name miss here is exit(1), not a failed
// EXPECT — reaching the end of this test is the assertion.
TEST(SoftwareCursorPackName, TheCursorResolvesFromTheCacheAfterThePackIsOpen) {
    if (!packPresent()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;

    const std::string name = cursorCacheNameFromSource();
    ASSERT_FALSE(name.empty());

    EQT::Graphics::EQE1Loader loader;
    ASSERT_TRUE(loader.open(kWilleqEqe1));
    ASSERT_NE(loader.lookup(name), nullptr)
        << "guarded so this test reports a miss instead of exiting the binary";

    EQT::Graphics::ConstrainedRendererConfig config;
    EQT::Graphics::ConstrainedTextureCache cache(config);
    cache.addEQE1Loader(&loader);

    // Empty bytes on purpose: the whole point is that the EQE1 hit is found
    // before any decode is attempted. getOrLoad() returns nullptr on an EQE1
    // hit (the caller then promotes it with uploadTexture(), which needs a
    // driver and is not what is under test here).
    const std::vector<char> noBytes;
    EXPECT_EQ(cache.getOrLoad(name, noBytes), nullptr);

    SUCCEED() << "getOrLoad('" << name
              << "') with the pack open did not take the fatal branch";
}

// And the consequence, made explicit and permanent: with the pack open, a
// cache name the pack does not carry is a hard exit, not a fallback. This is
// what "softwareCursor" would do the moment the initialisation order moved.
TEST(SoftwareCursorPackNameDeathTest, AnUnmatchableNameExitsWithThePackOpen) {
    if (!packPresent()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;

    EXPECT_EXIT(
        {
            EQT::Graphics::EQE1Loader loader;
            if (!loader.open(kWilleqEqe1)) std::_Exit(2);
            EQT::Graphics::ConstrainedRendererConfig config;
            EQT::Graphics::ConstrainedTextureCache cache(config);
            cache.addEQE1Loader(&loader);
            const std::vector<char> noBytes;
            cache.getOrLoad("softwareCursor", noBytes);
            std::_Exit(0);  // not reached: the miss above is exit(1)
        },
        ::testing::ExitedWithCode(1), "");
}
