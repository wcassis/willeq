# P11 — Measure the stacked levers (merge + composites + occlusion budget) against the 30fps KPI

Approved by Will 2026-07-26 ("go ahead with both in parallel").
Slot: wt/slot1, branch task/P11-stacked-measurement, cut from fable
(2c69c65). Benchmark measurement unit — Pi NON-INTERACTIVE only (P02
protocol; Will cannot play right now). Runs in parallel with P12
(slot2, --animate root-cause, x86-only — P11 OWNS the Pi). No
production changes except none; occluder-budget landing is a report.

## Motivation

Every KPI number so far measures levers in isolation; the "~44ms
stacked" figure is arithmetic. Also, P08c's composite perf numbers
(-33.1%/-56.0%) were measured through the P09b-discovered stand-in path
with its own cull test (different visible subsets) — possibly
contaminated; the scenegraph composite route (--composite-scenegraph,
the production-faithful one) has never been perf-measured. This unit
replaces projection with measurement and adds the third lever
(occluder budget, P08's +225-regions headroom finding — cuts GPU fill
as well as CPU submission; GPU sync is the next wall at 32.4% of
frame).

## Design

### A. Compose the flags (x86 first)

1. Verify --merge-zone-draws and --composite-scenegraph compose in the
   benchmark (composite entity draws + merged zone draws in one frame):
   geometry columns byte-identical to the individual modes' expectations
   on the canonical befallen replay; pixel spot-check at 2 frames
   (expected: FP class from merge only). Fix benchmark-side composition
   bugs if trivial; STOP if structural.
2. Occluder budget: locate the benchmark's occluder cap (P08's
   headroom experiment machinery, zone_bench_histogram-era) and expose a
   run-time knob if not already a flag (--occluder-budget N /
   uncapped). ALSO desk-map where PRODUCTION's cap lives
   (sim_visibility.cpp / config) and what a production landing would
   change — report only.

### B. Pi measurement (P02 protocol, canonical replays, production config)

3. befallen combined (--entities 205, the KPI scenario), 1000-frame
   canonical replay, variants:
   a. baseline (reuse P08b/P09c committed CSVs where schema-identical);
   b. composite-scenegraph only (the CLEAN re-measure of P08c's number);
   c. merge only (reuse P09c's committed run if schema-identical);
   d. merge + composite (THE stacked number);
   e. merge + composite + occluder budget raised (two settings if cheap:
      e.g. 2x and uncapped).
   Whole-run avg/p95 + submission phase columns + pinned-pose extraction
   (bf355/472/626/791/917/987). Randomize variant order per P02.
4. befallen zone-only merge+occlusion variant if invocation budget
   allows (isolates occlusion's zone-side effect).
5. Budget: <=12 invocations/session, 2 sessions OK. Reuse committed
   baselines aggressively — spend invocations on the NEW variants.

### C. Deliverables

6. THE TABLE: variant × (avg, p95, fps, draws, submission ms, GPU sync)
   against the 33.3ms/30fps line — the campaign's central question
   answered: does stacked cross KPI on befallen; if not, the measured
   residual and where it lives (submission vs GPU).
7. Corrected composite perf number (scenegraph route) vs P08c's
   stand-in number — state the delta plainly.
8. Occluder-budget verdict + production-landing map (report only).
9. **Side deliverable**: run the --all-zones composite sheet sweep
   (offline x86, P08f's documented invocation with --zones-json; ~25min
   by P08f timing) → report sheets generated/collisions/exclusions;
   structural audit; sheets to NFS. (Production play coverage for
   whenever Will is back; independent of the Pi work.)

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- Replay discipline: pose columns byte-identical to path files; reused
  baselines schema-verified; geometry columns byte-identical across
  variants EXCEPT where occlusion variants legitimately change
  regions_drawn/draws (document expected deltas — occlusion changes
  what is drawn BY DESIGN; draws/polys will drop; pose columns must
  still match).
- Pi rules: P02 via pi_common.py, /tmp-only I/O, ARM_BUILD_BIN_DIR trap
  (slot1 build), drop_caches never reboot, pristine exit. P12 (slot2)
  does NOT use the Pi — no coordination needed, but confirm no stray
  processes before sessions.
- CSVs → benchmarks/pi_runs/p11_stacked/.

## Out of scope

- Production code changes (occluder landing is a report; flips are
  Will's).
- Interactive Pi use; entity animation (P12's territory); impostors.

## Review

(executor fills in)
