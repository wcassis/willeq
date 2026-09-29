// eqe1_builder - Offline tool to convert EQ S3D archives to EQE1 flat pack files.
//
// Reads S3D archives, decodes BMP/DDS textures to RGBA, compresses to ETC1 via Etc2Comp
// with full mipmap chains (down to 4x4), and writes .eqe1 flat pack files.
// Non-texture data (.wld files) is embedded as raw data entries.
//
// Usage:
//   ./eqe1_builder --eq-path /path/to/EverQuestP1999 --archive qeynos2.s3d --output /path/to/output/
//   ./eqe1_builder --eq-path /path/to/EverQuestP1999 --all --output /path/to/output/
//
// Design: docs/plans/full_etc1_textures.md

#include "client/graphics/eqe1_format.h"
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
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace EQT::Graphics;

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Strip file extension (e.g., "foo.bmp" -> "foo")
static std::string stripExtension(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot != std::string::npos) return name.substr(0, dot);
    return name;
}

// ── BMP decoder ──────────────────────────────────────────────────────────────
// Handles 8-bit paletted, 24-bit, and 32-bit uncompressed BMPs.
// Output: RGBA pixels, bottom-up → top-down.

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
        // Read colorsUsed from DIB header (offset 46). 0 means 256.
        uint32_t colorsUsed = *reinterpret_cast<const uint32_t*>(d + 46);
        if (colorsUsed == 0) colorsUsed = 256;

        uint32_t dibHeaderSize = *reinterpret_cast<const uint32_t*>(d + 14);
        const uint8_t* palette = d + 14 + dibHeaderSize;
        size_t paletteSize = colorsUsed * 4;
        if (14 + dibHeaderSize + paletteSize > data.size()) return false;

        const uint8_t* pixels = d + dataOffset;
        int rowStride = (width + 3) & ~3;

        // EQ convention: palette index 0 is the transparent color.
        // Pixels referencing index 0 get alpha=0, all others get alpha=255.
        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? (height - 1 - y) : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = pixels[srcY * rowStride + x];
                if (idx >= colorsUsed) idx = 0;  // safety clamp
                int outIdx = (y * width + x) * 4;
                outRGBA[outIdx + 0] = palette[idx * 4 + 2]; // R
                outRGBA[outIdx + 1] = palette[idx * 4 + 1]; // G
                outRGBA[outIdx + 2] = palette[idx * 4 + 0]; // B
                outRGBA[outIdx + 3] = (idx == 0) ? 0 : 255; // Index 0 = transparent
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
                outRGBA[outIdx + 3] = 255;
            }
        }
        return true;
    } else if (bpp == 32 && (compression == 0 || compression == 3)) {
        // compression == 3 is BI_BITFIELDS (BITMAPV4/V5 headers) — same BGRA layout for 32bpp
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

// ── Alpha detection ──────────────────────────────────────────────────────────

static bool detectAlpha(const uint8_t* rgba, int w, int h) {
    for (int i = 0; i < w * h; ++i) {
        if (rgba[i * 4 + 3] < 255) return true;
    }
    return false;
}

// ── ETC1 encoding via Etc2Comp ───────────────────────────────────────────────

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

// ── Mipmap generation ────────────────────────────────────────────────────────

// Box-filter downsample RGBA by 2x, down to 1x1.
// Sub-4x4 levels are valid — ETC1 pads to one 4x4 block (8 bytes).
static std::vector<uint8_t> downsample2x(const uint8_t* src, int srcW, int srcH,
                                          int& dstW, int& dstH) {
    dstW = std::max(1, srcW / 2);
    dstH = std::max(1, srcH / 2);
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

// Generate full ETC1 mipmap chain. Returns concatenated ETC1 data for all levels.
static std::vector<uint8_t> generateMipChain(const uint8_t* rgba, int width, int height,
                                              bool isAlpha, uint8_t& outMipLevels) {
    outMipLevels = computeMipLevels(static_cast<uint16_t>(width),
                                     static_cast<uint16_t>(height));
    std::vector<uint8_t> result;

    // Level 0
    auto level0 = isAlpha ? encodeETC1_Alpha(rgba, width, height)
                          : encodeETC1_RGB(rgba, width, height);
    result.insert(result.end(), level0.begin(), level0.end());

    // Subsequent levels
    std::vector<uint8_t> current(rgba, rgba + width * height * 4);
    int w = width, h = height;

    for (uint8_t level = 1; level < outMipLevels; ++level) {
        int dw, dh;
        auto downsampled = downsample2x(current.data(), w, h, dw, dh);
        w = dw;
        h = dh;

        auto encoded = isAlpha ? encodeETC1_Alpha(downsampled.data(), w, h)
                               : encodeETC1_RGB(downsampled.data(), w, h);
        result.insert(result.end(), encoded.begin(), encoded.end());

        current = std::move(downsampled);
    }

    return result;
}

// ── v2 payload encoding (uncompressed RGB565/RGBA4444/RGBA8888) ────────────
//
// --payload-format etc1 (default) keeps the original ETC1 encode path
// (generateMipChain, above) completely untouched byte-for-byte. These
// helpers are only reached when --payload-format rgba8888|rgb565 is passed.

enum class BuilderPayloadMode { ETC1, RGBA8888, RGB565 };

static inline uint16_t packRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline uint16_t packRGBA4444(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint16_t>(((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4));
}

// Pack one RGBA8 level into the target v2 payload's byte layout (little-endian
// for the 16-bit formats, matching GL_UNSIGNED_SHORT_5_6_5 / _4_4_4_4 on both
// x86 and ARM — both are little-endian targets for this project).
static std::vector<uint8_t> packLevel(const uint8_t* rgba, int width, int height,
                                       EQE1PayloadFormat fmt) {
    std::vector<uint8_t> out;
    if (fmt == EQE1_PAYLOAD_RGBA8888) {
        out.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
        return out;
    }
    out.resize(static_cast<size_t>(width) * height * 2);
    for (int i = 0; i < width * height; ++i) {
        uint8_t r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
        uint16_t packed = (fmt == EQE1_PAYLOAD_RGB565) ? packRGB565(r, g, b) : packRGBA4444(r, g, b, a);
        out[i * 2 + 0] = static_cast<uint8_t>(packed & 0xFF);
        out[i * 2 + 1] = static_cast<uint8_t>((packed >> 8) & 0xFF);
    }
    return out;
}

// Generate a full uncompressed mip chain for a v2 payload format. Reuses the
// same box-filter downsample2x as the ETC1 path so mip dimensions/levels are
// identical across formats (fair comparison basis for the shootout).
static std::vector<uint8_t> generateV2MipChain(const uint8_t* rgba, int width, int height,
                                                EQE1PayloadFormat fmt, uint8_t& outMipLevels) {
    outMipLevels = computeMipLevels(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
    std::vector<uint8_t> result;

    auto level0 = packLevel(rgba, width, height, fmt);
    result.insert(result.end(), level0.begin(), level0.end());

    std::vector<uint8_t> current(rgba, rgba + static_cast<size_t>(width) * height * 4);
    int w = width, h = height;
    for (uint8_t level = 1; level < outMipLevels; ++level) {
        int dw, dh;
        auto downsampled = downsample2x(current.data(), w, h, dw, dh);
        w = dw;
        h = dh;
        auto packed = packLevel(downsampled.data(), w, h, fmt);
        result.insert(result.end(), packed.begin(), packed.end());
        current = std::move(downsampled);
    }
    return result;
}

// ── Data structures ─────────────────────────────────────────────────────────

struct TextureEntry {
    std::string name;       // lowercase, no extension
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    std::vector<uint8_t> rgbETC1;      // full mip chain (ETC1-compressed, or raw v2 payload bytes)
    std::vector<uint8_t> alphaETC1;    // full mip chain (empty if opaque, or if v2 payload — no companion)
    uint8_t mipLevels = 0;
    uint8_t payloadFormat = EQE1_PAYLOAD_ETC1;
};

// Encode one decoded RGBA texture into a TextureEntry per the requested
// builder mode. Shared by both S3D-archive and BMP-directory processing so
// the two code paths can't drift.
static TextureEntry encodeTexture(const std::string& name, const uint8_t* rgba,
                                   int width, int height, bool hasAlpha,
                                   BuilderPayloadMode mode) {
    TextureEntry tex;
    tex.name = name;
    tex.width = width;
    tex.height = height;
    tex.hasAlpha = hasAlpha;

    if (mode == BuilderPayloadMode::ETC1) {
        tex.payloadFormat = EQE1_PAYLOAD_ETC1;
        tex.rgbETC1 = generateMipChain(rgba, width, height, false, tex.mipLevels);
        if (hasAlpha) {
            uint8_t alphaMips = 0;
            tex.alphaETC1 = generateMipChain(rgba, width, height, true, alphaMips);
        }
        return tex;
    }

    // v2 modes never build an alpha companion — alpha (if any) is carried
    // in-band by the payload format itself.
    EQE1PayloadFormat fmt;
    if (mode == BuilderPayloadMode::RGBA8888) {
        fmt = EQE1_PAYLOAD_RGBA8888;
    } else {  // RGB565 mode: opaque -> RGB565, alpha-carrying -> RGBA4444
        fmt = hasAlpha ? EQE1_PAYLOAD_RGBA4444 : EQE1_PAYLOAD_RGB565;
    }
    tex.payloadFormat = fmt;
    tex.rgbETC1 = generateV2MipChain(rgba, width, height, fmt, tex.mipLevels);
    return tex;
}

struct RawDataEntry {
    std::string name;       // lowercase, with extension (e.g., "zone.wld")
    std::vector<char> data;
};

// ── EQE1 file assembly (shared by both modes) ──────────────────────────────
//
// v3 name table (B910, filed as B100). The 48-byte index entry carries a 32-byte name field
// (31 chars + NUL). EQ texture names reach 32+ characters — soldunga's
// gnome_stronghold_floor_transition is 33 — and this function's
// strncpy(name, ..., 31) used to truncate them silently, so the client's
// full-name lookup missed and the all-or-nothing rule FATAL-exited. v3
// (EQE1_VERSION_V3) fixes it for exactly the packs that need it: a pack
// whose every name fits in 31 chars writes v1 (ETC1) / v2 (any non-ETC1
// payload) byte-for-byte as before; a pack with a longer name becomes v3,
// where each index entry gains an 8-byte {nameOffset, nameLength} tail and
// the long name is stored once in a name table between the index and the
// data section (the data offsets — and therefore the table offsets — are
// all computable up front, so no back-patching is needed). See
// eqe1_format.h for the on-disk layout.
static bool writeEQE1File(const std::vector<TextureEntry>& textures,
                           const std::vector<RawDataEntry>& rawEntries,
                           const std::string& outputPath,
                           bool verbose,
                           uint16_t fileVersion = EQE1_VERSION) {
    // Calculate total entries (textures + alpha companions + raw data).
    // v2 payload formats never get a companion entry — their alpha (if any)
    // is carried in-band by the payload bytes themselves.
    uint16_t numEntries = 0;
    int alphaCount = 0;
    for (const auto& tex : textures) {
        numEntries++;  // main entry
        bool buildsCompanion = tex.hasAlpha && tex.payloadFormat == EQE1_PAYLOAD_ETC1;
        if (buildsCompanion) { numEntries++; }
        if (tex.hasAlpha) { alphaCount++; }
    }
    numEntries += static_cast<uint16_t>(rawEntries.size());

    // The entry set is fixed from here on; collect the names every entry
    // will carry, deduplicated, so the v3 name table stores each once.
    // (Alpha companions append "_alpha"; a base name at 25 chars pushes its
    // companion over the 31-char inline limit too.)
    const auto companionOf = [](const std::string& base) { return base + "_alpha"; };
    std::vector<std::string> entryNames;
    for (const auto& tex : textures) {
        entryNames.push_back(tex.name);
        if (tex.hasAlpha && tex.payloadFormat == EQE1_PAYLOAD_ETC1) {
            entryNames.push_back(companionOf(tex.name));
        }
    }
    for (const auto& raw : rawEntries) entryNames.push_back(raw.name);

    // A pack whose every name fits the 31-char inline field stays v1/v2,
    // byte-for-byte the format the pinned tree (and its sha256 pins) expects.
    bool needsNameTable = false;
    for (const auto& n : entryNames) {
        if (n.size() > EQE1_INLINE_NAME_LIMIT) { needsNameTable = true; break; }
    }
    const uint16_t version = needsNameTable ? EQE1_VERSION_V3 : fileVersion;
    const uint32_t stride = indexEntryStride(version);

    // v3: build the name table BEFORE computing data offsets — it sits
    // between the index and the data section, so its size shifts every data
    // offset. Each long name is stored once, in first-entry order
    // (deduplicated: a name and its companion only collide when equal, which
    // cannot happen since companions always append "_alpha").
    std::string nameTable;
    std::map<std::string, uint32_t> nameTablePos;
    if (version == EQE1_VERSION_V3) {
        for (const auto& n : entryNames) {
            if (n.size() <= EQE1_INLINE_NAME_LIMIT) continue;
            if (nameTablePos.find(n) == nameTablePos.end()) {
                nameTablePos.emplace(n, static_cast<uint32_t>(nameTable.size()));
                nameTable += n;
            }
        }
    }

    // Compute data offsets: header + index block + (v3) name table, then
    // 16-byte align. For v1/v2 nameTable is empty, so this is exactly the
    // historical layout (alignTo16(indexEnd)).
    uint32_t indexEnd = sizeof(EQE1Header) + numEntries * stride;
    uint32_t dataOffset = alignTo16(indexEnd + static_cast<uint32_t>(nameTable.size()));

    // Build index entries and track data positions. v3 names longer than the
    // inline limit reference the pre-built name table; their 32-byte inline
    // field is zeroed.
    struct EntryInfo {
        EQE1IndexEntry index;
        EQE1IndexEntryNameTail tail = {};
        const uint8_t* data;
        uint32_t dataSize;
    };
    std::vector<EntryInfo> entries;
    entries.reserve(numEntries);

    auto assignName = [&](EQE1IndexEntry& idx, const std::string& name,
                          EQE1IndexEntryNameTail& outTail) {
        outTail = {};
        if (name.size() <= EQE1_INLINE_NAME_LIMIT) {
            std::strncpy(idx.name, name.c_str(), EQE1_NAME_SIZE - 1);
        } else {
            idx.name[0] = '\0';  // zero the whole 32-byte field
            // entryNames covered every entry name, so the lookup is a hit.
            const auto it = nameTablePos.find(name);
            outTail.nameOffset = static_cast<uint32_t>(indexEnd) + it->second;
            outTail.nameLength = static_cast<uint32_t>(name.size());
        }
    };

    for (const auto& tex : textures) {
        bool buildsCompanion = tex.hasAlpha && tex.payloadFormat == EQE1_PAYLOAD_ETC1;

        // Main entry
        EQE1IndexEntry idx = {};
        EQE1IndexEntryNameTail mainTail = {};
        assignName(idx, tex.name, mainTail);
        idx.width = static_cast<uint16_t>(tex.width);
        idx.height = static_cast<uint16_t>(tex.height);
        idx.mipLevels = tex.mipLevels;
        idx.hasAlpha = tex.hasAlpha ? 1 : 0;
        idx.entryType = buildsCompanion ? EQE1_TYPE_ETC1_RGBA : EQE1_TYPE_ETC1_RGB;
        idx.dataOffset = dataOffset;
        idx.dataSize = static_cast<uint32_t>(tex.rgbETC1.size());
        idx.payloadFormat = tex.payloadFormat;
        entries.push_back({idx, mainTail, tex.rgbETC1.data(), idx.dataSize});
        dataOffset = alignTo16(dataOffset + idx.dataSize);

        // Alpha companion entry — ETC1 payload only. v2 payload formats
        // carry alpha in-band and never build one (idx.hasAlpha may still be
        // 1 for them, but buildsCompanion is false).
        if (buildsCompanion) {
            EQE1IndexEntry alphaIdx = {};
            std::string alphaName = companionOf(tex.name);
            EQE1IndexEntryNameTail alphaTail = {};
            assignName(alphaIdx, alphaName, alphaTail);
            alphaIdx.width = static_cast<uint16_t>(tex.width);
            alphaIdx.height = static_cast<uint16_t>(tex.height);
            alphaIdx.mipLevels = tex.mipLevels;
            alphaIdx.hasAlpha = 0;
            alphaIdx.entryType = EQE1_TYPE_ETC1_RGB;  // alpha stored as grayscale RGB
            alphaIdx.dataOffset = dataOffset;
            alphaIdx.dataSize = static_cast<uint32_t>(tex.alphaETC1.size());
            alphaIdx.payloadFormat = EQE1_PAYLOAD_ETC1;
            entries.push_back({alphaIdx, alphaTail, tex.alphaETC1.data(), alphaIdx.dataSize});
            dataOffset = alignTo16(dataOffset + alphaIdx.dataSize);
        }
    }

    // Raw data entries
    for (const auto& raw : rawEntries) {
        EQE1IndexEntry idx = {};
        EQE1IndexEntryNameTail rawTail = {};
        assignName(idx, raw.name, rawTail);
        idx.width = 0;
        idx.height = 0;
        idx.mipLevels = 0;
        idx.hasAlpha = 0;
        idx.entryType = EQE1_TYPE_RAW_DATA;
        idx.dataOffset = dataOffset;
        idx.dataSize = static_cast<uint32_t>(raw.data.size());
        idx.payloadFormat = EQE1_PAYLOAD_ETC1;
        entries.push_back({idx, rawTail, reinterpret_cast<const uint8_t*>(raw.data.data()), idx.dataSize});
        dataOffset = alignTo16(dataOffset + idx.dataSize);
    }

    assert(entries.size() == numEntries);

    // Write output file
    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "  Cannot open output: " << outputPath << std::endl;
        return false;
    }

    // Write header
    EQE1Header header = {};
    header.magic = EQE1_MAGIC;
    header.version = version;
    header.numEntries = numEntries;
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write index entries (v3: each with its 8-byte name-table tail)
    for (const auto& entry : entries) {
        out.write(reinterpret_cast<const char*>(&entry.index), sizeof(EQE1IndexEntry));
        if (version == EQE1_VERSION_V3) {
            out.write(reinterpret_cast<const char*>(&entry.tail),
                      sizeof(EQE1IndexEntryNameTail));
        }
    }

    // v3: write the name table immediately after the index, before the data
    // section (16-byte aligned; the data offsets already account for it via
    // the stride, because the index block it extends is what indexEnd sized).
    if (version == EQE1_VERSION_V3) {
        out.write(nameTable.data(), static_cast<std::streamsize>(nameTable.size()));
    }

    // Write data with 16-byte alignment padding
    for (const auto& entry : entries) {
        auto currentPos = static_cast<uint32_t>(out.tellp());
        if (currentPos < entry.index.dataOffset) {
            uint32_t padSize = entry.index.dataOffset - currentPos;
            std::vector<uint8_t> padding(padSize, 0);
            out.write(reinterpret_cast<const char*>(padding.data()), padSize);
        }
        out.write(reinterpret_cast<const char*>(entry.data), entry.dataSize);
    }

    out.close();

    size_t fileSize = fs::file_size(outputPath);
    std::cout << "  Written: " << outputPath << " (v" << version << ", "
              << fileSize / 1024 << " KB, "
              << textures.size() << " textures"
              << (alphaCount > 0 ? ", " + std::to_string(alphaCount) + " alpha" : "")
              << (!rawEntries.empty() ? ", " + std::to_string(rawEntries.size()) + " raw" : "")
              << (version == EQE1_VERSION_V3
                     ? ", name table " + std::to_string(nameTablePos.size()) + " long name(s)"
                     : "")
              << ")" << std::endl;

    return true;
}

// ── Archive processing ───────────────────────────────────────────────────────

static bool processArchive(const std::string& eqPath,
                            const std::string& archiveName,
                            const std::string& outputDir,
                            bool verbose,
                            BuilderPayloadMode mode) {
    std::string archivePath = eqPath + "/" + archiveName;
    PfsArchive archive;
    if (!archive.open(archivePath)) {
        std::cerr << "  Could not open: " << archivePath << std::endl;
        return false;
    }

    // Extract texture filenames (.bmp and .dds)
    std::vector<std::string> bmpFiles;
    archive.getFilenames(".bmp", bmpFiles);
    std::vector<std::string> ddsFiles;
    archive.getFilenames(".dds", ddsFiles);
    bmpFiles.insert(bmpFiles.end(), ddsFiles.begin(), ddsFiles.end());

    // Extract non-texture filenames
    std::vector<std::string> wldFiles;
    archive.getFilenames(".wld", wldFiles);

    std::cout << "  Files: " << bmpFiles.size() << " textures, "
              << wldFiles.size() << " wld" << std::endl;

    // Decode and encode all textures
    std::vector<TextureEntry> textures;
    int failCount = 0;

    for (const auto& filename : bmpFiles) {
        std::vector<char> fileData;
        if (!archive.get(filename, fileData) || fileData.empty()) {
            failCount++;
            continue;
        }

        std::string texName = stripExtension(toLower(filename));

        // Decode BMP or DDS-as-BMP
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        if (DDSDecoder::isDDS(fileData)) {
            auto decoded = DDSDecoder::decode(fileData);
            if (!decoded.isValid()) {
                if (verbose) std::cerr << "    SKIP (DDS decode failed): " << filename << std::endl;
                failCount++;
                continue;
            }
            width = decoded.width;
            height = decoded.height;
            rgba = std::move(decoded.pixels);
        } else {
            if (!decodeBMP(fileData, rgba, width, height)) {
                if (verbose) std::cerr << "    SKIP (BMP decode failed): " << filename << std::endl;
                failCount++;
                continue;
            }
        }

        bool hasAlpha = detectAlpha(rgba.data(), width, height);
        TextureEntry tex = encodeTexture(texName, rgba.data(), width, height, hasAlpha, mode);

        if (verbose) {
            std::cout << "    " << tex.name << " " << tex.width << "x" << tex.height
                      << " " << (int)tex.mipLevels << " mips"
                      << " " << tex.rgbETC1.size() << " bytes"
                      << (tex.hasAlpha ? " +alpha" : "") << std::endl;
        }

        textures.push_back(std::move(tex));
    }

    // Collect raw data entries
    std::vector<RawDataEntry> rawEntries;
    for (const auto& filename : wldFiles) {
        RawDataEntry raw;
        raw.name = toLower(filename);
        if (!archive.get(filename, raw.data) || raw.data.empty()) continue;
        rawEntries.push_back(std::move(raw));
    }

    // Determine output path
    std::string baseName = toLower(archiveName);
    if (baseName.size() > 4 && baseName.substr(baseName.size() - 4) == ".s3d") {
        baseName = baseName.substr(0, baseName.size() - 4);
    }
    std::string outputPath = outputDir + "/" + baseName + ".eqe1";

    // Use shared assembly for texture entries. --payload-format etc1 (the
    // default) writes EQE1_VERSION (1) — byte-for-byte the same file this
    // tool always produced. v2 modes write EQE1_VERSION_V2.
    uint16_t fileVersion = (mode == BuilderPayloadMode::ETC1) ? EQE1_VERSION : EQE1_VERSION_V2;
    if (!writeEQE1File(textures, rawEntries, outputPath, verbose, fileVersion)) {
        return false;
    }

    if (failCount > 0) {
        std::cout << "  (" << failCount << " files skipped)" << std::endl;
    }

    return true;
}

// ── BMP directory processing ────────────────────────────────────────────────

// T400 (board B225): per-texture payload override.
//
// The pack format was designed for this -- an EQE1 index entry carries its
// own payloadFormat byte and readers dispatch on THAT, not on the file
// version (see eqe1_format.h: "v2 packs may mix payload formats
// per-entry"). Until now nothing used it: --payload-format applied to every
// texture in the pack at once.
//
// `particle_atlas` needs it. Its whole content is an ALPHA ramp, and the
// default ETC1 encode stores alpha in a SEPARATE `_alpha` companion entry
// that only the alpha-TEST shader variants sample (ATLAS_ALPHA3D, via
// texture unit 1). Both particle draw paths sample ONE texture and read
// `texColor.a` from it -- ParticleManager's EMT_TRANSPARENT_ALPHA_CHANNEL
// material goes to SOLID3D_FS, and UnifiedParticleRenderer's point-sprite
// shader has a single sampler -- so an ETC1 (RGB-only) particle atlas reads
// alpha=1.0 everywhere no matter what the companion holds. Forcing this one
// 64x64 texture to RGBA8888 puts its alpha in-band where both of them
// actually look.
static BuilderPayloadMode modeForTexture(const std::string& texName,
                                          BuilderPayloadMode defaultMode,
                                          const std::vector<std::string>& rgba8888Names) {
    for (const auto& n : rgba8888Names) {
        if (n == texName) return BuilderPayloadMode::RGBA8888;
    }
    return defaultMode;
}

static bool processBmpDirectories(const std::vector<std::string>& bmpDirs,
                                   const std::string& outputDir,
                                   const std::string& nameOverride,
                                   BuilderPayloadMode mode,
                                   const std::vector<std::string>& rgba8888Names,
                                   bool verbose) {
    // Collect .bmp files and non-texture files (JSON, etc.) as raw data from all directories
    std::vector<fs::path> bmpFiles;
    std::vector<fs::path> rawFiles;
    for (const auto& bmpDir : bmpDirs) {
        for (const auto& entry : fs::directory_iterator(bmpDir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = toLower(entry.path().extension().string());
            if (ext == ".bmp") {
                bmpFiles.push_back(entry.path());
            } else if (ext == ".json" || ext == ".wld") {
                rawFiles.push_back(entry.path());
            }
        }
    }
    std::sort(bmpFiles.begin(), bmpFiles.end());
    std::sort(rawFiles.begin(), rawFiles.end());

    if (bmpFiles.empty()) {
        std::cerr << "  No .bmp files found in specified directories" << std::endl;
        return false;
    }

    std::cout << "  Files: " << bmpFiles.size() << " BMPs" << std::endl;

    // Decode and encode all textures
    std::vector<TextureEntry> textures;
    int failCount = 0;

    for (const auto& bmpPath : bmpFiles) {
        // Read file
        std::ifstream file(bmpPath, std::ios::binary);
        if (!file.is_open()) {
            if (verbose) std::cerr << "    SKIP (cannot open): " << bmpPath << std::endl;
            failCount++;
            continue;
        }
        std::vector<char> fileData((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());

        std::string texName = stripExtension(toLower(bmpPath.filename().string()));

        // Decode BMP
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        if (!decodeBMP(fileData, rgba, width, height)) {
            if (verbose) std::cerr << "    SKIP (BMP decode failed): " << bmpPath << std::endl;
            failCount++;
            continue;
        }

        bool hasAlpha = detectAlpha(rgba.data(), width, height);
        BuilderPayloadMode texMode = modeForTexture(texName, mode, rgba8888Names);
        TextureEntry tex = encodeTexture(texName, rgba.data(), width, height, hasAlpha, texMode);

        if (verbose) {
            std::cout << "    " << tex.name << " " << tex.width << "x" << tex.height
                      << " " << (int)tex.mipLevels << " mips"
                      << " " << tex.rgbETC1.size() << " bytes"
                      << (tex.hasAlpha ? " +alpha" : "")
                      << (texMode != mode ? " [payload override]" : "") << std::endl;
        }

        textures.push_back(std::move(tex));
    }

    if (textures.empty()) {
        std::cerr << "  No textures successfully decoded" << std::endl;
        return false;
    }

    // Determine output base name
    std::string baseName;
    if (!nameOverride.empty()) {
        baseName = toLower(nameOverride);
    } else {
        // Use last component of first directory path
        fs::path dirPath(bmpDirs.front());
        baseName = toLower(dirPath.filename().string());
        if (baseName.empty()) {
            // Handle trailing slash: /foo/bar/ → parent_path().filename()
            baseName = toLower(dirPath.parent_path().filename().string());
        }
    }

    std::string outputPath = outputDir + "/" + baseName + ".eqe1";

    // Include non-texture files as raw data entries
    std::vector<RawDataEntry> rawEntries;
    for (const auto& rawPath : rawFiles) {
        std::ifstream rawFile(rawPath, std::ios::binary);
        if (!rawFile.is_open()) continue;
        RawDataEntry raw;
        raw.name = toLower(rawPath.filename().string());
        raw.data.assign(std::istreambuf_iterator<char>(rawFile),
                        std::istreambuf_iterator<char>());
        if (verbose) {
            std::cout << "    " << raw.name << " (raw, " << raw.data.size() << " bytes)" << std::endl;
        }
        rawEntries.push_back(std::move(raw));
    }

    // A pack carrying ANY non-ETC1 payload is a v2 file, whether that came
    // from --payload-format or from a single --rgba8888-texture override.
    // Readers accept both version numbers and dispatch per entry.
    bool anyV2 = false;
    for (const auto& t : textures) {
        if (t.payloadFormat != EQE1_PAYLOAD_ETC1) { anyV2 = true; break; }
    }
    uint16_t fileVersion = anyV2 ? EQE1_VERSION_V2 : EQE1_VERSION;
    if (!writeEQE1File(textures, rawEntries, outputPath, verbose, fileVersion)) {
        return false;
    }

    if (failCount > 0) {
        std::cout << "  (" << failCount << " files skipped)" << std::endl;
    }

    return true;
}

// ── Entry point ──────────────────────────────────────────────────────────────

static void printUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " --eq-path <path> --archive <name> --output <dir>\n"
              << "       " << argv0 << " --eq-path <path> --all --output <dir>\n"
              << "       " << argv0 << " --bmp-dir <path> [--bmp-dir <path2> ...] --output <dir> [--name <name>]\n"
              << "\nS3D archive mode:\n"
              << "  --eq-path <path>     Path to EverQuest client directory\n"
              << "  --archive <name>     Process single archive (e.g., qeynos2.s3d)\n"
              << "  --all                Process all .s3d files\n"
              << "\nBMP directory mode:\n"
              << "  --bmp-dir <path>     Read BMPs from directory (repeatable, mutually exclusive with --eq-path)\n"
              << "  --name <name>        Override output base name (default: directory name)\n"
              << "\nCommon:\n"
              << "  --output <dir>       Output directory for .eqe1 files\n"
              << "  --verbose            Print per-texture stats\n"
              << "  --payload-format <etc1|rgba8888|rgb565>\n"
              << "                       Per-entry payload format (default: etc1, byte-for-byte\n"
              << "                       identical to the original v1 output). rgba8888 emits\n"
              << "                       uncompressed RGBA8888 for every texture. rgb565 emits\n"
              << "                       RGB565 for opaque textures and RGBA4444 for alpha-\n"
              << "                       carrying ones. Both non-etc1 modes write v2 packs\n"
              << "                       (version 2) with no alpha companion entries.\n"
              << "  --rgba8888-texture <name>\n"
              << "                       Force ONE texture (by pack entry name, no extension)\n"
              << "                       to the uncompressed RGBA8888 payload while every other\n"
              << "                       entry keeps --payload-format. Repeatable. Use it when a\n"
              << "                       texture's alpha must be IN-BAND rather than in an ETC1\n"
              << "                       _alpha companion -- shaders with a single sampler\n"
              << "                       (particles) never see the companion. BMP-directory mode\n"
              << "                       only. Emits a v2 pack.\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string archiveName;
    std::string outputDir;
    std::vector<std::string> bmpDirs;
    std::string nameOverride;
    bool allArchives = false;
    bool verbose = false;
    std::string payloadFormatArg = "etc1";
    std::vector<std::string> rgba8888Names;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (arg == "--archive" && i + 1 < argc) {
            archiveName = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--bmp-dir" && i + 1 < argc) {
            bmpDirs.push_back(argv[++i]);
        } else if (arg == "--name" && i + 1 < argc) {
            nameOverride = argv[++i];
        } else if (arg == "--payload-format" && i + 1 < argc) {
            payloadFormatArg = argv[++i];
        } else if (arg == "--rgba8888-texture" && i + 1 < argc) {
            rgba8888Names.push_back(toLower(argv[++i]));
        } else if (arg == "--all") {
            allArchives = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    BuilderPayloadMode mode;
    if (payloadFormatArg == "etc1") {
        mode = BuilderPayloadMode::ETC1;
    } else if (payloadFormatArg == "rgba8888") {
        mode = BuilderPayloadMode::RGBA8888;
    } else if (payloadFormatArg == "rgb565") {
        mode = BuilderPayloadMode::RGB565;
    } else {
        std::cerr << "Error: unknown --payload-format '" << payloadFormatArg
                  << "' (expected etc1|rgba8888|rgb565)\n";
        return 1;
    }

    if (outputDir.empty()) {
        std::cerr << "Error: --output is required\n";
        printUsage(argv[0]);
        return 1;
    }

    // Validate mutually exclusive modes
    if (!bmpDirs.empty() && !eqPath.empty()) {
        std::cerr << "Error: --bmp-dir and --eq-path are mutually exclusive\n";
        printUsage(argv[0]);
        return 1;
    }

    fs::create_directories(outputDir);

    // ── BMP directory mode ──────────────────────────────────────────────────
    if (!bmpDirs.empty()) {
        for (const auto& bmpDir : bmpDirs) {
            if (!fs::exists(bmpDir) || !fs::is_directory(bmpDir)) {
                std::cerr << "Error: BMP directory does not exist or is not a directory: " << bmpDir << std::endl;
                return 1;
            }
        }

        for (const auto& dir : bmpDirs) {
            std::cout << "Processing BMP directory: " << dir << std::endl;
        }
        if (processBmpDirectories(bmpDirs, outputDir, nameOverride, mode, rgba8888Names, verbose)) {
            return 0;
        }
        return 1;
    }

    // ── S3D archive mode ────────────────────────────────────────────────────
    if (!rgba8888Names.empty()) {
        std::cerr << "Error: --rgba8888-texture is only supported in --bmp-dir mode\n";
        return 1;
    }
    if (eqPath.empty()) {
        std::cerr << "Error: --eq-path or --bmp-dir is required\n";
        printUsage(argv[0]);
        return 1;
    }
    if (!fs::exists(eqPath) || !fs::is_directory(eqPath)) {
        std::cerr << "Error: EQ path does not exist or is not a directory: " << eqPath << std::endl;
        return 1;
    }
    if (!allArchives && archiveName.empty()) {
        std::cerr << "Error: --archive or --all is required\n";
        printUsage(argv[0]);
        return 1;
    }

    // Collect archives to process
    std::vector<std::string> archives;
    if (allArchives) {
        for (const auto& entry : fs::directory_iterator(eqPath)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            std::string lower = toLower(name);
            if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".s3d") {
                archives.push_back(name);
            }
        }
        std::sort(archives.begin(), archives.end());
        std::cout << "Processing " << archives.size() << " S3D archives..." << std::endl;
    } else {
        archives.push_back(archiveName);
    }

    int success = 0, failed = 0;
    size_t totalOutputSize = 0;

    for (size_t i = 0; i < archives.size(); ++i) {
        std::cout << "\n[" << (i + 1) << "/" << archives.size() << "] " << archives[i] << std::endl;

        if (processArchive(eqPath, archives[i], outputDir, verbose, mode)) {
            success++;

            // Accumulate stats
            std::string baseName = toLower(archives[i]);
            if (baseName.size() > 4) baseName = baseName.substr(0, baseName.size() - 4);
            std::string outPath = outputDir + "/" + baseName + ".eqe1";
            if (fs::exists(outPath)) {
                totalOutputSize += fs::file_size(outPath);
            }
        } else {
            failed++;
        }
    }

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "  Archives: " << success << " succeeded, " << failed << " failed" << std::endl;
    std::cout << "  Total output: " << totalOutputSize / (1024 * 1024) << " MB" << std::endl;

    return failed > 0 ? 1 : 0;
}
