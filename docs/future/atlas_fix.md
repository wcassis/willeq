# Plan: Mipmapped Atlas Pages + Pre-Tessellation for UV Tiling

## Context

The texture atlas system currently rejects 67% of zone triangles (qeynos2) because their UV coordinates span more than one repeating cell. These triangles fall back to per-texture rendering with individual `glBindTexture` calls, defeating the atlas's primary goal of reducing GPU state changes on Mali 400.

Additionally, atlas pages are uploaded as single-level ETC1 (no mipmaps), missing the hardware's mipmap support and causing aliasing at distance.

This plan adds mipmapped ETC1 atlas pages and pre-tessellates triangles at integer UV boundaries so nearly 100% of non-animated triangles can be batched through the atlas.

## Approach: Pre-Tessellation (not `fract()`)

`fract()` in the fragment shader would be simpler but causes wrong mip level selection at integer UV boundaries (derivative discontinuity → hardware selects too-coarse mip → blurry seam lines). Fixing this requires overriding the texture sampler's mip selection via `texture2DGradEXT()` from `GL_EXT_shader_texture_lod`, which is **not available** on Mali 400. Note: `fwidth()` from `GL_OES_standard_derivatives` is confirmed working on Mali 400 / Lima (tested via `gles2_derivatives_test`, all 4 tests pass) — but it only reads derivatives, it can't control the sampler's mip selection.

Pre-tessellation splits triangles at integer UV cell boundaries during mesh building. Each sub-triangle fits within one UV cell, so the existing per-vertex atlas UV precomputation works unchanged. Mipmaps work correctly because there are no UV discontinuities. No shader changes needed.

**Completed**: `fwidth()` alpha-test shader variants have been enabled on Mali 400 (commit `6704ed4`). The previous comment claiming Lima's `fwidth()` returns degenerate NaN values was incorrect — never tested on hardware. `gles2_derivatives_test` confirmed all derivative functions work correctly.

## Changes

### 1. Atlas file format v2 (mipmapped)

**File**: `tools/zone_atlas_builder.cpp`

Add `mipLevels` field to the header:
```
AtlasHeader (v2, 19 bytes):
  ... existing fields ...
  uint8_t mipLevels;    // NEW: number of ETC1 mip levels (e.g. 12 for 2048x2048)
```

Version bumped from 1 → 2. Page entries unchanged — `dataOffset` points to level 0, all mip levels stored contiguously, `dataSize` is total of all levels. Per-level sizes are deterministic from atlas dimensions:
```
level_size(n) = max(1, (w >> n) / 4) * max(1, (h >> n) / 4) * 8
```

The runtime loader remains backwards-compatible: version 1 = single level (existing behavior), version 2 = multi-level.

### 2. Offline mip chain generation

**File**: `tools/zone_atlas_builder.cpp`

After assembling each 2048x2048 RGBA page:
1. Box-filter downsample to 1024x1024, 512x512, ..., down to 1x1 (12 levels total)
2. ETC1-encode each level separately via `Etc::Encode()`
3. Concatenate all levels' ETC1 data into one contiguous block per page
4. Write to `.atlas` with updated header

The 4px wrap-around border naturally downsamples correctly through the mip chain. At mip level 2 (512x512, tile=64px) the border is 1px — still sufficient for bilinear filtering. Below mip 3, minor cross-tile bleeding may occur but is imperceptible at those viewing distances.

### 3. Runtime upload with mip levels

**Files**: `src/client/graphics/texture_atlas.cpp`, `include/client/graphics/texture_atlas.h`

In `uploadPreloadedPage()` (sync path):
- Compute per-level offset/size from atlas dimensions
- Call `glCompressedTexImage2D(GL_TEXTURE_2D, level, GL_ETC1_RGB8_OES, w>>level, h>>level, ...)` for each level
- Change `GL_TEXTURE_MIN_FILTER` from `GL_LINEAR` to `GL_LINEAR_MIPMAP_LINEAR` (trilinear)
- Keep `GL_TEXTURE_MAG_FILTER` as `GL_LINEAR`

In `uploadPreloadedPageAsync()` (GLES2 async path):
- Include all mip level data in the `UploadRequest`
- GPUUploadThread uploads each level

`PreloadData::PageData` stores the full mip chain as one contiguous vector (as written in the file). A new `mipLevels` field on PreloadData tells the uploader how many levels to upload.

Version 1 files: `mipLevels = 1`, single `glCompressedTexImage2D` call, `GL_LINEAR` min filter (existing behavior preserved).

### 4. Pre-tessellation in buildAtlasedMesh()

**File**: `src/client/graphics/eq/zone_geometry.cpp`

Replace the UV > 1.0 rejection block (lines 773-792) with a triangle splitting routine.

Algorithm — for each triangle whose UV span exceeds 1.0 in either dimension:
1. Compute the integer U and V grid lines that cross the triangle's UV bounding box
2. Clip the triangle against each grid line using Sutherland-Hodgman half-plane clipping
   - Clip against each integer U line (left-to-right), then each integer V line (bottom-to-top)
   - At each clip point, linearly interpolate position, normal, UV, and vertex color
3. Fan-triangulate each resulting convex polygon (all results are convex since we're intersecting convex shapes)
4. Each sub-triangle fits within one UV cell — feed it into the existing cell-basing + atlas UV precomputation code

Implemented as a helper function (e.g. `splitTriangleToUVCells()`) that takes one source triangle and returns a vector of sub-triangles. Called inline where the rejection currently happens.

Triangles that already fit within one cell (the current 33% that pass) go through unchanged — no performance regression for the common case.

### 5. No shader changes

The existing `AtlasSolid3D` / `AtlasAlpha3D` shaders work as-is. Tessellated sub-triangles have precomputed atlas UVs in `TCoords2` (attribute `aTexCoord1`), same as today. The vertex format (`S3DVertex2TCoords`) is unchanged.

## Files Modified

| File | Change |
|------|--------|
| `tools/zone_atlas_builder.cpp` | v2 header, mip chain generation + encoding |
| `include/client/graphics/texture_atlas.h` | `mipLevels` in PreloadData, v2 header struct |
| `src/client/graphics/texture_atlas.cpp` | Multi-level upload, v1/v2 compat, trilinear filter |
| `src/client/graphics/eq/zone_geometry.cpp` | `splitTriangleToUVCells()`, remove UV>1.0 rejection |

## Verification

1. **Build the new atlas**: `./build/bin/zone_atlas_builder --eq-path ... --zone qeynos2 --output ...`
   - Verify file size is ~4/3× larger (mip overhead)
   - Parse header to confirm version=2, mipLevels=12
2. **Run atlas_uv_audit**: Compare triangle pass/fail counts — should be ~0 failing (only animated textures excluded)
3. **Visual test on Orange Pi**: Run qeynos2, verify:
   - No cross-tile bleeding at close range
   - Distant textures are smoothly filtered (no shimmer/aliasing)
   - `/pmem` shows expected GPU memory increase (~33%)
   - `/frametiming` shows reduced draw call overhead
4. **Backwards compat**: Load a v1 .atlas file with the updated runtime — should work identically to before (single level, GL_LINEAR)
