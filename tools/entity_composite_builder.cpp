// Entity Composite Texture Builder
// Offline tool that builds composite texture sheets for character models.
// Each race/gender/variant produces a single texture sheet containing all body-part
// textures, pre-tiled ("unrolled") to eliminate GL_REPEAT dependency.
//
// This enables single-draw-call entity rendering on Mali 400, where draw calls
// are the bottleneck (~92us CPU each). Composite textures give +98% FPS vs unbatched.
//
// Phase 1: Offline composite generation + UV remapping metadata.
// Phase 2 (future): Runtime mesh builder consumes these outputs.
//
// Usage:
//   entity_composite_builder --eq-path <path> --output <dir> --race HUM
//   entity_composite_builder --eq-path <path> --output <dir> --all --verbose
//   entity_composite_builder --eq-path <path> --output <dir> --all --eqe1

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/dds_decoder.h"

#ifdef EQT_HAS_ETC2COMP
#include "client/graphics/eqe1_format.h"
#include <Etc.h>
#include <EtcImage.h>
#endif

#include <json/json.h>

using namespace EQT::Graphics;
namespace fs = std::filesystem;

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

// Round up to next power of 2
static int nextPow2(int v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

// ── BMP decoder (same as eqe1_builder) ──────────────────────────────────────

static bool decodeBMP(const std::vector<char>& data,
                      std::vector<uint8_t>& outRGBA,
                      int& outWidth, int& outHeight) {
    if (data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());

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
        uint32_t colorsUsed = *reinterpret_cast<const uint32_t*>(d + 46);
        if (colorsUsed == 0) colorsUsed = 256;
        uint32_t dibHeaderSize = *reinterpret_cast<const uint32_t*>(d + 14);
        const uint8_t* palette = d + 14 + dibHeaderSize;
        size_t paletteSize = colorsUsed * 4;
        if (14 + dibHeaderSize + paletteSize > data.size()) return false;
        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width + 3) & ~3;
        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = pixels[srcY * rowStride + x];
                if (idx >= colorsUsed) idx = 0;
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = palette[idx * 4 + 2];
                outRGBA[outIdx + 1] = palette[idx * 4 + 1];
                outRGBA[outIdx + 2] = palette[idx * 4 + 0];
                outRGBA[outIdx + 3] = 255;
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
                outRGBA[outIdx + 0] = pixels[srcIdx + 2];
                outRGBA[outIdx + 1] = pixels[srcIdx + 1];
                outRGBA[outIdx + 2] = pixels[srcIdx + 0];
                outRGBA[outIdx + 3] = 255;
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
                outRGBA[outIdx + 0] = pixels[srcIdx + 2];
                outRGBA[outIdx + 1] = pixels[srcIdx + 1];
                outRGBA[outIdx + 2] = pixels[srcIdx + 0];
                outRGBA[outIdx + 3] = pixels[srcIdx + 3];
            }
        }
        return true;
    }
    return false;
}

// ── Texture UV stats (per body-part texture) ────────────────────────────────

struct BodyPartTexture {
    std::string textureName;      // Original texture filename (e.g., "humch0001.bmp")
    float minU = 1e9f, maxU = -1e9f;
    float minV = 1e9f, maxV = -1e9f;
    int texWidth = 0, texHeight = 0;

    int tilesU() const { return std::max(1, static_cast<int>(std::ceil(maxU - minU))); }
    int tilesV() const { return std::max(1, static_cast<int>(std::ceil(maxV - minV))); }
    int unrolledW() const { return texWidth * tilesU(); }
    int unrolledH() const { return texHeight * tilesV(); }
};

// ── Placement rect in composite sheet ───────────────────────────────────────

struct PlacedTexture {
    std::string textureName;
    int x = 0, y = 0;            // Position in sheet
    int w = 0, h = 0;            // Unrolled size
    int tilesU = 1, tilesV = 1;  // Tile counts
    int origW = 0, origH = 0;    // Original texture size
};

// ── BMP writer ──────────────────────────────────────────────────────────────

static bool writeBMP(const std::string& path, const std::vector<uint8_t>& rgba,
                     int width, int height) {
    int rowStride = (width * 3 + 3) & ~3;
    int imageSize = rowStride * height;
    int fileSize = 54 + imageSize;

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    // BMP header
    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    *reinterpret_cast<uint32_t*>(header + 2) = fileSize;
    *reinterpret_cast<uint32_t*>(header + 10) = 54;
    *reinterpret_cast<uint32_t*>(header + 14) = 40;
    *reinterpret_cast<int32_t*>(header + 18) = width;
    *reinterpret_cast<int32_t*>(header + 22) = height;  // bottom-up
    *reinterpret_cast<uint16_t*>(header + 26) = 1;
    *reinterpret_cast<uint16_t*>(header + 28) = 24;
    *reinterpret_cast<uint32_t*>(header + 34) = imageSize;
    out.write(reinterpret_cast<char*>(header), 54);

    // Write pixel rows bottom-up (BMP convention)
    std::vector<uint8_t> row(rowStride, 0);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            int srcIdx = (y * width + x) * 4;
            row[x * 3 + 0] = rgba[srcIdx + 2]; // B
            row[x * 3 + 1] = rgba[srcIdx + 1]; // G
            row[x * 3 + 2] = rgba[srcIdx + 0]; // R
        }
        out.write(reinterpret_cast<char*>(row.data()), rowStride);
    }

    return true;
}

// ── Shelf packer ────────────────────────────────────────────────────────────
// Simple shelf-packing algorithm: sort by height descending, place left-to-right.

static bool shelfPack(std::vector<PlacedTexture>& textures, int maxSheetSize,
                       int& sheetW, int& sheetH) {
    if (textures.empty()) return false;

    // Sort by height descending (tallest first), then width descending
    std::sort(textures.begin(), textures.end(),
              [](const PlacedTexture& a, const PlacedTexture& b) {
                  if (a.h != b.h) return a.h > b.h;
                  return a.w > b.w;
              });

    // Place textures using shelf packing
    int curX = 0, curY = 0;
    int shelfHeight = 0;
    int maxW = 0;

    for (auto& tex : textures) {
        // Would this texture exceed the row width?
        if (curX + tex.w > maxSheetSize) {
            // Start new shelf
            curX = 0;
            curY += shelfHeight;
            shelfHeight = 0;
        }

        // Would this texture exceed the max height?
        if (curY + tex.h > maxSheetSize) {
            std::cerr << "ERROR: Cannot fit all textures in " << maxSheetSize
                      << "x" << maxSheetSize << " sheet" << std::endl;
            return false;
        }

        tex.x = curX;
        tex.y = curY;
        curX += tex.w;
        shelfHeight = std::max(shelfHeight, tex.h);
        maxW = std::max(maxW, curX);
    }

    // Final sheet size (power of 2)
    sheetW = nextPow2(maxW);
    sheetH = nextPow2(curY + shelfHeight);

    // Clamp to max
    if (sheetW > maxSheetSize) sheetW = maxSheetSize;
    if (sheetH > maxSheetSize) sheetH = maxSheetSize;

    return true;
}

// ── ETC1 encoding (optional) ────────────────────────────────────────────────

#ifdef EQT_HAS_ETC2COMP
static std::vector<uint8_t> encodeETC1_RGB(const uint8_t* rgba, int width, int height) {
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
                1, 1024,
                &encodingBits, &encodingBitsBytes,
                &extendedWidth, &extendedHeight,
                &encodingTimeMs);

    std::vector<uint8_t> result;
    if (encodingBits && encodingBitsBytes > 0) {
        result.assign(encodingBits, encodingBits + encodingBitsBytes);
        delete[] encodingBits;
    }
    return result;
}

static std::vector<uint8_t> downsample2x(const uint8_t* src, int srcW, int srcH,
                                          int& dstW, int& dstH) {
    dstW = std::max(4, srcW / 2);
    dstH = std::max(4, srcH / 2);
    std::vector<uint8_t> dst(dstW * dstH * 4);

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            int sx = std::min(x * 2, srcW - 1);
            int sy = std::min(y * 2, srcH - 1);
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

static std::vector<uint8_t> generateMipChain(const uint8_t* rgba, int width, int height,
                                              uint8_t& outMipLevels) {
    outMipLevels = computeMipLevels(static_cast<uint16_t>(width),
                                     static_cast<uint16_t>(height));
    std::vector<uint8_t> result;

    auto level0 = encodeETC1_RGB(rgba, width, height);
    result.insert(result.end(), level0.begin(), level0.end());

    std::vector<uint8_t> current(rgba, rgba + width * height * 4);
    int w = width, h = height;

    for (uint8_t level = 1; level < outMipLevels; ++level) {
        int dw, dh;
        auto downsampled = downsample2x(current.data(), w, h, dw, dh);
        w = dw;
        h = dh;
        auto encoded = encodeETC1_RGB(downsampled.data(), w, h);
        result.insert(result.end(), encoded.begin(), encoded.end());
        current = std::move(downsampled);
    }

    return result;
}

static bool writeEQE1(const std::string& outputPath,
                       const std::vector<uint8_t>& sheetRGBA,
                       int sheetW, int sheetH) {
    uint8_t mipLevels = 0;
    auto etc1Data = generateMipChain(sheetRGBA.data(), sheetW, sheetH, mipLevels);
    if (etc1Data.empty()) return false;

    // Single-texture EQE1 file
    uint16_t numEntries = 1;
    uint32_t indexEnd = sizeof(EQE1Header) + numEntries * EQE1_INDEX_ENTRY_SIZE;
    uint32_t dataOff = alignTo16(indexEnd);

    EQE1Header header = {};
    header.magic = EQE1_MAGIC;
    header.version = EQE1_VERSION;
    header.numEntries = numEntries;

    EQE1IndexEntry idx = {};
    // Name from output path filename
    std::string baseName = fs::path(outputPath).stem().string();
    if (baseName.size() >= EQE1_NAME_SIZE) baseName.resize(EQE1_NAME_SIZE - 1);
    std::strncpy(idx.name, baseName.c_str(), EQE1_NAME_SIZE - 1);
    idx.width = static_cast<uint16_t>(sheetW);
    idx.height = static_cast<uint16_t>(sheetH);
    idx.mipLevels = mipLevels;
    idx.hasAlpha = 0;
    idx.entryType = EQE1_TYPE_ETC1_RGB;
    idx.dataOffset = dataOff;
    idx.dataSize = static_cast<uint32_t>(etc1Data.size());

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&idx), sizeof(idx));

    // Alignment padding
    auto currentPos = static_cast<uint32_t>(out.tellp());
    if (currentPos < dataOff) {
        std::vector<uint8_t> padding(dataOff - currentPos, 0);
        out.write(reinterpret_cast<const char*>(padding.data()), padding.size());
    }

    out.write(reinterpret_cast<const char*>(etc1Data.data()), etc1Data.size());
    out.close();

    std::cout << "    EQE1: " << outputPath << " ("
              << fs::file_size(outputPath) / 1024 << " KB, "
              << sheetW << "x" << sheetH << ", "
              << (int)mipLevels << " mips)" << std::endl;
    return true;
}
#endif // EQT_HAS_ETC2COMP

// ── Model processing ────────────────────────────────────────────────────────

struct ModelResult {
    std::string raceName;
    int gender = 0;
    int variant = 0;
    int sheetW = 0, sheetH = 0;
    std::vector<PlacedTexture> placements;
    bool success = false;
};

// Get texture dimensions from BMP header in archive
static bool getTextureDimensions(PfsArchive& archive, const std::string& name,
                                  int& width, int& height) {
    std::vector<char> data;
    if (!archive.get(name, data) || data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    if (d[0] != 'B' || d[1] != 'M') return false;
    width = *reinterpret_cast<const int32_t*>(d + 18);
    height = std::abs(*reinterpret_cast<const int32_t*>(d + 22));
    return width > 0 && height > 0;
}

// Decode a texture from archive to RGBA pixels
static bool decodeTextureFromArchive(PfsArchive& archive, const std::string& name,
                                      std::vector<uint8_t>& outRGBA,
                                      int& outW, int& outH) {
    std::vector<char> data;
    if (!archive.get(name, data) || data.empty()) return false;

    if (DDSDecoder::isDDS(data)) {
        auto decoded = DDSDecoder::decode(data);
        if (!decoded.isValid()) return false;
        outW = decoded.width;
        outH = decoded.height;
        outRGBA = std::move(decoded.pixels);
        return true;
    }

    return decodeBMP(data, outRGBA, outW, outH);
}

// Apply variant substitution to a texture name.
// Pattern: {race}{part}00{page}.bmp → {race}{part}{variant:02d}{page}.bmp
// E.g., humch0001.bmp with variant=1 → humch0101.bmp
static std::string applyVariant(const std::string& texName, int variant) {
    if (variant == 0) return texName;

    // EQ texture naming: {race3}{part2}{variant2}{page2}.bmp
    // The variant digits are at positions 5-6 (0-indexed) in the stem
    std::string lower = toLower(texName);
    auto dot = lower.rfind('.');
    std::string stem = (dot != std::string::npos) ? lower.substr(0, dot) : lower;
    std::string ext = (dot != std::string::npos) ? lower.substr(dot) : "";

    // Need at least 7 chars: 3 race + 2 part + 2 variant (+ optional page digits)
    if (stem.size() < 7) return texName;

    // Replace variant digits (positions 5-6)
    char varStr[3];
    std::snprintf(varStr, sizeof(varStr), "%02d", variant);
    std::string result = stem.substr(0, 5) + varStr + stem.substr(7) + ext;
    return result;
}

static ModelResult processModel(const std::string& eqPath,
                                 const std::shared_ptr<CharacterModel>& model,
                                 const std::string& archivePath,
                                 PfsArchive& archive,
                                 int variant,
                                 int maxTiles,
                                 int maxSheetSize,
                                 const std::string& outputDir,
                                 bool writeEqe1,
                                 bool verbose) {
    ModelResult result;
    result.raceName = toUpper(model->name);
    result.gender = 0;  // TODO: parse from model name if needed
    result.variant = variant;

    // Collect UV extents per texture across all parts
    std::map<std::string, BodyPartTexture> bodyParts;

    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;

        const auto& texNames = part->textureNames();

        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;

            std::string texName = toLower(texNames[tri.textureIndex]);
            auto& bp = bodyParts[texName];
            if (bp.textureName.empty()) {
                bp.textureName = texName;
            }

            for (uint32_t vi : {tri.v1, tri.v2, tri.v3}) {
                if (vi >= part->vertices.size()) continue;
                const auto& v = part->vertices[vi];
                bp.minU = std::min(bp.minU, v.u);
                bp.maxU = std::max(bp.maxU, v.u);
                bp.minV = std::min(bp.minV, v.v);
                bp.maxV = std::max(bp.maxV, v.v);
            }
        }
    }

    if (bodyParts.empty()) {
        if (verbose) std::cerr << "  No body-part textures found for " << model->name << std::endl;
        return result;
    }

    // Get texture dimensions and apply variant substitution
    std::map<std::string, std::pair<int,int>> texDimCache;
    auto getTexDims = [&](const std::string& name) -> std::pair<int,int> {
        auto it = texDimCache.find(name);
        if (it != texDimCache.end()) return it->second;
        int w = 0, h = 0;
        getTextureDimensions(archive, name, w, h);
        texDimCache[name] = {w, h};
        return {w, h};
    };

    // Resolve textures with dimensions and variant substitution
    std::vector<BodyPartTexture> resolved;
    int skippedTiles = 0;

    for (auto& [key, bp] : bodyParts) {
        // Apply variant substitution
        std::string variantName = applyVariant(bp.textureName, variant);
        // Check if variant texture exists
        std::vector<char> testData;
        if (variant > 0 && archive.get(variantName, testData)) {
            bp.textureName = variantName;
        }

        auto dims = getTexDims(bp.textureName);
        bp.texWidth = dims.first;
        bp.texHeight = dims.second;

        if (bp.texWidth <= 0 || bp.texHeight <= 0) {
            if (verbose) std::cerr << "    Skip (no dims): " << bp.textureName << std::endl;
            continue;
        }

        // Check tile count against max
        if (bp.tilesU() > maxTiles || bp.tilesV() > maxTiles) {
            if (verbose) {
                std::cerr << "    Skip (tiles " << bp.tilesU() << "x" << bp.tilesV()
                          << " > " << maxTiles << "): " << bp.textureName << std::endl;
            }
            skippedTiles++;
            continue;
        }

        resolved.push_back(bp);
    }

    if (resolved.empty()) {
        if (verbose) std::cerr << "  No valid textures for " << model->name << std::endl;
        return result;
    }

    if (skippedTiles > 0 && verbose) {
        std::cerr << "  " << skippedTiles << " textures skipped (exceeded max tiles)" << std::endl;
    }

    // Build placement list with unrolled dimensions
    std::vector<PlacedTexture> placements;
    for (const auto& bp : resolved) {
        PlacedTexture pt;
        pt.textureName = bp.textureName;
        pt.w = bp.unrolledW();
        pt.h = bp.unrolledH();
        pt.tilesU = bp.tilesU();
        pt.tilesV = bp.tilesV();
        pt.origW = bp.texWidth;
        pt.origH = bp.texHeight;
        placements.push_back(pt);
    }

    // Pack into composite sheet
    int sheetW = 0, sheetH = 0;
    if (!shelfPack(placements, maxSheetSize, sheetW, sheetH)) {
        std::cerr << "  FAILED to pack " << model->name << " (variant " << variant << ")" << std::endl;
        return result;
    }

    result.sheetW = sheetW;
    result.sheetH = sheetH;
    result.placements = placements;

    if (verbose) {
        std::cout << "  " << model->name << " variant=" << variant
                  << " textures=" << placements.size()
                  << " sheet=" << sheetW << "x" << sheetH << std::endl;
        for (const auto& pt : placements) {
            std::cout << "    " << pt.textureName
                      << " @ (" << pt.x << "," << pt.y << ") "
                      << pt.w << "x" << pt.h
                      << " tiles=" << pt.tilesU << "x" << pt.tilesV << std::endl;
        }
    }

    // Render composite sheet
    std::vector<uint8_t> sheetRGBA(sheetW * sheetH * 4, 0);

    for (const auto& pt : placements) {
        // Load and decode source texture
        std::vector<uint8_t> srcRGBA;
        int srcW = 0, srcH = 0;
        if (!decodeTextureFromArchive(archive, pt.textureName, srcRGBA, srcW, srcH)) {
            std::cerr << "    WARNING: Cannot decode " << pt.textureName << std::endl;
            continue;
        }

        // Tile (unroll) the source texture into the sheet
        for (int ty = 0; ty < pt.tilesV; ++ty) {
            for (int tx = 0; tx < pt.tilesU; ++tx) {
                for (int py = 0; py < srcH; ++py) {
                    for (int px = 0; px < srcW; ++px) {
                        int dstX = pt.x + tx * srcW + px;
                        int dstY = pt.y + ty * srcH + py;
                        if (dstX >= sheetW || dstY >= sheetH) continue;

                        int srcIdx = (py * srcW + px) * 4;
                        int dstIdx = (dstY * sheetW + dstX) * 4;
                        sheetRGBA[dstIdx + 0] = srcRGBA[srcIdx + 0];
                        sheetRGBA[dstIdx + 1] = srcRGBA[srcIdx + 1];
                        sheetRGBA[dstIdx + 2] = srcRGBA[srcIdx + 2];
                        sheetRGBA[dstIdx + 3] = srcRGBA[srcIdx + 3];
                    }
                }
            }
        }
    }

    // Determine output basename
    std::string baseName = toLower(model->name) + "_" + std::to_string(result.gender)
                         + "_" + std::to_string(variant);

    // Write BMP
    std::string bmpPath = outputDir + "/" + baseName + ".bmp";
    if (!writeBMP(bmpPath, sheetRGBA, sheetW, sheetH)) {
        std::cerr << "  FAILED to write: " << bmpPath << std::endl;
        return result;
    }
    std::cout << "    BMP: " << bmpPath << " (" << sheetW << "x" << sheetH << ")" << std::endl;

    // Write UV mapping JSON
    Json::Value root;
    root["race"] = toUpper(model->name);
    root["gender"] = result.gender;
    root["variant"] = variant;
    root["sheetWidth"] = sheetW;
    root["sheetHeight"] = sheetH;

    Json::Value textures(Json::objectValue);
    for (const auto& pt : placements) {
        Json::Value entry;
        entry["x"] = pt.x;
        entry["y"] = pt.y;
        entry["w"] = pt.w;
        entry["h"] = pt.h;
        entry["tilesU"] = pt.tilesU;
        entry["tilesV"] = pt.tilesV;
        entry["uvScaleU"] = static_cast<double>(pt.w) / sheetW;
        entry["uvScaleV"] = static_cast<double>(pt.h) / sheetH;
        entry["uvOffsetU"] = static_cast<double>(pt.x) / sheetW;
        entry["uvOffsetV"] = static_cast<double>(pt.y) / sheetH;
        textures[pt.textureName] = entry;
    }
    root["textures"] = textures;

    std::string jsonPath = outputDir + "/" + baseName + ".json";
    std::ofstream jsonOut(jsonPath);
    if (jsonOut.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &jsonOut);
        jsonOut << std::endl;
        std::cout << "    JSON: " << jsonPath << std::endl;
    }

    // Optional EQE1 output
#ifdef EQT_HAS_ETC2COMP
    if (writeEqe1) {
        std::string eqe1Path = outputDir + "/" + baseName + ".eqe1";
        writeEQE1(eqe1Path, sheetRGBA, sheetW, sheetH);
    }
#else
    if (writeEqe1) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "WARNING: --eqe1 requested but ETC2Comp not available. Skipping." << std::endl;
            warned = true;
        }
    }
#endif

    result.success = true;
    return result;
}

// ── Archive scanning ────────────────────────────────────────────────────────

// Global chr archive names
static const char* GLOBAL_CHR_ARCHIVES[] = {
    "global_chr.s3d",
    "global2_chr.s3d",
    "global3_chr.s3d",
    "global4_chr.s3d",
    "global5_chr.s3d",
    "global6_chr.s3d",
    "global7_chr.s3d",
    nullptr
};

struct ArchiveModels {
    std::string archiveName;
    std::string archivePath;
    std::vector<std::shared_ptr<CharacterModel>> models;
};

static std::vector<ArchiveModels> loadGlobalArchives(const std::string& eqPath,
                                                      const std::string& filterRace,
                                                      bool verbose) {
    std::vector<ArchiveModels> results;

    for (int i = 0; GLOBAL_CHR_ARCHIVES[i]; ++i) {
        std::string archiveName = GLOBAL_CHR_ARCHIVES[i];
        std::string archivePath = eqPath + "/" + archiveName;

        if (!fs::exists(archivePath)) continue;

        if (verbose) std::cerr << "Loading " << archiveName << "..." << std::endl;

        S3DLoader loader;
        if (!loader.loadZone(archivePath)) {
            if (verbose) std::cerr << "  Could not load " << archiveName << std::endl;
            continue;
        }

        auto zone = loader.getZone();
        if (!zone || zone->characters.empty()) continue;

        ArchiveModels am;
        am.archiveName = archiveName;
        am.archivePath = archivePath;

        for (const auto& character : zone->characters) {
            if (!character) continue;

            // Filter by race if specified
            if (!filterRace.empty()) {
                std::string upperName = toUpper(character->name);
                std::string upperFilter = toUpper(filterRace);
                // Model names are like "HUM", "ELF", "DWF", etc.
                // Match if the model name starts with the filter
                if (upperName.find(upperFilter) == std::string::npos) continue;
            }

            // Only include models that have actual geometry parts
            bool hasParts = false;
            for (const auto& part : character->parts) {
                if (part && !part->vertices.empty()) { hasParts = true; break; }
            }
            if (!hasParts) continue;

            am.models.push_back(character);
        }

        if (!am.models.empty()) {
            if (verbose) {
                std::cerr << "  Found " << am.models.size() << " models" << std::endl;
            }
            results.push_back(std::move(am));
        }
    }

    return results;
}

// ── Usage / main ────────────────────────────────────────────────────────────

static void printUsage(const char* prog) {
    std::cout << "Entity Composite Texture Builder\n\n"
              << "Usage: " << prog << " --eq-path <path> --output <dir> [options]\n\n"
              << "Options:\n"
              << "  --eq-path <path>     Path to EverQuest client directory\n"
              << "  --output <dir>       Output directory for composite textures\n"
              << "  --race <NAME>        Process single race (e.g., HUM, ELF, DWF)\n"
              << "  --all                Process all races found in global chr archives\n"
              << "  --variant <N>        Specific variant (default: 0)\n"
              << "  --all-variants       Process variants 0-4\n"
              << "  --max-tiles <N>      Cap tile unrolling (default: 8)\n"
              << "  --sheet-size <N>     Max composite sheet dimension (default: 1024)\n"
              << "  --eqe1               Also output ETC1-compressed .eqe1 files\n"
              << "  --verbose            Per-texture stats\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string outputDir;
    std::string filterRace;
    bool allRaces = false;
    int variant = 0;
    bool allVariants = false;
    int maxTiles = 8;
    int maxSheetSize = 1024;
    bool writeEqe1 = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) eqPath = argv[++i];
        else if (arg == "--output" && i + 1 < argc) outputDir = argv[++i];
        else if (arg == "--race" && i + 1 < argc) filterRace = argv[++i];
        else if (arg == "--all") allRaces = true;
        else if (arg == "--variant" && i + 1 < argc) variant = std::atoi(argv[++i]);
        else if (arg == "--all-variants") allVariants = true;
        else if (arg == "--max-tiles" && i + 1 < argc) maxTiles = std::atoi(argv[++i]);
        else if (arg == "--sheet-size" && i + 1 < argc) maxSheetSize = std::atoi(argv[++i]);
        else if (arg == "--eqe1") writeEqe1 = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
    }

    if (eqPath.empty() || outputDir.empty() || (!allRaces && filterRace.empty())) {
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(eqPath) || !fs::is_directory(eqPath)) {
        std::cerr << "Error: EQ path does not exist: " << eqPath << std::endl;
        return 1;
    }

    fs::create_directories(outputDir);

    // Determine variants to process
    std::vector<int> variants;
    if (allVariants) {
        for (int v = 0; v <= 4; ++v) variants.push_back(v);
    } else {
        variants.push_back(variant);
    }

    // Load character models from global chr archives
    auto archives = loadGlobalArchives(eqPath, filterRace, verbose);
    if (archives.empty()) {
        std::cerr << "No character models found" << std::endl;
        return 1;
    }

    int totalModels = 0;
    for (const auto& am : archives) totalModels += am.models.size();
    std::cout << "Found " << totalModels << " character models in "
              << archives.size() << " archives" << std::endl;

    // Process each model
    int successCount = 0, failCount = 0;
    std::vector<ModelResult> allResults;

    for (const auto& am : archives) {
        // Open raw archive for texture data access
        PfsArchive archive;
        if (!archive.open(am.archivePath)) {
            std::cerr << "Cannot open archive: " << am.archivePath << std::endl;
            continue;
        }

        for (const auto& model : am.models) {
            for (int v : variants) {
                std::cout << "\n[" << model->name << " variant=" << v << "] "
                          << am.archiveName << std::endl;

                auto result = processModel(eqPath, model, am.archivePath, archive,
                                            v, maxTiles, maxSheetSize, outputDir,
                                            writeEqe1, verbose);

                if (result.success) {
                    successCount++;
                    allResults.push_back(result);
                } else {
                    failCount++;
                }
            }
        }
    }

    // Write composite index JSON
    if (!allResults.empty()) {
        Json::Value index(Json::objectValue);
        for (const auto& r : allResults) {
            std::string key = toLower(r.raceName) + "_" + std::to_string(r.gender)
                            + "_" + std::to_string(r.variant);
            Json::Value entry;
            entry["race"] = r.raceName;
            entry["gender"] = r.gender;
            entry["variant"] = r.variant;
            entry["sheetWidth"] = r.sheetW;
            entry["sheetHeight"] = r.sheetH;
            entry["textureCount"] = static_cast<int>(r.placements.size());
            entry["bmpFile"] = key + ".bmp";
            entry["jsonFile"] = key + ".json";
            index[key] = entry;
        }

        std::string indexPath = outputDir + "/composite_index.json";
        std::ofstream indexOut(indexPath);
        if (indexOut.is_open()) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(index, &indexOut);
            indexOut << std::endl;
        }
        std::cout << "\nIndex: " << indexPath << std::endl;
    }

    // Summary
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "  Models processed: " << successCount << " succeeded, "
              << failCount << " failed" << std::endl;

    if (!allResults.empty()) {
        size_t totalSheetPixels = 0;
        for (const auto& r : allResults) {
            totalSheetPixels += r.sheetW * r.sheetH;
        }
        std::cout << "  Total sheet area: " << totalSheetPixels / 1024 << " Kpixels" << std::endl;
        std::cout << "  ETC1 estimate: " << totalSheetPixels / 2 / 1024 << " KB" << std::endl;
    }

    return failCount > 0 ? 1 : 0;
}
