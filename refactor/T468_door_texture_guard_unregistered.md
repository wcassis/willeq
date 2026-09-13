# T468 — the door texture eviction guard is registered only by an uncalled function (B590)

Class A (unconfirmed → confirm first), **no lock, no Pi**, slot3. Same defect
family as B520/B522–B524 (T460, T463 — read both Reviews and their tests:
`tests/test_software_cursor_eviction.cpp`,
`tests/test_environment_texture_eviction.cpp`,
`tests/test_texture_cache_holder_census.cpp`). Found by T467 (its Review's
census table, row `DoorManager`).

## The lead

`IrrlichtRenderer::loadGlobalAssets()` builds a `DoorManager` with the cache,
BSP, PVS, frustum and shaders AND calls `ensureDoorTextureGuard()` (T124) —
but has no caller anywhere (`src/ include/ tests/ tools/ benchmarks/ docker/`).
The live door construction paths are `setupInstantScene()` (nothing wired) and
`loadZoneStep7_Doors()` (all but the guard). So in production the door eviction
guard is never registered: a door texture evicted by LRU under the orangepi
budget, or removed by `ConstrainedTextureCache::clear()` at zone unload, may
leave door materials holding a freed `ITexture*` (the B523 use-after-free
shape) or a placeholder, depending on whether `clearTextureReferences()` reaches
the door nodes. Also a stale comment (T467 names it).

## Tasks

1. Trace every `DoorManager` construction/zone path and what the T124 guard
   does. Prove or refute with a pre-fix failing test on the real cache /
   EDT_NULL device (T463 seam): door meshes built for a zone, then LRU eviction
   and `clear()` + next-zone build; assert no door material holds a texture
   absent from the driver and none is left on the placeholder while its real
   texture is loadable.
2. Fix at the construction path the zone load actually uses: register the
   guard there (idempotently), and give door construction ONE wiring path the
   way T467 did for the environment managers (so `setupInstantScene()` cannot
   diverge). Decide what to do with `loadGlobalAssets()`: delete it if it is
   dead in full (prove per the zero-caller rule), or make it the single path.
   Update `TextureCacheHolderCensus`'s door row if its declared policy changes.
3. `setupInstantScene()`: establish whether it is reachable in production
   (which mode/flag) and what it needs; board what you do not fix.

## Proof

Standard verification bar (EXECUTOR_PROTOCOL); G-batch goldens for every TU
touched (zone load steps are pinned — reviewed edits update goldens in place
per G-correction 32, say so); visual gate (door goldens must not move); ARM;
file sizes. Board B620–B629; strike/narrow B590.

File territory: `src/client/graphics/door_manager*`, the zone-load door step
and `setupInstantScene()`, `ensureDoorTextureGuard()`, `loadGlobalAssets()`,
the census test row, new tests. **Not** camera rig / camera tests / hotkeys
(T466, slot1). **T458 (slot2) has been told to DEFER any row touching
`loadGlobalAssets()`, `ensureDoorTextureGuard()` or `DoorManager`** — those are
yours.

STOP: the fix needs a zone-lifecycle order change in
`renderer_zone_lifecycle.cpp` beyond registering the guard (report); an image
golden moves.

## Review

(executor fills)
