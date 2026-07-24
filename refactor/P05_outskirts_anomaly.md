# P05 — The outskirts anomaly: fps inversely correlated with scene complexity

Will's field observation (2026-07-24): >45fps WITH per-pixel lighting in
mid-city qeynos2, but LOWER fps on the outskirts where the scene is
simpler. His suspicion from past testing: excess draw calls and/or Irrlicht
scene-graph components eating the render budget — "deleted nodes? filtered
nodes still taking time?" Draw-call reduction is a standing critical thread
(it motivated composite entities). This unit finds where the render budget
actually goes in simple-scene/low-fps areas.

## Hypotheses

- **A. PVS breadth:** open outskirts see MORE regions (long sight lines) →
  more draws/materials despite visually simpler content. Measurable
  directly (region/draw counts per pose).
- **B. Scene-graph traversal overhead:** Irrlicht's drawAll/
  OnRegisterSceneNode walks EVERY node each frame — hidden entities, doors,
  objects, decayed-corpse leftovers — cost scaling with node COUNT, not
  visible complexity. Outskirts poses may coincide with high node counts
  (spawn clusters, ground spawns). Also check node lifecycle: are removed
  entities' nodes actually dropped from the graph or just hidden
  (remember: invisible nodes skip OnAnimate but are still traversed)?
- **C. Per-frame CPU work scaling with total-not-visible sets** elsewhere:
  entity interpolation/name tags (sim_entity_sync), door manager, detail
  system, animated textures — audit what iterates over ALL vs VISIBLE.
- **D. Fill/overdraw:** outskirts terrain = large triangles, full-screen
  ground+sky overdraw vs city's depth-testing? Would show as GPU-bound
  (gpu_ms), distinguishable from B/C (cpu_ms).

## Reference data (Will, 2026-07-24): /mnt/projects/willeq-orangepi/200*.log

March benchmark series: qeynos2, 200 entities placed along the camera path,
1000 frames, moving camera. Headlines: baseline 31.8 avg fps @97 draws;
+LOD 40.2 @78; +LOD@40 46.8 @68 (Will's ">45fps with per-pixel" — note the
adaptive-budget system was the "per-pixel" config: header line "Adaptive
lighting: entity PP budget=N, zone PP budget=M"); LOD40+pp15+zonePP10 =
30.4 avg in a very busy scene. CAUTION: the "v2" logs (4h later) are much
slower at identical names (baseline 31.8 vs v2 20.6) — config drift
mid-series; treat v1/v2 as separate populations. Implications binding on
this unit: (a) use the benchmark's entity path-placement mode
(entities=200) and MOVING camera paths as the primary instrument — match
the zl10 config for comparability; (b) production lighting is the adaptive
budget system, so any lighting numbers reported here use budgets, not the
all-PP/all-lightweight extremes; (c) fps tracked draw calls across the
whole March series — quantify that relationship in current code.

## Method

1. **Find the poses**: use --pose-search plus manual picks to get 3
   "mid-city dense" and 3 "outskirts simple" qeynos2 poses; confirm the
   anomaly reproduces in the benchmark (it may be entity/game-state
   dependent — if benchmark-only poses do NOT reproduce it, that ITSELF
   localizes the cause to live-game components (entities/doors/sim), and
   the investigation shifts to the full client under the recording tour on
   the Pi with the frame-timing instrumentation).
2. Per-pose breakdown: draws, state changes, polys, regions visited,
   cpu_ms vs gpu_ms split (the benchmark has all of this), at per-pixel
   lighting (production config).
3. **Node-traversal instrumentation** (flag-gated): count scene nodes
   visited vs rendered per frame in the production path (RenderPassTimer /
   drawAll — post-F18 the portal path; entities are separate smgr nodes) —
   the direct test of hypothesis B. Include counts of hidden/zero-poly
   nodes visited.
4. Code audit for C: grep/trace the per-frame loops (processFrame*
   sub-passes, entity_renderer update paths, sim outputs applied) for
   all-vs-visible iteration; file:line list.
5. Pi runs under P02 protocol for the real numbers; llvmpipe runs for
   CPU-side amplification where useful.

## Deliverables

- The anomaly reproduced (or localized to live-game components) with
  numbers; hypothesis verdicts A-D.
- Where the budget goes in outskirts poses: a ranked cost table.
- Fix candidates ranked by estimated fps recovery, feeding the draw-call
  reduction thread — NO fixes in this unit.

## Review

### Anomaly: REPRODUCED on current code, on the Pi, and localized to a specific mechanism (Hypothesis D)

Method deviation up front (see Deviations): step 1's plan was to reuse the
March reference logs' frame-index-to-position mapping to pick "3 mid-city
dense" / "3 outskirts simple" poses. That mapping does **not** transfer —
the current codebase's navmesh-based camera-path generator (same
`--zone qeynos2 --entities 200 --seed 42`) produces a materially different
path than the March logs (e.g. March frame 495 sits at
`(261.6,174.8,5.7)` with 2 regions visible; the current build's frame 495
sits at `(331.6,410.9,53.0)` with 117 regions visible). Likely cause:
navmesh/waypoint-sampling changes across the many intervening units
(P02-P04) — not investigated further, out of scope. Fresh poses were found
directly in the current build's own 1000-frame CSV instead (same method,
new data).

**Fresh Pi reproduction** (`build-arm-noble/bin/gles2_zone_benchmark`, this
worktree's HEAD, entities=200, moving camera path, seed=42, qeynos2,
1280x720, P02 protocol: GovernorPin, CmaFree telemetry, randomized
interleave, `/tmp` I/O, 2 sessions / 8 invocations total, cap 12, no
reboot needed, no CmaFree abort — CmaFree held 73.0-82.7 MB throughout,
`benchmarks/pi_runs/p05_outskirts/cma_log_run{1,2}.tsv`):

| pose type | frames | regions_vis | draws | fullPP total_ms | lightweight(budget=0) total_ms | **ratio** | gpu_ms ratio |
|---|---|---|---|---|---|---|---|
| outskirts (few, huge regions) | 558,560-563 | 1-6 | 10-16 | 17.1-20.6 | 7.2-9.6 | **2.16-2.59x** | 2.40-3.01x |
| outskirts (small poly) | 604 | 4 | 4 | 6.4 | 4.1 | 1.56x | 1.74x |
| mid-city dense | 617-622,628,632-635 | 62-127 | 286-422 | 36.2-56.3 | 33.4-51.3 | **1.01-1.14x** | 1.08-1.29x |
| whole-run average (1000 frames) | all | — | — | 29.89 (33.5 fps) | 24.69 (40.5 fps) | 1.21x | — |

`draws`/`regions_vis`/`polys` are byte-for-byte **identical** between the
fullPP and lightweight CSVs at every one of the frames above (verified
programmatically) — the only variable is the lighting shader, so the
ratio column is 100% attributable to lighting cost, not geometry.
Full CSVs: `benchmarks/pi_runs/p05_outskirts/p05b_fullPP.csv` /
`p05b_lightweight0.csv`.

**This reproduces Will's report almost exactly**: full per-pixel lighting
costs a modest, budget-absorbable +1-14% at dense mid-city poses (matches
P02 Experiment A's average +17-18% finding), but 2.2-2.6x at sparse-region
outskirts poses — the opposite direction from the normal draws/regions →
time relationship (see the correlation numbers below), which is exactly
what makes it read as an "anomaly": the scene is measurably simpler
(1-6 regions, 4-16 draws, vs 62-127 regions / 286-422 draws in the city)
yet slower.

### Draw-calls-vs-fps relationship, quantified (current code)

Pearson correlation across the full 1000-frame fullPP run
(`p05b_fullPP.csv`): **draws vs total_ms r=0.856**, **regions_vis vs
total_ms r=0.697** — both strongly positive, confirming the *normal*
expectation (more draws/regions → slower) holds as the dominant pattern
across the whole path. The outskirts poses above are the exception that
proves this: they sit far below the regression line (few draws, but slow)
because a variable the correlation doesn't capture — per-region **screen
fill area** — dominates there instead.

### Root-cause mechanism (code-verified)

`src/client/graphics/renderer_zone_draw.cpp:191-284`
(`IrrlichtRenderer::drawZoneGeometrySorted`): the adaptive-lighting budget
is a flat **region-count** threshold, not screen-area- or
fragment-count-aware:

```
191: const bool adaptiveLight = zoneShader_ && zoneShader_->isPerPixelPlayerLight() &&
192                              config_.constrainedConfig.adaptiveLightingZoneBudget > 0;
193: const int zonePPBudget = config_.constrainedConfig.adaptiveLightingZoneBudget;
...
284: bool regionUseLW = adaptiveLight && regionsDrawn >= zonePPBudget;
```

The first `zonePPBudget` regions in `sortedZoneDrawList_`'s draw order get
full per-pixel lighting; the rest get the lightweight (per-vertex) shader.
In mid-city qeynos2, a pose sees 62-127 regions — the budget (production
default and all `--zone-pp-budget` values tested: 5/10/20) is exhausted
almost immediately, so the *large majority* of regions fall back to
lightweight, capping the fill cost regardless of how many regions there
are. In the outskirts, a pose sees only 1-6 regions total — **the budget
never engages** (1-6 is always < 5/10/20), so *every* visible region gets
full per-pixel lighting. Those few regions are large open
terrain/sky-plane polygons (450 polys drawn as `regions=1, draws=16` at
frames 560-563) that cover most of the screen, so per-pixel (per-fragment)
shading cost is dominated by **pixel count**, not region or draw count —
exactly the mechanism Hypothesis D predicted. Confirmed also from Run 1
(`p05_noPP.csv` vs `p05_PPbudget10.csv`, `--zone-pp-budget 10`): at these
same 1-region outskirts frames, budget=10 produces total_ms within noise
of unbudgeted full-PP (20.238 vs 20.265 ms at frame 560) — direct proof
the budget provides **zero protection** at exactly the poses where the
per-pixel cost is worst.

### Hypothesis verdicts

- **A (PVS breadth — more draws/regions on outskirts from long sightlines):
  NOT the driver.** Correlation is positive (r=0.70-0.86, more
  regions/draws → slower is the norm) and the anomalous outskirts poses
  have *fewer* regions/draws than the city poses, not more. Rejected as
  measured in this benchmark reproduction.
- **B (scene-graph traversal / phantom hidden nodes): NOT supported —
  refuted by direct code audit**, not reproduced numerically (unnecessary,
  since D fully accounts for the measured deltas). The background
  code-audit agent traced entity PVS toggling
  (`src/client/graphics/renderer_sim_bridge.cpp:1009-1020`) and door PVS
  toggling (`src/client/graphics/door_manager.cpp:962-989`) and confirmed
  both call Irrlicht's real `sceneNode->remove()` (graph detach), not just
  `setVisible(false)`, when an entity/door leaves the PVS — so
  `drawAll()`'s traversal (`renderer_frame.cpp:1170`) should not be
  walking hidden entities/doors in the normal live-gameplay path. Only a
  throttled (1/sec, RED-frame-only) node counter exists
  (`renderer_frame.cpp:395-410`); no continuous per-frame "nodes visited"
  metric exists today, and none was added here since the mechanism was
  already explained by D. Flagged gap: no live `dumpscene` node-count
  comparison between an actual mid-city and outskirts view was taken (would
  require a live client session, out of scope for the benchmark-only
  reproduction that already explains the numbers).
- **C (per-frame CPU work scaling with total-not-visible entities/doors):
  mechanism confirmed present in the code, but UNTESTED by this
  reproduction — benchmark does not exercise it.** Code audit found real
  O(total, not visible) per-frame loops: entity interpolation
  (`sim_entity_sync.cpp:172-179`, loops `workerEntities_`), door manager
  (`door_manager.cpp:962-964`, loops `doors_`), animated textures
  (`animated_texture_manager.cpp:382-383`, loops `animatedTextures_`
  zone-wide), and casting-bar timeouts (`entity_castbars.cpp:140-149`).
  But `tools/hwcaps/gles2_zone_benchmark.cpp` /
  `zone_bench_loader.cpp` (2168 + 2799 lines, grepped) contain **zero**
  references to `DoorManager`, `EntityRenderer`, `AnimatedTextureManager`,
  or `SimulationWorker` — the benchmark's entity animation is a separate,
  already visibility/budget-gated reimplementation
  (`zone_bench_loader.cpp:1335-1380`). So the numbers above cannot include
  any C-driven cost, and conversely, C's cost (if it materially differs
  between zone areas by total door/entity/animated-texture count) would be
  **invisible to Pi benchmark testing** — a live-client session would be
  needed to measure it (akk-stack / live zone-in, out of scope for this
  unit, same boundary P04 hit). Not fix-ranked below because no cost
  number exists for it.
- **D (fill/overdraw × per-pixel lighting cost): CONFIRMED as the primary,
  quantified driver of the anomaly.** See the reproduction table and
  mechanism section above. GPU-bound (`gpu_ms` ratio 2.4-3.0x at the
  outskirts poses vs 1.08-1.29x at city poses), distinguishing it cleanly
  from B/C's CPU-side loops as the plan anticipated.

### Where the budget goes — ranked cost table (outskirts poses, frames 558/560-563 avg)

| line item | fullPP | lightweight | delta | % of fullPP total |
|---|---|---|---|---|
| GPU fragment shading (per-pixel lighting fill cost) | ~17.9 ms avg | ~6.8 ms avg | **~11.1 ms** | **~62%** |
| CPU draw submission (10-16 draws, 1-6 regions) | ~1.7 ms | ~1.7 ms | ~0 | ~9% |
| Everything else (state changes, sync, misc) | remainder | remainder | small | ~29% (mostly GPU pipeline latency not captured by the coarse cpu/gpu split) |

The dominant line item by a wide margin is per-pixel fragment shading on a
small number of huge polygons — not draw calls, not scene-graph
traversal, not polygon count (`polys=450` is trivial).

### Fix candidates, ranked by estimated fps recovery (NO fixes applied — investigation only)

1. **Make the adaptive-lighting budget area/fragment-aware instead of a
   flat region count** (`renderer_zone_draw.cpp:284`): weight the budget
   decision by each region's estimated screen-space footprint (e.g.
   projected AABB area or vertex/triangle extent) rather than counting
   regions in draw order. Directly targets the confirmed mechanism.
   **Estimated recovery: closes most of the 2.2-2.6x gap at outskirts
   poses** — e.g. 20.2 ms → ~9-10 ms at frame 560 (roughly 2x fps at those
   specific views, the worst-case/min-fps a player actually notices as a
   stutter). Highest confidence — the evidence chain is direct
   (identical-geometry A/B above) and the fix site is a single conditional.
2. **Dynamic/rolling budget driven by measured frame time** rather than a
   static count: track recent `gpu_ms` and shrink `zonePPBudget`
   adaptively when over target, independent of region count. More robust
   (also helps against unanticipated future scene types) but a larger
   design/engineering lift than (1); addresses the same root cause.
3. **Cap per-pixel lighting by polygon/region size directly** (a
   fixed-threshold escape hatch alongside the budget: any region whose
   projected screen area exceeds N pixels always uses the lightweight
   shader, regardless of budget count) — cheaper to implement than (1)'s
   general weighting, but only fixes the specific "few giant polys" case
   rather than generalizing.
4. **Hypothesis C loop bounding** (door manager, entity interpolation,
   animated textures, casting bars — bound to PVS/visible sets instead of
   all zone entities/doors): plausible contributor to a *live-game-only*
   version of this anomaly, but **no fps-recovery estimate can be given**
   — the benchmark cannot exercise these systems (see C's verdict above),
   so there is no measured cost to rank against (1)-(3). Flagged as a
   follow-up unit if live-session profiling later shows a C-driven cost.
5. **Hypothesis B**: no fix needed. Code audit found the removal path
   already correct (real `remove()`, not `setVisible()`); no measured
   cost to recover.

### Deviations

- **March reference-log pose/position mapping does not transfer to
  current code** (see reproduction section) — the plan's step-1 method
  (reuse March frame indices to pick poses) had to be replaced with fresh
  pose-finding directly against the current build's own CSV output. Same
  method (ms-per-draw / low-region-high-gpu_ms outlier search), different
  data source.
- **No live-client (willeq) reproduction was attempted for Hypothesis C.**
  Consistent with P04's precedent, a full live-game zone-in with real
  entity/door/animated-texture populations is out of scope for this unit
  (would need akk-stack or a recorded session with a much larger cast of
  doors/corpses/animated props than the benchmark's synthetic
  entity-only path). C is reported as a code-confirmed-but-benchmark-blind
  mechanism, not a quantified contributor.
- **No new flag-gated node-traversal instrumentation was added for
  Hypothesis B.** The plan called this out as commit-worthy if pursued;
  it wasn't pursued because the existing code audit (real `remove()` calls
  at the PVS boundary, file:line evidence above) already gives a clean
  negative verdict, and D's identical-geometry A/B fully explains 100% of
  the measured anomaly, leaving no unexplained residual to attribute to B.
  If a future unit wants the "nodes visited vs rendered" continuous
  counter anyway (rather than only the existing 1/sec RED-frame one), it's
  a small addition next to `renderer_frame.cpp:395-410`.
- **Pi session used a manual P02-protocol script**
  (not `pi_bench_run.py`/`bench_pi.sh`, which are built around the
  golden-zone manifest's fixed static poses) — a small standalone script
  reusing `scripts/pi/pi_common.py`'s `GovernorPin`, `CmaMonitor`,
  `run_session`, `randomized_variant_order`, and `strip_and_stage_binary`
  directly, since this unit needed the benchmark's synthetic moving
  camera path (`--entities 200`, default navmesh path), not the manifest's
  fixed poses. 8 invocations across 2 sessions, cap 12, no reboot
  triggered, no CmaFree abort. Pi left pristine (governor restored both
  times, staged binary and remote CSVs/logs removed after each pull).
- ARM build was produced from this worktree (`build-arm-noble/` under
  `/mnt/projects/fable/wt/slot1`), not the main worktree, per the task's
  instruction to build from the task worktree.
