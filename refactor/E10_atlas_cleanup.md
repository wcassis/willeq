# E10: Remove Zone Atlas UV Workaround Path (Optional)

## Overview

With EQE1 providing individual ETC1 textures that support `GL_REPEAT` natively,
the zone atlas UV remapping path and atlas-specific shaders are no longer required
for correct texture rendering. This unit removes the atlas workaround path for zones,
simplifying the rendering pipeline.

This unit is **optional** — the EQE1 and atlas paths coexist without conflict.
Only pursue this if E09 validation confirms EQE1 is fully stable and the atlas
UV issues are not worth maintaining.

## Prerequisites

- E09 complete (EQE1 validated on target hardware)
- Confirmation that zone atlas path is no longer needed

## Plan

### Step 1: Assess what to remove

Inventory of atlas-specific code:

a. **Zone atlas loading in loadZoneSequential()** (irrlicht_renderer.cpp ~lines 3534-3659):
   - `TextureAtlas::preloadFromFile()` calls for zone + object atlases
   - Atlas page GL upload loop
   - Atlas shader page texture setup

b. **Atlas mesh building** (zone_geometry.cpp `buildAtlasedMesh()` ~lines 759-1246):
   - UV remapping from original texture coords to atlas tile coords
   - Material setup with atlas page textures
   - Alpha page index storage in MaterialTypeParam2

c. **Atlas shaders** (AtlasSolid3D, AtlasAlpha3D in COpenGLES2Shaders):
   - Separate shader programs for atlas rendering
   - Alpha texture unit 1 sampling

d. **ZoneShaderManager atlas shader compilation** (irrlicht_renderer.cpp):
   - Atlas shader compile + uniform setup

e. **Config fields**: `enableTextureAtlas`, `atlasPath`

f. **TextureAtlas class** (texture_atlas.h/cpp):
   - Entire class if no longer used by any system

### Step 2: Determine what to keep

- **TextureAtlas class**: May still be useful for UI atlas (`UIAtlas`). Check if
  UIAtlas uses the same class or a separate implementation.
- **Atlas shaders**: If UI atlas uses them, keep. Otherwise remove.
- **Config fields**: Remove `enableTextureAtlas`. Keep `atlasPath` only if UI atlas
  needs it.

### Step 3: Remove zone atlas loading from loadZoneSequential()

- Remove the atlas loading step (Step 3 "Building texture atlases")
- Remove `useZoneAtlas` flag and associated branching
- `buildTexturedMesh()` is always used (never `buildAtlasedMesh()`)

### Step 4: Remove buildAtlasedMesh()

- Remove ~490 lines from zone_geometry.cpp
- Keep `buildTexturedMesh()` as the only mesh building path

### Step 5: Remove atlas shaders (if UI atlas doesn't need them)

- Remove AtlasSolid3D and AtlasAlpha3D from shader sources
- Remove their compilation in ZoneShaderManager
- Remove their program IDs from renderer state

### Step 6: Clean up config

- Remove `enableTextureAtlas` field from ConstrainedRendererConfig
- Remove `--atlas-path` CLI parsing if no longer needed
- Remove atlas path validation from startup

### Step 7: Build and test

a. `cmake --build build -j24`
b. ARM cross-compile
c. Verify zone rendering works with EQE1 only (no atlas)
d. Verify UI atlas still works (if separate from zone atlas)

## Acceptance Criteria

- [ ] Zone atlas loading code removed from sequential loader
- [ ] `buildAtlasedMesh()` removed from zone_geometry.cpp
- [ ] Atlas shader programs removed (if UI doesn't need them)
- [ ] `enableTextureAtlas` config field removed
- [ ] Zone rendering works correctly with EQE1 textures
- [ ] UI rendering unaffected
- [ ] No dead code remaining from atlas system
- [ ] Both x86 and ARM builds succeed

## Review

All zone texture atlas code removed. UI atlas kept (separate system, no UV issues).

**Removed from headers:**
- `zoneAtlas_`, `objAtlas_` members from IrrlichtRenderer
- `objAtlasPageOffset_` member
- `enableTextureAtlas` config field from ConstrainedRendererConfig

**Removed from irrlicht_renderer.cpp:**
- Step 3 ("Building texture atlases") — entire 120-line block replaced with comment
- Atlas branch in Step 4 region mesh building — always uses `buildTexturedMesh` now
- Atlas branches in `rebuildRegionMesh`, `submitOneBgMeshBuild`, object mesh building
- GPU upload thread atlas page result handling (2 locations)
- Zone/obj atlas unload in zone cleanup
- Atlas memory stats in `/pmem`

**Removed from constrained_renderer_config.cpp:**
- `enableTextureAtlas = true` from orangepi preset
- `enableTextureAtlas` JSON parsing

**Kept:**
- `atlasPath` config field (used by UI atlas)
- `--atlas-path` CLI flag (used by UI atlas)
- UI atlas loading (changed gate from `enableTextureAtlas && !atlasPath.empty()` to `!atlasPath.empty()`)
- `TextureAtlas` class (still used by UI atlas)
- `buildAtlasedMesh()` in zone_geometry.cpp (dead code, can be removed separately)
- Atlas shaders AtlasSolid3D/AtlasAlpha3D (compiled but unused, can be removed separately)
- `zone_atlas_builder` tool (still functional for generating atlas files)

**Test removed:** `EXPECT_TRUE(config.enableTextureAtlas)` from test_constrained_renderer.cpp

Both x86 and ARM builds succeed. All tests pass.
