# D20f: Move Zone Data Loading to Game State Layer

## Context

Zone S3D archives contain data that serves two distinct layers:

1. **Game state data** — BSP tree (water detection, zone lines), region types, light
   positions. Needed by EverQuest for gameplay logic regardless of whether a renderer
   is attached.
2. **Renderer data** — per-region mesh geometry, textures, object meshes, character
   models. Only meaningful when rendering.

Currently, the entire S3D archive is loaded by the renderer's `loadZoneSequential()`
on the loading thread. Game state code then reaches back into the renderer to access
the BSP tree (`m_renderer->getZoneBspTree()`), creating a cross-boundary dependency
that D20e had to work around with opaque pointers and intent hacks.

The loading thread exists only to keep the network pump alive during loading — the
user cannot interact until loading completes. There are no player actions during
loading, so no renderer→game events need to flow.

**This refactor moves S3D archive parsing and BSP tree extraction to the game state
layer.** The renderer receives the pre-parsed BSP tree and zone geometry as input,
then builds GPU resources (meshes, textures, VBOs) during its sequential loading
phase.

## Architecture After D20f

```
Game State Layer                    Renderer Layer
───────────────                    ──────────────
1. Open S3D PFS archive            (waiting)
2. Parse WLD → BspTree             (waiting)
3. Extract region types, lights    (waiting)
4. Store in ZoneLoadSnapshot       (waiting)
5. Signal loading thread      ───► 6. Receive BspTree + S3DZone via snapshot
                                   7. Build meshes, upload textures
                                   8. Install portal system, doors
                                   9. Signal complete
```

The PFS archive reader, WLD parser, and S3D loader are pure file I/O + data
structure construction — no GL context or renderer state needed.

## Current Data Flow

**S3D → WLD → BspTree creation** (all in renderer today):
- `S3DLoader::loadZone()` opens PFS archive, creates `S3DZone`
- `WldLoader::parseWldBuffer()` parses fragments, builds `BspTree`
- Sequential loader Step 2 extracts BSP, computes region bounds
- BSP tree stored in `IrrlichtRenderer::zoneBspTree_`

**Game state uses of BSP tree:**
- `EverQuest::UpdateWaterState()` — water/lava/swimming detection
- `ZoneLines` — zone crossing detection via region types
- Pathfinding — region-aware routing

**Renderer uses of BSP tree:**
- PVS culling — `visibleRegions` bitvector per region
- Entity visibility — region membership check
- Camera collision — BSP region bounds
- Door manager — region assignment
- Detail system — region-based placement
- Portal system — built from BSP split planes

## Sub-units

### D20f1: Extract S3D/WLD parsing into standalone library

Move the S3D archive reader and WLD fragment parser out of the `client/graphics/eq/`
directory into a location accessible to game state code.

**Files to move** (or make available outside graphics):
- `pfs.h / pfs.cpp` — PFS archive reader (pure file I/O, no graphics deps)
- `wld_loader.h / wld_loader.cpp` — WLD fragment parser (creates BspTree, extracts
  geometry, lights, etc.)
- `s3d_loader.h / s3d_loader.cpp` — Orchestrates PFS + WLD loading, produces S3DZone

**Approach**: Create `include/client/eq_data/` and `src/client/eq_data/` for the
EQ-format file parsers. These are data format libraries, not renderer code. The
renderer will include them from the new location.

**Key constraint**: The WLD parser currently creates Irrlicht-specific types
(`irr::core::vector3df`, `irr::video::SColor`) in its output structs. These need to
be replaced with neutral types (`glm::vec3`, `uint32_t` for RGBA) in the output
data structures so game state code doesn't need Irrlicht headers.

**Structs to neutralize**:
- `BspNode` — uses `irr::core::vector3df` for split plane normal/point
- `BspRegion` — uses `irr::core::aabbox3df` for bounding box
- `WldLight` — uses `irr::video::SColor` for color
- `WldVertex` / geometry structs — used by renderer only, can stay Irrlicht-typed
  (renderer copies them into mesh buffers)

**Split strategy**: Separate the WLD output into two layers:
1. **Game data structs** (neutral types): `BspTree`, `BspNode`, `BspRegion`,
   `RegionType`, `ZoneLineInfo`, `WldLight` position/radius
2. **Render data structs** (Irrlicht types): `WldGeometry`, `WldVertex`,
   `WldTextureRef`, animation data

The game data structs move to `include/client/eq_data/zone_data.h`.
The render data structs stay in `include/client/graphics/eq/wld_loader.h`.

**Files modified**:
- New: `include/client/eq_data/zone_data.h` — BspTree, BspRegion, RegionType, etc.
- New: `include/client/eq_data/pfs_archive.h/.cpp` — moved from graphics/eq/
- New: `include/client/eq_data/wld_parser.h/.cpp` — WLD fragment parsing (BSP + metadata)
- Modified: `include/client/graphics/eq/wld_loader.h` — imports zone_data.h, keeps
  render-specific structs
- Modified: `include/client/graphics/eq/s3d_loader.h` — imports from new location
- Modified: CMakeLists.txt — new source files

### D20f2: Load BSP tree at game state layer

Move S3D archive opening and BSP tree extraction into EverQuest's zone loading flow,
before graphics loading starts.

**New method**: `EverQuest::LoadZoneData(const std::string& zoneName)` — called from
`OnGameStateComplete()` before signaling the loading thread.

This method:
1. Opens the zone S3D archive (`<zoneName>.s3d`) via `PfsArchive`
2. Parses the WLD file to extract `BspTree` + zone metadata (lights, region types)
3. Stores results in EverQuest members:
   - `m_zone_bsp_tree` (shared_ptr<BspTree>)
   - `m_zone_lights` (vector<ZoneLight>) — positions/radii for audio emitters
4. Updates `ZoneLoadSnapshot` to include the pre-parsed BSP tree

**Impact on loading thread**: The sequential loader receives the BSP tree via the
snapshot instead of parsing it from scratch. Step 1 (S3D Parse) still runs on the
loading thread for the geometry/texture data, but the BSP tree is already available.

Alternatively, the S3D archive can be opened once on the game thread and the open
archive handle passed to the loading thread, avoiding double-open.

**Files modified**:
- `include/client/eq.h` — add `m_zone_bsp_tree`, `LoadZoneData()`
- `src/client/eq.cpp` — implement `LoadZoneData()`, call from `OnGameStateComplete()`
- `include/client/zone_load_snapshot.h` — add `bspTree` field
- `src/client/graphics/irrlicht_renderer.cpp` — `loadZoneSequential()` Step 2 uses
  pre-parsed BSP from snapshot instead of re-parsing

### D20f3: Route water detection through game-state BSP tree

Convert `UpdateWaterState()` to read from `m_zone_bsp_tree` directly instead of
querying the renderer. This is the primary game-state consumer of the BSP tree.

**Changes**:
- `UpdateWaterState()` uses `m_zone_bsp_tree->findRegionForPoint()` directly
- Remove the `BspTreeAvailableIntent` workaround from D20e
- Remove `m_renderer->getZoneBspTree()` call

**Files modified**:
- `src/client/eq.cpp` — `UpdateWaterState()` reads `m_zone_bsp_tree`

### D20f4: Route zone line detection through game-state BSP tree

Zone line detection currently uses a mix of BSP region types and JSON data.
The BSP-based zone lines already work through the `ZoneLines` class.

**Changes**:
- `ZoneLines` receives BSP tree from game state, not renderer
- Zone line bounding boxes extracted from BSP at game state layer

**Files modified**:
- `include/client/zone_lines.h` — accept BSP tree from game state
- `src/client/eq.cpp` — pass `m_zone_bsp_tree` to zone line detection

### D20f5: Clean up renderer BSP tree ownership

After D20f2-f4, the renderer no longer owns the BSP tree — it receives it as input.

**Changes**:
- `loadZoneSequential()` receives BSP tree from snapshot, does not parse it
- `zoneBspTree_` in renderer is set from input, not from WLD parsing
- Remove duplicate BSP parsing code from renderer
- Verify all renderer BSP consumers still work (PVS, entity visibility, camera
  collision, door manager, detail system, portal system)

**Files modified**:
- `src/client/graphics/irrlicht_renderer.cpp` — receive BSP from snapshot
- `include/client/graphics/irrlicht_renderer.h` — document BSP is input, not owned

## Acceptance Criteria

1. S3D/WLD parsing code accessible to game state layer (no graphics includes)
2. BSP tree created at game state layer before graphics loading starts
3. `UpdateWaterState()` reads `m_zone_bsp_tree` directly (no renderer query)
4. Zone line detection uses game-state BSP tree
5. Renderer receives BSP tree as input via snapshot
6. No Irrlicht types in BSP/region data structures
7. Full build succeeds, all tests pass
8. Functional: zone loads, water detection works, zone lines trigger, PVS culling
   works, entities visible

## Risks

- **WLD parser coupling**: The WLD parser interleaves game-data extraction (BSP,
  lights) with render-data extraction (geometry, textures). Splitting it cleanly
  may require significant refactoring of `WldLoader::parseWldBuffer()`.
- **S3D double-open**: If game state opens the S3D to extract BSP and the renderer
  opens it again for geometry, we double the file I/O. Mitigate by passing the open
  archive handle through the snapshot.
- **BspTree struct size**: The BSP tree includes `visibleRegions` bitvectors per
  region (PVS data). This is renderer-specific. Consider splitting into
  `BspTreeCore` (game) + `BspTreePvs` (renderer), or accepting that game state
  carries PVS data it doesn't use.

## Verification

```bash
cmake --build build -j24
cd build && ctest --output-on-failure
# Verify BSP tree is loaded at game state layer
grep -n 'm_zone_bsp_tree' src/client/eq.cpp
# Verify no renderer BSP query from game state
grep -rn 'getZoneBspTree' src/client/eq.cpp
# Verify water detection works
# (functional test: enter zone with water, check swimming state)
```
