#pragma once

// Lima's BO cache: ON by default, `nobocache` is opt-in (B988, Will
// 2026-10-05: "make the nobocache flag opt-in (off by default)").
//
// B986 appended `nobocache` to LIMA_DEBUG for every DRM client so Lima would
// release freed stream uploads and draw-time buffers instead of retaining them
// (1.5-3.5 MiB standing still, 13-23 MiB in crowds). The same-binary Pi A/B
// (review/CODEX-AUDIT-1005/README.md) measured the price: scene draw gfaydark
// 7.7 -> 15.5 ms, qeynos2 3.3 -> 6.0 ms. The retained memory is B972's (#290)
// to remove at its source. Record: refactor/B986_pi_gpu_guard.md.
//
//   WILLEQ_LIMA_NOBOCACHE=1   restore B986's policy (append `nobocache`)
//
// A caller's own LIMA_DEBUG is always kept as given. This must run BEFORE
// createDeviceEx: GBM/EGL initializes Lima before the renderer can query
// GL_RENDERER. Other drivers ignore LIMA_DEBUG. Not cached: the client calls
// it once, and the tests vary the environment between calls.

#include <cstdlib>
#include <string>

namespace EQT {
namespace Graphics {

enum class LimaBoCachePolicy {
    NotDrm,          // not a DRM client: LIMA_DEBUG untouched
    CacheOn,         // the default: LIMA_DEBUG untouched, Lima's BO cache on
    NoBoCacheCaller, // the caller's LIMA_DEBUG already disables it (nobocache/all)
    NoBoCacheSwitch, // WILLEQ_LIMA_NOBOCACHE=1 appended `nobocache`
};

struct LimaBufferPolicyResult {
    bool ok = true;  // false only when setenv failed
    LimaBoCachePolicy policy = LimaBoCachePolicy::NotDrm;
};

inline bool limaNoBoCacheRequested() {
    const char* v = std::getenv("WILLEQ_LIMA_NOBOCACHE");
    return v && v[0] == '1';
}

// True when Mesa's LIMA_DEBUG value already disables the BO cache. Mesa's
// special "all" includes NO_BO_CACHE; otherwise match whole comma/space
// separated tokens.
inline bool limaDebugDisablesBoCache(const std::string& flags) {
    if (flags == "all") return true;
    const std::string delimiters = ", \t\n\r";
    for (size_t begin = flags.find_first_not_of(delimiters); begin != std::string::npos;) {
        const size_t end = flags.find_first_of(delimiters, begin);
        if (flags.substr(begin, end == std::string::npos ? end : end - begin) == "nobocache")
            return true;
        begin = flags.find_first_not_of(delimiters, end);
    }
    return false;
}

inline LimaBufferPolicyResult configureLimaBufferPolicy(bool useDRM) {
    LimaBufferPolicyResult r;
    if (!useDRM) return r;
    const char* current = std::getenv("LIMA_DEBUG");
    std::string flags = current ? current : "";
    if (limaDebugDisablesBoCache(flags)) {
        r.policy = LimaBoCachePolicy::NoBoCacheCaller;
        return r;
    }
    if (!limaNoBoCacheRequested()) {
        r.policy = LimaBoCachePolicy::CacheOn;
        return r;
    }
    // Appending to "all" would stop Mesa recognizing it; handled above.
    if (!flags.empty()) flags += ',';
    flags += "nobocache";
    r.ok = ::setenv("LIMA_DEBUG", flags.c_str(), 1) == 0;
    r.policy = LimaBoCachePolicy::NoBoCacheSwitch;
    return r;
}

inline const char* describeLimaBufferPolicy(LimaBoCachePolicy p) {
    switch (p) {
    case LimaBoCachePolicy::NotDrm: return "not a DRM client, LIMA_DEBUG untouched";
    case LimaBoCachePolicy::CacheOn: return "BO cache ON (default; WILLEQ_LIMA_NOBOCACHE=1 disables it)";
    case LimaBoCachePolicy::NoBoCacheCaller: return "BO cache OFF (caller's LIMA_DEBUG)";
    case LimaBoCachePolicy::NoBoCacheSwitch: return "BO cache OFF (WILLEQ_LIMA_NOBOCACHE=1 appended nobocache)";
    }
    return "unknown";
}

} // namespace Graphics
} // namespace EQT
