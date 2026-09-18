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
(executor, 2026-09-17)

Every figure below is `[measured]` unless it says otherwise. Full evidence:
`review/T500/README.md` and `review/T500/pi/`.

### Leg A — the split CLOSES, at −1.28 %

Will's addendum of 2026-09-17 made this the unit's deliverable rather than its
preamble, with a STOP if it did not close. It closes, but only after the
residual turned out to be five things rather than the plan's three, and the
last of them was in the ledger, not in the loader.

x86, twelve global_chr races, `Category::EntityModels`, deltas:

- the ten pre-existing terms covered **30,858,350 of 59,275,152** — 48 % was a
  residual nobody had named.
- the plan's three new terms: `merged_tex_map` **2,376,336** (12 maps, 29,688
  entries, **exactly 1 distinct**), `src_char_*` **4,906,281** (global
  2,049,649 / numbered 2,856,632 / zone 0 / other 0 — an exact SUBDIVISION of
  `characterModelBytes`, which did not move), `dup_geometry` **4,863,104**.
  That reached 46,204,592, residual 22.05 %.
- what the rest was made of, and the plan did not anticipate:
  **`src_skel_*` 14,401,943** — nothing had ever charged the skeletons the
  source `CharacterModel`s retain, and 12,629,571 of it is global2-7;
  **`skel_containers` 7,962,825** — what `calcSkeletonBytes()` omits (the map
  nodes holding the tracks, the `TrackDef`/`TrackRef` objects, the `shared_ptr`
  control blocks, a flat 64 B per `Animation` track where the real cost is
  ~152); `material_lists` 247,372; `tex_objects` 289,911;
  `loader_tex_maps` 407,016; `container_capacity` 4,672.
- **`block_overhead` 5,786,646 over 185,396 live blocks, 31.2 B each.** The
  ledger charges the whole glibc chunk — caller bytes, glibc's size word, its
  16-byte rounding, and the ledger's OWN 16-byte `LedgerHeader` — so no logical
  tally can ever reach `liveBytes`. This is the one that needed a ledger
  change: `CategoryStats` now carries `liveRequestedBytes`, `liveBlocks` and
  `blockOverheadBytes`, and the split is asserted against the logical side.
  The header grew no bytes — the cached chunk size was replaced by the
  requested size and the chunk is recomputed from glibc's own chunk word.

**Final: 54,172,089 tallied against 53,485,626 live requested, residual
−686,463 = −1.28 %.** The sign is an over-count and its cause is known: SSO
strings are charged `capacity()` = 15 where they cost no heap. `[assumed]`
that this is the whole of the −1.28 %; it is the only over-charging term found
and the magnitude is right, but it was not isolated.

Also from the addendum: `RssShmem` is read and reported (it was read nowhere),
`VmRSS` is on the `[memledger]` line, and `recordMappings()` at `startup` and
`ready` prints EVERY owner row rather than the top 16.

### Leg B — target (b), not (a); −19.0 MB on x86; red-first on ledger bytes

(b) is ~13× (a): 2.38 MB of map copies against ~32 MB of retained source data.
Inside (b), the plan's first option — dropping `partsWithTransforms`/`rawParts`
once the combined geometry is registered — **hits STOP 5**:
`preloadVariantModel()` re-reads exactly those vectors of an already-combined
character to pick body/head variant parts, and WearChange reaches it. Not done.

The plan's second option is what was done, in its narrowest form.
`getMergedTextures()` called `loadNumberedGlobalModels()` for global2-7's
TEXTURES and the call retained every `CharacterModel` in all six archives —
**15,486,203 B** — for archives the default old-models path takes no model
from. The textures are still loaded eagerly; the characters are loaded per
archive, on demand, by `ensureNumberedGlobalCharacters()`, which
`loadModelFromNumberedGlobal()` calls. **Nothing is dropped**: the worst case
(a race in none of the six) does the work it did before, and the test's own
evidence is that ONE numbered archive is still retained after loading twelve
global_chr races, because one of those races' models really comes from it.

Red (pre-fix): `src_char_numbered` 2,856,632 and `src_skel_numbered`
12,629,571 asserted == 0 on the texture path alone; logical live 53,485,626
against a 40 MiB bound. Green: 0, 0, and 34,449,051.

| | before | after |
|---|---:|---:|
| logical live | 53,485,626 | **34,449,051** (−35.6 %) |
| chunk live | 59,272,272 | 37,734,784 |
| live blocks | 185,396 | 102,371 |
| `tex_numbered` | 9,781,900 | 9,781,900 (unchanged) |
| split residual | −1.28 % | −0.88 % |

### Leg C — the Pi says the win does not reach it

Four runs (two zones × two builds), LOCAL lane, all exit 0, right zone, no OOM.

| zone | `entity_models` live @ ready | | peak | |
|---|---:|---:|---:|---:|
| | before | after | before | after |
| qeynos2 | 23,985,576 | 23,985,552 (**−24 B**) | 62,543,112 | 62,542,784 |
| befallen | 19,805,912 | 19,805,904 (**−8 B**) | 62,536,456 | 62,536,344 |

`getMergedTextures()` already guards the numbered-global load with
`!graphicsArchiveIndex_`, and the orangepi preset sets the index — so the
defect Leg B fixed does not exist there. The after-run's `[modelmem]` confirms
it from the other side: `src_char_numbered=0 src_skel_numbered=0
tex_numbered=0`. Board **B844**. The x86 win is real for every
non-archive-index consumer (the tools, the tests, `--no-graphics` rigs) and it
is worth having, but it is not a Pi win and is not reported as one.

RssAnon/RssFile/`over_target`/`08_Entities` moved in OPPOSITE directions in the
two zones and are not quoted as a result — qeynos2 −21.0 MB anon / +22.4 MB
file with VmRSS +1.4 MB, befallen +18.1 MB anon / +12.4 MB file. All four runs
swapped (528–38,916 kB). **This is exactly why the addendum asked for RssFile:
an anon-only table would have claimed a 21 MB win that does not exist.**
`unaccounted` 1,290,240 → 0 on qeynos2 and 0 in both befallen runs, which is
the same re-split, not a reduction. `malloc_slack` moved +615,888 /
−779,944 B at `ready` — noise against a 19–32 MB base — and then 31.58 →
47.67 MB between `ready` and `steady_60s` while the ledger freed 17.4 MB and
RssAnon fell 0.09 MB (B841).

The Pi's own head, from the `[modelmem]` line and not contested by B843:
`otherChrCaches_` (`src_skel_other` 4,106,346 + `src_char_other` 2,693,014 =
6.80 MB) then `dup_geometry` 5,381,308. That is the next unit's target.

### The finding that outlives this unit: B843

The `[modelmem]` tally closes to −0.9 % on x86 and **over-counts by 125 % on
the Pi** (49,778,752 against 22,146,989). The x86 test loads every model under
`ensureModelDataLoaded()`'s `EntityModels` scope; the Pi has
`graphicsArchiveIndex_` set and the same `TextureInfo` pixels are allocated
under other scopes. Nothing is lost — the ledger's own total is unaffected —
but a shared structure has two claimants and 100 % accounting allows one.
Boarded rather than fixed: the plan forbids touching the ledger's categories,
and deciding who owns a shared `TextureInfo` is Will's call.

### STOPs honoured

No budget moved (B831 still open). No golden re-blessed. Nothing that changes
what is drawn: no texture size, variant set, `chrCacheMaxEntries`,
`entityLoadRadius`/`entityRenderDistance`, no composite entities. The split
closed within 2 %. Leg B's fix drops nothing a reachable path reads — and the
alternative that would have (option 1, the `rawParts` drop) was stopped.

### STOP REACHED — the visual gate's memory-coverage rows (STOP 2)

`visual_gate.py` FAILS, and only on `characterModelsBytes`, by **exactly
1,807,682 B in all three zones**:

| zone | expected | got |
|---|---:|---:|
| qeynos2 | 25,743,220 | 23,935,538 |
| gfaydark | 25,374,894 | 23,567,212 |
| befallen | 25,502,699 | 23,695,017 |

A constant across three different zones, and it is Leg B's own measured number:
`src_char_numbered` 2,856,632 → 1,048,950 = 1,807,682. So the gate is
reporting the fix, correctly, and its expectation is stale.

**The three PNG goldens are untouched and the image comparisons pass** — the
memory-coverage run is a separate benchmark invocation that renders nothing
(T80). But the expectation lives in `tests/goldens/manifest.json`, and STOP 2
is "any golden re-bless", unqualified. **Not re-blessed. Reported.** Whoever
approves it should note that this is a memory row in the manifest and not a
pixel, that only `characterModelsBytes` moves, that the delta is identical in
all three zones and equals the measured saving, and that no budget in
`config/constrained_presets.json` is involved (STOP 1 / B831 untouched).

Everything else in the Proof bar is met: `cmake --build` exit 0 at -O2 and -O0;
`build-gles2-x86.sh` exit 0; `build-arm-noble.sh` exit 0 (both tips);
`check_file_sizes.sh --all` 1,249 files, 0 warn, 0 fail; the new ledger-byte
test byte-identical at -O0 and -O2 (B819); full ctest **4,913 of 4,915** — the
two reds are this gate and B845's `-j` isolation flake, which is green alone.

### Territory taken outside the plan, and why

`include/client/core/memory_ledger.{h,cpp}` (requested-bytes tracking,
`RssShmem`), `src/client/application.cpp` and
`src/client/bridge/bridge_component.cpp` (full `[memmap]` census),
`tests/test_memory_ledger.cpp` (the two new schema keys),
`scripts/refactor/check_write_only_fields.py` (the B337 exemption for
`MemoryStats::loadedModelCount` went stale the moment `[modelmem]` printed it),
`CMakeLists.txt` + `tests/cmake/graphics.cmake` (four targets that compile
`model_loading_search.cpp` now need the ledger TU). The ledger work is Will's
addendum; the rest is fallout from it. **The ledger's CATEGORIES were not
touched.**
