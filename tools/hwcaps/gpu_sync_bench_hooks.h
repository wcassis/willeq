// gpu_sync_bench_hooks.h — P52: thin gles2_zone_benchmark.cpp call-site
// wrappers around docker/irrlicht-drm/gpu_sync_probe.h's free functions.
//
// Extracted into its own header (rather than inlined at each call site in
// gles2_zone_benchmark.cpp) specifically to keep that file's own line count
// from growing further -- it was already at 3015 lines (over the
// scripts/refactor/check_file_sizes.sh FAIL threshold of 3000) at this
// unit's branch point, a pre-existing condition from prior units, not
// something this unit introduced. See gpu_sync_probe.h for the full design
// rationale (flag-gated, zero overhead when off, GPU timer query +
// CPU-side sync-point attribution).
#pragma once

#include "gpu_sync_probe.h"
#include "zone_bench_metrics.h"

#include <chrono>
#include <cstdint>
#include <utility>

namespace ZoneBench {

// Wraps the combined zone+entity draw-submission span as GPUSyncPhase::ZonePass
// (index 0) -- a proxy scope. This offline tool submits draw calls directly
// (collectDrawEntries/renderDrawEntries), not via IrrlichtRenderer's manual-
// zone-draw/RenderPassTimer machinery, so it cannot reproduce the production
// zone-pass/entity-pass split (that split only exists in
// src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp, exercised only by the real
// `willeq` render loop). What this CAN do -- and what the x86 sanity check
// uses it for -- is prove the GPU timer query mechanics (capability probe,
// begin/end, double-buffered non-blocking readback) round-trip correctly on
// llvmpipe. No-op when the probe is disabled or unsupported.
inline void gpuSyncBeginSubmissionPhase() { eqtGPUPhaseBegin(0); }
inline void gpuSyncEndSubmissionPhase() { eqtGPUPhaseEnd(0); }

// Times a glFinish()-shaped sync call -- the CPU-blocks-on-GPU-sync point
// named in the P52 plan (eglSwapBuffers/glFinish-glReadPixels/DRM
// flip-wait) -- when the probe is enabled; always invokes `finishFn` either
// way.
template <typename FinishFn>
inline void gpuSyncTimedFinish(bool enabled, FinishFn&& finishFn) {
    if (!enabled) { finishFn(); return; }
    const auto t0 = std::chrono::steady_clock::now();
    finishFn();
    const auto t1 = std::chrono::steady_clock::now();
    eqtRecordFinishReadPixelsMs(std::chrono::duration<double, std::milli>(t1 - t0).count());
}

// Wraps the Swap phase (GPUSyncPhase::Swap, index 3) around a caller-supplied
// swap call, collects any GPU query results that became available
// (non-blocking, double-buffered -- see gpu_sync_probe.h), and, when
// measuring and the probe is enabled, patches the frame record already
// pushed by MetricsCollector::endFrame() with this frame's GPU-sync stats
// (mirrors setSwapMsForLastFrame()'s existing "patch after the fact"
// pattern -- the P08 comment at the call site explains why totalMs is
// stamped before this runs).
template <typename SwapFn>
inline void gpuSyncWrapSwap(SwapFn&& swap, MetricsCollector& metrics, bool measuring, bool enabled) {
    eqtGPUPhaseBegin(3);
    swap();
    eqtGPUPhaseEnd(3);
    eqtGPUPhaseCollectResults();
    if (!measuring || !enabled) return;

    uint64_t zoneNs = 0, entityNs = 0, particlesNs = 0, swapNs = 0;
    const bool haveZone = eqtGetGPUPhaseNanos(0, &zoneNs);
    const bool haveEntity = eqtGetGPUPhaseNanos(1, &entityNs);
    const bool haveParticles = eqtGetGPUPhaseNanos(2, &particlesNs);
    const bool haveSwap = eqtGetGPUPhaseNanos(3, &swapNs);
    metrics.setGPUSyncStatsForLastFrame(
        haveZone ? static_cast<int64_t>(zoneNs) : -1,
        haveEntity ? static_cast<int64_t>(entityNs) : -1,
        haveParticles ? static_cast<int64_t>(particlesNs) : -1,
        haveSwap ? static_cast<int64_t>(swapNs) : -1,
        static_cast<float>(eqtGetLastSwapBuffersMs()),
        static_cast<float>(eqtGetLastFlipWaitMs()),
        static_cast<float>(eqtGetLastFinishReadPixelsMs()));
}

} // namespace ZoneBench
