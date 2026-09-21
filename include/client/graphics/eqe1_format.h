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

// v3 (B100): full-length entry names, for entry names longer than 31
// characters.
//
// The v1/v2 index entry carries its name in a fixed 32-byte field
// (31 chars + NUL), but EQ asset names reach 32+ characters (soldunga's
// gnome_stronghold_floor_transition is 33) and the builder silently
// truncated them at 31, so the client's full-name lookup missed and the
// all-or-nothing rule FATAL-exited the client. v3 keeps the 48-byte v1/v2
// entry as its first 48 bytes and appends a {nameOffset, nameLength} tail
// (56 bytes total). An entry whose name fits in 31 chars keeps it inline
// exactly as v1/v2 wrote it and carries nameOffset==0/nameLength==0; a
// longer name goes into a NAME TABLE held between the index and the data
// section (contiguous, each name stored once, NUL-free), and that entry's
// 32-byte name field is zeroed, nameOffset holds the name's absolute file
// offset and nameLength its length in bytes. The table sits after the index
// precisely because its offsets are computable before the data section is
// laid out. v1/v2 packs are byte-identical to what the builder always wrote
// — a pack whose every name fits in 31 chars stays v1/v2 (the pinned
// willeq.eqe1 included). Readers accept v1/v2/v3; a v1/v2 entry whose name
// is NOT null-terminated within the 32-byte field is a truncated name and
// the pack is REJECTED (the old read path silently keyed the truncated name
// into the lookup map, which is how a truncation could hide for years).
static constexpr uint16_t EQE1_VERSION_V3 = 3;

// v3 index entry: the 48-byte v1/v2 entry followed by the name-table tail.
static constexpr uint32_t EQE1_INDEX_ENTRY_SIZE_V3 = 56;

// Each index entry is exactly 48 bytes
static constexpr uint32_t EQE1_INDEX_ENTRY_SIZE = 48;

// Data entries are aligned to 16-byte boundaries
static constexpr uint32_t EQE1_DATA_ALIGNMENT = 16;

// Maximum texture name length (null-padded)
static constexpr uint32_t EQE1_NAME_SIZE = 32;

// v3: the maximum name length that fits inline in the 32-byte field.
// Names of EQE1_NAME_SIZE - 1 (31) characters or fewer stay inline exactly
// as v1/v2 wrote them; anything longer goes to the name table.
static constexpr uint32_t EQE1_INLINE_NAME_LIMIT = 31;

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
    uint16_t version;       // EQE1_VERSION (1), _V2 (2) or _V3 (3)
    uint16_t numEntries;    // Total entries (textures + alpha companions + raw data)
    uint32_t flags;         // Reserved, 0
    uint32_t reserved;      // Reserved, 0
};

// The v1/v2 index entry, and the first 48 bytes of a v3 one. v3 appends
// EQE1IndexEntryNameTail. Names up to EQE1_INLINE_NAME_LIMIT characters
// store inline in `name` in every version; a v3 entry with a longer name
// zeroes `name` and carries the name-table reference in the tail.
struct EQE1IndexEntry {
    char     name[32];      // Lowercase, null-padded, no extension (v1/v2: the
                            // whole name; v3: inline iff len <= 31, else zeroed
                            // with the name in the table)
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

// v3 tail: {nameOffset, nameLength} — both zero when `name` is inline.
// nameOffset is the absolute file offset of this name in the name table
// (which sits between the index and the data section, contiguous, NUL-free);
// nameLength its length in bytes. uint32_t so a table can outgrow 65,535
// names.
struct EQE1IndexEntryNameTail {
    uint32_t nameOffset;    // 0 = name stored inline in EQE1IndexEntry::name
    uint32_t nameLength;    // 0 = inline; else length of the table-resident name
};

#pragma pack(pop)

static_assert(sizeof(EQE1Header) == 16, "EQE1Header must be 16 bytes");
static_assert(sizeof(EQE1IndexEntry) == 48, "EQE1IndexEntry must be 48 bytes");
static_assert(sizeof(EQE1IndexEntryNameTail) == 8,
              "EQE1IndexEntryNameTail must be 8 bytes");
static_assert(EQE1_INDEX_ENTRY_SIZE_V3 == sizeof(EQE1IndexEntry) + sizeof(EQE1IndexEntryNameTail),
              "v3 entry size must be the v1/v2 entry plus the name tail");

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

// v3: byte stride of one index entry for a pack of the given version.
// v3 packs carry the 8-byte name tail per entry; v1/v2 do not.
inline uint32_t indexEntryStride(uint16_t version) {
    return version == EQE1_VERSION_V3 ? EQE1_INDEX_ENTRY_SIZE_V3 : EQE1_INDEX_ENTRY_SIZE;
}

// v3: does this entry carry its name inline (the v1/v2 way)?
inline bool eqe1NameInline(uint16_t version, const EQE1IndexEntryNameTail* tail) {
    if (version != EQE1_VERSION_V3) return true;
    return tail->nameOffset == 0 && tail->nameLength == 0;
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
