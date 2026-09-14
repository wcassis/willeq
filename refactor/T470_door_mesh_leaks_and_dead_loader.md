# T470 — DoorManager mesh leaks (B621); the uncalled synchronous zone loader (B622)

Class A/B, **no lock, no Pi**, slot3. Both found by T468 (read its Review in
`refactor/T468_door_texture_guard_unregistered.md` and its tests). The Orange
Pi has 483 MB; a per-zone mesh leak accumulates across zone changes in a
play session, so B621 is the priority half.

## B621 — `DoorManager` leaks meshes on two paths (read from code only)

Confirm each path with a pre-fix failing test on T468's seam (bare
`IrrlichtRenderer` + EDT_NULL device + real cache): count mesh references /
`IReferenceCounted` grabs and drops, or the driver's/scene manager's mesh
cache size, across build → rebuild → `clearDoors()` → next zone. Fix with the
ownership rule the rest of `DoorManager` uses (who grabs, who drops — cite
it). If a path is refuted, say so with the evidence. Check the same pattern in
`rebuildSingleDoor()`, placeholder door rebuilds and `doorMeshCache_`
replacement.

## B622 — `Application::loadZoneGraphics()` has no caller

The synchronous twin of `loadZoneGraphicsOnThread()`. Prove zero callers per
the protocol rule (`src/ include/ tests/ tools/ benchmarks/ docker/`), then
delete it and anything that only it reaches (census those, cascade until a
fixed point, the T458 way). Keep T464's `placeCameraForZoneIn()` call in the
live loader. Update `ZeroCallers` rows, `docs/module_map.md`, and the
`application` golden if it pins the deleted body (G-correction 32, in place,
say so).

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`application` is pinned); visual gate (no golden moves);
ARM; file sizes. Board B640–B649; strike/narrow B621/B622.

File territory: `src/client/graphics/door_manager*`,
`src/client/application_zone_loading.cpp` (the dead loader only),
`scripts/refactor/check_zero_callers.py` rows for those, door tests,
`docs/module_map.md`. **Not** `src/client/spell/` or the zone dispatch arms for
cast interrupts (T469, slot2), **not** camera rig / hotkeys (T466, slot1).

STOP: a leak fix needs a zone-lifecycle order change (report); an image golden
moves.

## Review

Slot3, branch `task/T470-door-mesh-leaks`, cut from `fable` `e4bc9619`. No
lock, no Pi, no server contact.

**STOP conditions:** neither was hit.
- No zone-lifecycle order changed. Every leak fix is local to `DoorManager`.
- No image golden moved.

### B621: CONFIRMED on both boarded paths, plus two it did not name

**Seam.** T468's: a bare `IrrlichtRenderer` bound to an EDT_NULL device and the
real `ConstrainedTextureCache`, using the `DoorManager` that `setupInstantScene()`
builds. A `MeshWatch` grabs the door mesh for the length of the test, so a
correctly freed mesh is never read after free, and reports
`getReferenceCount() - 1`: the holders other than the test.

**Irrlicht facts the counts rest on.**
- Every `ZoneMeshBuilder::build*Mesh()`, `createCubeMesh()` and
  `createOctagonalPrism()` does `new SMesh`, so it returns count 1, owned by
  the caller.
- `CMeshSceneNode`'s constructor calls `setMesh()`, which `grab()`s the new mesh
  and `drop()`s the old one. `~CMeshSceneNode` drops it.
- Nothing puts door meshes in the scene manager's mesh cache.

**Every leak path.** Holders other than the test, before the fix -> after.

| path | file:function | while drawn | after the node / entry is gone |
|---|---|---|---|
| placeholder from `registerDoor()`, used by every door of every zone load (Step 7 then rebuilds it through Path 1) | `door_manager_create.cpp:registerDoor` | 2 -> **1** | 1 -> **0** after `rebuildSingleDoor()` destroys the node |
| real mesh built while a texture is pending (`findDoorMesh()` does not cache it) | `door_manager_mesh.cpp:findDoorMesh`, callers `createDoor` / `buildDoorMesh` / `rebuildSingleDoor` | 2 -> **1** | 1 -> **0** after `clearDoors()` |
| `invalidateMeshCache(name)`, `(lowercase name)` and `("")` | `door_manager_mesh.cpp:invalidateMeshCache` | 2 (node + cache) | 2 -> **1** after the invalidate, 1 -> **0** after `clearDoors()` |
| placeholder from `createDoor()` (`IrrlichtBridge`'s live door spawn), swapped by Path 2's `setMesh()` | `door_manager_create.cpp:createDoor`, `door_manager_rebuild.cpp:rebuildSingleDoor` | 2 -> **1** | 1 -> **0** after the swap |
| shared cached mesh: 2 `createDoor` + 1 `registerDoor`/rebuild of `DOOR1` | cache-hit path | 4 (3 nodes + cache), unchanged | 0 at `unloadZone()`, unchanged: the green guard against over-dropping |

- Rows 2 and 3 are B621's two boarded paths.
- Rows 1 and 4 are new. Row 1 is the largest: one placeholder mesh per door,
  every zone. Neither was in B621 because the callers' failure branches
  already dropped the placeholder, and only the success paths leaked.
- `buildDoorMesh()` and `rebuildPlaceholderDoors()` have the same shapes as
  `createDoor()` and Path 2, and got the same fix. `rebuildPlaceholderDoors()`
  has no production caller (B494).

### The fix: one ownership rule

The rule is stated on `doorMeshCache_` in `door_manager.h`.
- **Before.** The only ownership statements were "Cache holds the reference,
  scene node will grab() on use" (`findDoorMesh`) and "Drop cached meshes —
  scene nodes hold their own grab() references" (`clearDoors`). In practice the
  cache BORROWED the creation reference, and callers dropped only on failure
  branches (`if (usePlaceholder) mesh->drop()`). So on every success path, a
  mesh the cache did not hold kept its creation reference forever.
- **After.** `findDoorMesh()` and `createPlaceholderMesh()` return ONE
  caller-owned reference, cached or not; a cache hit `grab()`s for the caller.
  The caller drops it right after `addMeshSceneNode()` or `setMesh()` succeeds,
  and on every failure branch. The cache takes its own `grab()` in the new
  private `cacheDoorMesh()`, and releases it in `invalidateMeshCache()` (both
  forms) and `clearDoors()` (unchanged).
- **Sites.**
  - `createDoor`, `registerDoor`, `buildDoorMesh`: one drop after the node
    exists, and unconditional drops on the failure branches.
  - `rebuildSingleDoor` Path 2 and `rebuildPlaceholderDoors`: a drop after
    `setMesh()`.
  - `findDoorMesh`'s three cache inserts go through `cacheDoorMesh()`.
- **No holder read a freed mesh.** No door mesh pointer is kept outside the
  node and the cache: `computeDoorCollision`, `DoorTextureGuard` and the
  diagnostics swap all read `sceneNode->getMesh()` live. The whole suite,
  including T468's eviction and unload cases, is green on the fix.

### B622: deleted, cascade to a fixed point

- **Zero callers.** Grepped `src/ include/ tests/ tools/ benchmarks/
  docker/ scripts/`: only the definition, the declaration, a count in
  `test_camera_zone_in_preference.cpp` and prose comments. The full `build/`
  tree, tools included, links without it.
- **Deleted:** `Application::loadZoneGraphics()`
  (`application_zone_loading.cpp`, 109 lines, now a tombstone) and its
  declaration (`application.h`).
- **Cascade step 1.** Every call it made is also made elsewhere:
  - by `loadZoneGraphicsOnThread()`: `setZoneLineBoundingBoxes`,
    `setupInstantScene`, `storeZoneEnvironment`, `registerEntity`,
    `registerDoor`, `placeCameraForZoneIn`, `SetPlayerGraphicsEntityPending`;
  - by `checkLoadingComplete()`: `getZoneBspTree`, `m_pendingBspTree*`,
    `m_graphicsCompleteReady`;
  - by `application.cpp`: `CreateZoneLoadSnapshot`.

  The one exception is `IrrlichtRenderer::isProgressiveLoadingActive()`, whose
  only production caller was the deleted loader. My first census missed it;
  `ZeroCallers` caught it in the suite ("NEW tests-only function"). It is
  deleted too, and its two tests (`test_zone_load_component`,
  `test_zone_stream_component`) read `Context().progressiveLoadingActive`, the
  same bound member, so both still assert what they asserted.
- **Cascade step 2.** `progressiveLoadingActive_` has many live readers
  (`renderer_frame.cpp`, `renderer_zone_stream.cpp`, `renderer_sim_bridge.cpp`,
  `renderer_entities.cpp`). Fixed point.
- **Kept:** T464's `placeCameraForZoneIn()` call in the live loader.
  `CameraZoneInSourceTest` now counts 1.
- **`check_zero_callers.py`: no row changed.** Neither deleted function had
  an exemption row, and no new exemption was added.
- **Stale citations fixed.**
  - `zone_lifecycle_component.h`'s forwarder caller lines (`:191`/`:206, 316`/
    `:210, 320` were already stale; now `:206`/`:221`/`:225`).
  - "called twice" in `test_zone_lifecycle_component.cpp` (two comments).
  - `test_corpse_death_pose.cpp`'s TWICE note.
  - `docs/module_map.md`: the `application_zone_loading.cpp` row, plus three
    door rows for line counts and `cacheDoorMesh`.

### Tests, red before and green after

Red build: `e4bc9619` with `src/` unmodified, test file only.

| test | unfixed | fixed |
|---|---|---|
| `DoorTextureEvictionTest.ARegisteredDoorsPlaceholderMeshDiesWithItsNode` | **FAILED**: holders 2 (want 1), then 1 (want 0) | OK |
| `DoorTextureEvictionTest.AMeshBuiltWhileItsTextureIsPendingDiesWithItsDoor` | **FAILED**: 2 / 1 | OK |
| `DoorTextureEvictionTest.InvalidatingTheDoorMeshCacheReleasesTheCachesReference` | **FAILED** x3 keys: 2 (want 1), then 1 (want 0) | OK |
| `DoorTextureEvictionTest.ACreatedDoorsPlaceholderDiesAtTheMeshSwap` | **FAILED**: 2 / 1 (the swapped-in mesh's 2 and 0 held) | OK |
| `DoorTextureEvictionTest.DoorsSharingACachedMeshReleaseItExactlyOnceAtZoneChange` | OK (the over-drop guard) | OK |
| T468's four cases in the same binary | OK | OK |
| `ZeroCallers` (tests-only half) | n/a | **FAILED** on the first full run (`isProgressiveLoadingActive`), then OK after the cascade step |

**Unchanged neighbours, green on the fix:** `test_camera_zone_in_preference`
5/5, `test_zone_lifecycle_component` 14/14, `test_door_loading` 4/4,
`test_door_collision` 19/19, `test_zone_load_component` 7/7,
`test_zone_stream_component` 14/14.

### Verification (builds strictly in sequence)

Every run wrote its own exit file under a unit-private scratch directory, and
every wait reported a non-zero elapsed time. There was no exit 143 anywhere in
the unit.

| step | result |
|---|---|
| red build (`--target test_door_texture_eviction`, `src/` unmodified) | exit 0; 4 of the 5 new cases FAILED, as tabled above |
| 1 `cmake --build build -j24` (whole tree, tools included) | **PASS**, exit 0, on the final tree (after the cascade step) |
| 2 `ctest -j6` | **PASS**: `100% tests passed, 0 tests failed out of 4748`, exit 0. Includes `ZeroCallers`, `FileSizeCeiling`, `DocCitations`, `TextureCacheHolderCensus`, the nine door cases and `visual_gate`. The first full run was 4747/4748 with `ZeroCallers` red; that finding is the cascade step above |
| G-batch goldens | **PASS**. No golden pins a touched TU (see deviation 4); `git diff fable -- tests/goldens` is empty |
| 3 `build-gles2-x86.sh` | **PASS**, exit 0; re-run after the final main build and before the final `ctest` |
| 4 visual gate | **PASS** (`visual_gate` 28.9 s inside the final suite, against the rebuilt `build-gles2`); no image golden moved |
| 5 `build-arm-noble.sh` | **PASS**, exit 0; `build-arm-noble/bin/willeq` is a fresh 32-bit ARM ELF |
| 6 `check_file_sizes.sh fable` | **PASS**: 27 ok, 0 warn, 0 fail. The count includes files T466 changed, because local `fable` moved to `4f94ba16` during the unit. Largest file I touched: `door_manager_create.cpp` 488 |

**`fable` moved during the unit** (`e4bc9619` -> `4f94ba16`: T466 merged,
plus T471/T472 plans).
- The only file both sides changed is `refactor/BOARD.md`, in different
  ranges.
- `git grep` on the new tip finds no new caller of `loadZoneGraphics()`,
  `isProgressiveLoadingActive()` or `placeCameraForZoneIn(`: the counts are the
  same as on `e4bc9619`.
- Not rebased; the orchestrator merges.

### Board
- **B621** struck: confirmed, with the counts above and the two extra paths.
- **B622** struck: deleted, with the two-step cascade.
- **B640** (A, *unconfirmed live*): a door whose real mesh was built while its
  texture was pending is never re-textured.
  - `rebuildSingleDoor()` has no branch for `meshBuilt=1, usePlaceholder=0`.
  - Measured with a throwaway probe on this seam, deleted before the commit:
    after the texture's data arrived and `invalidateMeshCache()` ran, the call
    returned `rebuilt=0`, the mesh was unchanged, and the node texture was
    `nullptr` before and after.
  - Outside this plan's scope, so boarded.
- B641–B649 unused.

### Deviations (flagged)
1. **`tests/test_camera_zone_in_preference.cpp` is T466's camera-test
   territory.** The deletion forces the one-line count change (2 -> 1) plus a
   comment. The test name is unchanged, and nothing about the camera rig is
   touched.
2. **Files outside the named territory, each required by the cascade:**
   - `include/client/graphics/irrlicht_renderer.h` (`isProgressiveLoadingActive`
     deleted);
   - `tests/test_zone_load_component.cpp` and
     `tests/test_zone_stream_component.cpp` (read the context member);
   - `include/client/application.h`;
   - `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h`,
     `tests/test_zone_lifecycle_component.cpp` and
     `tests/test_corpse_death_pose.cpp` (comments only);
   - `refactor/BOARD.md`.
3. **The refcount cases live in `test_door_texture_eviction.cpp`**, reusing
   T468's fixture, not in a new binary. That avoids a second ~285 MB test link
   and a copy of the zone/BMP helpers.
4. **The `application` golden does not pin `application_zone_loading.cpp`.**
   `application_init_order_g04.txt` pins `Application::initialize()`, and no
   golden test reads the loader TU or any door TU, so nothing was learned in
   place. G-correction 32 did not apply.
5. **No live proof** (no lock assigned). The leaks are proven on the real
   `DoorManager` and the real Irrlicht reference counts, not measured as RSS on
   the Pi.
