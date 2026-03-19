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

// Each index entry is exactly 48 bytes
static constexpr uint32_t EQE1_INDEX_ENTRY_SIZE = 48;

// Data entries are aligned to 16-byte boundaries
static constexpr uint32_t EQE1_DATA_ALIGNMENT = 16;

// Maximum texture name length (null-padded)
static constexpr uint32_t EQE1_NAME_SIZE = 32;

enum EQE1EntryType : uint8_t {
    EQE1_TYPE_ETC1_RGB  = 0,  // Opaque ETC1 texture (RGB only)
    EQE1_TYPE_ETC1_RGBA = 1,  // ETC1 texture with alpha (has companion _alpha entry)
    EQE1_TYPE_RAW_DATA  = 2,  // Non-texture data (e.g., .wld files)
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
    uint8_t  hasAlpha;      // 1 if companion "{name}_alpha" entry exists
    uint8_t  entryType;     // EQE1EntryType
    uint8_t  padding;       // 0
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

// Compute number of mip levels from base dimensions down to 4x4 minimum.
// A 256x256 texture gets 7 levels (256,128,64,32,16,8,4).
// A 4x4 texture gets 1 level.
inline uint8_t computeMipLevels(uint16_t width, uint16_t height) {
    uint8_t levels = 1;
    uint16_t w = width;
    uint16_t h = height;
    while (w > 4 || h > 4) {
        w = std::max<uint16_t>(4, w / 2);
        h = std::max<uint16_t>(4, h / 2);
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
        w = std::max<uint16_t>(4, w / 2);
        h = std::max<uint16_t>(4, h / 2);
    }
    return total;
}

// Align offset up to 16-byte boundary.
inline uint32_t alignTo16(uint32_t offset) {
    return (offset + 15) & ~15u;
}

}} // namespace EQT::Graphics
