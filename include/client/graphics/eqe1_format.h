// EQE1 flat pack binary format — pre-compressed ETC1 texture container.
// Replaces S3D archives for GLES2 platforms (Mali 400, etc.).
// One .eqe1 file per S3D archive, containing ETC1-compressed textures
// with full mipmap chains, plus raw data entries (e.g., .wld files).
// Design: docs/plans/full_etc1_textures.md

#pragma once

#include <cstdint>
#include <algorithm>

namespace EQT { namespace Graphics {

// File magic: "EQE1" in little-endian
static constexpr uint32_t EQE1_MAGIC = 0x31455145;

static constexpr uint16_t EQE1_VERSION = 1;

// v2: per-entry payloadFormat field (repurposes the former padding byte —
// wire-compatible with v1, whose index entries always carry padding==0,
// which decodes as EQE1_PAYLOAD_ETC1). v2 packs may mix payload formats
// per-entry; readers dispatch on payloadFormat, not on the file version,
// so a v1 loader path only needs to accept both version numbers.
static constexpr uint16_t EQE1_VERSION_V2 = 2;

// Each index entry is exactly 48 bytes
static constexpr uint32_t EQE1_INDEX_ENTRY_SIZE = 48;

// Data entries are aligned to 16-byte boundaries
static constexpr uint32_t EQE1_DATA_ALIGNMENT = 16;

// Maximum texture name length (null-padded)
static constexpr uint32_t EQE1_NAME_SIZE = 32;

enum EQE1EntryType : uint8_t {
    EQE1_TYPE_ETC1_RGB  = 0,  // Opaque ETC1 texture (RGB only), or v2 payload w/o alpha companion
    EQE1_TYPE_ETC1_RGBA = 1,  // ETC1 texture with alpha (has companion _alpha entry)
    EQE1_TYPE_RAW_DATA  = 2,  // Non-texture data (e.g., .wld files)
};

// v2: payload pixel format carried by an index entry's data bytes.
// ETC1 (0) is the v1 format and the implicit value of a zero-initialized
// (v1) padding byte, so v1 packs need no migration.
enum EQE1PayloadFormat : uint8_t {
    EQE1_PAYLOAD_ETC1     = 0,  // Compressed ETC1 (glCompressedTexImage2D); companion-alpha model
    EQE1_PAYLOAD_RGB565   = 1,  // Uncompressed opaque, GL_RGB/GL_UNSIGNED_SHORT_5_6_5
    EQE1_PAYLOAD_RGBA4444 = 2,  // Uncompressed w/ alpha, GL_RGBA/GL_UNSIGNED_SHORT_4_4_4_4
    EQE1_PAYLOAD_RGBA8888 = 3,  // Uncompressed w/ alpha, GL_RGBA/GL_UNSIGNED_BYTE
};

#pragma pack(push, 1)

struct EQE1Header {
    uint32_t magic;         // EQE1_MAGIC
    uint16_t version;       // EQE1_VERSION
    uint16_t numEntries;    // Total entries (textures + alpha companions + raw data)
    uint32_t flags;         // Reserved, 0
    uint32_t reserved;      // Reserved, 0
};

struct EQE1IndexEntry {
    char     name[32];      // Lowercase, null-padded, no extension
    uint16_t width;         // Texture width in pixels (0 for RAW_DATA)
    uint16_t height;        // Texture height in pixels (0 for RAW_DATA)
    uint8_t  mipLevels;     // Number of mip levels (0 for RAW_DATA)
    uint8_t  hasAlpha;      // 1 if this texture needs alpha-test; for
                            // EQE1_PAYLOAD_ETC1 it also means a companion
                            // "{name}_alpha" entry exists. v2 payload formats
                            // (RGBA4444/RGBA8888) carry alpha in-band and
                            // build no companion even when hasAlpha==1.
    uint8_t  entryType;     // EQE1EntryType
    uint8_t  payloadFormat; // EQE1PayloadFormat (was "padding", always 0 in
                            // v1 files — 0 == EQE1_PAYLOAD_ETC1, so v1 packs
                            // decode correctly unmodified)
    uint32_t dataOffset;    // Byte offset from file start to data
    uint32_t dataSize;      // Total data bytes (all mip levels, or raw data size)
};

#pragma pack(pop)

static_assert(sizeof(EQE1Header) == 16, "EQE1Header must be 16 bytes");
static_assert(sizeof(EQE1IndexEntry) == 48, "EQE1IndexEntry must be 48 bytes");

// ETC1 block size: each 4x4 pixel block = 8 bytes.
// Returns the compressed data size for a single mip level.
inline uint32_t etc1DataSize(uint16_t width, uint16_t height) {
    uint32_t blocksW = (static_cast<uint32_t>(width) + 3) / 4;
    uint32_t blocksH = (static_cast<uint32_t>(height) + 3) / 4;
    return blocksW * blocksH * 8;
}

// Compute number of mip levels from base dimensions down to 1x1.
// A 256x256 texture gets 9 levels (256,128,64,32,16,8,4,2,1).
// GLES2 requires a complete chain to 1x1 for mipmap-complete textures.
// Sub-4x4 levels still use one ETC1 block (8 bytes) with padding.
inline uint8_t computeMipLevels(uint16_t width, uint16_t height) {
    uint8_t levels = 1;
    uint16_t w = width;
    uint16_t h = height;
    while (w > 1 || h > 1) {
        w = std::max<uint16_t>(1, w / 2);
        h = std::max<uint16_t>(1, h / 2);
        ++levels;
    }
    return levels;
}

// Total ETC1 data size for a full mipmap chain.
inline uint32_t etc1MipChainSize(uint16_t width, uint16_t height, uint8_t mipLevels) {
    uint32_t total = 0;
    uint16_t w = width;
    uint16_t h = height;
    for (uint8_t level = 0; level < mipLevels; ++level) {
        total += etc1DataSize(w, h);
        w = std::max<uint16_t>(1, w / 2);
        h = std::max<uint16_t>(1, h / 2);
    }
    return total;
}

// Align offset up to 16-byte boundary.
inline uint32_t alignTo16(uint32_t offset) {
    return (offset + 15) & ~15u;
}

// v2 uncompressed payloads: bytes-per-pixel for a given format.
// ETC1 has no fixed bpp (block-compressed); callers must use
// etc1DataSize/etc1MipChainSize for that format instead.
inline uint32_t payloadBytesPerPixel(EQE1PayloadFormat fmt) {
    switch (fmt) {
        case EQE1_PAYLOAD_RGB565:
        case EQE1_PAYLOAD_RGBA4444:
            return 2;
        case EQE1_PAYLOAD_RGBA8888:
            return 4;
        case EQE1_PAYLOAD_ETC1:
        default:
            return 0;
    }
}

// Uncompressed data size for one mip level of a v2 payload format.
inline uint32_t payloadDataSize(uint16_t width, uint16_t height, EQE1PayloadFormat fmt) {
    return static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * payloadBytesPerPixel(fmt);
}

// Total uncompressed bytes for a full mip chain of a v2 payload format.
inline uint32_t payloadMipChainSize(uint16_t width, uint16_t height, uint8_t mipLevels,
                                     EQE1PayloadFormat fmt) {
    uint32_t total = 0;
    uint16_t w = width;
    uint16_t h = height;
    for (uint8_t level = 0; level < mipLevels; ++level) {
        total += payloadDataSize(w, h, fmt);
        w = std::max<uint16_t>(1, w / 2);
        h = std::max<uint16_t>(1, h / 2);
    }
    return total;
}

}} // namespace EQT::Graphics
