# T500 — the Pi's largest bucket at `ready`: `entity_models`, at the root

Class A. Will's standing decision (2026-09-17): "willeq's RSS footprint between 32 and
64 MB". T496 put `startup` inside target — RssAnon 58,331,136 B (55.63 MiB), `over_target`
0 [measured]. At `ready` the client is still **177,709,056 B (169.48 MiB)** and the residue
is zone/entity data. This unit takes the head of the *`ready`* queue. Leg A is offline;
Leg C needs the LOCAL eqemu lane + the Pi. Board entries start at **B836**. Base: the fable
tip carrying T496 Leg D (after the chain).

## What exists (read first)

All figures from the T496 Leg D confirming run (`review/T496/pi/raw/qeynos2_T496legD/ledger.txt`,
qeynos2, post-B817/B830) — the only Pi run that reflects the tip. [measured]

| bucket | `ready` live | `ready` peak | `steady_60s` live |
|---|---:|---:|---:|
| `malloc_slack` | 30,299,600 | — | 46,388,456 |
| `startup` | 28,121,184 | 28,121,288 | 28,120,960 |
| `audio` | 25,725,824 | 25,725,824 | 25,725,824 |
| **`entity_models`** | **23,985,624** | **62,543,336** | 14,409,184 |
| `zone_mesh` | 21,801,424 | 23,625,672 | 17,844,312 |
| `entity_instances` | 19,416,224 | 63,033,456 | 14,933,920 |
| `game_state` | 7,578,288 | | `spatial` 6,585,640, `zone_parse` 6,093,384 |

Two facts that pick the target:
1. `entity_models` is the largest non-fixed bucket at `ready` and has the largest peak
   (62.5 MB) — it sets the RSS high-water. `[LOAD-MEM-AUDIT]` step `08_Entities` moves anon
   131,904 → 172,680 kB, **+40,776 kB — the largest single load step**. [measured]
2. Between `ready` and `steady_60s` the ledger frees 16.0 MB and RssAnon falls **0.85 MB** —
   glibc keeps the arena (`malloc_slack` 30.3 → 46.4 MB). **A byte freed late is not a byte
   returned**; the win must be a byte never allocated or freed before the peak. [measured]
   Board **B841**.

Code behind the four buckets, read at the tip:
- **`entity_models`** — `race_model_loader.{h,cpp}` + `eq/model_loading*.cpp`,
  `eq/variant_loading*.cpp`, `eq/mesh_building.cpp`. Three duplications:
  (a) **`RaceModelData::textures` is a full COPY of the merged character-texture map, per
  model.** `getMergedTextures()` (`mesh_building.cpp:13`) caches `cachedMergedTextures_` and
  **returns it by value**; eight sites assign that value into a `RaceModelData`
  (`model_loading.cpp:445`, `model_loading_chr.cpp:125,154,482,507`, `variant_loading.cpp:202`,
  `variant_loading_animation.cpp:262`, `variant_loading_preload.cpp:260`), three more copy it
  entry-by-entry on top of an S3D map (`model_loading.cpp:303-309`,
  `model_loading_search.cpp:102-108,137-143`). Pixels shared, the **map is not**.
  `getMemoryStats()` counts none of it (`model_loading_search.cpp:533-551`). [measured] Bytes
  [assumed] until Leg A.
  (b) **The source `CharacterModel`s are retained for the whole process.**
  `globalCharacters_` / `numberedGlobalCharacters_` hold `parts`, `partsWithTransforms` and
  `rawParts` for every character in `global_chr.s3d`/`global2-7`, including races no spawn
  uses; `zoneCharacters_.clear()` exists (`model_loading.cpp:213`) but there is no
  `globalCharacters_.clear()` anywhere. [measured]
  (c) `RaceModelData::decodedTextures` (`race_model_loader.h:50`) has zero writers — dead
  field. [measured] Board **B840**.
- **`entity_instances`** — `createInstanceMesh()` (`eq/animated_mesh_scene_node.cpp:81`)
  copies Vertices AND Indices per entity; skinning (`animated_mesh_skinning.cpp:240-245,327-332`)
  writes only `Pos`/`Normal`. The skeleton IS shared (`shared_ptr`) — T496's per-entity
  skeleton worry is refuted. [measured] Index share ~10 % [assumed]; next unit.
- **`zone_mesh`** — after `uploadMeshHardwareBuffers()` the draw binds the VBO only, but the
  CPU arrays are still read by `ZoneMergeBuffers::build()` (first frame after loading,
  `mergeZoneDraws: true` on orangepi), `createTriangleSelector()` (load step 9) and the
  client-array fallback. Largest-looking, smallest real win, highest risk. [measured]
  Deferred, **B838**.
- **`startup` 26.82 MiB** — no Pi-side attribution exists; x86 heads (cursor 8.70, loading
  screen 7.47) were wrong in direction on Mali (B812). Needs its own measurement leg.
  [measured] Deferred, **B839**.

**Order:** 1. `entity_models` (this unit); 2. `entity_instances` (share the index array);
3. `startup` (Pi attribution leg first); 4. `zone_mesh` (merge/selector ordering first).

## Tasks

### Leg A — the byte split (offline, ADDITIVE ONLY)
1. Extend `RaceModelLoader::MemoryStats`: `mergedTextureMapBytes` (per `RaceModelData::textures`
   over `loadedModels_` + `variantModels_` + `preparedModelData_`, plus distinct-map count),
   `sourceCharacterBytes` split global / numbered / zone / other, `duplicateGeometryBytes`.
   Report from `getMemoryStats()` and one `LOG_INFO(MOD_GRAPHICS)` `[modelmem]` line at `ready`.
2. Prove the split sums: `[modelmem]` total vs `live(Category::EntityModels)` at the same
   instant, residual stated, in a ctest case. A split that does not close is not a split.
3. No free(), no lifetime moved. Output = the bound for Leg B's red.

### Leg B — the root fix (offline, RED FIRST, on ledger bytes)
Target = whichever of (a)/(b) Leg A measures larger. Headless test asserting
`Category::EntityModels` live bytes under a stated bound, failing at this tip on x86.
- **If (a)**: RED — load K race models sharing one merged map; assert `live` after K ≤ `live`
  after 1 + (K−1) × B_model (geometry+skeleton only). Root fix: `RaceModelData::textures`
  becomes `std::shared_ptr<const std::map<...>>` aliasing `cachedMergedTextures_` at the eight
  wholesale sites, a small per-model override map at the three layering sites, one
  `findTexture()` accessor for the ~8 read sites (`animated_mesh_creation.cpp:231,599-617`,
  `variant_loading.cpp:75-76,360`). Same pointers, same lookups, same pixels.
- **If (b)**: RED — after the zone's models are combined, `sourceCharacterBytes` ≤ B. Root fix:
  drop `partsWithTransforms`/`rawParts` once the combined geometry is registered, or load
  `globalCharacters_` per race code. **STOP if any reachable path (WearChange, a late spawn of
  an unseen race, a variant preload) would need a dropped part** — a re-parse is load time
  (secondary), a MISSING model is a pixel change (out of scope).
- Green on the same red; existing `getMemoryStats()` fields must not move.

### Leg C — confirm on the Pi (LOCAL lane, T496 Leg A recipe verbatim)
`scripts/build-arm-noble.sh`; deploy per `review/AUDIT-0914/scripts/run_pi.sh`;
`review/T496/scripts/pi_sweep.sh` (B802 read-back) driving `pi_zone_run.sh`, one run each for
**qeynos2** and **befallen**: `--drm --renderer gles2 --constrained orangepi -r 1280 720
--load-mem-audit --log-module=MAIN:INFO` (+ GRAPHICS:INFO for `[modelmem]`). Record `[memledger]`
at startup/login_world/ready/steady_60s, `[memmap]` at startup/ready, `08_Entities`, `[modelmem]`.
Output `review/T500/pi/table_entity_models.tsv`: before/after `entity_models` live AND peak,
RssAnon, over_target, `08_Entities` anon delta. Every row names the zone actually entered.

### Leg D — record
`docs/decisions.md` (the `ready`-point queue and its order); **B836** the order + table;
**B837** the root cause + its red; **B838** zone_mesh after VBO upload; **B839** `startup` has
no Pi attribution; **B840** `decodedTextures` dead field; **B841** arena retention.
`src/client/graphics/CLAUDE.md` + `docs/module_map.md` if a TU moves.

## Proof
`cmake --build` exit 0; full ctest green incl. the new test at **-O0 and -O2** (B819);
`build-gles2-x86.sh`; `build-arm-noble.sh`; `visual_gate.py` PASS with goldens untouched
(rerun alone on a quiet host); `check_file_sizes.sh --all` 0 fail; the Pi table.

## STOP (report, do not proceed)
1. **No budget may move** — B831 is open (one budget table asserted on llvmpipe and Mali;
   `entity_models` measured 64.56 MiB on x86 vs 22.87 on the Pi).
2. Any golden re-bless.
3. Anything that changes what is drawn: no texture size, variant set, `chrCacheMaxEntries`,
   `entityLoadRadius`/`entityRenderDistance`; **do not enable composite entities** (orangepi
   preset has none; both composite caches are empty on the Pi).
4. Leg A's split does not close within ~2 %.
5. Leg B's fix needs a part a reachable path still reads.

## Rules
LOCAL lane only; `eqemu_lock_take T500`; release on every path; `down.sh`; world DOWN before
the character restore (T493). Never the VM, never hermes-ext, no Testchar. Private scratch
`scratchpad/T500/`. No ccache; -j8; explicit `CMAKE_BUILD_TYPE`. Territory:
`src/client/graphics/eq/race_model_loader.{cpp,h}` + `eq/model_loading*.cpp` +
`eq/variant_loading*.cpp` + `eq/mesh_building.cpp`, `tests/test_entity_model_sharing.cpp` (new),
`review/T500/**`, `refactor/T500_*.md`, BOARD, `docs/decisions.md`. NOT
`config/constrained_presets.json` (B831), NOT goldens, NOT the gate's zone set, NOT the
ledger's categories, NOT `renderer_zone_*`/`renderer_zone_merge.cpp` (B838), NOT
`entity_mesh_build.cpp`/`animated_mesh_scene_node.cpp` (next unit).

## Review
(executor)
