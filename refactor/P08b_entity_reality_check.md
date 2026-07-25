# P08b — Entity-load reality check (is befallen's KPI gap an entity problem or a zone problem at gameplay loads?)

Approved by Will 2026-07-25 ("go ahead with the short entity-vs-gameplay-reality check").
Slot: wt/slot1, branch task/P08b-entity-reality, cut from fable (597deab).
SHORT measurement unit — one to two Pi sessions, additive benchmark
tweaks only if needed for measurement, no production changes, no levers
built. Answers one question and re-ranks P08's levers.

## Motivation

P08 found entities are 86.9% of combined submission time — but at the
benchmark's `--entities 200` stress config. Real gameplay caps visible
entities at ~40 (H04 live observation: 36-40 visible of 148 spawned,
qeynos2). Before funding the expensive cross-region zone-merge design
unit, we need to know: **at a gameplay-representative entity load, does
befallen still miss the 30fps KPI, and what is the zone/entity/GPU split
of the remaining gap?** Note also the tier-2 static baseline (no
entities) already has befallen at 39.5ms p95 — reconcile that with P08's
"zone-only submission is ~2.7ms avg on x86" (the difference is
presumably GPU sync + Pi-side submission at those specific manifest
poses; this unit should state the reconciliation explicitly with
numbers).

## Design

1. **Calibrate N to VISIBLE entities, not placed count.** The benchmark
   places N static entities spread evenly along the whole camera path, so
   placed-N != visible-per-frame. First: determine what per-frame visible
   entity count the CSV already exposes (entity draw/visible columns — if
   none exists, add one; additive CSV column, always-on, like P08's phase
   columns). Then find the placed-N whose visible-per-frame distribution
   on the canonical befallen path best matches gameplay (median ~35-40
   visible in populated stretches; report the distribution you achieved).
   Report the calibration curve (placed N -> visible median/p95) from
   cheap x86 runs.
2. **Animation representativeness**: gameplay entities are animated
   (skeletal); the stress config's entities were static. Check what
   `--animate` does in the benchmark and include ONE animated variant at
   the calibrated N on the Pi (anim_cpu_ms is already a CSV column). If
   `--animate` is broken/nonrepresentative, say so and skip with
   evidence rather than debugging it (out of scope).
3. **Pi measurement (P02 protocol)**, canonical befallen path replay
   (--path-file benchmarks/paths/befallen_seed42_1000f.path), production
   config (manifest productionConfig: count + --player-light-level 9):
   - Variants: calibrated-N static; calibrated-N --animate (if viable);
     entities 0 (zone-only) — reuse P08's committed run for this if the
     binary's CSV schema is unchanged, otherwise re-run;
     (P08's --entities 200 data is the stress reference, reuse).
   - Static pinned poses bf355/472/626/791/917/987 at calibrated N,
     x10 reps first discarded (P07d convention).
   - Optional if invocation budget allows (<=12/session, 2 sessions):
     one qeynos2 canonical replay at calibrated N (does the one
     above-KPI zone stay above KPI at gameplay load?).
4. **Decomposition at calibrated N**: phase columns (P08's) + entity/zone
   submission split via the isolation method (calibrated-N minus
   zone-only, same frames — path replay makes frames comparable across
   variants by construction). Include GPU sync in the story: if the gap
   at calibrated N is GPU-bound rather than submission-bound, that is THE
   finding — say it plainly.
5. **Deliverables**:
   - KPI verdict table: befallen p95/avg fps at entities 0 / calibrated-N
     (static + animated) / 200, whole-run and pinned poses.
   - The zone/entity/GPU split of the frame at calibrated N.
   - The tier-2-baseline reconciliation (design note above).
   - RE-RANKED P08 lever list in light of the data, with a one-paragraph
     recommendation: fund the cross-region merge unit, the occlusion
     budget unit, an entity-path unit, or something the data newly points
     at. No lever work in this unit.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  visual gate PASS with goldens UNTOUCHED; scripts/build-arm-noble.sh;
  scripts/refactor/check_file_sizes.sh fable (no new FAILs; put anything
  substantial in the P08 histogram TU or a new one).
- Replay discipline: pose columns byte-identical to the path file; if any
  CSV schema changed, geometry columns byte-identical old-vs-new binary
  on a zone-only run (prove additive-only).
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O to
  Pi-local /tmp, stripped staged binary from slot1 (ARM_BUILD_BIN_DIR
  trap), drop_caches never reboot, <=12 invocations/session (2 sessions
  OK), leave Pi pristine, report blocked if unreachable.
- CSVs → benchmarks/pi_runs/p08b_entity_reality/ (plain git add — tracked).

## Out of scope

- Building any lever (batching, occlusion budget, entity LOD/composite
  changes) — measurement and ranking only.
- Production code, presets, goldens, paths, lighting.
- Debugging --animate if it's broken (report and skip).
- Live-client sessions (benchmark-only; the live-game follow-ups in
  deferred.md stay separate).

## Review

(executor fills in)
