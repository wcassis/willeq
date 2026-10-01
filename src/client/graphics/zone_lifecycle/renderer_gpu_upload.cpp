#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
#include "client/graphics/gpu_buffers_legacy.h"  // B962
#include "common/logging.h"

#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

// Bridge functions for GLES2 static VBO management.
// Defined in COpenGLES2Driver.cpp (compiled into Irrlicht library), declared here
// to avoid including the full COpenGLES2Driver.h (which depends on Irrlicht-internal headers).
#ifdef EQT_HAS_GLES2
extern void gles2RegisterExternalHWBuffer(void* driver, const void* meshBuffer,
                                           unsigned int vbo, unsigned int ebo,
                                           unsigned int vertexCount, unsigned int indexCount,
                                           int vertexType);
#endif

#include <utility>

namespace EQT {
namespace Graphics {

#ifdef EQT_HAS_GLES2
void ZoneLifecycleComponent::initGPUUploadThread() {
    if (!ctx_.device) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: no device, skipping");
        return;
    }
    if (!ctx_.driver) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: no driver, skipping");
        return;
    }
    if (ctx_.driver->getDriverType() != irr::video::EDT_OGLES2) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: driver type {} != EDT_OGLES2 ({}), skipping",
                 static_cast<int>(ctx_.driver->getDriverType()), static_cast<int>(irr::video::EDT_OGLES2));
        return;
    }

    // B962: the thread's ONLY client is the legacy per-region-node path's VBO
    // upload (createZoneMesh()/rebuildRegionMesh()). With the zone pools on
    // there are no region nodes and no region VBOs, but the shared EGL context
    // was created anyway: a second Lima context with its own PLB and tile-heap
    // GEM buffers (refactor/B962/README.md) for nothing. Not created then.
    if (zonePoolsEnabled() && !gpuBuffersLegacy()) {
        LOG_INFO(MOD_GRAPHICS, "GPU upload thread: not created (zone pools on -- no region VBOs to upload)");
        return;
    }

#ifdef EQT_HAS_DRM
    // Get EGL handles from current context (avoids dependency on internal Irrlicht headers)
    EGLDisplay eglDisplay = eglGetCurrentDisplay();
    EGLContext eglContext = eglGetCurrentContext();
    if (eglDisplay == EGL_NO_DISPLAY || eglContext == EGL_NO_CONTEXT) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: no current EGL context, skipping");
        return;
    }

    // Retrieve the EGLConfig associated with the current context
    EGLint configId = 0;
    eglQueryContext(eglDisplay, eglContext, EGL_CONFIG_ID, &configId);
    EGLConfig eglConfig = nullptr;
    EGLint numConfigs = 0;
    EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
    eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs);
    if (numConfigs == 0 || !eglConfig) {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: failed to resolve EGLConfig, skipping");
        return;
    }

    ctx_.gpuUploadThread = std::make_unique<GPUUploadThread>();
    if (ctx_.gpuUploadThread->init(eglDisplay, eglContext, eglConfig)) {
        ctx_.gpuUploadThread->start();
    } else {
        LOG_WARN(MOD_GRAPHICS, "GPU upload thread: init failed, falling back to render-thread uploads");
        ctx_.gpuUploadThread.reset();
    }
#else
    LOG_DEBUG(MOD_GRAPHICS, "GPU upload thread: DRM not available, skipping");
#endif
}

void ZoneLifecycleComponent::processCompletedUploads() {
    if (!ctx_.gpuUploadThread || !ctx_.gpuUploadThread->isAvailable())
        return;

    auto results = ctx_.gpuUploadThread->pollResults();
    if (results.empty())
        return;

    // Resolve EGL fence wait function pointer (cached, one-time)
    static auto eglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
        eglGetProcAddress("eglClientWaitSyncKHR"));
    static auto eglDestroySyncKHR = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
        eglGetProcAddress("eglDestroySyncKHR"));

    EGLDisplay display = ctx_.gpuUploadThread->getEGLDisplay();

    for (auto& result : results) {
        // Wait on GPU fence (should be near-instant if upload already finished)
        if (result.fence != EGL_NO_SYNC_KHR && eglClientWaitSyncKHR && eglDestroySyncKHR) {
            EGLint waitResult = eglClientWaitSyncKHR(display, result.fence,
                                                      EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                                      EGL_FOREVER_KHR);
            if (waitResult == EGL_FALSE) {
                LOG_WARN(MOD_GRAPHICS, "GPUUpload: fence wait failed for request {}", result.requestId);
            }
            eglDestroySyncKHR(display, result.fence);
        }

        // T475 (board B670): the Texture / CompressedTexture arm that stood
        // here is deleted. It registered an uploaded texture and handed it to
        // the regions, doors and PVS objects queued as waiting for it; its
        // only producer (the cache's async upload queue, callbackKey high
        // byte 3) was made synchronous by Batch T's T04 (aaee58d1) and deleted
        // by T326 (77c842b4), so it never ran (T473). With it went the
        // pending-texture maps, doorTextureRebuildQueue_ and
        // objectTextureRebuildQueue_ with their Priority 8/9 and P1.6/P1.7
        // drains, textureRebuildQueue_, and B640's missing rebuildSingleDoor()
        // branch. A texture missing at mesh build does not arrive later: B671.
        switch (result.type) {
            case UploadRequestType::VertexBuffer: {
                // Decode callbackKey: high 16 bits = buffer index, low 48 bits = region index
                uint32_t bufferIdx = static_cast<uint32_t>(result.callbackKey >> 48);
                size_t regionIdx = static_cast<size_t>(result.callbackKey & 0xFFFFFFFFFFFFULL);
                auto it = ctx_.regionMeshNodes.find(regionIdx);
                if (it != ctx_.regionMeshNodes.end() && it->second) {
                    irr::scene::IMesh* mesh = it->second->getMesh();
                    if (mesh && bufferIdx < mesh->getMeshBufferCount()) {
                        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(bufferIdx);
                        if (buf) {
                            gles2RegisterExternalHWBuffer(
                                ctx_.driver, buf,
                                result.vbo, result.ebo,
                                result.vertexCount, result.indexCount,
                                static_cast<int>(buf->getVertexType()));
                            LOG_DEBUG(MOD_GRAPHICS, "GPUUpload: VBO for region {} buf {} ready (verts={}, indices={})",
                                      regionIdx, bufferIdx, result.vertexCount, result.indexCount);
                        }
                    }
                }
                owner_.pendingVBOUploads_.erase(regionIdx);
                break;
            }
        }
    }
}

void ZoneLifecycleComponent::drainGPUResults() {
    if (!ctx_.gpuUploadThread || !ctx_.gpuUploadThread->isAvailable())
        return;

    auto results = ctx_.gpuUploadThread->pollResults();
    for (auto& r : results)
        owner_.pendingGPUResults_.push_back(std::move(r));
}

bool ZoneLifecycleComponent::processOneGPUResult() {
    if (owner_.pendingGPUResults_.empty()) return false;

    auto result = std::move(owner_.pendingGPUResults_.front());
    owner_.pendingGPUResults_.erase(owner_.pendingGPUResults_.begin());

    // Resolve EGL fence wait function pointer (cached, one-time)
    static auto eglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
        eglGetProcAddress("eglClientWaitSyncKHR"));
    static auto eglDestroySyncKHR = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
        eglGetProcAddress("eglDestroySyncKHR"));

    EGLDisplay display = ctx_.gpuUploadThread->getEGLDisplay();

    // Wait on GPU fence (should be near-instant if upload already finished)
    if (result.fence != EGL_NO_SYNC_KHR && eglClientWaitSyncKHR && eglDestroySyncKHR) {
        EGLint waitResult = eglClientWaitSyncKHR(display, result.fence,
                                                  EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                                  EGL_FOREVER_KHR);
        if (waitResult == EGL_FALSE) {
            LOG_WARN(MOD_GRAPHICS, "GPUUpload processOne: fence wait failed for request {}", result.requestId);
        }
        eglDestroySyncKHR(display, result.fence);
    }

    // T475: the texture arm is gone here too -- see processCompletedUploads().
    switch (result.type) {
        case UploadRequestType::VertexBuffer: {
            uint32_t bufferIdx = static_cast<uint32_t>(result.callbackKey >> 48);
            size_t regionIdx = static_cast<size_t>(result.callbackKey & 0xFFFFFFFFFFFFULL);
            auto it = ctx_.regionMeshNodes.find(regionIdx);
            if (it != ctx_.regionMeshNodes.end() && it->second) {
                irr::scene::IMesh* mesh = it->second->getMesh();
                if (mesh && bufferIdx < mesh->getMeshBufferCount()) {
                    irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(bufferIdx);
                    if (buf) {
                        gles2RegisterExternalHWBuffer(
                            ctx_.driver, buf,
                            result.vbo, result.ebo,
                            result.vertexCount, result.indexCount,
                            static_cast<int>(buf->getVertexType()));
                        LOG_DEBUG(MOD_GRAPHICS, "GPUUpload processOne: VBO for region {} buf {} ready (verts={}, indices={})",
                                  regionIdx, bufferIdx, result.vertexCount, result.indexCount);
                    }
                }
            }
            owner_.pendingVBOUploads_.erase(regionIdx);
            break;
        }
    }

    return true;
}
#endif // EQT_HAS_GLES2

void ZoneLifecycleComponent::toggleGPUUploadThread() {
    if (owner_.isLoading()) return;
#ifdef EQT_HAS_GLES2
    gpuUploadEnabled_ = !gpuUploadEnabled_;
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: {}", gpuUploadEnabled_ ? "ENABLED" : "DISABLED");
    // T475: gpuUploadEnabled_ gates createZoneMesh()'s VBO submit
    // (renderer_zone_draw.cpp). The texture cache's copy of the thread pointer
    // that this used to propagate was never read, and is deleted.
#else
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: not available (non-GLES2 build)");
#endif
}

} // namespace Graphics
} // namespace EQT
