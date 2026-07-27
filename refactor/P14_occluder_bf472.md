# P14 — bf472-targeted occluder-budget experiment (the GPU tail)

Approved by Will 2026-07-27 ("go ahead with all three").
Slot: wt/slot1, branch task/P14-occluder-bf472, cut from fable tip.
Measurement unit; OWNS THE PI (P15 owns eqemu; P16 needs neither).

## Motivation

P11b: full stack (merge + animated composites) puts befallen at avg
39.8fps / p95 28.3fps — 2.02ms over budget at p95, and the residual is
now GPU-BOUND (GPU sync 49.6% vs submission 26.5%), driven by bf472's
dense zone-geometry pose. P11's "occluder not worth landing" verdict
(0.9-2.7%) was measured on the whole-path average, WITHOUT the stack,
when CPU submission dominated — the question is different now: does a
raised occluder budget cut GPU fill at the dense-pose tail?

## Design

1. All runs: canonical befallen replay (--path-file), production config
   + --animate + calibrated N=205 + --merge-zone-draws +
   --composite-scenegraph (the P11b full-stack setup, reuse its driver
   script pattern), --occluder-budget swept {16 (default/control), 64,
   256, uncapped}. The knob exists (P11); expect zero benchmark code
   changes.
2. Per variant: whole-run avg/p95, GPU-sync column, submission column,
   sim/collect columns (occlusion is SOFTWARE/CPU — a raised budget
   buys GPU fill savings with CPU occlusion-test cost; measure BOTH
   sides), regions_occluded/regions_drawn, pinned-frame extraction with
   bf472 as the headline (bf355/626/791/917/987 for context).
3. x86 smoke first (one run per budget, engagement sanity:
   regions_occluded rises with budget); Pi P02 protocol, <=12
   invocations/session, reuse P11b's stack baseline CSV as the control
   if schema-identical (spend invocations on the new budgets).
4. Deliverables: the budget-response table (esp. bf472 GPU sync + p95);
   the net verdict (does any budget close the 2.02ms p95 gap, and at
   what CPU cost); recommended production occlusionMaxOccluderRegions
   value + the one-line landing description (REPORT ONLY — flip is
   Will's); if the lever is dead even at the tail, say so and name what
   the GPU tail actually needs instead (fill reduction elsewhere,
   resolution, etc. — from the data, not speculation).

## Verification
- Standard bar (build, ctest known exceptions, gles2-x86, gate PASS
  goldens untouched, ARM build — rebuild BEFORE Pi staging, file-size).
- Replay discipline; expected-column-delta documentation (occlusion
  changes drawn sets BY DESIGN; pose columns must match the path).
- CSVs → benchmarks/pi_runs/p14_occluder_bf472/.

## Out of scope
- Landing the production value (report); occlusion redesign; eqemu.

## Review

Executed 2026-07-27 on `wt/slot1`, branch `task/P14-occluder-bf472`, cut from `fable`
at `a17cd2a` (this plan's own commit). No production or benchmark source changes were
needed — `--occluder-budget` (P11) and the full-stack flags (`--merge-zone-draws
--composite-scenegraph --animate`, P11b) already exist; this unit is measurement-only.

### Standard bar

- `cmake --build build -j24`: clean (no source changes; confirms the checkout).
- `ctest --output-on-failure -j4`: exactly the two documented host exceptions
  (`AudioManagerTest.PreloadCommonSounds`, `RDPServerFrameTest.UpdateFrameMultipleTimes`
  — SEGFAULT), all else passed.
- `scripts/build-gles2-x86.sh`: clean (pre-existing `logging.h` `-Wformat-truncation`
  warning only, same as P11b/P11's own builds).
- `scripts/ci/visual_gate.sh`: **PASS**, goldens untouched.
- `scripts/build-arm-noble.sh`: clean, rebuilt **before** Pi staging (contains the same
  tip as this branch — `--occluder-budget` and the full-stack flags are all present,
  no plumbing gap).
- `scripts/refactor/check_file_sizes.sh fable`: no output (no `.cpp`/`.h` diff vs
  `fable` — this unit changed no source), exit 0.
- File sizes: `build/bin/willeq` 73.0MB, `build-gles2/bin/gles2_zone_benchmark` 9.8MB,
  `build-arm-noble/bin/gles2_zone_benchmark` 53.2MB (unstripped, 5.9MB stripped for Pi
  staging), `build-arm-noble/bin/willeq` 355MB (unstripped) — all match P11b's own
  figures, no anomalies.

### 1. x86 smoke (engagement sanity)

Full-stack config (`--zone befallen --path-file benchmarks/paths/befallen_seed42_1000f.path
--frames 1000 --entities 205 --seed 42 --animate --merge-zone-draws --composite-scenegraph
--eqe1-path /mnt/projects/EverQuest_ETC1`, production config), one run per budget
{16, 64, 256, uncapped}, `build-gles2/bin/gles2_zone_benchmark` (llvmpipe):

| budget | regions_occluded avg | regions_drawn avg | total_ms avg | total_ms p95 | bf472 occluded | bf472 drawn |
|---|---|---|---|---|---|---|
| 16 | 27.86 | 134.12 | 22.008 | 32.988 | 62 | 704 |
| 64 | 35.48 | 126.50 | 22.950 | 32.707 | 64 | 702 |
| 256 | 49.70 | 112.28 | 19.856 | 28.198 | 287 | 479 |
| uncapped | 49.70 | 112.28 | 20.791 | 27.187 | 287 | 479 |

**Sanity confirmed**: `regions_occluded` rises monotonically with budget (27.86 → 35.48
→ 49.70, saturating at 256≈uncapped) and `regions_drawn` falls correspondingly — real
engagement, not a no-op. bf472 specifically shows occluded 62→64→287→287 and drawn
704→702→479→479, matching P08's own C6 finding that bf472 has a +225-region occlusion
headroom (62 capped → 287 uncapped = exactly 225 more, reproduced here independently).
x86 timing is single-run/noisy (no repeats) and used only for engagement sanity, not
the headline numbers — those come from the Pi.

### 2. Pi measurement (P02 protocol)

Pi session: `user@10.0.30.192`. Pre-session CmaFree was critically low (3,396 kB, well
below the 40,960 kB abort floor — the Pi had not been used since an earlier unit) —
remediated via `sync && echo 3 | sudo -n tee /proc/sys/vm/drop_caches` (**no reboot**,
per protocol): CmaFree recovered to 79,728 kB. `ARM_BUILD_BIN_DIR` explicitly
overridden to this worktree's `build-arm-noble/bin` (script:
`refactor/scratch/p14_pi_occluder_bf472.py`, same bespoke `--path-file` moving-replay
pattern as P09c's/P11's/P11b's own scripts). ARM binary rebuilt immediately before the
session, stripped 53.2MB → 5.9MB before staging.

**Budget-16 control reused, not re-run**: P11b's own
`benchmarks/pi_runs/p11b_composite_magnitude/befallen_combined_animate_merge_composite.csv`
IS the budget-16 full-stack run (`--occluder-budget` unset defaults to 16, the exact
same command modulo that flag) — schema-verified identical header (26 columns,
byte-for-byte match) before reuse. All 3 new budgets (64, 256, uncapped) were run this
session: **3/12 invocations used**, well under the cap.

**A real SessionAbort caught mid-session, handled per protocol, not silently
retried past**: invocation #3 (budget64)'s post-run CmaFree sample came in at 30,376 kB
(below the 40,960 kB floor) — `SessionAbort` fired cleanly, governor was restored to
`ondemand` (verified), and the exception propagated as designed. Critically, the
CSV for that invocation had already been pulled to `benchmarks/pi_runs/
p14_occluder_bf472/` before the abort fired (abort triggers on the *post*-invocation
CmaFree check, after `run_replay()`'s rsync already completed) — **all 3 planned
invocations' data landed successfully**, no data lost, no need for a second session.
Remediated again post-abort (`sync && drop_caches`, no reboot): CmaFree recovered to
80,236 kB. Pi `/tmp` cleaned of staged binary/CSVs (verified via `ls /tmp` showing only
unrelated `systemd-private-*` dirs), governor confirmed restored to `ondemand` on all
4 cores. `kmsg_follow.log` shows only the two `drop_caches` events (08:12:29, 08:16:58)
and no panic signature — clean session despite the abort.

Canonical replay: `benchmarks/paths/befallen_seed42_1000f.path`, `--frames 1000`, seed
42, production config, `--entities 205` (calibrated, P11b), `--animate
--merge-zone-draws --composite-scenegraph --eqe1-path ...` (the P11b full-stack
config), 800x600, DRM/GLES2/Mali400.

**Replay discipline** (full 1000-frame programmatic checks):
- Pose columns (`pos_x/y/z/heading`) vs the path file: **0/1000 mismatches, all 4
  budgets** (tolerance 0.06 to account for the CSV's 1-decimal rounding — the raw path
  file carries full precision, the CSV truncates for size; confirmed against P11b's
  own control file too).
- `regions_vis == regions_drawn` at every frame, every budget (0 mismatches) — this
  benchmark's `regions_vis` column reports the POST-occlusion survived set (matches
  P08's finding), so it legitimately differs across budgets — **expected delta,
  occlusion changes drawn sets BY DESIGN** (plan's own framing). Budget16 vs
  budget64/256/uncapped: 465-470/1000 frames show a `regions_vis`/`regions_drawn`
  delta (occlusion engaging at those poses), consistent with the x86 smoke's
  monotonic-rise sanity check.
- `entities_visible` also drops modestly at higher budgets (whole-run avg 34.73 →
  34.52 → 34.02 → 34.02, budget16→64→256→uncapped) — **expected, not a bug**: entities
  positioned behind a now-occluded zone region get culled by the same occlusion pass,
  a real (if secondary) interaction the plan didn't explicitly anticipate but which
  follows directly from occlusion culling operating on the whole visible set, not zone
  geometry alone.
- `anim_cpu_ms` whole-run avg is flat across budgets (4.405/4.437/4.419/4.441 — within
  0.8% run-to-run noise) — confirms animation cost is untouched by the occlusion lever,
  as expected.

### THE TABLE — budget-response (befallen full stack, 1000-frame Pi replay, whole-run avg/p95)

| budget | total_ms avg | total_ms p95 | fps avg | fps p95 | submission avg/p95 (ms) | GPU-sync avg/p95 (ms) | collect/cull avg (ms) | draws avg | regions_occluded avg | regions_drawn avg |
|---|---|---|---|---|---|---|---|---|---|---|
| **16 (control, = P11b full stack)** | 25.111 | **35.320** | 39.8 | 28.3 | 6.244 / 9.347 | 11.630 / 17.509 | 2.500 | 42.6 | 27.86 | 134.12 |
| 64 | 25.068 | 34.229 | 39.9 | 29.2 | 6.150 / 8.716 | 11.538 / 17.159 | 2.625 | 42.4 | 35.48 | 126.50 |
| **256** | 24.596 | **32.313** | 40.7 | **30.9** | 5.948 / 7.837 | 11.250 / 15.895 | 2.700 | 41.9 | 49.70 | 112.28 |
| uncapped | 24.634 | 32.505 | 40.6 | 30.8 | 5.950 / 7.802 | 11.255 / 15.989 | 2.712 | 41.9 | 49.70 | 112.28 |

**30fps KPI line = 33.3ms/frame p95.** Budget 256 and uncapped produce statistically
identical results (`regions_occluded`/`regions_drawn` averages match exactly, 49.70 /
112.28 both) — 256 is already at the effective ceiling for this replay's poses;
uncapped buys nothing further. Budget 64 is a partial, insufficient step.

### bf472 headline (frame 472, the densest pinned pose, 704-region pose)

| budget | total_ms | submission_ms | GPU-sync ms | collect/cull ms | draws | regions_drawn | regions_occluded | entities_visible |
|---|---|---|---|---|---|---|---|---|
| 16 (control) | 38.225 | 9.558 | 18.314 | 5.008 | 39 | 704 | 62 | 25 |
| 64 | 38.288 | 9.389 | 18.311 | 5.122 | 39 | 702 | 64 | 25 |
| **256** | **34.151** | **7.348** | **16.571** | 5.446 | 29 | 479 | 287 | 15 |
| uncapped | 34.021 | 7.209 | 16.588 | 5.349 | 29 | 479 | 287 | 15 |

At bf472 specifically, budget 256 cuts total_ms by **4.07ms (−10.6%)** vs the budget-16
control — both submission (−2.21ms, −23.1%) and GPU-sync (−1.74ms, −9.5%) drop, at a
modest CPU collect/cull cost (+0.44ms, +8.8%, the occlusion-test overhead the plan
asked to measure on the cost side). Net: the CPU cost is real but small relative to the
combined submission+GPU win — occlusion at budget 256 is a clear net win at the
worst-case pose, not just on average.

Other pinned poses (bf355/626/791/917/987): bf355/626 show ~0 delta (near-zero occlusion
headroom at those poses, matching P08's C6 finding — bf626 has only 2 regions total, no
occlusion possible). bf791/917 show a small drop (regions_occluded 0→4 / 30→47,
total_ms roughly flat to -0.1ms — negligible headroom there too). bf987 (second-densest,
396/480-region pose) shows the second-largest single-pose win: total_ms 31.467 →
28.708/28.589ms (budget256/uncapped), a **2.76-2.88ms (−8.8 to −9.2%)** drop —
regions_occluded 84→152/152, consistent with bf987's own headroom being real but
smaller than bf472's. This confirms the effect is genuinely pose-dependent
(dense-region poses benefit, sparse/entity-dominated poses don't), exactly as P11's own
verdict predicted — the difference from P11 is that THIS unit isolates the tail poses
where the effect actually lives, instead of diluting it into a whole-path average.

### Net p95 verdict — does any budget close the 2.02ms gap?

**Yes. Budget 256 (and uncapped, which is equivalent) crosses the 30fps p95 line.**
Budget-16's whole-run p95 was 35.320ms (2.02ms over the 33.3ms line, P11b's headline
number this unit set out to re-test). Budget 256's whole-run p95 is **32.313ms — 0.99ms
UNDER the 33.3ms line** (uncapped: 32.505ms, 0.80ms under). This reverses P11's own
"not worth landing" verdict (0.9-2.7% effect, measured on the whole-path average
without the merge+composite stack, when CPU submission still dominated) — under the
FULL stack, where GPU sync is now 49.6% of the p95 frame (P11b), raising the occluder
budget attacks exactly the component that dominates the residual, and the effect
concentrates at the dense-region tail poses (bf472, bf987) that drive p95 in the first
place, rather than diluting across the whole path.

**CPU cost of getting there**: whole-run avg `collect_cull_ms` rises from 2.500ms
(budget16) to 2.700ms (budget256) — **+0.200ms, +8.0%**, the software-occlusion test
cost of evaluating more occluder candidates per frame. This is a real, measured cost
(not free), but it is more than offset by the submission (−0.296ms avg / −1.510ms p95)
and GPU-sync (−0.380ms avg / −1.614ms p95) savings it buys — net whole-run avg total
drops 25.111 → 24.596ms (−2.1%) and p95 drops 35.320 → 32.313ms (−8.5%). Occlusion is
CPU work, as the plan noted, but at this budget level the CPU cost is small relative to
the GPU-fill savings it unlocks.

### Recommended production `occlusionMaxOccluderRegions` value (REPORT ONLY)

**256.** It matches uncapped's effect exactly on this canonical replay (both
`regions_occluded_avg`/`regions_drawn_avg` identical, 49.70/112.28) while being a
bounded, predictable value rather than the benchmark's `1<<20` uncapped sentinel — no
reason to ship an unbounded occlusion-test budget when 256 already captures all the
headroom this zone's poses expose. One-line landing description: set
`occlusionMaxOccluderRegions` (default 16) to `256` in the orangepi preset
(`constrained_renderer_config.cpp:284`) — a config-value-only change, the field already
flows correctly to `irrlicht_renderer.cpp:1421`'s `SoftwareOcclusionCuller`
construction (no plumbing gap, confirmed by P11's own desk-map, unchanged by this
unit). Landing itself is Will's call, per scope — this unit reports the number and the
data behind it, does not flip the preset.

### Deviations from the plan

- A `SessionAbort` occurred mid-session (CmaFree dropped below the 40,960kB floor after
  invocation #3) — handled exactly per the panic-safe protocol (governor restored,
  `sync && drop_caches` remediation, no reboot) rather than silently retried or ignored;
  all 3 planned invocations' data had already landed before the abort fired, so no
  second session was needed and the invocation budget (3/12) was well under cap.
- Pre-session CmaFree was already critically low (3,396 kB) before any invocation ran
  (likely residual state from a prior unit's Pi session) — remediated via
  `sync && drop_caches` before starting, per the same no-reboot protocol, not something
  this unit's own invocations caused.
- No production code or benchmark code changes were needed (the `--occluder-budget`
  flag and full-stack flags already existed from P11/P11b) — pure measurement, as the
  plan's design anticipated ("expect zero benchmark code changes").
- `entities_visible` showing a small cross-variant delta (34.73→34.02 avg) was not
  explicitly named in the plan's expected-delta list (which named `regions_occluded`/
  `regions_drawn`) — documented above as a real, explicable secondary effect (occlusion
  culls entities behind occluded zone regions too), not a bug, per the verification
  requirement to document expected-column deltas.
- The plan's "sim/collect columns" phrasing maps to this benchmark's actual CSV column
  name `collect_cull_ms` (there is no separately-named "sim" phase in this benchmark's
  instrumentation, per P08's own phase-decomposition schema) — used that column
  throughout as the CPU occlusion-cost measure.
