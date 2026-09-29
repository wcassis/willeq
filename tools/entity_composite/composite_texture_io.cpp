// tools/entity_composite/composite_texture_io.cpp -- BMP decode/encode, shelf packing, the
// seam gutter, ETC1 compression, mip chains and the .eqe1 writer.
// Moved from tools/entity_composite_builder.cpp by T385 (board B167).
// Bodies are verbatim; the only edits are the ones the split forces --
// `static` dropped from the functions that now cross a TU boundary (they
// are declared in composite_builder.h) and the whole file wrapped in
// namespace ecb.

#include "entity_composite/composite_builder.h"

namespace ecb {

// ── Helpers ──────────────────────────────────────────────────────────────────

 std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

 std::string toUpper(std::string s) {
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

// P48: applyPaletteMask defaults false, preserving the pre-P48 decode
// byte-for-byte for every call site that doesn't pass true — mirrors
// eqe1_builder.cpp's "EQ convention: palette index 0 is the transparent
// color" decode (tools/eqe1_builder.cpp ~line 88), applied here ONLY for
// placements this unit has determined are WLD-masked (part->textureMasked()),
// not unconditionally the way eqe1_builder.cpp applies it to every 8bpp
// texture it processes. This keeps unmasked composite sheets' pixel data
// generation identical to before this unit (and since writeBMP() never reads
// the alpha channel, and writeEQE1() only emits an alpha companion when the
// sheet-level masked flag is set, unmasked sheets' on-disk output is
// byte-identical regardless of this flag's plumbing).
 bool decodeBMP(const std::vector<char>& data,
                      std::vector<uint8_t>& outRGBA,
                      int& outWidth, int& outHeight,
                      bool applyPaletteMask) {
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
                outRGBA[outIdx + 3] = (applyPaletteMask && idx == 0) ? 0 : 255;
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

// (moved to entity_composite/composite_builder.h)

 bool writeBMP(const std::string& path, const std::vector<uint8_t>& rgba,
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

 bool shelfPack(std::vector<PlacedTexture>& textures, int maxSheetSize,
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
 void writeSeamInset(std::vector<uint8_t>& sheetRGBA, int sheetW, int /*sheetH*/,
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

// P48: alpha-as-grayscale-RGB companion encode, mirroring
// tools/eqe1_builder.cpp's encodeETC1_Alpha exactly (same technique the zone
// ETC1 companion convention this unit is asked to mirror already uses) — the
// source alpha channel is replicated into R=G=B and encoded as an opaque
// ETC1_RGB block; entity_renderer.cpp reads it back via
// ConstrainedTextureCache::getAlphaTexture() and samples its R channel.
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
                                              uint8_t& outMipLevels, bool isAlpha = false) {
    outMipLevels = computeMipLevels(static_cast<uint16_t>(width),
                                     static_cast<uint16_t>(height));
    std::vector<uint8_t> result;

    auto level0 = isAlpha ? encodeETC1_Alpha(rgba, width, height)
                          : encodeETC1_RGB(rgba, width, height);
    result.insert(result.end(), level0.begin(), level0.end());

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

// P48: hasAlpha defaults false, keeping the pre-P48 single-entry, hasAlpha=0,
// EQE1_TYPE_ETC1_RGB output byte-for-byte identical for every unmasked sheet.
// When true (sheet-level "any placement was WLD-masked"), this mirrors the
// zone/per-part ETC1 companion convention (tools/eqe1_builder.cpp
// writeEQE1File): the main entry becomes EQE1_TYPE_ETC1_RGBA/hasAlpha=1, and a
// second index entry named "{baseName}_alpha" (EQE1_TYPE_ETC1_RGB, grayscale
// R=G=B=alpha, palette-0 decoded to 0 upstream in decodeBMP) immediately
// follows it — the exact layout ConstrainedTextureCache's existing ETC1 upload
// path (constrained_texture_cache.cpp, "companion" handling, unchanged by this
// unit) already knows how to find and bind on texture unit 1.
 bool writeEQE1(const std::string& outputPath,
                       const std::vector<uint8_t>& sheetRGBA,
                       int sheetW, int sheetH,
                       bool hasAlpha) {
    uint8_t mipLevels = 0;
    auto etc1Data = generateMipChain(sheetRGBA.data(), sheetW, sheetH, mipLevels, false);
    if (etc1Data.empty()) return false;

    uint8_t alphaMipLevels = 0;
    std::vector<uint8_t> alphaEtc1Data;
    if (hasAlpha) {
        alphaEtc1Data = generateMipChain(sheetRGBA.data(), sheetW, sheetH, alphaMipLevels, true);
        if (alphaEtc1Data.empty()) {
            std::cerr << "  WARNING: alpha companion encode failed for " << outputPath
                      << " — writing opaque-only EQE1 instead" << std::endl;
            hasAlpha = false;
        }
    }

    std::string baseName = fs::path(outputPath).stem().string();
    std::string mainName = baseName;

    // B910 (filed as B100): sheet names are pack stems and can exceed the 32-byte inline
    // name field (a truncated one silently broke the name-to-pack round
    // trip the same way eqe1_builder's truncation broke soldunga). A pack
    // with a name longer than the inline limit is written as v3: the long
    // name moves to a name table between the index and the data section
    // (tools/eqe1_builder.cpp writeEQE1File is the canonical example; the
    // layout is eqe1_format.h). Every shipped sheet name fits, so this
    // branch keeps the v1 output byte-for-byte for them.
    std::string alphaName;
    if (hasAlpha) alphaName = baseName + "_alpha";
    const bool needsNameTable =
        mainName.size() > EQE1_INLINE_NAME_LIMIT ||
        (!alphaName.empty() && alphaName.size() > EQE1_INLINE_NAME_LIMIT);
    const uint16_t version = needsNameTable ? EQE1_VERSION_V3 : EQE1_VERSION;
    const uint32_t stride = indexEntryStride(version);
    uint16_t numEntries = hasAlpha ? 2 : 1;
    uint32_t indexEnd = sizeof(EQE1Header) + numEntries * stride;
    // Pre-compute the v3 name-table size so the data offsets account for it
    // (the table is written between the index and the data section). assignName
    // below appends in the same main-then-alpha order, so the offsets line up.
    uint32_t nameTableSize = 0;
    if (version == EQE1_VERSION_V3) {
        if (mainName.size() > EQE1_INLINE_NAME_LIMIT) nameTableSize += mainName.size();
        if (!alphaName.empty() && alphaName.size() > EQE1_INLINE_NAME_LIMIT)
            nameTableSize += alphaName.size();
    }
    uint32_t dataOff = alignTo16(indexEnd + nameTableSize);

    EQE1Header header = {};
    header.magic = EQE1_MAGIC;
    header.version = version;
    header.numEntries = numEntries;

    // One or two names, each stored at most once in the table.
    std::string nameTable;
    auto assignName = [&](EQE1IndexEntry& idx, EQE1IndexEntryNameTail& tail,
                          const std::string& name) {
        tail = {};
        if (name.size() <= EQE1_INLINE_NAME_LIMIT) {
            std::strncpy(idx.name, name.c_str(), EQE1_NAME_SIZE - 1);
        } else {
            idx.name[0] = '\0';
            tail.nameOffset = indexEnd + nameTable.size();
            tail.nameLength = static_cast<uint32_t>(name.size());
            nameTable += name;
        }
    };

    EQE1IndexEntry idx = {};
    EQE1IndexEntryNameTail mainTail = {};
    assignName(idx, mainTail, mainName);
    idx.width = static_cast<uint16_t>(sheetW);
    idx.height = static_cast<uint16_t>(sheetH);
    idx.mipLevels = mipLevels;
    idx.hasAlpha = hasAlpha ? 1 : 0;
    idx.entryType = hasAlpha ? EQE1_TYPE_ETC1_RGBA : EQE1_TYPE_ETC1_RGB;
    idx.dataOffset = dataOff;
    idx.dataSize = static_cast<uint32_t>(etc1Data.size());

    EQE1IndexEntry alphaIdx = {};
    EQE1IndexEntryNameTail alphaTail = {};
    uint32_t alphaDataOff = 0;
    if (hasAlpha) {
        alphaDataOff = alignTo16(dataOff + idx.dataSize);
        assignName(alphaIdx, alphaTail, alphaName);
        alphaIdx.width = static_cast<uint16_t>(sheetW);
        alphaIdx.height = static_cast<uint16_t>(sheetH);
        alphaIdx.mipLevels = alphaMipLevels;
        alphaIdx.hasAlpha = 0;
        alphaIdx.entryType = EQE1_TYPE_ETC1_RGB;  // alpha stored as grayscale RGB
        alphaIdx.dataOffset = alphaDataOff;
        alphaIdx.dataSize = static_cast<uint32_t>(alphaEtc1Data.size());
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&idx), sizeof(idx));
    if (version == EQE1_VERSION_V3) {
        out.write(reinterpret_cast<const char*>(&mainTail),
                  sizeof(EQE1IndexEntryNameTail));
    }
    if (hasAlpha) {
        out.write(reinterpret_cast<const char*>(&alphaIdx), sizeof(alphaIdx));
        if (version == EQE1_VERSION_V3) {
            out.write(reinterpret_cast<const char*>(&alphaTail),
                      sizeof(EQE1IndexEntryNameTail));
        }
    }
    // v3: name table between the index and the data section (dataOff already
    // accounts for the extended entries via the v3 stride).
    if (version == EQE1_VERSION_V3) {
        out.write(nameTable.data(), static_cast<std::streamsize>(nameTable.size()));
    }

    auto padTo = [&out](uint32_t target) {
        auto currentPos = static_cast<uint32_t>(out.tellp());
        if (currentPos < target) {
            std::vector<uint8_t> padding(target - currentPos, 0);
            out.write(reinterpret_cast<const char*>(padding.data()), padding.size());
        }
    };

    padTo(dataOff);
    out.write(reinterpret_cast<const char*>(etc1Data.data()), etc1Data.size());
    if (hasAlpha) {
        padTo(alphaDataOff);
        out.write(reinterpret_cast<const char*>(alphaEtc1Data.data()), alphaEtc1Data.size());
    }
    out.close();

    std::cout << "    EQE1: " << outputPath << " (v" << version << ", "
              << fs::file_size(outputPath) / 1024 << " KB, "
              << sheetW << "x" << sheetH << ", "
              << (int)mipLevels << " mips"
              << (hasAlpha ? ", +alpha companion" : "") << ")" << std::endl;
    return true;
}
#endif // EQT_HAS_ETC2COMP

}  // namespace ecb
