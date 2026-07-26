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

Executed 2026-07-26 on `wt/slot1`, branch `task/P11-stacked-measurement`, cut from
`fable` at `2b2b168`.

### Phase A — x86 composition check + occluder-budget knob

**Composition check** (`build-gles2/bin/gles2_zone_benchmark`, EGL surfaceless/llvmpipe,
befallen combined e205, production config, 3 measured frames): `--merge-zone-draws` and
`--composite-scenegraph` compose cleanly, no structural bug.
- Geometry columns (`draws`/`state_changes`/`polys`/`regions_vis`/`regions_drawn`/
  `regions_occluded`) at every frame: `both == merge` exactly on `draws` (2023/1342/672
  vs baseline/composite's 2028/1350/690 — the merge-only reduction), and `state_changes`/
  `polys`/`regions_*` byte-identical across ALL FOUR modes (baseline, merge, composite,
  both) — additive composition, as the plan predicted.
- 2-frame pixel spot-check (RGB exact compare, 800x600): `baseline vs composite` = 0 diff
  px (composite-scenegraph renders through the SAME Irrlicht scene-graph dispatch as
  baseline — a mesh-building/preload difference only, not a draw-dispatch difference, so
  it is visually and geometrically inert in this benchmark); `baseline vs merge` and
  `merge vs both` = the SAME known small alpha-edge FP class (frame 0: 2,178px max_delta 3;
  frame 1: 1,666px max_delta 3) — confirms the FP class comes from the merge lever only,
  composite contributes zero additional pixel delta. **No composition bugs; nothing to
  fix; not structural.**

**Occluder-budget knob**: none existed at runtime — `maxOccluderRegions` was hardcoded to
16 in `zone_bench_loader.cpp`'s zone load step 10 (software occlusion culler setup).
Added `--occluder-budget N|uncapped` (`tools/hwcaps/gles2_zone_benchmark.cpp` CLI parse +
`Options.occluderBudget`; `ZoneBench::BenchConfig.occluderBudget`,
`tools/hwcaps/zone_bench_loader.h`; consumed at `zone_bench_loader.cpp`'s occConfig setup,
`-1` = unset = the prior hardcoded 16, `uncapped` maps to `1<<20`, which exceeds any
canonical zone's region count). Verified on x86: `--occluder-budget 16` reproduces the
pre-flag output byte-for-byte on the 3 sample frames; `32`/`uncapped` accepted without
regression (no visible effect on this 3-frame sample — the P08 headroom effect needs the
1000-frame Pi replay's more varied poses, confirmed in Phase B).

**Production occluder-cap desk-map**: production's cap is
`ConstrainedRendererConfig::occlusionMaxOccluderRegions`
(`include/client/graphics/constrained_renderer_config.h:92`, default 16, set explicitly in
`constrained_renderer_config.cpp:284`'s orangepi preset — same value the benchmark
hardcoded), consumed at `irrlicht_renderer.cpp:1421` (`occConfig.maxOccluderRegions =
config_.constrainedConfig.occlusionMaxOccluderRegions;`) when constructing the runtime
`SoftwareOcclusionCuller`. A production landing of a raised budget would mean: (1) add an
`occlusionMaxOccluderRegions` override to the orangepi preset (or a new CLI/config knob
threading into `ConstrainedRendererConfig`) — same one-line change class as this unit's
benchmark flag; (2) the config already flows to the right place (`irrlicht_renderer.cpp`
already reads the field, no plumbing gap) — this is a config-value change, not a code
change. See Phase C occluder verdict below for whether it's worth landing.

### Phase B — Pi measurement (P02 protocol)

Pi session: `user@10.0.30.192`, reachable throughout. Two mini-sessions (5 invocations
planned, 1 SessionAbort after invocation #1 on CmaFree threshold, remediated via
`sync && drop_caches` — CmaFree 39,832kB → 76,416kB, **no reboot** — then 4 remaining
invocations in a clean retry session): 5/12 invocations total used across both, well under
cap. `ARM_BUILD_BIN_DIR` overridden to this worktree's `build-arm-noble/bin` before any Pi
contact (script: `refactor/scratch/p11_pi_stacked_replay.py`, same bespoke pattern as
P09c's `p09c_pi_merge_replay.py`); ARM binary rebuilt immediately before the session
(includes `--merge-zone-draws`, `--composite-scenegraph`, `--occluder-budget`). Governor
pinned to `performance` during runs, restored to `ondemand` on every exit (verified).
Pi left pristine (leftover `/tmp/p11_*`/staged binary removed, verified via `ls /tmp`
showing only unrelated systemd-private dirs).

**Reuse (schema-verified)**: `benchmarks/pi_runs/p09c_measure/befallen_combined_unmerged.csv`
and `befallen_combined_merged.csv` — header identical to this build's CSV output
(`frame,pos_x,...,entities_visible`, 26 columns) — reused as the baseline and merge-only
rows instead of re-running them, per the plan's invocation-budget guidance.

Canonical replay: `benchmarks/paths/befallen_seed42_1000f.path`, 1000 frames, seed 42,
production config (`--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count
--player-light-level 9`), `--entities 205`, 800x600, DRM/GLES2/Mali400.

**Replay discipline**: pose columns (`pos_x/y/z/heading`) 0/1000 mismatches vs the path
file, every new variant. Unmerged-equivalent geometry columns byte-identical to the
reused baselines where the lever doesn't change them (`draws`/`polys` 0 mismatches,
composite-scenegraph-only vs unmerged baseline; `draws` 0 mismatches, merge+composite vs
merge-only baseline) — confirms composite-scenegraph changes nothing on the wire (matches
Phase A) and the stacked run's merge-side draw reduction is exactly what merge-alone
produces. Occlusion variants legitimately change `regions_occluded`/`draws` (documented
below), by design.

### THE TABLE (befallen combined e205, 1000-frame replay, whole-run avg/p95)

| variant | avg total_ms | p95 total_ms | fps (avg) | fps (p95) | draws avg | submission avg/p95 (ms) | GPU-sync avg/p95 (ms) |
|---|---|---|---|---|---|---|---|
| baseline (unmerged, no composite) [reused P09c] | 104.26 | 279.39 | 9.6 | 3.6 | 710.1 | 67.03 / 186.95 | 33.98 / 81.99 |
| composite-scenegraph only [NEW — corrected P08c number] | 104.52 | 279.41 | 9.6 | 3.6 | 710.1 | 67.03 / 187.28 | 34.24 / 81.99 |
| merge only [reused P09c] | 54.67 | 65.81 | 18.3 | 15.2 | 465.1 | 31.50 / 39.51 | 19.95 / 25.76 |
| **merge + composite (THE stacked number)** | **54.66** | **65.93** | **18.3** | **15.2** | 465.1 | 31.48 / 39.53 | 19.96 / 25.74 |
| merge + composite + occluder-budget 32 (2x) | 54.57 | 65.76 | 18.3 | 15.2 | 464.0 | 31.38 / 39.32 | 19.94 / 25.86 |
| merge + composite + occluder-budget uncapped | 53.19 | 65.36 | 18.8 | 15.3 | 455.6 | 30.39 / 39.10 | 19.45 / 25.24 |
| zone-only, merge + occluder-budget uncapped (e0, isolates zone-side occlusion) | 10.99 | 24.10 | 91.0 | 41.5 | 7.9 | 1.89 / 4.80 | 6.53 / 13.92 |

**30fps KPI line = 33.3ms/frame.** Verdict: **stacked levers do NOT cross the KPI on
befallen's combined e205 scenario** — even the fully-stacked variant (merge + composite +
uncapped occluder budget) sits at avg 53.19ms / p95 65.36ms, roughly **1.6x (avg) to 2.0x
(p95) over budget**. Measured residual at the stacked point: avg residual = 53.19 − 33.3 =
19.9ms, p95 residual = 65.36 − 33.3 = 32.1ms. Where it lives: submission (30.39ms avg /
39.10ms p95) is still the larger share (~57% avg, ~60% p95 of total), GPU-sync is the
second (~37% avg / ~39% p95) — entity draws (untouched by any of these three levers, per
P08c/P09c) dominate the remaining submission time (`draws_avg` only drops from 710→456,
~36%, because ~455 of those are entity draws the merge/composite/occlusion levers don't
touch), and GPU-sync (fill-rate-bound, `glFinish`) is the next-largest single line item —
matching the motivation's framing ("GPU sync is the next wall").

### Corrected scenegraph-composite number vs P08c's stand-in — the delta

**This is the headline correction.** P08c's `--composite-entities` stand-in measured
**-33.1% p95** on befallen (187.10ms vs baseline 279.67ms, its own table). The
production-faithful `--composite-scenegraph` route measured here is **-0.006% p95**
(279.41ms vs baseline 279.39ms/279.407ms fresh, i.e. statistically zero — within
run-to-run noise) — **not a partial correction, a complete one: P08c's entire composite
performance claim on befallen does not reproduce through the real dispatch path.**

Why: `--composite-scenegraph` (see the code's own comment at
`gles2_zone_benchmark.cpp:97`) exercises the REAL composite mesh-building/texture-preload
call (`setCompositeEntityPath`/`preloadModelData`) but still renders entities through the
SAME per-entity Irrlicht scene-graph draw calls as baseline — i.e. composite textures
change WHAT gets bound per entity, not HOW MANY draws happen. `--composite-entities` (the
stand-in) instead simulates a hypothetical "1 draw per entity" dispatch that production
does not actually have wired up. Confirmed structurally on x86 (Phase A: `baseline vs
composite` pixel-identical, 0 diff px; `draws`/`state_changes`/`polys` identical to
baseline at every frame) and on the Pi (`composite-scenegraph only` vs reused unmerged
baseline: 0/1000 `draws`/`polys` mismatches). **Consequence for the stacked number**:
composite-scenegraph contributes ~0ms to the stacked total (54.66ms merge+composite vs
54.67ms merge-only — within noise) — the entire stacked win is attributable to the merge
lever; composite is currently a no-op on the wire despite exercising the real asset path.
This does not contradict P08c's own honesty about the stand-in (P08c explicitly flagged
"--composite-entities is a stand-in... production's REAL composite pipeline is a
different, better implementation") — it quantifies exactly how large that gap is: the full
33.1%, not a partial fraction.

### Occluder-budget verdict + production-landing map

Raising the occluder budget past production's current cap of 16 buys very little on
befallen's canonical replay: 2x (32) is noise-level (54.57 vs 54.66ms avg, 0.16% p95
improvement); uncapped is the largest effect measured and still small (53.19 vs 54.66ms
avg, **2.7% avg / 0.86% p95** improvement) — `regions_occluded_avg` nearly doubles
(27.86 → 49.70) confirming more regions ARE being culled, and `draws_avg` drops modestly
(465.1 → 455.6, ~2%), but the entity-draw-dominated combined scenario dilutes the effect
the same way the zone merge's draw reduction gets diluted (P09c's finding, reconfirmed
here). The zone-only isolation variant (merge + occluder uncapped, e0) shows the same
story at the zone level: 10.99ms avg / 24.10ms p95, essentially matching P09c's zone-only
merged baseline (11.255ms avg / 25.497ms p95) with no meaningful occlusion delta —
befallen's canonical camera path does not have enough distant/large-occluder-eligible
geometry sitting just past the 16-region cutoff for this to matter. **Verdict: raising the
occluder budget is NOT worth a production landing on this evidence** — the desk-mapped
change (`occlusionMaxOccluderRegions` in the orangepi preset, `constrained_renderer_config.cpp:284`,
consumed at `irrlicht_renderer.cpp:1421`) is a one-line config value flip with no code-path
gap, so it is cheap if a DIFFERENT zone/camera-path profile ever shows a bigger headroom
gap (P08's original +225-region headroom finding was measured differently — a static,
farther-view pose, not this moving replay's poses — so this is not a contradiction of that
finding, just a statement that THIS canonical replay doesn't exercise it enough to bank a
production change on). Report only, no production change made.

### Side deliverable — `--all-zones` composite sheet sweep

Ran the full pre-Luclin roster (offline x86, one process, in-memory collision registry
across all zones): `entity_composite_builder --eq-path /mnt/projects/EverQuestP1999
--output /mnt/projects/EverQuest_ETC1 --all-zones --zones-json
docs/reference/pre_luclin_zones.json --sheet-size 2048 --eqe1 --report-json
benchmarks/contact_sheets/p11_all_zones_sweep/report.json` — 120 zones (74
classic/27 kunark/19 velious, 1 with no `_chr.s3d` archive). **Elapsed: 184.0s** (much
faster than the plan's ~25min estimate extrapolated from P08f's 3-zone/49.9s timing —
most of the 120-zone roster's models turned out to already be covered by the 3 golden
zones + global archives already built, so the bulk of the run resolves as duplicates
rather than fresh builds).

**Result: 191 sheets built, 1,441 duplicates (already covered), 97 collisions (reported,
not built — same-named race code with different texture content across zone archives, the
mechanism P08f documented), 39 build failures (all `DR2_HS_DEF`, sleeper zone, all 5
variants — texture set doesn't fit even at the 2048x2048 sheet-size ceiling), 0
animated-texture skips, 1 zone with no character archive.** Structural audit
(`refactor/scratch/p08f_sheet_audit.py --report`, same tool as P08f, no changes needed):
**191/191 new sheets clean, 0 broken** (`benchmarks/contact_sheets/p11_all_zones_sweep/sheet_audit_all_zones.json`).
Total sheet count on the NFS output (`/mnt/projects/EverQuest_ETC1`) after this run: 477
`.bmp` files (286 from P08f's golden-zone + global work + 191 new — reconciles exactly).
Did not re-audit the full 477-sheet set (P08f's precedent) given the new-sheet audit is
already clean and the 286 pre-existing sheets were already audited clean by P08f; flagged
as a smaller-scope audit than P08f's own re-audit-everything pass, not a gap in THIS unit's
new output. Report: `benchmarks/contact_sheets/p11_all_zones_sweep/report.json`. Sheets
themselves are on NFS (`/mnt/projects/EverQuest_ETC1`, not committed); the report JSON and
audit JSON are committed under `benchmarks/contact_sheets/p11_all_zones_sweep/`.

### Verification

- `cmake --build build -j24`: clean (only pre-existing `logging.h` `-Wformat-truncation`
  warning, unrelated to this unit's changes).
- `ctest --output-on-failure`: 1224/1225 passed; the 1 failure
  (`AudioManagerTest.PreloadCommonSounds`) matches a documented known-host exception;
  `RDPServerFrameTest.UpdateFrameMultipleTimes` did not fail this run (host-flaky, not
  always reproducing — consistent with it being a known, not guaranteed, exception).
- `scripts/build-gles2-x86.sh` build tree (`build-gles2`) rebuilt clean for the benchmark
  target; `scripts/ci/visual_gate.sh` → **PASS**, goldens untouched.
- `scripts/build-arm-noble.sh`: clean, twice (once before the Pi session, once after the
  file-size trim below) — `gles2_zone_benchmark` cross-compiles with the new
  `--occluder-budget` flag both times.
- `scripts/refactor/check_file_sizes.sh --files <changed set>`: initially
  `zone_bench_loader.cpp` **3003 lines — a genuinely NEW FAIL** (baseline was exactly 3000,
  the FAIL ceiling, per P08f's own note); trimmed the new occluder-budget comment (3 lines
  → inline single-line comment) back to **3000 lines exactly (WARN, not FAIL)** — same
  boundary P08f describes hitting, same fix (compact comments, no logic changes),
  reverified build + composition check unaffected after the trim.
  `gles2_zone_benchmark.cpp` 2791 lines (WARN, pre-existing baseline already WARN;
  the `--occluder-budget` CLI addition is ~8 lines, does not newly cross any ceiling).
  `zone_bench_loader.h` 458 lines (ok). **No new FAILs after the trim.**
- CSVs: `benchmarks/pi_runs/p11_stacked/` — `befallen_combined_composite_only.csv`,
  `befallen_combined_merge_composite.csv`, `befallen_combined_merge_composite_occ2x.csv`,
  `befallen_combined_merge_composite_occ_uncapped.csv`,
  `befallen_zone_merge_occ_uncapped_e0.csv`, `cma_telemetry.tsv`.

### Deviations from the plan

- The plan's step B4 ("zone-only merge+occlusion variant if invocation budget allows") was
  run at `--occluder-budget uncapped` only, not also a 2x/32 zone-only variant — invocation
  budget was spent on the combined-scenario occluder variants (the KPI-relevant scenario)
  instead; the zone-only run's purpose (isolate the zone-side occlusion effect) is served
  by the single uncapped data point, which already shows the effect is negligible on this
  replay, making a second zone-only occluder setting low-value.
- The `--all-zones` sweep ran far faster (184s) than the plan's ~25min estimate; not a
  problem, just a note that P08f's 3-zone extrapolation overestimated the 120-zone
  roster's incremental cost once duplicate detection is accounted for.
- One SessionAbort (CmaFree threshold) during Pi Phase B, remediated per the panic-safe
  protocol (`sync && drop_caches`, no reboot) and the remaining invocations completed in a
  clean retry session — same pattern P08c/P09c document, not a new failure mode.
- No production code was changed (occluder-budget landing and the composite-scenegraph
  finding are both report-only, per the plan's scope).
