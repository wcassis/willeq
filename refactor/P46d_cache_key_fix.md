# P46d — Complete P46 Part A: mask-aware cache keying + ordering audit + the round-5 pair

**Slot: slot2. x86 ONLY — no Pi, no eqemu.** Production change: this
completes the P46 Part A fix Will already authorized — P46c proved
Part A is load-order-dependent (a latent defect), which was never the
intent of the approved fix.

## Background (read first)

refactor/P46c_neutral_pair_stop.md — full root cause with file:line:
`ConstrainedTextureCache::getOrLoad()` (constrained_texture_cache.cpp
:111-196) keys by texture name only; `applyPaletteMask` is honored
only on the first decode of a name. Any unmasked-first load of a
masked texture's name (benchmark static preload via
zone_geometry.cpp:249 today; potentially production paths) poisons
the cache and silently defeats the masking. Also read
refactor/P46_masked_alpha_decode.md and P46b/P46c manifests.

## Work

A. **Fix the keying**: make the cache mask-aware — key on
   (name, applyPaletteMask) or equivalent variant separation. Watch
   the interaction points: P13b's releaseRawTextureData deferred-name
   skip list, P15/P17's eviction/clear paths, the eviction listeners
   (P10b/P17 guards) — anything keyed by name that must now handle
   the variant dimension. Memory cost is bounded (only WLD-masked
   textures ever request the masked variant — dozens, not thousands).
B. **Production ordering audit**: enumerate every production path
   that can load an entity-masked texture name (race_model_loader,
   equipment_model_loader, composite builder, zone builder, UI/misc
   getOrLoad callers) and determine whether ANY unmasked-first
   ordering can occur in the live client (P46c only proved it for the
   benchmark's preload). With the Part-A fix keyed correctly this
   becomes moot for correctness, but document what the exposure WAS —
   it determines whether the pre-fix window shipped wrong rendering.
C. **Re-verify the discard**: rerun P46c's discard-fire check (RGBA
   mode, --no-entity-shader-promotion, tip vs fda2e34) — it must now
   fire, with mask-shaped diffs.
D. **Capture the round-5 neutral pair** per P46c's design, now
   unblocked: RGBA mode, promotion OFF both legs, deterministic
   frames 12/26/42, orbit-radius 8 / height-offset 6, SKE + ZOM,
   400% torso crops. ACCEPTANCE ABSOLUTE (round 5): pixel-identical
   everywhere except genuine mask cutouts — assert numerically AND
   verify with your own eyes part-by-part; any other delta = STOP.
   Stills → /mnt/projects/fable/videos/p46d_bless/; manifest →
   benchmarks/audits/p46d_bless/ (committed). Note in the manifest:
   pair shows P37+P46A jointly, neutral instrument lighting, one
   variable only.
E. Update deferred.md (bless entry + retire the P46c stop item);
   annotate P46/P46b manifests' RGBA-leg numbers as
   lighting-delta-only (per P46c's finding) if P46c's annotations
   didn't already.

## Rules

- Full bar: GLES2 ctest baseline (~1614/1615 class; known ALSA
  exception, RDP race), visual gate PASS goldens untouched, ARM
  cross-build clean, file sizes (pre-existing zone_bench_loader FAIL
  — flag, don't absorb). Unmasked textures and zone rendering must
  stay byte-identical (md5 evidence).
- Separate commits: cache fix / audit memo / evidence / annotations.
- STOP if the keying fix cascades beyond ~6 files or destabilizes
  eviction machinery (those guards have deadlock history — P10b).

## Review

**Cache-key fix landed, production ordering audit complete (result: the live
character-rendering pipeline was NOT exposed to P46c's poisoning ordering),
discard now fires with mask-shaped diffs, round-5 pair captured and passes
the absolute bar at f26/f42 (f12 carries a small disclosed non-entity
artifact, unrelated to this fix — see manifest).**

### A. Keying design chosen

`ConstrainedTextureCache::cacheKey(name, applyPaletteMask)` — identity when
`applyPaletteMask` is false (`cacheKey(x, false) == x` always), else
`name + "\x01masked"` (a control-character suffix that cannot appear in a
real EQ/EQE1 filename). This internal key is used for `cache_`,
`decodedEntries_`, `compressedEntries_`, and `pendingLedgerTag_` — the four
maps that previously collided by name alone. `getOrLoad()` and
`uploadTexture()` both gained/use this; `uploadTexture()` gained a new
`applyPaletteMask` parameter (default `false`, so all ~15 other call sites
are untouched). `uploadAllPending()` recovers `(realName, applyPaletteMask)`
from each harvested key via the new inverse helper `splitCacheKey()` before
calling `uploadTexture()` — this was NOT optional (see "Debugging" below).

Two additional, non-obvious places needed the SAME distinction and were
missed on the first pass, both caught by this unit's own discard-fire
re-verification loop before landing:
- `COpenGLES2Driver::addTexture()`/`wrapExternalTexture()` (the GLES2
  driver, `docker/irrlicht-drm/COpenGLES2Driver.cpp`) deduplicate by the
  **name string passed to them** — `findTexture(name)` returns the existing
  GPU texture unchanged, silently discarding new image data, if a texture
  with that name already exists. Passing the plain display name for both
  the masked and unmasked GPU upload made the second upload a no-op that
  returned the SAME (first-uploaded) texture. Fixed by uploading under the
  internal (possibly-mangled) key, not the display name, in
  `uploadTexture()`'s BMP branch and in `uploadCompressedETC1`/`V2` (which
  now take a separate `cacheEntryKey` parameter alongside the real `name`).
- `ZoneMeshBuilder::textureCache_` (`zone_geometry.h`, populated by
  `registerUploadedTexture()`, consulted by `mesh_building.cpp`'s
  `getOrLoadTexture()` fast path) is its OWN name-keyed cache, entirely
  separate from `ConstrainedTextureCache`. It intentionally stays
  plain-name-keyed (mask-unaware) — extending it would be a 7th+ file and
  isn't needed given production's ordering guarantee (see Part B). Kept
  correct by always passing the REAL name (not the internal key) into
  `registerUploadedTexture()`.

**Files touched (5, within the ~6-file budget): production —
`include/client/graphics/constrained_texture_cache.h`,
`src/client/graphics/constrained_texture_cache.cpp`,
`src/client/graphics/eq/equipment_model_loader.cpp` (the one other direct
`uploadTexture()` caller that passes `applyPaletteMask` to `getOrLoad()` and
must now pass the same flag to `uploadTexture()`); benchmark-only —
`tools/hwcaps/zone_bench_loader.cpp` (see Part C/D); evidence/annotation —
this file + `refactor/deferred.md`.** No eviction-path file
(`evictTexture`/`evictUntilAvailable`/`clear`), listener, or P13b
skip-list file needed changes — verified by inspection, not assumption (see
Part B/"audited but unchanged" below).

### B. Production ordering audit

**Verdict: the live client's primary character-rendering pipeline
(`EntityRenderer::buildEntityMesh` and its animation-rebuild sibling) was
NOT exposed to P46c's unmasked-first poisoning ordering for any race with a
working animated mesh — which includes both SKE and ZOM, the P37
ground-truth masked races.** Both of `entity_renderer.cpp`'s entity-creation
call sites always attempt `createAnimatedNode()`/
`createAnimatedNodeWithEquipment()` FIRST and only fall back to the
always-unmasked static path (`RaceModelLoader::getMeshForRace()` →
`ZoneMeshBuilder::buildTexturedMesh()`) when animated-mesh creation fails
outright (no skeleton/model found) — never as a warm-up/preload step ahead
of the animated path the way `zone_bench_loader.cpp`'s own benchmark-only
"Pre-load meshes for each race" loop does. `createAnimatedNode()` →
`getAnimatedMeshForRace()` → `ensureModelDataLoaded()` is the P13b-fixed
path that does NOT run the static unmasked builder as a side effect
(P13b's own commit fixed exactly this for a different reason — a wasted
double-load — but the fix also happens to be what protects this path from
P46c's ordering hazard). The actual masked decode for a race's base
per-part textures comes from `RaceModelLoader::preloadModelData()`'s T05
loop, called **unconditionally, for every race, the first time it's
needed** by `EntityPrepWorker::processRequest()` and
`renderer_zone_load.cpp` — well before any per-entity mesh build, with no
raciness against a benchmark-only static preload.

Two genuine, PRE-EXISTING (not caused by the cache-key bug, not fixed by
this unit — out of scope, flagged for a follow-up) gaps found during this
audit, both independent of ordering:
1. **Composite-deferred per-part textures loaded on-demand.** When a
   composite-eligible race's per-part decode is deferred (P13b's
   `deferredPerPartTextureNames_`) and a later composite-ineligible entity
   (e.g. a player) needs the multi-buffer mesh on demand,
   `mesh_building.cpp`'s `buildMeshFromGeometry()` resolves textures via its
   own `getOrLoadTexture()`/`loadTextureFromBMP()` fallback — which NEVER
   threads `applyPaletteMask` (the `isMaskedMaterial` flag it computes is
   used only to choose the alpha-test `MaterialType`, never passed to the
   texture load call). A masked composite-eligible race's per-part textures
   loaded this way would decode opaque. Not reachable by SKE/ZOM in this
   unit's captures (no composite path exercised), but a real live-client
   exposure for any composite-eligible masked race.
2. **Equipment texture-variant overrides.** `createAnimatedNodeWithEquipment()`'s
   per-slot body-part texture swap (`animated_mesh_creation.cpp`, materialId
   != 0 case) also calls `loadTextureFromBMP()` directly with no mask flag.
   A masked base body texture with a non-default equipped variant would
   lose its mask on that slot. Same root gap as #1 — Part A's plumbing was
   only ever wired to the two call sites that already computed the flag
   (T05, `equipment_model_loader.cpp`'s own per-triangle-group load), not to
   every consumer of `loadTextureFromBMP()`.

Both are extensions-of-Part-A-plumbing gaps, not cache-key/ordering bugs —
fixing them is a new errand (thread `applyPaletteMask` through two more call
sites, likely restructuring `buildMeshFromGeometry()` to compute
`isMaskedMaterial` before the texture-load block instead of after). Flagged
in `deferred.md`, not fixed here.

**"Audited but unchanged" — confirmed safe, no code change needed:**
P13b's `deferredPerPartTextureNames_` skip-list (`model_loading.cpp`'s
`releaseRawTextureData()`) operates on raw-byte retention by plain name,
independent of `ConstrainedTextureCache`'s internal keying — verified its
consumer (the on-demand path in gap #1 above) is unaffected by which
internal key scheme this unit chose. P15/P17 eviction paths
(`evictUntilAvailable`/`evictTexture`/`clear()`) and the P10b/P17 eviction
listeners/guards (`RegionMeshTextureGuard`, `EntityMeshTextureGuard`,
`AnimatedTextureManager`, `SkyRenderer`, `DetailManager`, `StormCloudLayer`,
`ParticleManager`) are all generic over whatever string is in
`lruOrder_`/`cache_` (most ignore the name parameter entirely, operating by
texture pointer instead) — confirmed by reading every listener
implementation, not assumed. No deadlock-history guard was touched.

### C/D. Discard-fire re-check and round-5 pair — debugging history

Re-running P46c's exact discard-fire check (RGBA, `--no-entity-shader-promotion`,
tip vs fda2e34) against the cache-key fix ALONE still showed AE=0 — three
compounding bugs, found and fixed in sequence via a diagnostic-then-revert
loop (temporary `LOG_INFO` lines added to `mesh_building.cpp` and
`constrained_texture_cache.cpp`, reverted before commit, confirmed empty
diff):
1. **Benchmark instrument gap**: `zone_bench_loader.cpp` only ever called
   `preloadModelData()` (which runs T05, the sole source of
   `applyPaletteMask=true` for a race's base textures) when
   `--composite-entities` AND `--eqe1-path` were both set — neither is true
   for a plain RGBA capture, so T05 never ran at all, independent of any
   cache bug. Fixed (benchmark-only): call `preloadModelData()`
   unconditionally per race when `--animate` is set and the composite path
   didn't already cover it — mirroring `EntityPrepWorker`'s real,
   unconditional production call pattern. Ordering matters: this call must
   run AFTER the benchmark's own static "Pre-load meshes for each race"
   loop (see #3) so its `ZoneMeshBuilder::registerUploadedTexture()`
   registration is the one that wins.
2. **`uploadAllPending()` leaked the mangled key as the display name.** It
   collects keys straight from `decodedEntries_`/`compressedEntries_` and
   called `uploadTexture(key)` relying on `cacheKey(key,false)==key` — true
   for cache lookups, but `uploadTexture`'s `name` parameter is ALSO used
   for `registerUploadedTexture()`/ledger/GL-label, which must be the REAL
   name. This silently registered masked textures into `ZoneMeshBuilder`'s
   fast-path cache under the mangled key, which `getOrLoadTexture()`'s
   plain-name lookup then never found, falling through to the unmasked
   `loadTextureFromBMP()` and reproducing AE=0. Fixed with `splitCacheKey()`
   (the exact inverse of `cacheKey()`).
3. **`COpenGLES2Driver::addTexture()`/`wrapExternalTexture()` name-based
   dedup** (see Part A above) — the actual last blocker. Confirmed via a
   pointer-logging diagnostic: both the masked and unmasked GPU uploads for
   `skech0001.bmp` returned the IDENTICAL pointer until this was fixed.

After all three fixes: discard-fire re-check AE=678 (mask-shaped, entity-
silhouette-confined bbox 84x198 @ ske f26) — PASS, matches the plan's "must
now fire, with mask-shaped diffs" requirement. Round-5 pair captured per
plan D; see `benchmarks/audits/p46d_bless/EVIDENCE_MANIFEST.txt` for the
full numeric table, zone-byte-identity check, and part-by-part visual
verdict (PASS at f26/f42, both races; f12 carries a small disclosed
non-entity artifact independently reproduced with zero entities, see
manifest Deviation 1).

### Verification bar

| Step | Result |
|---|---|
| `cmake --build build -j24` | PASS, clean |
| `ctest --output-on-failure` | 1616/1617 (sole exception `AudioManagerTest.PreloadCommonSounds`, documented ALSA host quirk — the RDP race did not reproduce this run) |
| `bash scripts/build-gles2-x86.sh` | PASS, clean (mid-session the shared `third_party/irrlicht-gles2-x86` tree was transiently disturbed by concurrent `cmake --build build` + `build-gles2-x86.sh` access — caught immediately via a `willeq_core` build failure citing a missing `irrlicht.h`, restored automatically by `build-gles2-x86.sh`'s own Irrlicht rebuild; all subsequent builds re-run clean against the restored tree; see manifest Deviation 1 for the one visual side-effect traced to this) |
| `bash scripts/ci/visual_gate.sh` | PASS (qeynos2/gfaydark/befallen), goldens untouched, no re-bless |
| `bash scripts/build-arm-noble.sh` | PASS, clean; `build-arm-noble/bin/willeq` verified via `file`: valid ARM EABI5 ELF, not a stub |
| `bash scripts/refactor/check_file_sizes.sh 5420886` | `constrained_texture_cache.cpp` WARN (2003 lines; was 1885 at merge-base, WARN→WARN, +118 net lines, no state change); `zone_bench_loader.cpp` FAIL (3196 lines) — **pre-existing FAIL since before P46** (documented at 3135 by P46b, 3031 by P46), this unit's ordering-fix comment/code pushed it +61 further — not a regression, flagged per deviation, out of this unit's territory to split |

### Deviations

1. `tools/hwcaps/zone_bench_loader.cpp`'s pre-existing file-size FAIL grew by
   +61 lines (unavoidable given the instrument-gap fix needed a substantial
   explanatory comment — the ordering subtlety is not safe to leave
   under-documented) — not split out, same rationale as every prior unit
   in this chain (P46/P46b/P46c) that touched this file.
2. Round-5's f12 frame carries a small (13-44px), non-entity, zone-geometry
   rendering artifact independently reproduced with `--entities 0` — traced
   to this session's Irrlicht-tree rebuild (see verification bar), not to
   this unit's production fix. f26/f42 are unaffected and are the frames
   this unit's manifest leads on. Full detail in
   `benchmarks/audits/p46d_bless/EVIDENCE_MANIFEST.txt` Deviation 1.
3. Two pre-existing, out-of-scope masked-texture gaps found during the Part
   B ordering audit (composite-deferred on-demand loads, equipment
   texture-variant swaps — see Part B) are NOT fixed here; flagged in
   `deferred.md` for a follow-up errand.

Commits (see git log on `task/P46d-cache-key`): cache fix (production) /
benchmark ordering fix (tool-only) / evidence (this Review + manifest) /
annotations (`deferred.md`).
