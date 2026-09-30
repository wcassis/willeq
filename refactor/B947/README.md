# B947 — every evictable texture has a restore source (S3D-path character/equipment textures, and the rest)

Date: 2026-09-30. Branch `task/B947-char-texture-restore` from `gitlab/fable` @
`267e5a36` (B944 merged), GitLab #265 (class A). Found by B944
(`refactor/B944/README.md` §5).

Will's rules this unit works to: memory first; the caps are prescriptive and
eviction under a cap is required behaviour; a crash or a placeholder on
eviction is a class-A defect; 100 % accounting.

## 1. The census — every upload path into `ConstrainedTextureCache`

Taken by `grep` over `src/ include/ tools/ benchmarks/` for `getOrLoad(`,
`uploadTexture(`, `registerTexture(`, `protectTexture(` and
`addRenderTargetTexture(`. "Holder" = what keeps the raw `ITexture*` after the
upload. "S3D before" is the tip (`267e5a36`); "after" is this branch.

| # | upload path (callsite) | holder of the raw `ITexture*` | EQE1 path source | S3D path, before | S3D path, after |
|---|---|---|---|---|---|
| 1 | zone regions: `ZoneMeshBuilder::loadTextureFromBMP` via `buildTexturedMesh` / atlased build | region mesh materials, `ZoneMergeBuffers`, `ZoneMeshBuilder::textureCache_` | pack (B944) | name-keyed `ArchiveTextureSource` over zone `.s3d` (B944) | **origin** = zone `.s3d` member; provider kept as fallback |
| 2 | objects (`_obj.s3d`), same builder | object mesh/node materials | pack | provider (B944) | **origin** |
| 3 | animated zone texture frames: `AnimatedTextureManager::loadTexture` | `frameTextures`, mesh buffers, merge-table swap hook | pack | provider **if** the frame name is in the zone archives | **origin** |
| 4 | sky: `loadZoneStep10_Sky` | `SkyRenderer` nodes | pack | provider (`sky.s3d`) | **origin** (`sky.s3d` member) |
| 5 | **character per-part T05**: `RaceModelLoader::preloadModelData` (`*_chr.s3d`, `global*_chr.s3d`, zone `_chr.s3d`) | **`RaceModelLoader` `meshCache_` / `animatedMeshCache_` / variant caches**, `ZoneMeshBuilder::textureCache_`, entity nodes | pack (checked, §3) | **none → DESTROYED**; cached meshes dangle (B944's ASan UAF holder) | **origin** |
| 6 | character mesh build fallbacks: `mesh_building.cpp` (`loadTextureFromBMP`), `ZoneMeshBuilder::getOrLoadTexture` (lazy/deferred per-part) | same as 5 | pack | **none → destroyed** | **origin** |
| 7 | variant textures: `loadZoneStep8c_VariantTextures` | entity prep data → entity meshes | pack | **none → destroyed** | **origin** |
| 8 | equipment prep: `loadZoneStep8d_EquipmentPrep` (`gequip*.s3d`) | equipment meshes | pack | **none → destroyed** | **origin** |
| 9 | `EquipmentModelLoader::buildMesh` (`gequip*.s3d`, incl. the `.bmp`/`.dds` sibling fallback) | `EquipmentModelLoader::meshCache_` (T126 listener → placeholder) | pack | **none → destroyed**, placeholder | **origin** (member recorded when it is the sibling) |
| 10 | armour / equipment texture swap: `animated_mesh_creation.cpp` → `loadTextureFromBMP` (`RaceModelLoader::getArmorTexture`, `global*_amr.s3d`) | entity node materials | pack | **none → destroyed** | **origin** |
| 11 | procedural `data/textures/*.bmp`: `particle_atlas`, `spell_sprite_atlas`, `detail_atlas`, rain/snow overlays, `storm_cloud_blend_N`, `spell_particle_fallback`; SpellEffects DDS (`SpellVisualFX::loadDDSTexture`) | managers' members, particle/detail materials (listener guards → placeholder) | `willeq.eqe1` (persistent pack) | **none → destroyed**, placeholder | **origin** = the file (absolute path) |
| 12 | process-lifetime: `ui_atlas`, `item_icons0/1` (EQE1 only), `software_cursor`, `creature_atlas`, `tumbleweed` | `UIAtlas`/`UIRenderer`, `ItemIconSheets`, cursor, boids, tumbleweed | `protectTexture()` | `protectTexture()` (never evicted) | unchanged — protected, **never evicted**; now pinned by a GLES2 test |
| 13 | `registerTexture()` externals | caller | — | **none → destroyed** | **PINNED** (never evicted); its one caller, `SkyRenderer::finalizeStripUpload`, is reached from `beginStripUpload`, which has zero callers (boarded as B494) |
| 14 | paperdoll RTT (`CharacterModelView`, `addRenderTargetTexture`) | the view | not in the cache | driver-owned, never evicted | unchanged |
| 15 | composite entity sheets (EQE1 only; off on `orangepi`) | composite meshes | pack | n/a | unchanged |
| 16 | any texture of a non-GLES2 driver (the `EDT_NULL` unit tests only) | tests | — | destroyed (listener guards) | **unchanged, by design** — the listener-guard tests pin those guards; the client and tools create only `EDT_OGLES2` textures |

**After this unit no GLES2 texture in the cache can be destroyed by an
eviction.** It is released-and-restorable (rows 1–11, 15), protected (12), or
pinned (13, and any decode-path entry whose bytes cannot be read back
identical). `clear()` (zone unload) and a `registerTexture()` replacement still
destroy, as before.

## 2. The fix

- **`TextureOrigin`** (`include/client/graphics/texture_origin.h`): where a
  decode-path texture's raw bytes came from — an archive + member, or a plain
  file. `PfsArchive` now keeps its path as a shared string
  (`PfsArchive::sharedPath()`), and every S3D reader that fills a `TextureInfo`
  stamps it: `S3DLoader::loadTextures/loadObjectTextures/loadCharacterTextures`,
  `SkyLoader`, `RaceModelLoader::getArmorTexture`,
  `EquipmentModelLoader::getEquipmentTextureLocked` and `extractFromParsed`
  (which also records the member when the bytes came from the `.bmp`/`.dds`
  sibling). `TextureInfo::origin()` builds the origin; every `getOrLoad` /
  `loadTextureFromBMP` caller in rows 1–11 passes it (new trailing optional
  parameter, `nullptr` = unknown).
- **Byte identity.** On the decode path the cache records the raw size and an
  FNV-1a-64 digest next to the origin. A restore reads the origin (or, failing
  that, the name-keyed B944 providers) and accepts the bytes only if size and
  digest match — so a name held by two archives, or a file changed on disk,
  can never restore other pixels.
- **Checked once, at the first eviction** (`decodedSourceAvailable()`): the
  source is read and compared then, and remembered. That answer decides
  release vs pin, so a restore that is allowed is one that will succeed.
- **Pin, never destroy.** A GLES2 entry with no usable source is PINNED
  (`pinEntry()`): it stays resident, counts toward the usage, is skipped by
  `evictUntilAvailable()` like a protected entry, and is not counted as an
  eviction. One WARN per name (`... has no restore source -- PINNED ...`);
  `getPinnedTextureCount()` / `isPinned()`; the `unloadZone` line and the
  benchmark's `[texture-cache]` line print the count. If everything resident
  is protected or pinned, an upload that does not fit is refused (the
  pre-existing protected-entry behaviour) rather than paid for with a
  dangling pointer.
- **Archives for restore** are opened by the cache's own
  `ArchiveTextureSource` (`readMember()`, new; mapped, directory table under
  `Archives`) on first need and closed by `clear()` at zone unload. Plain files
  are read with `ArchiveTextureSource::readFile()`.
- EXECUTOR_PROTOCOL, `src/client/graphics/CLAUDE.md` ("Eviction keeps the
  texture OBJECT") and the cache header comments say the same.

## 3. The EQE1 path, checked for character entries

Character textures on the shipped path come from `<race>_chr.eqe1` /
`global*_chr.eqe1` / zone `_chr.eqe1` packs as `Source::Compressed` entries
whose restore source is a pointer into the loader's mapping. Every loader is
inserted at ONE place (`Eqe1ArchiveComponent`, `ctx_.eqe1Loaders[...]`) —
including T266's late opens through `EQE1TextureIndex::resolve` — and erased at
ONE place (`unloadZone()`, after `clearEQE1Loaders()` has dropped the source of
every unprotected entry). So a character entry's pack outlives every eviction
and restore of that zone. `AnEqe1CharacterEntryFromALateOpenedPackRestoresFromThePack`
pins it with `global2_chr.eqe1` opened late through the resolver.

## 4. Tests

`tests/test_texture_eviction_character_restore.cpp` (new; real `EDT_OGLES2`
surfaceless device; self-skips without EGL, `data/eq_client`, the shipped db
or the EQE1 tree):

| test | asserts |
|---|---|
| `CachedAnimatedMeshSurvivesEvictionBelowTheWorkingSetAndRebindsPixelIdentical` | `RaceModelLoader::getAnimatedMeshForRace(1, 0)` (S3D path) under a 1 MiB ceiling (below one human's texture set); fillers evict every texture its cached materials hold; each is still `isLiveTexture`, the driver's object, name readable, not pinned, no listener fired; the cached mesh (same pointer) holds the same textures; each redraw equals its first draw, whole 64×64 frame byte-compared; 0 restore failures |
| `CharacterAndEquipmentTexturesCarryTheirArchiveOrigin` | equipment `TextureInfo`s carry an archive-member origin whose bytes are the loader's own |
| `PlainFileOriginRestoresAndAChangedSourceIsPinnedNeverMisrestored` | a file-origin texture is released and restores pixel-identically; one whose file changed before its first eviction is PINNED and keeps its own pixels |
| `AnExternalTextureIsPinnedAndNeverEvictedAgain` | `registerTexture()` external: pinned, resident, never destroyed, count 1 |
| `ProtectedProcessLifetimeTexturesAreNeverEvicted` | a protected texture survives two floods and `clear()` |
| `AnEqe1CharacterEntryFromALateOpenedPackRestoresFromThePack` | §3 |

`tests/test_texture_eviction_residency.cpp`: B944's
`WithoutASourceTheTextureIsDestroyedAndTheMeshBuilderForgetsIt` became
`WithoutASourceAGles2TextureIsPinnedNeverDestroyed`; the source-read count in
the ring test rose from 1 to 4 (each entry's first eviction reads its source
once to check it).

Results: 6/6 + 4/4 in the RelWithDebInfo tree; **6/6 + 4/4 ASan-clean** in an
ASan build (`-fsanitize=address`, `EQT_MEMORY_LEDGER=OFF`).
**Revert-and-prove** (ASan build): with origin-bearing entries forced back to
"no source" and the pin replaced by the destroy, the cached-mesh test FAILS
(`an eviction destroyed something`, `humlg0001.bmp was destroyed by an
eviction`) and the plain-file test hits **ASan heap-use-after-free** in
`drawFrame` (drawing the destroyed texture). The temporary switch was removed;
the log is `review/B947/revert_and_prove_asan_test.log`.

## 5. Live — B944's rig, VM lane, ASan client

`refactor/B944/evict_client_capture.sh` (owner `b947`; lock, one snapshot,
restore with the world DOWN, `down.sh`, release — every run printed
`snapshot restored BYTE-IDENTICALLY` and an empty lock), `--spec orangepi`
(the real preset) with the ceiling forced by `WILLEQ_TEXTURE_MEMORY_BYTES=8388608`
(8 MiB; the preset's is 53,477,376), `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`,
800×600, the four-heading tour twice, frames every 30th. The rig gained two
columns (`pinned_warn`, `restore_fail_warn`). `rc=1` is LeakSanitizer failing
at exit in a capability binary after a clean shutdown (B944 §4), recorded as
`lsan_exit=y clean_shutdown=y`.

| run | zone | ASan | placeholder / pinned / restore-fail WARNs | at unload: entries (resident), evictions / restores / failures / pinned |
|---|---|---|---|---|
| **before** (tip `267e5a36`), S3D | qeynos2 | none | 0 / – / – | **237** (30), 3068 / 2621 / 0 / – |
| **before**, S3D | gfaydark | none | 0 / – / – | **160** (27), 4972 / 4625 / 0 / – |
| **after**, S3D | qeynos2 | **none** | 0 / 0 / 0 | **510** (59), 3005 / 2554 / 0 / **0** |
| **after**, S3D | gfaydark | **none** | 0 / 0 / 0 | **376** (41), 4523 / 4188 / 0 / **0** |
| **after**, EQE1 | qeynos2 | **none** | 0 / 0 / 0 | 412 (257), 163 / 8 / 0 / **0** |
| **after**, EQE1 | gfaydark | **none** | 0 / 0 / 0 | 324 (241), 121 / 38 / 0 / **0** |

The entry counts are the measurement of the defect: an evicted-and-restorable
entry stays in the map, a destroyed one leaves it. On the tip the S3D runs
ended with 273 (qeynos2) and 216 (gfaydark) fewer entries than this branch —
the textures those evictions destroyed. The tip did not hit the ASan read in
these 60 s tours (the cached mesh's freed pointer is read only when another
entity of the race is built after the eviction); the deterministic proof of
that read is §4's revert-and-prove. The EQE1 numbers match B944's (162/8,
121/38). Frames and contact sheets: `/mnt/projects/fable/review/B947/`
(README there).

## 6. Accounting

- Restores go to the P13 upload ledger as `"restore"` rows (B944, unchanged).
  A pin is neither an upload nor an eviction and writes no ledger row; it is
  counted by `getPinnedTextureCount()`.
- `getCurrentUsage()` counts resident entries, pinned ones included (they hold
  storage). The driver's `gpuTextureBytes` still counts resident storage only;
  B948 (#266, the ETC1 alpha double count) is untouched.
- New memory: `TextureInfo` +2 `shared_ptr` (32 B on x86-64, 16 B on ARM32
  per `TextureInfo`), null on the EQE1 path's names-only entries;
  one shared path string per opened `PfsArchive`; per cache entry a
  `TextureOrigin` + digest + flags; the eviction-time source check reads the
  raw bytes once per entry (transient, `TexturesDecoded`); the restore
  archives' directory tables (`Archives`) until zone unload. None of it was
  measured separately (§8).

## 7. Verification

- `build` (GLES2 config, RelWithDebInfo, tests on): exit 0. Full `ctest -j8`:
  **5107 of 5108 passed**; the one red is `visual_gate`, on its iceclad CPU
  ceiling only (below).
- `ctest -L hygiene`: 20/20 (`ZeroCallers` needed one `EXEMPT_TESTS_GROUPS`
  row, `ConstrainedTextureCache::isPinned`, observer).
- `scripts/build-gles2-x86.sh`: exit 0. `visual_gate.py` standalone, five gate
  zones: every golden frame and every memory assertion PASSES (iceclad
  allocator slack 34.0 % / 33.9 % against the 35 % bound); the gate FAILS only
  on **iceclad p95 process-CPU** (17.9, 12.0, 13.3 ms over three runs; ceiling
  9.53 = the 4.77 ms baseline x 2). That is this host, not this branch:
  `gles2_zone_benchmark` at the gate's iceclad invocation, tip (`267e5a36`,
  its own `build-gles2`) and branch alternated three times each, measured CPU
  draw (process) p95 **tip 28.9 / 10.3 / 10.7 ms, branch 9.5 / 9.1 / 10.9 ms**
  — the tip is over the ceiling too. The benchmark does no eviction at the
  preset ceiling (`[texture-cache] ... evictions=0 ... pinned=0`), so nothing
  this unit changed runs per frame there. No budget, ceiling, golden or gate
  assertion was changed.
- `scripts/build-arm-noble.sh`: exit 0. Not run on the Pi.
- `check_file_sizes.sh ... gitlab/fable`: 0 FAIL, 0 WARN.

## 8. Not verified

- **The Orange Pi.** Nothing ran on the Pi; the eviction-time source check (one
  inflate + digest per first eviction) and the restore's archive opens are
  unmeasured on the Cortex-A7. x86 runs are llvmpipe.
- **Live pixel equivalence.** Live frames are dumped for Will, not compared
  (not frame-synchronised); pixel identity is proven by the unit test only.
- **The new bytes' size.** The `TextureInfo`/entry growth and the restore
  archives' directory tables were not measured in the ledger; the gate's
  memory assertions are the only check.
- **Pins in the wild.** Both zones on both paths pinned 0. A zone whose
  textures come from somewhere this census missed would pin (and WARN) rather
  than dangle, but other zones were not run.
- The live tour did not reproduce the tip's UAF read (see §5); that read is
  proven deterministically, not live.
- LRU thrash (B949) is visible at 8 MiB on the S3D path (thousands of
  restores); performance, not correctness, and not touched.
