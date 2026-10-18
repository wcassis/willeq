// B1034 (#357): unit coverage for
// docker/irrlicht-drm/gbm_config_select.h's
// eqt_drm::selectEglConfigForGbmFormat() -- the EGL-config choice for the
// GBM surface's format, extracted from CIrrDeviceFB::initEGL() so the
// decision can be pinned without a DRM device or a GL context (same shape
// as test_drm_mode_select, test_drm_card_select, test_x11_keysym_map).
//
// The defect it guards (from the Fable review of MR !143, note 3889):
// eglChooseConfig lists every 10-bit config ahead of every 8-bit one, so a
// driver with more than 64 matches cut the XRGB8888 config off the fixed
// configs[64]; the fallback was configs[0] (10-bit) and the function
// RETURNED TRUE, so the error moved to eglCreateWindowSurface and the
// caller's no-depth retry never ran. The header keeps that shape as
// chooseConfigForGbmFormatFixed64() for the negative half of this net: the
// 70-config case reproduces the defect in the OLD code and shows the new
// decision does not have it.
//
// "The picks are unchanged" is pinned two ways:
//   * tests/data/b1034/surfaceless_es2.json -- a REAL eglChooseConfig dump
//     from the x86 tree's environment (llvmpipe surfaceless, the CI GL pin;
//     scripts/ci/egl_config_dump.sh, kept honest by the EglConfigDumpB1034
//     hygiene test). The x86 fork's KMS/GBM path has no WINDOW-bit configs
//     on a surfaceless display, so the surfaceless PBUFFER list is the x86
//     config set; its pick (configs[0]) must stay configs[0].
//   * tests/data/b1034/lima_single.json -- the LIMA shape as measured in
//     B1030: Lima lists exactly one XRGB8888 config (no 10-bit configs at
//     all), so a driver with one 8-bit config keeps picking it. Encoded
//     from B1030's measurement; the real Lima lane runs (the acceptance's
//     lane_smoke) are the live half of the pin.
//
// The pure header needs no EGL/gbm/Irrlicht include, so this target is
// unguarded (it builds in the hygiene tree too, like test_drm_mode_select).

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "docker/irrlicht-drm/gbm_config_select.h"

// The fixture paths are per-target defines (tests/cmake/core_utils.cmake)
// written as WILLEQ_REPO_PATH("rel") -- run-time const char* EXPRESSIONS
// (common/willeq_paths.h, force-included into every test TU; B1090), so
// they resolve the same way in the hygiene tree (cwd = the build tree) and
// on the ThinkPad (cwd = the worktree). They are expressions, not string
// literals, so a fixture is read as readFixture(B1034_SURFACELESS) -- one
// expression, never string-concatenated.

namespace {

using eqt_drm::EglConfigEntry;
using eqt_drm::EglConfigRequest;
using eqt_drm::chooseConfigForGbmFormatFixed64;
using eqt_drm::selectEglConfigForGbmFormat;

const uint32_t kXRGB8888 = eqt_drm::kGbmFormatXRGB8888;
const uint32_t kARGB8888 = eqt_drm::kGbmFormatARGB8888;
const uint32_t kXR30 = 0x30303258;  // 10-bit XRGB (Panfrost's first visual)
const uint32_t kAR30 = 0x30303041;  // 10-bit ARGB

EglConfigEntry cfg(uint32_t visual, int depth, int stencil = 0) {
    EglConfigEntry c;
    c.visual = visual;
    c.renderableType = eqt_drm::kEglOpenGLES2Bit;
    c.depth = depth;
    c.stencil = stencil;
    return c;
}

EglConfigRequest es2Request(int depth, int stencil = 0) {
    EglConfigRequest r;
    r.renderableType = eqt_drm::kEglOpenGLES2Bit;
    r.depth = depth;
    r.stencil = stencil;
    return r;
}

// ---- the B1034 defect, reproduced in the OLD shape --------------------

// 70 matches for RED/GREEN/BLUE >= 8: all 10-bit first (eglChooseConfig's
// real order), the XRGB8888 config at index 65. The loop below fills the
// first 64 (indices 0..63); the six explicit pushes are indices 64..69, so
// the XRGB8888 config lands on index 65 -- just past the fixed configs[64]
// the OLD code reads, exactly the shape a >64-match driver produces.
std::vector<EglConfigEntry> seventyConfigs() {
    std::vector<EglConfigEntry> list;
    list.reserve(70);
    for (int i = 0; i < 32; ++i) {
        list.push_back(cfg(kXR30, 16));
        list.push_back(cfg(kAR30, 16));
    }
    list.push_back(cfg(kAR30, 24));          // 64
    list.push_back(cfg(kXRGB8888, 16));      // 65 -- the 8-bit config, past configs[64]
    list.push_back(cfg(kARGB8888, 16));      // 66
    list.push_back(cfg(kXR30, 24));          // 67
    list.push_back(cfg(kXR30, 24, 8));       // 68
    list.push_back(cfg(kXR30, 32));          // 69
    return list;
}

// index 65 of seventyConfigs() IS the XRGB8888 config.
TEST(EglConfigSelect, SeventyConfigsPutXRGB8888AtIndex65) {
    const auto list = seventyConfigs();
    ASSERT_EQ(list.size(), 70u);
    EXPECT_EQ(list[65].visual, kXRGB8888);
}

// The OLD code: the fixed 64-entry array never sees index 65, walks the
// 10-bit configs, finds no XRGB8888, and falls back to configs[0] (10-bit)
// RETURNING TRUE -- the error that used to move to eglCreateWindowSurface.
TEST(EglConfigSelect, Fixed64ReproducesTheB1034Defect) {
    const auto list = seventyConfigs();
    const auto request = es2Request(16);
    int pick = -1;
    const bool ok = chooseConfigForGbmFormatFixed64(list, request, kXRGB8888, &pick);
    ASSERT_TRUE(ok);  // the defect: it SAYS it chose a config...
    ASSERT_GE(pick, 0);
    EXPECT_NE(list[(size_t)pick].visual, kXRGB8888)
        << "the old shape must reproduce the 10-bit fallback (the defect under test)";
    EXPECT_EQ(pick, 0);
}

// ...and the NEW decision picks the XRGB8888 config at index 65, because it
// saw all 70 configs (the NULL-array count query + allocation the device TU
// now does).
TEST(EglConfigSelect, SeventyConfigsSelectIndex65) {
    const auto list = seventyConfigs();
    const auto request = es2Request(16);
    const int pick = selectEglConfigForGbmFormat(list, request, kXRGB8888);
    EXPECT_EQ(pick, 65);
    ASSERT_NE(pick, -1);
    EXPECT_EQ(list[(size_t)pick].visual, kXRGB8888);
}

// ---- the fallbacks ------------------------------------------------------

// A driver whose only 8-bit config is ARGB8888 (no XRGB8888 visual): the
// pick is the ARGB config -- Mesa's KMS platform accepts the ARGB/XRGB mix
// -- NOT configs[0] (a 10-bit visual that eglCreateWindowSurface would
// refuse).
TEST(EglConfigSelect, ArgbOnlyFallsBackToArgbNotFirst) {
    const std::vector<EglConfigEntry> list = {
        cfg(kXR30, 16), cfg(kAR30, 16), cfg(kARGB8888, 16),
    };
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, kXRGB8888), 2);
    // the old shape would have picked 0 (XR30):
    int old = -1;
    ASSERT_TRUE(chooseConfigForGbmFormatFixed64(list, request, kXRGB8888, &old));
    EXPECT_EQ(old, 0);
}

// XRGB8888 is the first match: eglChooseConfig's own order within the visual
// is untouched (this is the B1030 rule the lanes rely on).
TEST(EglConfigSelect, XrgbFirstMatchWins) {
    const std::vector<EglConfigEntry> list = {
        cfg(kXRGB8888, 16), cfg(kXRGB8888, 24), cfg(kARGB8888, 16),
    };
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, kXRGB8888), 0);
}

// A non-XRGB gbm format gets NO ARGB preference (the mix is documented for
// XRGB8888 surfaces): first exact visual, else configs[0].
TEST(EglConfigSelect, NonXrgbFormatGetsNoArgbPreference) {
    const std::vector<EglConfigEntry> list = {
        cfg(kARGB8888, 16), cfg(kAR30, 16),
    };
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, kAR30), 1);
    // and when even that is absent: configs[0], unchanged from the old shape
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, 0x12345678), 0);
}

// An empty list is a choice failure (the device TU logs and returns false).
TEST(EglConfigSelect, EmptyListFails) {
    const std::vector<EglConfigEntry> list;
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, kXRGB8888), -1);
    int old = -1;
    EXPECT_FALSE(chooseConfigForGbmFormatFixed64(list, request, kXRGB8888, &old));
}

// A config the request does not satisfy (the wrong renderable type) is never
// picked, even when it is the only 8-bit config.
TEST(EglConfigSelect, UnsatisfiedConfigsAreSkipped) {
    EglConfigEntry desktopOnly = cfg(kXRGB8888, 16);
    desktopOnly.renderableType = eqt_drm::kEglOpenGLBit;
    const std::vector<EglConfigEntry> list = {
        cfg(kXR30, 16), desktopOnly,
    };
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, kXRGB8888), 0);
}

// ---- the real lists ------------------------------------------------------

struct Fixture {
    std::vector<EglConfigEntry> configs;
    bool ok = false;
};

// The fixture files are hand-written JSON in exactly the dump script's
// shape (one object per line); parse that shape, nothing more.
Fixture readFixture(const char* path) {
    Fixture f;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        const std::regex re(
            R"(\{"visual":\s*(\d+),\s*"renderableType":\s*(\d+),\s*"depth":\s*(\d+),\s*"stencil":\s*(\d+)\})");
        if (std::regex_search(line, m, re)) {
            EglConfigEntry c;
            c.visual = (uint32_t)std::stoul(m[1].str());
            c.renderableType = std::stoi(m[2].str());
            c.depth = std::stoi(m[3].str());
            c.stencil = std::stoi(m[4].str());
            f.configs.push_back(c);
            f.ok = true;
        }
    }
    return f;
}

TEST(EglConfigSelect, LlvmPipeSurfacelessPickIsUnchanged) {
    const Fixture f = readFixture(B1034_SURFACELESS);
    ASSERT_TRUE(f.ok) << "fixture tests/data/b1034/surfaceless_es2.json unreadable";
    ASSERT_GE(f.configs.size(), 2u);
    // The dump: every visual is 0 (surfaceless EGL has no GBM visual) and
    // every config is an "8-bit" RGBA one -- there is no 10-bit config and
    // no XRGB8888 visual, so the pick is configs[0] under BOTH shapes. The
    // pin is that the new decision does not invent a different pick.
    for (const auto& c : f.configs)
        EXPECT_EQ(c.visual, 0u);
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(f.configs, request, kXRGB8888), 0);
    int old = -1;
    ASSERT_TRUE(chooseConfigForGbmFormatFixed64(f.configs, request, kXRGB8888, &old));
    EXPECT_EQ(old, 0);
    // and the no-depth retry (initEGL's fallbackAttribs) picks the same head
    EXPECT_EQ(selectEglConfigForGbmFormat(f.configs, es2Request(0), kXRGB8888), 0);
    // a deeper request still lands on the head: eglChooseConfig's own order
    // within the (absent) visual is untouched
    EXPECT_EQ(selectEglConfigForGbmFormat(f.configs, es2Request(24), kXRGB8888), 0);
}

TEST(EglConfigSelect, LimaPickIsUnchanged) {
    const Fixture f = readFixture(B1034_LIMA);
    ASSERT_TRUE(f.ok) << "fixture tests/data/b1034/lima_single.json unreadable";
    ASSERT_EQ(f.configs.size(), 1u);
    EXPECT_EQ(f.configs[0].visual, kXRGB8888)
        << "B1030 measured Lima listing a single XRGB8888 config";
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(f.configs, request, kXRGB8888), 0);
    int old = -1;
    ASSERT_TRUE(chooseConfigForGbmFormatFixed64(f.configs, request, kXRGB8888, &old));
    EXPECT_EQ(old, 0);
}

// The shape B1030 measured on the ROCK 4D (Panfrost/Mesa 26: AR30 and XR30
// listed before AR24/XR24), with the XRGB8888 visual present but NOT first:
// the 10-bit configs are skipped, the 8-bit XRGB8888 wins.
TEST(EglConfigSelect, PanfrostShapeSkipsTenBitAndPicksXrgb8888) {
    const std::vector<EglConfigEntry> list = {
        cfg(kAR30, 16), cfg(kXR30, 16),
        cfg(kAR30, 24), cfg(kXR30, 24),
        cfg(kARGB8888, 16), cfg(kXRGB8888, 16),
    };
    const auto request = es2Request(16);
    EXPECT_EQ(selectEglConfigForGbmFormat(list, request, kXRGB8888), 5);
}

}  // namespace
