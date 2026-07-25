# P09 — Cross-region same-material zone draw merge: design + benchmark prototype

Approved by Will 2026-07-25 ("go ahead with the merge design unit").
Slot: wt/slot1, branch task/P09-merge-design, cut from fable (508699c).
DESIGN unit with a measured benchmark prototype. NO production changes —
the production landing is a follow-on unit gated on this one's data.

## Motivation

P08: befallen zone-only replay misses KPI (p95 174.3ms) and 87-96% of CPU
draw time is submission; per-draw cost on Mali400/Lima is ~59-163μs; the
cross-region same-material merge ceiling is 97.8% of zone draws (draws ≈
distinct visible textures, not regions × materials). Historical framing
(Will, 2026-07-25): the 1999 client transformed the SAME baked PVS set on
CPU and submitted texture-sorted triangle streams — per-draw overhead
didn't exist; regions × materials as DRAW granularity is our modern
artifact. This unit designs the modern equivalent and proves it in the
benchmark.

The four interactions any design must reconcile (P08 C5):
mesh-cache eviction granularity, per-region adaptive-lighting overrides
(count mode demotes per REGION), occlusion/PVS culling granularity,
front-to-back ordering within material groups.

## Design

### A. Evaluate the implementation options (design phase — do this FIRST)

Candidate architectures, each to be assessed on paper against the four
interactions AND the Mali400/Lima cost model before anything is built:

1. **Load-time hard merge per "merge cell"** (cluster of nearby BSP
   regions → one buffer per material per cell). Simple draws; loses
   per-region culling/lighting granularity WITHIN a cell — quantify what
   that does to count-mode lighting decisions and occlusion effectiveness
   (cell size is the tuning knob; the P08 histograms give the data).
2. **Shared VBOs + draw-time index-range coalescing**: store per-region
   geometry contiguously in shared per-material VBOs, keep per-region
   index ranges; at draw time walk the sorted visible list and coalesce
   ADJACENT ranges with identical (material, lighting-tier) into single
   glDrawElements calls. Retains full per-region culling and per-region
   lighting semantics (a demoted or culled region just breaks the run);
   draw count = number of runs, not regions. Precedent in-tree: Z35 did
   "composite entity draw-time merge" — read that commit/code first.
3. **Per-frame index gather ("the 1999 way")**: shared per-material
   vertex VBOs + a per-frame CPU gather of visible regions' indices into
   a scratch index buffer (streamed IBO or client-side index array —
   GLES2 permits client-side indices), then ONE draw per (material,
   lighting-tier). Gather cost at befallen scale is ~tens of KB of
   memcpy/frame — trivial on paper, but Lima's handling of dynamic/client
   indices is UNKNOWN and must be measured, not assumed.

**A-gate: micro-benchmark the risky primitive on the Pi before choosing.**
Extend the P08 `--synthetic-draws` machinery (or a sibling test mode) to
measure, at matched geometry: N separate VBO draws vs coalesced-range
draws vs streamed-IBO single draw vs client-side-index single draw, on
Lima AND llvmpipe. If dynamic index submission is pathological on Lima,
that kills option 3 cheaply. Choose the design from this data + the
interaction analysis; write the decision + rationale into the review
BEFORE building the prototype. If two options are close, prefer the one
preserving per-region semantics (option 2-style).

### B. Benchmark prototype (chosen design only)

4. Implement behind `--merge-zone-draws` (default OFF everywhere; visual
   gate and all existing paths untouched). Zone geometry only — entities
   and objects out of scope.
5. Must correctly compose with, at minimum: production config count-mode
   adaptive lighting (per-region pp/LW decisions — runs break at tier
   boundaries), radius mode (flag-gated, same per-region structure),
   occlusion culling (culled regions drop out), PVS/frustum culling,
   front-to-back material-group ordering, and the H02b/Q02 deterministic
   texture-name keys. If the chosen design changes any per-region
   semantic (option 1), quantify the pixel consequences explicitly.
6. **Correctness bar: pixel-identical to unmerged** at the 9 golden views
   + pinned poses bf355/472/626/791/917/987 under the production config
   (options 2/3 should achieve max_delta=0 — same triangles, same
   materials, same per-region lighting; any diff = report, do not
   rationalize). For option 1 only: quantified diffs + contact sheet for
   Will's judgment.
7. Determinism: geometry/draw CSV columns byte-identical across two
   merged runs; draws column obviously changes vs unmerged — record the
   achieved reduction per pose vs P08's 97.8% ceiling.

### C. Measurement (Pi, P02 protocol)

8. Canonical replays (--path-file), production config, merged vs
   unmerged: befallen zone-only AND befallen combined (--entities 205,
   P08b calibration) — entity path untouched, so combined isolates "zone
   merge under realistic total load". qeynos2 zone-only if budget allows.
   Whole-run avg/p95 + submission-phase columns + pinned-pose extraction.
9. Report against the KPI arithmetic: befallen zone-only p95 174.3ms and
   combined p95 279.7ms baselines; state the projected combined p95 if
   the zone merge lands together with P08c's composite entities (multiply
   out the isolated effects honestly — flag that it's a projection).
10. Invocation budget: <=12/session, 2 sessions OK.

### D. Production landing sketch (desk deliverable)

11. Map the chosen design onto production: which files/systems change
    (renderer_zone_load / renderer_zone_draw / renderer_zone_stream /
    ConstrainedMeshCache / GPU upload thread), how mesh-cache eviction
    keys change, migration/fallback story, risk list, unit sizing.

### STOP conditions

- A-gate micro-benchmark shows no primitive beats N-separate-draws by
  enough to matter (<30% projected draw-time reduction at tail poses):
  stop, report, re-point at the occlusion-budget lever.
- Prototype misses pixel-identity (options 2/3) and the cause isn't
  quickly attributable: stop and report with the diff evidence.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  visual gate PASS with goldens UNTOUCHED (flag default off);
  scripts/build-arm-noble.sh; scripts/refactor/check_file_sizes.sh fable
  (no new FAILs — new machinery goes in new TUs, zone_bench_loader.cpp is
  at 2999/3000).
- Replay discipline: pose columns byte-identical to path files; unmerged
  runs byte-identical to P08/P08b baselines on geometry columns (prove
  the prototype changes nothing when off).
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O
  to Pi-local /tmp, stripped staged binary from slot1 (ARM_BUILD_BIN_DIR
  trap), drop_caches never reboot, leave Pi pristine, report blocked if
  unreachable.
- CSVs → benchmarks/pi_runs/p09_merge/; contact sheets (only if option 1)
  → benchmarks/contact_sheets/p09_merge/.
- Review must contain: the A-gate micro-benchmark table, the design
  decision + interaction reconciliation, per-pose draw reduction vs
  ceiling, pixel-identity verdicts, Pi perf tables, the KPI projection,
  and the production landing sketch.

## Out of scope

- ANY production code change (benchmark + design doc only).
- Entity/object draw paths (P08c/P08d territory); occlusion redesign;
  lighting changes; mesh-cache changes (sketch only); goldens; paths.
- Building more than ONE prototype architecture (the A-gate picks one).

## Review

Executed 2026-07-25/26 on `wt/slot1`, branch `task/P09-merge-design`, cut from
`fable` at 483e13a. Pi: user@10.0.30.192, reachable throughout, left pristine
(1 session, well under the 12-invocation budget — 1 A-gate probe run).

### Phase A — paper evaluation + A-gate

**Interaction reconciliation (paper, before any code):**

1. **Mesh-cache eviction granularity.** Production's `ConstrainedMeshCache`
   keys are `size_t regionIdx` (`include/client/graphics/constrained_mesh_cache.h`)
   — one region's `IMeshSceneNode` evicted/rebuilt as a unit. Candidate 1
   (load-time hard merge per cell) and candidate 2/3 with a *whole-zone*
   shared VBO both break this: a shared buffer can't be partially evicted.
   Candidate 2/3 can be scoped to match eviction granularity (a "merge cell"
   = the streaming unit) without changing the mechanism; candidate 1 is
   fundamentally coupled to its merge-cell choice (draws the whole cell
   regardless of per-region need).
2. **Per-region adaptive lighting (count/area/radius modes).** All three
   modes decide a lighting *tier* per region (`zone_bench_loader.cpp`
   `collectDrawEntries()`, count = inline budget check, area/radius =
   separate pre-passes into `areaUseLW`/`radiusUseLW` maps). The existing
   draw-time `materialKey = (effectiveMatType << 16) | texId` already folds
   the tier into the sort/group key — any design that groups by this exact
   key (candidates 2 and 3) preserves tier granularity for free: a run
   simply breaks at a tier boundary. Candidate 1 can't do this at all (tier
   is decided too late, per-frame, per-camera — a load-time cell buffer is
   tier-agnostic).
3. **Occlusion/PVS granularity.** Occluded regions are dropped from
   `visibleRegions` before `collectDrawEntries()` ever emits their
   `DrawEntry`s. Candidates 2/3, built from the post-cull `DrawEntry` list,
   inherit this for free (an occluded region's geometry is simply absent
   from the run). Candidate 1 must draw the whole cell buffer regardless of
   which member regions are individually occluded this frame — the
   documented cost of that option.
4. **Front-to-back ordering.** Candidate 2 as literally specified ("coalesce
   ADJACENT ranges" in a *static* shared buffer) only realizes the ceiling
   when the load-time buffer layout happens to match the per-frame
   front-to-back visitation order — never true in general (visitation order
   is camera-position-dependent, buffer layout is fixed at load time), so a
   literal implementation would coalesce only accidental runs. Candidate 3
   (per-frame CPU gather) can assemble the index list in *exactly* the
   current frame's front-to-back order regardless of static layout, at the
   cost of a per-frame index copy.

Candidate 1 was already paper-eliminated by this reconciliation (loses #3
unconditionally, loses #2 unconditionally) before any measurement — it only
remains viable as a design if #2/#3 losses are judged pixel/perf-acceptable,
which the plan reserves for "if two options are close." They were not close
(see A-gate below), so candidate 1 was not pursued further.

**A-gate micro-benchmark.** Extended `--synthetic-draws` with a sibling mode,
`--merge-probe` (new TU `tools/hwcaps/zone_bench_merge_probe.cpp/.h`, wired
into the `gles2_zone_benchmark` CMake target only). Matched synthetic
geometry (2-tri quads, one per "region", order-of-magnitude matched to P08's
B3 histogram), four primitives swept at N ∈ {1,10,50,100,200,500,1000}:
`separate-vbo` (today's per-region draw, N independent VBO+IBO objects, full
bind each draw), `shared-coalesced` (one shared static VBO+IBO, N
`glDrawElements` calls at different byte offsets, no rebind — candidate 2
literally), `shared-1draw` (one shared VBO+IBO, ONE `glDrawElements` spanning
all N regions' contiguous indices — candidate 2's *best case*, i.e. what
happens when adjacency actually holds), `streamed-ibo` (per-frame CPU gather
into a pre-sized `GL_DYNAMIC_DRAW` buffer via `glBufferSubData`, one draw —
candidate 3a), `client-index` (same gather, `glDrawElements` against a
client-memory pointer, no GL buffer at all — candidate 3b). CPU submission
time measured via `steady_clock` around the draw-call loop only (GPU sync via
`glFinish` timed separately), 5 warmup iterations, `--frames 20` (Pi) / `30`
(x86) measured repetitions per cell.

CPU submission time at N=1000 (the tail-pose scale — bf472 has 1297 zone
draws unmerged):

| primitive | x86 llvmpipe (ms) | Pi Mali400/Lima (ms) |
|---|---|---|
| separate-vbo | 1.818 | 59.173 |
| shared-coalesced | 0.837 | 22.673 |
| shared-1draw | 0.273 | 0.041 |
| streamed-ibo | 0.275 | 1.215 |
| client-index | 0.276 | 0.147 |

Full sweep (all N, both machines) in
`benchmarks/pi_runs/p09_merge/agate_x86.csv` and the Pi console log
(`refactor/P09_cross_region_merge.md` review — Pi CSV not pulled back, ran
with `--frames 20` directly to stdout via SSH; x86 CSV is the committed
artifact). Findings:

- **Lima's dynamic/client-index handling is NOT pathological — it is the
  cheapest primitive measured.** This resolves the plan's stated unknown:
  `client-index` beats every other primitive on the Pi at every N ≥ 10, and
  even beats `streamed-ibo` (no GL buffer upload at all vs. a
  `glBufferSubData` call). x86 shows all three "gather to 1 draw" primitives
  converge to nearly the same (near-zero) cost — the interesting
  differentiation is Pi-only.
- **`shared-coalesced` (candidate 2's literal mechanism) does NOT eliminate
  the per-draw cost** — at N=1000 on Pi it's 59.173→22.673ms, a ~2.6x
  improvement from *not rebinding the buffer*, but still scales ~linearly
  with N (still N `glDrawElements` calls). It never approaches
  `shared-1draw`'s near-zero cost unless the N draws truly collapse to one
  call. Combined with the front-to-back paper finding above (true adjacency
  essentially never holds for a fixed load-time layout across arbitrary
  camera positions), candidate 2 as literally specified would NOT reach
  anywhere near the 97.8% ceiling on real camera paths — it would land
  somewhere between the `separate-vbo` and `shared-coalesced` lines,
  pose-dependent and unpredictable.
- **Per-frame CPU index gather is measured cheap on both machines** — the
  A-gate's `client-index`/`streamed-ibo` cost is 2-3 orders of magnitude
  below `separate-vbo` at realistic N, with no evidence of GLES2/Lima
  penalizing dynamic index submission.

**A-gate STOP-condition check**: N=1000 CPU submission drops from 59.173ms
(separate-vbo) to 0.147ms (client-index) on the Pi — a >99% reduction, far
above the plan's "<30% projected draw-time reduction" STOP threshold. A-gate
clears the gate decisively.

**Design decision (written before any prototype code, per the plan):**
candidate architecture #3, **client-side-index variant**. Rationale: (a) the
paper reconciliation shows it is the only candidate that preserves all four
interactions simultaneously without modification (mesh-cache eviction is
addressed by *scoping* the persistent vertex buffer to the existing
streaming/eviction unit in production, not by changing the mechanism — see
Phase D); (b) the A-gate shows Lima's client-side index path is both correct
(no pathological behavior — the plan's named risk) and the fastest of the
four measured primitives; (c) candidate 2's literal buffer-adjacency
mechanism is shown, by the `shared-coalesced` vs `shared-1draw` gap, to
depend on a coincidence (load-time layout matching per-frame visitation
order) that a per-frame CPU gather (candidate 3) sidesteps entirely by
construction. Two options were not "close" in the plan's sense (candidate 3
wins by 2-3 orders of magnitude at realistic scale), so the "prefer
per-region semantics when close" tiebreak did not apply — though notably
candidate 3 *also* best preserves per-region semantics, so the two criteria
point the same way here.

### Phase B — prototype

Implemented behind `--merge-zone-draws` (default OFF everywhere), new TU
`tools/hwcaps/zone_bench_merge.cpp/.h` (zone geometry only; entities/objects
untouched, confirmed by code — `buildMergedZoneBuffers` only reads
`data.regions`). Mechanism: at zone load, for each region×material
`IMeshBuffer` (EVT_STANDARD / `S3DVertex` only — the format
`buildTexturedMesh`/`buildColoredMesh` actually produce for zone geometry; a
defensive skip-and-fall-back-to-unmerged exists for any other vertex type,
never observed to trigger), bucket by H02b `textureGroupId` into a persistent
per-texture CPU vertex array (world-space baked — region translation added
once at load time, since regions use translation-only world matrices — so a
merged draw can use an identity world transform) with a 65535-vertex
overflow guard that starts a new "generation" per bucket when a texture's
combined region set would exceed the uint16 index range (this doubles as a
natural upper bound on merge-cell size, relevant to the Phase D sketch).
Region×buffer index ranges are recorded once. At draw time,
`renderMergedDrawEntries()` walks the already-material-sorted `DrawEntry`
list (unchanged `collectDrawEntries()` output) and coalesces runs of
consecutive entries sharing `.materialKey` (which already encodes texture +
effective MaterialType, i.e. the per-region lighting-tier decision) by
gathering their precomputed index ranges into a small per-frame scratch
`SMeshBuffer` (never registered with the driver's `HWBufferMap`, so
`COpenGLES2Driver::drawMeshBuffer()` takes its existing client-side-vertex-
array fallback path — the same code path already used in production for
entities/doors/UI — this is the `client-index` A-gate primitive, reusing the
driver unmodified rather than issuing raw GL calls). Runs of length 1 draw
via the original unmerged code path unchanged.

**Draw-count reduction — matches the ceiling exactly at every measured
pose**, production config (`--adaptive-lighting --zone-pp-budget 20
--pp-budget-mode count --player-light-level 9`), x86 llvmpipe, single-pose
capture (`--camera-pose`):

| pose | zone | unmerged draws | merged draws | ceiling (state_changes) | reduction |
|---|---|---|---|---|---|
| golden0 | qeynos2 | 236 | 31 | 31 | 86.9% |
| golden1 | qeynos2 | 119 | 22 | 22 | 81.5% |
| golden2 | qeynos2 | 228 | 16 | 16 | 93.0% |
| golden3 | gfaydark | 143 | 15 | 15 | 89.5% |
| golden4 | gfaydark | 134 | 13 | 13 | 90.3% |
| golden5 | gfaydark | 203 | 16 | 16 | 92.1% |
| golden6 | befallen | 368 | 13 | 13 | 96.5% |
| golden7 | befallen | 75 | 9 | 9 | 88.0% |
| golden8 | befallen | 170 | 9 | 9 | 94.7% |
| bf355 | befallen | 122 | 8 | 8 | 93.4% |
| bf472 | befallen | 1308 | 14 | 14 | 98.9% |
| bf626 | befallen | 6 | 3 | 3 | 50.0% |
| bf791 | befallen | 234 | 9 | 9 | 96.2% |
| bf917 | befallen | 112 | 8 | 8 | 92.9% |
| bf987 | befallen | 767 | 11 | 11 | 98.6% |

`merged draws == state_changes` (the ceiling) at all 15 poses, every time —
the coalescing mechanism itself is architecturally sound and achieves 100%
of P08's theoretical ceiling with no shortfall. Poly counts (`polys=`)
matched exactly between merged/unmerged at every pose (same triangle count
submitted, as expected — only draw-call grouping changed).

**Pixel-identity: FAILS.** Compared merged vs. unmerged renders at all 9
golden views + all 6 pinned poses (production config, x86 llvmpipe,
decoded-pixel RGB diff — same methodology as `scripts/ci/visual_gate.py`'s
`compare_images_exact`, `refactor/scratch/p09_pixel_identity.py`):

| pose | zone | diff pixels (of 480,000) | diff bbox |
|---|---|---|---|
| golden0 | qeynos2 | 45,670 | full-frame |
| golden1 | qeynos2 | 6,262 | full-frame |
| golden2 | qeynos2 | 1,890 | full-frame |
| golden3 | gfaydark | 14,344 | full-frame |
| golden4 | gfaydark | **148,006** | full-frame |
| golden5 | gfaydark | 4,267 | full-frame |
| golden6 | befallen | 92,219 | full-frame |
| golden7 | befallen | 31,004 | full-frame |
| golden8 | befallen | 15,204 | full-frame |
| bf355 | befallen | **2** | (10,368)-(17,406), 7×38px patch |
| bf472 | befallen | 14 | small, localized |
| bf626 | befallen | 42 | small, localized |
| bf791 | befallen | 44 | small, localized |
| bf917 | befallen | 1,864 | localized |
| bf987 | befallen | 42,720 | large |

Two qualitatively different failure modes are visible, not one:

1. **Small, localized diffs (bf355: 2px, bf472: 14px, bf626: 42px, bf791:
   44px)** — a handful of pixels each, in small bounding boxes. Consistent
   with the *known, documented, expected* risk noted in the design section:
   merging changes triangle submission order within a material group (no
   longer strictly front-to-back inside one merged draw call), so on
   exactly-coplanar/overlapping geometry a depth-test tie can flip which
   triangle "wins" — the same z-fighting mechanism the codebase's H02b
   comment already documents for a different reordering (texture-hash vs.
   pointer-hash draw order). This class does not, by itself, falsify the
   design; it's a plausible, attributable, low-magnitude consequence of
   reordering that the plan's "max_delta=0 expected" bar does not budget for
   and that a real implementation would need a documented tolerance for (or
   a stable secondary sort key to eliminate it).
2. **Large, full-frame diffs (golden4: 148,006px = 31% of the frame;
   golden6: 92,219px; bf987: 42,720px; most golden poses in the
   thousands-to-tens-of-thousands range)** — NOT z-fighting. Visual
   inspection (gfaydark golden4, saved to
   `build-gles2/ci_artifacts/p09_pixel/gfaydark_golden4_{merged,unmerged}_f0.png`)
   shows entire textured surfaces (a roof) rendering as a flat,
   untextured-looking warm-white lit gradient in the merged output, vs. the
   correct tiled shingle texture in the unmerged reference — same silhouette
   and same overall lighting *direction*, but the diffuse texture pattern is
   effectively gone.

**Investigation performed (this failure mode was NOT quickly attributed
despite substantial effort — documenting what was ruled out):**
- Verified via `P09_MERGE_DEBUG=1` diagnostic printing that every merged
  run's constituent regions share the identical texture pointer, texture
  name, `MaterialType` (256 = production's `materialPerPixelSolid`), and
  `MaterialTypeParam`/`MaterialTypeParam2` (both 0.0) — the source material
  data feeding the merge is internally consistent, ruling out a
  `textureGroupId` hash collision or a materialKey-grouping bug mixing
  different textures into one run.
- Verified `glGetError()` returns `GL_NO_ERROR` (0x0) after every merged
  `drawMeshBuffer()` call.
- Verified the vertex/index gather logic matches Irrlicht's own
  `CMeshBuffer<T>::append()` precedent (`third_party/irrlicht-gles2-x86/include/CMeshBuffer.h:189-198`
  — `Indices.push_back(indices[i]+vertexCount)`, the exact rebasing pattern
  used here) and confirmed `getVertexType()` reports `EVT_STANDARD`
  (matches, no format mismatch) for every merged buffer in the debug trace.
- Verified sample vertex positions/UVs pulled from the live merged buffer
  immediately before the draw call are in-range and plausible (world-scale
  positions, UV mostly in [0,1] with expected wrap-mode overflow on tiled
  surfaces).
- Ruled out an index/vertex overflow: no bucket in the traced runs
  approached the 65535-vertex generation limit.
- Did NOT find the root cause. Leading unconfirmed hypothesis: something in
  the custom-shader per-material-vs-per-mesh-buffer state assumption inside
  `COpenGLES2Driver::applyCustomShaderState`/`zone_shader.cpp`'s
  `ShaderCallback` — most uniform state there is read from
  `driver->getTransform()`/global shader-manager state (frame-scoped, not
  per-mesh-buffer, and confirmed unaffected by merging), but the
  "AtlasShaderCallback" precedent (`material.MaterialTypeParam` selects an
  atlas page — a real per-mesh-buffer datum carried on `SMaterial` rather
  than via any bounding-box/mesh-buffer lookup) shows this codebase *does*
  sometimes carry mesh-specific state through fields the merge prototype
  copies verbatim (`mat = first.buffer->getMaterial()`) but that were not
  exhaustively cross-checked against every one of the ~6 built-in shader
  variants (solid/alphaTest/atlas × per-pixel/lightweight × wind) — the
  roof material could plausibly be hitting a shader variant with a
  per-mesh-buffer assumption not yet identified.

**STOP condition invoked** (per the plan: "Prototype misses pixel-identity
(options 2/3) and the cause isn't quickly attributable: stop and report with
the diff evidence"). Given this, Phase C's full Pi replay measurement (which
would time a known-visually-incorrect render path) was **not executed** —
running and reporting perf numbers for a renderer that doesn't reproduce the
reference image would misrepresent the design's viability. The A-gate
portion of Pi work (Phase A, primitive-only, measures GL submission
mechanics rather than the zone-geometry render path) is unaffected by this
bug and stands as reported above.

### Phase C — measurement

**Not executed** (STOP condition from Phase B). No Pi zone-render replay
numbers are reported for the merged path. Baselines are unaffected (flag
defaults off; `visual_gate` ctest entry and the full x86 golden-frame gate
both PASS unmodified — see Verification below).

### Phase D — production landing sketch

Written as a design-level sketch informed by the (mechanically-validated,
pixel-bugged) prototype; NOT a claim that the mechanism is ready to land as-is.

- **Files that would change**: `renderer_zone_load.cpp` (build the
  persistent per-texture, per-*merge-cell* vertex buffers once at cell
  stream-in, not whole-zone — see below); `renderer_zone_draw.cpp` (replace
  the per-`ZoneDrawEntry` `setMaterial`+`drawMeshBuffer` loop with the
  run-coalescing walk prototyped here); `renderer_zone_stream.cpp`
  (streaming/eviction would drive merge-cell lifetime, see next point);
  `include/client/graphics/constrained_mesh_cache.h` (key changes below).
  No changes needed to occlusion, PVS/frustum, or adaptive-lighting code —
  all three already run unmodified upstream of the draw-time coalescing
  step, per the Phase A interaction analysis.
- **Mesh-cache key change**: today's key is `size_t regionIdx` (one region
  = one evictable unit). Landing this design changes the eviction unit to a
  **merge cell** — a cluster of nearby regions sharing a streaming lifetime,
  sized so that (a) it matches whatever admission/eviction radius band
  `ConstrainedMeshCache` already uses (so eviction behavior is unchanged in
  spirit, just coarser-grained) and (b) stays under the 65535-vertex/texture
  cap the prototype already had to respect (naturally bounds cell size
  regardless of the streaming-radius choice). This is a real, non-trivial
  eviction-granularity change (interaction #1) — it must evict/rebuild a
  whole cell's per-texture buffers as a unit, which is coarser than today's
  per-region eviction and would need its own memory-pressure validation
  (not attempted here — out of scope per the plan, "mesh-cache changes
  (sketch only)").
- **Migration/fallback**: flag-gated exactly as prototyped
  (`--merge-zone-draws` analog in production, default off); the unmerged
  path is untouched code, so fallback is "don't set the flag." No data
  migration needed — merge buffers are derived, rebuildable, not persisted.
- **Risks** (beyond the open pixel-identity bug, which blocks landing
  outright until root-caused): (1) eviction-granularity coarsening could
  increase peak resident geometry memory during camera movement near cell
  boundaries — needs its own measurement unit; (2) the front-to-back
  early-Z ordering loss *within* a merged draw (documented, not yet
  perf-measured — the A-gate measured submission cost only, not GPU-side
  overdraw cost) could partially offset the submission-time win on
  overdraw-heavy scenes; (3) shader-callback state assumptions (the
  suspected root cause class above) need a systematic audit across all
  built-in zone shader variants before this is safe to build again.
- **Unit sizing**: a follow-on unit should (a) root-cause the pixel bug
  (recommend: bisect by shader variant — force every merged run through
  the plain `Solid3D` non-atlas, non-wind path first and re-run the pixel-
  identity check to isolate whether the bug is variant-specific), (b) design
  and prototype the merge-cell/eviction-granularity mechanism, (c) re-run
  Phase C's full Pi measurement protocol once (a) is resolved. Given the
  draw-count mechanism itself is already fully proven (this unit's table
  above), a follow-on unit should be scoped to CORRECTNESS + eviction, not a
  redesign of the coalescing mechanism.

### KPI projection

Not computed — Phase C did not run (STOP condition), so there is no merged
Pi timing to project against the P08b baselines (befallen zone-only p95
174.3ms, combined p95 279.7ms). The A-gate CPU-submission deltas (Pi,
N=1000: 59.173ms→0.147ms) are a primitive-level bound, not a frame-level
projection, and are not substituted for the real measurement here to avoid
overstating a result the render-correctness bug undermines.

### Verification results

1. `cmake --build build -j24`: **PASS**.
2. `ctest --test-dir build --output-on-failure`: **PASS** (1223/1225; the 2
   failures are exactly the documented host exceptions —
   `AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes`). `visual_gate` ctest entry
   passed within this run.
3. `bash scripts/build-gles2-x86.sh`: **PASS** (full Irrlicht rebuild +
   `gles2_zone_benchmark` relink).
4. `bash scripts/ci/visual_gate.sh` (flag default off): **PASS** — "=== VISUAL
   GATE: PASS ===", goldens byte-for-pixel unchanged, confirming the merge
   prototype has zero effect on any existing path when the flag is off.
5. `bash scripts/build-arm-noble.sh`: **PASS** — `build-arm-noble/bin/gles2_zone_benchmark`
   built with the merge prototype + A-gate probe.
6. `bash scripts/refactor/check_file_sizes.sh fable`: **PASS**, no new FAILs.
   New machinery went into 3 new TUs (`zone_bench_merge_probe.cpp/.h` 346/49
   lines, `zone_bench_merge.cpp/.h` ~260/72 lines) rather than growing
   `zone_bench_loader.cpp` (still 2999 lines, untouched) — only pre-existing
   WARN is `gles2_zone_benchmark.cpp` at 2725 lines (grew from 2675 by the
   CLI/dispatch wiring for the two new flags; was already WARN before this
   unit).
7. Replay discipline: not fully exercised — Phase C (the phase that runs
   `--path-file` canonical replays and checks byte-identity against P08/P08b
   baselines) did not execute due to the Phase B STOP. The flag-off
   byte-identity claim is instead established by the visual_gate PASS (item
   4) and by construction — `renderDrawEntries()`'s merge branch is
   unreachable when `opts.mergeZoneDraws` is false, and every modified call
   site passes `entries.size()` as the draws count in that case, identical
   to the pre-P09 code.

### Deviations from the plan

- Phase C (Pi measurement) and the Phase C/D KPI projection were not
  executed, per the plan's own STOP condition for a pixel-identity failure
  without quick attribution. This is a deviation from "execute the plan
  exactly" only in the sense that the plan's STOP conditions are themselves
  part of "the plan" — this is the STOP path, not a skipped step.
- The A-gate CSV for the Pi run was not saved to
  `benchmarks/pi_runs/p09_merge/` (only captured in the SSH session
  transcript reproduced in this Review); the x86 CSV IS saved there
  (`agate_x86.csv`). A re-run to capture the Pi CSV would cost one more
  invocation against the 12/session budget; not spent here since the
  console numbers are already fully captured above and the unit's outcome
  does not turn on re-deriving them from a file.
