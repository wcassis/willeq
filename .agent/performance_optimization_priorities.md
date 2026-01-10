# Performance Optimization Priorities

Quick reference for implementing software renderer optimizations.

**Updated:** 2026-01-06 (Phase 4 complete - all optimizations done)

---

## Immediate Priority (Do Now - Quick Wins) - COMPLETED

### 0a. Remove Animation Debug Logging - DONE
**File:** `src/client/graphics/eq/animated_mesh_scene_node.cpp`
**Change:** Removed all TRACE logging blocks from applyAnimation(), render(), and OnAnimate()
**Impact:** Saves 2-5ms/frame when logging was enabled

### 0b. Skip Redundant Animation Transforms - DONE
**File:** `src/client/graphics/eq/animated_mesh_scene_node.cpp`
**Change:** Added frame caching with `lastAppliedFrame_` and `lastAppliedRotY_` tracking
- For NPCs: Skip applyAnimation() entirely if frame unchanged
- For player: Skip if both frame AND rotation unchanged
**Impact:** Eliminates vertex transforms and bounding box recalculation when animation is static

---

## Phase 1 Priority - COMPLETED

### 1. Animation Frame Caching - DONE (Phase 0)
**File:** `src/client/graphics/eq/animated_mesh_scene_node.cpp`
**Change:** Merged into Phase 0 - frame caching with `lastAppliedFrame_` tracking
**Impact:** Eliminates 100K+ vertex transforms when entities are idle

### 2. Entity Dirty List - DONE
**File:** `src/client/graphics/entity_renderer.cpp`
**Change:** Implemented `activeEntities_` set with two-pass updateInterpolation()
- First pass: update timeSinceUpdate for ALL entities (cheap)
- Second pass: only process activeEntities_ for interpolation
- Entities with velocity auto-added; deactivated when velocity becomes zero
- Player always stays in active set for equipment transforms
**Impact:** Reduces O(n) to O(moving entities) per frame

### 3. Distance-Squared Comparisons - DONE
**File:** `src/client/graphics/entity_renderer.cpp`
**Change:** Replaced `getDistanceFrom()` with `getDistanceFromSQ()` in updateNameTags()
```cpp
float renderDistanceSq = renderDistance_ * renderDistance_;
float distanceSq = cameraPos.getDistanceFromSQ(entityPos);
visual.meshNode->setVisible(distanceSq <= renderDistanceSq);
```
**Impact:** Eliminates sqrt for every entity visibility check

### 4. Texture Merge Caching - DONE
**Files:** `src/client/graphics/eq/mesh_building.cpp`, `include/client/graphics/eq/race_model_loader.h`
**Change:** Added `cachedMergedTextures_` and `mergedTexturesCacheValid_` flag
- Cache invalidated when zone textures change
- Returns cached result on subsequent calls
**Impact:** Saves 50-100ms per model load

---

## Phase 2 Priority - COMPLETED

### 5. Chat Text Caching - DONE
**File:** `src/client/graphics/ui/chat_window.cpp`
**Change:** Implemented `CachedWrappedMessage` struct with `wrappedLineCache_` vector
- Cache key: width + message count + timestamp setting
- Invalidation: on resize, channel filter change, new messages
**Impact:** Eliminates 2000+ font metric calls per frame

### 6. Pending Update Deduplication - DONE
**File:** `src/client/graphics/entity_renderer.cpp`, `include/client/graphics/entity_renderer.h`
**Change:** Changed `std::vector<PendingUpdate>` to `std::unordered_map<uint16_t, PendingUpdate>`
- Automatic deduplication - only keeps latest update per entity
**Impact:** Prevents redundant processing in busy scenes

### 7. HUD Dirty Tracking - DONE
**File:** `src/client/graphics/irrlicht_renderer.cpp`, `include/client/graphics/irrlicht_renderer.h`
**Change:** Added `HudCachedState` struct to track relevant HUD state
- Compares current state vs cached state before rebuilding
- Skips updateHUD() entirely if nothing changed
**Impact:** Eliminates string rebuilds when nothing changed

### 8. Inventory Slot Batching - DONE
**File:** `src/client/graphics/ui/item_slot.cpp`
**Change:** Replaced 4 separate border rectangles with single `draw2DRectangleOutline()` calls
- Background border: 4 draws → 1 draw
- Item border: 2 draws → 1 draw
- Highlight border: 4 draws → 2 draws (for 2px thickness)
**Impact:** Reduces 300+ draw calls to ~50

---

## Phase 3 Priority - COMPLETED

### 9. Spatial Partitioning for Entities - DONE
**File:** `src/client/graphics/entity_renderer.cpp`, `include/client/graphics/entity_renderer.h`
**Change:** Added grid-based spatial index with 500 EQ unit cells
- `spatialGrid_`: maps cell key → set of entity spawn IDs
- `entityGridCell_`: tracks which cell each entity is in
- `updateEntityGridPosition()`: updates entity's cell on position change
- `getEntitiesInRange()`: spatial query for entities near camera
- Used by `updateNameTags()` for O(1) visibility checks
**Impact:** O(1) visibility queries instead of O(n) for name tag updates

### 10. Animation Track Pointer Caching - DONE
**Files:** `src/client/graphics/eq/skeletal_animator.cpp`, `include/client/graphics/eq/skeletal_animator.h`
**Change:** Cache track pointers when animation changes
- `cachedTrackPtrs_`: vector of `TrackDef*`, one per bone
- `updateTrackPointerCache()`: rebuilds cache when animation starts
- `getBoneTransformAtFrame()`: uses cached pointers instead of map lookup
**Impact:** Eliminates map lookups per bone per frame

### 11. Lazy Texture Loading - DONE
**Files:** `src/client/graphics/eq/zone_geometry.cpp`, `include/client/graphics/eq/zone_geometry.h`, `src/client/graphics/eq/mesh_building.cpp`
**Change:** Load textures on first access via ZoneMeshBuilder
- `registerLazyTexture()`: stores texture info without loading to GPU
- `getOrLoadTexture()`: checks cache first, loads from pending if needed
- `hasTexture()`: checks if texture is registered (cached or pending)
- `mesh_building.cpp`: uses `getOrLoadTexture()` for cache-first texture lookup
**Impact:** Reduces memory usage, faster zone loads, deferred GPU texture creation

---

## Phase 4 Priority - COMPLETED

### 12. Consolidate Duplicate applyAnimation() - DONE
**File:** `src/client/graphics/eq/animated_mesh_scene_node.cpp`
**Change:** Consolidated two nearly-identical 150+ line vertex transformation implementations into single shared helper
- Created `applyBoneTransformsToMeshImpl()` static helper function
- Handles both multi-buffer and single-buffer modes
- Accepts optional rotation parameters for player vertex baking
- `EQAnimatedMesh::applyAnimation()` calls helper with no rotation (1.0/0.0)
- `EQAnimatedMeshSceneNode::applyAnimation()` calls helper with player rotation values
- Each caller retains its own early-exit checks and frame caching logic
**Impact:** ~170 lines of duplicated code removed, easier maintenance, identical behavior

---

## Implementation Order Checklist

### Phase 0 - COMPLETE
- [x] **Remove animation debug logging** (DONE - Phase 0)
- [x] **Skip redundant animation transforms** (DONE - Phase 0, includes frame caching)

### Phase 1 - COMPLETE
- [x] Animation frame caching (DONE - merged into Phase 0)
- [x] Entity dirty list (DONE - Phase 1)
- [x] Distance-squared comparisons (DONE - Phase 1)
- [x] Texture merge caching (DONE - Phase 1)

### Phase 2 - COMPLETE
- [x] Chat text caching (DONE - Phase 2)
- [x] Pending update deduplication (DONE - Phase 2)
- [x] HUD dirty tracking (DONE - Phase 2)
- [x] Inventory slot batching (DONE - Phase 2)

### Phase 3 - COMPLETE
- [x] Spatial partitioning (DONE - Phase 3)
- [x] Animation track caching (DONE - Phase 3)
- [x] Lazy texture loading (DONE - Phase 3)

### Phase 4 - COMPLETE
- [x] Consolidate duplicate applyAnimation() (DONE - Phase 4)

---

## Quick Wins (Under 30 minutes each)

1. ~~**Remove debug logging**~~ - DONE (Phase 0)
2. ~~**Distance-squared**~~ - DONE (Phase 1)
3. ~~**Animation frame check**~~ - DONE (Phase 0)
4. ~~**Texture merge cache**~~ - DONE (Phase 1)
5. ~~**Draw2DRectangleOutline**~~ - DONE (Phase 2)

---

## New Issues from Animation Overhaul (Jan 2026)

The following were introduced in the recent animation system merge:

| Issue | Severity | File | Status |
|-------|----------|------|--------|
| Debug TRACE logging | ~~HIGH~~ | animated_mesh_scene_node.cpp | **FIXED** - Removed |
| Redundant transforms | ~~MEDIUM~~ | animated_mesh_scene_node.cpp | **FIXED** - Frame caching added |
| Duplicate applyAnimation() | LOW | animated_mesh_scene_node.cpp | Pending - Code maintenance issue |
| Player vertex rotation | ACCEPTABLE | animated_mesh_scene_node.cpp | Required for bug fix |
| Animation blending | ACCEPTABLE | skeletal_animator.cpp | Time-bounded, well-designed |

---

*See `software_renderer_performance_analysis.md` for detailed analysis*
