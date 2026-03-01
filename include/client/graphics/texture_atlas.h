#ifndef EQT_GRAPHICS_TEXTURE_ATLAS_H
#define EQT_GRAPHICS_TEXTURE_ATLAS_H

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
#include <EGL/egl.h>
#endif

namespace EQT {
namespace Graphics {

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
// Forward declaration (only needed for DMA-BUF blit path, not native GLES2)
class GLES2EGLHelper;
#endif

#ifdef EQT_HAS_GLES2
class GPUUploadThread;
#endif

// Atlas file format constants (must match zone_atlas_builder.cpp)
static constexpr uint32_t ATLAS_MAGIC     = 0x54415145;  // "EQAT" little-endian
static constexpr uint16_t ATLAS_VERSION   = 1;
static constexpr int      ATLAS_TILE_SIZE = 256;
static constexpr int      ATLAS_TILE_BORDER = 4;
static constexpr int      ATLAS_TILE_INNER  = ATLAS_TILE_SIZE - 2 * ATLAS_TILE_BORDER; // 248

// Information about a single tile in the atlas
struct AtlasTileInfo {
    uint16_t pageIndex;       // Which atlas page this tile is on
    float uvOffsetU;          // U offset of tile start in atlas [0..1]
    float uvOffsetV;          // V offset of tile start in atlas [0..1]
    float uvScale;            // Scale factor: TILE_INNER / ATLAS_WIDTH
    bool hasAlpha;            // Whether this texture has alpha (dual ETC1)
    uint16_t alphaPageIndex;  // Page index for alpha data (if hasAlpha)
};

// Runtime loader for .atlas files built by zone_atlas_builder.
// Uploads ETC1 compressed pages to GL and provides per-texture tile lookup.
class TextureAtlas {
public:
    TextureAtlas() = default;
    ~TextureAtlas();

    // CPU-only preload result — file I/O + tile lookup, no GL calls.
    // Built on background thread, consumed on main thread via uploadPreloadedPage/finalizePreload.
    struct PreloadData {
        uint16_t atlasWidth = 0;
        uint16_t atlasHeight = 0;
        uint16_t numPages = 0;
        uint16_t tileSize = 0;
        struct PageData {
            std::vector<uint8_t> etc1Data;  // Raw compressed bytes
            uint32_t dataSize = 0;
        };
        std::vector<PageData> pages;
        std::map<std::string, AtlasTileInfo> tileLookup;
        bool valid = false;
    };

    // Background thread: reads file, builds tile lookup, stores ETC1 data in RAM.
    // No GL calls — safe to call from any thread.
    static PreloadData preloadFromFile(const std::string& atlasPath);

    // Main thread: uploads one page to GL. Call once per GREEN frame.
    // Returns true when this was the last page (all pages uploaded).
    bool uploadPreloadedPage(PreloadData& data, int pageIndex);

#ifdef EQT_HAS_GLES2
    // Main thread: submit one page upload to the GPU upload thread.
    // Returns true when this was the last page (all pages submitted).
    // The actual GL upload happens asynchronously; page texture handle is set
    // when processCompletedUploads() receives the result.
    bool uploadPreloadedPageAsync(PreloadData& data, int pageIndex,
                                  GPUUploadThread* uploadThread, uint32_t atlasType);

    // Set page texture handle from async upload result
    void setPageTexture(int pageIndex, uint32_t glTexId, size_t gpuBytes);
#endif

    // Main thread: adopts tile lookup from preload data, marks atlas as loaded.
    // Call after all pages have been uploaded.
    void finalizePreload(PreloadData& data);

    // Load an atlas file and upload ETC1 pages to GPU.
    // Returns true on success.
    bool load(const std::string& atlasPath);

#if defined(EQT_HAS_DRM) && !defined(EQT_HAS_GLES2)
    // Load with GLES2 EGL image sharing for ETC1 hardware decode on Lima/Mali.
    // Falls back to direct GL upload if gles2Helper is null or unavailable.
    // Not needed when using native GLES2 backend (direct glCompressedTexImage2D works).
    bool load(const std::string& atlasPath,
              GLES2EGLHelper* gles2Helper,
              EGLContext glContext, EGLSurface glSurface);
#endif

    // Unload all GPU textures and clear data
    void unload();

    // Look up a texture by name (lowercase, e.g. "wall01.bmp")
    // Returns nullptr if not in atlas
    const AtlasTileInfo* lookup(const std::string& textureName) const;

    // Get the GL texture handle for a page
    uint32_t getPageTexture(uint16_t pageIndex) const;

    // Get number of atlas pages
    uint16_t getPageCount() const { return static_cast<uint16_t>(pageTextures_.size()); }

    // Get number of tiles
    uint16_t getTileCount() const { return static_cast<uint16_t>(tileLookup_.size()); }

    // Get total GPU memory used by atlas pages (in bytes)
    size_t getGPUMemoryUsage() const { return gpuMemoryUsage_; }

    // Get atlas width/height
    uint16_t getAtlasWidth() const { return atlasWidth_; }
    uint16_t getAtlasHeight() const { return atlasHeight_; }

    bool isLoaded() const { return loaded_; }

private:
    bool loaded_ = false;
    uint16_t atlasWidth_ = 0;
    uint16_t atlasHeight_ = 0;

    // GL texture handles for each page
    std::vector<uint32_t> pageTextures_;

    // Per-texture tile lookup (keyed by lowercase name)
    std::map<std::string, AtlasTileInfo> tileLookup_;

    // Memory tracking
    size_t gpuMemoryUsage_ = 0;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_TEXTURE_ATLAS_H
