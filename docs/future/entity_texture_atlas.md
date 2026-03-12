# Entity Texture Atlas — Offline ETC1 Compression for Character/Equipment Textures

## Problem Statement

Entity and equipment texture loading is currently **disabled** (all "Path B" unconstrained loading commented out as of commit `feb3c90`). Character models and equipment render as gray untextured placeholders. This was intentional — the old loading code had critical problems:

1. **No memory management**: Textures loaded via `buildTexturedMesh()` and `loadTextureFromBMP()` bypassed the `ConstrainedTextureCache` entirely — no LRU eviction, no budget enforcement, invisible in `/pmem` stats
2. **32bpp RGBA on GPU**: All entity textures decoded from DDS/BMP to 32-bit ARGB, converted to RGBA, uploaded uncompressed. A 256×256 texture = 256 KB on GPU
3. **No ETC1 compression**: Zone textures use ETC1 (0.5 bytes/texel via pre-built atlases), but entity textures were always 4 bytes/texel — 8× more memory and bandwidth
4. **Duplicated work**: Multiple code paths loaded the same textures independently (race loader, equipment loader, variant loader, mesh builder all had separate texture loading)
5. **No caching across entities**: Same race/equipment textures decoded and uploaded per entity instance rather than shared

On Orange Pi One (512MB shared RAM, Mali 400 with 256 KB L2 cache), 40 visible entities at 32bpp = 30-40 MB of untracked texture memory, consuming 50-60% of the 64 MB constrained cache budget and causing silent OOM.

### Currently Disabled Code Locations

| File | What's Disabled |
|------|----------------|
| `src/client/graphics/eq/race_model_loader.cpp:339-342` | `buildTexturedMesh()` for character bodies — falls back to `buildColoredMesh()` (gray) |
| `src/client/graphics/eq/animated_mesh_creation.cpp:439-475` | Equipment texture variant overrides — 173 lines commented out, no armor color variants |
| `src/client/graphics/eq/mesh_building.cpp:238-271` | Texture loading at mesh build time — 34 lines commented, meshes built without textures |
| `src/client/graphics/eq/equipment_model_loader.cpp:558-618` | Equipment mesh texture loading — 60 lines commented, equipment untextured |
| `src/client/graphics/eq/equipment_model_loader.cpp:650-662` | Texture reference cleanup — disabled, ref counting broken |
| `src/client/graphics/eq/zone_geometry.cpp:261-271` | Fallback texture loading — returns nullptr immediately, no unconstrained fallback |

## Solution: Offline Entity Texture Atlases

Pre-compress all character and equipment textures into ETC1 atlas files offline, using the same architecture as `zone_atlas_builder`. The client loads pre-built `.atlas` files at runtime — no DDS/BMP decoding, no runtime compression, no unconstrained memory paths.

### Memory Impact

| | 32bpp (old, disabled) | ETC1 Atlas (new) | Savings |
|---|---|---|---|
| Per 256×256 texture | 256 KB | 32 KB | 8× |
| 40 entities (~80 textures) | 30-40 MB | 3.5-5 MB | ~8× |
| Cache budget used | 50-60% | ~8% | Massive headroom |
| Mali 400 L2 cache fit | 1 texture | 8 textures | 8× fewer cache misses |
| Memory bandwidth per texel | 4 bytes | 0.5 bytes | 8× less bus traffic |

### Performance Impact (Runtime, Not Just Load-Time)

Mali 400 is a tile-based renderer with a 256 KB shared L2 cache. Entity texture format directly affects frame rendering time:

- **Texture cache thrashing**: At 32bpp, one 256×256 texture fills the entire L2. Every entity switch evicts the previous entity's texels. At ETC1, 8 entity textures fit simultaneously.
- **Memory bandwidth**: Mali 400 shares its memory bus with the CPU (~1.5-2 GB/s practical). With 40 entities, bilinear-filtered texture sampling reads ~1.5 MB/frame at 32bpp vs ~192 KB at ETC1.
- **Load time**: Pre-compressed atlas pages load via `glCompressedTexImage2D` — no decode step, no format conversion, no per-pixel ARGB→RGBA shuffle.

## Architecture

### Existing Infrastructure to Reuse

The `zone_atlas_builder` tool and `TextureAtlas` runtime loader already implement everything needed:

| Component | Existing Implementation | Reuse For Entity Atlases |
|-----------|------------------------|--------------------------|
| ETC1 compression | `etc2comp` (FetchContent'd by CMake) | Same library, same API |
| Atlas binary format | `.atlas` files (magic `EQAT`, pages + tiles) | Same format, same reader |
| Bin-packing | 2048×2048 pages with rectangular tile packing | Same algorithm |
| Mipmaps | v2 mipmap chain in atlas format | Same approach |
| Alpha handling | Dual-page (RGB ETC1 + alpha-as-luminance ETC1) | Same approach |
| Runtime loading | `TextureAtlas::preloadFromFile()` | Same class |
| UV remapping | Zone geometry remaps UVs to atlas tile space | Same transform for entity meshes |
| GPU upload | `glCompressedTexImage2D(GL_ETC1_RGB8_OES)` | Same GL call |
| Cache integration | Atlas pages managed by constrained cache | Same budget/eviction |

### Input Archives

EQ Titanium character and equipment textures live in these S3D archives:

**Character models** (race/gender body meshes + skin textures):
- `global_chr.s3d` — shared character data
- Per-race: `hum_chr.s3d`, `elf_chr.s3d`, `dwf_chr.s3d`, `hie_chr.s3d`, `daf_chr.s3d`, `haf_chr.s3d`, `gnm_chr.s3d`, `trk_chr.s3d`, `ogr_chr.s3d`, `ikr_chr.s3d`, `vah_chr.s3d`, `frg_chr.s3d`, `ske_chr.s3d`, etc.
- Texture naming: `{race}{gender}{variant}{part}{id}.bmp` (e.g., `humhe0001.bmp` = human head variant 00, texture 01)

**Equipment models** (weapons, armor, shields):
- `gequip.s3d`, `gequip2.s3d`, `gequip3.s3d` — 700+ models
- Texture naming: `{modelprefix}{id}.bmp` or `.dds`

**Armor texture variants** (appearance customization):
- Equipment textures have variant suffixes that change based on equipped armor material
- Example: `clkhe0101.bmp` vs `clkhe0201.bmp` — same helm, different armor color
- These variants are what the disabled code in `animated_mesh_creation.cpp:439-475` was trying to apply

### Output Structure

```
data/entity_atlases/
├── global_chr.atlas          # Shared character textures
├── hum_chr.atlas             # Human race textures (all gender/variant combos)
├── elf_chr.atlas             # Elf race textures
├── dwf_chr.atlas             # Dwarf race textures
├── ...                       # One per race archive
├── gequip.atlas              # Equipment textures (weapons, armor)
├── gequip2.atlas             # Equipment textures batch 2
└── gequip3.atlas             # Equipment textures batch 3
```

Discovery: `--entity-atlas-path <dir>` CLI flag or config key, matching the existing `--atlas-path` pattern for zone atlases.

### Binary Format

Identical to zone atlas format (`.atlas`, magic `EQAT`, v2 with mipmaps):

```
Header:
  magic: 0x54415145 ("EQAT")
  version: 2
  pageSize: 2048
  numPages: N
  numTiles: M

Per page (N entries):
  width, height, dataOffset, dataSize, mipLevels

Per tile (M entries):
  name: lowercase texture name (e.g., "humhe0001")
  pageIndex: which atlas page
  uvOffset: (u0, v0) in page
  uvScale: (uScale, vScale) in page
  alphaPageIndex: -1 if opaque, else index of alpha page
  alphaTileIndex: -1 if opaque, else tile index in alpha page

Compressed data:
  ETC1 blocks per page, including mipmap chain
```

Tile lookup is by **original texture name** (lowercase), so the runtime can look up `humhe0001` and get back the atlas page + UV rect — exactly how zone tile lookup works.

## Work Chunks

### Chunk 1: Entity Atlas Builder Tool

**Goal**: Offline tool that extracts textures from character/equipment S3D archives and packs them into ETC1-compressed `.atlas` files.

**Scope**:
- New `tools/entity_atlas_builder.cpp` (~800-1000 lines, modeled on `tools/zone_atlas_builder.cpp`)
- CLI: `entity_atlas_builder --eq-path <path> --archive <name> --output <dir>` or `--all`
- For each input S3D archive:
  1. Open archive via `PFSLoader` (existing S3D reader)
  2. Extract all texture files (BMP and DDS entries)
  3. Decode to RGBA pixels (reuse existing `DDSDecoder` and BMP decode from `race_model_loader.cpp`)
  4. Bin-pack into 2048×2048 atlas pages (reuse zone atlas packing algorithm)
  5. Separate opaque vs alpha textures — opaque get RGB-only ETC1, alpha get dual-page
  6. ETC1 compress each page via `etc2comp`
  7. Generate mipmap chain (box filter downsample, ETC1 compress each level)
  8. Write `.atlas` binary file with per-tile lookup table keyed by original texture name
- Add to CMakeLists.txt alongside `zone_atlas_builder`

**Key files to reference**:
- `tools/zone_atlas_builder.cpp` — primary template, copy and adapt
- `src/client/graphics/eq/race_model_loader.cpp:701-822` — DDS/BMP decode logic to extract
- `include/client/graphics/texture_atlas.h` — atlas binary format definition

**Deliverable**: Running `entity_atlas_builder --eq-path /path/to/EQ --all --output data/entity_atlases/` produces atlas files for all character and equipment archives.

### Chunk 2: Batch Generation Script

**Goal**: Shell script to generate all entity atlases in one command, matching `tools/generate_all_surface_maps.sh` pattern.

**Scope**:
- New `tools/generate_all_entity_atlases.sh`
- Iterates all character race archives + gequip archives
- Calls `entity_atlas_builder` per archive
- Reports total page count, total size, per-archive stats
- Output to `data/entity_atlases/` (or configurable)

**Deliverable**: One-command atlas generation for all entity textures.

### Chunk 3: Runtime Atlas Loading for Race Models

**Goal**: Load character model textures from pre-built entity atlases instead of the disabled Path B code. Re-enable textured character rendering.

**Scope**:
- Add `entityAtlasPath_` config/CLI option to `IrrlichtRenderer` (matching `atlasPath_` for zone atlases)
- At zone load time, load relevant entity atlases into `TextureAtlas` instances (same class used for zone atlases)
- Modify `race_model_loader.cpp` to replace the disabled `buildTexturedMesh()` path:
  - Instead of decoding DDS/BMP to ARGB and uploading 32bpp, look up texture name in entity atlas
  - Get atlas page texture handle + UV rect
  - Build mesh with atlas UVs (remap vertex UVs from original texture space to atlas tile space)
  - Apply atlas page texture as the mesh material texture
- Remove the `buildColoredMesh()` gray placeholder fallback (or keep as fallback when atlas is missing)
- Entity atlas pages loaded through `ConstrainedTextureCache` — proper memory budget, LRU eviction, `/pmem` visibility

**Key files to modify**:
- `src/client/graphics/eq/race_model_loader.cpp` — replace disabled Path B with atlas lookup
- `src/client/graphics/eq/mesh_building.cpp` — UV remapping to atlas tile coordinates
- `src/client/graphics/irrlicht_renderer.cpp` — entity atlas loading at zone init
- `include/client/graphics/constrained_texture_cache.h` — entity atlas integration

**Key reference for UV remapping**: `src/client/graphics/eq/zone_geometry.cpp` already remaps zone mesh UVs to atlas tile space. Same transform: `newUV = tileOffset + originalUV * tileScale`.

**Deliverable**: Character models render with correct skin/body textures loaded from ETC1 atlases. Memory usage ~8× lower than old disabled code.

### Chunk 4: Runtime Atlas Loading for Equipment Models

**Goal**: Load equipment textures from pre-built entity atlases. Re-enable textured weapons and armor.

**Scope**:
- Modify `equipment_model_loader.cpp` to replace disabled texture loading (lines 558-618):
  - Look up equipment texture name in gequip atlas
  - Get atlas page + UV rect
  - Build equipment mesh with remapped UVs
- Handle equipment texture reference counting properly (replace disabled cleanup at lines 650-662)
- Equipment models share atlas pages — multiple weapons/armor pieces on the same page get the same texture bind (automatic batching opportunity)

**Key files to modify**:
- `src/client/graphics/eq/equipment_model_loader.cpp` — replace disabled Path B
- `src/client/graphics/eq/mesh_building.cpp` — UV remapping for equipment meshes

**Deliverable**: Weapons and armor render textured from ETC1 atlases.

### Chunk 5: Armor Variant Textures

**Goal**: Re-enable equipment appearance variants (leather vs plate armor colors, etc). This is the disabled code in `animated_mesh_creation.cpp:439-475`.

**Scope**:
- Armor variants are different textures for the same mesh geometry (e.g., `clkhe0101` vs `clkhe0201`)
- The entity atlas builder (Chunk 1) packs ALL variant textures — they're just entries with different names in the same archive
- At runtime, when building an entity's visual:
  - Determine the correct variant texture name based on equipment appearance values
  - Look up that variant name in the entity atlas (same lookup as Chunk 3/4)
  - Apply the variant's atlas page + UV rect to the mesh
- Replace the 173 lines of commented code in `animated_mesh_creation.cpp:439-475` with a clean atlas lookup path

**Key files to modify**:
- `src/client/graphics/eq/animated_mesh_creation.cpp` — replace disabled variant override path
- `src/client/graphics/eq/variant_loading.cpp` — variant name resolution (likely still functional, just the texture application was disabled)

**Deliverable**: NPCs and players render with correct armor appearance variants (leather, chain, plate, robe, etc).

### Chunk 6: Remove Dead Code

**Goal**: Clean up all disabled "Path B" code and the old texture loading infrastructure that the atlas system replaces.

**Scope**:
- Remove all commented-out "DISABLED: unconstrained Path B" blocks across the 6 affected files
- Remove `buildTexturedMesh()` if no longer used (replaced by atlas-aware mesh building)
- Remove `loadTextureFromBMP()` direct-upload path from mesh_building.cpp
- Remove DDS/BMP decode code from `race_model_loader.cpp` runtime path (decoding now happens only in the offline tool)
- Remove the ARGB→RGBA conversion pipeline in `constrained_texture_cache.cpp::queueDecodedARGB()` for entity textures (atlas data is already ETC1)
- Clean up `gpu_upload_thread.cpp` — entity texture uploads are now `glCompressedTexImage2D` (atlas pages), not `glTexImage2D` (raw RGBA)
- Audit and remove any other dead texture loading code that the atlas system makes obsolete

**Key files to clean up**:
- `src/client/graphics/eq/race_model_loader.cpp`
- `src/client/graphics/eq/animated_mesh_creation.cpp`
- `src/client/graphics/eq/mesh_building.cpp`
- `src/client/graphics/eq/equipment_model_loader.cpp`
- `src/client/graphics/eq/zone_geometry.cpp`
- `src/client/graphics/constrained_texture_cache.cpp`
- `src/client/graphics/gpu_upload_thread.cpp`

**Deliverable**: Clean codebase with a single texture loading path (atlas-based, ETC1, constrained cache). No dead code, no disabled paths.

## Execution Order

```
Chunk 1 (Atlas builder tool)          — standalone offline tool
    ↓
Chunk 2 (Batch generation script)     — trivial, depends on Chunk 1
    ↓
Chunk 3 (Race model atlas loading)    — re-enables character textures
    ↓
Chunk 4 (Equipment atlas loading)     — re-enables weapon/armor textures
    ↓
Chunk 5 (Armor variant textures)      — re-enables appearance customization
    ↓
Chunk 6 (Dead code removal)           — cleanup after all paths migrated
```

Chunks 3, 4, and 5 can be developed somewhat in parallel since they modify different files, but 3 should go first to establish the runtime atlas lookup pattern that 4 and 5 follow.

## Relationship to Other Plans

- **Custom Renderer** (`docs/future/custom_renderer.md`): Entity atlas loading is independent of the Irrlicht removal. The atlas system works with the current Irrlicht-based renderer (atlas pages are `ITexture` objects managed by the constrained cache). When the custom renderer lands, atlas pages become raw GL texture handles — a trivial change.
- **UI/Text** (`docs/future/ui_fixes.md`): No dependency in either direction.
- **Zone Atlas Builder** (`tools/zone_atlas_builder.cpp`): The entity atlas builder is modeled directly on this tool. Same binary format, same compression library, same runtime loader class.

## Notes

- The entity atlas builder should detect and log textures with alpha channels so they get the dual-page treatment. Most character/equipment textures are opaque (skin, metal, leather). Alpha is rare (some cloaks, robes, hair with cutout edges).
- Atlas page size of 2048×2048 is appropriate. EQ entity textures are small (mostly 256×256, some 128×128). A single 2048×2048 page holds 64 tiles of 256×256 — likely one page per race archive, 1-3 pages for gequip.
- Consider generating a manifest file alongside the atlas (JSON or text) listing all packed texture names and their atlas locations. Useful for debugging ("is texture X in the atlas?") without needing to parse the binary format.
- The pre-built atlas approach also eliminates the per-entity DDS/BMP decode cost during gameplay. When a new entity spawns, its textures are already on the GPU (or loaded as a pre-compressed atlas page via the constrained cache). No background thread decode work needed.
