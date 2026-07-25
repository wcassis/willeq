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

(executor fills in)
