# Full ETC1 Texture Conversion — Flat Pack Architecture

## Problem

Entity textures currently decode from BMP/DDS to 32bpp RGBA on the GPU (4 bytes/texel). Zone textures use pre-built `.atlas` files with ETC1 compression (0.5 bytes/texel), but the atlas approach requires UV remapping and atlas-specific shaders, which causes visual artifacts on tiling textures (94% of zone geometry uses UV repeating).

## Solution

Convert **all** EQ textures (zone, object, character) to ETC1 offline and package them in a new flat pack container format (`.eqe1`). Each texture remains an individual GL texture — no atlas bin-packing, no UV remapping, no shader changes. Textures with alpha get dual ETC1 entries (RGB + alpha-as-luminance). `GL_REPEAT` works normally.

This replaces the current per-texture BMP decode pipeline with direct `glCompressedTexImage2D` upload from pre-compressed data.

### Memory Impact

| | 32bpp RGBA (current) | ETC1 (new) | Savings |
|---|---|---|---|
| Per 256×256 opaque | 256 KB | 32 KB | 8× |
| Per 256×256 alpha | 256 KB | 64 KB (dual) | 4× |
| 40 entities (~80 textures) | 30-40 MB | 3.5-5 MB | ~8× |
| Zone + obj textures | ~30 MB | ~4 MB | ~8× |
| Total GPU texture memory | ~65 MB | ~8 MB | ~8× |

### Texture Survey (from extracted S3Ds)

| Category | Count | Alpha | Dominant sizes |
|---|---|---|---|
| chr (characters) | 7,620 | 0 (0%) | 64×64, 64×32, 32×32 (98% ≤128×128) |
| obj (objects) | 1,120 | 41 (3.7%) | 256×256 (75%) |
| zone (geometry) | 961 | 14 (1.5%) | 256×256 (93%) |
| **Total** | **9,677** | **55 unique (0.6%)** | |

Alpha textures: trees, fire, grass, chains, rugs — all in obj/zone, zero in chr.

### Performance Impact (Beyond Memory)

- **No BMP decode at runtime**: Skip per-pixel ARGB→RGBA conversion, DDS decompression, box filter downsampling
- **No zlib inflate**: Flat pack is uncompressed — ETC1 data is already dense, deflate achieves <10% on it
- **Mali 400 L2**: 8× more textures fit in the 256 KB shared L2 cache simultaneously
- **Memory bandwidth**: ~1.5 MB/frame at 32bpp → ~192 KB/frame at ETC1 for 40 entities

## Flat Pack Format (.eqe1)

One `.eqe1` file per S3D archive. No compression — data is `mmap`-friendly and ready for direct GPU upload.

```
EQE1 File Layout
================

Header (16 bytes):
  magic:       uint32  "EQE1" (0x31455145)
  version:     uint16  1
  numEntries:  uint16  N
  flags:       uint32  reserved (0)
  reserved:    uint32  0

Index (N × 48 bytes, immediately after header):
  name:        char[32]   lowercase texture name, null-padded
  width:       uint16     texture width in pixels
  height:      uint16     texture height in pixels
  mipLevels:   uint8      number of mip levels (full chain down to 4×4)
  hasAlpha:    uint8      0 = opaque (RGB only), 1 = has alpha entry
  entryType:   uint8      0 = ETC1 texture, 2 = raw data (e.g., .wld)
  padding:     uint8      0
  dataOffset:  uint32     offset from file start to ETC1 data
  dataSize:    uint32     total bytes (all mip levels)

Entry types (determined by hasAlpha and name):
  type 0: ETC1 texture (RGB opaque)
  type 1: ETC1 texture (RGB + separate alpha entry)
  type 2: raw data (.wld files — not textures)

Data (concatenated, 16-byte aligned entries):
  [ETC1 blocks for entry 0: mip0, mip1, ..., mipN]  (full chain down to 4×4)
  [ETC1 blocks for entry 1: mip0, mip1, ..., mipN]
  ...

Alpha entries (for hasAlpha=1 textures):
  Stored as separate index entries named "{name}_alpha"
  R=G=B=alpha value, same dimensions and mip chain
  Runtime binds RGB entry as texture 0, alpha entry as texture 1
```

### Name table (version 3)

The 32-byte inline name field holds at most 31 characters + NUL. The lookup
side keys on the FULL name (`.bmp` stripped, lowercased — nothing else), so a
name that did not fit was silently truncated on write and never found on read:
soldunga's `gnome_stronghold_floor_transition` (33) stored as
`gnome_stronghold_floor_transiti` and FATAL-exited the zone load (B910,
filed as B100; 2026-09-21). The fix adds a v3 name table rather than canonicalizing both
sides to 31 chars, because the format cannot carry the full name any other way
and truncating on read would break every zone whose source names differ
beyond 31 chars. v1/v2 packs are unchanged and remain byte-identical; only a
pack with at least one name over 31 chars is written as v3:

```
v3 layout:
  Header:  version field = 3 (everything else identical to v1/v2)
  Index:   N × (48-byte v1 entry + 8-byte name tail)
    name tail:  uint32  nameOffset   file offset of the name in the table
                uint32  nameLength   byte length
                (a name that fits inline: nameOffset = 0, nameLength = 0,
                 the 32-byte field holds the name as in v1/v2)
  Name table:  the distinct long names, concatenated, immediately after the
    index (the builder writes it contiguous and NUL-free; each name once)
  Data:  identical to v1/v2 (16-byte-aligned entries); dataOffset values
    account for the index + table
```

`include/client/graphics/eqe1_format.h` is the normative definition
(`EQE1IndexEntryNameTail`, `indexEntryStride()`); the loader rejects a v3
name-table reference that is out of bounds or escapes the table, and a v1/v2
entry whose name field is not null-terminated (the truncation signature of a
pack that needs a v3 rebuild). `eqe1_builder` and
`entity_composite_builder` emit v3 when needed and stay v1/v2 otherwise;
`verify_eqe1` validates all three versions. **A v1/v2 pack is a LEGITIMATE
format** (the builder keeps a pack that way when every name fits the
31-char inline field — `willeq.eqe1` is one, v2), but it cannot hold a
>31-char name, so a zone whose textures need one dies in the client with
the B930 FATAL that names the pack and its version (issue #248). Auditing
a host's pack directory for exactly that:

```
for f in <dir>/*.eqe1; do ./build/bin/verify_eqe1 --require-v3 "$f"; done
```

prints the version on every line and exits non-zero (reporting `STALE`) on
any v1/v2 pack. Pinned by the `test_eqe1_verify_stale_pack` ctest test.

### File Mapping

```
EQ client dir (original):     ETC1 client dir (converted):
  qeynos2.s3d          →       qeynos2.eqe1
  qeynos2_obj.s3d      →       qeynos2_obj.eqe1
  qeynos2_chr.s3d      →       qeynos2_chr.eqe1
  global_chr.s3d       →       global_chr.eqe1
  gequip.s3d           →       gequip.eqe1
  ...
```

Non-texture files (`.wld`) are embedded in the `.eqe1` container with `entryType=2` (raw data). This preserves the 1:1 mapping of one S3D → one `.eqe1` and avoids loose files.

## Implementation

Detailed unit plans are in `refactor/E01_eqe1_format.md` through `refactor/E10_atlas_cleanup.md`.
Progress tracked in `refactor/progress.md` (Batch E).

### Phase E1 — Offline Tooling

| Unit | Description |
|------|-------------|
| E01 | EQE1 binary format definition + header structures (`include/client/graphics/eqe1_format.h`) |
| E02 | EQE1 builder tool — S3D → ETC1 flat pack converter (`tools/eqe1_builder.cpp`) |
| E03 | Batch conversion script + verification tool (`tools/convert_all_to_eqe1.sh`, `tools/verify_eqe1.cpp`) |

### Phase E2 — Runtime Loader

| Unit | Description |
|------|-------------|
| E04 | EQE1Loader class — file reader with O(1) name lookup (`include/client/graphics/eqe1_loader.h`) |
| E05 | Compressed upload path in ConstrainedTextureCache (`glCompressedTexImage2D` + `GL_REPEAT`) |

### Phase E3 — Integration

| Unit | Description |
|------|-------------|
| E06 | Configuration — `--eqe1-path` CLI flag + config JSON field |
| E07 | Zone load wiring — open EQE1 files in `loadZoneSequential()`, register with cache |
| E08 | Entity texture wiring — chr/obj/gequip EQE1 loading during entity prep |

### Phase E4 — Validation and Cleanup

| Unit | Description |
|------|-------------|
| E09 | End-to-end validation on Orange Pi (memory, visual, performance, stability) |
| E10 | Remove zone atlas UV workaround path (optional) |

### Execution Order

```
E01 (format header)           — no dependencies
  ↓
E02 (builder tool)            — depends on E01
  ↓
E03 (batch script + verify)   — depends on E02
  ↓
E04 (runtime loader)          — depends on E01 (can parallel with E02/E03)
  ↓
E05 (cache integration)       — depends on E04
  ↓
E06 (configuration)           — depends on E01 (can parallel with E02-E05)
  ↓
E07 (zone load wiring)        — depends on E04, E05, E06
  ↓
E08 (entity wiring)           — depends on E05, E07
  ↓
E09 (validation)              — depends on E03, E07, E08
  ↓
E10 (atlas cleanup)           — depends on E09, optional
```

E01, E04, and E06 can be developed in parallel (no dependencies between them).
E02/E03 (offline tooling) and E04/E05 (runtime) are independent tracks.

## Relationship to Existing Systems

- **Zone atlas system** (`.atlas` files, `TextureAtlas` class): Orthogonal. EQE1 replaces the BMP decode path, not the atlas packing path. Both can coexist — use EQE1 for individual textures, keep `.atlas` for future draw call batching if desired.
- **Entity atlas plan** (`docs/future/entity_texture_atlas.md`): Superseded by this plan for the memory savings goal. Entity atlas packing for draw call batching remains a potential future optimization.
- **Constrained texture cache**: Enhanced, not replaced. The cache gains a compressed upload path but keeps the same LRU budget, eviction, and `/pmem` tracking.
- **GLES2 driver**: Primary target. `GL_ETC1_RGB8_OES` is natively supported on Mali 400. Desktop GL platforms keep using the existing S3D/BMP decode path — EQE1 is a GLES2-only optimization.

## Design Decisions

1. **WLD file handling**: Embedded in `.eqe1` with `entryType=2` (raw data). Preserves 1:1 S3D → EQE1 mapping, no loose files.

2. **Desktop GL fallback**: GLES2-only feature. Desktop builds continue using S3D/BMP decode. The memory pressure that motivates ETC1 doesn't exist on desktop (8+ GB RAM).

3. **Mipmap levels**: Full chain down to 4×4 (the ETC1 block size minimum). Mali 400 supports hardware mipmapping with GLES2. A 256×256 texture gets 7 levels; a 64×32 gets 4 levels. ~33% overhead, eliminates distance shimmer.

4. **Data alignment**: 16-byte aligned entries. Minimal padding waste (up to 15 bytes per entry), clean for GL drivers and SIMD. Avoids the ~38MB worst-case waste of 4KB page alignment across 9,677 entries.
