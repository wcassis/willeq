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

Slot3, branch `task/T468-door-texture-guard`. It started from `fable` `e4ca9374` and
was rebased onto `7138a22d` (T458) before final verification, on the
coordinator's instruction. No lock, no Pi, no server contact.

**STOP conditions:** neither was hit.
- `renderer_zone_lifecycle.cpp` gained a construction body and a call in
  `setupInstantScene()`. No step of `unloadZone()` moved.
- No image golden moved.

### Verdict: CONFIRMED for LRU eviction (a use-after-free), REFUTED for zone unload

**Every door construction path before T468.** Grepped `src/ include/ tests/
tools/ benchmarks/ docker/`.

| site | wiring | reached |
|---|---|---|
| `ZoneLifecycleComponent::setupInstantScene()` | none: no cache, no guard | every session, via `Application::loadZoneGraphicsOnThread()` (the loading thread). Built first, so it is the manager a session uses |
| `ZoneLoadComponent::loadZoneStep7_Doors()` | cache, zone, BSP, PVS, frustum, neighbours, shaders; no guard | only when no manager exists, i.e. never after the instant scene |
| `IrrlichtRenderer::loadGlobalAssets()` | all of the above plus `ensureDoorTextureGuard()` (T124) | **no caller anywhere**, and it was also the only caller of `ensureDoorTextureGuard()` outside `test_zone_stream_component.cpp` |

Step 1 (`loadZoneStep1_S3D`) later gave the instant-scene manager the zone,
the cache and the shader types, and Step 2 the BSP, PVS region, frustum culler
and neighbours. So doors were built fully textured, but no `DoorTextureGuard`
ever listened to the cache.

**What happens to door textures, before T468.**

*LRU eviction (`evictTexture()`).* Confirmed use-after-free.
- `DoorManager` detaches a PVS-hidden door's `pivotNode` from the scene graph
  (`setDoorState()`, `update()`, `rebuildSingleDoor()`, `createDoor()` and
  `buildDoorMesh()` at insertion).
- `ConstrainedTextureCache::clearTextureReferences()` walks from the root, so it
  cannot reach the detached door. `driver_->removeTexture()` then frees a
  texture that is still in two places:
  - the door node's material copy;
  - the mesh buffer of `DoorManager::doorMeshCache_`'s shared mesh.
- The door draws freed memory when PVS shows it again.
- A door that is in the graph is swapped to the placeholder by the walk (no
  hazard).

*Zone unload (`unloadZone()` -> `ConstrainedTextureCache::clear()`).* No hazard.
- `unloadZone()` calls `DoorManager::clearDoors()` before `clear()`.
- `clearDoors()` re-attaches every detached pivot, `remove()`s and `drop()`s
  every door node, and drops the whole door mesh cache.
- So no door material survives into `clear()`. The next zone's doors build
  against fresh uploads of the same names.

**After T468.**
- *LRU:* the registered `DoorTextureGuard` walks `doors_` directly, so a hidden
  door's node material and its mesh buffer both get the placeholder, never the
  freed pointer.
- *Unload:* unchanged. Nothing is left to guard.
- *Placeholder after an LRU eviction:* persists for the rest of the zone, like
  every other zone-geometry holder (`RegionMeshTextureGuard`'s own WARN says
  "until re-uploaded"; nothing re-uploads). Boarded as **B620** rather than
  given a door-only reload.

### The fix

- **One construction path.** New `ZoneLifecycleComponent::ensureDoorManager()`
  (public, `renderer_zone_lifecycle.cpp`) is idempotent.
  - It builds `ctx_.doorManager` if absent, wired with everything the context
    holds at that moment. That is the union of Step 7's wiring and
    `loadGlobalAssets()`'s.
  - It always ends with `ctx_.ZoneStream().ensureDoorTextureGuard()`.
- **Both callers use it.** `setupInstantScene()` and `loadZoneStep7_Doors()`
  now call it and construct nothing themselves.
- **Per-zone wiring unchanged.** Steps 1 and 2 still re-apply their values.
  - The instant-scene manager is now also given the frustum culler and the
    shader types early.
  - Neither is read before Step 2 or Step 7 in a load: placeholder meshes do not
    use shader types, and the frame loop does not run while the loading thread
    owns the GL context.
  - The visual gate confirms no pixel moved.
- **`loadGlobalAssets()`: DELETED**, not made the single path.
  - It had zero callers in `src/ include/ tests/ tools/ benchmarks/ docker/`,
    and the full build (tools included) links without it.
  - Its other duties were already gone: "no eager file I/O here", and the sky
    and detail managers are created in `initLoadingScreen()`.
  - Its `globalAssetsLoaded_` flag and its startup comment in
    `irrlicht_renderer.h` went with it.
  - The `("IrrlichtRenderer", "loadGlobalAssets")` row in
    `scripts/refactor/check_zero_callers.py` was removed. Otherwise
    `ZeroCallers` fails on a stale exemption.
- **`setupInstantScene()`** is reachable in production through
  `loadZoneGraphicsOnThread()`, on every zone load of a graphics session. It
  needs only the device pointers, plus the cache for the guard. Its twin, the
  synchronous `Application::loadZoneGraphics()`, has no caller: boarded
  **B622**.
- **Census.** `TextureCacheHolderCensus`'s `zone_geometry.cpp` row
  (ZoneScoped) now covers door meshes too, and gains the marker
  `ctx_.ZoneStream().ensureDoorTextureGuard()` in
  `renderer_zone_lifecycle.cpp`. The policy is unchanged, so no new row was
  needed: door textures come through `ZoneMeshBuilder::loadTextureFromBMP()`.
- **Stale comments fixed:**
  - `OverlaysComponent::applyEnvironmentalDisplaySettings()` (ParticleManager is
    created in `initLoadingScreen()`);
  - the `ensureDoorTextureGuard()` caller comment (`renderer_zone_merge.cpp`);
  - `DoorTextureGuard`'s lifetime note (`renderer_zone_merge.h`);
  - `zone_stream_component.h`;
  - `test_zone_stream_component.cpp`;
  - Step 7's trailing comment;
  - `docs/module_map.md` (three rows).

### Tests, red before and green after

**Seam.** T467's bare `IrrlichtRenderer` bound to an EDT_NULL device and the
real `ConstrainedTextureCache`. The door manager is built by the renderer's own
`setupInstantScene()`. The test then does Step 1's `setZone` + cache and
Step 7's `registerDoor` + `rebuildSingleDoor`, with a real 24-bit BMP door
texture decoded and uploaded through the cache.

**What "freed" means.** The material still names the evicted pointer AND the
driver no longer lists it. The first red run left a false edge: a filler
texture allocated after the eviction reused the address. The fixture now
allocates every filler before registering any, and the rerun proved the free.

**Red builds.** Built on `e4ca9374` with `src/` unmodified (test files only).

| test | unfixed | fixed (rebased) |
|---|---|---|
| `DoorTextureEvictionTest.AHiddenDoorNeverHoldsAFreedTextureUnderBudgetPressure` | **FAILED x3**: node material, mesh buffer, and node after re-show all `0x642667728da0 ... DANGLING (freed)` | OK |
| `DoorTextureEvictionTest.AVisibleDoorNeverHoldsAFreedTextureUnderBudgetPressure` | OK (scene walk reaches it; pins no change there) | OK |
| `DoorTextureEvictionTest.DoorsDrawTheirRealTextureAfterAZoneUnloadAndTheNextZoneBuild` | OK: **the refutation of the unload half** | OK |
| `DoorManagerConstructionSource.TheDoorManagerHasOneConstructionSite` | **FAILED**: 3 sites (`renderer_init.cpp:707`, `renderer_zone_lifecycle.cpp:626`, `renderer_zone_load_objects.cpp:471`) | OK |
| `TextureCacheHolderCensus.EveryDeclaredPolicyHasItsMarker` | **FAILED**: the door-guard marker is absent | OK |
| `TextureCacheHolderCensus.EveryGetOrLoadSiteDeclaresALifetimePolicy` | OK | OK |

**Unchanged neighbours, green after the fix:**
`test_zone_stream_component` 14/14, `test_zone_lifecycle_component` 14/14,
`test_zone_load_component` 7/7, `test_t124_texture_holder_guards` 7/7,
`test_environment_texture_eviction` 8/8.

### Verification (all on the rebased branch, builds strictly in sequence)

| step | result |
|---|---|
| 1 `cmake --build build -j24` | **PASS**, exit 0 (own exit file; no exit 143 anywhere in the unit) |
| 2 `ctest -j6` | **PASS**: `100% tests passed, 0 tests failed out of 4743`, exit 0; includes `ZeroCallers`, `FileSizeCeiling`, `DocCitations`, the census and the four new cases |
| G-batch goldens | **PASS**. No touched TU is pinned: none of `tests/goldens/*.txt` names `renderer_zone_lifecycle`, `renderer_zone_load_objects`, `renderer_init` or the door manager. All golden cases passed in the suite, and nothing needed learning in place |
| 3 `build-gles2-x86.sh` | **PASS**, exit 0 (run after the main build finished, per T458's protocol note) |
| 4 visual gate | **PASS** (`visual_gate` passed inside the `-j6` suite against the rebuilt `build-gles2`, 24.9 s); `tests/goldens/` unchanged vs `7138a22d` |
| 5 `build-arm-noble.sh` | **PASS**, exit 0; `build-arm-noble/bin/willeq` rebuilt, ARM ELF |
| 6 `check_file_sizes.sh fable` | **PASS**: 12 ok, 0 warn, 0 fail. Largest touched: `irrlicht_renderer.h` 1,191 (down from 1,195), `renderer_zone_lifecycle.cpp` 806 |

### Board
- **B590** struck, with the evidence above. B591-B599 were already unused.
- **B620** (B, unconfirmed live): a zone-geometry texture evicted by the LRU
  stays the placeholder for the rest of the zone, and `doorMeshCache_` hands
  that placeholder to later doors of the same model.
- **B621** (B, hygiene, unconfirmed): `DoorManager` leaks meshes. The
  pending-texture mesh from `findDoorMesh()` is never dropped, and
  `invalidateMeshCache()` erases without `drop()`.
- **B622** (B, unreached): `Application::loadZoneGraphics()` has no caller.
- **B606**'s mention of `loadGlobalAssets` notes the deletion. B623-B629
  unused.

### Deviations (flagged)
1. **Rebased onto `7138a22d`, not re-cut from the current `fable` tip.** The
   tip is `6905c80f`, which adds a T469 plan and a protocol paragraph on top
   of `7138a22d`. The instruction named `7138a22d`. Neither extra commit
   touches code, and its protocol rule (builds strictly in sequence) was
   followed.
2. **Files outside the named territory**, each required by the change:
   - `scripts/refactor/check_zero_callers.py` (the deleted function's exemption
     row, as the coordinator directed);
   - `src/client/graphics/overlays/overlays_hud_env.cpp` (one comment line, the
     stale comment the plan names);
   - `include/client/graphics/irrlicht_renderer.h`;
   - `include/client/graphics/renderer_zone_merge.h`;
   - `include/client/graphics/zone_stream/zone_stream_component.h`;
   - `src/client/graphics/zone_stream/renderer_zone_merge.cpp` (comments);
   - `tests/test_zone_stream_component.cpp` (comment);
   - `tests/cmake/renderer_guards.cmake` (one new block after T467's, away from
     T466's camera entries);
   - `docs/module_map.md`.

   Nothing in the camera rig, camera tests or hotkeys was touched.
3. **Log text changed.** The instant scene's "Door manager created for instant
   scene (placeholder cubes only)" and Step 7's DEBUG "created DoorManager
   (...)" are now one INFO line: "Door manager created (zone=..., cache=...,
   ...)". Nothing in the tree greps for either old string.
4. **No live proof.** No lock was assigned, and P13/P13b measured zero LRU
   evictions on the Pi, so the use-after-free may be unreachable at today's
   budget (B620 names the measurement). The defect is proven on the real cache
   and the real door manager, not in a session.
5. **`docs/plans/dynamic_tree_movement_plan.md` and
   `detail_system_implementation_plan.md` still name `loadGlobalAssets()`.**
   Both are historical plans (`docs/CLAUDE.md`), and neither was edited.
