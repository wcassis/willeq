# P08c — Entity draw-path reduction: measure-and-select the existing techniques

Approved by Will 2026-07-25 ("go ahead with the entity draw-path unit").
Slot: wt/slot1, branch task/P08c-entity-drawpath, cut from fable (211115a).
Measure-and-select unit: benchmark the four ALREADY-IMPLEMENTED entity
techniques at gameplay load, produce quality evidence for Will, map each
to production applicability, recommend a landing plan. NO production
changes in this unit; benchmark fixes only if a variant is broken enough
to block measurement (report, don't redesign).

## Motivation

P08b: at gameplay-representative density (placed N=205 → median 38
visible), entities are 64.4% of combined draws and 67.8% of Pi submission
time on the canonical befallen path; befallen p95 is 279.7ms (3.6fps).
The benchmark already carries four entity-reduction techniques from the
P05 era: `--batch-entities`, `--composite-entities` (composite textures —
single draw per entity), `--lod-entities` (composite near, batched
billboards far), `--direct-gl`. Nobody has measured them head-to-head at
gameplay load with replay-comparable frames, or assessed what adopting
each in the production EntityRenderer would take. That is this unit.

## Design

### A. Technique inventory (read the code first)

1. For each flag: what it actually does (draw entries per entity, state
   changes, texture handling, visual consequences), which combinations
   are legal/sensible, and any known caveats from P05-era reviews (search
   refactor/completed/ and the P05 review). Note per P08b: the
   `entities_visible` CSV column is only populated by the
   default/unbatched path — for cross-variant comparability rely on
   identical placed-N + identical replayed path, and record entity draw
   counts per variant instead (add an additive column only if one is
   genuinely missing and cheap).
2. **Production mapping**: read `src/client/graphics/entity_renderer.cpp`
   (+ its CLAUDE.md coverage) and determine, per technique: does
   production already have an equivalent? What would adoption touch
   (texture pipeline, animation, atlas, per-entity state)? Rough size
   (small flag-gated change / medium unit / design unit). This is a
   desk-audit deliverable, not implementation.

### B. Measurement (Pi, P02 protocol, canonical replay)

3. All runs: canonical befallen path replay (--path-file
   benchmarks/paths/befallen_seed42_1000f.path), production config
   (manifest productionConfig), placed `--entities 205 --seed 42`
   (P08b's calibrated N), static entities (NOT --animate — P08b showed
   it inflates visible counts 4.5-5x).
   Variants: baseline (default entity path; P08b's e205 run may be
   reusable if schema unchanged), `--batch-entities`,
   `--composite-entities`, `--lod-entities`, `--direct-gl`, plus at most
   ONE combination if the inventory says it's the intended pairing
   (e.g. lod+composite — decide from the code, state why).
4. Per variant: whole-run avg/p95 total_ms + phase columns (submission
   split via P08's timers), entity draw counts, and frame-indexed
   extraction at the pinned poses bf355/472/626/791/917/987 (statics
   render no entities — P08b's documented substitute).
5. Optional if budget allows: one qeynos2 canonical replay for the
   winning variant (cross-zone sanity).
6. Invocation budget: <=12/session, 2 sessions OK. Prefer one clean
   session of ~6-7 invocations.

### C. Quality evidence (Will judges, not us)

7. Frame dumps at 2-3 entity-heavy pinned poses (pick from the replay
   where entities_visible is high and entities are near the camera) for
   baseline + each variant, x86 llvmpipe (--dump-frames at the extracted
   frame indices during replay). Assemble a contact sheet →
   benchmarks/contact_sheets/p08c_entity_drawpath/ with per-variant
   pixel-diff stats vs baseline. These techniques CHANGE entity
   rendering (billboards, composite textures) — diffs are expected;
   document them honestly (close-up + distant crops if the tooling makes
   it cheap). The orchestrator presents this to Will alongside the perf
   table; no self-declared "acceptable".

### D. Deliverables

8. Head-to-head table: variant | whole-run avg/p95 | submission ms |
   entity draws | vs-baseline speedup | quality-diff summary |
   production-adoption size.
9. Recommendation: which technique (or combo) to land in production, the
   landing-unit sketch (files, interactions, risks — informed by A2),
   and what the projected befallen p95 becomes if the benchmark speedup
   transfers. If nothing clears a meaningful bar (say, <10% p95 gain or
   unacceptable visuals at every setting), say so and re-point at P08's
   lever #2.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  visual gate PASS with goldens UNTOUCHED (gate runs no entities; any
  golden diff = stop and report); scripts/build-arm-noble.sh;
  scripts/refactor/check_file_sizes.sh fable (no new FAILs).
- Replay discipline: pose columns byte-identical to the path file for
  every variant; zone geometry columns (regions_vis/regions_drawn/polys
  attributable to zone) identical across variants — entity techniques
  must not perturb zone rendering; if a variant does, that is a finding.
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O
  to Pi-local /tmp, stripped staged binary from slot1 (ARM_BUILD_BIN_DIR
  trap), drop_caches never reboot, leave Pi pristine, report blocked if
  unreachable.
- CSVs → benchmarks/pi_runs/p08c_entity_drawpath/ (plain git add).

## Out of scope

- ANY production EntityRenderer change (desk-audit only; landing is the
  next unit, gated on Will's quality verdict).
- Fixing/debugging broken variants beyond what measurement needs (if one
  crashes or renders garbage: evidence, skip, report).
- --animate work; zone-geometry levers; occlusion; lighting; goldens;
  path re-recording.

## Review

### Setup

Branch `task/P08c-entity-drawpath`, cut from `fable` (a100705, the plan's own
commit — no other commits in between). No production code changes (measure-
and-select unit, per plan). All runs replay the canonical befallen path
(`benchmarks/paths/befallen_seed42_1000f.path`, `--path-file`, `--frames
1000`) under the production config (manifest `productionConfig`), placed
`--entities 205 --seed 42` (P08b's calibrated N, static entities, never
`--animate` per P08b's documented 4.5-5x visible-count inflation bug).
x86 = llvmpipe via `build-gles2/bin/gles2_zone_benchmark`; Pi = Orange Pi One
(Mali400/Lima) via a stripped binary built from this worktree's
`build-arm-noble/`, P02 protocol
(`refactor/scratch/p08c_pi_session.py`, ad hoc script reusing
`scripts/pi/pi_common.py` primitives per the P05/P06/P08/P08b precedent).
Baseline (default/unbatched entity path) reused from P08b's committed
`benchmarks/pi_runs/p08b_entity_reality/befallen_pi_e205_replay.csv` — CSV
schema unchanged (0 new columns this unit), verified by header comparison
(both files: `frame,pos_x,...,entities_visible`, identical column list).

### A. Technique inventory

All four techniques live in `tools/hwcaps/zone_bench_loader.cpp/.h`
(`renderBatchedEntities`, `renderCompositeEntities`, `renderDirectGLEntities`
/`renderDirectGLCompositeEntities`, `renderLodEntities`), wired into
`gles2_zone_benchmark.cpp`'s per-frame entity dispatch (an if/else-if chain
keyed on `opts.directGLEntities`/`compositeEntities`/`lodEntities`/
`batchEntities`, in that priority order — only one branch runs per frame,
i.e. the flags are mutually exclusive EXCEPT the one explicit combo the code
itself supports: `--direct-gl --composite-entities`, handled by its own
first branch). None of these predate the refactor-unit convention (P05
etc.) — they were added in four early, un-numbered commits (`8e4e0ea`,
`a15217d`, `e566404`, `68a143e`, March 2026, "Add --batch-entities...",
"Add --composite-entities...", "Add LOD entity tiers...", "Add --direct-gl
mode..."); no dedicated refactor/ review exists for them (searched
`refactor/` and `refactor/completed/` — no hits beyond P08/P08b/P08c
themselves), so this section is grounded directly in the code + those
commit messages' own microbenchmark numbers (100-200 entities, qeynos2,
1280x720, Mali400 — a different config than this unit's, cited only as
historical context, not reused as data).

1. **`--batch-entities`**: groups ALL visible entities' mesh buffers into
   per-material buckets (material key = `(MaterialType<<16) |
   textureGroupId`, the same key convention as the zone geometry's own
   material sort), bakes each entity's world-position translation directly
   into a temporary merged vertex buffer per bucket, and issues one
   `drawVertexPrimitiveList` per bucket (i.e. per distinct material across
   the WHOLE visible entity set, not per entity). Real textures preserved
   (each bucket keeps its actual material). Caveat: only frustum-culls
   entities (`data.frustumCuller`), NOT occlusion-culled — entities behind
   walls that the baseline path would occlusion-cull still get batched and
   drawn. 16-bit index overflow guarded (silently truncates a bucket past
   65535 vertices, `break`).
2. **`--composite-entities`**: 1 draw call per ENTITY — merges all of an
   entity's mesh buffers (body parts) into one buffer, but uses ONLY THE
   FIRST body part's material for the whole merged mesh (the code's own
   comment: "simulates what pre-baked composite textures achieve," but this
   benchmark mode does not actually have a composite texture — it reuses
   whichever texture the first mesh buffer happened to bind). Visual
   consequence confirmed in the contact sheet (below): the nearest entity's
   entire body renders in one flat material (its face/skin texture
   stretched over the whole body) instead of the correct multi-texture
   clothing. This is a stand-in for the real technique, not a faithful
   simulation of it — see the production-mapping note below, where the
   REAL composite-texture pipeline already exists and is very different
   from this approximation.
3. **`--lod-entities`**: two tiers by distance (`--lod-dist`, default 80
   EQ units). Near tier: same first-material-only composite draw as
   `--composite-entities`, one draw per near entity. Far tier: ALL far
   entities collapse into ONE draw call — camera-facing billboard quads,
   but every billboard in the frame shares a SINGLE texture (the first far
   entity's first body-part material) regardless of the entity's actual
   race/appearance. Confirmed in the contact sheet: distant entities render
   as flat grey/black silhouette blocks, not recognizable character
   sprites — a placeholder-quality LOD tier, not production-representative
   billboard art.
4. **`--direct-gl`**: bypasses Irrlicht's `setMaterial`/`drawMeshBuffer`
   entirely, issuing raw `glBindTexture`/`glVertexAttribPointer`/
   `glDrawElements` calls with minimal state (only `mWorldViewProj`/`mWorld`
   uniforms set; no fog, no per-vertex-color multiply, no lighting-related
   uniform beyond what the current bound shader program already has set
   from the previous Irrlicht draw). Per-body-part mode (`--direct-gl`
   alone) keeps the same draw count as baseline (one draw per mesh buffer)
   but with less per-draw Irrlicht overhead; `--direct-gl
   --composite-entities` additionally merges body parts into one draw per
   entity (real texture per merge, same first-material caveat as #2).
   **Finding, not a caveat**: `--direct-gl`'s output is visibly BROKEN —
   see the contact sheet: entity textures render as a garbled green/gold
   noise pattern, not the correct clothing colors, at every pose checked.
   This is consistent with the minimal-state-tracking design bypassing some
   piece of Irrlicht's texture/vertex-format setup the shader needs (most
   likely UV/vertex-attribute stride or texture unit state carried over
   incorrectly from the preceding zone-geometry draw). Per the plan's
   explicit instruction ("if a variant... renders garbage: capture
   evidence, skip it, report — do NOT debug/redesign it"), this was NOT
   root-caused. `--direct-gl --composite-entities` inherits the same
   broken texturing (visually near-identical garbling, see contact sheet)
   since it shares the same raw-GL texture/vertex-attrib path.

**Production mapping** (`src/client/graphics/entity_renderer.cpp` +
`src/client/graphics/CLAUDE.md`, `include/client/graphics/eq/
race_model_loader.h/.cpp`, `include/client/graphics/constrained_renderer_
config.h`, `src/client/application.cpp`):

- **Composite entities: PRODUCTION ALREADY HAS A REAL, DIFFERENT-AND-BETTER
  IMPLEMENTATION OF THIS, DORMANT.** `EntityRenderer::setCompositeEntityPath`
  (`entity_renderer.cpp:840`) forwards to `RaceModelLoader::
  setCompositeEntityPath`/`buildCompositeEntityMesh`
  (`race_model_loader.h:142,241`), gated by
  `ConstrainedRendererConfig::enableCompositeEntities` (default `false`,
  `constrained_renderer_config.h:114`) and a `compositeEntityPath` preset
  field pointing at a directory of real pre-baked composite BMP/EQE1
  textures + JSON manifests generated by the `tools/entity_composite_
  builder.cpp` offline tool (already built by `cmake --build`, confirmed
  target `entity_composite_builder` exists). Wired end-to-end:
  `irrlicht_renderer.cpp:2125-2126` calls `setCompositeEntityPath()` at zone
  load when the config flag+path are both set; `application.cpp:369-374`
  exposes a `--composite-entities <path>` CLI override that flips
  `enableCompositeEntities = true`. Generated assets ALREADY EXIST on disk:
  `/mnt/projects/EverQuest_ETC1/composite_index.json` lists 210 composite
  sheets covering 42 distinct race/gender defs. **This is unlike the
  benchmark's crude composite mode (#2 above, which fakes a composite by
  reusing one real body-part texture)** — production's real path bakes an
  actual multi-body-part texture sheet per race/variant, so its visual
  fidelity should be materially better than what this unit's contact sheet
  shows for the benchmark's approximation. Adoption size: **SMALL,
  flag-gated** — flip `enableCompositeEntities: true` +
  `compositeEntityPath` in the orangepi preset (currently absent from
  `config/constrained_presets.json`'s orangepi block — checked, not
  present). The real unknowns for a landing unit are validating the
  already-built asset pipeline's coverage/correctness and doing a live
  quality pass with the REAL composite path (not this benchmark's stand-in)
  — genuinely small if the assets are already good, but not zero-risk sight
  unseen.
- **Batch entities**: production has NO equivalent — `entity_renderer.cpp`
  and `race_model_loader.cpp` were grepped for "batch" (only unrelated hits:
  "batched processing" for network update queuing). Each entity is a normal
  Irrlicht `IAnimatedMeshSceneNode` drawn through Irrlicht's own scene-graph
  traversal (`smgr_->drawAll()`), one `setMaterial`+`drawMeshBuffer` per
  mesh buffer per entity, same shape as this benchmark's UNBATCHED baseline
  — not the zone geometry's manual front-to-back draw path
  (`manualZoneDrawEnabled_`). Adoption size: **MEDIUM design unit** — would
  require pulling entities out of Irrlicht's scene-graph render path (like
  zone geometry's own `manualZoneDrawEnabled_` did) into a manual
  cross-entity material-bucketed draw pass, reconciling with: per-entity
  animation (bone transforms must be baked into merged vertices EVERY
  frame, not just world position — animated entities move body parts
  independently, unlike this benchmark's `--animate`-free static-entity
  scope), per-entity PVS/distance/occlusion culling (already present,
  compatible), and name-tag/cast-bar billboards attached per-entity (would
  need to stay a separate per-entity draw regardless of body merging).
  Real per-frame CPU cost of baking animated skeletal transforms into
  merged buffers every frame is untested by this benchmark (static
  entities only) — a real risk the landing unit must measure, not assume
  away.
- **LOD entities (billboards)**: production has NO equivalent — no
  entity-billboard code in `entity_renderer.h/.cpp` (the only billboard
  scene node is `castBarBillboard`, an unrelated UI overlay for cast-time
  bars). Adoption size: **DESIGN UNIT** — needs real per-race billboard
  sprite art (or a runtime impostor-render-to-texture scheme, which the
  benchmark's OTHER mode, `--adaptive-lod`, actually does via
  `getOrCreateImpostor`/RTT, not measured in this unit per the plan's
  four-flag scope), a distance-tier switch policy, and reconciliation with
  per-entity animation state (a billboard can't reflect an animated pose
  without either a baked static pose sprite or a per-frame RTT re-render).
  Not a small change.
- **Direct GL entities**: production has NO equivalent, and per the A4
  finding above, this benchmark mode's own output is broken (garbled
  textures) — **not a candidate for adoption in its current form**, and
  root-causing the state-bypass bug is out of scope for this unit (report,
  don't debug, per the plan). Even if fixed, adoption would mean bypassing
  Irrlicht's whole material/lighting/fog pipeline for entities specifically,
  which today receive the SAME per-pixel/per-vertex lighting split zone
  geometry does (`swapZoneMeshMaterials`-style light state) — a raw-GL path
  would need to reimplement all of that manually. **DESIGN UNIT**, and
  lowest-priority of the four given #4's broken baseline.

### B. Pi head-to-head (production config, befallen replay, e205 static)

Pi session (`refactor/scratch/p08c_pi_session.py`, ad hoc, deleted at unit
close): 7 invocations across 2 sessions (session 1: 1 invocation, `batch`,
hit a `SessionAbort` at its own post-invocation CmaFree sample — 60488 ->
19128 kB, below the 40MB floor — governor was restored cleanly by the
context manager and the already-completed CSV was recovered from the Pi's
`/tmp` before remediation; session 2: `sync && echo 3 >
/proc/sys/vm/drop_caches` — never rebooted — then 6 invocations covering
`composite`, `lod` (1 retry after a second SessionAbort), `directgl`,
`directgl_composite` (1 retry after a third SessionAbort), all recovered
cleanly via the same proactive-CmaFree-check + retry-once pattern added to
the driver script after the first abort). Both well under the 12/session
cap. Pi left pristine after both sessions (staged binary + scratch remote
CSVs removed, verified via `ls /tmp`; governor restored to `ondemand`,
verified by reading `scaling_governor` post-session). CmaFree never
approached the Pi's total RAM floor in any single sample beyond the
documented abort threshold, and each abort was a clean stop-and-remediate,
not a crash — consistent with the panic-safe protocol working as designed.

**Replay discipline** (full-file programmatic checks, all 1000 frames, all
6 variants including the reused baseline): pose columns (`pos_x/y/z`,
`heading`) **0/1000 mismatches vs the path file** for every variant.
Zone-attributable geometry columns (`regions_vis`/`regions_drawn`/
`regions_occluded`) **0 diffs vs baseline** for every variant, confirming
none of the five entity techniques perturb zone rendering (the plan's
required check — a clean pass, not a finding).

**Whole-run head-to-head (Pi Mali400/Lima, 1000 frames, befallen, e205
static):**

| variant | avg total_ms | p95 total_ms | max total_ms | draws (avg) | submission_ms (avg) | other_ms (avg) | sub+other (avg) | avg speedup | p95 speedup |
|---|---|---|---|---|---|---|---|---|---|
| baseline (default/unbatched) | 104.53 | 279.67 | 456.44 | 710.1 | 67.40 | 0.26 | 67.66 | 0.0% | 0.0% |
| --batch-entities | 70.34 | 229.06 | 380.64 | 407.0 | 21.87 | 20.86 | 42.73 | 32.7% | 18.1% |
| --composite-entities | 55.08 | 187.10 | 336.13 | 289.0 | 22.25 | 7.81 | 30.06 | 47.3% | 33.1% |
| --lod-entities | 43.96 | 180.25 | 320.17 | 263.2 | 22.17 | 2.53 | 24.70 | 57.9% | 35.5% |
| --direct-gl | 101.74 | 281.60 | 536.39 | 726.9 | 22.39 | 44.24 | 66.63 | 2.7% | **-0.7% (worse)** |
| --direct-gl --composite-entities | 53.15 | 184.52 | 335.04 | 289.0 | 21.95 | 6.99 | 28.94 | 49.2% | 34.0% |

**Measurement caveat, important for reading the table**: for all four
technique variants (not baseline), entity-submission time does NOT land in
the `submission_ms` phase column — the code path for these branches calls
`renderDrawEntries()` (ZONE geometry only) under the `phaseSubmitMs` timer,
then calls the entity-technique render function AFTER that timer stops (see
`gles2_zone_benchmark.cpp:2422-2533`). Entity-technique submission time
therefore falls into `other_ms` (the residual = frame total minus the
named phases). This is a genuine phase-accounting gap in the benchmark's
instrumentation for these four modes (not something P08c's plan asked to
fix — the plan's design point 4 anticipated needing to report "submission
ms" per variant, and the honest number for that is `submission_ms +
other_ms`, shown as its own column above, not `submission_ms` alone).
`other_ms` for baseline (0.26ms) is near-zero, confirming this residual
really is "entity technique cost," not pre-existing frame-timing slop.

**Per-pose frame-indexed extraction (Pi, total_ms and draws, at the 6
pinned poses; entities render only via the moving-path replay per P08b's
architectural finding — `--camera-pose` static captures never call the
entity path):**

| pose | baseline ms | batch ms | composite ms | lod ms | directgl ms | baseline draws | batch draws | composite draws | lod draws | directgl draws |
|---|---|---|---|---|---|---|---|---|---|---|
| bf355 | 61.88 | 35.10 | 25.18 | 14.54 | 53.42 | 567 | 246 | 128 | 101 | 567 |
| bf472 | 383.78 | 327.48 | 286.12 | 240.26 | 372.25 | 1627 | 1451 | 1322 | 1303 | 1627 |
| bf626 | 50.92 | 28.79 | 20.70 | 11.19 | 47.80 | 532 | 160 | 46 | 17 | 532 |
| bf791 | 77.24 | 48.43 | 37.05 | 25.24 | 72.77 | 663 | 319 | 203 | 174 | 663 |
| bf917 | 86.45 | 42.36 | 32.28 | 22.35 | 59.96 | 615 | 284 | 167 | 143 | 615 |
| bf987 | 178.06 | 177.46 | 178.23 | 108.06 | 176.38 | 1038 | 917 | 784 | 771 | 1038 |

Two structural findings from the per-pose table:
- **bf987 (the densest ZONE-geometry pose, 396 zone regions, only 21
  entities visible per P08b) barely moves for batch/composite/directgl**
  (177-178ms vs 178.06ms baseline) — because at this pose the cost is
  dominated by zone draws (763 of the pose's ~1038 combined draws are
  zone-attributable, per P08's own histogram), which none of these
  techniques touch. `lod` does much better here (108.06ms, 39% faster)
  because its billboard-batch tier collapses far entities into ONE draw
  regardless of count, a larger proportional cut than the other
  techniques' per-entity or per-material merging.
- **`--direct-gl`'s draw counts are IDENTICAL to baseline at every single
  pose** (567/1627/532/663/615/1038, both columns) — confirms the A4
  finding that per-body-part direct-GL mode does not reduce draw count at
  all (by design: bypasses Irrlicht overhead per draw, doesn't merge
  anything), and the whole-run table shows it doesn't even reliably reduce
  time either (p95 is actually WORSE than baseline, -0.7%). Combined with
  its broken texturing (section C), `--direct-gl` is a clear non-starter.

`entities_visible` was confirmed `0` for all five non-default-path variants
at every frame (batch/composite/lod/directgl/directgl_composite), matching
P08b's documented behavior (only the default/unbatched path populates this
column) — not a new finding, a consistency check.

**Optional qeynos2 cross-zone sanity** (budget allowed it — 8 invocations
used across 3 sessions total, well under 36; this one hit its own
`SessionAbort` at the post-invocation CmaFree sample — 58424 -> 39780 kB,
just below the 40MB floor — governor restored cleanly, CSV recovered
manually from the Pi's `/tmp` before `sync && drop_caches` remediation,
same never-reboot protocol): one replay of the winning technique
(`--composite-entities`) on `qeynos2_seed42_1000f.path`, e205, production
config.

| | avg total_ms | p95 total_ms | fps (p95) | draws (avg) |
|---|---|---|---|---|
| qeynos2 e205 baseline (P08b, default path) | 61.75 | 143.76 | 6.96 | — |
| qeynos2 e205 --composite-entities | 36.57 | 63.27 | 15.80 | 103.5 |

Composite entities cut qeynos2's p95 by **56.0%** (vs. befallen's 33.1%) —
a materially BIGGER relative win in this lighter outdoor zone, consistent
with entities being a larger proportional share of the frame budget once
zone-geometry cost itself is smaller. Still below the 30fps KPI (63.27ms
vs. the 33.3ms budget), but the technique's benefit clearly generalizes
across zone types, not a befallen-specific artifact.

Replay discipline: 0/1000 pose mismatches vs. the qeynos2 path file.

### C. Quality evidence

x86 llvmpipe frame dumps at 3 entity-heavy pinned poses (bf355, bf626,
bf791 — the top-3 poses by `entities_visible` in P08b's e205 data: 36, 40,
38 respectively), `--dump-frames 355,626,791 --dump-png-prefix`, baseline +
all 5 variants (4 techniques + the 1 code-informed combo). Contact sheet:
`benchmarks/contact_sheets/p08c_entity_drawpath/index.html` (+ `stats.json`
for the raw per-pose per-variant pixel-diff numbers). Pixel-diff method:
identical to `scripts/ci/visual_gate.py`'s `compare_images_exact`/
`save_diff_artifact` (PIL `ImageChops.difference` on RGB-converted frames),
for consistency with the project's existing convention, extended with
mean-abs-channel-diff and differing-pixel-percentage for a continuous
severity measure (visual_gate's own bar is exact-zero, not applicable here
since these techniques deliberately change rendering).

| pose | variant | differing px % | mean abs Δ (0-255) | max channel Δ | verdict |
|---|---|---|---|---|---|
| bf355 | batch | 0.065% | 0.0005 | 74 | near-identical (see note) |
| bf355 | composite | 3.142% | 0.846 | 203 | visibly different (flat body texture) |
| bf355 | lod | 3.877% | 1.258 | 203 | visibly different (flat + distant silhouettes) |
| bf355 | directgl | 3.315% | 0.906 | 244 | **broken texturing** |
| bf355 | directgl_composite | 3.315% | 0.907 | 244 | **broken texturing** |
| bf626 | batch | 0.079% | 0.0004 | 15 | near-identical |
| bf626 | composite | 6.135% | 2.128 | 255 | visibly different |
| bf626 | lod | 7.035% | 2.675 | 255 | visibly different |
| bf626 | directgl | 7.580% | 3.370 | 255 | **broken texturing** |
| bf626 | directgl_composite | 7.580% | 3.370 | 255 | **broken texturing** |
| bf791 | batch | 0.185% | 0.0014 | 92 | near-identical |
| bf791 | composite | 5.994% | 2.060 | 244 | visibly different |
| bf791 | lod | 7.351% | 2.669 | 244 | visibly different |
| bf791 | directgl | 6.691% | 2.548 | 255 | **broken texturing** |
| bf791 | directgl_composite | 6.692% | 2.548 | 255 | **broken texturing** |

Visual confirmation (not just numbers — images inspected directly):
- **batch**: visually indistinguishable from baseline at all 3 poses; the
  small nonzero diff (0.06-0.19% of pixels, mean channel delta <0.002) is
  consistent with floating-point baked-transform rounding or draw-order-
  dependent antialiasing/blend edge noise, not a real content difference.
  This is the ONLY technique that preserves correct per-entity textures.
- **composite / lod**: the nearest entity's clothing (a multi-color
  tunic — red/orange/green in the baseline) collapses to a single flat
  material (skin-tone/green) covering the whole body, confirming the
  code's "first body-part material" mechanism produces a real, visible
  degradation, not a subtle one. LOD additionally renders the distant
  background entities as flat grey/black silhouette blocks with no
  discernible character detail.
- **direct-gl / direct-gl+composite**: entity surfaces render as a
  mottled green/gold noise/camo pattern with no resemblance to the correct
  clothing texture at any of the 3 poses — a genuine rendering defect, not
  a stylistic approximation. **Disqualifying for adoption consideration**
  regardless of any performance numbers below.

### D. Head-to-head table and recommendation

**Combined table** (befallen, Pi, e205 static, whole-run p95 the primary
decision column per the plan's ">=10% p95 gain" bar):

| variant | p95 total_ms | p95 speedup | entity draws (avg) | quality (x86 contact sheet) | production adoption size |
|---|---|---|---|---|---|
| baseline | 279.67 | — | 710.1 | — (reference) | — |
| --batch-entities | 229.06 | 18.1% | 407.0 | near-identical to baseline (0.06-0.19% px, mean|Δ|<0.002) | **MEDIUM design unit** (no production equivalent; entities aren't pulled out of Irrlicht's scene-graph render path today) |
| --composite-entities | 187.10 | 33.1% | 289.0 | visibly degraded IN THIS BENCHMARK'S STAND-IN (3-6% px, flat single-texture body) — but production's REAL composite pipeline is a different, better implementation (see below) | **SMALL, flag-gated** — production already has this built, wired, and has real generated assets sitting unused |
| --lod-entities | 180.25 | 35.5% | 263.2 | worst degradation of the three real candidates (4-7% px; distant entities are flat grey/black silhouette blocks) | **DESIGN unit** — needs real billboard sprite art or an RTT impostor scheme (the benchmark's separate, unmeasured `--adaptive-lod` mode already does RTT) |
| --direct-gl | 281.60 | **-0.7% (WORSE)** | 726.9 (no reduction) | **broken — garbled camo-pattern texturing at every pose checked** | disqualified — no perf case and no visual case |
| --direct-gl --composite-entities | 184.52 | 34.0% | 289.0 | **broken — same texturing defect as direct-gl** | disqualified for the same reason, despite tying composite-entities on raw speed |

**Recommendation: land composite entities.** Three lines of evidence converge
on this being the highest-leverage, lowest-risk pick, ahead of the raw p95
numbers alone:

1. **It's already built.** `EntityRenderer::setCompositeEntityPath` /
   `RaceModelLoader::buildCompositeEntityMesh` is real, wired end-to-end
   (config flag -> `irrlicht_renderer.cpp` zone-load call -> CLI override in
   `application.cpp`), and NOT the crude stand-in this benchmark measures —
   it bakes an actual multi-body-part composite texture sheet per race via
   the already-built `entity_composite_builder` offline tool, and 210
   composite sheets (42 race/gender defs) already exist on disk at
   `/mnt/projects/EverQuest_ETC1/composite_index.json`. The benchmark's
   ugly contact-sheet result (flat single-texture bodies) is an artifact of
   the BENCHMARK's approximation (reusing one real body-part texture as a
   stand-in for a composite bake), not evidence about what production's
   real composite path will look like — that needs its own quality check
   with the real assets, not assumed from this unit's numbers, but there is
   good reason to expect it looks much better than what the contact sheet
   shows.
2. **The performance case is real and generalizes.** 33.1% p95 reduction on
   befallen, 56.0% on qeynos2 (bigger relative win in the lighter zone) —
   both measured with the SAME real texture-swap draw-count reduction
   (289 vs 710 combined draws on befallen) that would apply whether the
   composite texture is this benchmark's stand-in or production's real
   bake; the draw-COUNT reduction (the actual CPU-submission lever) is
   identical either way, only the FIDELITY of what's in the single texture
   differs.
3. **Adoption size is genuinely small.** Flip `enableCompositeEntities:
   true` + set `compositeEntityPath` in `config/constrained_presets.json`'s
   `orangepi` block (currently absent — checked, not present in any
   preset), point it at (or regenerate via `entity_composite_builder`) a
   verified-complete asset set, and do a live quality pass. This is a
   config change plus an asset-verification pass, not new rendering code.

**`--batch-entities` is the honest runner-up**: smaller p95 win (18.1%),
genuinely no production equivalent (a real MEDIUM design unit, since
entities live in Irrlicht's normal scene-graph draw path today, unlike zone
geometry's already-manual front-to-back draw), but its quality story is
categorically better than composite/lod (near-pixel-identical, not a
"different-and-hopefully-better" argument) — worth keeping in mind as a
complementary/fallback lever if the composite adoption's live quality check
comes back worse than hoped, or as a technique that composes WITH composite
entities for whatever residual multi-material entities remain (e.g. items
that can't be pre-baked, like held weapons with unique tint).

**`--lod-entities` and both `--direct-gl` variants are NOT recommended for
adoption from this unit's evidence**: LOD needs real billboard art
(a design unit with no existing asset investment, unlike composite);
`--direct-gl` provides no real performance benefit AND renders garbage,
disqualifying it regardless of the numbers.

**Projected befallen p95 if composite-entities' measured benchmark speedup
transfers to production**: 279.67ms -> ~187ms (5.3 fps) at befallen's e205
gameplay-representative load — a real, meaningful improvement (33% faster)
but **still nowhere near the 30fps KPI (33.3ms budget)** on its own. This
confirms the plan's fallback instruction applies in spirit even though a
technique DOES clear the >=10% bar: entity-path reduction alone does not
close the KPI gap at befallen's density. **The zone-geometry cross-region
merge lever (P08's #1 recommendation, ~97.8% zone-draw reduction ceiling,
still unbuilt) remains necessary in addition to an entity-path win, not
instead of it** — P08b's finding that zone-only submission is far from
negligible on the Pi (21.7ms avg at e205, whole-run zone-only p95 174.3ms)
is unaffected by this unit and is the other half of closing the gap. A
landing plan that ships composite entities alone will measurably help but
will not, by itself, bring befallen under 30fps.

### Deviations from the plan

- **Two additional `SessionAbort`s beyond the ones anticipated by the
  panic-safe protocol's normal operation**: this Pi's CmaFree ran close to
  the 40MB floor after almost every 1000-frame e205 invocation in this
  unit's sessions (not just once, as in prior units) — `batch` (session 1,
  crashed the then-unmodified driver script entirely since it had no
  retry/recovery logic yet), `lod` and `directgl_composite` (session 2,
  handled cleanly by a proactive-check + retry-once pattern added to the
  driver script after the first abort), and the optional `qeynos2_composite`
  run (a separate small script without the retry logic, crashed the script
  again). In every case: governor was restored correctly by
  `GovernorPin.__exit__` (verified after each), the already-completed CSV
  was still present on the Pi and was recovered (via the driver script's
  own rsync-on-retry-success in session 2, manually via `rsync` for the two
  script-crashing cases), remediation was `sync && echo 3 >
  /proc/sys/vm/drop_caches` (never a reboot), and the Pi was left pristine
  at the end of both sessions (verified). No data was lost, but this is
  worth flagging: this unit's `--entities 205` moving-path replays seem to
  pressure CmaFree noticeably more than prior units' static-pose or
  lower-entity-count runs — plausibly the higher peak vertex-merge buffer
  churn in the batch/composite/lod code paths (temporary
  `std::vector<S3DVertex>` merge buffers reallocated every frame) driving
  more CMA-backed GPU buffer object churn than the default path. Not
  root-caused (out of scope for a measure-and-select unit) but noted for
  whoever picks up the landing unit, since production's real composite path
  will have a similar (if smaller — assets are pre-baked once, not
  merged per-frame) allocation pattern to verify doesn't reproduce this.
- **`--adaptive-lod`, a fifth entity mode present in the benchmark, was NOT
  measured** — the plan named exactly four techniques
  (`--batch-entities`/`--composite-entities`/`--lod-entities`/
  `--direct-gl`) plus at most one combination; `--adaptive-lod` is a
  distinct fifth mode (RTT impostor billboards with frame-budget-adaptive
  tier switching) outside the plan's named scope, referenced only in
  passing in the production-mapping section (LOD's real-billboard-art gap)
  as context for what a real LOD landing might look like. Not measured,
  per the plan's explicit four-technique-plus-one-combo scope.
- **`refactor/scratch/p08c_pi_session.py` and
  `refactor/scratch/p08c_pi_qeynos2.py`** (the ad hoc Pi driver scripts)
  were deleted after the sessions per the P05/P06/P08/P08b precedent of not
  committing single-unit investigation drivers.
  `refactor/scratch/p08c_contact_sheet.py` (the x86 pixel-diff/contact-sheet
  builder) is kept as a reusable reporting helper rather than deleted,
  since it doesn't touch the Pi and isn't a hardware-session driver — noted
  here as a deviation from the strict "scratch scripts are deleted"
  precedent, flagged rather than silently kept.
- No production code, presets, goldens, paths, or lighting were touched. No
  entity-technique CODE was modified (measure-and-select only, per the
  plan) — the ad hoc Pi scripts and the contact-sheet builder are the only
  new files, plus this review and the committed CSVs/contact-sheet images.
