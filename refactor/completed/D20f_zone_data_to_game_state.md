# D20f: Move Zone Data Loading to Game State Layer

## Context

Zone S3D archives contain data needed by both game state (collision, pathfinding,
water detection) and the renderer (meshes, textures, PVS culling). Currently the
renderer owns all S3D loading. D20e worked around this with opaque pointers.

This refactor makes S3D/WLD-derived data structures available at the game state
layer. The renderer still builds GPU resources but receives the parsed data as
input rather than owning the parse.

**Key insight**: BspTree, BspNode, BspRegion, BspBounds already use neutral types
(floats, ints, std::vector). No Irrlicht type neutralization needed — just a
header reorganization.

## What game state needs from S3D

1. **BSP tree** — water detection, zone lines, region membership
2. **Zone mesh bounding boxes** — per-region AABB for coarse collision
3. **Object bounding boxes** — placeables (trees, buildings, rocks) with position
   and AABB for pathfinding/collision augmentation
4. **Door bounding boxes** — doors are placeables with open/closed state that
   affects collision. Includes lifts (moving platform floor), sliding doors,
   swinging doors, and designer-placed world geometry (trees, buildings, rocks
   used as "doors"). Store open and closed bounding boxes; while animating,
   entities pass through. Lifts need a moveable floor bounding box.
5. **Light positions/radii** — for audio emitter placement

Game state does NOT need: vertex data, texture data, animation tracks, UV coords,
or any GPU-specific structures. Bounding boxes are sufficient for collision.

## Sub-units

### D20f1: Extract BSP + zone data structs to shared header

Move BSP-related structs out of `wld_loader.h` into a standalone header with no
graphics dependencies. Both game state and renderer include it.

**New file**: `include/client/zone_bsp.h`

Contents (moved from `include/client/graphics/eq/wld_loader.h` lines 15-124):
- `RegionType` enum
- `ZoneLineType` enum
- `ZoneLineInfo` struct
- `BspNode` struct
- `BspRegion` struct
- `BspBounds` struct
- `BspTree` struct (with findRegionForPoint, checkZoneLine, computeRegionBounds)

**New file**: `include/client/zone_object_data.h`

Game-state-friendly structs for objects and doors:
- `ZoneObjectBounds` — AABB + position + name for a placeable object
- `ZoneDoorBounds` — AABB for open state, AABB for closed state, current state,
  position, heading, door type (normal/lift/sliding), lift floor Z range
- `ZoneLightData` — position, radius, color (uint32_t RGBA)

**Modified**: `include/client/graphics/eq/wld_loader.h` — `#include "client/zone_bsp.h"`
instead of defining BSP structs inline. Keeps render-specific structs (WldVertex,
WldGeometry, etc.).

**Implementation of BspTree methods**: Move `findRegionForPoint()` etc. to a new
`src/client/zone_bsp.cpp` (or keep header-only if small enough). Currently they
live in `src/client/graphics/eq/wld_loader.cpp`.

### D20f2: Populate zone data at game state layer

After zone loading completes, extract bounding boxes from the parsed S3D data
and store them in game state.

**New member on EverQuest**: `m_zone_bsp_tree` changes from `shared_ptr<void>` to
`shared_ptr<BspTree>` (typed). Add `m_zone_objects` (vector<ZoneObjectBounds>)
and `m_zone_door_bounds` (map<uint8_t, ZoneDoorBounds>).

**Population flow**: The renderer already parses S3D and builds the BSP tree.
After zone loading completes, Application extracts:
- BSP tree from renderer (`getZoneBspTree()`)
- Object bounding boxes from renderer's object list
- Door bounding boxes from renderer's door manager
And stores them in EverQuest via setters.

This is the pragmatic first step — the renderer still does the parsing, but game
state gets typed copies. D20f5 (future) can move parsing earlier if needed.

**Files modified**:
- `include/client/eq.h` — typed BSP tree, zone objects, door bounds
- `src/client/eq.cpp` — setters, UpdateWaterState uses typed BSP
- `src/client/application.cpp` — extract and pass data after zone load
- `include/client/zone_load_snapshot.h` — optional: add BSP tree field

### D20f3: Route water detection through game-state BSP tree

`UpdateWaterState()` reads from typed `m_zone_bsp_tree` directly.

- Remove the `BspTreeAvailableIntent` workaround from D20e
- Remove `shared_ptr<void>` cast hack
- Include `zone_bsp.h` in eq.cpp (already has wld_loader.h which will include it)

### D20f4: Clean up D20e workarounds

- Remove `BspTreeAvailableIntent` from renderer_intents.h and ProcessBridgeIntents
- Remove `shared_ptr<void> m_zone_bsp_tree` (replaced by typed version in D20f2)
- Remove `static_pointer_cast` in UpdateWaterState

## Acceptance Criteria

1. BSP structs in standalone header with no graphics includes
2. Zone object/door bounding box structs defined for future collision use
3. `m_zone_bsp_tree` is typed `shared_ptr<BspTree>` (no void cast)
4. `UpdateWaterState()` reads BSP tree directly
5. BspTreeAvailableIntent workaround removed
6. Full build succeeds, all tests pass
7. Functional: zone loads, water detection works, zone lines trigger

## Non-goals (future work)

- Moving S3D file parsing from renderer to game state (larger refactor)
- Building collision query API using bounding boxes
- Lift/platform movement tracking
- Populating door bounds from actual mesh geometry (needs mesh→AABB extraction)

## Verification

```bash
cmake --build build -j24
./build/bin/test_packet && ./build/bin/test_eq_client && ./build/bin/test_spell_database
grep -n 'shared_ptr<void>' include/client/eq.h  # should be empty
grep -n 'BspTreeAvailableIntent' include/client/events/renderer_intents.h  # should be empty
grep -rn 'zone_bsp.h' include/ src/  # should show includes from both game state and graphics
```
