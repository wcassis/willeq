#pragma once
// P49 (docker/irrlicht-drm/CIrrDeviceFB.cpp DRM mode-selection decision
// logic, extracted for unit testing).
//
// Background (refactor/P43_res_visibility_root_cause.md): when a caller's
// requested resolution has no exact match among the connector's advertised
// modes, CIrrDeviceFB::initDRM() silently substitutes the connector's
// preferred mode and overwrites Width/Height -- every historical "400x300"
// Pi run actually rendered 2560x1440 with nobody noticing because the only
// signal was a buried ELL_WARNING log line. P49 makes that loud instead of
// silent; this header is the pure decision function the loud/refuse logic
// on both the production client (CIrrDeviceFB.cpp) and the benchmark
// (gles2_zone_benchmark) needs to agree on, factored out so it can be unit
// tested on x86 -- there is no DRM/KMS path on x86 at all (see docker/CLAUDE.md),
// so the real CIrrDeviceFB.cpp can only be build-verified there, never run.
//
// Deliberately dependency-free (no libdrm/gbm/EGL/Irrlicht headers) so it
// compiles and runs anywhere, including a plain host unit test binary.
#include <cstdint>
#include <vector>

namespace eqt_drm {

// One connector-advertised mode, reduced to the fields the selection
// decision needs. Mirrors the subset of drmModeModeInfo CIrrDeviceFB.cpp
// reads: hdisplay, vdisplay, vrefresh, and whether DRM_MODE_TYPE_PREFERRED
// is set in .type.
struct DRMModeCandidate {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t refresh = 0;
    bool preferred = false;
};

struct DRMModeSelection {
    // Index into the input `modes` vector. Only -1 when `modes` is empty
    // (callers already require count_modes > 0 before reaching this point).
    int index = -1;
    // True iff a mode matching the exact requested width/height was found
    // (the "happy path" -- no silent substitution occurred).
    bool exactMatch = false;
};

// Same logic previously inline in CIrrDeviceFB::initDRM() (P43 root cause,
// CIrrDeviceFB.cpp:335-382 pre-P49): exact match with the highest refresh
// rate wins; if there is no exact match, fall back to the connector's
// preferred mode; if there is no preferred mode either, fall back to the
// first advertised mode. requestedW/H == 0 (no resolution requested) always
// takes the fallback path with exactMatch=false, matching prior behavior.
inline DRMModeSelection selectDRMMode(const std::vector<DRMModeCandidate>& modes,
                                       uint32_t requestedW, uint32_t requestedH) {
    DRMModeSelection result;
    if (modes.empty())
        return result;

    if (requestedW > 0 && requestedH > 0) {
        for (size_t i = 0; i < modes.size(); ++i) {
            if (modes[i].width == requestedW && modes[i].height == requestedH) {
                if (result.index < 0 ||
                    modes[i].refresh > modes[static_cast<size_t>(result.index)].refresh) {
                    result.index = static_cast<int>(i);
                }
            }
        }
        if (result.index >= 0) {
            result.exactMatch = true;
            return result;
        }
    }

    for (size_t i = 0; i < modes.size(); ++i) {
        if (modes[i].preferred) {
            result.index = static_cast<int>(i);
            result.exactMatch = false;
            return result;
        }
    }

    result.index = 0;
    result.exactMatch = false;
    return result;
}

}  // namespace eqt_drm
