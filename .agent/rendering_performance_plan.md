# Rendering Performance Optimization Plan

## Problem Statement

`smgr->drawAll()` consistently takes 51-55ms, limiting framerate to ~18-20 FPS. This is the Irrlicht scene manager's main render call.

## Current Scene Composition

| Element | Count | Notes |
|---------|-------|-------|
| Zone vertices | 60,824 | Single mesh, cannot be partially culled |
| Zone triangles | 34,479 | All rendered every frame |
| Placeable objects | 476 | Each a separate draw call, no distance culling |
| Zone lights | 274 | Created but managed by unified light system |
| Object lights | 152 | Distance + occlusion culled |
| Entity meshes | Variable | ~10-50 depending on area |

## Root Causes

### 1. Single Zone Mesh
The entire zone geometry is built as one `IMeshSceneNode` with 34k triangles. Irrlicht cannot partially cull this - when any part is visible, all 34k triangles are processed by the software renderer.

### 2. No Object Distance Culling
All 476 `objectNodes_` are rendered every frame regardless of distance from camera. Each object is a separate draw call with its own mesh.

### 3. Backface Culling Disabled
`BackfaceCulling = false` is set on all materials (zone, objects, entities). This effectively doubles the triangle count since both sides of every polygon are rasterized.

### 4. Per-Vertex Lighting
Software renderer performs lighting calculations per-vertex. With 60k+ vertices and active lights, this adds significant overhead even with our 8-light limit.

### 5. No Spatial Partitioning
No octree, BSP traversal, or sector-based rendering. The scene graph is flat - all nodes are checked every frame.

## Proposed Solutions

### Phase 1: Object Distance Culling (High Impact, Low Effort) ✅ COMPLETE

**Goal**: Reduce draw calls by hiding distant objects

**Implementation**:
1. Add `objectRenderDistance_` config option (default: 300 units)
2. In `processFrame()` or dedicated `updateObjectVisibility()`:
   - Calculate distance from camera to each object
   - Call `setVisible(false)` on objects beyond render distance
3. Optimize by only updating every N frames or on significant camera movement (>10 units)

**Expected Impact**: 50-70% reduction in object draw calls depending on zone density

**Files modified**:
- `irrlicht_renderer.h` - Added `objectRenderDistance_`, `objectPositions_`, `lastCullingCameraPos_` members
- `irrlicht_renderer.cpp` - Added `updateObjectVisibility()` function, cached positions in `createObjectMeshes()`

**Implementation Details**:
- `objectRenderDistance_` = 300 units (squared distance comparison for efficiency)
- `objectPositions_` vector caches object positions at creation time to avoid per-frame lookups
- `lastCullingCameraPos_` tracks camera position to skip updates when camera moves < 10 units
- `updateObjectVisibility()` called each frame in `processFrame()`, early-exits if camera hasn't moved significantly
- Uses `setVisible(true/false)` on object scene nodes to control rendering

**Status**: ❌ No significant impact. Zone mesh (34k triangles) is the bottleneck, not objects.

**Test Results**:
- drawAll(): 51-56ms (unchanged)
- FPS: ~21-23 (unchanged)

### Phase 2: Zone Mesh Sectoring (Highest Impact, High Effort)

**Goal**: Split zone into cullable sectors for frustum culling

**Implementation Options**:

#### Option A: BSP Region-Based Sectors
EQ zones have BSP region data (`HCMap` already loads this). Use regions to split geometry:
1. During zone loading, assign triangles to BSP regions
2. Create separate mesh node per region (or group of adjacent regions)
3. Irrlicht automatically frustum culls each sector

**Pros**: Uses existing EQ data, natural room/area boundaries
**Cons**: Region count varies wildly, some zones have 1000+ regions

#### Option B: Grid-Based Sectors
Divide zone into uniform grid cells:
1. Define sector size (e.g., 128x128 units)
2. During mesh building, bucket triangles by which sector(s) they touch
3. Create mesh node per sector

**Pros**: Predictable sector count, simple implementation
**Cons**: May split logical areas, triangles spanning sectors need duplication or special handling

#### Option C: Octree Scene Node ❌ TESTED - NO IMPACT
Use Irrlicht's `IOctreeSceneNode`:
1. Build zone mesh as normal
2. Add to scene as octree node instead of mesh node
3. Irrlicht handles spatial partitioning internally

**Pros**: Minimal code changes, built-in to Irrlicht
**Cons**: May not work well with software renderer, less control

**Test Results**: Implemented and tested. No performance improvement - software renderer does not benefit from octree culling. Reverted.

**Recommended**: Option B (grid sectors) may still help if it reduces draw calls

**Files to modify**:
- `zone_geometry.cpp/h` - Sector building logic
- `irrlicht_renderer.cpp` - Use sectored mesh or octree node

### Phase 3: Backface Culling Audit (Medium Impact, Low Effort) ❌ TESTED - VISUAL ARTIFACTS

**Goal**: Reduce triangle count by enabling backface culling where safe

**Analysis needed**:
1. Zone geometry - Most should be single-sided (walls, floors, ceilings)
2. Placeable objects - Closed meshes can use backface culling
3. Entities - Character meshes are typically closed
4. Exceptions - Flags, banners, foliage need double-sided rendering

**Test Results**: Enabling backface culling on zone mesh and objects caused many surfaces to appear with missing polygons. EQ zone geometry has inconsistent winding or intentionally double-sided surfaces. Reverted.

**Conclusion**: Cannot use backface culling without fixing geometry winding at the source (WLD loader) or per-material flags from the original EQ data.

### Phase 4: Lighting Optimization (Medium Impact, Low Effort)

**Goal**: Reduce per-vertex lighting overhead

**Options**:
1. **Distance-based lighting disable**: Objects beyond N units use `Lighting = false`
2. **Reduced light count for distant geometry**: Only nearest 2-4 lights for far objects
3. **Ambient-only mode**: Option to disable dynamic lighting entirely
4. **Light influence radius**: Reduce attenuation so lights affect fewer vertices

**Implementation**:
- Already have unified light management limiting to 8 lights
- Could add tier system: near objects get all 8, medium get 4, far get ambient only

### Phase 5: Level of Detail System (Medium Impact, High Effort)

**Goal**: Reduce vertex/triangle count for distant objects

**Implementation**:
1. Generate simplified meshes at load time (50%, 25% detail levels)
2. Switch LOD based on distance to camera
3. Use billboards or hide for very distant objects

**Note**: This is significant effort and may not be necessary if phases 1-3 provide sufficient improvement.

## Implementation Priority

| Phase | Impact | Effort | Priority | Status |
|-------|--------|--------|----------|--------|
| 1. Object Distance Culling | High | Low | **Do First** | ❌ No impact |
| 2c. Octree Scene Node | Highest | Low | Tried second | ❌ No impact |
| 3. Backface Culling Audit | Medium | Low | Tried third | ❌ Visual artifacts |
| 2b. Grid-Based Sectors | Highest | High | Next option | Pending |
| 4. Lighting Optimization | Medium | Low | Alternative | Pending |
| 5. LOD System | Medium | High | Only if needed | Pending |

## Key Finding

The software renderer's `drawAll()` time is dominated by CPU rasterization of all triangles. Scene-graph level optimizations (octree culling, object visibility) don't help because the renderer still processes all geometry. Only reducing actual triangle count helps, but EQ geometry requires double-sided rendering.

## Metrics to Track

Before and after each phase, measure:
1. `smgr->drawAll()` time (currently 51-55ms)
2. FPS (currently 14-20)
3. Draw call count (if measurable)
4. Visible triangle count (if measurable)

## Questions to Resolve

1. Does Irrlicht's software renderer benefit from octree nodes, or is that only for hardware culling?
2. Are there EQ zone flags indicating which surfaces should be double-sided?
3. What's the acceptable minimum render distance for objects without visual pop-in being noticeable?
4. Should distant objects fade out (alpha) or hard cut?
