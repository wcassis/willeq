// B955 (GitLab #273): publish the driver's logical GPU buffer tally to the
// memory ledger's gpu_buffers row. See the header.

#include "client/graphics/diagnostics/gpu_buffer_tally.h"
#include "client/core/gpu_buffer_census.h"

#include <IVideoDriver.h>  // B962: getScreenSize() for the depth/stencil row

#include <atomic>
#include <chrono>
#include <cstring>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetGpuTextureStorageUsage(void* driver);  // B962
#endif

namespace EQT {
namespace Graphics {

namespace {
// The loading thread and the render thread both call this (each while it owns
// the context); the flag makes the one-time GL_RENDERER read race-free.
std::atomic<bool> g_rendererChecked{false};
// B962: the default framebuffer's depth/stencil bits, read once with it bound
// (the first call, before any render target exists).
std::atomic<int> g_depthBits{-1};
std::atomic<int> g_stencilBits{0};
}

void publishGpuBufferTally(irr::video::IVideoDriver* driver) {
#ifdef EQT_HAS_GLES2
    if (!driver) return;
    if (!g_rendererChecked.load(std::memory_order_relaxed)) {
        const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (r) {
            g_rendererChecked.store(true, std::memory_order_relaxed);
            const bool software = strstr(r, "llvmpipe") || strstr(r, "softpipe") ||
                                  strstr(r, "swrast") || strstr(r, "lavapipe");
            eqt::memory::publishGpuSoftwareRasteriser(software);
            GLint fbo = 0, depth = 0, stencil = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            if (fbo == 0) {
                glGetIntegerv(GL_DEPTH_BITS, &depth);
                glGetIntegerv(GL_STENCIL_BITS, &stencil);
                g_stencilBits.store(stencil, std::memory_order_relaxed);
                g_depthBits.store(depth, std::memory_order_relaxed);
            }
        }
    }
    const int depthBits = g_depthBits.load(std::memory_order_relaxed);
    const auto screen = driver->getScreenSize();
    eqt::memory::publishGpuLogicalTally(
        gles2GetGpuTextureStorageUsage(driver), gles2GetHWBufferMemoryUsage(driver),
        depthBits < 0 ? 0 : eqt::memory::depthStencilBytesFor(screen.Width, screen.Height, depthBits,
                                                              g_stencilBits.load(std::memory_order_relaxed)));
#else
    (void)driver;
#endif
}

void publishGpuBufferTallyThrottled(irr::video::IVideoDriver* driver, int periodMs) {
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (last.time_since_epoch().count() != 0 &&
        now - last < std::chrono::milliseconds(periodMs)) {
        return;
    }
    last = now;
    publishGpuBufferTally(driver);
}

} // namespace Graphics
} // namespace EQT
