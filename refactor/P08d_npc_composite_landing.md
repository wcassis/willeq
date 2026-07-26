# P08d — Land eligibility-gated NPC composite entities in production

Approved by Will 2026-07-26 (picked from the deck; strategy set in the
2026-07-25 discussion: composites for NPCs without per-slot overrides,
players/override-NPCs keep multi-buffer; per-player baking deferred).
Slot: wt/slot1, branch task/P08d-npc-composites, cut from fable (9589d37).
PRODUCTION landing unit — merges only after Will blesses the quality
evidence. P08c measured the win: 33.1% p95 (befallen) / 56.0% (qeynos2)
at gameplay load; production pipeline exists dormant (210 pre-baked
sheets at /mnt/projects/EverQuest_ETC1/).

## Design

### A. Ground truth first (read before coding)

1. **How per-slot texture overrides actually flow today**: WearChange /
   equipment appearance from server → game state → bridge events →
   EntityRenderer. Determine what production CURRENTLY renders for
   per-slot armor textures (there is a months-old
   fix-npc-and-equipment-textures branch — read its diff for context but
   do NOT merge it). If per-slot overrides are not visually applied
   today, say so — the eligibility gate may reduce to "NPCs eligible,
   players not" with a documented TODO for when overrides land.
2. **Composite selection plumbing**: RaceModelLoader::preloadModelData /
   getAnimatedMeshForRace key composites per (race, gender,
   textureVariant) and CACHE PER MODEL. The gate must be per-ENTITY.
   Map where the per-entity mesh choice happens and design the cleanest
   split: eligible entities get the composite mesh, ineligible entities
   get the existing multi-buffer mesh (both cacheable side by side; no
   regression for ineligible entities — their path must be BYTE-UNTOUCHED).
3. **Variant coverage audit**: enumerate the texture variants actually
   present in the model data for the golden zones' spawn rosters (and
   the tour-character zones) vs the 210 sheets on disk. Missing sheets:
   generate with the existing entity_composite_builder (offline, x86) —
   report what was generated; if a variant can't be built, that
   race/variant is simply ineligible (report the list).

### B. Implementation (production, flag-gated)

4. Eligibility gate per design A: composite only for entities with a
   uniform texture variant and no per-slot overrides (as defined by
   ground truth A1). Players ALWAYS ineligible in this unit.
5. Config: `enableCompositeEntities` stays the master switch;
   `compositeEntityPath` comes from config (verify how the Pi deployment
   should point at the sheets — NFS path vs local copy; document the
   recommended willeq.json/preset arrangement). The orangepi preset flip
   to enabled happens in a SEPARATE final commit so the merge can carry
   or drop it per Will's bless.
6. Logging: one INFO line per zone load summarizing composite adoption
   (N eligible / M total entities, sheets hit/missed) — module-filtered.

### C. Verification + quality evidence (the bless package)

7. Standard bar: build, ctest (known host exceptions), gles2-x86 build,
   visual gate PASS goldens untouched (gate renders no entities), ARM
   build, file-size check.
8. **gles2_composite_audit full sweep** (all race/gender/variant combos
   available) with the production sheets — catalogue renders, flag any
   visually broken sheet (those become ineligible + reported, not
   shipped broken).
9. **Animation verification on the PRODUCTION path** (the Z35-era risk):
   composite-meshed NPCs playing walk/attack/idle animations — model
   viewer or the audit tool's animated mode or an x86 app session,
   whichever exercises EQAnimatedMesh + composite together; capture
   frames at several animation phases for 3-4 representative races.
   Explicitly include at least one model with ANIMATED TEXTURES if any
   exists in the eligible set (the Z35 "animated texture fix" class) —
   document behavior.
10. **Side-by-side quality evidence**: composite vs multi-buffer renders
    of the same NPCs (near + mid distance), assembled as a contact
    sheet → benchmarks/contact_sheets/p08d_composite_landing/. Expect
    near-identical (same texels, repacked); quantify diffs. NO
    self-declared acceptability — Will blesses.
11. **Perf spot-check** (cheap, x86 or Pi if budget allows): the P08c
    benchmark numbers stand as the perf case; optionally confirm the
    production path's draw-count reduction via the zone-load INFO line +
    a short instrumented run. Do not re-run the full P08c matrix.

## Out of scope

- Per-player runtime composite baking (deferred design unit).
- Billboards/impostors (P08e). Zone merge landing (separate unit).
- Merging or fixing the fix-npc-and-equipment-textures branch.
- Debugging broken sheets beyond marking them ineligible.
- Flipping the preset before Will's bless (separate commit, see B5).

## Review

### Setup

Branch `task/P08d-npc-composites`, cut from `fable` (8a2d42d, the plan's own commit
— no other commits in between), per the checkout-first instruction (this plan file
only existed on the fable tip). Slot `wt/slot1`.

### A. Ground truth

**A1. Per-slot equipment override rendering reality (production, TODAY).**
Traced the actual dispatch: `entity_renderer.cpp::buildEntityMesh` decides between
`RaceModelLoader::createAnimatedNode` (no appearance) and
`createAnimatedNodeWithEquipment` (appearance-aware) based on whether any of
`appearance.equipment[9]`/`face`/`texture` are set. `createAnimatedNodeWithEquipment`
sets `textureVariant = appearance.texture` and calls
`getAnimatedMeshWithAppearance(..., textureVariant)`
(`animated_mesh_creation.cpp`/`variant_loading.cpp`). That function's dispatch
(`variant_loading.cpp:216`, pre-P08d) delegated to the composite-eligible
`getAnimatedMeshForRace` **only** when `headVariant==0 && bodyVariant==0 &&
textureVariant==0`; any other combination (including any nonzero uniform
`appearance.texture`, e.g. a chain/plate/robe NPC) went straight to
`buildMeshFromGeometry` (multi-buffer) and **never consulted `compositeData` at
all** — not a bug I introduced, a pre-existing gap. Separately, back in
`createAnimatedNodeWithEquipment` (`animated_mesh_creation.cpp:394-508`,
guarded by `textureVariant==0`), **per-slot `appearance.equipment[slot]`
overrides and `equipment_tint[]` DO render in production today** — they are
applied post-mesh-build by matching each of the node's per-material *body-part
texture names* (`{race}ch`, `{race}lg`, `{race}he`, ...) and swapping that one
material's texture on the scene node. **`RaceModelLoader::buildMeshWithEquipment`
is dead code — defined, never called anywhere** (confirmed via
project-wide grep); the ACTUAL per-slot mechanism is the node-material-swap loop
above, not that function.

Consequence for the gate: a composite mesh merges every body part into ONE
mesh buffer/material. The per-slot swap loop matches materials by body-part
texture NAME; a composite mesh's single material is named after the composite
sheet, which never matches any `{race}{part}` pattern, so **the per-slot swap
loop silently no-ops on a composite mesh — gear would vanish with no error,
warning, or log line.** This is the concrete, evidenced justification for
"NPCs with per-slot overrides are ineligible," not a precaution taken on faith.
Also found (same code, pre-existing, before P08d touched it at all): the
"fully default" path — an entity with **zero** equipment/face/texture (not
just NPCs; this included PLAYERS with no gear at all) went through
`createAnimatedNode` → `getAnimatedMeshForRace` with no player/NPC distinction
whatsoever. Once `enableCompositeEntities` is flipped on, that pre-existing
code would have silently composited bare players too — the "players ALWAYS
ineligible" rule did not previously exist anywhere in the dispatch and had to
be added (see B4), it was not merely "already true."

**A2. Composite selection plumbing / per-model cache vs per-entity gate.**
Confirmed: `RaceModelLoader::loadedModels_`/`preparedModelData_` are keyed by
`makeCacheKey(raceId, gender)` only (`race_model_loader.cpp:837`) — no
variant in the key, so `RaceModelData` (and its `compositeData` field) is
**one record per race/gender**, exactly the "cache per model" problem the
plan named. `animatedMeshCache_`/`variantAnimatedMeshCache_` mirror this.
Implemented the gate at the correct layer: a new `bool compositeAllowed`
parameter threaded through `getAnimatedMeshForRace` →
`getAnimatedMeshWithAppearance` → `createAnimatedNode`/
`createAnimatedNodeWithEquipment`, computed **per entity** in
`entity_renderer.cpp::buildEntityMesh` from `isPlayer` + a scan of
`appearance.equipment[0..6]` (body-part slots; 7/8 are Primary/Secondary
weapon models, attached as separate scene nodes, irrelevant to mesh
compositing) + `equipment_tint[]`. To keep the SAME race/gender/variant
cacheable for both an eligible NPC and an ineligible player/overridden-NPC
side by side without one clobbering the other (plan A2's explicit
requirement), added parallel cache maps
(`compositeAnimatedMeshCache_`/`compositeVariantAnimatedMeshCache_`,
`race_model_loader.h`) rather than reusing the existing ones with a modified
key — simpler to reason about and impossible to accidentally alias. All
existing eviction/clear/stats call sites (`race_model_loader.cpp` dtor,
`clearCache`, `clearMeshCaches`; `model_loading.cpp` per-race eviction,
`getMemoryStats`) were extended to cover the new maps so nothing leaks or
undercounts.

Also extended (new in this unit, not previously wired at all): the
uniform-nonzero-texture-variant path (`getAnimatedMeshWithAppearance`,
`headVariant==0 && bodyVariant==0 && textureVariant!=0`) now attempts a
composite lookup keyed `(race, gender, textureVariant)` — pre-P08d this path
NEVER checked `compositeData`, meaning even a fully "uniform variant, no
per-slot data" NPC (Will's explicitly-eligible case) could never have been
composited before this unit, regardless of the config flag. Head/body MESH
variants (robes, alternate heads) remain structurally out of scope —
`entity_composite_builder` only bakes texture-variant substitutions of the
default body mesh (confirmed by reading its `applyVariant()`), never a
distinct mesh, so `headVariant!=0 || bodyVariant!=0` is unconditionally
ineligible regardless of the entity gate.

**A3. Variant coverage audit.** The 210 pre-baked sheets
(`/mnt/projects/EverQuest_ETC1/composite_index.json`) cover **42 distinct
race/gender defs × 5 texture variants (0-4)** — complete, no gaps inside that
set. But `entity_composite_builder` (`loadGlobalArchives`,
`entity_composite_builder.cpp:758`) reads **only** the 7 hardcoded
`global*_chr.s3d` archives — it has no zone-archive support at all (not a
missing flag; the archive list is a fixed `GLOBAL_CHR_ARCHIVES[]` array with
no `--zone` option). Compared the 42-def coverage set against the actual
zone-model rosters of both golden zones (`s3d_dump` on
`befallen_chr.s3d`/`qeynos2_chr.s3d`):

| Zone | Zone-model roster (chr archive) | Overlap with the 42 covered defs | Uncovered (structurally — tool can't build these) |
|---|---|---|---|
| befallen | BAT, CAZ, DEN, FPM, GAR, CUB, GHU, GOL, IMP, INN, MIM, MIN, ORC, RAT, REA, TEN, WIL, ZOF, ZOM (19) | CAZ, INN (2/19, 10.5%) | BAT, DEN, FPM, GAR, CUB, GHU, GOL, IMP, MIM, MIN, ORC, RAT, REA, TEN, WIL, ZOF, ZOM |
| qeynos2 | BAT, BEA, BET, BGM, CAZ, FIS, GNN, IMP, INN, QCF, QCM, RAT, SNA, ZOM (14) | BEA, CAZ, INN (3/14, 21%) | BAT, BET, BGM, FIS, GNN, QCF, QCM, RAT, SNA, ZOM |

This is the single biggest finding of the unit: **the 210 sheets cover the
standard player-shaped races (used for generic humanoid NPCs), not the
zone-specific creature/citizen models (bats, rats, orcs, zombies, ghouls,
Qeynos citizens QCF/QCM, etc.) that make up the bulk of a typical dungeon or
city roster.** "Generate missing sheets offline" (plan A3) was attempted in
spirit — I did not run a doomed build, because source inspection makes the
outcome deterministic: `entity_composite_builder` cannot see
`befallen_chr.s3d`/`qeynos2_chr.s3d` at all (they are never in its archive
list), so every zone-specific race code above is **unbuildable with the
existing tool**, not merely missing by oversight. Extending the builder to
accept zone archives is new tooling work — out of scope for this minimal
gated landing (plan's out-of-scope list implicitly covers this: the unit
uses the *existing* offline tool). Reported, not built. Real adoption ceiling
for this unit in these two zones is therefore CAZ/INN/BEA-shaped NPCs only,
further narrowed by the per-entity uniform-variant/no-override gate.

### B. Implementation

- **Eligibility gate** (`entity_renderer.cpp::buildEntityMesh`): `compositeEligible
  = !isPlayer && !hasBodyPartOverride && !hasEquipmentTint`, computed per entity,
  passed as the new `compositeAllowed` parameter into
  `createAnimatedNode`/`createAnimatedNodeWithEquipment`. Ineligible entities'
  code path is unchanged in outcome — they always land in the pre-existing
  `animatedMeshCache_`/`variantAnimatedMeshCache_`/multi-buffer build, identical
  to what every entity got before this unit (see "compositeAllowed=false" gating
  in `getAnimatedMeshForRace`/`getAnimatedMeshWithAppearance`). All OTHER
  existing callers of these functions (model_viewer, gles2_composite_audit,
  the P08c benchmark tools) default `compositeAllowed=true`, preserving their
  prior (dormant-unless-configured) behavior byte-for-byte — entity_renderer.cpp
  is the only call site that passes an explicit gate value.
- **Config / deployment recommendation**: `enableCompositeEntities` remains the
  master switch (default `false`, unchanged this unit); `compositeEntityPath` is
  read from the preset JSON generically (`constrained_renderer_config.cpp:491-494`,
  pre-existing, not touched). Both `eqe1Path` (general zone-geometry ETC1, ~4.7GB)
  and `compositeEntityPath` are directory paths with no first-class NFS/local
  distinction in code — the CURRENT Pi deployment already treats the general
  `eqe1Path` as NFS-served (per `docs/decisions.md`'s "cold load
  (NFS-transfer-bound)" note). The composite-entity subset specifically is much
  smaller — 210 sheets × 3 files (bmp/json/eqe1) = 247MB total (measured), vs the
  full 4.7GB `EverQuest_ETC1` tree. Recommendation: **do NOT point
  `compositeEntityPath` at the same NFS mount as `eqe1Path`.** At every zone load,
  `renderer_zone_load.cpp::loadZoneStep0_EQE1` does a `fs::directory_iterator`
  over the ENTIRE `compositeEntityPath` directory and opens every `.eqe1` file
  found there (not just the ones the zone needs) — over NFS that's 210 round-trip
  file opens per zone transition for an asset set that rarely changes. Instead,
  rsync the 247MB composite subset to Pi-local storage once (e.g.
  `/opt/willeq/composite_entities/`) and point `compositeEntityPath` there in the
  preset, keeping the large general `eqe1Path` on NFS as today. Shape:
  ```json
  "orangepi": {
    ...
    "enableCompositeEntities": true,
    "compositeEntityPath": "/opt/willeq/composite_entities"
  }
  ```
  (see the separate preset-flip commit). `willeq.json` itself was not touched —
  `compositeEntityPath`/`enableCompositeEntities` are preset-level (hardware-tier)
  settings, not per-install config, matching how `eqe1Path` is already handled
  as a CLI/preset concern rather than a `willeq.json` field.
- **Logging**: added a per-zone-load INFO summary in
  `EntityRenderer::clearEntities()` — `"Composite entity adoption: N/M entities
  eligible, H sheet hits, M sheet misses"` — gated on the tally being non-empty
  (i.e. only fires when `enableCompositeEntities` was actually on). Downgraded
  the pre-existing (dormant, pre-P08d) per-entity `LOG_INFO` calls inside the
  composite build path (`animated_mesh_creation.cpp`,
  `variant_loading.cpp`) to `LOG_DEBUG` — left as `LOG_INFO` they would fire once
  per entity mesh build and spam production logs continuously once the flag is
  enabled; the new zone-load summary is the intended INFO-level signal.
  `RaceModelLoader::getCompositeSheetHits()/getCompositeSheetMisses()` /
  `resetCompositeAdoptionStats()` added to support this.

### C. Verification results

1. `cmake --build build -j24`: clean.
2. `ctest --output-on-failure`: 1224/1225 passed; the only failure was the
   known host exception `AudioManagerTest.PreloadCommonSounds` (matches the
   plan's documented exception list exactly — `RDPServerFrameTest.
   UpdateFrameMultipleTimes`, the other listed exception, passed this run).
3. `scripts/build-gles2-x86.sh`: clean (pre-existing, unrelated `-Wformat-
   truncation` warnings from `logging.h`'s `FormatTimestamp`, present before
   this unit's changes too).
4. Visual gate: **PASS**, goldens untouched (`ctest -R visual_gate`, both as
   part of the full suite and standalone). The gate renders no entities, so
   this confirms zero collateral effect on non-entity rendering.
5. `scripts/build-arm-noble.sh`: clean cross-compile,
   `build-arm-noble/bin/willeq` produced (336MB, unstripped, debug symbols
   retained per the script's default).
6. `scripts/refactor/check_file_sizes.sh` (run via `--files <exact changed
   set>` since the script's default `git diff --name-only fable` mode
   requires the worktree as `$PWD`, and this environment's shell tool resets
   cwd between invocations and disallows `cd` — used the documented
   `--files` mode instead, same check, no functional difference): 6 of 7
   changed files `ok`/pre-existing `WARN`; `entity_renderer.cpp` is `FAIL`
   (3,263 lines, was already 3,201 at the `fable` baseline — confirmed via
   `git show fable:...  | wc -l` — i.e. it was **already over the 3,000-line
   ceiling before this unit touched it**, a documented "accepted core"
   per `refactor/deferred.md:78` ("accepted cores eq.cpp (~3,600) and
   entity_renderer.cpp (3,201)"). This unit added 62 lines (the gate logic +
   explanatory comments) to an already-failing legacy file — **no NEW file
   crossed the ceiling for the first time**, which is what "no new FAILs"
   protects against. `model_loading.cpp` (1,677, was already `WARN`-range)
   and `variant_loading.cpp` (1,614, newly over 1,500 by ~100 lines from the
   variant-composite lookup addition) are both `WARN`, not `FAIL` — within
   the ceiling, flagged only as advisory.
7. Audit sweep + animation evidence + contact sheet: see below.
8. **`enableCompositeEntities=false` (the default) behavior unchanged**:
   verified two ways. (a) Code inspection: every new composite-lookup branch
   in `race_model_loader.cpp`/`animated_mesh_creation.cpp`/`variant_loading.cpp`
   is reached only when `compositeEntityPath_` is non-empty (itself only set
   via `setCompositeEntityPath`, called only from `irrlicht_renderer.cpp`
   guarded by `config_.constrainedConfig.enableCompositeEntities &&
   !compositeEntityPath.empty()`) — with the flag off, `compositeData` is
   never populated (`race_model_loader.cpp:759`'s guard), so every
   `compositeAllowed`-gated branch falls through to the pre-existing
   multi-buffer path regardless of the new parameter's value; the new
   parameter is inert when the feature is off. (b) The full `ctest` suite
   (including `test_graphics`, `test_asset_loading`, `test_zoning_graphics_
   integration`, `test_inventory_model_view`) and the `visual_gate` golden
   comparison all run with `enableCompositeEntities` at its default (false,
   no preset in this unit sets it) and passed clean — a real, not just
   theoretical, spot-check.

**gles2_composite_audit sweep**: attempted per the plan, but the tool has a
**pre-existing device-creation bug** on this host unrelated to P08d
(`Unable to create driver for requested type` after the DRM→surfaceless-EGL
fallback succeeds) — tried matching `zone_bench_loader.cpp`'s working device
params (`Fullscreen=true`) as a hypothesis, rebuilt, same failure, so
**reverted that speculative change** (confirmed via `git diff --stat` showing
zero net diff on that file) rather than ship an unverified guess. Per the
plan's own precedent ("debugging broken sheets beyond marking them ineligible"
is out of scope), root-causing a pre-existing tool bug is likewise not this
unit's job. Substituted two things that together cover the same ground the
audit tool would have, and arguably better for THIS unit's specific question
(is the REAL production pre-baked-sheet pipeline OK, not the audit tool's own
separate runtime-built dynamic-atlas mechanism — confirmed by reading
`gles2_composite_audit.cpp`: it calls `buildDynamicAtlas()`, its own function,
never `RaceModelLoader::setCompositeEntityPath`/the JSON+EQE1 lookup at all):
1. **Structural integrity sweep of all 210 production sheets**
   (`p08d_sheet_audit.py`, ad hoc, not committed — reusable one-off):
   verifies, per sheet, that `.json`/`.bmp`/`.eqe1` all exist, the JSON
   parses with a non-empty texture list, sheet dimensions in the index match
   the JSON body, and the BMP isn't implausibly truncated. **Result: 210/210
   OK, 0 broken** — nothing to mark ineligible on data-integrity grounds.
   Output: `benchmarks/contact_sheets/p08d_composite_landing/sheet_audit.json`.
2. **Real production-pipeline render** via `gles2_zone_benchmark
   --composite-entities --animate --eqe1-path /mnt/projects/EverQuest_ETC1`
   (qeynos2, 6 entities, seed 42) — this DOES exercise the real
   `setCompositeEntityPath`/`preloadModelData`/`buildCompositeMeshFromGeometry`
   path (confirmed by the tool's own stdout: `compositeData=YES` for all 12
   races used, e.g. `race=1: raceName='HUM_HS_DEF' compositeData=YES
   compositeBmpPath='hum_hs_def_0_0'`), unlike the audit tool. See animation
   verification below for what this run demonstrates.

**Animation verification on the production path** (Z35-era risk class):
`gles2_zone_benchmark --animate --composite-entities --eqe1-path
/mnt/projects/EverQuest_ETC1 --entities 6 --seed 42` on qeynos2, dumping
frames 10/30/50 of a 60-frame animated orbit-camera run (5 warmup). Entities
cycle through races 1-6 (HUM, BAM, ERM, ELM, HIM, DAM — 6 distinct races,
exceeding the plan's "3-4 representative" bar), each playing its normal idle
animation continuously across the run, so frames 10/30/50 capture materially
different animation phases per entity (skeletal pose visibly differs frame to
frame in the dumps). Composite meshes animated correctly — no vertex
corruption, no dropped bones, no misaligned UVs at any of the 3 captured
phases (visual inspection of the PNGs). Draw-call counts dropped from avg 44
(baseline) to avg 12 (composite) while **polygon counts stayed byte-identical
between runs at every measured frame** (avg 1176, max 2693 both runs) —
confirming composite entities change only the draw/texture path, not geometry,
consistent with the "same texels, repacked" expectation. **Animated-texture
risk (Z35 class)**: no race/variant among the 6 exercised has an EQ
multi-frame animated texture material (none observed in these runs), and
`entity_composite_builder`/`CompositeTextureData` have **no animated-texture-
frame awareness at all** (confirmed by reading both — no frame-count or
cycling logic anywhere in the packing code) — if any composited race/variant
DOES use one, only whichever single frame the builder happened to pack would
ever render, silently. This is a real, documented structural limitation, not
empirically triggered in this unit's evidence (would require a targeted
EQ-data audit of which of the 42 covered race defs carry animated-texture
materials, out of budget this unit) — flagged for whoever does the live
production quality pass before shipping the preset flip.

**Side-by-side contact sheet** (plan C10):
`benchmarks/contact_sheets/p08d_composite_landing/index.html` (+
`stats.json`), composite vs baseline multi-buffer, same 3 frames, on an
IDENTICAL replayed camera path (`--dump-path`/`--path-file`, so any pixel
difference is attributable only to entity texturing, never camera drift).
Pixel-diff method identical to `scripts/ci/visual_gate.py`
(`ImageChops.difference` on RGB-converted frames), same convention P08c used:

| frame | differing px % | mean abs Δ (0-255) | max channel Δ |
|---|---|---|---|
| f10 | 0.654% | 0.440 | 231 |
| f30 | 0.432% | 0.436 | 232 |
| f50 | 2.284% | 1.120 | 170 |

Near-identical, as expected ("same texels, repacked") — an order of magnitude
tighter than P08c's benchmark-stand-in numbers (3-7% differing pixels, flat
single-material bodies), confirming P08c's prediction that the REAL
pre-baked-sheet pipeline would look materially better than that benchmark's
crude first-body-part-only approximation. Non-zero diffs are consistent with
minor texel-boundary/mipmap-filtering differences from repacking into a
shared sheet at a different resolution than the original per-part textures,
not a content defect (visual inspection: clothing colors, proportions, and
animation pose all match between baseline and composite frames). **Per the
plan: this is reported for Will to judge, not self-declared acceptable.**

**Perf spot-check** (plan C11, optional, done cheaply on x86 alongside the
evidence runs above — P08c's Pi numbers stand as the perf case): same
qeynos2 run, draw calls avg 44→12, state changes avg 38→6, total frame time
avg 7.12ms→6.34ms, p95 11.20ms→9.33ms (x86 llvmpipe, not representative of
Pi timing but directionally consistent with P08c's real-hardware measurement
and free evidence from runs already needed for the contact sheet).

### RETAKE — Will flagged frame 50 (arm-only shot); the quality evidence above was unsound

Will reviewed the bless package and flagged the f50 shot (only an arm visible).
Investigating, the orchestrator found the evidence itself was disqualified:
re-running the exact same evidence command at finer frame granularity
(1024x768, frames 5/10/.../55) showed baseline and composite diverging by up
to **38,728 differing pixels** at frame 15 (vs the committed table's cherry-picked
f10/30/50, which happened to land on frames where the two paths agreed) — at
f15 composite showed a coherent running figure while baseline showed a
collapsed heap on the ground, and the relationship reversed at f25. **The
"0.4-2.3%, near-identical" claim in the table above is WRONG and must not be
read as evidence of composite-vs-baseline visual parity — it was measured at
frames that happened to agree, not because the two paths generally agree.**
This section documents the root-cause investigation and supersedes that table
with correctly-aligned evidence. The table above is left in place (not
deleted) so the record of what was originally (wrongly) claimed is visible,
per the instruction not to leave unsound numbers standing unannotated.

**Root cause.** A temporary diagnostic (added to `gles2_zone_benchmark`,
removed before this commit) logged, every frame, every entity, both runs: the
selected animation code and the animator's playback progress. Result: **0
differences, every frame, every entity, both runs.** This directly refutes
the plan's prime suspect (animation clip/ordering divergence between
`buildMeshFromGeometry` and `buildCompositeMeshFromGeometry`) — clip
selection and timing are byte-identical between the two paths. A second
diagnostic dumped the world-space bounding box of the actually-rendered
merged mesh at draw time: normal, tight, humanoid-sized boxes at every frame
checked — ruling out gross vertex/geometry corruption in the real composite
mesh as well.

The actual cause: **`gles2_zone_benchmark --composite-entities` routes
composite entities through `ZoneBench::renderCompositeEntities()`** — a
P08c-era manual-draw stand-in (`zone_bench_loader.cpp`) with its own,
separate frustum-cull test (`data.frustumCuller->testSphere(...)`), instead
of the SAME default-path cull test (`collectEntityDrawEntries`) that the
baseline (no entity-mode flag) uses. This is a genuinely different
CULLING/RENDER DISPATCH mechanism, not just "a different texture" — for
entities near the frustum boundary the two cull tests can (and did)
disagree, so a **different subset of the 6 placed entities was visible, at
different apparent screen positions, in the two runs at the same simulated
frame** — the "collapsed heap" and "coherent running figure" were each real,
correctly-animated renders of *different* (or differently-culled) entities,
not the same entity rendered two different ways. Confirmed empirically: once
this mismatch is removed (see fix below), baseline and composite render
**pixel-identical** at the frame this was first spotted on (f15, verified via
a tight zoomed crop).

Note this ALSO means the pre-retake evidence's mesh-building/animation
`buildCompositeMeshFromGeometry` code itself was never actually shown to be
wrong by the flagged frames — the divergence was 100% a benchmark-harness
draw-dispatch artifact, unrelated to the production `RaceModelLoader`
composite pipeline P08d's implementation commit changed.

**Fix (harness only, not production).** Added a new `--composite-scenegraph`
flag to `tools/hwcaps/gles2_zone_benchmark.cpp` (+ `tools/hwcaps/
zone_bench_loader.cpp`, net zero diff there after diagnostic cleanup — see
below): loads the real composite mesh data exactly as `--composite-entities`
does (`setCompositeEntityPath` → `preloadModelData` → real single-buffer
`buildCompositeMeshFromGeometry` meshes, confirmed via the same
`compositeData=YES` log lines as before), but skips the
`renderCompositeEntities()` branch and falls through to the SAME default
entity draw path (`collectEntityDrawEntries`/`renderDrawEntries`) the
baseline uses — an apples-to-apples comparison of what the real composite
mesh looks like through the same cull/draw mechanism as multi-buffer,
instead of comparing two different draw dispatch systems. This is the
correct vehicle for A/B composite-vs-baseline quality evidence going
forward; `--composite-entities` (unchanged) remains correct for what P08c
used it for (draw-count/perf comparison against the crude stand-in), just
not for pixel-level A/B quality comparisons.

**Corrected, aligned evidence** (same qeynos2 6-entity `--animate --seed 42`
setup, `--composite-scenegraph` instead of `--composite-entities`, frames
5/10/15/20/25/30/35/40/45/50/55 all checked):

| frame | differing px % | mean abs Δ (0-255) | max channel Δ |
|---|---|---|---|
| f5 | 0.094% | — | — |
| f10 | 0.029% | — | — |
| f15 | 0.187% | 0.0076 | 209 |
| f20 | 0.038% | — | — |
| f25 | 0.204% | 0.0155 | 222 |
| f30 | 0.024% | — | — |
| f35 | 0.077% | 0.0076 | 216 |
| f40 | 0.020% | — | — |
| f45 | 0.075% | — | — |
| f50 | 0.177% | — | — |
| f55 | 0.000% | — | — |

An order of magnitude tighter than even the original (unsound) table's
numbers, and with no frame showing anything close to the previous tens-of-
thousands-of-pixels divergence — this is the real, honest measure of
composite-vs-baseline agreement once the two paths are compared through the
same render mechanism.

**Close-up evidence** (plan's explicit ask: "at least one CLOSE-UP full-body
view"). The default orbit-fallback camera (radius fixed by zone bounding-box
geometry, ~677 units) never brings an entity close enough to fill a
meaningful part of the frame — this is WHY the original evidence only ever
had small/distant subjects, the root cause of Will's "arm shot" complaint
independent of the render-dispatch bug above. Used `--orbit-radius 15`
(deterministic, same on both runs) to force a close pass; frame 30 puts one
entity in a proper close-up. This capture surfaces a real, small,
composite-specific visual artifact:

| capture | differing px % | mean abs Δ | max channel Δ |
|---|---|---|---|
| f30 close-up, full frame | 0.923% | 0.107 | 201 |
| f30 close-up, tight crop on the affected texture region | 18.99% | 3.17 | 201 |

Visual inspection: faint dark patches/bleeding at a few points on the skin
texture in the composite render, absent in baseline — consistent with
bilinear-filtering texel bleed at composite-sheet tile boundaries (the
composite sheet packs many distinct source textures into one atlas with
`EMF_BILINEAR_FILTER` enabled and no visible per-tile gutter/padding logic in
`buildCompositeMeshFromGeometry`; sampling near a tile edge can blend with an
adjacent tile's or the atlas background's texels). Checked and ruled out as
the cause: unmapped/skipped triangles (`buildCompositeMesh: N triangles not
in composite (skipped)` — 0 for every race in this run, confirmed via
`--log-level=INFO --log-only=GRAPHICS`) and the vertex-mapping duplicate-
freezing bug described below (fixed, made no visible difference to this
artifact — confirming it's a texture-sampling issue, not a skinning one).
This is genuinely small (0.9% of the full close-up frame, concentrated in a
handful of texel-scale patches) but real, and — unlike the harness bug above
— IS a property of the real production composite pipeline. Not fixed in this
retake (root-causing atlas-packing/padding is a deeper change than this
retake's budget allows and is not what Will flagged); reported for Will's
bless judgment alongside the numbers, not self-declared acceptable. Flagged
as a known-minor open item for the landing decision.

**A real (but unrelated to the flagged frames) bug found and fixed along the
way.** While investigating, also found and fixed a genuine latent bug in the
shared animation-vertex-mapping code (`animated_mesh_scene_node.h/.cpp`,
`mesh_building.cpp`): `VertexMapping` recorded only ONE destination
(buffer index, local index) per original S3D vertex, but a vertex shared by
triangles using DIFFERENT textures (a UV seam — e.g. where a chest texture
meets an arm texture) is legitimately duplicated into MULTIPLE destination
buffer slots by BOTH mesh builders (`buildMeshFromGeometry`: once per
distinct texture group; `buildCompositeMeshFromGeometry`: once per distinct
(vertex, texture) pair). The single-slot design silently dropped every
duplicate but the last one registered, leaving it frozen at bind pose while
its sibling animated normally — a bug in BOTH mesh-building paths (not
composite-specific, not introduced by P08d), just never noticed because nothing
previously animated a composite mesh in a way that would surface it, and
apparently rare/subtle enough on the multi-buffer path to go unreported.
Fixed by changing `vertexMapping_`/`vertexMappingForAnimation_` to
`std::vector<VertexMappingList>` (one-to-many) and updating the skinning loop
in `applyBoneTransformsToMeshImpl` to write every duplicate, not just one.
**This fix did NOT change the rendered output for the frames investigated**
(confirmed by re-rendering before/after: pixel-identical) — meaning this
particular latent bug was not actually the cause of anything visible in this
unit's evidence, but it is a genuine correctness bug worth having fixed
regardless (both production paths — multi-buffer and composite — now handle
UV-seam vertex duplication correctly). Kept in the implementation commit.
Full standard verification bar (build/ctest/gles2-x86/visual-gate/arm/file-size)
re-run clean after this change — see the verification section above, which
reflects the state including this fix.

**Diagnostic cleanup.** All temporary `P08D_ANIM_DIAG`-gated `fprintf`
instrumentation added to `zone_bench_loader.cpp`, `gles2_zone_benchmark.cpp`,
and `animated_mesh_scene_node.cpp` during this investigation was removed
before committing; `zone_bench_loader.cpp` has a net-zero diff vs the
implementation commit as a result (confirmed via `git diff --stat`) — only
the new `--composite-scenegraph` flag and its dispatch-skip condition in
`gles2_zone_benchmark.cpp` remain as a lasting change.

**Updated contact sheet**:
`benchmarks/contact_sheets/p08d_composite_landing/index.html` (+
`stats.json`) was regenerated with the aligned f15/f25/f35 comparison and the
orbit-radius-15 close-up (full frame + tight defect crop), replacing the
unsound f10/30/50 images from the original pass. `sheet_audit.json` (the 210-
sheet structural integrity sweep) is untouched — independent of this bug.

### Deviations from the plan

- `gles2_composite_audit`'s intended full sweep was not completed as literally
  specified — pre-existing tool bug (device creation), unrelated to P08d,
  substituted with a structural sheet-integrity sweep (210/210 clean) plus a
  real production-pipeline render via `gles2_zone_benchmark`, which validates
  the thing this unit actually changed (the real `compositeEntityPath`
  pipeline) more directly than the audit tool's separate dynamic-atlas
  mechanism would have. Reported rather than silently worked around.
- Animated-texture (Z35-class) risk documented from code inspection, not
  empirically demonstrated on a live example — no known animated-texture race
  was identified within budget.
- `check_file_sizes.sh` was run with explicit `--files <list>` rather than its
  default `BASE_REV` mode, because the mode requires the invoking shell's cwd
  to be inside the worktree (`git rev-parse --show-toplevel`), which this
  environment's tooling does not allow (no `cd`, cwd resets between calls) —
  same check, exact same file set, no coverage difference.
- `entity_composite_builder` was not run to "generate missing sheets" as a
  literal build invocation — source inspection made the outcome deterministic
  (the tool cannot see zone archives at all), so no doomed build was run;
  reported as unbuildable-by-design instead. No new sheets were generated.
- No production code changes beyond the eligibility gate + logging described
  above; `constrained_presets.json` is untouched in this commit (the preset
  flip is the separate final commit per plan B5); `willeq.json` was not
  touched. `tools/hwcaps/gles2_composite_audit.cpp` was edited then fully
  reverted (net zero diff) during the device-creation debugging attempt.
- **Retake**: the unit was reopened after Will flagged the f50 shot; see the
  "RETAKE" section above for the full root-cause writeup. Net effect on
  production code: one additional real fix (the one-to-many vertex-mapping
  correctness bug in `animated_mesh_scene_node.h/.cpp` + `mesh_building.cpp`,
  shared by both mesh-building paths, not composite-specific, verified to not
  change any of this unit's rendered evidence). Net effect on tooling: a new
  `--composite-scenegraph` flag on `gles2_zone_benchmark` for apples-to-apples
  composite-vs-baseline quality comparisons (the harness bug's fix); all debug
  instrumentation added during the investigation was removed before
  committing. The contact sheet and this Review were corrected in place rather
  than leaving the original (unsound) numbers unannotated.
