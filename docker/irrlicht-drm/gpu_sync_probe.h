// gpu_sync_probe.h — P52 GPU-sync characterization instrumentation.
//
// Flag-gated per-phase GPU/CPU timing, following gl_call_counters.h's
// zero-cost-when-off precedent (P38): every call site guards on
// g_gpuSyncProbe.enabled (or the eqtGPUSyncProbeEnabled() accessor) before
// doing any work. Off by default; enabled only by the benchmark tool
// (gles2_zone_benchmark --gpu-sync-probe), never by production (willeq
// never calls eqtEnableGPUSyncProbe()).
//
// Two independent measurement paths, both gated by the same flag:
//
//  1. GPU-side: GL_EXT_disjoint_timer_query (GL_TIME_ELAPSED_EXT), when the
//     driver advertises it — probed at runtime via eglGetProcAddress() +
//     the GL_EXTENSIONS string, never assumed (llvmpipe and Lima may
//     differ). Wraps the zone-pass / entity-pass / particles+UI draw
//     phases with glBeginQueryEXT/glEndQueryEXT, double-buffered across
//     frames so readback (glGetQueryObjectuivEXT) never stalls the frame
//     that issued the query — if a phase's result isn't ready yet by the
//     time it's read back (one frame later), it is simply dropped for that
//     frame rather than blocked on.
//
//  2. CPU-side sync-point attribution (always collected when enabled,
//     independent of GPU query support): which calls actually block, and
//     for how long — eglSwapBuffers, the DRM waitForFlipComplete() poll,
//     glFinish()/glReadPixels() (where the caller already uses them, e.g.
//     the benchmark's --dump-frames screenshot path). These are reported
//     separately from the GPU-timed phases because they are not
//     meaningfully GPU-timer-queryable: they are CPU-side EGL/DRM
//     synchronization points, not GPU command-stream work.
//
// This header declares plain-typed free functions only (int/double/bool —
// no GL/EGL types), the same interop shape as the existing
// gles2CreateStaticHWBuffer() precedent (COpenGLES2Driver.cpp defines it,
// application-layer callers `extern`-declare it locally without including
// any GL headers or this header). docker/irrlicht-drm/COpenGLES2Driver.cpp
// and both CIrrDeviceFB.cpp forks (ARM: docker/irrlicht-drm/, x86:
// docker/irrlicht-drm-x86/) may include this header directly, since they
// already sit in the driver-internal build context.
#pragma once

#include <cstdint>

namespace irr {
namespace video {

// Phase indices, passed as plain `int` across the free-function API so
// application-layer TUs don't need to include this header just to name a
// phase — see GPUSyncPhase below for the canonical values.
enum class GPUSyncPhase : int {
    ZonePass    = 0,  // manual zone geometry draw (drawZoneGeometrySorted/WithPortals)
    EntityPass  = 1,  // Irrlicht SOLID pass proper (entities, doors — zone nodes are invisible when manual zone draw is active)
    ParticlesUI = 2,  // particle render (unified + legacy) + GUI drawAll
    Swap        = 3,  // driver_->endScene() -> Device->present() (eglSwapBuffers / DRM page flip)
    Count       = 4
};

struct GPUSyncProbe {
    bool enabled = false;                 // master flag — off means zero overhead
    bool capabilityChecked = false;       // eqtProbeGPUTimerQuerySupport() ran once
    bool hasDisjointTimerQuery = false;   // GL_EXT_disjoint_timer_query available (runtime-probed, never assumed)

    // GPU-side (only populated when hasDisjointTimerQuery)
    uint64_t lastGPUNanos[(int)GPUSyncPhase::Count] = {};
    bool lastGPUValid[(int)GPUSyncPhase::Count] = {};

    // CPU-side sync-point attribution, milliseconds, most recent frame —
    // collected whenever enabled==true regardless of GPU query support.
    double lastSwapBuffersMs = 0.0;       // eglSwapBuffers() wall time
    double lastFlipWaitMs = 0.0;          // waitForFlipComplete() wall time (ARM/DRM only; always 0 on x86 surfaceless/X11 — no DRM page flip there)
    double lastFinishReadPixelsMs = 0.0;  // glFinish()/glReadPixels() wall time, recorded by callers that already use them (e.g. benchmark --dump-frames)
};

// Single process-wide instance (the driver is a process singleton in this
// codebase), defined in COpenGLES2Driver.cpp.
extern GPUSyncProbe g_gpuSyncProbe;

} // namespace video
} // namespace irr

// ---------------------------------------------------------------------
// Free-function API (plain types only — see file comment above).
// ---------------------------------------------------------------------

void eqtEnableGPUSyncProbe(bool enable);
bool eqtGPUSyncProbeEnabled();

// Runtime capability probe. Safe to call unconditionally (it no-ops when
// the master flag is off, and only runs once); call it once after the GL
// context is current (COpenGLES2Driver construction is where this is
// wired). Does NOT enable the probe itself.
void eqtProbeGPUTimerQuerySupport();
bool eqtGPUTimerQuerySupported();

// Per-phase GPU timer query begin/end. `phase` is a GPUSyncPhase cast to
// int. No-ops (single bool check) when the probe is disabled OR the
// capability probe found no GL_EXT_disjoint_timer_query support.
void eqtGPUPhaseBegin(int phase);
void eqtGPUPhaseEnd(int phase);

// Collects any GPU query results that became available this frame
// (non-blocking — skips any not yet ready) and flips the double-buffer for
// the next frame's writes. Call exactly once per frame, after the Swap
// phase's eqtGPUPhaseEnd(). No-op when disabled.
void eqtGPUPhaseCollectResults();

// Read back the most recent GPU time for a phase, in nanoseconds. Returns
// false if never measured (no GPU query support, probe disabled, or the
// phase's query wasn't ready by readback time).
bool eqtGetGPUPhaseNanos(int phase, uint64_t* outNanos);

// CPU-side sync-point recording — called from CIrrDeviceFB.cpp (both ARM
// and x86 forks) around the specific blocking calls, and from the
// benchmark tool around its own glFinish()/glReadPixels() calls. No-ops
// when disabled.
void eqtRecordSwapBuffersMs(double ms);
void eqtRecordFlipWaitMs(double ms);
void eqtRecordFinishReadPixelsMs(double ms);

// Accessors for the benchmark tool's CSV columns / renderer diagnostics.
double eqtGetLastSwapBuffersMs();
double eqtGetLastFlipWaitMs();
double eqtGetLastFinishReadPixelsMs();
