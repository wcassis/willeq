// B1096 (#416) -- the ORDER of the fix, pinned in source: initLoadingScreen
// must install the EQE1 name->archive resolution (initializeEQE1EarlyResolution)
// after willeq.eqe1 opens (U02) and before the first pre-zone UI work (U03).
//
// The player's race model (the paperdoll) is built on the render thread
// BEFORE loadZoneStep0_EQE1 runs, so the resolver must be in place at init
// time; the zone Step0 re-registration is a no-op. Order is the whole fix,
// so this test reads it out of the production source (the idiom from
// test_software_cursor_pack_name.cpp, which pins the same class of initialisation
// order in createSoftwareCursor).
//
// This TU compiles on the parent commit too (it names no new symbol), which is
// what makes it the red: on the parent, initLoadingScreen has no
// initializeEQE1EarlyResolution call, so the anchor is missing and the test
// fails. Green at the head: the call sits between the U02 block and U03.
// No assets needed -- pure source scan.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST(PreZoneEqe1Resolution, TheSourceRegistersEarlyResolutionAfterTheWilleqPackOpens) {
    const std::string kSrc =
        std::string(WILLEQ_SOURCE_DIR) + "/src/client/graphics/renderer/renderer_init.cpp";
    const std::string src = readFile(kSrc);
    ASSERT_FALSE(src.empty()) << "cannot read " << kSrc;
    const size_t fn = src.find("bool IrrlichtRenderer::initLoadingScreen(");
    ASSERT_NE(fn, std::string::npos) << "initLoadingScreen() not found in " << kSrc;
    const size_t end = src.find("\n}", fn);
    const std::string body = src.substr(fn, (end == std::string::npos ? src.size() : end) - fn);

    const size_t willeqOpen = body.find("getOrOpenEQE1(\"willeq\")");
    const size_t earlyInit = body.find("initializeEQE1EarlyResolution(");
    const size_t uiRenderer = body.find("frame_->uiRenderer_ = std::make_unique<UIRenderer>()");
    ASSERT_NE(willeqOpen, std::string::npos)
        << "the U02 willeq.eqe1 open moved; update this test's anchors";
    ASSERT_NE(uiRenderer, std::string::npos)
        << "the U03 UI-renderer block moved; update this test's anchors";
    ASSERT_NE(earlyInit, std::string::npos)
        << "B1096: initLoadingScreen must call initializeEQE1EarlyResolution() "
           "before any pre-zone model build can ask the cache for a name whose "
           "owner is not willeq.eqe1";
    EXPECT_GT(earlyInit, willeqOpen)
        << "the resolver must be registered after willeq.eqe1 opens (the resolver "
           "and the open loader must both be in place when the first pre-zone "
           "lookup runs)";
    EXPECT_GT(uiRenderer, earlyInit)
        << "the install must precede the first pre-zone UI work (U03)";
}
