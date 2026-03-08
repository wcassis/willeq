// zone_atlas_builder - Offline tool to build ETC1-compressed texture atlases from EQ zone S3D archives.
//
// Reads zone and zone_obj S3D archives, extracts textures (DDS-as-BMP and real BMP),
// decodes to RGBA, bin-packs into 2048x2048 atlas pages, compresses to ETC1 via Etc2Comp,
// and writes .atlas binary files for fast runtime loading on Mali 400.
//
// Usage:
//   ./zone_atlas_builder --eq-path /path/to/EverQuestP1999 --zone qeynos2 --output ./atlases/
//   ./zone_atlas_builder --eq-path /path/to/EverQuestP1999 --all --output ./atlases/

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/dds_decoder.h"
#include <Etc.h>
#include <EtcImage.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── Atlas file format constants ──────────────────────────────────────────────
static constexpr uint32_t ATLAS_MAGIC    = 0x54415145;  // "EQAT" little-endian
static constexpr uint16_t ATLAS_VERSION  = 2;
static constexpr uint16_t ATLAS_WIDTH    = 2048;
static constexpr uint16_t ATLAS_HEIGHT   = 2048;
static constexpr uint16_t TILE_SIZE      = 256;
static constexpr uint16_t TILES_PER_ROW  = ATLAS_WIDTH / TILE_SIZE;   // 8
static constexpr int       TILE_BORDER   = 4;
static constexpr int       TILE_INNER    = TILE_SIZE - 2 * TILE_BORDER;  // 248
static constexpr int       TILES_PER_PAGE = TILES_PER_ROW * TILES_PER_ROW; // 64

// Per-page format
static constexpr uint8_t FORMAT_ETC1_RGB   = 0;
static constexpr uint8_t FORMAT_ETC1_ALPHA = 1;

// ── On-disk structures (packed) ──────────────────────────────────────────────
#pragma pack(push, 1)
struct AtlasHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t numPages;
    uint16_t numTiles;
    uint16_t atlasWidth;
    uint16_t atlasHeight;
    uint16_t tileSize;
    uint16_t tilesPerRow;
    uint8_t  mipLevels;    // v2: number of ETC1 mip levels (e.g. 12 for 2048x2048)
};

struct AtlasPageEntry {
    uint16_t pageIndex;
    uint8_t  format;           // FORMAT_ETC1_RGB or FORMAT_ETC1_ALPHA
    uint16_t rgbPageIndex;     // For alpha pages: corresponding RGB page; 0xFFFF for RGB pages
    uint32_t dataOffset;
    uint32_t dataSize;
};

struct AtlasTileEntry {
    char     textureName[64];
    uint16_t pageIndex;
    uint8_t  tileCol;
    uint8_t  tileRow;
    uint8_t  hasAlpha;
    uint16_t alphaPageIndex;
};
#pragma pack(pop)

// ── Tile data ────────────────────────────────────────────────────────────────
struct DecodedTile {
    std::string name;
    std::vector<uint8_t> rgba;   // 256x256 RGBA after resize + border
    bool hasAlpha = false;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Simple box-filter resize from arbitrary dimensions to 248x248 RGBA
static void resizeTo248(const uint8_t* src, int srcW, int srcH,
                        uint8_t* dst) {
    const int dstW = TILE_INNER;
    const int dstH = TILE_INNER;
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            float srcXf = static_cast<float>(x) * srcW / dstW;
            float srcYf = static_cast<float>(y) * srcH / dstH;
            int sx = static_cast<int>(srcXf);
            int sy = static_cast<int>(srcYf);
            if (sx >= srcW) sx = srcW - 1;
            if (sy >= srcH) sy = srcH - 1;
            int idx = (sy * srcW + sx) * 4;
            int didx = (y * dstW + x) * 4;
            dst[didx + 0] = src[idx + 0];
            dst[didx + 1] = src[idx + 1];
            dst[didx + 2] = src[idx + 2];
            dst[didx + 3] = src[idx + 3];
        }
    }
}

// Place a 248x248 tile into a 256x256 slot with 4px wrap-around borders.
// Output: 256x256 RGBA
static void placeTileWithBorder(const uint8_t* inner248, uint8_t* out256) {
    const int inner = TILE_INNER;  // 248
    const int outer = TILE_SIZE;   // 256
    const int border = TILE_BORDER; // 4

    // Clear output
    std::memset(out256, 0, outer * outer * 4);

    // Copy inner content to center
    for (int y = 0; y < inner; ++y) {
        std::memcpy(out256 + ((y + border) * outer + border) * 4,
                    inner248 + (y * inner) * 4,
                    inner * 4);
    }

    // Wrap-around borders: left/right
    for (int y = 0; y < inner; ++y) {
        int oy = y + border;
        // Left border: repeat from right edge of inner
        for (int bx = 0; bx < border; ++bx) {
            int srcX = inner - border + bx;
            std::memcpy(out256 + (oy * outer + bx) * 4,
                        inner248 + (y * inner + srcX) * 4, 4);
        }
        // Right border: repeat from left edge of inner
        for (int bx = 0; bx < border; ++bx) {
            std::memcpy(out256 + (oy * outer + border + inner + bx) * 4,
                        inner248 + (y * inner + bx) * 4, 4);
        }
    }

    // Wrap-around borders: top/bottom (including corners)
    for (int x = 0; x < outer; ++x) {
        // Top border: copy from bottom rows of the same column
        for (int by = 0; by < border; ++by) {
            int srcRow = inner - border + by + border;  // row in output
            std::memcpy(out256 + (by * outer + x) * 4,
                        out256 + (srcRow * outer + x) * 4, 4);
        }
        // Bottom border: copy from top rows of the same column
        for (int by = 0; by < border; ++by) {
            int dstRow = border + inner + by;
            int srcRow = border + by;
            std::memcpy(out256 + (dstRow * outer + x) * 4,
                        out256 + (srcRow * outer + x) * 4, 4);
        }
    }
}

// Check if an RGBA image has any pixels with alpha < 255
static bool detectAlpha(const uint8_t* rgba, int w, int h) {
    for (int i = 0; i < w * h; ++i) {
        if (rgba[i * 4 + 3] < 255) return true;
    }
    return false;
}

// Simple BMP decoder for 8-bit indexed and 24-bit uncompressed BMPs
static bool decodeBMP(const std::vector<char>& data,
                      std::vector<uint8_t>& outRGBA,
                      int& outWidth, int& outHeight) {
    if (data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());

    // BMP header
    if (d[0] != 'B' || d[1] != 'M') return false;

    uint32_t dataOffset = *reinterpret_cast<const uint32_t*>(d + 10);
    int32_t width  = *reinterpret_cast<const int32_t*>(d + 18);
    int32_t height = *reinterpret_cast<const int32_t*>(d + 22);
    uint16_t bpp   = *reinterpret_cast<const uint16_t*>(d + 28);
    uint32_t compression = *reinterpret_cast<const uint32_t*>(d + 30);

    if (width <= 0 || width > 4096) return false;
    bool bottomUp = height > 0;
    if (height < 0) height = -height;
    if (height <= 0 || height > 4096) return false;

    outWidth = width;
    outHeight = height;
    outRGBA.resize(width * height * 4);

    if (bpp == 8 && compression == 0) {
        // 8-bit indexed - read palette (256 entries x 4 bytes: BGRA)
        const uint8_t* palette = d + 54;
        size_t paletteSize = 256 * 4;
        if (54 + paletteSize > data.size()) return false;

        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width + 3) & ~3;  // rows padded to 4 bytes

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = pixels[srcY * rowStride + x];
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = palette[idx * 4 + 2]; // R
                outRGBA[outIdx + 1] = palette[idx * 4 + 1]; // G
                outRGBA[outIdx + 2] = palette[idx * 4 + 0]; // B
                outRGBA[outIdx + 3] = 255;                   // A
            }
        }
        return true;
    } else if (bpp == 24 && compression == 0) {
        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width * 3 + 3) & ~3;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                int srcIdx = srcY * rowStride + x * 3;
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = pixels[srcIdx + 2]; // R
                outRGBA[outIdx + 1] = pixels[srcIdx + 1]; // G
                outRGBA[outIdx + 2] = pixels[srcIdx + 0]; // B
                outRGBA[outIdx + 3] = 255;                 // A
            }
        }
        return true;
    } else if (bpp == 32 && compression == 0) {
        const uint8_t* pixels = d + dataOffset;
        int rowStride = width * 4;

        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                int srcIdx = srcY * rowStride + x * 4;
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = pixels[srcIdx + 2]; // R
                outRGBA[outIdx + 1] = pixels[srcIdx + 1]; // G
                outRGBA[outIdx + 2] = pixels[srcIdx + 0]; // B
                outRGBA[outIdx + 3] = pixels[srcIdx + 3]; // A
            }
        }
        return true;
    }

    return false;
}

// ── Mipmap generation (helpers before ETC1, mip chain generator after) ───────

// Box-filter downsample RGBA image by 2x in each dimension
static std::vector<uint8_t> downsample2x(const uint8_t* src, int srcW, int srcH) {
    int dstW = std::max(1, srcW / 2);
    int dstH = std::max(1, srcH / 2);
    std::vector<uint8_t> dst(dstW * dstH * 4);

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            int sx = x * 2;
            int sy = y * 2;
            // Clamp for odd-sized sources
            int sx1 = std::min(sx + 1, srcW - 1);
            int sy1 = std::min(sy + 1, srcH - 1);

            for (int c = 0; c < 4; ++c) {
                int sum = src[(sy * srcW + sx) * 4 + c]
                        + src[(sy * srcW + sx1) * 4 + c]
                        + src[(sy1 * srcW + sx) * 4 + c]
                        + src[(sy1 * srcW + sx1) * 4 + c];
                dst[(y * dstW + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
    return dst;
}

// Compute number of mip levels for a given dimension (down to 1x1)
static int computeMipLevels(int w, int h) {
    int levels = 1;
    while (w > 1 || h > 1) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        levels++;
    }
    return levels;
}

// ── ETC1 encoding using Etc2Comp ─────────────────────────────────────────────

// Encode an RGBA atlas page (2048x2048) to ETC1 RGB.
// Returns compressed ETC1 data (~2MB for 2048x2048).
static std::vector<uint8_t> encodeETC1_RGB(const uint8_t* rgba, int width, int height) {
    // Etc2Comp expects float RGBA in [0,1]
    std::vector<float> floatPixels(width * height * 4);
    for (int i = 0; i < width * height * 4; ++i) {
        floatPixels[i] = rgba[i] / 255.0f;
    }

    unsigned char* encodingBits = nullptr;
    unsigned int encodingBitsBytes = 0;
    unsigned int extendedWidth = 0, extendedHeight = 0;
    int encodingTimeMs = 0;

    Etc::Encode(floatPixels.data(),
                static_cast<unsigned int>(width),
                static_cast<unsigned int>(height),
                Etc::Image::Format::ETC1,
                Etc::ErrorMetric::RGBA,
                ETCCOMP_DEFAULT_EFFORT_LEVEL,
                1,      // jobs
                1024,   // max jobs
                &encodingBits,
                &encodingBitsBytes,
                &extendedWidth,
                &extendedHeight,
                &encodingTimeMs);

    std::vector<uint8_t> result;
    if (encodingBits && encodingBitsBytes > 0) {
        result.resize(encodingBitsBytes);
        std::memcpy(result.data(), encodingBits, encodingBitsBytes);
        delete[] encodingBits;
    }
    return result;
}

// Encode alpha channel as grayscale into ETC1 (R=G=B=alpha value).
static std::vector<uint8_t> encodeETC1_Alpha(const uint8_t* rgba, int width, int height) {
    std::vector<float> floatPixels(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        float a = rgba[i * 4 + 3] / 255.0f;
        floatPixels[i * 4 + 0] = a;
        floatPixels[i * 4 + 1] = a;
        floatPixels[i * 4 + 2] = a;
        floatPixels[i * 4 + 3] = 1.0f;
    }

    unsigned char* encodingBits = nullptr;
    unsigned int encodingBitsBytes = 0;
    unsigned int extendedWidth = 0, extendedHeight = 0;
    int encodingTimeMs = 0;

    Etc::Encode(floatPixels.data(),
                static_cast<unsigned int>(width),
                static_cast<unsigned int>(height),
                Etc::Image::Format::ETC1,
                Etc::ErrorMetric::RGBA,
                ETCCOMP_DEFAULT_EFFORT_LEVEL,
                1, 1024,
                &encodingBits,
                &encodingBitsBytes,
                &extendedWidth,
                &extendedHeight,
                &encodingTimeMs);

    std::vector<uint8_t> result;
    if (encodingBits && encodingBitsBytes > 0) {
        result.resize(encodingBitsBytes);
        std::memcpy(result.data(), encodingBits, encodingBitsBytes);
        delete[] encodingBits;
    }
    return result;
}

// ── Mipmap chain generation (uses ETC1 encoders above) ──────────────────────

// Generate full mip chain from RGBA page and ETC1-encode each level.
// Returns contiguous ETC1 data for all levels.
static std::vector<uint8_t> generateMipChainETC1(const uint8_t* rgba, int width, int height,
                                                   bool isAlpha) {
    int levels = computeMipLevels(width, height);
    std::vector<uint8_t> result;

    // Level 0: encode the full-size page
    auto level0 = isAlpha ? encodeETC1_Alpha(rgba, width, height)
                          : encodeETC1_RGB(rgba, width, height);
    result.insert(result.end(), level0.begin(), level0.end());

    // Subsequent levels: downsample and encode
    std::vector<uint8_t> current(rgba, rgba + width * height * 4);
    int w = width, h = height;

    for (int level = 1; level < levels; ++level) {
        auto downsampled = downsample2x(current.data(), w, h);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);

        auto encoded = isAlpha ? encodeETC1_Alpha(downsampled.data(), w, h)
                               : encodeETC1_RGB(downsampled.data(), w, h);
        result.insert(result.end(), encoded.begin(), encoded.end());

        current = std::move(downsampled);
    }

    return result;
}

// ── Main atlas builder ───────────────────────────────────────────────────────

struct TextureData {
    std::string name;
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    bool isAnimated = false;
};

static std::vector<TextureData> extractTextures(EQT::Graphics::PfsArchive& archive) {
    std::vector<TextureData> result;
    std::vector<std::string> bmpFiles;
    archive.getFilenames(".bmp", bmpFiles);

    for (auto& filename : bmpFiles) {
        std::vector<char> data;
        if (!archive.get(filename, data) || data.empty()) continue;

        TextureData tex;
        tex.name = toLower(filename);

        // Check if it's actually DDS (common in EQ - DDS with .bmp extension)
        if (EQT::Graphics::DDSDecoder::isDDS(data)) {
            auto decoded = EQT::Graphics::DDSDecoder::decode(data);
            if (!decoded.isValid()) continue;
            tex.width = decoded.width;
            tex.height = decoded.height;
            tex.rgba = std::move(decoded.pixels);
        } else {
            // Real BMP
            if (!decodeBMP(data, tex.rgba, tex.width, tex.height)) continue;
        }

        tex.hasAlpha = detectAlpha(tex.rgba.data(), tex.width, tex.height);
        result.push_back(std::move(tex));
    }

    return result;
}

struct AtlasPage {
    std::vector<uint8_t> rgbaData;  // 2048x2048 RGBA
    std::vector<int> tileIndices;   // Which tiles are on this page (indices into tiles vector)
    bool isAlphaPage = false;
    int rgbPageIdx = -1;            // For alpha pages: index of the corresponding RGB page

    AtlasPage() : rgbaData(ATLAS_WIDTH * ATLAS_HEIGHT * 4, 0) {}
};

struct TilePlacement {
    std::string name;
    int pageIndex;
    int tileCol;
    int tileRow;
    bool hasAlpha;
    int alphaPageIndex;
};

static bool buildAtlas(const std::string& eqPath,
                       const std::string& archiveName,
                       const std::string& outputPath) {
    // Open the archive
    std::string archivePath = eqPath + "/" + archiveName;
    EQT::Graphics::PfsArchive archive;
    if (!archive.open(archivePath)) {
        std::cerr << "  Could not open: " << archivePath << std::endl;
        return false;
    }

    // Extract all textures
    auto textures = extractTextures(archive);
    if (textures.empty()) {
        std::cout << "  No textures found in " << archiveName << std::endl;
        return false;
    }

    // Filter out animated textures (they'll use fallback per-texture rendering)
    // In zone S3Ds, animated textures are rare and identified by the WLD loader.
    // Since we don't parse WLD here, we include all textures. Animated ones will
    // be excluded at runtime when buildAtlasedMesh checks textureAnimations.

    std::cout << "  Extracted " << textures.size() << " textures" << std::endl;

    // Separate into opaque and alpha
    std::vector<size_t> opaqueIndices, alphaIndices;
    for (size_t i = 0; i < textures.size(); ++i) {
        if (textures[i].hasAlpha) {
            alphaIndices.push_back(i);
        } else {
            opaqueIndices.push_back(i);
        }
    }

    std::cout << "  Opaque: " << opaqueIndices.size()
              << ", Alpha: " << alphaIndices.size() << std::endl;

    // Prepare 256x256 tile data for each texture (with borders)
    struct PreparedTile {
        size_t texIndex;
        std::vector<uint8_t> tileRGBA;  // 256x256 RGBA with border
    };

    auto prepareTile = [&](size_t texIndex) -> PreparedTile {
        auto& tex = textures[texIndex];
        PreparedTile pt;
        pt.texIndex = texIndex;

        // Resize to 248x248
        std::vector<uint8_t> inner(TILE_INNER * TILE_INNER * 4);
        resizeTo248(tex.rgba.data(), tex.width, tex.height, inner.data());

        // Place with wrap-around border into 256x256
        pt.tileRGBA.resize(TILE_SIZE * TILE_SIZE * 4);
        placeTileWithBorder(inner.data(), pt.tileRGBA.data());

        return pt;
    };

    // Build pages
    std::vector<AtlasPage> pages;
    std::vector<TilePlacement> placements;

    auto packTiles = [&](const std::vector<size_t>& indices, bool needsAlpha) {
        int tilesPlaced = 0;
        int pageStart = static_cast<int>(pages.size());

        for (size_t idx : indices) {
            // Find page with space or create new one
            int targetPage = -1;
            for (int p = pageStart; p < static_cast<int>(pages.size()); ++p) {
                if (static_cast<int>(pages[p].tileIndices.size()) < TILES_PER_PAGE) {
                    targetPage = p;
                    break;
                }
            }
            if (targetPage < 0) {
                pages.emplace_back();
                targetPage = static_cast<int>(pages.size()) - 1;
            }

            auto& page = pages[targetPage];
            int slot = static_cast<int>(page.tileIndices.size());
            int col = slot % TILES_PER_ROW;
            int row = slot / TILES_PER_ROW;

            // Prepare the tile
            auto prepared = prepareTile(idx);

            // Blit into atlas page RGBA
            for (int y = 0; y < TILE_SIZE; ++y) {
                int pageY = row * TILE_SIZE + y;
                int pageX = col * TILE_SIZE;
                std::memcpy(page.rgbaData.data() + (pageY * ATLAS_WIDTH + pageX) * 4,
                            prepared.tileRGBA.data() + (y * TILE_SIZE) * 4,
                            TILE_SIZE * 4);
            }

            page.tileIndices.push_back(static_cast<int>(idx));

            TilePlacement tp;
            tp.name = textures[idx].name;
            tp.pageIndex = targetPage;
            tp.tileCol = col;
            tp.tileRow = row;
            tp.hasAlpha = textures[idx].hasAlpha;
            tp.alphaPageIndex = -1;  // Filled in later
            placements.push_back(tp);

            tilesPlaced++;
        }
    };

    // Pack opaque tiles
    packTiles(opaqueIndices, false);

    // Remember where alpha RGB pages start for alpha page linking
    int alphaRGBPageStart = static_cast<int>(pages.size());

    // Pack alpha tiles (into their own pages for separate alpha encoding)
    packTiles(alphaIndices, true);

    std::cout << "  Atlas pages: " << pages.size() << std::endl;

    // Encode pages to ETC1 with full mip chains
    int mipLevels = computeMipLevels(ATLAS_WIDTH, ATLAS_HEIGHT);
    std::cout << "  Generating " << mipLevels << " mip levels per page" << std::endl;

    struct EncodedPage {
        std::vector<uint8_t> etc1Data;  // All mip levels contiguous
        uint8_t format;
        int rgbPageIndex;  // For alpha pages
    };
    std::vector<EncodedPage> encodedPages;

    // Encode RGB for all pages (with mip chain)
    for (size_t p = 0; p < pages.size(); ++p) {
        std::cout << "  Encoding page " << p << " RGB (mipmapped)..." << std::flush;
        EncodedPage ep;
        ep.etc1Data = generateMipChainETC1(pages[p].rgbaData.data(), ATLAS_WIDTH, ATLAS_HEIGHT, false);
        ep.format = FORMAT_ETC1_RGB;
        ep.rgbPageIndex = 0xFFFF;
        encodedPages.push_back(std::move(ep));
        std::cout << " done (" << encodedPages.back().etc1Data.size() << " bytes)" << std::endl;
    }

    // Encode alpha pages for pages containing alpha textures (with mip chain)
    for (size_t p = alphaRGBPageStart; p < pages.size(); ++p) {
        std::cout << "  Encoding page " << p << " alpha (mipmapped)..." << std::flush;
        EncodedPage ep;
        ep.etc1Data = generateMipChainETC1(pages[p].rgbaData.data(), ATLAS_WIDTH, ATLAS_HEIGHT, true);
        ep.format = FORMAT_ETC1_ALPHA;
        ep.rgbPageIndex = static_cast<int>(p);
        int alphaPageIdx = static_cast<int>(encodedPages.size());
        encodedPages.push_back(std::move(ep));
        std::cout << " done" << std::endl;

        // Update placements for tiles on this page to reference the alpha page
        for (auto& tp : placements) {
            if (tp.pageIndex == static_cast<int>(p) && tp.hasAlpha) {
                tp.alphaPageIndex = alphaPageIdx;
            }
        }
    }

    // Write .atlas file
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "  Cannot open output file: " << outputPath << std::endl;
        return false;
    }

    // Write header
    AtlasHeader header;
    header.magic = ATLAS_MAGIC;
    header.version = ATLAS_VERSION;
    header.numPages = static_cast<uint16_t>(encodedPages.size());
    header.numTiles = static_cast<uint16_t>(placements.size());
    header.atlasWidth = ATLAS_WIDTH;
    header.atlasHeight = ATLAS_HEIGHT;
    header.tileSize = TILE_SIZE;
    header.tilesPerRow = TILES_PER_ROW;
    header.mipLevels = static_cast<uint8_t>(mipLevels);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Calculate data offsets
    uint32_t dataStart = sizeof(AtlasHeader)
                       + sizeof(AtlasPageEntry) * encodedPages.size()
                       + sizeof(AtlasTileEntry) * placements.size();

    // Write page entries
    uint32_t currentOffset = dataStart;
    for (size_t p = 0; p < encodedPages.size(); ++p) {
        AtlasPageEntry pe;
        pe.pageIndex = static_cast<uint16_t>(p);
        pe.format = encodedPages[p].format;
        pe.rgbPageIndex = static_cast<uint16_t>(encodedPages[p].rgbPageIndex);
        pe.dataOffset = currentOffset;
        pe.dataSize = static_cast<uint32_t>(encodedPages[p].etc1Data.size());
        out.write(reinterpret_cast<const char*>(&pe), sizeof(pe));
        currentOffset += pe.dataSize;
    }

    // Write tile entries
    for (const auto& tp : placements) {
        AtlasTileEntry te;
        std::memset(te.textureName, 0, sizeof(te.textureName));
        std::strncpy(te.textureName, tp.name.c_str(), sizeof(te.textureName) - 1);
        te.pageIndex = static_cast<uint16_t>(tp.pageIndex);
        te.tileCol = static_cast<uint8_t>(tp.tileCol);
        te.tileRow = static_cast<uint8_t>(tp.tileRow);
        te.hasAlpha = tp.hasAlpha ? 1 : 0;
        te.alphaPageIndex = static_cast<uint16_t>(tp.alphaPageIndex >= 0 ? tp.alphaPageIndex : 0xFFFF);
        out.write(reinterpret_cast<const char*>(&te), sizeof(te));
    }

    // Write ETC1 data
    for (const auto& ep : encodedPages) {
        out.write(reinterpret_cast<const char*>(ep.etc1Data.data()), ep.etc1Data.size());
    }

    out.close();

    size_t fileSize = fs::file_size(outputPath);
    std::cout << "  Written: " << outputPath << " (" << fileSize / 1024 << " KB, "
              << placements.size() << " tiles, "
              << encodedPages.size() << " pages)" << std::endl;

    return true;
}

// ── Zone listing ─────────────────────────────────────────────────────────────

static std::vector<std::string> listZones(const std::string& eqPath) {
    std::set<std::string> zones;
    for (const auto& entry : fs::directory_iterator(eqPath)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        std::string lower = toLower(name);

        // Match zone.s3d files (not _obj, _chr, etc.)
        if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".s3d") {
            std::string base = lower.substr(0, lower.size() - 4);
            // Skip _obj, _chr, _2obj, global*, etc.
            if (base.find("_obj") != std::string::npos) continue;
            if (base.find("_chr") != std::string::npos) continue;
            if (base.find("_lit") != std::string::npos) continue;
            if (base.find("global") != std::string::npos) continue;
            if (base.find("snd") == 0) continue;  // sndXXX.pfs
            zones.insert(base);
        }
    }
    return std::vector<std::string>(zones.begin(), zones.end());
}

// ── Entry point ──────────────────────────────────────────────────────────────

static void printUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " --eq-path <path> --zone <name> --output <dir>\n"
              << "       " << argv0 << " --eq-path <path> --all --output <dir>\n"
              << "\nOptions:\n"
              << "  --eq-path <path>   Path to EverQuest client directory\n"
              << "  --zone <name>      Zone short name (e.g., qeynos2)\n"
              << "  --all              Process all zones\n"
              << "  --output <dir>     Output directory for .atlas files\n"
              << "  --list             List available zones and exit\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string zoneName;
    std::string outputDir;
    bool allZones = false;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (arg == "--zone" && i + 1 < argc) {
            zoneName = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--all") {
            allZones = true;
        } else if (arg == "--list") {
            listOnly = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (eqPath.empty()) {
        std::cerr << "Error: --eq-path is required\n";
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(eqPath)) {
        std::cerr << "Error: EQ path does not exist: " << eqPath << std::endl;
        return 1;
    }

    if (listOnly) {
        auto zones = listZones(eqPath);
        std::cout << "Available zones (" << zones.size() << "):" << std::endl;
        for (const auto& z : zones) {
            std::cout << "  " << z << std::endl;
        }
        return 0;
    }

    if (!allZones && zoneName.empty()) {
        std::cerr << "Error: --zone or --all is required\n";
        printUsage(argv[0]);
        return 1;
    }

    if (outputDir.empty()) {
        std::cerr << "Error: --output is required\n";
        printUsage(argv[0]);
        return 1;
    }

    // Create output directory
    fs::create_directories(outputDir);

    std::vector<std::string> zones;
    if (allZones) {
        zones = listZones(eqPath);
        std::cout << "Processing " << zones.size() << " zones..." << std::endl;
    } else {
        zones.push_back(toLower(zoneName));
    }

    int success = 0, failed = 0;
    for (const auto& zone : zones) {
        std::cout << "\n=== " << zone << " ===" << std::endl;

        // Build zone atlas
        std::string zoneArchive = zone + ".s3d";
        std::string zoneOutput = outputDir + "/" + zone + ".atlas";
        if (buildAtlas(eqPath, zoneArchive, zoneOutput)) {
            success++;
        } else {
            failed++;
        }

        // Build object atlas (if _obj archive exists)
        std::string objArchive = zone + "_obj.s3d";
        std::string objPath = eqPath + "/" + objArchive;
        if (fs::exists(objPath)) {
            std::string objOutput = outputDir + "/" + zone + "_obj.atlas";
            buildAtlas(eqPath, objArchive, objOutput);
        }
    }

    std::cout << "\nDone: " << success << " succeeded, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}
