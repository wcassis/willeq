#pragma once
// B1034 (#357): the EGL-config choice for the GBM surface's format, extracted
// from CIrrDeviceFB::initEGL() (docker/irrlicht-drm/CIrrDeviceFB.cpp and its
// docker/irrlicht-drm-x86 fork) so the decision is unit-testable without a
// DRM device or a GL context. The device TU keeps a thin static wrapper of
// the same name that calls selectEglConfigForGbmFormat() and owns the
// warnings; that wrapper is IDENTICAL in both forks (hygiene test
// scripts/ci/test_gbm_config_select_forks.py keeps it that way).
//
// B1030 (#350) established the RULE -- pick the first eglChooseConfig match
// whose EGL_NATIVE_VISUAL_ID is the gbm_surface's format (GBM_FORMAT_XRGB8888
// in both devices), because a config whose visual does not match the format
// fails in eglCreateWindowSurface (Mesa refuses a config/format mismatch),
// and on a driver that exposes 10-bit configs (Panfrost/Mesa 26 on the ROCK
// 4D lists AR30 and XR30 before AR24/XR24) the first match for
// RED/GREEN/BLUE >= 8 is a 10-bit visual. B1030 left a fixed configs[64],
// which is this header's defect: eglChooseConfig lists every 10-bit config
// ahead of every 8-bit one, so a driver with more than 64 matches cuts the
// XRGB8888 config off the array, the fallback is configs[0] (10-bit) and the
// function returns true -- the error then moves to eglCreateWindowSurface
// and the caller's no-depth retry never runs.
//
// The fix: query the count first (eglChooseConfig with a NULL array),
// allocate that many, and walk them all. Two fallbacks, in order:
//   1. an ARGB8888 visual (0x34325042): Mesa's KMS platform accepts ARGB/
//      XRGB mixing for the gbm_surface's XRGB8888 buffers, so an ARGB config
//      is still a WORKING surface (kmscube relies on exactly this on
//      surfaceless); a driver whose only 8-bit config is ARGB must not fall
//      through to a 10-bit config. A driver that lists XRGB8888 never
//      reaches this arm, so a lane that always picked its XRGB8888 config
//      keeps picking it;
//   2. configs[0] (the old behaviour), which keeps working on every
//      current driver: Lima lists exactly one config and llvmpipe's
//      surfaceless configs carry no XRGB8888 visual at all, so both lanes'
//      picks are unchanged (pinned by tests/test_egl_config_select.cpp
//      against a real dump, tests/data/b1034/).
//
// Deliberately dependency-free: no EGL/gbm/Irrlicht headers. The EGL
// constants it needs are the ones in the device TU's own <EGL/egl.h>,
// redefined locally so the pure header compiles in a plain host unit test
// the way drm_mode_select.h (P49) and drm_card_select.h (B1054) do. The
// visual ids are the GL_fourcc codes of gbm.h (linux/drm_fourcc.h):
// XRGB8888 = 0x34325258, ARGB8888 = 0x34325042.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace eqt_drm {

// EGL_RENDERABLE_TYPE -- the attrib of the caller's request that separates
// the ES2 configs from the desktop-GL ones.
constexpr int kEglRenderableType = 0x3040;
constexpr int kEglOpenGLBit = 0x0001;
constexpr int kEglOpenGLES2Bit = 0x0004;

// The GBM formats a gbm_surface can carry (gbm.h, GL_fourcc).
constexpr uint32_t kGbmFormatXRGB8888 = 0x34325258;  // GBM_FORMAT_XRGB8888
constexpr uint32_t kGbmFormatARGB8888 = 0x34325042;  // GBM_FORMAT_ARGB8888

// One EGLConfig, reduced to what the decision reads. `visual` is the
// EGL_NATIVE_VISUAL_ID (a GBM format id on the KMS platform).
struct EglConfigEntry {
    uint32_t visual;
    int renderableType;  // kEglRenderableType value
    int depth;           // EGL_DEPTH_SIZE
    int stencil;         // EGL_STENCIL_SIZE
};

// The caller's request, reduced the same way (the configAttribs of
// initEGL(), depth/stencil 0 in the caller's no-depth retry).
struct EglConfigRequest {
    int renderableType;
    int depth;
    int stencil;
};

// Does `c` satisfy the request? eglChooseConfig's own filter is "at least"
// on each component (the device TU requests the exact bits, so every match
// is exact on a conforming driver -- the filter keeps the function honest on
// a list the caller did not query itself, like the test fixtures).
inline bool eglConfigMatchesRequest(const EglConfigEntry& c,
                                    const EglConfigRequest& r) {
    return c.renderableType == r.renderableType && c.depth >= r.depth &&
           c.stencil >= r.stencil;
}

// The selection, pure. `configs` is EVERY config eglChooseConfig returned
// for the request, in its order (the device TU allocates exactly that many
// after a NULL-array count query). Returns the index of the pick, or -1 when
// the list is empty.
//
//   first XRGB8888 (the surface's own visual, eglChooseConfig's order
//     within the visual) > first ARGB8888 (Mesa accepts the ARGB/XRGB mix,
//     so a working 8-bit surface) > configs[0] (the old fallback).
//
// eglChooseConfig orders deeper colour buffers first -- every 10-bit
// (AR30/XR30) config ahead of every 8-bit one -- so "first XRGB8888" is not
// "an XRGB8888": the walk must not stop at the first 10-bit match, which is
// precisely what the fixed configs[64] + configs[0] fallback did when the
// 8-bit config was cut off the array.
inline int selectEglConfigForGbmFormat(const std::vector<EglConfigEntry>& configs,
                                       const EglConfigRequest& request,
                                       uint32_t gbmFormat) {
    int argbIndex = -1;
    for (size_t i = 0; i < configs.size(); ++i) {
        const EglConfigEntry& c = configs[i];
        if (!eglConfigMatchesRequest(c, request))
            continue;
        if (c.visual == gbmFormat)
            return static_cast<int>(i);
        if (gbmFormat == kGbmFormatXRGB8888 && c.visual == kGbmFormatARGB8888 &&
            argbIndex < 0)
            argbIndex = static_cast<int>(i);
    }
    if (argbIndex >= 0)
        return argbIndex;
    return configs.empty() ? -1 : 0;
}

// The B1030 shape this unit replaced, kept for the negative half of the
// regression net (tests/test_egl_config_select.cpp proves the defect is
// REPRODUCIBLE in this code and gone in selectEglConfigForGbmFormat()):
// a fixed 64-entry array, walk what fits, fall back to configs[0].
inline bool chooseConfigForGbmFormatFixed64(const std::vector<EglConfigEntry>& configs,
                                            const EglConfigRequest& request,
                                            uint32_t gbmFormat,
                                            int* outIndex) {
    std::vector<EglConfigEntry> fixed;
    fixed.assign(configs.begin(), configs.begin() + std::min<size_t>(configs.size(), 64));
    if (fixed.empty())
        return false;
    for (size_t i = 0; i < fixed.size(); ++i) {
        const EglConfigEntry& c = fixed[i];
        if (eglConfigMatchesRequest(c, request) && c.visual == gbmFormat) {
            *outIndex = static_cast<int>(i);
            return true;
        }
    }
    *outIndex = 0;  // the old fallback: the first match, whatever its visual
    return true;
}

}  // namespace eqt_drm
