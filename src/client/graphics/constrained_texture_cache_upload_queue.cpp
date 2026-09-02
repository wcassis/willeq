// constrained_texture_cache_upload_queue.cpp — the placeholder-texture accessor,
// the quarantined T04 upload queue, and the live pending-work predicates.
// ⚠ ~299 of this file's ~373 lines are a deliberately DISABLED `#if 0` region,
// preserved VERBATIM by J10; the live isPending()/hasPendingWork() are the
// copies AFTER the `#endif`, not the disabled definitions inside it.
// Split from the 2,211-line original by refactor/J10_constrained_texture_cache_split.md.
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/background_thread_pool.h"
#include "common/logging.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/gpu_upload_thread.h"
#include "client/graphics/work_priority.h"
#endif
#include <algorithm>
#include <cstring>
#include <functional>
#include <fstream>
#include <cstdlib>

// P03: the EQE1 upload path (ETC1 + v2 payloads) is available on any GLES2
// driver build, not just DRM/ARM — EQT_HAS_GLES2 is defined for the x86
// llvmpipe dev build (EQT_DRM=OFF, EQT_GLES2=ON), where gles2WrapTexture and
// the GL entry points below are equally present. The ARM/Pi build defines
// BOTH macros, so its preprocessed output is unchanged by this widening.
#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)
#define EQT_EQE1_UPLOAD_AVAILABLE 1
#include <GL/gl.h>
#include <GL/glext.h>
// glGenerateMipmap and glCompressedTexImage2D may not be directly available
// in all GL headers; get function pointers via EGL at runtime
#include <EGL/egl.h>
typedef void (*PFNGLGENERATEMIPMAPPROC_)(GLenum target);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC_)(GLenum target, GLint level,
    GLenum internalformat, GLsizei width, GLsizei height, GLint border,
    GLsizei imageSize, const void* data);

// Defined in COpenGLES2Driver.cpp — wraps a raw GL texture handle in an ITexture*
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes);
#endif

namespace EQT {
namespace Graphics {

// ---------------------------------------------------------------------------
// Placeholder texture (public locks, internal does not)
// ---------------------------------------------------------------------------

irr::video::ITexture* ConstrainedTextureCache::getPlaceholderTexture() {
    std::lock_guard<std::mutex> lock(mutex_);
    return getPlaceholderTextureInternal();
}

// ---------------------------------------------------------------------------
// T04: Upload queue removed. Use getOrLoad() + uploadTexture() instead.
// ---------------------------------------------------------------------------

#if 0  // T04: Removed — replaced by getOrLoad() + uploadTexture()
void ConstrainedTextureCache::queueDecoded(const std::string& name,
                                            std::vector<uint8_t> rgbaPixels,
                                            int width, int height, bool hasAlpha) {
    std::lock_guard<std::mutex> lock(decodedQueueMutex_);
    decodedQueue_.push_back({name, std::move(rgbaPixels), width, height, hasAlpha});
}

void ConstrainedTextureCache::queueDecodedARGB(const std::string& name,
                                                const std::vector<uint32_t>& argbPixels,
                                                int width, int height, bool hasAlpha) {
    // Convert ARGB (Irrlicht ECF_A8R8G8B8 in memory: B G R A) → RGBA for GL
    size_t pixelCount = argbPixels.size();
    std::vector<uint8_t> rgba(pixelCount * 4);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(argbPixels.data());
    for (size_t i = 0; i < pixelCount; ++i) {
        size_t si = i * 4;
        rgba[si + 0] = src[si + 2];  // R (was at offset 2 in BGRA layout)
        rgba[si + 1] = src[si + 1];  // G
        rgba[si + 2] = src[si + 0];  // B
        rgba[si + 3] = src[si + 3];  // A
    }
    queueDecoded(name, std::move(rgba), width, height, hasAlpha);
}

int ConstrainedTextureCache::processUploadQueue() {
    // Step 1: Acquire decodedQueueMutex_, swap batch out, release.
    // This is the ONLY lock on decodedQueueMutex_ in this method — mutex_ is
    // acquired AFTER release, maintaining the lock-ordering invariant.
    std::vector<DecodedUpload> batch;
    {
        std::lock_guard<std::mutex> lock(decodedQueueMutex_);
        batch.swap(decodedQueue_);
    }

    if (batch.empty()) return 0;

    // Step 2: Acquire mutex_ for all cache mutations.
    std::lock_guard<std::mutex> lock(mutex_);

    int uploaded = 0;
    for (auto& item : batch) {
        // Clear from pending decode set
        pendingDecodes_.erase(item.name);

        // Sentinel: empty pixels means decode failed — just clean up
        if (item.rgbaPixels.empty()) {
            continue;
        }

        // Already in cache (raced with another upload path)
        if (cache_.find(item.name) != cache_.end()) {
            touchInternal(item.name);
            continue;
        }

        // Budget check: calculate size and evict if needed
        size_t textureSize = calculateTextureSize(item.width, item.height);
        if (!evictUntilAvailable(textureSize)) {
            LOG_WARN(MOD_GRAPHICS, "Constrained cache: budget exhausted, dropping '{}' (need {} bytes, budget {} bytes, used {} bytes)",
                item.name, textureSize, config_.textureMemoryBytes, currentUsage_);
            continue;
        }

#ifdef EQT_HAS_GLES2
        // GLES2 async path: submit to GPU upload thread
        if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
            UploadRequest req;
            req.type = UploadRequestType::Texture;
            req.width = static_cast<uint32_t>(item.width);
            req.height = static_cast<uint32_t>(item.height);
            req.pixelData = std::move(item.rgbaPixels);
            req.textureName = item.name;
            // High byte 3 = constrained cache texture (unified path)
            req.callbackKey = uint64_t(3) << 56;
            req.priority = WorkPriorityKey::make(0, AssetType::ZoneTexture).value;

            gpuUploadThread_->submit(std::move(req));
            pendingAsyncUploads_.insert(item.name);
            ++uploaded;
            LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: queued GPU upload for '{}' ({}x{})",
                      item.name, item.width, item.height);
            continue;
        }
#endif

        // Desktop GL / software: synchronous upload via driver_->addTexture()
        // Convert RGBA to ARGB (Irrlicht's native format)
        std::vector<uint8_t> argbData(item.rgbaPixels.size());
        for (size_t i = 0; i < item.rgbaPixels.size(); i += 4) {
            argbData[i + 0] = item.rgbaPixels[i + 2];  // B
            argbData[i + 1] = item.rgbaPixels[i + 1];  // G
            argbData[i + 2] = item.rgbaPixels[i + 0];  // R
            argbData[i + 3] = item.rgbaPixels[i + 3];  // A
        }

        irr::core::dimension2d<irr::u32> size(item.width, item.height);
        irr::video::IImage* image = driver_->createImageFromData(
            irr::video::ECF_A8R8G8B8, size, argbData.data(), false);

        irr::video::ITexture* texture = nullptr;
        if (image) {
            texture = driver_->addTexture(item.name.c_str(), image);
            image->drop();
        }

        if (!texture) {
            LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: addTexture failed for '{}' ({}x{})",
                      item.name, item.width, item.height);
            continue;
        }

#ifdef EQT_HAS_DRM
        // Set bilinear filtering directly on GL texture (Lima/Mali400 workaround).
        // P42 (P38 finding, refactor/perf_truth_audit.md): simplified -- see
        // the getOrLoad() sibling block's comment; MIN_FILTER here was always
        // stomped by applyMaterialRenderState() before this texture was ever
        // sampled, so the enableMipmaps branch was a dead no-op. No behavior
        // change.
        {
            irr::u32 glName = texture->getDriverTextureHandle();
            if (glName != 0) {
                glBindTexture(GL_TEXTURE_2D, glName);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
#endif

        // Register in LRU cache
        lruOrder_.push_back(item.name);
        CachedTexture entry;
        entry.texture = texture;
        entry.sizeBytes = textureSize;
        entry.hasAlpha = item.hasAlpha;
        entry.lruIterator = std::prev(lruOrder_.end());
        cache_[item.name] = entry;
        currentUsage_ += textureSize;

        // Also register in mesh builder for entity texture lookups
        if (meshBuilder_) {
            meshBuilder_->registerUploadedTexture(item.name, texture, item.hasAlpha);
        }

        ++uploaded;
        LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: uploaded '{}' {}x{} ({} bytes)",
                  item.name, item.width, item.height, textureSize);
    }

    return uploaded;
}

void ConstrainedTextureCache::drainDecodedQueue() {
    std::vector<DecodedUpload> batch;
    {
        std::lock_guard<std::mutex> dqLock(decodedQueueMutex_);
        if (decodedQueue_.empty()) return;
        batch.swap(decodedQueue_);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : batch)
        stagedUploads_.push_back(std::move(item));
}

bool ConstrainedTextureCache::processOneUpload() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stagedUploads_.empty()) return false;

    auto item = std::move(stagedUploads_.front());
    stagedUploads_.erase(stagedUploads_.begin());

    // Clear from pending decode set
    pendingDecodes_.erase(item.name);

    // Sentinel: empty pixels means decode failed — just clean up
    if (item.rgbaPixels.empty()) {
        return true;  // consumed work
    }

    // Already in cache (raced with another upload path)
    if (cache_.find(item.name) != cache_.end()) {
        touchInternal(item.name);
        return true;
    }

    // Budget check: calculate size and evict if needed
    size_t textureSize = calculateTextureSize(item.width, item.height);
    if (!evictUntilAvailable(textureSize)) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache processOne: budget exhausted, dropping '{}' (need {} bytes, budget {} bytes, used {} bytes)",
            item.name, textureSize, config_.textureMemoryBytes, currentUsage_);
        return true;
    }

#ifdef EQT_HAS_GLES2
    // GLES2 async path: submit to GPU upload thread
    if (gpuUploadThread_ && gpuUploadThread_->isAvailable()) {
        UploadRequest req;
        req.type = UploadRequestType::Texture;
        req.width = static_cast<uint32_t>(item.width);
        req.height = static_cast<uint32_t>(item.height);
        req.pixelData = std::move(item.rgbaPixels);
        req.textureName = item.name;
        // High byte 3 = constrained cache texture (unified path)
        req.callbackKey = uint64_t(3) << 56;
        req.priority = WorkPriorityKey::make(0, AssetType::ZoneTexture).value;

        gpuUploadThread_->submit(std::move(req));
        pendingAsyncUploads_.insert(item.name);
        LOG_DEBUG(MOD_GRAPHICS, "Constrained cache processOne: queued GPU upload for '{}' ({}x{})",
                  item.name, item.width, item.height);
        return true;
    }
#endif

    // Desktop GL / software: synchronous upload via driver_->addTexture()
    // Convert RGBA to ARGB (Irrlicht's native format)
    std::vector<uint8_t> argbData(item.rgbaPixels.size());
    for (size_t i = 0; i < item.rgbaPixels.size(); i += 4) {
        argbData[i + 0] = item.rgbaPixels[i + 2];  // B
        argbData[i + 1] = item.rgbaPixels[i + 1];  // G
        argbData[i + 2] = item.rgbaPixels[i + 0];  // R
        argbData[i + 3] = item.rgbaPixels[i + 3];  // A
    }

    irr::core::dimension2d<irr::u32> size(item.width, item.height);
    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8, size, argbData.data(), false);

    irr::video::ITexture* texture = nullptr;
    if (image) {
        texture = driver_->addTexture(item.name.c_str(), image);
        image->drop();
    }

    if (!texture) {
        LOG_DEBUG(MOD_GRAPHICS, "Constrained cache processOne: addTexture failed for '{}' ({}x{})",
                  item.name, item.width, item.height);
        return true;
    }

#ifdef EQT_HAS_DRM
    // Set bilinear filtering directly on GL texture (Lima/Mali400 workaround).
    // P42 (P38 finding, refactor/perf_truth_audit.md): this used to branch on
    // config_.enableMipmaps to request GL_LINEAR_MIPMAP_LINEAR here, but
    // COpenGLES2Driver::applyMaterialRenderState() unconditionally
    // overwrites MIN_FILTER to plain GL_LINEAR/GL_NEAREST on every single
    // draw before this texture is ever sampled -- the mipmap filter mode
    // set here was always stomped before use, a total no-op (and, while
    // live, requested filtering against an incomplete mip chain -- no code
    // path here ever called glGenerateMipmap). Simplified to the filter
    // that actually took effect; no behavior change, this branch never
    // affected rendering.
    {
        irr::u32 glName = texture->getDriverTextureHandle();
        if (glName != 0) {
            glBindTexture(GL_TEXTURE_2D, glName);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
#endif

    // Register in LRU cache
    lruOrder_.push_back(item.name);
    CachedTexture entry;
    entry.texture = texture;
    entry.sizeBytes = textureSize;
    entry.hasAlpha = item.hasAlpha;
    entry.lruIterator = std::prev(lruOrder_.end());
    cache_[item.name] = entry;
    currentUsage_ += textureSize;

    // Also register in mesh builder for entity texture lookups
    if (meshBuilder_) {
        meshBuilder_->registerUploadedTexture(item.name, texture, item.hasAlpha);
    }

    LOG_DEBUG(MOD_GRAPHICS, "Constrained cache processOne: uploaded '{}' {}x{} ({} bytes)",
              item.name, item.width, item.height, textureSize);
    return true;
}

size_t ConstrainedTextureCache::getStagedUploadCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stagedUploads_.size();
}

bool ConstrainedTextureCache::isPending(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingDecodes_.count(name) > 0 || pendingAsyncUploads_.count(name) > 0;
}

bool ConstrainedTextureCache::hasPendingWork() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !decodedEntries_.empty() || !pendingAsyncUploads_.empty();
}
#endif  // T04: End of removed upload queue block

bool ConstrainedTextureCache::isPending(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return decodedEntries_.count(name) > 0 || pendingAsyncUploads_.count(name) > 0;
}

bool ConstrainedTextureCache::hasPendingWork() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !decodedEntries_.empty() || !pendingAsyncUploads_.empty();
}

} // namespace Graphics
} // namespace EQT
