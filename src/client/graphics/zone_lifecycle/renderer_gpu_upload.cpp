#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
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
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes = 0);
extern void gles2RegisterExternalHWBuffer(void* driver, const void* meshBuffer,
                                           unsigned int vbo, unsigned int ebo,
                                           unsigned int vertexCount, unsigned int indexCount,
                                           int vertexType);
#endif

#include <algorithm>
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
        // Wire to subsystems already created
        if (ctx_.constrainedTextureCache)
            ctx_.constrainedTextureCache->setGPUUploadThread(ctx_.gpuUploadThread.get());
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

        switch (result.type) {
            case UploadRequestType::Texture:
            case UploadRequestType::CompressedTexture: {
                // Wrap the externally-uploaded GL texture as an Irrlicht ITexture
                irr::video::ITexture* wrappedTex = nullptr;
                if (result.glTextureName != 0 && !result.textureName.empty()) {
                    wrappedTex = static_cast<irr::video::ITexture*>(
                        gles2WrapTexture(ctx_.driver, result.textureName.c_str(),
                                         result.glTextureName,
                                         result.width, result.height,
                                         result.gpuBytes));
                    LOG_DEBUG(MOD_GRAPHICS, "GPUUpload: texture '{}' ready ({}x{}, {} bytes)",
                              result.textureName, result.width, result.height, result.gpuBytes);
                }

                // Route based on high byte of callbackKey
                uint8_t sourceType = static_cast<uint8_t>(result.callbackKey >> 56);

                if (sourceType == 3) {
                    // Unified constrained cache texture (zone + entity textures)
                    // Register in LRU cache and mesh builder for entity scene node creation
                    if (wrappedTex && ctx_.constrainedTextureCache) {
                        size_t texSize = static_cast<size_t>(result.width) * result.height * 4;
                        ctx_.constrainedTextureCache->registerTexture(result.textureName, wrappedTex, texSize, false);
                        ctx_.constrainedTextureCache->clearPendingAsync(result.textureName);

                        // Also register in mesh builder so entity SceneNodeCreation finds it
                        if (ctx_.entityRenderer) {
                            auto* meshBuilder = ctx_.entityRenderer->getMeshBuilder();
                            if (meshBuilder) {
                                meshBuilder->registerUploadedTexture(result.textureName, wrappedTex, false);
                            }
                        }

                        // Check if any regions were waiting for this texture
                        auto pendIt = pendingTextureRegions_.find(result.textureName);
                        if (pendIt != pendingTextureRegions_.end()) {
                            for (size_t regionIdx : pendIt->second) {
                                // Deduplicate: only add if not already queued
                                bool alreadyQueued = false;
                                for (const auto& entry : owner_.textureRebuildQueue_) {
                                    if (entry.regionIdx == regionIdx) {
                                        alreadyQueued = true;
                                        break;
                                    }
                                }
                                if (!alreadyQueued) {
                                    float distSq = 0.0f;
                                    auto bbIt = ctx_.regionBoundingBoxes.find(regionIdx);
                                    if (bbIt != ctx_.regionBoundingBoxes.end()) {
                                        auto center = bbIt->second.getCenter();
                                        float dx = center.X - ctx_.playerX;
                                        float dy = center.Y - ctx_.playerY;
                                        distSq = dx * dx + dy * dy;
                                    }
                                    // Player's region gets priority (distSq = -1)
                                    if (regionIdx == ctx_.currentPvsRegion) distSq = -1.0f;
                                    owner_.textureRebuildQueue_.push_back({regionIdx, distSq});
                                }
                            }
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload: texture '{}' arrived, queued {} regions for rebuild",
                                     result.textureName, pendIt->second.size());
                            pendingTextureRegions_.erase(pendIt);
                        }

                        // Doors waiting for this texture
                        auto doorPendIt = owner_.pendingTextureDoors_.find(result.textureName);
                        if (doorPendIt != owner_.pendingTextureDoors_.end()) {
                            for (uint8_t doorId : doorPendIt->second) {
                                if (ctx_.doorManager) {
                                    const auto* dv = ctx_.doorManager->getDoor(doorId);
                                    if (dv) ctx_.doorManager->invalidateMeshCache(dv->modelName);
                                }
                                if (std::find(owner_.doorTextureRebuildQueue_.begin(),
                                              owner_.doorTextureRebuildQueue_.end(), doorId) == owner_.doorTextureRebuildQueue_.end()) {
                                    owner_.doorTextureRebuildQueue_.push_back(doorId);
                                }
                            }
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload: texture '{}' arrived, queued {} doors for rebuild",
                                     result.textureName, doorPendIt->second.size());
                            owner_.pendingTextureDoors_.erase(doorPendIt);
                        }

                        // PVS objects waiting for this texture
                        auto objPendIt = owner_.pendingTextureObjects_.find(result.textureName);
                        if (objPendIt != owner_.pendingTextureObjects_.end()) {
                            for (size_t objIdx : objPendIt->second) {
                                if (std::find(owner_.objectTextureRebuildQueue_.begin(),
                                              owner_.objectTextureRebuildQueue_.end(), objIdx) == owner_.objectTextureRebuildQueue_.end()) {
                                    owner_.objectTextureRebuildQueue_.push_back(objIdx);
                                }
                            }
                            LOG_INFO(MOD_GRAPHICS, "GPUUpload: texture '{}' arrived, queued {} objects for rebuild",
                                     result.textureName, objPendIt->second.size());
                            owner_.pendingTextureObjects_.erase(objPendIt);
                        }
                    }
                } else if (sourceType == 4) {
                    // Icon texture — icon loading removed (WindowManager deleted)
                }
                break;
            }

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

    // Sort texture rebuild queue: player's region first (distSq=-1), then nearest
    if (!owner_.textureRebuildQueue_.empty()) {
        std::sort(owner_.textureRebuildQueue_.begin(), owner_.textureRebuildQueue_.end(),
                  [](const IrrlichtRenderer::TextureRebuildEntry& a, const IrrlichtRenderer::TextureRebuildEntry& b) {
                      return a.distanceSq < b.distanceSq;
                  });
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

    switch (result.type) {
        case UploadRequestType::Texture:
        case UploadRequestType::CompressedTexture: {
            // Wrap the externally-uploaded GL texture as an Irrlicht ITexture
            irr::video::ITexture* wrappedTex = nullptr;
            if (result.glTextureName != 0 && !result.textureName.empty()) {
                wrappedTex = static_cast<irr::video::ITexture*>(
                    gles2WrapTexture(ctx_.driver, result.textureName.c_str(),
                                     result.glTextureName,
                                     result.width, result.height,
                                     result.gpuBytes));
                LOG_DEBUG(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' ready ({}x{}, {} bytes)",
                          result.textureName, result.width, result.height, result.gpuBytes);
            }

            // Route based on high byte of callbackKey
            uint8_t sourceType = static_cast<uint8_t>(result.callbackKey >> 56);

            if (sourceType == 3) {
                // Unified constrained cache texture (zone + entity textures)
                if (wrappedTex && ctx_.constrainedTextureCache) {
                    size_t texSize = static_cast<size_t>(result.width) * result.height * 4;
                    ctx_.constrainedTextureCache->registerTexture(result.textureName, wrappedTex, texSize, false);
                    ctx_.constrainedTextureCache->clearPendingAsync(result.textureName);

                    // Also register in mesh builder so entity SceneNodeCreation finds it
                    if (ctx_.entityRenderer) {
                        auto* meshBuilder = ctx_.entityRenderer->getMeshBuilder();
                        if (meshBuilder) {
                            meshBuilder->registerUploadedTexture(result.textureName, wrappedTex, false);
                        }
                    }

                    // Check if any regions were waiting for this texture → queue for rebuild.
                    // T67: this used to also check pendingTextureBuffers_ for a
                    // material-swap shortcut (materialSwapQueue_), but that map's
                    // only writer was ConstrainedMeshCache's background-build path
                    // (finalizeOneMeshResult(), removed with the cache — it was
                    // never constructed in any shipped build), so pendingTextureBuffers_
                    // was always empty and this always fell through to the full-rebuild
                    // branch below. Behavior unchanged; the always-empty lookup is
                    // just gone now. See src/client/graphics/CLAUDE.md for the
                    // reference pointer.
                    auto pendIt = pendingTextureRegions_.find(result.textureName);
                    if (pendIt != pendingTextureRegions_.end()) {
                        for (size_t regionIdx : pendIt->second) {
                            bool alreadyQueued = false;
                            for (const auto& entry : owner_.textureRebuildQueue_) {
                                if (entry.regionIdx == regionIdx) {
                                    alreadyQueued = true;
                                    break;
                                }
                            }
                            if (!alreadyQueued) {
                                float distSq = 0.0f;
                                auto bbIt = ctx_.regionBoundingBoxes.find(regionIdx);
                                if (bbIt != ctx_.regionBoundingBoxes.end()) {
                                    auto center = bbIt->second.getCenter();
                                    float dx = center.X - ctx_.playerX;
                                    float dy = center.Y - ctx_.playerY;
                                    distSq = dx * dx + dy * dy;
                                }
                                if (regionIdx == ctx_.currentPvsRegion) distSq = -1.0f;
                                owner_.textureRebuildQueue_.push_back({regionIdx, distSq});
                            }
                        }
                        LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued {} regions for rebuild",
                                 result.textureName, pendIt->second.size());
                        pendingTextureRegions_.erase(pendIt);
                    }

                    // Doors waiting for this texture
                    auto doorPendIt = owner_.pendingTextureDoors_.find(result.textureName);
                    if (doorPendIt != owner_.pendingTextureDoors_.end()) {
                        for (uint8_t doorId : doorPendIt->second) {
                            if (ctx_.doorManager) {
                                const auto* dv = ctx_.doorManager->getDoor(doorId);
                                if (dv) ctx_.doorManager->invalidateMeshCache(dv->modelName);
                            }
                            if (std::find(owner_.doorTextureRebuildQueue_.begin(),
                                          owner_.doorTextureRebuildQueue_.end(), doorId) == owner_.doorTextureRebuildQueue_.end()) {
                                owner_.doorTextureRebuildQueue_.push_back(doorId);
                            }
                        }
                        LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued {} doors for rebuild",
                                 result.textureName, doorPendIt->second.size());
                        owner_.pendingTextureDoors_.erase(doorPendIt);
                    }

                    // PVS objects waiting for this texture
                    auto objPendIt = owner_.pendingTextureObjects_.find(result.textureName);
                    if (objPendIt != owner_.pendingTextureObjects_.end()) {
                        for (size_t objIdx : objPendIt->second) {
                            if (std::find(owner_.objectTextureRebuildQueue_.begin(),
                                          owner_.objectTextureRebuildQueue_.end(), objIdx) == owner_.objectTextureRebuildQueue_.end()) {
                                owner_.objectTextureRebuildQueue_.push_back(objIdx);
                            }
                        }
                        LOG_INFO(MOD_GRAPHICS, "GPUUpload processOne: texture '{}' arrived, queued {} objects for rebuild",
                                 result.textureName, objPendIt->second.size());
                        owner_.pendingTextureObjects_.erase(objPendIt);
                    }
                }
            } else if (sourceType == 4) {
                // Icon texture — icon loading removed (WindowManager deleted)
            }
            break;
        }

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

    // Sort texture rebuild queue: player's region first (distSq=-1), then nearest
    if (!owner_.textureRebuildQueue_.empty()) {
        std::sort(owner_.textureRebuildQueue_.begin(), owner_.textureRebuildQueue_.end(),
                  [](const IrrlichtRenderer::TextureRebuildEntry& a, const IrrlichtRenderer::TextureRebuildEntry& b) {
                      return a.distanceSq < b.distanceSq;
                  });
    }

    return true;
}
#endif // EQT_HAS_GLES2

void ZoneLifecycleComponent::toggleGPUUploadThread() {
    if (owner_.isLoading()) return;
#ifdef EQT_HAS_GLES2
    gpuUploadEnabled_ = !gpuUploadEnabled_;
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: {}", gpuUploadEnabled_ ? "ENABLED" : "DISABLED");

    // Propagate to subsystems: set or clear their gpuUploadThread_ pointers
    GPUUploadThread* ptr = gpuUploadEnabled_ ? ctx_.gpuUploadThread.get() : nullptr;
    if (ctx_.constrainedTextureCache)
        ctx_.constrainedTextureCache->setGPUUploadThread(ptr);
#else
    LOG_INFO(MOD_GRAPHICS, "GPU upload thread: not available (non-GLES2 build)");
#endif
}

} // namespace Graphics
} // namespace EQT
