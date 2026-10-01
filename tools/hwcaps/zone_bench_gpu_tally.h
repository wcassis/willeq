#pragma once

// B955 (GitLab #273): the benchmark's side of the gpu_buffers ledger row
// (include/client/core/gpu_buffer_census.h). The client publishes the
// driver's GPU buffer tally from src/client/graphics/diagnostics/
// gpu_buffer_tally.cpp; the benchmark tools do not link the renderer TUs, so
// this header does the same two calls against the driver they link directly,
// and prints the [memgpu] line beside every [memledger] line, the client's
// schema.

#include <cstdio>
#include <cstring>
#include <string>

#include "client/core/memory_ledger.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
#endif

namespace ZoneBench {

// Publish the driver's tally (and, once, whether GL_RENDERER is a software
// rasteriser). The benchmark is single-threaded and owns the context.
inline void publishGpuTally(void* driver) {
#ifdef EQT_HAS_GLES2
    if (!driver) return;
    static bool checked = false;
    if (!checked) {
        const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (r) {
            checked = true;
            eqt::memory::publishGpuSoftwareRasteriser(
                strstr(r, "llvmpipe") || strstr(r, "softpipe") || strstr(r, "swrast") ||
                strstr(r, "lavapipe"));
        }
    }
    eqt::memory::publishGpuLogicalTally(gles2GetGpuTextureMemoryUsage(driver),
                                        gles2GetHWBufferMemoryUsage(driver));
#else
    (void)driver;
#endif
}

// Publish, snapshot and print the [memledger] + [memgpu] pair for `point`.
// Returns the snapshot for callers that read more from it.
inline eqt::memory::Snapshot printLedgerPoint(const char* point, void* driver) {
    publishGpuTally(driver);
    eqt::memory::Snapshot s = eqt::memory::snapshot(point);
    printf("%s\n%s\n", eqt::memory::toLogLine(s).c_str(),
           eqt::memory::gpuBuffersToLogLine(point, s.zone, s.gpuBuffers, s.ownedBytes).c_str());
    fflush(stdout);
    return s;
}

} // namespace ZoneBench
