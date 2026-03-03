#include "client/graphics/texture_atlas.h"
#ifdef EQT_HAS_GLES2
#include "client/graphics/gpu_upload_thread.h"
#include "client/graphics/work_priority.h"
#endif
#include "common/logging.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(EQT_HAS_GRAPHICS)
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <fmt/format.h>

namespace EQT {
namespace Graphics {

// On-disk structures (must match zone_atlas_builder.cpp)
#pragma pack(push, 1)
struct AtlasFileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t numPages;
    uint16_t numTiles;
    uint16_t atlasWidth;
    uint16_t atlasHeight;
    uint16_t tileSize;
    uint16_t tilesPerRow;
};

struct AtlasFilePageEntry {
    uint16_t pageIndex;
    uint8_t  format;
    uint16_t rgbPageIndex;
    uint32_t dataOffset;
    uint32_t dataSize;
};

struct AtlasFileTileEntry {
    char     textureName[64];
    uint16_t pageIndex;
    uint8_t  tileCol;
    uint8_t  tileRow;
    uint8_t  hasAlpha;
    uint16_t alphaPageIndex;
};
#pragma pack(pop)

// ETC1 GL extension constant
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

TextureAtlas::~TextureAtlas() {
    unload();
}

TextureAtlas::PreloadData TextureAtlas::preloadFromFile(const std::string& atlasPath) {
    PreloadData result;

    std::ifstream file(atlasPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas::preload: Cannot open {}", atlasPath);
        return result;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    // Read header
    AtlasFileHeader header;
    if (fileSize < sizeof(header)) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas::preload: File too small: {}", atlasPath);
        return result;
    }
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.magic != ATLAS_MAGIC) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas::preload: Bad magic in {}", atlasPath);
        return result;
    }
    if (header.version != ATLAS_VERSION) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas::preload: Unsupported version {} in {}", header.version, atlasPath);
        return result;
    }

    result.atlasWidth = header.atlasWidth;
    result.atlasHeight = header.atlasHeight;
    result.numPages = header.numPages;
    result.tileSize = header.tileSize;

    // Read page entries
    std::vector<AtlasFilePageEntry> pageEntries(header.numPages);
    file.read(reinterpret_cast<char*>(pageEntries.data()),
              sizeof(AtlasFilePageEntry) * header.numPages);

    // Read tile entries
    std::vector<AtlasFileTileEntry> tileEntries(header.numTiles);
    file.read(reinterpret_cast<char*>(tileEntries.data()),
              sizeof(AtlasFileTileEntry) * header.numTiles);

    // Read all ETC1 compressed data into per-page vectors
    result.pages.resize(header.numPages);
    for (int i = 0; i < header.numPages; ++i) {
        const auto& pe = pageEntries[i];
        auto& page = result.pages[i];
        page.dataSize = pe.dataSize;
        page.etc1Data.resize(pe.dataSize);
        file.seekg(pe.dataOffset);
        file.read(reinterpret_cast<char*>(page.etc1Data.data()), pe.dataSize);
    }

    // Build tile lookup (CPU-only: string lowercase + float UV math)
    float tileSize = static_cast<float>(header.tileSize);
    float atlasW = static_cast<float>(header.atlasWidth);
    float atlasH = static_cast<float>(header.atlasHeight);
    float border = static_cast<float>(ATLAS_TILE_BORDER);
    float inner = static_cast<float>(ATLAS_TILE_INNER);

    for (int i = 0; i < header.numTiles; ++i) {
        const auto& te = tileEntries[i];

        AtlasTileInfo info;
        info.pageIndex = te.pageIndex;
        info.uvOffsetU = (te.tileCol * tileSize + border) / atlasW;
        info.uvOffsetV = (te.tileRow * tileSize + border) / atlasH;
        info.uvScale = inner / atlasW;
        info.hasAlpha = (te.hasAlpha != 0);
        info.alphaPageIndex = te.alphaPageIndex;

        std::string name(te.textureName);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        result.tileLookup[name] = info;
    }

    result.valid = true;
    LOG_INFO(MOD_GRAPHICS, "TextureAtlas::preload: {} ({} pages, {} tiles, read from disk)",
             atlasPath, header.numPages, header.numTiles);

    return result;
}

bool TextureAtlas::uploadPreloadedPage(PreloadData& data, int pageIndex) {
#ifndef EQT_HAS_GRAPHICS
    return true;
#else
    if (!data.valid || pageIndex < 0 || pageIndex >= static_cast<int>(data.pages.size())) {
        return true;  // Nothing to do
    }

    // Ensure pageTextures_ vector is large enough
    if (pageTextures_.size() < data.pages.size()) {
        pageTextures_.resize(data.pages.size(), 0);
    }

    auto& page = data.pages[pageIndex];

    // Drain any stale GL errors from prior render frames — uploadPreloadedPage
    // is called once per GREEN frame with a full render pass in between, so
    // errors from drawMeshBuffer/setMaterial etc. would pollute our check.
    {
        GLenum stale;
        int staleCount = 0;
        while ((stale = glGetError()) != GL_NO_ERROR) {
            staleCount++;
            LOG_WARN(MOD_GRAPHICS, "TextureAtlas: drained stale GL error 0x{:04X} before page {} upload",
                     stale, pageIndex);
        }
        if (staleCount > 0) {
            LOG_WARN(MOD_GRAPHICS, "TextureAtlas: cleared {} stale GL error(s) before page {} upload",
                     staleCount, pageIndex);
        }
    }

    // Generate and upload one GL texture
    glGenTextures(1, &pageTextures_[pageIndex]);
    GLenum errGen = glGetError();
    if (errGen != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas: glGenTextures error 0x{:04X} for page {}", errGen, pageIndex);
    }

    glBindTexture(GL_TEXTURE_2D, pageTextures_[pageIndex]);
    GLenum errBind = glGetError();
    if (errBind != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas: glBindTexture error 0x{:04X} for page {} (texId={})",
                  errBind, pageIndex, pageTextures_[pageIndex]);
    }

    LOG_INFO(MOD_GRAPHICS, "TextureAtlas: uploading page {} ETC1 {}x{} size={} texId={}",
             pageIndex, data.atlasWidth, data.atlasHeight, page.dataSize, pageTextures_[pageIndex]);

    glCompressedTexImage2D(GL_TEXTURE_2D, 0,
                           GL_ETC1_RGB8_OES,
                           data.atlasWidth, data.atlasHeight,
                           0,
                           static_cast<GLsizei>(page.dataSize),
                           page.etc1Data.data());
    GLenum errUpload = glGetError();
    if (errUpload != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas: glCompressedTexImage2D error 0x{:04X} for page {} ({}x{}, {} bytes)",
                  errUpload, pageIndex, data.atlasWidth, data.atlasHeight, page.dataSize);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gpuMemoryUsage_ += page.dataSize;

    GLenum errParam = glGetError();
    if (errParam != GL_NO_ERROR) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas: glTexParameteri error 0x{:04X} for page {}", errParam, pageIndex);
    }

    // Free this page's ETC1 data now that it's on GPU
    page.etc1Data.clear();
    page.etc1Data.shrink_to_fit();

    bool isLast = (pageIndex >= static_cast<int>(data.pages.size()) - 1);
    return isLast;
#endif
}

#ifdef EQT_HAS_GLES2
bool TextureAtlas::uploadPreloadedPageAsync(PreloadData& data, int pageIndex,
                                             GPUUploadThread* uploadThread, uint32_t atlasType) {
    if (!data.valid || pageIndex < 0 || pageIndex >= static_cast<int>(data.pages.size())) {
        return true;  // Nothing to do
    }

    // Ensure pageTextures_ vector is large enough
    if (pageTextures_.size() < data.pages.size()) {
        pageTextures_.resize(data.pages.size(), 0);
    }

    auto& page = data.pages[pageIndex];

    if (!uploadThread || !uploadThread->isAvailable()) {
        LOG_ERROR(MOD_GRAPHICS, "TextureAtlas: GPU upload thread unavailable, skipping page {}", pageIndex);
        page.etc1Data.clear();
        page.etc1Data.shrink_to_fit();
        bool isLast = (pageIndex >= static_cast<int>(data.pages.size()) - 1);
        return isLast;
    }

    // Build upload request
    UploadRequest req;
    req.type = UploadRequestType::CompressedTexture;
    req.width = data.atlasWidth;
    req.height = data.atlasHeight;
    req.pixelData = std::move(page.etc1Data);
    req.compressedSize = page.dataSize;
    req.textureName = fmt::format("atlas_{}_{}", atlasType, pageIndex);
    // Encode atlas type + page index in callbackKey
    req.callbackKey = (static_cast<uint64_t>(atlasType) << 32) | static_cast<uint64_t>(pageIndex);
    req.priority = WorkPriorityKey::make(0, AssetType::ZoneTexture).value;

    uploadThread->submit(std::move(req));

    // Data moved out, clear source
    page.etc1Data.clear();
    page.etc1Data.shrink_to_fit();

    LOG_INFO(MOD_GRAPHICS, "TextureAtlas: queued async upload for page {} (type={}, {}x{}, {} bytes)",
             pageIndex, atlasType, data.atlasWidth, data.atlasHeight, page.dataSize);

    bool isLast = (pageIndex >= static_cast<int>(data.pages.size()) - 1);
    return isLast;
}

void TextureAtlas::setPageTexture(int pageIndex, uint32_t glTexId, size_t gpuBytes) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pageTextures_.size()))
        return;

    pageTextures_[pageIndex] = glTexId;
    gpuMemoryUsage_ += gpuBytes;

    LOG_DEBUG(MOD_GRAPHICS, "TextureAtlas: async page {} ready (texId={}, {} bytes)",
              pageIndex, glTexId, gpuBytes);
}
#endif // EQT_HAS_GLES2

void TextureAtlas::finalizePreload(PreloadData& data) {
    if (!data.valid) return;

    atlasWidth_ = data.atlasWidth;
    atlasHeight_ = data.atlasHeight;
    tileLookup_ = std::move(data.tileLookup);
    loaded_ = true;

    LOG_INFO(MOD_GRAPHICS, "TextureAtlas: Finalized preload ({} pages, {} tiles, {:.1f}MB GPU)",
             pageTextures_.size(), tileLookup_.size(),
             gpuMemoryUsage_ / (1024.0f * 1024.0f));

    data.valid = false;  // Consumed
}

void TextureAtlas::unload() {
#ifdef EQT_HAS_GRAPHICS
    if (!pageTextures_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(pageTextures_.size()), pageTextures_.data());
        pageTextures_.clear();
    }
#endif
    tileLookup_.clear();
    gpuMemoryUsage_ = 0;
    loaded_ = false;
}

const AtlasTileInfo* TextureAtlas::lookup(const std::string& textureName) const {
    auto it = tileLookup_.find(textureName);
    if (it != tileLookup_.end()) {
        return &it->second;
    }
    return nullptr;
}

uint32_t TextureAtlas::getPageTexture(uint16_t pageIndex) const {
    if (pageIndex < pageTextures_.size()) {
        return pageTextures_[pageIndex];
    }
    return 0;
}

} // namespace Graphics
} // namespace EQT
