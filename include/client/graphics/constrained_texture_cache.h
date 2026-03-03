#ifndef EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
#define EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H

#include "client/graphics/constrained_renderer_config.h"
#include <irrlicht.h>
#include <map>
#include <list>
#include <mutex>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_set>

namespace EQT {
namespace Graphics {

class GPUUploadThread;
class BackgroundThreadPool;
class ZoneMeshBuilder;

// Listener interface for texture eviction notifications.
// Subsystems that register textures with the cache can implement this
// to null out local pointers when their textures are evicted.
class TextureEvictionListener {
public:
    virtual ~TextureEvictionListener() = default;
    virtual void onTextureEvicted(const std::string& name) = 0;
};

// Decoded texture pixels waiting for GPU upload
struct DecodedUpload {
    std::string name;
    std::vector<uint8_t> rgbaPixels;  // Decoded RGBA, ready for GPU
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

// LRU texture cache with memory budget enforcement
// Used for resource-constrained rendering modes (Voodoo1, etc.)
class ConstrainedTextureCache {
public:
    ConstrainedTextureCache(const ConstrainedRendererConfig& config,
                            irr::video::IVideoDriver* driver);
    ~ConstrainedTextureCache();

    // Get or load a texture
    // If texture is already cached, returns it and marks as recently used
    // If not cached, processes the texture data (downsample, convert to 16-bit)
    // and adds to cache, evicting LRU textures if needed
    // Returns nullptr if texture cannot be loaded
    irr::video::ITexture* getOrLoad(const std::string& name,
                                     const std::vector<char>& data);

    // Mark a texture as recently used (moves to back of LRU list)
    void touch(const std::string& name);

    // Check if texture is in cache
    bool hasTexture(const std::string& name) const;

    // Get a cached texture without loading (returns nullptr if not cached)
    irr::video::ITexture* getTexture(const std::string& name);

    // Register an externally-created texture into the LRU cache.
    // The texture must already be created via driver_->addTexture().
    // Performs LRU bookkeeping and eviction but skips decode/upload.
    // Returns true if registered, false if texture is too large for budget.
    bool registerTexture(const std::string& name, irr::video::ITexture* texture,
                         size_t sizeBytes, bool hasAlpha = false);

    // Check if a cached texture has alpha transparency
    bool hasAlpha(const std::string& name) const;

    // Clear all cached textures
    void clear();

    // Memory statistics
    size_t getCurrentUsage() const { return currentUsage_; }
    size_t getMemoryLimit() const { return config_.textureMemoryBytes; }
    size_t getAvailableMemory() const { return config_.textureMemoryBytes - currentUsage_; }
    size_t getTextureCount() const { return cache_.size(); }

    // Cache statistics
    size_t getCacheHits() const { return cacheHits_; }
    size_t getCacheMisses() const { return cacheMisses_; }
    size_t getEvictionCount() const { return evictionCount_; }
    size_t getCompressedUploadCount() const { return compressedUploadCount_; }

    // Get hit rate as percentage (0-100)
    float getHitRate() const;

    // Reset statistics
    void resetStatistics();

    // Freeze/unfreeze the cache
    // When frozen, no evictions occur (prevents crashes from dangling texture pointers)
    // Call freeze() after zone load is complete to protect zone textures
    void freeze() { frozen_ = true; }
    void unfreeze() { frozen_ = false; }
    bool isFrozen() const { return frozen_; }

    // Get config for debug display
    const ConstrainedRendererConfig& getConfig() const { return config_; }

    // Set GPU upload thread for async texture uploads (GLES2 only)
    void setGPUUploadThread(GPUUploadThread* thread) { gpuUploadThread_ = thread; }

    // Remove a texture name from the pending async set (called when upload completes)
    void clearPendingAsync(const std::string& name) { pendingAsyncUploads_.erase(name); }

    // Set scene manager for safe eviction (scans meshes to remove texture references)
    void setSceneManager(irr::scene::ISceneManager* smgr) { smgr_ = smgr; }

    // Get placeholder texture (used when a texture is evicted from a mesh material)
    irr::video::ITexture* getPlaceholderTexture();

    // Queue decoded RGBA pixels for budget-safe GPU upload on the render thread.
    // Thread-safe — may be called from background threads or the render thread.
    void queueDecoded(const std::string& name, std::vector<uint8_t> rgbaPixels,
                      int width, int height, bool hasAlpha);

    // Queue decoded ARGB pixels (Irrlicht ECF_A8R8G8B8 format) for GPU upload.
    // Converts ARGB → RGBA internally then pushes to the upload queue.
    // Thread-safe — may be called from background threads or the render thread.
    void queueDecodedARGB(const std::string& name, const std::vector<uint32_t>& argbPixels,
                          int width, int height, bool hasAlpha);

    // Process queued decoded textures: budget check, evict, GPU upload, register.
    // Call once per render frame from the main thread.
    // Returns the number of textures uploaded this frame.
    int processUploadQueue();

    // Check if a texture name is pending decode or upload (not yet in cache)
    bool isPending(const std::string& name) const;

    // Set background thread pool for async texture decode (non-owning)
    void setBackgroundThreadPool(BackgroundThreadPool* pool) { bgThreadPool_ = pool; }

    // Set mesh builder for entity texture registration (non-owning)
    void setMeshBuilder(ZoneMeshBuilder* meshBuilder) { meshBuilder_ = meshBuilder; }

    // Eviction listener management
    void addEvictionListener(TextureEvictionListener* listener);
    void removeEvictionListener(TextureEvictionListener* listener);

private:
    // Remove all references to a texture from mesh materials in the scene
    // This must be called before driver_->removeTexture() to prevent dangling pointers
    void clearTextureReferences(irr::video::ITexture* texture);
    // Probe for GL compressed texture support (called once in constructor)
    void probeCompressedTextureSupport();

    // Try uploading DDS data as compressed texture directly to GPU
    // Returns nullptr if compressed upload not available or data not suitable
    irr::video::ITexture* tryCompressedUpload(const std::string& name,
                                               const std::vector<char>& data);

    // Evict least recently used texture(s) until we have at least 'bytesNeeded' available
    // Returns true if successful, false if cannot free enough space
    bool evictUntilAvailable(size_t bytesNeeded);

    // Evict a single texture by name
    void evictTexture(const std::string& name);

    // Calculate texture size in bytes (after processing)
    size_t calculateTextureSize(int width, int height) const;

    // Process raw texture data: decode, downsample if needed, convert to 16-bit
    // Returns processed RGBA data with final dimensions
    // Sets hasAlpha to true if texture has meaningful alpha channel
    bool processTextureData(const std::vector<char>& rawData,
                            std::vector<uint8_t>& processedData,
                            int& width, int& height,
                            bool& hasAlpha);

    // Downsample texture data using box filter
    // Halves dimensions until both are <= maxDimension
    void downsampleToMaxSize(std::vector<uint8_t>& data,
                             int& width, int& height,
                             int channels);

    // Convert 32-bit RGBA to 16-bit format
    // Returns RGB565 for opaque, RGBA1555 for transparent
    std::vector<uint16_t> convertTo16Bit(const std::vector<uint8_t>& rgba,
                                          int width, int height,
                                          bool hasAlpha);

    // Detect if texture has meaningful alpha (not all 255)
    bool detectAlpha(const std::vector<uint8_t>& rgba, int width, int height);

    // Decode BMP data to RGBA
    bool decodeBMP(const std::vector<char>& data,
                   std::vector<uint8_t>& rgba,
                   int& width, int& height);

    // Decode DDS data to RGBA (uses existing DDSDecoder)
    bool decodeDDS(const std::vector<char>& data,
                   std::vector<uint8_t>& rgba,
                   int& width, int& height);

    // Cached texture entry
    struct CachedTexture {
        irr::video::ITexture* texture;
        size_t sizeBytes;
        bool hasAlpha = false;
        std::list<std::string>::iterator lruIterator;
    };

    ConstrainedRendererConfig config_;
    irr::video::IVideoDriver* driver_;
    irr::scene::ISceneManager* smgr_ = nullptr;

    // Placeholder texture for evicted textures (created on demand)
    irr::video::ITexture* placeholderTexture_ = nullptr;

    // Texture cache: name -> cached entry
    std::map<std::string, CachedTexture> cache_;

    // LRU order: front = oldest (evict first), back = newest
    std::list<std::string> lruOrder_;

    // Current memory usage
    size_t currentUsage_ = 0;

    // Statistics
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
    size_t evictionCount_ = 0;
    size_t compressedUploadCount_ = 0;

    // Compressed texture support
    bool compressedTexturesAvailable_ = false;

    // Frozen flag - when true, no evictions occur
    bool frozen_ = false;

    // GPU upload thread for async texture uploads (non-owning)
    GPUUploadThread* gpuUploadThread_ = nullptr;
    // Texture names currently being uploaded asynchronously (prevents duplicate submissions)
    std::unordered_set<std::string> pendingAsyncUploads_;

    // Background thread pool for async texture decode (non-owning)
    BackgroundThreadPool* bgThreadPool_ = nullptr;

    // Mesh builder for entity texture registration (non-owning)
    ZoneMeshBuilder* meshBuilder_ = nullptr;

    // Thread-safe queue of decoded textures waiting for GPU upload
    mutable std::mutex decodedQueueMutex_;
    std::vector<DecodedUpload> decodedQueue_;

    // Texture names submitted for background decode but not yet uploaded (render thread only)
    std::unordered_set<std::string> pendingDecodes_;

    // Eviction listeners (non-owning)
    std::vector<TextureEvictionListener*> evictionListeners_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
