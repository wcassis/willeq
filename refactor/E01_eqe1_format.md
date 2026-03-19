# E01: EQE1 Binary Format Definition + Header Structures

## Overview

Define the EQE1 flat pack binary format as C++ header structures. This is the foundation
for both the builder tool (E02) and the runtime loader (E04). No executable code — just
the format definition and constants.

## Prerequisites

- None. This is the first unit in Batch E.

## Key Decisions

- EQE1 output directory: `/mnt/projects/EverQuest_ETC1`
- Texture names stored **without** `.bmp` extension (e.g., `humhe0001` not `humhe0001.bmp`)
- DDS-in-BMP files keep the original name (minus `.bmp`) — name is the lookup key
- Archives with zero textures still produce `.eqe1` files (RAW_DATA entries only)

## Plan

### Steps

1. Create `include/client/graphics/eqe1_format.h` with the following structures:

   a. **File magic and version constants**:
      - `EQE1_MAGIC = 0x31455145` ("EQE1" little-endian)
      - `EQE1_VERSION = 1`
      - `EQE1_INDEX_ENTRY_SIZE = 48`
      - `EQE1_DATA_ALIGNMENT = 16` (byte alignment for entry data)

   b. **EQE1Header** (16 bytes, packed):
      ```
      uint32_t magic;       // EQE1_MAGIC
      uint16_t version;     // EQE1_VERSION
      uint16_t numEntries;  // total entries (textures + raw data)
      uint32_t flags;       // reserved, 0
      uint32_t reserved;    // 0
      ```

   c. **EQE1EntryType** enum:
      ```
      ETC1_RGB    = 0,  // Opaque ETC1 texture (RGB only)
      ETC1_RGBA   = 1,  // ETC1 texture with alpha (has companion _alpha entry)
      RAW_DATA    = 2,  // Non-texture data (e.g., .wld files)
      ```

   d. **EQE1IndexEntry** (48 bytes, packed):
      ```
      char     name[32];     // lowercase, null-padded
      uint16_t width;        // texture width (0 for RAW_DATA)
      uint16_t height;       // texture height (0 for RAW_DATA)
      uint8_t  mipLevels;    // number of mip levels (0 for RAW_DATA)
      uint8_t  hasAlpha;     // 1 if companion "{name}_alpha" entry exists
      uint8_t  entryType;    // EQE1EntryType
      uint8_t  padding;      // 0
      uint32_t dataOffset;   // byte offset from file start to data
      uint32_t dataSize;     // total data bytes (all mip levels)
      ```
      Verify: 32 + 2 + 2 + 1 + 1 + 1 + 1 + 4 + 4 = 48 bytes.

   e. **Helper functions** (inline, header-only):
      - `uint32_t etc1DataSize(uint16_t width, uint16_t height)`:
        Returns `((width+3)/4) * ((height+3)/4) * 8` (ETC1 block size calculation)
      - `uint8_t computeMipLevels(uint16_t width, uint16_t height)`:
        Returns count of levels from (width,height) down to 4×4 minimum.
        Formula: `floor(log2(min(width,height)/4)) + 1`, minimum 1.
      - `uint32_t etc1MipChainSize(uint16_t width, uint16_t height, uint8_t mipLevels)`:
        Returns total bytes for all mip levels of an ETC1 texture.
        Sums `etc1DataSize(w >> level, h >> level)` for each level, clamping to min 4×4.
      - `uint32_t alignTo16(uint32_t offset)`:
        Returns `(offset + 15) & ~15u`

2. Add `static_assert` checks:
   - `static_assert(sizeof(EQE1Header) == 16)`
   - `static_assert(sizeof(EQE1IndexEntry) == 48)`

3. Add `#pragma pack(push, 1)` / `#pragma pack(pop)` around the structs to ensure
   binary layout matches file format on all platforms (ARM and x86).

4. Add brief doc comment at top of header explaining the format purpose and referencing
   `docs/plans/full_etc1_textures.md` for the full design.

## Acceptance Criteria

- [ ] `include/client/graphics/eqe1_format.h` exists with all structures defined
- [ ] `static_assert` confirms EQE1Header is 16 bytes
- [ ] `static_assert` confirms EQE1IndexEntry is 48 bytes
- [ ] `etc1DataSize(256, 256)` returns 32768 (32 KB)
- [ ] `etc1DataSize(64, 32)` returns 1024 (1 KB)
- [ ] `etc1DataSize(4, 4)` returns 8 (one block)
- [ ] `computeMipLevels(256, 256)` returns 7 (256→128→64→32→16→8→4)
- [ ] `computeMipLevels(64, 32)` returns 5 (64×32→32×16→16×8→8×4→4×4)
- [ ] `computeMipLevels(4, 4)` returns 1 (already at minimum)
- [ ] `alignTo16(0)` returns 0, `alignTo16(1)` returns 16, `alignTo16(16)` returns 16
- [ ] Header compiles clean on both x86 (host) and ARM (cross-compile) with no warnings
- [ ] No executable code — header only, no .cpp file needed

## Review

All steps completed as planned. One correction to acceptance criteria:
`computeMipLevels(64, 32)` returns 5, not 4. The `while (w > 4 || h > 4)` loop
continues until **both** dimensions reach 4: 64×32 → 32×16 → 16×8 → 8×4 → 4×4 = 5 levels.

All static_asserts pass. All helper function assertions verified via standalone test.
Header compiles clean with `-std=c++17` on x86. No .cpp file needed.
