#pragma once
// B1054 (#374): DRM card-selection decision logic, extracted from
// CIrrDeviceFB::initDRM() (docker/irrlicht-drm/CIrrDeviceFB.cpp and its
// docker/irrlicht-drm-x86 fork, identical loops) for unit testing.
//
// Background (measured 2026-10-13 on the ThinkPad, card0 = NVIDIA Quadro
// with no connected output, card1 = Intel UHD 630 with eDP-1): initDRM()
// tried /dev/dri/card0 then card1 until one OPENED. A card that opens but
// has no connected connector ended the search and the device fell back to
// surfaceless EGL (whose "llvmpipe" log line was wrong too -- on that box
// the surfaceless vendor was NVIDIA). The Intel panel on card1 was never
// tried.
//
// This is the pure decision function: given an ordered list of candidate
// device paths and two callbacks supplied by the caller (open the card,
// and, for an open card, report whether it has a usable display), pick the
// FIRST candidate that both opens AND has a usable display (connected
// connector + a CRTC). WILLEQ_DRM_DEVICE, when set, is an explicit
// override: exactly that path is tried, and a failure is reported with the
// reason -- it is NEVER silently widened back to the auto list.
//
// Deliberately dependency-free (no libdrm/gbm/EGL/Irrlicht headers) so it
// compiles and runs anywhere, including a plain host unit test binary --
// the same shape as drm_mode_select.h (P49). Both device forks include this
// header; the x86 build script copies it into the Irrlicht build area like
// drm_mode_select.h.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#ifdef __has_include
#    if __has_include(<dirent.h>) && __has_include(<unistd.h>)
#        define EQT_DRM_CARD_SELECT_HAS_DIR
#        include <dirent.h>
#        include <unistd.h>
#    endif
#endif

namespace eqt_drm {

// What the caller learned about one card after opening it. Filled by the
// card-check callback (in CIrrDeviceFB that is the
// drmModeGetResources/connector/CRTC probe the old inline loop did); the
// decision below only reads these three fields.
struct DRMCardState {
    bool hasConnectedConnector = false;
    bool hasCrtc = false;
    // The winning connector's DRM connector type id (DRM_MODE_CONNECTOR_*),
    // for the "connector=<n>" log field; 0 when there is no connected
    // connector.
    int connectorType = 0;
};

struct DRMCardSelection {
    // Index into the candidate list. -1 when `ok` is false.
    int index = -1;
    bool ok = false;
    // Why selection failed (diagnostic suffix for the caller's error line):
    // "" (not applicable when ok), "no candidates" (empty list),
    // "no card opens", "no card with a usable display" (every card that
    // opened had no connected connector or no CRTC), "card does not open"
    // (override path failed to open), "card has no usable display"
    // (override path opened but the card check failed).
    std::string reason;
};

// Candidate ordering for the auto (non-override) list. card0 < card1 < ...
// numerically: "card10" sorts after "card2", which a plain string compare
// would get wrong. The comparison looks at the BASENAME (after the last
// '/') because callers pass full paths; anything that is not "card<N>"
// (e.g. a caller-supplied absolute path to a render node) keeps its string
// order, so an override-style path stays exactly where the caller put it.
inline bool cardPathLess(const std::string& a, const std::string& b) {
    const std::string ba = a.substr(a.find_last_of('/') + 1);
    const std::string bb = b.substr(b.find_last_of('/') + 1);
    const size_t kPrefix = 4;  // strlen("card")
    if (ba.size() > kPrefix && bb.size() > kPrefix &&
        ba.compare(0, kPrefix, "card") == 0 && bb.compare(0, kPrefix, "card") == 0) {
        int ia = 0, ib = 0;
        if (std::sscanf(ba.c_str() + kPrefix, "%d", &ia) == 1 &&
            std::sscanf(bb.c_str() + kPrefix, "%d", &ib) == 1 && ia != ib)
            return ia < ib;
    }
    return ba < bb;
}

#ifdef EQT_DRM_CARD_SELECT_HAS_DIR
// Enumerate <dir>/card* in ascending card-number order. The directory may
// be missing entirely (headless runner); opendir returns null and we simply
// have no candidates. Non-card entries (renderD128, by-path, ...) are
// dropped by the "card" + all-digits check.
inline std::vector<std::string> enumerateDRMCards(const std::string& dir = "/dev/dri") {
    std::vector<std::string> names;
    DIR* dp = ::opendir(dir.c_str());
    if (dp) {
        while (dirent* ent = ::readdir(dp)) {
            std::string name(ent->d_name);
            if (name.size() <= 4 || name.compare(0, 4, "card") != 0)
                continue;
            if (std::all_of(name.begin() + 4, name.end(), [](char c) {
                    return c >= '0' && c <= '9';
                }))
                names.push_back(name);
        }
        ::closedir(dp);
    }
    std::vector<std::string> paths;
    paths.reserve(names.size());
    for (const std::string& n : names)
        paths.push_back(dir + "/" + n);
    std::sort(paths.begin(), paths.end(), cardPathLess);
    return paths;
}
#endif

// The ordered candidate list for auto selection: WILLEQ_DRM_DEVICE, when
// set to a non-empty path, is an explicit override -- that single path is
// the whole list (a failure on it is reported, never widened back to the
// auto list). Otherwise /dev/dri/card* in numeric order.
inline std::vector<std::string> drmCardCandidates(const std::string& dir = "/dev/dri") {
    const char* override = std::getenv("WILLEQ_DRM_DEVICE");
    if (override && override[0] != '\0')
        return {override};
#ifdef EQT_DRM_CARD_SELECT_HAS_DIR
    return enumerateDRMCards(dir);
#else
    (void)dir;
    return {};
#endif
}

// The decision itself. `openCard(path) -> fd (-1 on failure)` and
// `checkCard(fd, path) -> state` are the caller's libdrm side; everything
// here is pure and callback-fakeable. Returns the first candidate that
// opens AND reports hasConnectedConnector && hasCrtc; a card that opens
// but fails the check is closed and the search CONTINUES (the pre-B1054
// defect stopped at the first opened card). `override` marks the
// WILLEQ_DRM_DEVICE case (a single, caller-mandated path): a failure there
// carries the override-specific reason, so the caller's error line can
// say "explicit override, no fallback" instead of the auto-list reasons.
inline DRMCardSelection selectDRMCard(
    const std::vector<std::string>& candidates,
    const std::function<int(const std::string&)>& openCard,
    const std::function<DRMCardState(int, const std::string&)>& checkCard,
    bool override = false) {
    DRMCardSelection result;
    if (candidates.empty()) {
        result.reason = "no candidates";
        return result;
    }

    bool anyOpened = false;
    for (size_t i = 0; i < candidates.size(); ++i) {
        int fd = openCard(candidates[i]);
        if (fd < 0)
            continue;
        anyOpened = true;
        DRMCardState state = checkCard(fd, candidates[i]);
        // B1061 (#381): the probe fd is closed on EVERY path, the winner's
        // included. The caller re-opens the winning card for its own fd; if
        // the probe fd stayed open it would be the FIRST open of that node
        // and therefore hold DRM master, leaving the caller's fd without it
        // -- every drmModeSetCrtc then fails (measured on the XU4 play rig
        // after B1054: frames rendered, nothing reached the HDMI).
        if (fd > 0)  // fd 0 is not a real card; closing it would close stdin
            ::close(fd);
        if (state.hasConnectedConnector && state.hasCrtc) {
            result.index = static_cast<int>(i);
            result.ok = true;
            return result;
        }
    }

    if (override)
        result.reason = anyOpened ? "card has no usable display"
                                  : "card does not open";
    else if (!anyOpened)
        result.reason = "no card opens";
    else
        result.reason = "no card with a usable display";
    return result;
}

}  // namespace eqt_drm
