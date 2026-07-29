// gl_call_counters.h — P38 audit instrumentation.
//
// Optional per-frame counters for glBindTexture / glTexParameteri call
// volume, used by the P38 time-budget audit to quantify driver-side state
// churn (refactor/perf_truth_audit.md). Disabled by default
// (g_glCounters.enabled == false): every call site guards the increment
// behind this single bool check, so the cost when off is one predicted-false
// branch per texture bind / glTexParameteri call (a handful per draw call at
// most) — no measurable overhead, no new allocation, no syscalls. Enabled
// only by the benchmark tool (gles2_zone_benchmark --gl-counters), never by
// production (willeq never calls eqtEnableGLCallCounters()).
#pragma once

namespace irr {
namespace video {

struct GLCallCounters {
    bool enabled = false;
    unsigned texBinds = 0;   // glBindTexture calls (real GL calls, not cache hits)
    unsigned texParams = 0;  // glTexParameteri calls
};

// Single process-wide instance — the driver is a singleton in this codebase
// (one COpenGLES2Driver per process), so a global avoids threading the
// counters through every call site's signature.
extern GLCallCounters g_glCounters;

} // namespace video
} // namespace irr

// Small free-function API for the benchmark tool (avoids reaching into the
// irr::video namespace / linking against driver internals from tools/hwcaps).
inline void eqtEnableGLCallCounters(bool enable) {
    irr::video::g_glCounters.enabled = enable;
}
inline void eqtResetGLCallCounters() {
    irr::video::g_glCounters.texBinds = 0;
    irr::video::g_glCounters.texParams = 0;
}
inline unsigned eqtGetGLTexBinds() { return irr::video::g_glCounters.texBinds; }
inline unsigned eqtGetGLTexParams() { return irr::video::g_glCounters.texParams; }
