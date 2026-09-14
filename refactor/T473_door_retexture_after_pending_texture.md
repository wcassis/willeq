# T473 — a door whose mesh was built while its texture was pending is never re-textured (B640)

Class A (unconfirmed live), **no lock, no Pi**, slot3. Found by T470 (read its
Review, `refactor/T470_door_mesh_leaks_and_dead_loader.md`, and T468's; reuse
their seam in `tests/test_door_texture_eviction.cpp`). The Pi loads textures
under a tight budget and asynchronously, so a door built before its texture
lands is the likely case there, not an edge case.

## The lead

`DoorManager::findDoorMesh()` can build a real (non-placeholder) mesh while the
door's texture is still pending, and deliberately does not cache it (T470 row 2).
When the texture arrives, `rebuildSingleDoor()` has no branch for a door that
already has a real mesh — T470's throwaway probe measured `rebuilt=0`, mesh
unchanged, texture `nullptr` before and after. Result: that door draws
untextured (or with whatever the null texture binds to on GLES2) for the rest of
the zone.

## Tasks

1. Establish the texture-arrival path for doors: what signals a pending door
   texture has landed (the zone texture merge / `renderer_zone_merge`,
   `ZoneMeshBuilder::loadTextureFromBMP`, async upload results), and which
   door rebuild entry points it calls. Cite each hop.
2. Pre-fix failing test on the T468/T470 seam: build a door with its texture
   pending, deliver the texture through the real arrival path, assert the door's
   node material and the mesh it draws carry the real texture (and the mesh is
   then cached per T470's ownership rule).
3. Fix in the rebuild path (re-texture or rebuild a real-mesh-without-texture
   door), respecting T470's reference ownership (`cacheDoorMesh()`, caller-owned
   returns) and T468's guard. Add a refcount assertion so the fix cannot
   reintroduce a leak.
4. Census: other zone-geometry holders that can be built before their texture
   arrives and never re-textured (object/placeable meshes, region meshes — B620
   is the eviction sibling); board what you find (B670–B679), fix only doors.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
touched TUs; visual gate (door goldens must not move — if one moves because a
door now textures correctly, STOP and report, Will blesses); ARM; file sizes.
Board B670–B679; strike/narrow B640.

File territory: `src/client/graphics/door_manager*`, the door texture-arrival
hook, door tests. **Not** the camera component / hotkeys / `/camera` (T471,
slot1), **not** entity/equipment attachment (T472, slot2).

STOP: the fix needs a zone-lifecycle order change (report); an image golden
moves.

## Review

Slot3, branch `task/T473-door-retexture`, cut from `fable` `adf1985d`. No lock,
no Pi, no server contact.

**STOP conditions:** neither was hit. No production code changed, so no
zone-lifecycle order moved and no image golden moved.

### Verdict: the live defect is REFUTED; the code gap is real but unreachable

**Task 1: the texture-arrival path, hop by hop.**

| # | hop | site |
|---|---|---|
| 1 | A door texture resolves null while the mesh is built, so it is recorded as missing ("async GPU upload still pending", says the comment) | `ZoneMeshBuilder::buildTexturedMesh()` (`zone_geometry.cpp`) -> `missingTextures_`; `DoorManager::findDoorMesh()` copies them into `lastMissingTextures_` and does not cache the mesh |
| 2 | Step 7 files the door under each missing name | `ZoneLoadComponent::loadZoneStep7_Doors()` (`renderer_zone_load_objects.cpp`) -> `ZoneLifecycle().pendingTextureDoors()[tex].insert(doorId)` |
| 3 | A GPU upload result of type `Texture`/`CompressedTexture` with `callbackKey >> 56 == 3` arrives: it is registered in the cache, and the door arm calls `invalidateMeshCache(modelName)` and pushes the id to `doorTextureRebuildQueue` | `ZoneLifecycleComponent::processCompletedUploads()` (loading screen) and `processOneGPUResult()` (GREEN frames), `renderer_gpu_upload.cpp`, both driven from `FrameComponent` (`renderer_frame.cpp`) |
| 4 | The queue is drained one door at a time into `DoorManager::rebuildSingleDoor()` | `renderer_frame.cpp` Priority 8; `ZoneStreamComponent` P1.6 (`renderer_zone_stream.cpp`) |
| 5 | `rebuildSingleDoor()`: Path 1 needs `!meshBuilt && usePlaceholder`, Path 2 needs `usePlaceholder`. A real-mesh door has `meshBuilt=1, usePlaceholder=0`, so it returns false | `door_manager_rebuild.cpp` |

**Hop 3 has no producer, so hops 3-5 never run for any texture.**
- Only one producer ever set the key `uint64_t(3) << 56`:
  `ConstrainedTextureCache`'s async upload queue. It also did
  `pendingAsyncUploads_.insert(...)`.
- Batch T's T04 (`aaee58d1`, 2026-03-16) wrapped that queue in
  `#if 0 // T04: Removed — replaced by getOrLoad() + uploadTexture()`, when it
  made texture loading synchronous (`git show aaee58d1 --
  src/client/graphics/constrained_texture_cache.cpp`).
- T326 (`77c842b4`) deleted the block (its diff shows the two removed lines,
  twice).
- **Today.** Grepping `src/ include/ tools/ tests/ benchmarks/ docker/` finds
  only two `GPUUploadThread::submit()` callers:
  - `TextureAtlas`: a `CompressedTexture` whose key is
    `atlasType << 32 | page`, so high byte 0;
  - `rebuildRegionMesh`: `VertexBuffer`, a type the texture arm does not
    take.
- No `<< 56` anywhere, and nothing inserts into `pendingAsyncUploads_`.
- `ConstrainedTextureCache::setGPUUploadThread()` stores a pointer nothing
  reads.

**So B640's lead does not describe a live path.** The cache is synchronous:
`getOrLoad()` decodes or finds the EQE1 entry, and `uploadTexture()` promotes it
on the same call (`ZoneMeshBuilder::loadTextureFromBMP()`). A texture missing at
Step 7 is missing for a reason that never "arrives":
- the budget is exhausted in `uploadTexture()`;
- the decode fails;
- the name is absent from the zone's texture map;
- or the pixel data was released and the texture is no longer cached.

What a player can see, a door built without its texture staying that way, is
boarded as **B671** (unconfirmed live).

**The code gap, re-measured rather than quoted.** A throwaway probe on T470's
seam, deleted before the commit, built a door with its texture data empty and
not cached.
- **Setup.** It restored the bytes, then ran the arrival arm's two door calls,
  exactly as `renderer_gpu_upload.cpp` makes them.
- **Printed:**
  ```
  PROBE before: meshBuilt=1 usePlaceholder=0 missing=1 holders=1 nodeTex=(nil)
  PROBE after: rebuilt=0 sameMesh=1 holders=1 nodeTex=(nil)
  ```
- **Result.** T470's measurement holds: `rebuildSingleDoor()` cannot
  re-texture that door, even when the bytes are back.

### Tasks 2 and 3: no fix, and why

- **Why no fix.** The re-texture branch the plan asks for would be reached only
  from hop 3, which cannot fire. Adding it would ship an
  implemented-but-unreached branch, the defect class the board exists to
  remove.
- **No red-before test.** The plan's test ("deliver the texture through the
  real arrival path") cannot be written truthfully, for two reasons:
  - that path's producer does not exist;
  - its consumer needs a live `GPUUploadThread`. `processOneGPUResult()`
    returns false without one, which `test_zone_lifecycle_component.cpp`
    already pins.
- **A tripwire instead.**
  `DoorTextureArrivalSource.NoTextureUploadIsSubmittedWithTheArrivalKey`
  (`tests/test_door_texture_eviction.cpp`) scans `src/` and `include/` for the
  producer's two signatures: a `<< 56` shift, or a `pendingAsyncUploads_`
  writer.
  - It fails the moment an async texture producer returns.
  - Its message says that B640 is then live, and that `rebuildSingleDoor()`
    needs a real-mesh re-texture branch under T470's ownership rule.
- **Refcount evidence.** No ownership code changed.
  - All five of T470's refcount cases and T468's three eviction cases are green
    on this branch.
  - The probe's `holders=1` before and after shows the arrival calls neither
    leak nor over-drop the uncached real mesh: the node is its one holder, and
    the cache has none.

### Task 4: census of sibling holders

| holder | recorded when its texture is missing | re-texture consumer | reachable? |
|---|---|---|---|
| doors, Step 7 | `pendingTextureDoors_` | `doorTextureRebuildQueue` -> Priority 8 / P1.6 -> `rebuildSingleDoor()`, which is a no-op for a real mesh | recorded yes; drained **never** (B670); no branch (B640) |
| doors, `createDoor()` (IrrlichtBridge live spawn) and `buildDoorMesh()` | **not recorded** | none | reachable; untextured for the zone (B671) |
| PVS objects, Step 6 and `buildDeferredObject()` | `pendingTextureObjects` | `objectTextureRebuildQueue` -> Priority 9 / P1.7, which removes the node and rebuilds it (would re-texture) | recorded yes; drained **never** (B670) |
| region meshes, `rebuildRegionMesh()` | `pendingTextureRegions_` | `textureRebuildQueue_`: its consumer was removed by T67, so it is drained only at `unloadZone()` | dead at both ends (B670) |
| entity meshes | `registerUploadedTexture()` in the same arm | same arm | never (B670) |
| anything evicted by the LRU | not recorded | none ("until re-uploaded") | B620, unchanged |

### Tests, red before and green after

| test | state | result |
|---|---|---|
| `DoorTextureArrivalSource.NoTextureUploadIsSubmittedWithTheArrivalKey`, on the committed tree | no producer | OK |
| the same, with a temporary `src/client/graphics/t473_mutation_probe_tmp.cpp` carrying the deleted producer's two lines (not in any target; deleted before the commit) | producer present | **FAILED**, naming both lines and B640 |
| the other nine cases in `test_door_texture_eviction` (T468 x4 incl. the construction-site source test, T470 x5) | unchanged | OK |

There is no red-before case for a fix because there is no fix (see above). The
mutation run is the red half for the pin.

### Verification (builds strictly in sequence)

Every step wrote its own exit file under a unit-private scratch directory
(`scratchpad/T473/`, a fresh label per run), and every wait reported a non-zero
elapsed time. There was no exit 143 anywhere in the unit.

| step | result |
|---|---|
| probe build (`--target test_door_texture_eviction`, with the throwaway case) | exit 0 (90 s); probe output and pin red under mutation, as above |
| 1 `cmake --build build -j24` (final tree, probes removed) | **PASS**, exit 0 (90 s) |
| 3 `build-gles2-x86.sh` (after step 1's exit file) | **PASS**, exit 0 (400 s) |
| 2 `ctest -j6` (after step 3, so `visual_gate` read the fresh `build-gles2`) | **PASS**: `100% tests passed, 0 tests failed out of 4762`, exit 0. Includes the new pin, `ZeroCallers`, `FileSizeCeiling`, `DocCitations`, both `TextureCacheHolderCensus` cases and `visual_gate` (42.1 s) |
| G-batch goldens | **PASS**. No production TU touched, so none is pinned; `git diff fable -- tests/goldens` is empty |
| 4 visual gate | **PASS** inside the suite; no image golden moved |
| 5 `build-arm-noble.sh` | **PASS**, exit 0 (696 s); `build-arm-noble/bin/willeq` is a fresh 32-bit ARM EABI5 ELF |
| 6 `check_file_sizes.sh fable` | **PASS**: 1 ok, 0 warn, 0 fail (`tests/test_door_texture_eviction.cpp` 558) |

### Board
- **B640** narrowed from A to **B (unreached)**. The live half is refuted with
  the evidence above, and the entry now names its tripwire. It stays open
  because it closes with B670: delete the arm, or restore a producer and add
  the branch.
- **B670** (B, unreached): the GPU texture-arrival arm and all of its
  downstream queues have had no producer since `aaee58d1`. It lists every dead
  member and needs a decision: delete, or revive.
- **B671** (A, *unconfirmed live*): a zone-geometry mesh built without its
  texture stays untextured, and the `createDoor()` / `buildDoorMesh()` paths do
  not even record it. Includes the Pi log grep that would confirm it.
- B672–B679 unused.

### Deviations (flagged)
1. **No production fix, against tasks 2-3 as written.** The plan assumed a
   reachable arrival path. The re-texture branch would have been reached only
   from a producer that does not exist, so I refuted the defect and pinned
   the refutation instead. The branch is owed if and when B670 is resolved by
   reviving the producer.
2. **The plan's pre-fix test (deliver through the real arrival path) was not
   written.** Its producer does not exist, and its consumer needs a live EGL
   upload thread. It is replaced by the source tripwire, demonstrated red
   under mutation, and by a throwaway behavioural probe whose output is
   quoted above.
3. **B671 is boarded as class A, unconfirmed.** The visual gate runs
   `gles2_zone_benchmark`, not the client's Step 6/7, and no lock was
   assigned, so nobody measured whether a door texture is ever missing at
   build time in a real session.
4. **Temporary files.** The throwaway probe case and
   `src/client/graphics/t473_mutation_probe_tmp.cpp` were deleted before the
   final build. Neither is in the commit.
