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

### Setup

Branch `task/P08b-entity-reality`, cut from `fable` (e4d3ada, one commit past
the plan's 597deab — only the plan file itself in between). Instrumentation
change: `collectEntityDrawEntries()` (`tools/hwcaps/zone_bench_loader.cpp/.h`)
gained an `outEntitiesVisible` out-param counting DISTINCT entities that
survive distance/frustum/occlusion cull (vs. `outEntityDrawCalls`, which
counts mesh-buffer draw entries — one entity can have several body-part
buffers). Threaded into a new always-present trailing CSV column
`entities_visible` (`zone_bench_metrics.h/.cpp`, additive/backward-compatible
append, same convention as P07d's/P08's own trailing columns) — 0 for
zone-only runs and for the batched/composite/LOD/direct-GL entity modes
(only the default/unbatched entity path populates it; that path is the P08/
P08b canonical config). `zone_bench_loader.cpp` line count: 2999 (was 2996;
trimmed comments to stay under the 3000 FAIL threshold — see Verification).

All runs replay the canonical befallen path (`benchmarks/paths/
befallen_seed42_1000f.path`, `--path-file`, `--seed 42 --frames 1000`) under
the production config (manifest `productionConfig`: `--adaptive-lighting
--zone-pp-budget 20 --pp-budget-mode count --player-light-level 9`), 800x600.
x86 = llvmpipe via `build-gles2/bin/gles2_zone_benchmark`; Pi = Orange Pi One
(Mali400/Lima) via a stripped binary built from this worktree's
`build-arm-noble/`, P02 protocol (`refactor/scratch/p08b_pi_session.py`, ad
hoc script reusing `scripts/pi/pi_common.py` primitives per the P05/P06/P08
precedent, deleted after the session). One Pi session, 5 invocations (well
under the 12 cap), no `SessionAbort`. CmaFree started critically low (1008
kB, likely residual fragmentation from a prior session) — remediated with
`sync && echo 3 > /proc/sys/vm/drop_caches` (never rebooted, per the rules)
before the session proper, recovering to 78792 kB; stayed in
[60200, 78792] kB for the rest of the session, comfortably above the 40 MB
abort floor. Governor restored to `ondemand`, staged binary/path files/
scratch CSVs removed from Pi `/tmp` after — Pi left pristine (verified: only
unrelated systemd-private dirs remain). Data:
`benchmarks/pi_runs/p08b_entity_reality/`.

### 1. Calibration: placed-N -> visible-per-frame

Cheap x86 sweeps (`--entities N --path-file befallen_seed42_1000f.path`,
1000 frames), `entities_visible` column distribution:

| placed N | median visible | p95 visible | max visible |
|---|---|---|---|
| 40 | 7 | 8 | 8 |
| 80 | 15 | 16 | 16 |
| 120 | 22 | 24 | 24 |
| 160 | 30 | 31 | 31 |
| 200 | 37 | 39 | 39 |
| **205** | **38** | **40** | **40** |
| 210 | 39 | 40/41 | 41 |

Visible/placed ratio is remarkably linear (~0.185) across the whole sweep —
no saturation effects from render-distance/frustum/occlusion cull kicking in
disproportionately at higher N on this path. **Calibrated N = 205**, giving
a median of 38 / p95 of 40 visible entities per frame — squarely inside the
plan's 35-40 target band, and matching the H04 live-observation figure (36-40
visible of 148 spawned, qeynos2) closely.

**Headline finding buried in the calibration curve itself**: P08's own
`--entities 200` "stress" config, which P08's motivation explicitly framed as
NOT gameplay-representative, turns out to already sit almost exactly at the
gameplay-representative visible count (median 37 / p95 39 vs. calibrated
205's 38/40) — a 2.5% placed-N difference. P08's "stress" config was, by
this unit's visible-count metric, already close to real gameplay density.
This matters for how much of P08's entity-dominance finding (86.9% of
combined submission time, x86) carries over to true gameplay load — see the
KPI verdict and split below: it carries over almost entirely.

### 2. Animation representativeness — `--animate` is nonrepresentative, SKIPPED per plan

`--animate` was checked (x86, N=205, 100 frames) rather than debugged, per
the plan's explicit escape hatch. Evidence it is broken for this purpose:
at IDENTICAL frames/poses on the same path, `--animate` reports far more
visible entities and draws than the static run at the same N:

| frame | pos | static entities_visible / draws | animate entities_visible / draws |
|---|---|---|---|
| 2 | (-238.3,0.2) | 38 / 524 | 193 / 2562 |
| 5 | (-240.0,10.0) | 40 / 798 | 192 / 2798 |
| 10 | (-243.5,26.3) | 38 / 686 | 183 / 2598 |
| 19 | (-252.2,55.0) | 40 / 691 | 165 / 2337 |

A consistent ~4.5-5x more visible entities under `--animate` at identical
camera pose/entity placement/render-distance settings — entity culling
(distance/frustum/occlusion, all position-based, unaffected by animation
state) should be identical between the two modes but isn't. `anim_cpu_ms`
itself is populated and non-zero (confirming animation IS running), so this
is specifically a visibility/culling interaction bug in the animate path,
not a total no-op. Root-causing this is out of scope per the plan
("if --animate is broken/nonrepresentative: evidence + skip, don't debug
it") — **skipped**. No Pi invocation spent on it. Evidence:
`benchmarks/pi_runs/p08b_entity_reality/befallen_x86_e205_animate_smoke.csv`.

### 3. Pi measurement — a benchmark limitation found along the way

Design point 3 asked for "static pinned poses at calibrated N, x10 reps
first discarded (P07d convention)" via `--camera-pose`. Running this
(`befallen_pi_e0_pinned.csv` / `befallen_pi_e205_pinned.csv`) surfaced a
pre-existing benchmark architecture fact, not something this unit changed:
**the static multi-pose capture path (`--camera-pose`) never calls
`collectEntityDrawEntries()`** (`gles2_zone_benchmark.cpp:2134-2178` calls
only `collectDrawEntries`/`collectObjectDrawEntries`) — entities are placed
along a camera PATH (`placeEntitiesAlongPath`) and are architecturally
absent from static single-pose captures regardless of `--entities`. Proof:
the "pinned" e0 and e205 CSVs are byte-identical in draws/state_changes
(92/6/165/130/763/1297 at bf355/626/791/917/987/472 respectively — exactly
matching P08's zone-only histogram numbers) — `--entities 205` had zero
effect on the static-pose run.

**Deviation from the plan**: the pinned-pose deliverable instead uses
frame-indexed extraction from the whole-run moving-path replay CSVs
(row `frame==355`, etc.) — exactly the method P08 itself used for its own
"Per-pose phase decomposition" table (P08 review section B), so this is
consistent with precedent, not a new invention. The `--camera-pose` x10-rep
runs are kept in the committed data as a useful-but-narrower artifact: they
confirm zone-only geometry is deterministic and byte-identical to P08's
histogram at those exact poses, but they are NOT entity-inclusive and are
not used for the entity-related tables below. Fixing the static-pose path to
render entities would be a real (if small) benchmark code change beyond an
"additive CSV column," judged out of proportion for this SHORT unit — noted
for a future harness-hygiene unit if pinned-pose entity data is ever needed
again.

Variants actually run/reused:
- **entities=0** (zone-only): x86 + Pi, fresh whole-run replay (P08's Pi
  run didn't include an entities=0 variant, so no reuse was possible there;
  x86 zone-only was independently re-run and cross-checked 0-diff against
  P08's committed `befallen_x86_replay_noentities.csv`, see Verification).
- **entities=205** (calibrated N, static): x86 + Pi, fresh whole-run replay.
- **entities=200** (P08's stress reference): REUSED as-is, no re-run —
  `benchmarks/pi_runs/p08_draw_calls/befallen_{x86,pi}_replay.csv`.
- **qeynos2, entities=205** (optional, budget allowed it): one Pi whole-run
  replay of `qeynos2_seed42_1000f.path` at the same calibrated N.

Replay-discipline checks (full-file programmatic, not spot checks):
- Pose columns vs. the path file: **0/1000 mismatches** (x86 and Pi, both
  entities=0 and entities=205 runs).
- Geometry columns (draws/state_changes/polys/regions_vis/regions_drawn/
  regions_occluded) x86 vs. Pi, same config: **0 diffs** across all 1000
  frames, both entities=0 and entities=205 — including the new
  `entities_visible` column, also 0 diffs cross-machine.
- Additive-only proof (CSV schema changed — new trailing column): a
  from-scratch uninstrumented build at 597deab (the plan's stated cut point,
  via a temporary `git worktree add`, removed after) run zone-only on the
  same path: **0 diffs** on all pre-existing geometry columns vs. this
  branch's instrumented binary, all 1000 frames.

### KPI verdict table (whole-run, 1000 frames, Pi Mali400/Lima — the KPI-relevant machine)

| variant | avg total_ms | p95 total_ms | fps (p95) | fps (avg) | max total_ms |
|---|---|---|---|---|---|
| entities=0 (zone-only) | 36.728 | 174.257 | 5.74 | 27.23 | 313.790 |
| **entities=205 (calibrated, gameplay-representative)** | **104.532** | **279.673** | **3.58** | **9.57** | 456.436 |
| entities=200 (P08 stress, reused) | 102.799 | 285.552 | 3.50 | 9.73 | 452.148 |
| entities=205 --animate | SKIPPED (nonrepresentative, see above) | | | | |

**Verdict: NO, befallen is not close to the 30fps KPI at gameplay-representative
entity load — not even close.** p95 fps at calibrated N is 3.58 (279.7ms),
worse than 8x under budget. The calibrated-N (205) and stress-config (200)
numbers are nearly identical (p95 279.7 vs 285.6ms, avg 104.5 vs 102.8ms) —
confirming the calibration-curve finding above: P08's "stress" config was
already gameplay-representative by visible-entity count, so its KPI verdict
was never actually pessimistic relative to real gameplay. **This is NOT
purely an entity problem, though**: even zone-only (entities=0) already
misses the KPI by a wide margin on the moving path (p95 174.3ms = 5.74 fps),
which is 4.4x worse than the tier-2 static baseline's 39.5ms p95 (25.3 fps,
also entities=0) — see the tier-2 reconciliation below for why.

**Per-pose steady-state (frame-indexed from the whole-run replay, the
P08-precedent substitute for the pinned-pose deliverable — see deviation
above), Pi:**

| pose | e0 total_ms (fps) | e205 total_ms (fps) | e200 total_ms (fps) | e205 draws | e205 entities_visible |
|---|---|---|---|---|---|
| bf355 | 11.195 (89.3) | 61.880 (16.2) | 69.059 (14.5) | 567 | 36 |
| bf472 | 280.062 (3.6) | 383.778 (2.6) | 333.453 (3.0) | 1627 | 25 |
| bf626 | 4.622 (216.4) | 50.922 (19.6) | 51.495 (19.4) | 532 | 40 |
| bf791 | 19.200 (52.1) | 77.241 (13.0) | 98.058 (10.2) | 663 | 38 |
| bf917 | 13.607 (73.5) | 86.454 (11.6) | 76.735 (13.0) | 615 | 37 |
| bf987 | 121.134 (8.3) | 178.063 (5.6) | 191.288 (5.2) | 1038 | 21 |

Every pinned pose misses 30fps at calibrated N — even bf355 (16.2fps) and
bf626 (19.6fps), the two lightest zone-geometry poses from P08's own table,
now miss KPI once entities are included at gameplay density (they didn't
appear in P08's headline table as entity-dominated, but they clearly are
once measured at realistic entity load rather than only the stress config
context P08 pulled them from). bf472 (704 zone regions, the densest pose)
is catastrophic at both N: ~2.6-3.6 fps regardless of entity count — this
one is a zone-geometry problem first (compare to its e0 number, 3.6fps,
already far below KPI with zero entities).

### Zone/entity/GPU split at calibrated N (isolation method: e205 minus e0, same frames)

Whole-run averages, Pi:

| | draws | submission_ms | collect_cull_ms | gpu_ms | total_ms |
|---|---|---|---|---|---|
| e0 (zone-only) | 253.0 | 21.730 | 2.257 | 12.471 | 36.728 |
| e205 (combined, calibrated N) | 710.1 | 67.398 | 2.815 | 33.905 | 104.532 |
| **entity contribution** | **457.2 (64.4% of combined)** | **45.668 (67.8% of combined)** | — | — | — |

**GPU-sync check (design point 4): the gap at calibrated N is still
submission-bound, NOT GPU-bound** — submission is 64.5% of total frame time
at e205 (67.398/104.532), GPU sync 32.4% (33.905/104.532), collect/cull a
minor 2.7%. Same shape as P08's own whole-run split (submission 63.8%, GPU
33.0% at e200) — the STOP condition does not trigger; this confirms P08's
architecture-level finding (CPU submission dominates) is still true at real
gameplay entity load, not an artifact of the stress config.

**A machine-dependent nuance P08 (x86-heavy in its per-pose tables) didn't
surface**: on x86, entities are 86.9%/87.5% of combined submission time
(P08's number / this unit's e205 re-confirmation: (22.045-2.759)/22.045 =
87.5%) — but on the Pi, the actual KPI-relevant hardware, entities are only
**67.8%** of combined submission time at calibrated N. Zone-geometry
submission itself is NOT negligible on the Pi (21.730ms zone-only vs.
2.759ms on x86 for the identical scene — the Mali400/Lima per-draw driver
overhead the P08 synthetic sweep quantified hits zone draws too, they're
just outnumbered ~2.4:1 by entity draws in the combined frame). **The
zone-geometry lever (P08's #1, cross-region merge) has real headroom on the
actual target hardware even after accounting for gameplay-representative
entity load** — it is not rendered moot by this unit's findings, contrary
to what the x86-only submission split might suggest.

### Tier-2 baseline reconciliation

Motivation's question: tier-2 static baseline has befallen at 39.5ms p95
(no entities) — reconcile with P08's "zone-only submission is ~2.7ms avg on
x86" and this unit's zone-only whole-run p95 (174.3ms Pi / 17.8ms x86, both
far worse than 39.5ms).

**Reconciliation**: the tier-2 baseline (`benchmarks/pi_baseline/`,
Q01-recaptured, befallen rgba 39.593ms p95) samples only **3 fixed
designated camera poses** (`tests/goldens/manifest.json`'s befallen
`extraArgs`, cycled 60 times), all picked during golden curation for visual
representativeness, not worst-case density. This unit's zone-only whole-run
replay traverses the ENTIRE 1000-frame canonical path, including the tail
poses P08 pinned specifically because they're the worst 5% (bf472 alone,
704 visible regions, drags the whole-run p95 up on its own: its own e0
total_ms is 280.062ms, ~1600x the average of an easy pose). The 39.5ms
number is not wrong — it's an honest average over 3 chosen views — but it
was never representative of the WORST poses a real play session traverses,
and this unit's whole-run p95 is the more honest "how bad does it get"
number. Numerically: zone-only submission at an EASY pose (e.g. bf355:
4.463ms submission, Pi) is close in spirit to the tier-2 baseline's
per-frame cost; zone-only submission at the WORST pose (bf472: 184.560ms
submission, Pi) is ~40x worse — the whole-run p95 (174.3ms) sits in between
because p95 captures the tail-heavy distribution, not just the worst single
frame. **This is a distinct, additional finding beyond the motivation's
"presumably GPU sync + Pi-side submission" hypothesis: the gap is
overwhelmingly explained by WHICH poses are sampled (3 curated views vs. the
full worst-case-inclusive path), not by GPU sync specifically** — GPU sync
is a real but secondary contributor (12.471ms avg / 88.964ms at bf472,
Pi) alongside submission (21.730ms avg / 184.560ms at bf472).

### Optional: qeynos2 at calibrated N

qeynos2 was the one zone ABOVE KPI in the tier-2 baseline (32.4/36.6 fps
p95, zone-only, static 3-pose sampling). At calibrated N=205 (calibrated
against befallen's path/entity-placement, not independently recalibrated for
qeynos2 — its own visible-entity distribution came out lower: median 28,
p95 69, on its own path), whole-run replay on the Pi: **avg 61.749ms, p95
143.759ms (6.96 fps)** — qeynos2 does NOT stay above KPI at gameplay
entity load. As with befallen, this conflates two effects (entities, and
moving-path worst-case coverage vs. 3 curated static views) that this
optional single run can't cleanly separate — reported as directional
evidence (entities matter zone-wide, not just in befallen) rather than a
qeynos2-specific isolation result; a proper qeynos2 zone-only whole-run
replay would be needed to split the two effects, out of scope for this
SHORT unit's budget.

### Re-ranked P08 lever list

P08's original ranking (zone-geometry-only investigation, x86-heavy
per-pose analysis):
1. Cross-region same-material draw merge (zone-only, 97.8% draw reduction
   ceiling) — recommended next unit.
2. Occlusion culling budget increase — pose-dependent headroom.
3. Entity draw-path reduction — flagged in passing, not primary (P08 was
   explicitly zone-geometry-scoped).
4. Within-region merge — dead, ~0% ceiling.

**Re-ranked, in light of this unit's gameplay-load data:**

1. **Entity draw-path reduction — PROMOTED to #1.** At calibrated
   gameplay-representative entity load (not just P08's stress config, which
   turned out to be nearly identical anyway), entities are 64.4% of combined
   draws and 67.8% of combined submission time on the Pi (86.9%/87.5% on
   x86) — the single largest cost component on the actual target hardware,
   confirmed under real gameplay density, not a stress-config artifact. The
   benchmark ALREADY has four entity-reduction variants built and measurable
   (`--batch-entities`, `--composite-entities`, `--lod-entities`,
   `--direct-gl`, from P05-era work) — meaning a follow-up unit can measure
   real levers immediately instead of designing new infrastructure from
   scratch, unlike lever #2 below. Recommended as the next unit.
2. **Cross-region same-material zone-geometry draw merge — still real, now
   #2.** P08's 97.8% ceiling is unaffected by this unit's findings, and this
   unit adds a materially important correction: zone-only submission is
   FAR from negligible on the Pi (21.7ms avg, ~2.4x more per-draw-expensive
   relatively than on x86, per P08's own driver-overhead sweep) — the lever
   is still worth funding, just not the only or even necessarily the larger
   win once entities are counted honestly. Needs a proper design unit (new
   shared-VBO layout, per P08's C5 interaction list) — this doesn't change.
3. **Occlusion culling budget increase — unchanged, #3.** Pose-dependent
   headroom (up to +225 regions at bf472), cheap/low-risk relative to #1/#2.
   A natural interim/companion unit regardless of which of #1/#2 gets funded
   first.
4. **Within-region merge — still dead, #4.** No new evidence changes this.

**Recommendation**: fund an **entity draw-path reduction unit** next, ahead
of the cross-region zone-geometry merge P08 recommended. The rationale is
threefold: (a) this unit's gameplay-load calibration shows entities are the
single largest submission-time contributor on the Pi even at realistic
density, not an artifact of P08's stress config; (b) the benchmark already
has four candidate entity-reduction techniques implemented and ready to
measure (`--batch-entities`/`--composite-entities`/`--lod-entities`/
`--direct-gl`), so the next unit is a measurement-and-select exercise, not a
from-scratch design like the zone-geometry merge; (c) it doesn't foreclose
the zone-geometry merge — that lever's ceiling and interaction list (P08 C5)
are untouched by this unit and should still be funded as a following (or
parallel, different-file-set) unit, since zone submission remains a real,
non-negligible cost on the actual target hardware once measured honestly
(21.7ms avg, not the ~2.7ms this unit's own x86-only initial read might
have suggested). The occlusion-budget unit remains a cheap interim
candidate for whichever of the two bigger units lands first.

### Verification

- `cmake --build build -j24`: clean.
- `ctest --test-dir build --output-on-failure`: known host exceptions only
  (`AudioManagerTest.PreloadCommonSounds`,
  `RDPServerFrameTest.UpdateFrameMultipleTimes`); `visual_gate` PASSED.
- `scripts/build-gles2-x86.sh`: clean.
- Visual gate (`scripts/ci/visual_gate.py`, no `--bless`): PASS, goldens
  untouched (this unit adds one CSV column and threads one new function
  out-param only; no rendering-path or default changes).
- `scripts/build-arm-noble.sh`: clean, final binary used for the Pi session.
- `scripts/refactor/check_file_sizes.sh fable`: exit 0.
  `tools/hwcaps/zone_bench_loader.cpp` 2999 lines (WARN, pre-existing
  category — right at the edge of the 3000 FAIL threshold; comments were
  trimmed during this unit specifically to stay under it, see Setup).
  `tools/hwcaps/gles2_zone_benchmark.cpp` unchanged line-count-wise (2 lines
  added at the one call site) — still its pre-existing WARN.
- Replay discipline: pose columns 0/1000 mismatches (x86 and Pi, both
  entities=0/205 runs); geometry columns 0 diffs x86-vs-Pi (all 1000 frames,
  both variants, `entities_visible` included); additive-only proof (0 diffs
  vs. an uninstrumented from-scratch build at the plan's 597deab cut point,
  zone-only run, all 1000 frames, all pre-existing geometry columns).
- Pi rules: P02 protocol via `scripts/pi/pi_common.py` primitives, all timed
  I/O to Pi-local `/tmp`, stripped staged binary from this worktree's
  `build-arm-noble/bin` (`ARM_BUILD_BIN_DIR` explicitly overridden), one
  `drop_caches` remediation before the session (CmaFree started at 1008 kB,
  recovered to 78792 kB; never rebooted), 5 invocations in 1 session (well
  under 12), no `SessionAbort`, governor restored to `ondemand`, Pi left
  pristine (staged binary/path files/scratch CSVs removed, verified via a
  post-session `ls /tmp`).

### Deviations from the plan

- **Pinned-pose static runs (design point 3)**: `--camera-pose` static
  captures do not render entities at all (pre-existing benchmark
  architecture — entities are placed along a camera PATH, and the
  static-multi-pose code path never calls the entity collection function).
  Substituted frame-indexed extraction from the whole-run replay CSVs at the
  same six frame indices — the same method P08 itself used for its own
  per-pose table, so this is precedent-consistent, not a new approach. The
  `--camera-pose` x10-rep runs were still executed and committed (useful as
  a zone-only-geometry determinism cross-check against P08's histogram
  data) but are not the source of any entity-related number in this review.
- **`--animate` not debugged, per the plan's own explicit escape hatch**:
  evidence gathered (4.5-5x visible-entity discrepancy vs. static at
  identical frames) and reported; no Pi invocation spent on it.
- **qeynos2 optional run (design point 3) answers a conflated question**:
  the single replay at calibrated N (calibrated for befallen, not
  independently recalibrated for qeynos2) can't cleanly separate "entities"
  from "moving-path worst-case coverage vs. 3 curated static tier-2 views"
  — reported as directional evidence, not a clean isolation result, per the
  plan's SHORT-unit budget (a proper qeynos2 zone-only whole-run replay
  would be needed to split the two effects; out of scope here).
- No production code, presets, goldens, paths, or lighting were touched.
  No lever was built (per the plan's explicit out-of-scope).
