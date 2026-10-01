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
#include "client/core/gem_owner_census.h"  // B962

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetGpuTextureStorageUsage(void* driver);  // B962
#endif

namespace ZoneBench {

// Publish the driver's tally (and, once, whether GL_RENDERER is a software
// rasteriser). The benchmark is single-threaded and owns the context.
inline void publishGpuTally(void* driver) {
#ifdef EQT_HAS_GLES2
    if (!driver) return;
    static bool checked = false;
    static GLint depthBits = -1, stencilBits = 0;
    if (!checked) {
        const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (r) {
            checked = true;
            eqt::memory::publishGpuSoftwareRasteriser(
                strstr(r, "llvmpipe") || strstr(r, "softpipe") || strstr(r, "swrast") ||
                strstr(r, "lavapipe"));
            // B962: the window surface's depth/stencil (read with FBO 0 bound).
            GLint fbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            if (fbo == 0) {
                glGetIntegerv(GL_DEPTH_BITS, &depthBits);
                glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
            }
        }
    }
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);  // the benchmark never resizes; FBO 0's size at the first point
    static GLint surfaceW = 0, surfaceH = 0;
    if (!surfaceW && vp[2] > 0) { surfaceW = vp[2]; surfaceH = vp[3]; }
    eqt::memory::publishGpuLogicalTally(
        gles2GetGpuTextureStorageUsage(driver), gles2GetHWBufferMemoryUsage(driver),
        depthBits < 0 ? 0 : eqt::memory::depthStencilBytesFor(surfaceW, surfaceH, depthBits, stencilBits));
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
           eqt::memory::gpuBuffersToLogLine(point, s.zone, s.gpuBuffers, s.ownedRssAnonBytes).c_str());
    fflush(stdout);
    eqt::memory::gemCensusDump(point);  // B962: the census shim's owner table, if preloaded
    return s;
}

} // namespace ZoneBench
