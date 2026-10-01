// B955 (GitLab #273): publish the driver's logical GPU buffer tally to the
// memory ledger's gpu_buffers row. See the header.

#include "client/graphics/diagnostics/gpu_buffer_tally.h"
#include "client/core/gpu_buffer_census.h"

#include <atomic>
#include <chrono>
#include <cstring>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
#endif

namespace EQT {
namespace Graphics {

namespace {
// The loading thread and the render thread both call this (each while it owns
// the context); the flag makes the one-time GL_RENDERER read race-free.
std::atomic<bool> g_rendererChecked{false};
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
        }
    }
    eqt::memory::publishGpuLogicalTally(gles2GetGpuTextureMemoryUsage(driver),
                                        gles2GetHWBufferMemoryUsage(driver));
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
