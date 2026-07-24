#ifndef EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
#define EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_format.h"
#include <irrlicht.h>
#include <map>
#include <list>
#include <mutex>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace EQT {
namespace Graphics {

class GPUUploadThread;
class BackgroundThreadPool;
class ZoneMeshBuilder;
class EQE1Loader;

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

// Decoded texture entry — decoded but not yet uploaded to GPU.
// Created by getOrLoad(), promoted to CachedTexture by uploadTexture().
struct DecodedEntry {
    std::vector<uint8_t> rgbaPixels;  // Decoded RGBA pixels
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

// LRU texture cache with memory budget enforcement
// Used for resource-constrained rendering modes (Voodoo1, etc.)
class ConstrainedTextureCache {
public:
    // Construct with config only (no driver needed). Call setDriver() later
    // when the graphics context is available for GPU uploads.
    explicit ConstrainedTextureCache(const ConstrainedRendererConfig& config);
    ~ConstrainedTextureCache();

    // Set video driver for GPU texture uploads. Must be called before
    // uploadTexture() or uploadAllPending(). Probes compressed texture support.
    void setDriver(irr::video::IVideoDriver* driver);

    // Decode and cache a texture (no GL calls — safe to call from any thread).
    // If already uploaded (CachedTexture), returns the ITexture*.
    // If already decoded (DecodedEntry), returns nullptr (call uploadTexture to promote).
    // If not in cache, decodes raw bytes to RGBA and stores as DecodedEntry, returns nullptr.
    // Returns nullptr on decode failure.
    irr::video::ITexture* getOrLoad(const std::string& name,
                                     const std::vector<char>& data);

    // Upload a decoded texture to the GPU.
    // Promotes DecodedEntry → CachedTexture via driver_->addTexture().
    // Must be called on a thread that owns the rendering context.
    // Returns the uploaded ITexture*, or nullptr if not found/failed.
    irr::video::ITexture* uploadTexture(const std::string& name);

    // Upload all pending decoded textures to the GPU.
    // Calls uploadTexture() for each DecodedEntry in the cache.
    // Used by the sequential loader after populating the cache with getOrLoad().
    // Returns the number of textures uploaded.
    int uploadAllPending();

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

    // Get ETC1 alpha companion texture (for binding to texture unit 1)
    // Returns nullptr if texture has no alpha companion.
    irr::video::ITexture* getAlphaTexture(const std::string& name) const;

    // Reverse-lookup overload for callers that only have the GPU texture
    // pointer (e.g. ZoneMeshBuilder::buildCompositeMesh, which builds from a
    // single merged texture with no single originating cache name). Linear
    // scan over the cache — fine given the current (small, mesh-build-time)
    // call volume.
    irr::video::ITexture* getAlphaTexture(irr::video::ITexture* texture) const;

    // H02d (Option A alpha-source-aware shader selection): does this cached
    // texture come from the ETC1/EQE1 pipeline (uploadCompressedETC1, which
    // builds a companion alpha texture on unit 1) or the direct RGBA/DDS
    // decode path (uploadTexture, which has real alpha in the main texture's
    // own alpha channel and never needs/builds a companion)? Callers use this
    // to pick the matching alpha-test shader variant. Returns false for
    // unknown/not-yet-uploaded names.
    bool isETC1Texture(const std::string& name) const;

    // Reverse-lookup overload by GPU texture pointer (see getAlphaTexture(ITexture*)).
    bool isETC1Texture(irr::video::ITexture* texture) const;

    // Clear all cached textures
    void clear();

    // Memory statistics
    size_t getCurrentUsage() const;
    size_t getMemoryLimit() const;
    size_t getAvailableMemory() const;
    size_t getTextureCount() const;

    // Cache statistics
    size_t getCacheHits() const;
    size_t getCacheMisses() const;
    size_t getEvictionCount() const;
    size_t getCompressedUploadCount() const;

    // Get hit rate as percentage (0-100)
    float getHitRate() const;

    // Reset statistics
    void resetStatistics();

    // Freeze/unfreeze the cache
    // When frozen, no evictions occur (prevents crashes from dangling texture pointers)
    // Call freeze() after zone load is complete to protect zone textures
    void freeze();
    void unfreeze();
    bool isFrozen() const;

    // Get config for debug display
    const ConstrainedRendererConfig& getConfig() const { return config_; }

    // Set GPU upload thread for async texture uploads (GLES2 only)
    void setGPUUploadThread(GPUUploadThread* thread);

    // Remove a texture name from the pending async set (called when upload completes)
    void clearPendingAsync(const std::string& name);

    // Set scene manager for safe eviction (scans meshes to remove texture references)
    void setSceneManager(irr::scene::ISceneManager* smgr);

    // Get placeholder texture (used when a texture is evicted from a mesh material)
    irr::video::ITexture* getPlaceholderTexture();

    // T04: queueDecoded, queueDecodedARGB, processUploadQueue, drainDecodedQueue,
    // processOneUpload, getStagedUploadCount removed. Use getOrLoad() + uploadTexture() instead.

    // Check if a texture name is pending decode or upload (not yet in cache)
    bool isPending(const std::string& name) const;

    // Check if any textures are pending decode or async upload
    bool hasPendingWork() const;

    // Set background thread pool for async texture decode (non-owning)
    void setBackgroundThreadPool(BackgroundThreadPool* pool);

    // Set mesh builder for entity texture registration (non-owning)
    void setMeshBuilder(ZoneMeshBuilder* meshBuilder);

    // Eviction listener management
    void addEvictionListener(TextureEvictionListener* listener);
    void removeEvictionListener(TextureEvictionListener* listener);

    // EQE1 flat pack loader management (GLES2 ETC1 compressed textures)
    void addEQE1Loader(const EQE1Loader* loader);
    void clearEQE1Loaders();

    // P04: global EQE1 archives are no longer opened eagerly at zone load.
    // This callback (set once by the renderer) opens the next not-yet-tried
    // global archive on a lookup miss and registers it via addEQE1Loader()
    // internally; returns nullptr once the list is exhausted. Only archives a
    // zone actually references end up mmap'd.
    void setEQE1LazyOpener(std::function<const EQE1Loader*()> opener) {
        eqe1LazyOpener_ = std::move(opener);
    }

    // Look up raw (non-texture) data from registered EQE1 loaders.
    // Used for JSON metadata, WLD files, etc. packed as EQE1_TYPE_RAW_DATA entries.
    // Returns true if found, with data pointer and size. Data is owned by the EQE1 loader.
    bool getEQE1RawData(const std::string& name, const uint8_t*& data, uint32_t& size) const;

private:
    // Look up a texture name in registered EQE1 loaders.
    // Strips .bmp extension and lowercases before lookup.
    // Returns nullptr if not found in any loader.
    struct CompressedEntry {
        const uint8_t* etc1Data;  // v2: raw uncompressed payload bytes (name kept for churn's sake)
        uint32_t dataSize;
        uint16_t width;
        uint16_t height;
        uint8_t  mipLevels;
        bool hasAlpha;
        const uint8_t* alphaData;
        uint32_t alphaDataSize;
        uint8_t  payloadFormat = 0;  // EQE1PayloadFormat; 0 == EQE1_PAYLOAD_ETC1
    };
    const CompressedEntry* findCompressedEntry(const std::string& name);

    // P04: copy of the loader-pointer vector taken under eqe1LoadersMutex_.
    std::vector<const EQE1Loader*> snapshotEQE1Loaders() const;

    // Upload pre-compressed ETC1 data to GPU via glCompressedTexImage2D.
    // Creates GL texture, wraps in ITexture*, registers in LRU cache.
    // Must be called on a thread with GL context. Caller holds mutex_.
    // ETC1/v1 path — byte-for-byte untouched by the v2 payload-format work.
    irr::video::ITexture* uploadCompressedETC1(const std::string& name,
                                                const CompressedEntry& entry);

    // Upload an uncompressed v2 payload (RGB565/RGBA4444/RGBA8888) to GPU via
    // glTexImage2D with the format/type matching entry.payloadFormat. No
    // alpha companion is ever built (v2 alpha lives in the main texture's own
    // alpha channel) — CachedTexture::isETC1 stays false, which routes these
    // textures to H02d's main-alpha shader variant. Caller holds mutex_.
    irr::video::ITexture* uploadCompressedV2(const std::string& name,
                                              const CompressedEntry& entry);

    // Move name to back of LRU list — call only while holding mutex_
    void touchInternal(const std::string& name);

    // Get or create placeholder — call only while holding mutex_
    irr::video::ITexture* getPlaceholderTextureInternal();

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

    // Cached texture entry
    struct CachedTexture {
        irr::video::ITexture* texture;
        irr::video::ITexture* alphaTexture = nullptr;  // ETC1 alpha companion (unit 1)
        size_t sizeBytes;
        bool hasAlpha = false;
        // H02d: true if uploaded via uploadCompressedETC1 (companion-based
        // alpha), false if uploaded via uploadTexture (direct RGBA/DDS decode,
        // alpha lives in the main texture's own alpha channel).
        bool isETC1 = false;
        std::list<std::string>::iterator lruIterator;
    };

    // Protects all state except decodedQueue_ (which has its own decodedQueueMutex_)
    mutable std::mutex mutex_;

    ConstrainedRendererConfig config_;
    irr::video::IVideoDriver* driver_;
    irr::scene::ISceneManager* smgr_ = nullptr;

    // Placeholder texture for evicted textures (created on demand)
    irr::video::ITexture* placeholderTexture_ = nullptr;

    // Texture cache: name -> uploaded texture entry
    std::map<std::string, CachedTexture> cache_;

    // Decoded but not yet uploaded: name -> decoded RGBA pixels
    std::map<std::string, DecodedEntry> decodedEntries_;

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

    // T04: decodedQueueMutex_, decodedQueue_, stagedUploads_, pendingDecodes_ removed.
    // Replaced by decodedEntries_ (decode) + uploadTexture() (upload).

    // Eviction listeners (non-owning)
    std::vector<TextureEvictionListener*> evictionListeners_;

    // EQE1 flat pack loaders (non-owning, populated per zone).
    // P04: guarded by eqe1LoadersMutex_ (NOT mutex_) — addEQE1Loader is called
    // from prep-worker/equipment threads via getOrOpenEQE1 while the render
    // thread iterates the vector inside findCompressedEntry/getEQE1RawData;
    // with lazy global-archive opening those two now overlap routinely. A
    // separate mutex (not mutex_) because the lazy opener runs while mutex_ is
    // held and re-enters addEQE1Loader on the same thread.
    std::vector<const EQE1Loader*> eqe1Loaders_;
    mutable std::mutex eqe1LoadersMutex_;

    // Compressed entries pending GPU upload (from EQE1 lookup)
    std::map<std::string, CompressedEntry> compressedEntries_;

    // P04: lazy-open callback for global EQE1 archives (see setEQE1LazyOpener).
    std::function<const EQE1Loader*()> eqe1LazyOpener_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
