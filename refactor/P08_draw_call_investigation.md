# P08 — Draw-call/CPU submission investigation (befallen tail poses)

Approved by Will 2026-07-25 ("go ahead with P08").
Slot: wt/slot1, branch task/P08-draw-calls, cut from fable (f502298).
Investigation unit (P05 style): root-cause and quantify, prototype at most
ONE lever behind a flag, REPORT — no default/architecture changes.

## Motivation

With the lighting arc closed (docs/decisions.md 2026-07-24 + 2026-07-25
addendum), the 30fps KPI gap is CPU-bound draw submission. Current blessed
tier-2 baseline (Q03: count mode + GLS level 9, benchmarks/pi_baseline/):
qeynos2 32.4/36.6 fps p95 (ABOVE KPI), gfaydark 25.7 (rgba) / 30.3 (eqe1),
befallen 25.3/26.5 — befallen is the worst and gfaydark-rgba second.
P06-era evidence: befallen tail frames run ~368 draws with tiny
polys-per-draw at ~26.5ms CPU, and the software occlusion culler is
near-blind in dungeons (single-digit occlusions out of ~156 visible
regions). Nothing has ever decomposed WHERE the CPU milliseconds go per
draw — that is this unit.

Everything here runs under the production config (manifest
productionConfig: count mode, budget 20, --player-light-level 9) and, for
moving runs, REPLAYS the canonical befallen path
(benchmarks/paths/befallen_seed42_1000f.path, --path-file) — first perf
unit with drift-free cross-unit-comparable moving data. Do NOT re-record
paths.

## Design

### A. Identify and pin the tail poses (new capability, use it)

1. x86 + Pi replay of the canonical befallen path (production config).
   From the Pi CSV, take the p95-and-worse frames (by total_ms), map frame
   index → path-file line → pose, and pin the 4-6 recurring worst poses as
   named statics (`bf<frameidx>` convention, like fr291/fr627). Record the
   pose list in the review — these become the standing befallen reference
   poses for all future units. Sanity: confirm draws/regions_vis at those
   poses roughly match the P06-era picture (~350+ draws).

### B. Decompose the frame (the core question: where do 26.5ms go?)

2. Instrument the benchmark's manual draw path with per-phase CPU timers,
   new CSV columns (always present; near-zero overhead — steady_clock
   reads, no syscalls): collect/cull phase (draw-entry collection incl.
   adaptive-lighting decisions), sort phase, submission phase
   (setMaterial + drawMeshBuffer loop), and everything-else (frame total
   minus phases). If the driver swallows time asynchronously, note what
   the submission phase does and does not capture (glFlush/eglSwapBuffers
   accounting — put swap/present in its own column).
3. Draw-composition histograms at the pinned poses (one-shot `--dump`-
   gated report, not per-frame): draws per region, polys per draw
   (distribution, not just mean), state changes, and how many draws are
   "tiny" (<32 polys). This answers WHY befallen is draw-heavy: BSP leaf
   granularity x per-material buffers is the hypothesis — verify it.
4. Per-draw fixed cost on the Pi (the number every lever's ROI hangs on):
   measure empirically with a controlled sweep — same total geometry
   split into N draws for several N (e.g. a `--split-draws N` test flag on
   one big region, or an extension of the existing micro-benchmark
   family). Deliverable: ms per 100 draws on Mali400/Lima at 800x600, and
   the same sweep on x86 llvmpipe for contrast (CPU-side driver cost vs
   GPU cost — use cpu_ms/gpu_ms columns to separate).

### C. Quantify the levers (measure ceilings, do not build them yet)

5. **Batching ceiling**: from the pose histograms, compute the draw count
   if same-material buffers were merged (i) within each region, (ii)
   across visible regions. Pure arithmetic from the histogram data — no
   implementation. Note the interactions a real batching lever must
   respect: per-region mesh cache/lazy loading, per-region adaptive-
   lighting material overrides, per-region occlusion/PVS culling
   granularity, front-to-back order within material groups.
6. **Occlusion headroom**: at the pinned poses, regions_occluded vs
   regions_vis (already CSV columns), plus a manual ground-truth estimate
   at 2-3 poses (how many visible regions contribute ZERO visible pixels —
   e.g. render with per-region color IDs at `--dump` time and count
   regions with no surviving pixels). Headroom = draws attributable to
   fully-hidden regions. Quantify only; occlusion redesign is its own
   future unit.
7. **Prototype AT MOST ONE lever**, and only if B4's fixed-cost number
   makes the ROI case decisive (>=20% predicted p95 improvement at the
   tail poses). Candidate order by expected cost/benefit: (a) submission-
   time index-merge of consecutive same-material draw entries from the
   same region (no VBO rebuild — check whether region buffers share VBOs
   or can use glDrawElements over concatenated index ranges; if buffers
   are separate VBOs this candidate dies cheaply, say so), (b) load-time
   merge of tiny same-material buffers within a region. Behind a flag
   (`--merge-draws` / config field, DEFAULT OFF everywhere). Measured on
   the Pi at the pinned poses + full canonical replay; byte-identical
   pixel check vs unmerged at the pinned poses (draw order within a
   material group changes are acceptable ONLY if pixels are identical;
   any pixel diff = report, do not rationalize).
8. STOP conditions: if the decomposition shows the CPU time is NOT in
   submission (e.g. it's in collection/sort/driver-swap), stop lever work
   and report — the ranked-levers list then targets whatever actually
   dominates. If prototype results are ambiguous, report without tuning.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  **visual gate PASS with goldens UNTOUCHED** (instrumentation must not
  change rendered output; new CSV columns are fine); scripts/
  build-arm-noble.sh; scripts/refactor/check_file_sizes.sh fable
  (gles2_zone_benchmark.cpp WARN is pre-existing; must not FAIL — if your
  additions push it near a limit, put the instrumentation report code in a
  new TU).
- Replay discipline: all moving runs via --path-file with the canonical
  befallen path; pose columns byte-identical to the path file (programmatic
  check); geometry columns byte-identical across instrumented vs
  uninstrumented builds (instrumentation changes nothing).
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O to
  Pi-local /tmp, stripped staged binary from slot1 (ARM_BUILD_BIN_DIR trap
  — env.sh defaults to main), drop_caches never reboot, <=12 invocations/
  session (2 sessions OK), leave Pi pristine, report blocked if
  unreachable.
- Review must contain: pinned pose list (frame idx + pose + why), the
  phase-decomposition table (x86 + Pi, tail poses + whole-run), draw
  histograms, ms-per-100-draws numbers, batching-ceiling arithmetic,
  occlusion-headroom counts, ranked lever list with predicted savings,
  prototype data if built (with the pixel-identity verdict), and a
  recommendation for the next unit.

## Out of scope

- Any default/preset/architecture change (report only; a landing unit
  follows if the data says so).
- Occlusion/PVS redesign (quantify headroom only).
- Entity/object draw paths (zone geometry only — entities are a separate
  submission path; note their draw counts in passing if free).
- Re-recording camera paths; touching the goldens; lighting work.

## Review

(executor fills in)
