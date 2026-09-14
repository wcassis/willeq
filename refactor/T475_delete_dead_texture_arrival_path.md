# T475 — delete the dead async texture-arrival path (B670, closing B640)

Class B (unreached), **no lock, no Pi**, slot3. Read T473's Review
(`refactor/T473_door_retexture_after_pending_texture.md`) — it proved the path
has no producer — and T470/T468's Reviews for the door ownership rules.

## The decision (orchestrator)

**Delete, do not restore.** Texture loading became synchronous in Batch T's T04
(`aaee58d1`, 2026-03-16), and T326 (`77c842b4`) deleted the cache's async
upload queue — the only code that ever submitted a GPU upload with
`callbackKey >> 56 == 3`. Load time is secondary to correctness with no maximum
(Will, standing decision #1), so there is no reason to bring an async texture
producer back. Everything downstream of that key is unreachable: the
`processCompletedUploads()` / `processOneGPUResult()` texture arm
(`renderer_gpu_upload.cpp`), `doorTextureRebuildQueue` and its two drains
(`renderer_frame.cpp` Priority 8, `renderer_zone_stream.cpp` P1.6),
`pendingTextureDoors()` / `pendingTextureObjects` / `pendingTextureRegions_` and
their consumers, `setGPUUploadThread()`'s never-read pointer,
`pendingAsyncUploads_` (read, never written), and whatever only those reach.

## Tasks

1. Census every symbol of the path (T473's hop list and census table are the
   starting set), prove zero producers/readers per the protocol's zero-caller
   rule (`src/ include/ tests/ tools/ benchmarks/ docker/`), then delete,
   cascading to a fixed point the T458 way. Keep what the atlas-page and region
   vertex-buffer uploads use (`GPUUploadThread` itself stays).
2. Replace T473's tripwire test with the right net for a deleted path: a source
   census that fails if a texture-keyed upload (high byte 3) or a pending-texture
   door/object/region queue reappears without a consumer. B640's missing
   `rebuildSingleDoor()` branch closes with the path; say so in the Review.
3. Leave a short tombstone comment where the arm was (standing decision #13:
   delete commented-out CODE, keep tombstone PROSE explaining a deliberate
   absence), naming T04/T326/T473/T475.
4. B671 (a mesh built without its texture stays untextured for the zone) is
   NOT this unit — it is a live-measurement item for a later lock holder; do not
   touch its logging.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`renderer_frame_render` and zone-stream order goldens are
pinned — in-place per G-correction 32, say so); `ZeroCallers` / `WriteOnlyFields`
rows updated; visual gate (no image golden moves); ARM; file sizes; doc nets
(`docs/module_map.md`, CLAUDE.md mentions). Board B690–B699; strike B670, B640.

File territory: `renderer_gpu_upload.cpp`, the two queue drains, the pending
texture queues in zone load / zone stream / door / object / region code, their
headers, tests, `scripts/refactor/check_zero_callers.py`, docs. **Not** the
camera component, `CameraController`, `/camera`, `camera_rig.h` (T474, slot1),
**not** entity/equipment attachment (T472, slot2).

STOP: a symbol on the path turns out to have a live producer (report it with
the citation — the decision then returns to the orchestrator); an image golden
moves.

## Review

Slot3, branch `task/T475-delete-texture-arrival`, cut from `fable` `eaead058`.
No lock, no Pi, no server contact. Every build and test run wrote its own exit
file under a unit-private scratch directory (`scratchpad/T475/`), and every
wait reported a non-zero elapsed time. There was no exit 143 anywhere.

**STOP conditions:** neither was hit. Nothing on the path had a live producer
(each symbol's proof is below), and no image golden moved.

### Census: zero producers and readers, per symbol

Grepped `src/ include/ tests/ tools/ benchmarks/ docker/` for each symbol
before deleting it, then re-ran `check_zero_callers.py --check` and
`check_write_only_fields.py --check` after each round until both were clean.

| symbol | producer / writer | reader / consumer | verdict |
|---|---|---|---|
| GPU upload with `callbackKey >> 56 == 3` | none (T04 `#if 0`, T326 deleted) | the arrival arm | dead |
| `UploadRequestType::Texture` / `CompressedTexture` | `TextureAtlas::uploadPreloadedPageAsync()` only, which has **zero callers** (a B494 row) | the arm, which never used an atlas result | dead at both ends |
| arrival arm in `processCompletedUploads()` / `processOneGPUResult()` | no texture result can be produced | n/a | dead |
| `pendingTextureRegions_` | `rebuildRegionMesh()` | the arm only | written, never drained |
| `textureRebuildQueue_`, `TextureRebuildEntry` | the arm only | the zone-stream status log and completion gate (always empty) | dead |
| `pendingTextureDoors_` + accessors | Step 7 | the arm only | written, never drained |
| `doorTextureRebuildQueue` (ctx + renderer member) | the arm only | Priority 8, P1.6 | dead |
| `pendingTextureObjects` (ctx + renderer member) | Step 6, `buildDeferredObject()` | the arm only | written, never drained |
| `objectTextureRebuildQueue` (ctx + renderer member) | the arm only | Priority 9, P1.7, status log, completion gate | dead |
| `ConstrainedTextureCache::setGPUUploadThread()` / `gpuUploadThread_` | init and `/upload` toggle | none | write-only |
| `ConstrainedTextureCache::clearPendingAsync()` | n/a | the arm only | dead |
| `pendingAsyncUploads_` | none | `isPending()`, `hasPendingWork()` | read-only, always empty |
| `ZoneStreamComponent::buildDeferredObject()` | n/a | Priority 9, P1.7, and one no-op test call | dead once the drains were gone (cascade 2) |
| `ZoneLoadComponent::treeIdentifier()` | n/a | `buildDeferredObject()` only (ZeroCallers flagged it, round 2) | dead (cascade 3) |
| `TextureAtlas::uploadPreloadedPageAsync()` / `setPageTexture()` | n/a | none (B494 zero-caller rows) | dead, the last texture submitter |
| `GPUUploadThread::processTextureUpload()` / `processCompressedTextureUpload()` and the texture fields of `UploadRequest` / `UploadResult` | only the two request types above | n/a | dead |

**Kept, because they are live:** `GPUUploadThread` itself, VBO-only
(`createZoneMesh()` submits `VertexBuffer`, and both drains still apply it);
`drainGPUResults()`, `pendingGPUResults_`, `pendingVBOUploads_`;
`toggleGPUUploadThread()` / `gpuUploadEnabled_` (gates the VBO submit);
`DoorManager::invalidateMeshCache()` (called by `diagnostics_debug.cpp`);
`rebuildSingleDoor()` (Step 7); `getMissingTextures()` /
`getLastMissingTextures()` (B671's log lines); `ZoneMeshBuilder::registerUploadedTexture()`
and `ConstrainedTextureCache::registerTexture()` (other callers);
`DeferredObject::nodeIndex` (read by Step 11's fixup); `treeIdentifier_` (Step 6).

### Deletion cascade, to a fixed point

Round 1: the arm and everything above that only it touched. Round 2
(ZeroCallers): `buildDeferredObject()`. Round 3 (ZeroCallers):
`treeIdentifier()`. Round 4: ZeroCallers and WriteOnlyFields both clean, so the
cascade stopped there.

| file | + | - |
|---|---|---|
| `src/client/graphics/zone_stream/renderer_zone_stream.cpp` | 24 | 354 |
| `src/client/graphics/zone_lifecycle/renderer_gpu_upload.cpp` | 14 | 238 |
| `src/client/graphics/gpu_upload_thread.cpp` | 2 | 117 |
| `src/client/graphics/texture_atlas.cpp` | 1 | 63 |
| `src/client/graphics/frame/renderer_frame.cpp` | 6 | 37 |
| `include/client/graphics/zone_lifecycle/zone_lifecycle_component.h` | 6 | 21 |
| `include/client/graphics/gpu_upload_thread.h` | 12 | 19 |
| `include/client/graphics/texture_atlas.h` | 4 | 15 |
| `include/client/graphics/constrained_texture_cache.h` | 6 | 13 |
| `include/client/graphics/irrlicht_renderer.h` | 3 | 11 |
| `src/client/graphics/constrained_texture_cache.cpp` | 0 | 10 |
| `src/client/graphics/zone_load/renderer_zone_load_objects.cpp` | 7 | 7 |
| `src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp` | 5 | 7 |
| `src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp` | 2 | 6 |
| `src/client/graphics/renderer/renderer_init.cpp` | 3 | 3 |
| `include/client/graphics/renderer_context.h` | 2 | 3 |
| `src/client/graphics/constrained_texture_cache_upload_queue.cpp` | 4 | 2 |
| headers: `door_manager.h`, `frame_component.h`, `zone_load_component.h`, `zone_stream_component.h` | 9 | 7 |

Production code: about 930 lines removed. The `+` column is almost entirely
tombstone prose (standing decision #13), each naming T04/T326/T473/T475. No
commented-out code was left behind.

**B640 closes with the path.** `rebuildSingleDoor()`'s missing branch (a real
mesh built without its texture) could be asked for only by the Priority 8 /
P1.6 drains, and both are gone with their only producer. There is nothing left
that could want that branch. What a player can still see, a door built
without its texture staying untextured, is B671 and is untouched.

### The replacement net: red under mutation, green without

T473's tripwire (`DoorTextureArrivalSource.NoTextureUploadIsSubmittedWithTheArrivalKey`)
is replaced by **`TextureArrivalPathCensus`** (`tests/test_door_texture_eviction.cpp`,
two cases):

- **`NoHalfOfTheDeletedArrivalPathIsBackInCode`**
  - Scans `src/ include/ tools/ benchmarks/` with comments stripped, so
    tombstones are allowed.
  - Seven signatures: a `<< 56` key, a `>> 56` route, the two texture request
    types, `pendingAsyncUploads_`, any `pendingTexture{Doors,Objects,Regions}`,
    any `{door,object,}textureRebuildQueue`, and the deleted function names.
  - The message says that restoring an async producer needs a consumer for
    every queue it fills, plus B640's re-texture branch.
- **`TheSignaturesMatchTheDeletedCodeAndIgnoreTombstones`**
  - Pins that each signature matches the real deleted lines.
  - Pins that the stripper removes comments.
  - Pins that live neighbours do not match: the VBO key `i << 48`,
    `getLastMissingTextures()`, `case VertexBuffer`, `getMissingTextures()`.

| run | state | result |
|---|---|---|
| `test_door_texture_eviction` on the final tree | no arrival code | 11/11 OK |
| `--gtest_filter=TextureArrivalPathCensus.*` with a temporary `src/client/graphics/t475_mutation_probe_tmp.cpp` (in no target; line 2 a tombstone naming two queues, line 6 `std::vector<uint8_t> doorTextureRebuildQueue_;`, line 7 `return uint64_t(3) << 56;`) | half the path back | **FAILED**: names lines 6 and 7 only (the comment on line 2 is ignored); the signatures case stays OK |
| the same filter after deleting the probe | clean | 2/2 OK |

The census found one real hit before the mutation run.
`ZoneStreamComponent::onPvsRegionChanged()`'s VBO reprioritise lambda routed on
`callbackKey >> 56`. It now routes on `req.type == UploadRequestType::VertexBuffer`
(see deviation 3).

### Goldens

`git diff eaead058 -- tests/goldens` is empty: no image golden and no order
golden moved.
- `frame_render_order_g05.txt` pins `renderer_frame_render.cpp`, which this unit
  did not touch.
- There is no statement golden for `renderer_frame.cpp` or
  `renderer_zone_stream.cpp` (the plan's "zone-stream order goldens" do not
  exist in `tests/goldens/`), so no in-place G-correction-32 edit was needed.
- Run explicitly for every touched TU's golden or component test:
  `test_frame_render_order` 4/4, `test_frame_component` 21/21,
  `test_zone_lifecycle_component` 14/14, `test_zone_stream_component` 14/14.

### ZeroCallers / WriteOnlyFields row changes

- **`check_zero_callers.py`**: removed `("TextureAtlas", "setPageTexture")` and
  `("TextureAtlas", "uploadPreloadedPageAsync")` (B494), whose functions are
  deleted. No row added.
  - `buildDeferredObject()` and `treeIdentifier()` were deleted rather than
    exempted.
  - Before (baseline `--bucket all` at `eaead058`): 414 zero, 290 tests-only. After: `zero-caller census clean: 412
    functions, all classified in EXEMPT`, tests-only clean at 290.
- **`check_write_only_fields.py`**: no row change. `gpuUploadThread_` (write-only)
  was not in the table because `setGPUUploadThread()` shared the name; it is
  deleted, and the census is clean at 67.

### Verification (builds strictly in sequence)

| step | result |
|---|---|
| 1 `cmake --build build -j24` | **PASS**, exit 0 (build 1: 1,455 s; build 2, after the `treeIdentifier()` round: 60 s) |
| 3 `build-gles2-x86.sh` (after step 1's exit file) | **PASS**, exit 0 (820 s) |
| 2 `ctest -j6` (after step 3, so `visual_gate` read the fresh `build-gles2`) | **PASS**: `100% tests passed, 0 tests failed out of 4775`, exit 0. Includes both `TextureArrivalPathCensus` cases, all 9 `DoorTextureEvictionTest` cases, `ZeroCallers`, `WriteOnlyFields`, `FileSizeCeiling`, `DocClaims`, `DocCitations`, `DocCounts`, `CommentClaims` and `visual_gate` (24.6 s) |
| 4 visual gate | **PASS** inside the suite; no image golden moved |
| 5 `build-arm-noble.sh` (after ctest) | **PASS**, exit 0 (1,051 s); `build-arm-noble/bin/willeq` is a fresh 32-bit ARM EABI5 ELF (05:44 UTC) |
| 6 `check_file_sizes.sh fable` | **PASS**: 36 ok, 0 warn, 0 fail. `fable` had moved to `a2602187` (T472 Leg A, T476 plan), so the 36 include their files; every file this unit touched is among them and under the ceiling (largest: `renderer_zone_draw.cpp` 1,141) |
| doc nets | `DocClaims`, `DocCitations` (final run after the B690 entry: 1,311 ok, 0 fail, 44 skip), `DocCounts`, `CommentClaims`: all exit 0 standalone and in the suite |

### Docs

- `docs/module_map.md`: the `renderer_gpu_upload.cpp` row (447 -> 223, now
  VBO-only), `renderer_zone_stream.cpp` (874 -> 545, `buildDeferredObject()`
  gone), and the line counts of `renderer_zone_draw.cpp`,
  `renderer_zone_lifecycle.cpp`, `renderer_zone_load_objects.cpp`,
  `renderer_frame.cpp` and `renderer_context.h`. Some of those counts were
  already stale before this unit and were re-measured.
- `src/client/graphics/CLAUDE.md`: `GPUUploadThread` is "region VBO uploads".
  The root `CLAUDE.md` names none of the deleted symbols; grepped.

### Board

- **B640** struck, closed with B670 (see above).
- **B670** struck, with the full deletion list.
- **B671** kept open. Its holder list no longer says "dead map"; it notes that
  `buildDeferredObject()` is gone and that the three log lines keep matching
  its grep.
- **B690** (B, hygiene) new:
  - `AssetType::ZoneTexture` has no production user after T475; only
    `tests/test_work_priority.cpp` names it.
  - The four `constrained_texture_cache*.cpp` TUs include
    `gpu_upload_thread.h` / `work_priority.h` and use nothing from either
    (pre-existing).
  - `texture_atlas.cpp` includes `<fmt/format.h>` for the deleted
    `uploadPreloadedPageAsync()` alone.
  - None of these is a function or a field, so no net sees them.
- B691-B699 unused.

### Deviations (flagged)

1. **The cascade went past the plan's list, and one of the plan's premises was
   wrong.**
   - The plan (from T473/B670) said the atlas pages are a live
     `GPUUploadThread::submit()` caller to keep. They are not:
     `TextureAtlas::uploadPreloadedPageAsync()` has zero callers and was already
     a boarded B494 zero-caller row, and no arm ever consumed an atlas result.
   - It was the last texture submitter, so it went with `setPageTexture()`, the
     two texture request types, both worker bodies and the texture fields.
   - `GPUUploadThread` itself stays, as the plan requires, now VBO-only.
   - `buildDeferredObject()` and `treeIdentifier()` were not named in the plan;
     both were deleted because the zero-caller rule required it.
   - The B494 entry's prose still lists "texture-atlas" API in general. Its
     other `TextureAtlas` rows are untouched.
2. **B671's three log lines were reworded, against "do not touch its logging".**
   - Each ended "queued for later rebuild" or "will rebuild when uploads
     complete", which became false the moment the queues were deleted, and a
     stale claim is a first-class defect (Will).
   - Only that clause changed. Each line still logs and still starts with the
     exact text B671's confirming grep matches ("SEQ Step7: door ... missing
     textures", "SEQ Step6: obj... missing textures", "rebuildRegionMesh: ...
     missing textures"). No log line was added or removed.
3. **The VBO reprioritise callback routes on request type, not the key's high
   byte.**
   - It had to lose the `>> 56` for the census to hold.
   - For a region with at least 512 mesh buffers, buffers 512 and up used to get
     the lowest priority and now get the region's PVS depth, like the rest.
     Every other request is unchanged.
   - No zone is known to carry that many buffers in one region, and the path
     runs only with the DRM upload thread (the Pi).
4. **Two progressive-load readings changed shape, not value.**
   - The periodic "Progressive status" log lost its `texRebuild=` / `objRebuild=`
     fields.
   - The completion gate no longer tests two queues that were always empty.
5. **Temporary file.** `src/client/graphics/t475_mutation_probe_tmp.cpp` was
   created for the red run and deleted before the final runs. It is not in the
   commit.
