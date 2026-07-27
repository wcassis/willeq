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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
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
    std::string textureName;      // Texture filename (may be variant-substituted)
    std::string originalName;     // Original name from model geometry (set during resolve)
    float minU = 1e9f, maxU = -1e9f;
    float minV = 1e9f, maxV = -1e9f;
    int texWidth = 0, texHeight = 0;
    bool isAnimated = false;      // P08f: true if any triangle referencing this texture
                                   // uses an animated (multi-frame) material (0x30/0x31 fragment)

    int tilesU() const { return std::max(1, static_cast<int>(std::ceil(maxU - minU))); }
    int tilesV() const { return std::max(1, static_cast<int>(std::ceil(maxV - minV))); }
    int unrolledW() const { return texWidth * tilesU(); }
    int unrolledH() const { return texHeight * tilesV(); }
};

// ── Placement rect in composite sheet ───────────────────────────────────────

struct PlacedTexture {
    std::string textureName;      // Actual texture file used (may be variant-substituted)
    std::string originalName;     // Original name from model geometry (for UV lookup key)
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
// UNCHANGED from pre-P16 — tight packing, no reserved gutter footprint. (An
// earlier version of this fix reserved 2*pad extra width/height per placement
// for the seam gutter; measurement against the live set showed that tax
// routinely tipped an already power-of-2-tight sheet over the next pow2
// threshold, INFLATING sheets by up to 4x — worse than the unroll-inflation
// problem this unit exists to fix. Replaced with the inset approach below,
// which needs zero extra footprint.)

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

// P16 seam fix, take 2: a UV INSET rather than a reserved gutter. Packing
// stays byte-identical to pre-P16 (tight, no footprint growth — see the
// shelfPack comment above for why a reserved gutter was rejected). Instead,
// within a placement's EXISTING core rect, the outer `pad` texels on every
// side are overwritten in place with edge-duplicated content from the
// interior, and the caller shrinks the UV mapping (uvOffset/uvScale, computed
// in processModel) to sample only the untouched interior — so geometry never
// actually reads the overwritten border under normal sampling, and a bilinear
// read that strays a fraction of a texel past the inset boundary blends
// against a same-texture, edge-consistent color instead of a neighboring
// placement's unrelated pixels. This costs a `pad`-texel-wide sliver of real
// resolution at each tile's edge (a standard, well-understood tradeoff for
// atlas seam correctness — a UV inset, explicitly anticipated by the P16
// plan for exactly this situation) instead of any sheet growth.
//
// Wrap-style (opposite-edge, not nearest-edge) duplication: for a texture
// that repeats (tilesU/tilesV > 1, physically unrolled into the sheet), the
// content immediately "before" the sampled region's left edge, under true
// GL_REPEAT wrap, IS the sampled region's right edge — so this is exactly
// correct there, not an approximation; for a non-repeating placement it is
// harmless (same-texture content is a strictly better neighbor than whatever
// unrelated texture is packed next in the atlas).
static void writeSeamInset(std::vector<uint8_t>& sheetRGBA, int sheetW, int /*sheetH*/,
                            const PlacedTexture& pt, int pad) {
    if (pad <= 0) return;
    int x0 = pt.x, y0 = pt.y, w = pt.w, h = pt.h;

    // Snapshot the placement's current pixels so every border write below
    // reads from the ORIGINAL (pre-overwrite) content, regardless of write
    // order or how small w/h are relative to pad — avoids self-contamination
    // when interior and border regions are close together.
    std::vector<uint8_t> snapshot(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        std::memcpy(&snapshot[static_cast<size_t>(y) * w * 4],
                    &sheetRGBA[(static_cast<size_t>(y0 + y) * sheetW + x0) * 4],
                    static_cast<size_t>(w) * 4);
    }
    auto snapPix = [&](int lx, int ly) -> const uint8_t* {
        lx = std::max(0, std::min(w - 1, lx));
        ly = std::max(0, std::min(h - 1, ly));
        return &snapshot[(static_cast<size_t>(ly) * w + lx) * 4];
    };
    auto setPix = [&](int lx, int ly, const uint8_t* src) {
        uint8_t* dst = &sheetRGBA[(static_cast<size_t>(y0 + ly) * sheetW + (x0 + lx)) * 4];
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
    };

    for (int k = 0; k < pad; ++k) {
        for (int ly = 0; ly < h; ++ly) {
            setPix(k, ly, snapPix(w - 2 * pad + k, ly));         // left border <- interior right edge
            setPix(w - 1 - k, ly, snapPix(2 * pad - 1 - k, ly)); // right border <- interior left edge
        }
    }
    for (int k = 0; k < pad; ++k) {
        for (int lx = 0; lx < w; ++lx) {
            setPix(lx, k, snapPix(lx, h - 2 * pad + k));         // top border <- interior bottom edge
            setPix(lx, h - 1 - k, snapPix(lx, 2 * pad - 1 - k)); // bottom border <- interior top edge
        }
    }
    for (int ky = 0; ky < pad; ++ky) {
        for (int kx = 0; kx < pad; ++kx) {
            setPix(kx, ky, snapPix(w - 2 * pad + kx, h - 2 * pad + ky));
            setPix(w - 1 - kx, ky, snapPix(2 * pad - 1 - kx, h - 2 * pad + ky));
            setPix(kx, h - 1 - ky, snapPix(w - 2 * pad + kx, 2 * pad - 1 - ky));
            setPix(w - 1 - kx, h - 1 - ky, snapPix(2 * pad - 1 - kx, 2 * pad - 1 - ky));
        }
    }
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
    // P08f: animated-texture exclusion (plan A3, the Z35-class builder gap).
    // Set when checkAnimatedTextures was requested and a used body-part texture
    // is a multi-frame animated material — the model is skipped entirely rather
    // than silently baking frame 0.
    bool skippedAnimated = false;
    std::string animatedTextureName;
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
                                 bool verbose,
                                 int physicalTileCap = 1,
                                 int seamPad = 2,
                                 bool checkAnimatedTextures = false) {
    ModelResult result;
    result.raceName = toUpper(model->name);
    result.gender = 0;  // TODO: parse from model name if needed
    result.variant = variant;

    // Collect UV extents per texture across all parts
    std::map<std::string, BodyPartTexture> bodyParts;

    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;

        const auto& texNames = part->textureNames();
        const auto& texAnims = part->textureAnimations();  // same indexing as texNames()

        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;

            std::string texName = toLower(texNames[tri.textureIndex]);
            auto& bp = bodyParts[texName];
            if (bp.textureName.empty()) {
                bp.textureName = texName;
            }
            if (checkAnimatedTextures && tri.textureIndex < texAnims.size() &&
                texAnims[tri.textureIndex].isAnimated) {
                bp.isAnimated = true;
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

    // P08f plan A3: animated-texture exclusion (the Z35-class builder gap). Baking a
    // single frame of a multi-frame material would silently freeze it — skip the whole
    // model instead and let the caller report it as ineligible-by-animation.
    if (checkAnimatedTextures) {
        for (const auto& [key, bp] : bodyParts) {
            if (bp.isAnimated) {
                result.skippedAnimated = true;
                result.animatedTextureName = key;
                if (verbose) {
                    std::cerr << "  SKIP (animated texture " << key << "): " << model->name << std::endl;
                }
                return result;
            }
        }
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

    int skippedHelm = 0;
    for (auto& [key, bp] : bodyParts) {
        // Filter out helmet model textures — these are rendered as separate
        // equipment draw calls, not applied to the base/robe body mesh.
        {
            const std::string& name = bp.textureName;
            if (name.substr(0, 4) == "helm" ||
                name.substr(0, 11) == "helmleather" ||
                name.substr(0, 7) == "magecap") {
                if (verbose) std::cerr << "    Skip (helmet model): " << name << std::endl;
                skippedHelm++;
                continue;
            }
        }

        // Save original name before variant substitution (for JSON lookup key)
        std::string originalName = bp.textureName;
        // Apply variant substitution
        std::string variantName = applyVariant(bp.textureName, variant);
        // Check if variant texture exists
        std::vector<char> testData;
        if (variant > 0 && archive.get(variantName, testData)) {
            bp.textureName = variantName;
        }
        bp.originalName = originalName;

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

    if ((skippedTiles > 0 || skippedHelm > 0) && verbose) {
        if (skippedTiles > 0)
            std::cerr << "  " << skippedTiles << " textures skipped (exceeded max tiles)" << std::endl;
        if (skippedHelm > 0)
            std::cerr << "  " << skippedHelm << " textures skipped (helmet models)" << std::endl;
    }

    // Build placement list with unrolled dimensions.
    //
    // P16 unroll discipline: tilesU()/tilesV() (the LOGICAL tile count) reflect
    // the actual max UV extent the geometry uses — that part was already
    // correct. The inflation P13 measured (3.25x avg, up to 6.06x) comes from
    // physically duplicating that many pixel-identical copies of the source
    // texture into the sheet. Since the source textures are GL_REPEAT-tileable
    // by construction (that's why the geometry's UVs exceed [0,1] at all), a
    // capped PHYSICAL copy count reproduces pixel-identical output to the full
    // unroll as long as sampling wraps modulo the physical count instead of
    // dividing by the logical one — see the matching runtime change in
    // mesh_building.cpp (fractU = fmod(shiftedU, tilesU) / tilesU). tilesU in
    // the JSON therefore now means "physical copies stored / wrap divisor",
    // capped at physicalTileCap; the shift math (which only depends on
    // floor(minU), independent of this field) is unaffected.
    std::vector<PlacedTexture> placements;
    for (const auto& bp : resolved) {
        PlacedTexture pt;
        pt.textureName = bp.textureName;
        pt.originalName = bp.originalName.empty() ? bp.textureName : bp.originalName;
        pt.tilesU = std::min(bp.tilesU(), std::max(1, physicalTileCap));
        pt.tilesV = std::min(bp.tilesV(), std::max(1, physicalTileCap));
        pt.w = bp.texWidth * pt.tilesU;
        pt.h = bp.texHeight * pt.tilesV;
        pt.origW = bp.texWidth;
        pt.origH = bp.texHeight;
        placements.push_back(pt);
    }

    // Pack into composite sheet (each placement reserves a seamPad-texel gutter)
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

        // P16 seam fix: overwrite the outer seamPad texels of this placement's
        // core rect in place with edge-duplicated interior content, so
        // bilinear sampling near a tile boundary never blends against a
        // neighboring placement's pixels. Clamp pad so the untouched interior
        // is always at least half of each dimension (degenerate for tiny
        // textures otherwise) — effPad is also what the JSON UV inset below
        // must agree with.
        int effPad = std::min({seamPad, pt.w / 4, pt.h / 4});
        if (effPad > 0) writeSeamInset(sheetRGBA, sheetW, sheetH, pt, effPad);
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
        // P16: uvScale/uvOffset key off the INSET interior rect (see
        // writeSeamInset above), not the full placement — this is what keeps
        // sampled UVs off the edge-duplicated seam border. Must use the same
        // effPad formula as the pixel-writing loop.
        int effPad = std::min({seamPad, pt.w / 4, pt.h / 4});
        int ix = pt.x + effPad, iy = pt.y + effPad;
        int iw = pt.w - 2 * effPad, ih = pt.h - 2 * effPad;

        Json::Value entry;
        entry["x"] = pt.x;
        entry["y"] = pt.y;
        entry["w"] = pt.w;
        entry["h"] = pt.h;
        entry["tilesU"] = pt.tilesU;
        entry["tilesV"] = pt.tilesV;
        entry["seamPad"] = effPad;
        entry["uvScaleU"] = static_cast<double>(iw) / sheetW;
        entry["uvScaleV"] = static_cast<double>(ih) / sheetH;
        entry["uvOffsetU"] = static_cast<double>(ix) / sheetW;
        entry["uvOffsetV"] = static_cast<double>(iy) / sheetH;
        // Key by original (geometry) name so the mesh builder can look up textures
        // using the names from the model data, regardless of variant substitution.
        std::string jsonKey = pt.originalName.empty() ? pt.textureName : pt.originalName;
        textures[jsonKey] = entry;
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

// ── P08f: zone-archive support ──────────────────────────────────────────────
//
// entity_composite_builder originally saw only the 7 hardcoded global*_chr.s3d
// archives (GLOBAL_CHR_ARCHIVES above), so zone-native NPC models (the
// CAZ/INN/BEA-shaped majority of a typical dungeon/city roster) were
// structurally unbuildable — not a missing sweep, the tool literally never
// opened the file. This mirrors RaceModelLoader's own zone-archive access
// (RaceModelLoader::loadZoneModels, model_loading.cpp:140 — a SINGLE
// `{zone}_chr.s3d` file, no numbered/sibling archives for zone-specific
// content; confirmed by reading the runtime loader rather than assumed) —
// zone models live in exactly one file per zone.
//
// Sheet naming/keying is intentionally UNCHANGED from the global path
// (`{racename}_0_{variant}`, no zone qualifier) because that is what the
// runtime looks up: RaceModelLoader::preloadModelData
// (race_model_loader.cpp:807-810) builds `compositeBaseName = modelName +
// "_0_" + textureVariant` from the model's race name alone and never
// consults the zone name — confirmed by reading the runtime code, not
// assumed. This is exactly why name collisions between a zone model and a
// global (or another zone's) model of the same race name matter: writing
// both would mean one silently clobbers the other's sheet on disk.

// Loads character models from a SINGLE archive file (global or zone). Returns
// std::nullopt if the archive doesn't exist or has no usable characters —
// callers treat that as "not present for this zone", not an error.
static std::optional<ArchiveModels> tryLoadArchiveModels(const std::string& archivePath,
                                                           const std::string& archiveName,
                                                           const std::string& filterRace,
                                                           bool verbose) {
    if (!fs::exists(archivePath)) return std::nullopt;

    if (verbose) std::cerr << "Loading " << archiveName << "..." << std::endl;

    S3DLoader loader;
    if (!loader.loadZone(archivePath)) {
        if (verbose) std::cerr << "  Could not load " << archiveName << std::endl;
        return std::nullopt;
    }

    auto zone = loader.getZone();
    if (!zone || zone->characters.empty()) return std::nullopt;

    ArchiveModels am;
    am.archiveName = archiveName;
    am.archivePath = archivePath;

    for (const auto& character : zone->characters) {
        if (!character) continue;

        if (!filterRace.empty()) {
            std::string upperName = toUpper(character->name);
            std::string upperFilter = toUpper(filterRace);
            if (upperName.find(upperFilter) == std::string::npos) continue;
        }

        bool hasParts = false;
        for (const auto& part : character->parts) {
            if (part && !part->vertices.empty()) { hasParts = true; break; }
        }
        if (!hasParts) continue;

        am.models.push_back(character);
    }

    if (am.models.empty()) return std::nullopt;

    if (verbose) std::cerr << "  Found " << am.models.size() << " models" << std::endl;
    return am;
}

// Opens the single zone-specific character archive, mirroring
// RaceModelLoader::loadZoneModels exactly (one file, `{zone}_chr.s3d`, no
// numbered siblings — zone chr archives are never split like the globals are).
static std::optional<ArchiveModels> loadZoneArchive(const std::string& eqPath,
                                                     const std::string& zoneShortName,
                                                     bool verbose) {
    std::string archiveName = zoneShortName + "_chr.s3d";
    std::string archivePath = eqPath + "/" + archiveName;
    return tryLoadArchiveModels(archivePath, archiveName, "", verbose);
}

// Builds a per-model "content signature" — the sorted set of body-part
// texture names a model's triangles actually reference (pre-variant,
// pre-helm-filter — deliberately the rawest available discriminator so two
// models are only judged identical if their geometry/texture usage truly
// matches). Used for A2's collision detection: two archives producing a
// model with the SAME race name are the same model only if this matches.
static std::vector<std::string> collectSignature(const std::shared_ptr<CharacterModel>& model) {
    std::set<std::string> names;
    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;
        const auto& texNames = part->textureNames();
        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;
            names.insert(toLower(texNames[tri.textureIndex]));
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

struct RegisteredModel {
    std::string sourceArchive;
    std::vector<std::string> signature;
};

// Registers every model from `archives` into `registry` keyed by uppercase
// race name — used to seed the collision registry with the global set before
// any zone archive is processed (plan A2).
static void registerArchives(const std::vector<ArchiveModels>& archives,
                              std::map<std::string, RegisteredModel>& registry) {
    for (const auto& am : archives) {
        for (const auto& model : am.models) {
            std::string key = toUpper(model->name);
            if (registry.count(key)) continue;  // first writer wins within this pass
            registry[key] = RegisteredModel{am.archiveName, collectSignature(model)};
        }
    }
}

// P08f plan A4: discover the texture-variant set that actually exists for a
// model in its archive, instead of a fixed 0-4 sweep (which — for zone
// models not curated the way the 42 global defs were — would silently
// rebuild N duplicate copies of variant 0 whenever a higher variant number
// isn't actually present; applyVariant() falls back to the base texture
// when the variant file is missing, so a naive sweep can't tell the
// difference between "distinct variant" and "no such variant").
// Variant v is considered to exist if ANY of the model's resolved body-part
// textures has a distinct v-suffixed file in the archive.
static std::vector<int> discoverVariants(PfsArchive& archive,
                                          const std::shared_ptr<CharacterModel>& model,
                                          int maxVariant) {
    std::set<std::string> baseTexNames;
    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;
        const auto& texNames = part->textureNames();
        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;
            baseTexNames.insert(toLower(texNames[tri.textureIndex]));
        }
    }

    std::vector<int> variants = {0};
    for (int v = 1; v <= maxVariant; ++v) {
        bool found = false;
        for (const auto& name : baseTexNames) {
            std::string variantName = applyVariant(name, v);
            if (variantName == name) continue;  // applyVariant no-op (name too short etc.)
            std::vector<char> testData;
            if (archive.get(variantName, testData)) { found = true; break; }
        }
        if (found) variants.push_back(v);
    }
    return variants;
}

// Whole-model animated-texture probe, checked once per model (independent of
// variant — the animation flag lives on the base material, not the variant
// substitution) so the zone-mode driver can skip a model's entire variant
// set in one shot instead of re-discovering it per variant.
static bool detectAnimatedTexture(const std::shared_ptr<CharacterModel>& model,
                                   std::string& outTextureName) {
    for (const auto& part : model->parts) {
        if (!part || part->vertices.empty() || part->triangles.empty()) continue;
        const auto& texNames = part->textureNames();
        const auto& texAnims = part->textureAnimations();
        for (const auto& tri : part->triangles) {
            if (tri.textureIndex >= texNames.size()) continue;
            if (tri.textureIndex < texAnims.size() && texAnims[tri.textureIndex].isAnimated) {
                outTextureName = toLower(texNames[tri.textureIndex]);
                return true;
            }
        }
    }
    return false;
}

// Merges newly-built index entries into any pre-existing composite_index.json
// in outputDir, WITHOUT touching existing keys (the 210 global entries this
// unit must leave byte-identical) — zone-mode additions are always new keys
// by construction (the collision registry guarantees no key this function
// writes was already registered under a matching or colliding signature).
static void mergeCompositeIndex(const std::string& outputDir,
                                 const std::vector<ModelResult>& newResults) {
    std::string indexPath = outputDir + "/composite_index.json";
    Json::Value index(Json::objectValue);

    std::ifstream existing(indexPath);
    if (existing.is_open()) {
        Json::CharReaderBuilder rb;
        std::string errs;
        if (!Json::parseFromStream(rb, existing, &index, &errs)) {
            std::cerr << "WARNING: could not parse existing " << indexPath
                      << " (" << errs << ") — starting a fresh index" << std::endl;
            index = Json::Value(Json::objectValue);
        }
    }

    for (const auto& r : newResults) {
        std::string key = toLower(r.raceName) + "_" + std::to_string(r.gender)
                        + "_" + std::to_string(r.variant);
        if (index.isMember(key)) continue;  // never overwrite (collision registry should prevent this)
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

    std::ofstream indexOut(indexPath);
    if (indexOut.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(index, &indexOut);
        indexOut << std::endl;
    }
}

// Persists the zone-sourced portion of the collision registry to a sidecar
// file in outputDir, and loads it back on a later run. This closes the gap
// a purely in-memory, single-process registry would otherwise leave: two
// SEPARATE tool invocations (e.g. golden zones today, --all-zones next
// week) must still detect a collision between a model built in an earlier
// run and one found in a later run — A2 requires this regardless of how
// many processes it takes to build the full roster.
static const char* SIGNATURE_REGISTRY_FILENAME = "p08f_zone_signature_registry.json";

static void loadPersistedRegistry(const std::string& outputDir,
                                   std::map<std::string, RegisteredModel>& registry) {
    std::string path = outputDir + "/" + SIGNATURE_REGISTRY_FILENAME;
    std::ifstream in(path);
    if (!in.is_open()) return;

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, in, &root, &errs)) {
        std::cerr << "WARNING: could not parse " << path << " (" << errs
                  << ") — ignoring persisted collision registry" << std::endl;
        return;
    }

    for (const auto& key : root.getMemberNames()) {
        if (registry.count(key)) continue;  // global entries always win
        RegisteredModel rm;
        rm.sourceArchive = root[key]["source"].asString();
        for (const auto& t : root[key]["signature"]) rm.signature.push_back(t.asString());
        registry[key] = rm;
    }
}

static void savePersistedRegistry(const std::string& outputDir,
                                   const std::map<std::string, RegisteredModel>& registry,
                                   const std::map<std::string, RegisteredModel>& globalOnly) {
    std::string path = outputDir + "/" + SIGNATURE_REGISTRY_FILENAME;
    Json::Value root(Json::objectValue);
    for (const auto& [key, rm] : registry) {
        if (globalOnly.count(key)) continue;  // global entries are reproducible, don't persist
        Json::Value entry;
        entry["source"] = rm.sourceArchive;
        Json::Value sig(Json::arrayValue);
        for (const auto& t : rm.signature) sig.append(t);
        entry["signature"] = sig;
        root[key] = entry;
    }
    std::ofstream out(path);
    if (out.is_open()) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &out);
        out << std::endl;
    }
}

// Reads docs/reference/pre_luclin_zones.json's flattened list of short_name
// values (all expansions' zones concatenated) — the --all-zones roster.
static std::vector<std::string> loadZoneShortNames(const std::string& zonesJsonPath) {
    std::vector<std::string> names;
    std::ifstream in(zonesJsonPath);
    if (!in.is_open()) return names;

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, in, &root, &errs)) {
        std::cerr << "ERROR: could not parse " << zonesJsonPath << ": " << errs << std::endl;
        return names;
    }

    const Json::Value& expansions = root["expansions"];
    for (const auto& expName : expansions.getMemberNames()) {
        const Json::Value& zones = expansions[expName]["zones"];
        for (const auto& z : zones) {
            if (z.isMember("short_name")) names.push_back(z["short_name"].asString());
        }
    }
    return names;
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
              << "  --max-tiles <N>      Skip a texture if its LOGICAL UV extent (the actual\n"
              << "                       repeat count the geometry uses) exceeds this (default: 8)\n"
              << "  --sheet-size <N>     Max composite sheet dimension (default: 1024)\n"
              << "  --eqe1               Also output ETC1-compressed .eqe1 files\n"
              << "  --verbose            Per-texture stats\n"
              << "\n"
              << "P16 sheet-quality options:\n"
              << "  --tile-cap <N>       Cap PHYSICAL unroll copies per axis (default: 1).\n"
              << "                       Repeats beyond this rely on the seam gutter's wrap\n"
              << "                       padding + the runtime's fmod-based UV wrap (see\n"
              << "                       mesh_building.cpp) instead of physically duplicating\n"
              << "                       pixel data — this is the unroll-inflation fix.\n"
              << "  --seam-pad <N>       UV inset width in texels: the outer N texels of each\n"
              << "                       placed texture are edge-duplicated in place (no sheet\n"
              << "                       growth) and excluded from the sampled UV range, to\n"
              << "                       stop bilinear bleed at tile boundaries (default: 2)\n"
              << "\n"
              << "P08f zone-archive options:\n"
              << "  --zone <shortname>   Build sheets for one zone's {shortname}_chr.s3d\n"
              << "                       archive (e.g., befallen, qeynos2, gfaydark)\n"
              << "  --all-zones          Build sheets for every zone in --zones-json\n"
              << "  --zones-json <path>  docs/reference/pre_luclin_zones.json (required\n"
              << "                       with --all-zones)\n"
              << "  --max-variant <N>    Highest variant number to probe for existence\n"
              << "                       when discovering a zone model's variant set\n"
              << "                       (default: 9)\n"
              << "  --report-json <path> Write a structured build report (per-zone\n"
              << "                       model status: built/duplicate/collision/\n"
              << "                       animated-skip/failed) to this path\n"
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
    // P16: unroll discipline (physical tile cap) + seam-gutter width.
    // Default cap=1: mathematically exact (not an approximation) for EQ
    // character textures specifically, because the original engine only
    // ever samples these with GL_REPEAT wrap in the first place — that is
    // why geometry UVs exceed [0,1] at all. A single physical copy + the
    // fmod-based runtime wrap (mesh_building.cpp) + the seam inset above
    // reproduces the N-copy physical unroll pixel-for-pixel, at ~61.5%
    // less sheet bytes measured across the global race set (vs cap=2's
    // 16.8% — most textures already had tilesU/V <= 2 and were untouched
    // by a cap of 2, so it undershot P13's projected 50-65% reduction).
    int physicalTileCap = 1;
    int seamPad = 2;
    // P08f additions. --zone may be repeated (accumulates) — processing every
    // requested zone within ONE process run is what lets the in-memory
    // collision registry (A2) catch cross-zone collisions too, not just
    // zone-vs-global; two separate tool invocations would each start from a
    // fresh registry and could each independently overwrite the other's
    // same-named sheet without ever detecting it. --all-zones adds the full
    // pre_luclin_zones.json roster to whatever --zone(s) were also given.
    std::vector<std::string> zoneArgs;
    bool allZones = false;
    std::string zonesJsonPath;
    int maxVariantProbe = 9;
    std::string reportJsonPath;

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
        else if (arg == "--tile-cap" && i + 1 < argc) physicalTileCap = std::atoi(argv[++i]);
        else if (arg == "--seam-pad" && i + 1 < argc) seamPad = std::atoi(argv[++i]);
        else if (arg == "--zone" && i + 1 < argc) zoneArgs.push_back(argv[++i]);
        else if (arg == "--all-zones") allZones = true;
        else if (arg == "--zones-json" && i + 1 < argc) zonesJsonPath = argv[++i];
        else if (arg == "--max-variant" && i + 1 < argc) maxVariantProbe = std::atoi(argv[++i]);
        else if (arg == "--report-json" && i + 1 < argc) reportJsonPath = argv[++i];
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
    }

    bool zoneMode = !zoneArgs.empty() || allZones;

    if (eqPath.empty() || outputDir.empty() ||
        (!zoneMode && !allRaces && filterRace.empty())) {
        printUsage(argv[0]);
        return 1;
    }
    if (allZones && zonesJsonPath.empty()) {
        std::cerr << "Error: --all-zones requires --zones-json <path to pre_luclin_zones.json>"
                  << std::endl;
        return 1;
    }

    if (!fs::exists(eqPath) || !fs::is_directory(eqPath)) {
        std::cerr << "Error: EQ path does not exist: " << eqPath << std::endl;
        return 1;
    }

    fs::create_directories(outputDir);

    if (zoneMode) {
        // ── P08f zone-archive build path (plan A1-A4, B5-B7) ────────────────
        // Deliberately a SEPARATE code path from the original global-only flow
        // below (not a refactor of it) so that no plain `--race`/`--all`
        // invocation's behavior or output changes at all — see plan A1's
        // "keep global behavior byte-identical" requirement.
        auto overallStart = std::chrono::steady_clock::now();

        std::vector<std::string> zoneNames = zoneArgs;
        if (allZones) {
            auto allNames = loadZoneShortNames(zonesJsonPath);
            for (auto& n : allNames) {
                if (std::find(zoneNames.begin(), zoneNames.end(), n) == zoneNames.end())
                    zoneNames.push_back(n);
            }
        }

        // Seed the collision registry with every global model (A2) — loaded
        // quietly, purely for name/signature comparison; global sheets are
        // NOT (re)written by this path.
        auto globalArchives = loadGlobalArchives(eqPath, "", false);
        std::map<std::string, RegisteredModel> registry;
        registerArchives(globalArchives, registry);
        std::map<std::string, RegisteredModel> globalOnly = registry;  // snapshot before zone entries
        std::cout << "Collision registry seeded with " << registry.size()
                  << " global model names" << std::endl;
        loadPersistedRegistry(outputDir, registry);
        std::cout << "Collision registry after loading prior-run zone entries: "
                  << registry.size() << " model names" << std::endl;

        // A REGISTERED model name (known signature, from a global archive or an
        // earlier zone in this registry) is NOT necessarily already BUILT — the
        // 210 pre-existing sheets cover only 42 of the ~59 distinct global race
        // defs (the rest were apparently never included in whatever invocation
        // produced the original set). Registry membership alone is therefore the
        // wrong signal for "skip, already covered": it must be cross-checked
        // against sheets that actually exist on disk, or a same-named,
        // same-signature model that has no sheet yet would be wrongly skipped
        // forever as a false "duplicate" (caught empirically: IMP_HS_DEF is a
        // real global2_chr.s3d model with no pre-existing sheet).
        std::set<std::string> existingSheetRaces;
        if (fs::exists(outputDir)) {
            for (const auto& entry : fs::directory_iterator(outputDir)) {
                if (!entry.is_regular_file()) continue;
                std::string fn = entry.path().filename().string();
                if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".bmp") continue;
                std::string stem = fn.substr(0, fn.size() - 4);
                auto lastUnderscore = stem.rfind('_');
                if (lastUnderscore == std::string::npos) continue;
                auto secondLast = stem.rfind('_', lastUnderscore - 1);
                if (secondLast == std::string::npos) continue;
                existingSheetRaces.insert(stem.substr(0, secondLast));
            }
        }
        std::cout << "Found " << existingSheetRaces.size()
                  << " distinct race names with an existing sheet on disk in " << outputDir
                  << std::endl;

        Json::Value report(Json::objectValue);
        Json::Value zonesReport(Json::objectValue);
        std::vector<ModelResult> allNewResults;
        int totalBuilt = 0, totalDuplicate = 0, totalCollision = 0,
            totalAnimatedSkip = 0, totalFailed = 0, totalNoArchive = 0;

        for (const auto& zoneName : zoneNames) {
            auto zoneStart = std::chrono::steady_clock::now();
            Json::Value zoneReport(Json::objectValue);
            Json::Value modelsReport(Json::objectValue);

            auto archiveOpt = loadZoneArchive(eqPath, zoneName, verbose);
            if (!archiveOpt) {
                zoneReport["archiveFound"] = false;
                zonesReport[zoneName] = zoneReport;
                totalNoArchive++;
                std::cout << "[" << zoneName << "] no " << zoneName
                          << "_chr.s3d (or no usable characters) — skipped" << std::endl;
                continue;
            }
            const ArchiveModels& am = *archiveOpt;
            zoneReport["archiveFound"] = true;
            zoneReport["archiveName"] = am.archiveName;
            zoneReport["modelsTotal"] = static_cast<int>(am.models.size());

            PfsArchive archive;
            if (!archive.open(am.archivePath)) {
                std::cerr << "Cannot open archive: " << am.archivePath << std::endl;
                zoneReport["archiveOpenFailed"] = true;
                zonesReport[zoneName] = zoneReport;
                continue;
            }

            int zoneBuilt = 0;
            for (const auto& model : am.models) {
                std::string raceKey = toUpper(model->name);
                Json::Value modelReport(Json::objectValue);

                auto regIt = registry.find(raceKey);
                bool hasExistingSheet = existingSheetRaces.count(toLower(raceKey)) > 0;
                if (regIt != registry.end()) {
                    auto sig = collectSignature(model);
                    if (sig == regIt->second.signature) {
                        if (hasExistingSheet) {
                            // Genuinely already covered — a sheet for this exact
                            // model exists on disk (either the original 210 or an
                            // earlier zone/model in this same run).
                            modelReport["status"] = "duplicate";
                            modelReport["existingSource"] = regIt->second.sourceArchive;
                            modelsReport[raceKey] = modelReport;
                            totalDuplicate++;
                            continue;
                        }
                        // Known model, matching signature, but NO sheet actually
                        // built yet (e.g. a global race def outside the original
                        // 210) — fall through to the normal build path below
                        // instead of skipping.
                    } else {
                        modelReport["status"] = "collision";
                        modelReport["existingSource"] = regIt->second.sourceArchive;
                        modelReport["reason"] = "same race name, different texture signature — "
                                                 "not rebuilt to avoid silently overwriting the "
                                                 "existing sheet";
                        modelsReport[raceKey] = modelReport;
                        totalCollision++;
                        std::cerr << "COLLISION: " << raceKey << " in " << am.archiveName
                                  << " has a different texture set than the existing "
                                  << regIt->second.sourceArchive << " model of the same name — skipped"
                                  << std::endl;
                        continue;
                    }
                }

                std::string animTexName;
                if (detectAnimatedTexture(model, animTexName)) {
                    modelReport["status"] = "animatedSkip";
                    modelReport["animatedTexture"] = animTexName;
                    modelsReport[raceKey] = modelReport;
                    totalAnimatedSkip++;
                    std::cout << "  [" << raceKey << "] SKIP animated texture: "
                              << animTexName << std::endl;
                    // Still register the name (with its signature) so a LATER zone/global
                    // archive with the identical model is recognized as a duplicate, not
                    // re-flagged as a fresh animated-skip.
                    registry[raceKey] = RegisteredModel{am.archiveName, collectSignature(model)};
                    continue;
                }

                auto variantsToBuild = discoverVariants(archive, model, maxVariantProbe);
                Json::Value variantsBuiltArr(Json::arrayValue);
                Json::Value variantsDiscoveredArr(Json::arrayValue);
                for (int v : variantsToBuild) variantsDiscoveredArr.append(v);

                bool anyBuilt = false;
                for (int v : variantsToBuild) {
                    std::cout << "\n[" << model->name << " variant=" << v << "] "
                              << am.archiveName << " (zone=" << zoneName << ")" << std::endl;
                    auto result = processModel(eqPath, model, am.archivePath, archive,
                                                v, maxTiles, maxSheetSize, outputDir,
                                                writeEqe1, verbose, physicalTileCap, seamPad,
                                                /*checkAnimatedTextures=*/true);
                    if (result.success) {
                        variantsBuiltArr.append(v);
                        allNewResults.push_back(result);
                        anyBuilt = true;
                        zoneBuilt++;
                        totalBuilt++;
                        existingSheetRaces.insert(toLower(raceKey));
                    } else if (result.skippedAnimated) {
                        // Shouldn't normally trigger (whole-model probe already caught it),
                        // but a per-variant substituted texture could in principle differ —
                        // report defensively rather than silently drop.
                        std::cout << "  variant=" << v << " SKIP (animated texture "
                                  << result.animatedTextureName << ")" << std::endl;
                    } else {
                        std::cout << "  variant=" << v << " FAILED to build" << std::endl;
                        totalFailed++;
                    }
                }

                modelReport["status"] = anyBuilt ? "built" : "failed";
                modelReport["variantsDiscovered"] = variantsDiscoveredArr;
                modelReport["variantsBuilt"] = variantsBuiltArr;
                modelsReport[raceKey] = modelReport;

                registry[raceKey] = RegisteredModel{am.archiveName, collectSignature(model)};
            }

            auto zoneEnd = std::chrono::steady_clock::now();
            double zoneSecs = std::chrono::duration<double>(zoneEnd - zoneStart).count();
            zoneReport["sheetsBuilt"] = zoneBuilt;
            zoneReport["elapsedSeconds"] = zoneSecs;
            zoneReport["models"] = modelsReport;
            zonesReport[zoneName] = zoneReport;

            std::cout << "\n[" << zoneName << "] " << zoneBuilt << " sheets built in "
                      << zoneSecs << "s" << std::endl;
        }

        mergeCompositeIndex(outputDir, allNewResults);
        savePersistedRegistry(outputDir, registry, globalOnly);

        auto overallEnd = std::chrono::steady_clock::now();
        double overallSecs = std::chrono::duration<double>(overallEnd - overallStart).count();

        report["zones"] = zonesReport;
        report["summary"]["sheetsBuilt"] = totalBuilt;
        report["summary"]["duplicates"] = totalDuplicate;
        report["summary"]["collisions"] = totalCollision;
        report["summary"]["animatedSkips"] = totalAnimatedSkip;
        report["summary"]["buildFailures"] = totalFailed;
        report["summary"]["zonesWithNoArchive"] = totalNoArchive;
        report["summary"]["elapsedSeconds"] = overallSecs;

        std::cout << "\n=== P08f Zone Build Summary ===" << std::endl;
        std::cout << "  Sheets built: " << totalBuilt << std::endl;
        std::cout << "  Duplicates (already covered, skipped): " << totalDuplicate << std::endl;
        std::cout << "  Collisions (reported, NOT built): " << totalCollision << std::endl;
        std::cout << "  Animated-texture skips: " << totalAnimatedSkip << std::endl;
        std::cout << "  Build failures: " << totalFailed << std::endl;
        std::cout << "  Zones with no _chr.s3d: " << totalNoArchive << std::endl;
        std::cout << "  Elapsed: " << overallSecs << "s" << std::endl;

        if (!reportJsonPath.empty()) {
            std::ofstream reportOut(reportJsonPath);
            if (reportOut.is_open()) {
                Json::StreamWriterBuilder builder;
                builder["indentation"] = "  ";
                std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
                writer->write(report, &reportOut);
                reportOut << std::endl;
                std::cout << "Report: " << reportJsonPath << std::endl;
            } else {
                std::cerr << "WARNING: could not write report to " << reportJsonPath << std::endl;
            }
        }

        return 0;
    }

    // ── Original global-only build path (UNCHANGED — byte-identical output) ──

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
                                            writeEqe1, verbose, physicalTileCap, seamPad);

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
