# B944 — texture-cache eviction under the ceiling: the crash, the client's exposure, the fix

Date: 2026-09-30. Branch `task/B944-texture-eviction` from `gitlab/fable` @
`d2e8dd5b`, GitLab #262 (class A). Found by B943 (`refactor/B943/README.md`):
with `textureMemoryBytes` = 20 MiB the gate's qeynos2 golden run segfaulted.

Will's rules this unit works to: memory first; the caps are prescriptive and
eviction under a cap is REQUIRED behaviour (the original client streamed
textures in 32 MB); a crash on eviction is class A.

## 1. The benchmark crash, reproduced

`gles2_zone_benchmark` gained a test knob, `--texture-memory-bytes N`
(`tools/hwcaps/zone_bench_options.cpp`; 0 = the preset's value, the shipped
preset is untouched). The gate's own qeynos2 golden invocation
(`scripts/ci/visual_gate.py` `run_benchmark`: `--frames 60 --warmup 5 --res 800
600`, the three `--camera-pose`s, the production args) with
`--texture-memory-bytes 20971520`, on `d2e8dd5b`:

- exit **139 (SIGSEGV)**, every run;
- `gdb --batch` backtrace:
  ```
  #0 ZoneBench::textureGroupId(irr::video::SMaterial const&)   (+120)
  #1 ZoneBench::collectDrawEntries(...)
  #2 runStaticPoseCapture(...)
  #3 main
  ```
- the faulting instruction is the loop reading the texture NAME
  (`mat.getTexture(0)->getName().getPath()[i]`); the name's buffer pointer read
  `0x3f800000c0c10000` (freed memory reused for float data);
- with breakpoints on `ConstrainedTextureCache::evictTexture` and
  `CNullDriver::removeTexture`: **157 evictions before the crash**, and the
  crashing texture object (`0x55555afb1bf0`, its `NamedPath` at +8 is the
  `rdi` of the fault) is in the `removeTexture` list — destroyed by eviction.

**The dangling reference:** the benchmark's region meshes (`ZoneBenchData::regions`,
built by `ZoneMeshBuilder::buildTexturedMesh` → `loadTextureFromBMP` →
`ConstrainedTextureCache::uploadTexture`) hold raw `ITexture*` in their
mesh-buffer `SMaterial` texture slots. Each later upload during the same mesh
build ran `evictUntilAvailable()` → `evictTexture()`, which DESTROYED the LRU
texture (`driver_->removeTexture()`). The benchmark never registers a scene
manager with the cache (so `clearTextureReferences()` is a no-op) and no
eviction listener, so nothing touched the materials; the first frame's
`collectDrawEntries()` hashed the freed texture's name.

## 2. The client's exposure — it crashes too, on the shipped EQE1 path

Rig: `refactor/B944/evict_client_capture.sh` (lock, ONE snapshot, restore with
the world DOWN, `down.sh`, release — `scripts/client_ledger_capture.sh`'s
structure), rendered x86 client (surfaceless llvmpipe) on the VM lane, owner
`b944`, qeynos2, a `--script` tour that turns through the four headings twice
and `/q`s, frames dumped every 30th frame. The pre-fix client was built with
ASan (`-fsanitize=address`, `EQT_MEMORY_LEDGER=OFF`) from a detached worktree
at `d2e8dd5b`. The ceiling was forced with the client's existing memory-spec
form of `--constrained` (`TOTALxTEXTURExFB` MiB; not the orangepi preset —
see the rig header).

| run (pre-fix, `d2e8dd5b`) | result |
|---|---|
| `--eqe1-path`, `--constrained 256x4x16` | **ASan heap-use-after-free on the FIRST rendered frame after zone-in**; 327 `RegionMeshTextureGuard` placeholder WARNs during the load |
| S3D path (no `--eqe1-path`), `--constrained 256x20x16` | no ASan error in the 60 s tour; **169 placeholder WARNs** (region geometry swapped to the magenta/grey checkerboard for the rest of the zone) |

The EQE1 report (`review/B944/before_eqe1_asan/qeynos2/client.log`):

```
READ of size 8 ... thread T0
  #0 irr::core::string<char>::c_str()
  #1 RaceModelLoader::createAnimatedNodeWithEquipment()   animated_mesh_creation.cpp:672
  #2 EntityRenderer::buildEntityMesh()                    entity_mesh_build.cpp:276
  #3 EntityRenderer::updateEntityAppearance()             entity_lifecycle.cpp:163
  ... IrrlichtBridge::applyEvent <- forwardStagedEvents <- Application::render
freed by thread T70 (the loading thread):
  COGLES2Texture::~COGLES2Texture <- CNullDriver::removeTexture
  <- ConstrainedTextureCache::evictTexture <- evictUntilAvailable
  <- uploadCompressedETC1 <- uploadTexture <- ParticleManager::loadSpellSpriteAtlas
  <- ParticleManager::onZoneEnter <- ZoneLoadComponent::loadZoneStep11_Env
previously allocated by thread T70:
  gles2WrapTexture <- uploadCompressedETC1 <- uploadTexture <- uploadAllPending
  <- ZoneLoadComponent::loadZoneStep8_Entities
```

**The client's dangling holder:** `RaceModelLoader`'s cached animated mesh
(`animatedMeshCache_`, an `EQAnimatedMesh` shared by every entity of that
race). Step 8 uploaded an entity texture into its buffer material; Step 11's
particle atlas upload evicted (destroyed) it; the first appearance update
after zone-in built a node from the cached mesh and read the freed texture's
name (line 672 reads `buffer->getMaterial().getTexture(0)->getName()`).
`EntityMeshTextureGuard` scans only LIVE entity nodes, so the cached mesh had
no guard. The census of other raw holders the old design left dangling:

| holder | guarded before B944? |
|---|---|
| region-mesh materials (client) | yes — `RegionMeshTextureGuard` swaps to the placeholder (a wrong draw, permanent for the zone visit) |
| region-mesh materials (benchmark) | **no** (the crash above) |
| `RaceModelLoader` cached meshes (`meshCache_`, `animatedMeshCache_`, variant/composite caches) | **no** (the ASan UAF above) |
| `ZoneMeshBuilder::textureCache_` (filled by the cache's own `registerUploadedTexture`) | **no** — a later `getOrLoadTexture(name)` returned the freed pointer |
| entity nodes, equipment nodes, object nodes, doors, boids, sky, particles, storm clouds, detail | yes — listener guards (placeholder or reload) |
| `EquipmentModelLoader::meshCache_` | yes — its own listener (placeholder) |
| the ETC1 alpha companion | never destroyed at all — **leaked** on every eviction AND every `clear()` (zone unload), and a re-upload of an evicted name wrapped a NEW companion GL name under the old companion's driver name, which `wrapExternalTexture()` dedups — the new GL texture was orphaned (never deleted, never counted) |

So: **the client can crash (EQE1 path, shipped), and where it does not crash it
draws wrong** (every guard's answer to an eviction was the placeholder, forever).

## 3. The fix — eviction keeps the object

`ConstrainedTextureCache` (class comment in
`include/client/graphics/constrained_texture_cache.h`,
`src/client/graphics/constrained_texture_cache_residency.cpp`) and the GLES2
driver (`docker/irrlicht-drm/COGLES2Texture.{h,cpp}` "Residency",
`COpenGLES2Driver.cpp` `gles2Texture*` functions):

- **Eviction releases the GL storage, not the object.** `evictTexture()` calls
  `releaseStorage()` on the texture and its alpha companion (`glDeleteTextures`,
  `GpuBytes` 0) and keeps both objects — same pointer, name, size, companion
  link — in `cache_` as a non-resident entry. Every raw holder above stays
  valid. The eviction listeners are NOT notified, so nothing swaps a valid
  texture for the placeholder any more.
- **The driver's first bind restores it.** `COGLES2Texture::getOpenGLTextureName()`
  (the accessor every bind path in the driver uses; `getDriverTextureHandle()`
  and `lock()` go through it too, so ParticleManager's raw-GL binds are covered)
  calls the owner's restore function when the texture is evicted.
  `ConstrainedTextureCache::restoreEvicted()` evicts older entries if it must,
  rebuilds the storage with the FIRST upload's own code
  (`createETC1Storage()` / `createV2Storage()` / `createDecodedTexture()`, split
  out of the three upload paths for this) and adopts the new GL name into the
  same object; it saves and restores the active unit's binding around the
  upload. Removal paths use `getStoredTextureName()` and never restore.
- **Restore sources.** An EQE1 entry keeps its `CompressedEntry` (pointers into
  the mapped pack; a struct, not pixels). A decode-path (S3D) entry is re-read
  by name from a `TextureSourceProvider`: `ArchiveTextureSource`
  (`include/client/graphics/archive_texture_source.h`) over the zone's `.s3d`,
  `_obj.s3d` and `sky.s3d`, registered by `ZoneLoadComponent::loadZoneStep1_S3D`
  and by the benchmark's `loadZone()` (mapped, file-backed; nothing opens until
  a restore needs it). `clearEQE1Loaders()` drops the pack sources of every
  unprotected entry, because the loaders are destroyed right after it.
- **No source → the pre-B944 destroy**, now through one body
  (`destroyTextures()`) that destroys the alpha companion with its main texture
  (listeners are told about both) and, on this eviction path, also scrubs
  `ZoneMeshBuilder`'s name map (`ZoneMeshBuilder::forgetTexture`, new). Reached
  by S3D-path character/equipment textures, `registerTexture()` externals and
  non-GLES2 (EDT_NULL test) textures. `clear()` (zone unload) goes through the
  same body, so the per-zone companion leak is gone.
- **Accounting stays honest.** `getCurrentUsage()` and the LRU list hold
  resident entries only; the driver's `getGpuTextureMemoryUsage()` drops by the
  released storage and rises on restore (a shell counts 0); the P13 ledger
  records each eviction and each restore (`"restore"` rows). New:
  `getResidentTextureCount()`, `getRestoreCount()`, `getRestoreFailureCount()`,
  `isResident()`. The benchmark prints them (`[texture-cache] ...`, stdout
  only; the memory JSON schema is unchanged), and the client's `unloadZone`
  line names them.
- A test knob, `WILLEQ_TEXTURE_MEMORY_BYTES` (ConstrainedTextureCache's
  constructor, LOG_WARN when set), forces the ceiling under the REAL preset for
  a live run. No shipped path sets it; no budget, ceiling, golden or gate
  assertion changed.

## 4. Evidence after the fix

**Benchmark, the gate's qeynos2 golden invocation** (build-gles2 at this branch):

| ceiling | exit | f0 / f1 / f2 vs `tests/goldens/qeynos2_f*.png` | `[texture-cache]` |
|---|---|---|---|
| 20 MiB (`--texture-memory-bytes 20971520`) | 0 | **identical / identical / identical** (RGB byte compare, 0 differing pixels) | entries 126, resident 62, evictions 85, restores 21, restore failures 0, used 20,884,116 / limit 20,971,520 |
| preset (51 MiB) | 0 | identical / identical / identical | entries 126, resident 126, evictions 0, restores 0, used 42,226,984 (B943's figure) |

At 20 MiB `gpuTextureBytes` read 15,728,640 (vs 31,735,808 at the preset
ceiling, B943) — the evicted storage really is given back.

**Client** (ASan build of this branch, VM lane, the same rig), under the REAL
orangepi preset with the ceiling forced by the `WILLEQ_TEXTURE_MEMORY_BYTES`
test knob (`/mnt/projects/fable/review/B944/README.md` has the media):

| run | evictions / restores / restore failures (at unload) | result |
|---|---|---|
| EQE1, 8 MiB, qeynos2 | 162 / 8 / 0 (401 entries, 247 resident, 8,380,454 B used) | no ASan error, clean shutdown, 0 placeholder WARNs, 55 frames |
| EQE1, 8 MiB, gfaydark | 121 / 38 / 0 (328 entries, 245 resident, 8,375,510 B) | same |
| EQE1, 8 MiB, qeynos2, repeat | 162 / 8 / 0 | same |
| S3D, 20 MiB, qeynos2 | 201 / 22 / 0 (516 entries, 337 resident, 20,756,755 B) | same; 0 placeholder WARNs (pre-fix: 169 at a comparable ceiling) |
| S3D, 20 MiB, gfaydark | 145 / 43 / 0 (330 entries, 228 resident, 20,764,985 B) | same |
| EQE1, preset ceiling (control, twice) | 0 / 0 / 0 (392 entries, 15,179,662 B) | same |

(`rc` is 1 on every ASan run: LeakSanitizer cannot run in a capability binary
and exits 1 after the clean shutdown; the rig records `lsan_exit` and
`clean_shutdown` separately. Running without the capability instead stalled
gfaydark's zone-in at "Finalizing connection" -- the documented UDP-buffer
overflow -- so the capability stays.)

Live frames cannot be pixel-compared (not frame-synchronised), so the
DETERMINISTIC version of the same question was asked of the benchmark with the
client's EQE1 + objects + entities load at the gate zones' golden poses:

| benchmark | evictions / restores | frames vs the same run at the preset ceiling |
|---|---|---|
| gfaydark, EQE1 + `--objects` + 40 entities, 2 MiB | 453 / 356 | f0, f1, f2 **identical** |
| qeynos2, EQE1 + `--objects` + 40 entities, 4 MiB | 143 / 36 | f0, f1, f2 **identical** |

The one visible difference between an eviction run and its control is the
sky's brightness, which follows EQ time of day, not eviction: the repeat
eviction run stayed bright while its no-eviction control a few minutes later
was the dark one.

**Unit test** `tests/test_texture_eviction_residency.cpp` (ctest; real
EDT_OGLES2 surfaceless device; the ETC1 half uses `qeynos2_obj.eqe1`; each
fixture self-skips without them):

- `EvictionKeepsTheObjectAndABindRestoresTheSamePixels` — five textures held by
  `SMaterial`s the cache does not know, ceiling of three: two evictions, three
  resident, five live objects; usage and driver bytes = exactly three
  textures; the evicted objects are still the cache's AND the driver's
  (`findTexture`), their names readable; no listener fired; a `draw2DImage` of
  an evicted texture through the driver restores it and the framebuffer shows
  its own colour; two rounds of drawing all five (ten restores/evictions round
  the ring) each show their own colour under the ceiling.
- `WithoutASourceTheTextureIsDestroyedAndTheMeshBuilderForgetsIt` — the
  fallback destroys, fires the listener, and `ZoneMeshBuilder` forgets it.
- `EvictionReleasesAndABindRestoresTheCompanionToo` (ETC1) — the companion
  object survives, `getDriverTextureHandle()` restores both, driver bytes after
  the round trip equal the texture's own.
- `ClearDestroysTheAlphaCompanionWithItsTexture` — `clear()` leaves neither in
  the driver and the driver bytes return to baseline.

Results: 4/4 in the RelWithDebInfo tree; **4/4 and ASan-clean** in an ASan
build (`-fsanitize=address`). Revert-and-prove: with eviction forced back to
the pre-B944 destroy (a temporary switch in `canReleaseStorage()`, removed),
the two eviction tests FAIL (25 assertion failures) and the two others pass —
the net tests the change.

The ASan client run surfaced one defect in the FIRST version of this fix: a
destructor step that cleared the protected entries' residency back-pointers
read textures the renderer had already destroyed at shutdown (the device goes
before the cache). The step was removed — a protected entry is never evicted,
so it never carries a back-pointer. The full suite surfaced a second one:
`destroyTextures()` also scrubbed `ZoneMeshBuilder`'s name map when called
from `clear()` in the cache's DESTRUCTOR, and the benchmark destroys
`RaceModelLoader` (the builder's owner) before the cache — a heap-use-after-free
(ASan) that corrupted the heap and failed both `EntityViewerRegressionTest`
cases. The scrub now runs only on the eviction/replacement destroy path (at
zone unload `RaceModelLoader::clearMeshCaches()` has already emptied the map).
The live client runs above used the build before that last change; it touches
only the destructor path.

**The ceiling itself.** B943 set `textureMemoryBytes` to 51 MiB rather than
the 20 MiB its census suggested, because 20 MiB crashed. With B944 a ceiling
below the working set no longer crashes and the gate's golden frames still
match at 20 MiB. Whether to lower it is a budget change — Will's call; this
unit changed no budget.

## Verification

- `build` (GLES2 config, RelWithDebInfo, tests on): full `ctest -j6`
  **5090/5090 passed** (129 of them GTEST_SKIP on their own conditions, e.g.
  no session capture; the tip's skip count was not taken),
  `visual_gate` included. Before the destructor fix above the same run had
  3 reds: both `EntityViewerRegressionTest` cases (that bug) and `visual_gate`
  (iceclad p95 CPU under `-j6` load, B927's known host shape).
- `ctest -L hygiene`: 20/20. `ZeroCallers` needed one `EXEMPT_TESTS_GROUPS`
  row (`ConstrainedTextureCache::isResident`, observer).
- `visual_gate.py` standalone, five gate zones: all golden frames and memory
  assertions pass except **memory-coverage iceclad allocator slack 35.7 %
  (bound 35.0 %)** — B937's known local-host result (35.4 % recorded there);
  the coverage runs use the preset ceiling (no evictions).
- `scripts/refactor/check_file_sizes.sh ... gitlab/fable`: 0 FAIL; one WARN,
  `docker/irrlicht-drm/COpenGLES2Driver.cpp` (already over 1,500 before this
  unit; `docker/` is outside the `--all` set the ctest scans).
- ARM: `scripts/build-arm-noble.sh` exit 0 (the patched Irrlicht, driver
  changes included, rebuilt in the container; `build-arm-noble/bin/willeq` ELF
  32-bit ARM). Not run on the Pi.

## 5. Findings filed

| issue | class | what |
|---|---|---|
| **B947** #265 | A | S3D-path character/equipment textures have no restore source: their eviction still destroys them and `RaceModelLoader`'s cached meshes keep the freed pointer (the holder this unit's pre-fix ASan run caught). Not the shipped path. |
| **B948** #266 | B | `gpuTextureBytes` counts every ETC1 alpha chain twice (main texture wrapped with main+alpha bytes, companion with alpha again). |
| **B949** #267 | B | the LRU is load order, not draw order: an on-screen texture can be evicted first and restored next bind (thrash; performance). |
| **B950** #268 | B | `ZoneMeshBuilder`'s layout depends on `EQT_HAS_TEXTURE_ATLAS`, a PRIVATE define of `willeq_core`; `test_placed_object_meshes` constructs one without it (this unit's own test hit the ASan stack-buffer-overflow). |

Also corrected in passing: the header's `freeze()` comment claimed "when frozen,
no evictions occur (prevents crashes from dangling texture pointers)" --
`frozen_` is read by nothing and nothing calls `freeze()`.

## Not verified

- **The Orange Pi.** Nothing here ran on the Pi; the restore path's cost
  (an ETC1 re-upload per restore) and its behaviour under Lima are unmeasured.
  The x86 ASan runs are llvmpipe.
- **Frame-for-frame visual equivalence in the LIVE client.** The benchmark's
  goldens are pixel-identical at 20 MiB; the live client's frames are dumped
  (`review/B944/`) for Will to judge, but live runs are not frame-synchronised
  (NPCs, chat, headings reached at different times), so no pixel compare was
  made between the eviction and control runs.
- **LRU order is load order, not use order.** The cache is touched on lookup,
  not on draw, so the oldest-loaded texture is evicted first even if it is on
  screen every frame; it restores on its next bind. Correct but can thrash
  (performance, secondary). Filed as B949.
- **S3D-path character and equipment textures have no restore source**; their
  eviction still destroys them and the pre-B944 holders of those
  (`RaceModelLoader`'s cached meshes) are still unguarded on that path. The S3D
  path is not the shipped Pi path (EQE1 all-or-nothing). Filed as B947.
- The zone-merge draw (`renderer_zone_draw.cpp`) still disables itself for the
  zone visit on the first eviction; with B944 the merged buffers' textures stay
  valid, so that bail-out is now only conservative. Not changed.
- Other zones than qeynos2/gfaydark were not run with a low ceiling.
