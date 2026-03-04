#include "client/graphics/constrained_texture_cache.h"
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

#ifdef EQT_HAS_DRM
#include <GL/gl.h>
#include <GL/glext.h>
// glGenerateMipmap and glCompressedTexImage2D may not be directly available
// in all GL headers; get function pointers via EGL at runtime
#include <EGL/egl.h>
typedef void (*PFNGLGENERATEMIPMAPPROC_)(GLenum target);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC_)(GLenum target, GLint level,
    GLenum internalformat, GLsizei width, GLsizei height, GLint border,
    GLsizei imageSize, const void* data);
#endif

namespace EQT {
namespace Graphics {

ConstrainedTextureCache::ConstrainedTextureCache(const ConstrainedRendererConfig& config,
                                                   irr::video::IVideoDriver* driver)
    : config_(config)
    , driver_(driver)
{
    probeCompressedTextureSupport();
}

ConstrainedTextureCache::~ConstrainedTextureCache() {
    clear();
}

// ---------------------------------------------------------------------------
// Internal helpers (called while holding mutex_)
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::touchInternal(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        lruOrder_.erase(it->second.lruIterator);
        lruOrder_.push_back(name);
        it->second.lruIterator = std::prev(lruOrder_.end());
    }
}

irr::video::ITexture* ConstrainedTextureCache::getPlaceholderTextureInternal() {
    if (placeholderTexture_) {
        return placeholderTexture_;
    }

    // Create a small checkerboard placeholder texture (8x8 magenta/black)
    const int size = 8;
    std::vector<uint32_t> pixels(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Checkerboard pattern: magenta and dark gray
            bool checker = ((x / 2) + (y / 2)) % 2 == 0;
            pixels[y * size + x] = checker ? 0xFFFF00FF : 0xFF202020;  // ARGB
        }
    }

    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(size, size),
        pixels.data(), false);

    if (image) {
        placeholderTexture_ = driver_->addTexture("_placeholder_", image);
        image->drop();
    }

    return placeholderTexture_;
}

// ---------------------------------------------------------------------------
// Public methods — all acquire mutex_ (except queueDecoded/queueDecodedARGB
// which only use decodedQueueMutex_)
// ---------------------------------------------------------------------------

irr::video::ITexture* ConstrainedTextureCache::getOrLoad(const std::string& name,
                                                          const std::vector<char>& data) {
    // Capture bgThreadPool_ under lock; release mutex_ before calling into
    // the pool or doing synchronous decode (avoids mutex_ → decodedQueueMutex_
    // lock-order inversion with processUploadQueue).
    BackgroundThreadPool* pool = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check if already cached
        auto it = cache_.find(name);
        if (it != cache_.end()) {
            touchInternal(name);
            ++cacheHits_;
            return it->second.texture;
        }

        ++cacheMisses_;

        // Debug: skip all texture uploads to isolate endScene regression
        if (config_.skipConstrainedTextureUpload) {
            return nullptr;
        }

        // Already pending decode or upload — return nullptr without resubmitting
        if (pendingDecodes_.count(name) || pendingAsyncUploads_.count(name)) {
            return nullptr;
        }

        // Try compressed upload first (skips CPU decode, saves GPU memory)
        if (compressedTexturesAvailable_) {
            irr::video::ITexture* compressed = tryCompressedUpload(name, data);
            if (compressed) {
                return compressed;
            }
        }

        // Decide path: background or synchronous
        if (bgThreadPool_) {
            pendingDecodes_.insert(name);
            pool = bgThreadPool_;
        }
    }
    // mutex_ released

    // Submit decode task to background thread pool
    if (pool) {
        // Copy data for the background thread (source data lifetime may not outlast the task)
        auto dataCopy = std::make_shared<std::vector<char>>(data);
        std::string texName = name;
        pool->submit(10, [this, texName, dataCopy]() {
            std::vector<uint8_t> processedData;
            int width = 0, height = 0;
            bool hasAlpha = false;
            if (processTextureData(*dataCopy, processedData, width, height, hasAlpha)) {
                queueDecoded(texName, std::move(processedData), width, height, hasAlpha);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: bg decode failed for '{}'", texName);
                // Remove from pending on failure so it can be retried
                std::lock_guard<std::mutex> lk(decodedQueueMutex_);
                // Use a sentinel empty entry to signal removal from pendingDecodes_ on render thread
                decodedQueue_.push_back({texName, {}, 0, 0, false});
            }
        });
        LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: bg decode submitted for '{}' ({} bytes)", name, data.size());
        return nullptr;  // Caller retries next frame
    }

    // Fallback: synchronous decode (no background thread pool).
    // processTextureData only reads immutable config_ and local data — safe without mutex_.
    // queueDecoded only locks decodedQueueMutex_ — no lock-order issue.
    std::vector<uint8_t> processedData;
    int width = 0, height = 0;
    bool hasAlpha = false;

    if (!processTextureData(data, processedData, width, height, hasAlpha)) {
        LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: processTextureData failed for '{}' ({} bytes)", name, data.size());
        return nullptr;
    }

    // Queue for upload on render thread (processUploadQueue will handle budget + GPU upload)
    queueDecoded(name, std::move(processedData), width, height, hasAlpha);
    return nullptr;  // Will be available after processUploadQueue runs
}

void ConstrainedTextureCache::touch(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    touchInternal(name);
}

bool ConstrainedTextureCache::hasTexture(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(name) != cache_.end();
}

irr::video::ITexture* ConstrainedTextureCache::getTexture(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        touchInternal(name);
        return it->second.texture;
    }
    return nullptr;
}

bool ConstrainedTextureCache::registerTexture(const std::string& name,
                                               irr::video::ITexture* texture,
                                               size_t sizeBytes, bool hasAlphaFlag) {
    if (!texture) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    // Already in cache — check if same texture (just touch) or replacement
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        if (it->second.texture == texture) {
            touchInternal(name);
            return true;
        }
        // Different texture under same name — evict the old one first
        evictTexture(name);
    }

    // Too large to ever fit
    if (sizeBytes > config_.textureMemoryBytes) {
        return false;
    }

    // Evict textures if needed to make room
    if (!evictUntilAvailable(sizeBytes)) {
        return false;
    }

    // Add to LRU cache
    lruOrder_.push_back(name);
    CachedTexture entry;
    entry.texture = texture;
    entry.sizeBytes = sizeBytes;
    entry.hasAlpha = hasAlphaFlag;
    entry.lruIterator = std::prev(lruOrder_.end());
    cache_[name] = entry;
    currentUsage_ += sizeBytes;

    return true;
}

bool ConstrainedTextureCache::hasAlpha(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(name);
    return it != cache_.end() && it->second.hasAlpha;
}

void ConstrainedTextureCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Notify listeners for each texture being cleared
    for (auto& pair : cache_) {
        for (auto* listener : evictionListeners_) {
            listener->onTextureEvicted(pair.first);
        }
        if (pair.second.texture) {
            driver_->removeTexture(pair.second.texture);
        }
    }
    cache_.clear();
    lruOrder_.clear();
    currentUsage_ = 0;
}

// ---------------------------------------------------------------------------
// Statistics (lock mutex_ for consistent reads)
// ---------------------------------------------------------------------------

size_t ConstrainedTextureCache::getCurrentUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentUsage_;
}

size_t ConstrainedTextureCache::getMemoryLimit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.textureMemoryBytes;
}

size_t ConstrainedTextureCache::getAvailableMemory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.textureMemoryBytes - currentUsage_;
}

size_t ConstrainedTextureCache::getTextureCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

size_t ConstrainedTextureCache::getCacheHits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheHits_;
}

size_t ConstrainedTextureCache::getCacheMisses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheMisses_;
}

size_t ConstrainedTextureCache::getEvictionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return evictionCount_;
}

size_t ConstrainedTextureCache::getCompressedUploadCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return compressedUploadCount_;
}

float ConstrainedTextureCache::getHitRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = cacheHits_ + cacheMisses_;
    if (total == 0) return 0.0f;
    return (static_cast<float>(cacheHits_) / total) * 100.0f;
}

void ConstrainedTextureCache::resetStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    cacheHits_ = 0;
    cacheMisses_ = 0;
    evictionCount_ = 0;
}

// ---------------------------------------------------------------------------
// Freeze / setters (lock mutex_)
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::freeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = true;
}

void ConstrainedTextureCache::unfreeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = false;
}

bool ConstrainedTextureCache::isFrozen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frozen_;
}

void ConstrainedTextureCache::setGPUUploadThread(GPUUploadThread* thread) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpuUploadThread_ = thread;
}

void ConstrainedTextureCache::clearPendingAsync(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingAsyncUploads_.erase(name);
}

void ConstrainedTextureCache::setSceneManager(irr::scene::ISceneManager* smgr) {
    std::lock_guard<std::mutex> lock(mutex_);
    smgr_ = smgr;
}

void ConstrainedTextureCache::setBackgroundThreadPool(BackgroundThreadPool* pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    bgThreadPool_ = pool;
}

void ConstrainedTextureCache::setMeshBuilder(ZoneMeshBuilder* meshBuilder) {
    std::lock_guard<std::mutex> lock(mutex_);
    meshBuilder_ = meshBuilder;
}

// ---------------------------------------------------------------------------
// Placeholder texture (public locks, internal does not)
// ---------------------------------------------------------------------------

irr::video::ITexture* ConstrainedTextureCache::getPlaceholderTexture() {
    std::lock_guard<std::mutex> lock(mutex_);
    return getPlaceholderTextureInternal();
}

// ---------------------------------------------------------------------------
// Upload queue (decodedQueueMutex_ for queue, then mutex_ for cache state)
// ---------------------------------------------------------------------------

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
        // Set bilinear filtering directly on GL texture (Lima/Mali400 workaround)
        {
            irr::u32 glName = texture->getDriverTextureHandle();
            if (glName != 0) {
                glBindTexture(GL_TEXTURE_2D, glName);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                if (config_.enableMipmaps) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                } else {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                }
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
    // Set bilinear filtering directly on GL texture (Lima/Mali400 workaround)
    {
        irr::u32 glName = texture->getDriverTextureHandle();
        if (glName != 0) {
            glBindTexture(GL_TEXTURE_2D, glName);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            if (config_.enableMipmaps) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            } else {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            }
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
    return !pendingDecodes_.empty() || !pendingAsyncUploads_.empty();
}

// ---------------------------------------------------------------------------
// Eviction listeners (lock mutex_)
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::addEvictionListener(TextureEvictionListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (listener) {
        evictionListeners_.push_back(listener);
    }
}

void ConstrainedTextureCache::removeEvictionListener(TextureEvictionListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    evictionListeners_.erase(
        std::remove(evictionListeners_.begin(), evictionListeners_.end(), listener),
        evictionListeners_.end());
}

// ---------------------------------------------------------------------------
// Private methods — NO locking (always called under mutex_ or immutable data)
// ---------------------------------------------------------------------------

bool ConstrainedTextureCache::evictUntilAvailable(size_t bytesNeeded) {
    // Check if texture is too large to ever fit
    if (bytesNeeded > config_.textureMemoryBytes) {
        return false;
    }

    // Evict LRU textures until we have enough space
    // Safe eviction: clearTextureReferences() removes texture from mesh materials before deletion
    while (currentUsage_ + bytesNeeded > config_.textureMemoryBytes && !lruOrder_.empty()) {
        std::string oldest = lruOrder_.front();
        evictTexture(oldest);
    }

    return (currentUsage_ + bytesNeeded <= config_.textureMemoryBytes);
}

void ConstrainedTextureCache::evictTexture(const std::string& name) {
    auto it = cache_.find(name);
    if (it == cache_.end()) return;

    irr::video::ITexture* texture = it->second.texture;
    size_t sizeBytes = it->second.sizeBytes;

    LOG_DEBUG(MOD_GRAPHICS, "Evicting texture '{}' ({} bytes)", name, sizeBytes);

    // Notify listeners before clearing references and removing texture
    for (auto* listener : evictionListeners_) {
        listener->onTextureEvicted(name);
    }

    // CRITICAL: Clear all references to this texture from mesh materials BEFORE removing
    // This prevents crashes from dangling texture pointers during rendering
    if (texture) {
        clearTextureReferences(texture);
        driver_->removeTexture(texture);
    }

    // Update memory tracking
    currentUsage_ -= sizeBytes;

    // Remove from LRU list
    lruOrder_.erase(it->second.lruIterator);

    // Remove from cache
    cache_.erase(it);

    ++evictionCount_;
}

size_t ConstrainedTextureCache::calculateTextureSize(int width, int height) const {
    // Always use 4 bytes per pixel (32-bit ARGB) for compatibility with software renderer
    size_t base = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (config_.enableMipmaps) {
        // Mipmap chain adds ~33% overhead (sum of 1/4 + 1/16 + ... converges to 1/3)
        return base + base / 3;
    }
    return base;
}

bool ConstrainedTextureCache::processTextureData(const std::vector<char>& rawData,
                                                  std::vector<uint8_t>& processedData,
                                                  int& width, int& height,
                                                  bool& hasAlpha) {
    // Decode based on format
    if (DDSDecoder::isDDS(rawData)) {
        DecodedImage image = DDSDecoder::decode(rawData);
        if (!image.isValid()) {
            return false;
        }
        width = static_cast<int>(image.width);
        height = static_cast<int>(image.height);
        processedData = std::move(image.pixels);
    } else {
        // Assume BMP
        if (!decodeBMP(rawData, processedData, width, height)) {
            return false;
        }
    }

    // Downsample if needed
    if (width > config_.maxTextureDimension || height > config_.maxTextureDimension) {
        downsampleToMaxSize(processedData, width, height, 4);  // 4 channels (RGBA)
    }

    // Detect alpha channel
    hasAlpha = detectAlpha(processedData, width, height);

    return true;
}

void ConstrainedTextureCache::downsampleToMaxSize(std::vector<uint8_t>& data,
                                                   int& width, int& height,
                                                   int channels) {
    // Repeatedly halve dimensions using box filter until within limits
    while (width > config_.maxTextureDimension || height > config_.maxTextureDimension) {
        int newWidth = width / 2;
        int newHeight = height / 2;

        // Ensure minimum size of 1
        if (newWidth < 1) newWidth = 1;
        if (newHeight < 1) newHeight = 1;

        std::vector<uint8_t> newData(newWidth * newHeight * channels);

        // Box filter: average 2x2 blocks
        for (int y = 0; y < newHeight; ++y) {
            for (int x = 0; x < newWidth; ++x) {
                for (int c = 0; c < channels; ++c) {
                    int sum = 0;
                    int count = 0;

                    // Sample up to 2x2 pixels
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            int srcX = x * 2 + dx;
                            int srcY = y * 2 + dy;
                            if (srcX < width && srcY < height) {
                                sum += data[(srcY * width + srcX) * channels + c];
                                ++count;
                            }
                        }
                    }

                    newData[(y * newWidth + x) * channels + c] =
                        static_cast<uint8_t>(sum / count);
                }
            }
        }

        data = std::move(newData);
        width = newWidth;
        height = newHeight;
    }
}

std::vector<uint16_t> ConstrainedTextureCache::convertTo16Bit(const std::vector<uint8_t>& rgba,
                                                               int width, int height,
                                                               bool hasAlpha) {
    std::vector<uint16_t> result(width * height);

    for (int i = 0; i < width * height; ++i) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];

        if (hasAlpha) {
            // RGBA 1555: 1 bit alpha, 5 bits each RGB
            // Irrlicht ECF_A1R5G5B5 format: A(1) R(5) G(5) B(5)
            uint16_t a1 = (a >= 128) ? 1 : 0;
            uint16_t r5 = (r >> 3) & 0x1F;
            uint16_t g5 = (g >> 3) & 0x1F;
            uint16_t b5 = (b >> 3) & 0x1F;
            result[i] = (a1 << 15) | (r5 << 10) | (g5 << 5) | b5;
        } else {
            // RGB 565: 5 bits red, 6 bits green, 5 bits blue
            // Irrlicht ECF_R5G6B5 format: R(5) G(6) B(5)
            uint16_t r5 = (r >> 3) & 0x1F;
            uint16_t g6 = (g >> 2) & 0x3F;
            uint16_t b5 = (b >> 3) & 0x1F;
            result[i] = (r5 << 11) | (g6 << 5) | b5;
        }
    }

    return result;
}

bool ConstrainedTextureCache::detectAlpha(const std::vector<uint8_t>& rgba, int width, int height) {
    // Check if any pixel has alpha < 255
    for (int i = 0; i < width * height; ++i) {
        if (rgba[i * 4 + 3] < 255) {
            return true;
        }
    }
    return false;
}

bool ConstrainedTextureCache::decodeBMP(const std::vector<char>& data,
                                         std::vector<uint8_t>& rgba,
                                         int& width, int& height) {
    if (data.size() < 54) return false;  // Minimum BMP header size

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());

    // Check BMP magic
    if (ptr[0] != 'B' || ptr[1] != 'M') return false;

    // Read header
    uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(ptr + 10);
    int32_t bmpWidth = *reinterpret_cast<const int32_t*>(ptr + 18);
    int32_t bmpHeight = *reinterpret_cast<const int32_t*>(ptr + 22);
    uint16_t bitsPerPixel = *reinterpret_cast<const uint16_t*>(ptr + 28);

    if (bmpWidth <= 0 || bmpHeight == 0) return false;

    // Handle negative height (top-down DIB)
    bool topDown = (bmpHeight < 0);
    if (topDown) bmpHeight = -bmpHeight;

    width = bmpWidth;
    height = bmpHeight;

    // Calculate row size (must be aligned to 4 bytes)
    int bytesPerPixel = bitsPerPixel / 8;
    int rowSize = ((bmpWidth * bytesPerPixel + 3) / 4) * 4;

    if (dataOffset + rowSize * height > data.size()) return false;

    // Allocate output
    rgba.resize(width * height * 4);

    // Decode pixels
    const uint8_t* pixelData = ptr + dataOffset;

    for (int y = 0; y < height; ++y) {
        int srcY = topDown ? y : (height - 1 - y);
        const uint8_t* row = pixelData + srcY * rowSize;

        for (int x = 0; x < width; ++x) {
            int dstIdx = (y * width + x) * 4;

            if (bitsPerPixel == 24) {
                rgba[dstIdx + 0] = row[x * 3 + 2];  // R
                rgba[dstIdx + 1] = row[x * 3 + 1];  // G
                rgba[dstIdx + 2] = row[x * 3 + 0];  // B
                rgba[dstIdx + 3] = 255;             // A
            } else if (bitsPerPixel == 32) {
                rgba[dstIdx + 0] = row[x * 4 + 2];  // R
                rgba[dstIdx + 1] = row[x * 4 + 1];  // G
                rgba[dstIdx + 2] = row[x * 4 + 0];  // B
                rgba[dstIdx + 3] = row[x * 4 + 3];  // A
            } else if (bitsPerPixel == 8) {
                // 8-bit indexed - need palette
                // For simplicity, treat as grayscale
                uint8_t val = row[x];
                rgba[dstIdx + 0] = val;
                rgba[dstIdx + 1] = val;
                rgba[dstIdx + 2] = val;
                rgba[dstIdx + 3] = 255;
            } else {
                // Unsupported format
                return false;
            }
        }
    }

    return true;
}


void ConstrainedTextureCache::clearTextureReferences(irr::video::ITexture* texture) {
    if (!smgr_ || !texture) {
        return;
    }

    // Get placeholder to replace evicted textures (use internal — already under mutex_)
    irr::video::ITexture* placeholder = getPlaceholderTextureInternal();

    // Recursively scan all scene nodes and their mesh materials
    std::function<void(irr::scene::ISceneNode*)> scanNode = [&](irr::scene::ISceneNode* node) {
        if (!node) return;

        // Check if this node has a mesh (CMeshSceneNode or CAnimatedMeshSceneNode)
        irr::scene::IMeshSceneNode* meshNode = nullptr;
        irr::scene::IAnimatedMeshSceneNode* animNode = nullptr;

        if (node->getType() == irr::scene::ESNT_MESH) {
            meshNode = static_cast<irr::scene::IMeshSceneNode*>(node);
        } else if (node->getType() == irr::scene::ESNT_ANIMATED_MESH) {
            animNode = static_cast<irr::scene::IAnimatedMeshSceneNode*>(node);
        }

        // Check mesh materials
        irr::scene::IMesh* mesh = nullptr;
        if (meshNode && meshNode->getMesh()) {
            mesh = meshNode->getMesh();
        } else if (animNode && animNode->getMesh()) {
            mesh = animNode->getMesh()->getMesh(0);  // Get first frame mesh
        }

        if (mesh) {
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(i);
                if (buffer) {
                    irr::video::SMaterial& mat = buffer->getMaterial();
                    for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                        if (mat.getTexture(t) == texture) {
                            mat.setTexture(t, placeholder);
                        }
                    }
                }
            }
        }

        // Also check node's own materials (override materials)
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
            irr::video::SMaterial& mat = node->getMaterial(i);
            for (irr::u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
                if (mat.getTexture(t) == texture) {
                    mat.setTexture(t, placeholder);
                }
            }
        }

        // Recurse to children
        const auto& children = node->getChildren();
        for (auto* child : children) {
            scanNode(child);
        }
    };

    // Start scan from root
    scanNode(smgr_->getRootSceneNode());
}

void ConstrainedTextureCache::probeCompressedTextureSupport() {
    if (!config_.enableCompressedTextures) {
        compressedTexturesAvailable_ = false;
        return;
    }

#ifdef EQT_HAS_DRM
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && strstr(extensions, "GL_EXT_texture_compression_s3tc")) {
        compressedTexturesAvailable_ = true;
        LOG_INFO(MOD_GRAPHICS, "Compressed textures: S3TC support detected, enabled");
    } else {
        compressedTexturesAvailable_ = false;
        LOG_WARN(MOD_GRAPHICS, "Compressed textures: S3TC not available, falling back to software decode");
    }
#else
    compressedTexturesAvailable_ = false;
#endif
}

irr::video::ITexture* ConstrainedTextureCache::tryCompressedUpload(
        const std::string& name, const std::vector<char>& data) {
#ifdef EQT_HAS_DRM
    // Extract compressed DXT data without decompressing
    CompressedTextureData compressed = DDSDecoder::extractCompressed(data);
    if (!compressed.isValid()) {
        return nullptr;  // Not DDS or unsupported format
    }

    // Cannot downsample compressed data — skip if oversized
    if (static_cast<int>(compressed.width) > config_.maxTextureDimension ||
        static_cast<int>(compressed.height) > config_.maxTextureDimension) {
        LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: '{}' {}x{} exceeds max dimension {}, falling back",
            name, compressed.width, compressed.height, config_.maxTextureDimension);
        return nullptr;
    }

    // Track uncompressed size for memory budget.  On Lima/Mesa the driver
    // software-decodes S3TC and stores textures uncompressed in GPU memory,
    // so the budget must reflect the real GPU footprint (including mipmaps).
    size_t textureSize = calculateTextureSize(compressed.width, compressed.height);

    // Evict textures if needed to make room
    if (!evictUntilAvailable(textureSize)) {
        LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: eviction failed for '{}' (need {} bytes)", name, textureSize);
        return nullptr;
    }

    // Create a correctly-sized Irrlicht texture so its internal state (dimensions,
    // format tracking) matches the compressed data we'll upload.  Using a zeroed
    // buffer is a temporary allocation that is freed after addTexture.
    size_t dummyBytes = static_cast<size_t>(compressed.width) * compressed.height * 4;
    std::vector<uint8_t> dummyPixels(dummyBytes, 0);

    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(compressed.width, compressed.height),
        dummyPixels.data(), false);

    if (!image) {
        return nullptr;
    }

    irr::video::ITexture* texture = driver_->addTexture(name.c_str(), image);
    image->drop();
    // Release the temporary dummy buffer immediately
    { std::vector<uint8_t>().swap(dummyPixels); }

    if (!texture) {
        return nullptr;
    }

    // Get the underlying OpenGL texture name
    irr::u32 glName = texture->getDriverTextureHandle();
    if (glName == 0) {
        driver_->removeTexture(texture);
        return nullptr;
    }

    // Copy compressed data to aligned buffer (ARM bus error safety)
    std::vector<uint8_t> alignedData(compressed.data, compressed.data + compressed.dataSize);

    // Resolve GL function pointers via EGL
    static auto pfnCompressedTexImage2D = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC_>(
        eglGetProcAddress("glCompressedTexImage2D"));
    static auto pfnGenerateMipmap = reinterpret_cast<PFNGLGENERATEMIPMAPPROC_>(
        eglGetProcAddress("glGenerateMipmap"));

    if (!pfnCompressedTexImage2D) {
        LOG_WARN(MOD_GRAPHICS, "Compressed upload: glCompressedTexImage2D not available");
        driver_->removeTexture(texture);
        return nullptr;
    }

    // Drain any stale GL errors left by Irrlicht's addTexture
    while (glGetError() != GL_NO_ERROR) {}

    // Upload compressed data directly to the GPU
    glBindTexture(GL_TEXTURE_2D, glName);
    pfnCompressedTexImage2D(GL_TEXTURE_2D, 0, compressed.glFormat,
                            compressed.width, compressed.height, 0,
                            static_cast<GLsizei>(alignedData.size()),
                            alignedData.data());

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_WARN(MOD_GRAPHICS, "Compressed upload: glCompressedTexImage2D failed for '{}' (GL error 0x{:X})",
            name, static_cast<unsigned>(err));
        driver_->removeTexture(texture);
        return nullptr;
    }

    // Generate mipmaps if enabled
    if (config_.enableMipmaps && pfnGenerateMipmap) {
        pfnGenerateMipmap(GL_TEXTURE_2D);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            // Mipmap generation failed — not fatal, texture still usable
            LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: glGenerateMipmap failed for '{}' (GL error 0x{:X})", name, static_cast<unsigned>(err));
        }
    }

    // Set bilinear filtering directly on GL texture (see getOrLoad comment)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (config_.enableMipmaps) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: '{}' {}x{} {} (DXT {} bytes, GPU {} bytes)",
        name, compressed.width, compressed.height,
        (compressed.glFormat == 0x83F0 ? "DXT1" :
         compressed.glFormat == 0x83F2 ? "DXT3" : "DXT5"),
        compressed.dataSize, textureSize);

    // DXT3 and DXT5 always have alpha; DXT1 RGBA has 1-bit alpha
    bool compressedHasAlpha = (compressed.glFormat != 0x83F0);  // Not GL_COMPRESSED_RGB_S3TC_DXT1

    // Add to cache
    lruOrder_.push_back(name);
    CachedTexture entry;
    entry.texture = texture;
    entry.sizeBytes = textureSize;
    entry.hasAlpha = compressedHasAlpha;
    entry.lruIterator = std::prev(lruOrder_.end());
    cache_[name] = entry;
    currentUsage_ += textureSize;
    ++compressedUploadCount_;

    return texture;
#else
    (void)name;
    (void)data;
    return nullptr;
#endif
}

} // namespace Graphics
} // namespace EQT
