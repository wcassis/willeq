#ifndef EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
#define EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H

#include "client/graphics/constrained_renderer_config.h"
#include <irrlicht.h>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <cstdint>

namespace EQT {
namespace Graphics {

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

    // Set scene manager for safe eviction (scans meshes to remove texture references)
    void setSceneManager(irr::scene::ISceneManager* smgr) { smgr_ = smgr; }

    // Get placeholder texture (used when a texture is evicted from a mesh material)
    irr::video::ITexture* getPlaceholderTexture();

private:
    // Remove all references to a texture from mesh materials in the scene
    // This must be called before driver_->removeTexture() to prevent dangling pointers
    void clearTextureReferences(irr::video::ITexture* texture);
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

    // Frozen flag - when true, no evictions occur
    bool frozen_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
