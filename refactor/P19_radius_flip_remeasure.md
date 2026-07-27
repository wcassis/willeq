# P19 — Radius-mode lighting: re-measure under the final stack for the flip decision

Pre-authorized batch (Will, 2026-07-27). Slot: wt/slot3, branch
task/P19-radius-remeasure. OWNS THE PI. Protocol:
refactor/EXECUTOR_PROTOCOL.md. Parallel: P17 (slot1, eqemu), P18
(slot2, animation files). Benchmark RUNS only — no benchmark code
edits expected (flags exist since P06/P07d).

## Motivation

The lighting thread (docs/decisions.md 2026-07-24 + 2026-07-25
addendum): count mode stays default ON TIMING ALONE — radius mode is
provably lossless (pixel-identical where it demotes) but measured
1.1-17% SLOWER (Q03 era). Everything has changed since: the GLS
fixture (Q03), the merge (P10), real animated composites (P11b/P12b),
occluder 256 (P14). Radius's cost was per-pixel-lit REGION count;
under the stack the frame composition is different. Will independently
wants radius-style lighting ("efficient light calc in a radius around
the player"). Re-measure honestly; if radius is now within noise of
count, it becomes the flip candidate (quality strictly better).

## Design

1. Pi, P02, canonical replays (befallen AND qeynos2), FULL current
   stack (merge + composite-scenegraph + --animate + occluder 256 +
   production config), count vs radius (--pp-budget-mode radius),
   calibrated N=205. Whole-run avg/p95 + pinned-frame extraction +
   demotion stats (light_radius/regions_demoted columns).
2. x86 pixel spot: radius vs full-PP still pixel-identical at 2-3
   poses under the CURRENT fixture (regression check on the Q03
   losslessness result).
3. Deliverable: the count-vs-radius table under the stack; flip
   verdict (REPORT ONLY — decisions.md says the flip is its own unit
   with golden/tier-2 re-bless if it goes ahead); note qeynos2 (above
   KPI) headroom vs befallen (at KPI) margins.

## Review

Executed 2026-07-27 on `wt/slot3`, branch `task/P19-radius-remeasure`, cut from `fable`
at `f05e804`. No production or benchmark source changes were needed — `--pp-budget-mode`
(P07d), `--merge-zone-draws`/`--composite-scenegraph`/`--animate` (P09/P11b),
`--occluder-budget` (P11, now defaulted to 256 in production per P14) all already exist;
this unit is measurement-only. Two ad hoc driver scripts (not committed as shared assets,
per the P07d/Q01/Q03/P14 precedent): `refactor/scratch/p19_pi_radius_remeasure.py` (+
`_session2.py` for the post-abort remainder) and `refactor/scratch/p19_x86_pixel_spot.py`.

### Standard bar

- `cmake --build build -j24`: clean.
- `ctest --test-dir build --output-on-failure -j4`: 1234/1235 passed; the one failure is
  the documented host exception `AudioManagerTest.PreloadCommonSounds`
  (`RDPServerFrameTest.UpdateFrameMultipleTimes`, the other documented exception, passed
  this run). `visual_gate` (test #1235) **PASSED**.
- `scripts/build-gles2-x86.sh`: clean.
- `scripts/ci/visual_gate.py` (no `--bless`): **PASS**, goldens untouched — no golden
  changes expected or made, per the plan's report-only scope.
- `scripts/build-arm-noble.sh`: clean, rebuilt and staged for the Pi session.
- `scripts/refactor/check_file_sizes.sh fable`: exit 0, no output (no `.cpp`/`.h` diff vs
  `fable` — this unit changed no source).

### Config used (single source of truth)

Production config (`tests/goldens/manifest.json` `productionConfig.args`):
`--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count --player-light-level 9`
(GLS level 9, R=336.378, per the Q03 amendment). Full final stack (added on top, per the
plan): `--animate --merge-zone-draws --composite-scenegraph --occluder-budget 256`
(256 is now also production's shipped default per P14 — passed explicitly here for
reproducibility/clarity, not because it changes behavior vs the unadorned default).
Calibrated `--entities 205 --seed 42` (P11b calibration). Variant flip: `--pp-budget-mode
count` (control) vs `--pp-budget-mode radius` (last-flag-wins over the production
default). Canonical replays: `benchmarks/paths/{befallen,qeynos2}_seed42_1000f.path` via
`--path-file`, `--frames 1000`, 800x600, DRM/GLES2/Mali400.

### Pi measurement (P02 protocol)

Pi (`user@10.0.30.192`) reachable throughout. Pre-session CmaFree 81,984 kB (healthy,
above the 40,960 kB floor). `ARM_BUILD_BIN_DIR` explicitly overridden to this worktree's
`build-arm-noble/bin`. 4 planned invocations (befallen×{count,radius},
qeynos2×{count,radius}), randomized order `[1, 3, 2, 0]` (seed 19).

**Session 1**: 3/4 invocations completed (befallen_radius, qeynos2_radius, qeynos2_count)
before a real `SessionAbort` fired cleanly at invocation #3's post-run CmaFree check
(18,292 kB, below the 40,960 kB floor) — governor was restored to `ondemand`
automatically, and all 3 completed invocations' CSVs had already landed
(`benchmarks/pi_runs/p19_radius_flip_remeasure/{befallen_radius,qeynos2_radius,
qeynos2_count}.csv`) before the abort. Remediated per protocol: `sync && echo 3 |
sudo -n tee /proc/sys/vm/drop_caches` (**no reboot**) — CmaFree recovered to 80,416 kB.

**Session 2**: the single remaining invocation (`befallen_count`) via
`p19_pi_radius_remeasure_session2.py` (reuses session 1's exact `run_replay`/`summarize`
helpers by importing the first script as a module). CmaFree pre 80,536 kB, post
61,908 kB — no abort, clean completion.

**Total: 4 invocations across 2 sessions** (well under the ≤12/session, 2-sessions-OK
budget). Post-session cleanup: staged binary + scratch CSVs/logs/path files removed from
Pi `/tmp` manually (this ad hoc script, like its P07d/Q03/P14 predecessors, has no
built-in cleanup step); `sync && drop_caches` remediation run once more after cleanup —
CmaFree recovered to 82,116 kB. `ls /tmp` post-cleanup shows only unrelated
`systemd-private-*` dirs. Governor confirmed `ondemand` on all 4 cores.
`/mnt/projects/fable/pi_console/kmsg_follow.log` shows only the two `drop_caches` events
from this session's remediations (12:38:44 and 12:40:46) and no panic signature —
**clean, pristine exit**.

**Replay discipline** (full 1000-frame programmatic checks, both zones):
- Pose columns (`pos_x/y/z/heading`) vs the path file: **0/1000 mismatches**, all 4
  runs (tolerance 0.06 for the CSV's 1-decimal rounding, same convention as P14).
- `regions_vis`/`regions_drawn`/`polys`: **0 mismatches** count-vs-radius, both zones —
  geometry/visibility decisions are unaffected by the per-pixel-lighting mode, as
  expected (PP mode only decides which already-visible regions get per-pixel vs
  per-vertex/demoted shading).
- `draws`: **564/1000 (befallen) and 665/1000 (qeynos2) frames differ** count-vs-radius —
  **expected delta**: which regions get per-pixel vs per-vertex shading changes how
  `--merge-zone-draws`/`--composite-scenegraph` batch draw calls, so the draw count
  legitimately differs by mode even though the drawn geometry set does not.
- `light_radius`: constant `336.378` in every radius-mode row (both zones, matches the
  Q03 GLS-level-9 formula exactly); `0.000` in every count-mode row (the R solve is only
  invoked in radius mode — expected, not a bug).

### THE TABLE — whole-run avg/p95, both zones, both modes (Pi, 1000-frame canonical replay)

| zone | mode | avg total_ms | p95 total_ms | fps avg | fps p95 | regions_vis avg | regions_demoted_radius avg | demoted-nonzero frames |
|---|---|---|---|---|---|---|---|---|
| befallen | count | 24.491 | 32.389 | 40.8 | 30.9 | 112.28 | 0.000 (n/a) | 0 (n/a) |
| befallen | radius | 25.296 | 34.381 | 39.5 | 29.1 | 112.28 | 3.967 | 443/1000 (44.3%) |
| qeynos2 | count | 26.505 | 41.556 | 37.7 | 24.1 | 31.18 | 0.000 (n/a) | 0 (n/a) |
| qeynos2 | radius | 26.857 | 42.293 | 37.2 | 23.6 | 31.18 | 0.605 | 266/1000 (26.6%) |

**30fps KPI line = 33.3ms/frame p95.** Befallen (the at-KPI zone) sits just under the
line in both modes (count 32.389ms, radius 34.381ms — radius alone crosses it by
1.08ms). qeynos2 (the above-KPI zone, city/outdoor, denser overall path) is well over the
line in both modes (count 41.556ms, radius 42.293ms) — consistent with prior units'
framing of qeynos2 as the zone with real headroom pressure regardless of this lever.

**Radius vs count, whole-run**: befallen **+3.3% avg / +6.2% p95** slower; qeynos2
**+1.3% avg / +1.8% p95** slower. Radius is slower in both zones, at both avg and p95,
under the full final stack — same direction as every prior measurement (P07d, Q03), but
notably **smaller magnitude at qeynos2** than Q03's own qeynos2 figure (Q03: avg +1.1%
under the OLD stack without merge/composite/occluder-256; this unit: qeynos2 avg +1.3%,
essentially flat vs Q03 despite the intervening stack changes) and a **larger p95 gap at
befallen** (befallen wasn't measured on the moving path at all in Q03/P07d — those units'
befallen data was static-pose only).

### Pinned-frame extraction + demotion stats

**Befallen** (established P08/P11b/P14 pinned set: bf355/472/626/791/917/987):

| frame | regions_vis | count total_ms | radius total_ms | delta% | regions_demoted_radius |
|---|---|---|---|---|---|
| 355 | 60 | 20.997 | 21.237 | +1.1% | 6 |
| 472 | 479 | 34.195 | 34.996 | +2.3% | 0 |
| 626 | 2 | 21.021 | 20.897 | -0.6% | 0 |
| 791 | 96 | 25.508 | 28.246 | +10.7% | 0 |
| 917 | 72 | 24.012 | 24.303 | +1.2% | 6 |
| 987 | 328 | 28.633 | 33.632 | **+17.5%** | 14 |

**Qeynos2** (no established pinned set from prior units — derived here post-hoc from the
count-mode run: densest steady-state `regions_vis` frames plus 2 evenly spaced; frame 780
was excluded as a one-frame load-hitch outlier, noted below):

| frame | regions_vis | count total_ms | radius total_ms | delta% | regions_demoted_radius |
|---|---|---|---|---|---|
| 93 | 88 | 33.114 | 35.976 | +8.6% | 1 |
| 250 | 23 | 32.155 | 32.168 | +0.0% | 0 |
| 750 | 29 | 11.984 | 11.892 | -0.8% | 0 |
| 885 | 89 | 35.237 | 37.140 | +5.4% | 1 |

**Load-hitch note**: frame 780 (`regions_vis`=102, the single densest frame in the whole
qeynos2 replay) shows `count`=129.649ms / `radius`=110.082ms — both wildly elevated vs.
its neighbors (frames 770-794 all sit in the normal 13-41ms band). This is a one-frame
streaming/load hitch (new geometry entering view for the first time), not a genuine
steady dense-pose cost, and was deliberately excluded from the pinned set above so as not
to report a hitch artifact as a lighting-mode timing signal. Flagged here rather than
silently dropped, per the evidence-discipline rule — `benchmarks/pi_runs/
p19_radius_flip_remeasure/qeynos2_{count,radius}.csv` frame 780 for anyone who wants to
re-check it.

Pinned-pose pattern (both zones): radius is slower or roughly flat at every measured
frame except two near-zero-region frames (bf626, qz750) where it's marginally faster
(noise). The worst single-frame gap is befallen's bf987 (+17.5%, 14 regions demoted) —
consistent with the whole-run finding that radius's cost scales with how many regions it
keeps per-pixel that count would have demoted by its flat budget-20 cutoff, and that
dense city/zone poses are where that gap is largest.

### x86 pixel spot check (regression on the Q03 losslessness claim)

`build-gles2/bin/gles2_zone_benchmark` (llvmpipe), current fixture (`--player-light-level
9`), radius vs full-PP (no `--adaptive-lighting`), at 3 poses (bf472, bf987 — the P08
pinned poses — plus qeynos2's first golden view), tested BOTH without and WITH the full
stack flags (`--animate --merge-zone-draws --composite-scenegraph --occluder-budget
256`):

| pose | stack=none | stack=FULL |
|---|---|---|
| befallen/bf472 | identical, 0/480000 px diff, max_delta=0 | identical, 0/480000 px diff, max_delta=0 |
| befallen/bf987 | identical, 0/480000 px diff, max_delta=0 | identical, 0/480000 px diff, max_delta=0 |
| qeynos2/qeynos2_f0 | identical, 0/480000 px diff, max_delta=0 | identical, 0/480000 px diff, max_delta=0 |

**Pixel-identical at all 3 poses, both with and without the full stack — the Q03
losslessness result holds under the current fixture and is unaffected by the merge/
composite/occluder changes**, as expected (those changes affect draw-call batching and
occlusion culling, not per-pixel shading correctness).

### Flip verdict (REPORT ONLY — no default touched)

Per the plan and `docs/decisions.md`, this unit does not flip `--pp-budget-mode`'s
default or any preset. Summary for the orchestrator/Will:

- **Losslessness still holds, and is now proven robust to the full stack.** Radius vs
  full-PP remains pixel-identical (0 diff, max_delta 0) at all 3 x86 spot-check poses,
  both with and without merge+composite+occluder-256. This confirms Q03's central
  finding — radius mode never unlights geometry the light actually reaches — is not an
  artifact of the pre-stack benchmark harness.
- **Radius is still measurably slower than count under the full stack, in both zones,
  on both the whole-run average and p95.** Befallen (the at-KPI zone): +3.3% avg /
  +6.2% p95. Qeynos2 (the above-KPI zone): +1.3% avg / +1.8% p95. Neither gap is "within
  noise" by the standard this thread has used throughout (P07d/Q03's own bar was ~1%
  moving-path average as the noise floor; befallen's 3.3%/6.2% is clearly
  outside that, qeynos2's 1.3%/1.8% is borderline but still consistently in the same
  direction on both avg and p95, across both sessions' invocation order).
- **The gap has NOT closed since Q03, and if anything widened at the at-KPI zone.**
  Q03 measured qeynos2 (the only zone it tested on the moving path) at +1.1% avg — this
  unit's qeynos2 figure (+1.3% avg) is statistically indistinguishable from that despite
  the intervening merge/composite/occluder-256 stack changes; the stack simply did not
  move the needle on this specific lever. Befallen, newly measured on the moving path for
  the first time in this thread, shows a larger gap (+3.3% avg / +6.2% p95) than qeynos2
  does — the opposite of what would be needed for a flip case, since befallen is the zone
  actually AT the 30fps KPI line (radius alone would push its p95 from 32.389ms to
  34.381ms, crossing back over the 33.3ms line that P14's occluder-256 work specifically
  closed).
- **Verdict: radius mode is NOT a flip candidate under the current stack and fixture.**
  It remains provably lossless (quality strictly better than count, confirmed again here)
  but measurably slower everywhere tested, including — critically — at befallen, where
  flipping would re-open the exact KPI gap P14 closed. The demotion-frequency data
  (43-44% of befallen frames, 27% of qeynos2 frames now demote at least one region under
  radius, vs Q03's own already-elevated 24.1% figure) shows radius is doing real,
  non-trivial work at these light-9/stack conditions — it is not "measuring nothing" the
  way P07d's original mega-lamp fixture was — it is simply not free. Any future
  flip candidate would need either a cheaper radius-mode implementation (the cost here is
  radius keeping MORE regions per-pixel than count's flat cutoff, not radius's demotion
  logic itself being expensive) or a different KPI trade (e.g. accepting befallen's p95
  regression in exchange for radius's quality win) — neither is in scope for a
  report-only unit. Per `docs/decisions.md`, any actual flip remains its own unit with
  its own tier-2 re-bless.

### Deviations from the plan

- A `SessionAbort` occurred mid-session-1 (CmaFree dropped below the 40,960 kB floor
  after invocation #3) — handled per the panic-safe protocol (governor restored,
  `sync && drop_caches` remediation, no reboot); all 3 completed invocations' data had
  already landed, and the single remaining invocation (`befallen_count`) was run in a
  short session 2 — well within the ≤12/session, 2-sessions-OK budget (4 total
  invocations used).
- qeynos2 has no established pinned-frame set from prior units (unlike befallen's
  P08/P11b/P14 precedent of bf355/472/626/791/917/987) — the plan didn't specify one, so
  4 frames were derived post-hoc from the count-mode run's densest steady-state
  `regions_vis` values plus 2 evenly-spaced frames; one candidate (frame 780, the single
  densest frame) was excluded and flagged separately as a one-frame load-hitch outlier
  rather than silently used or silently dropped, per the evidence-discipline rule.
- The x86 pixel spot check was run both without AND with the full stack flags (the plan
  only required "under the CURRENT fixture," matching Q03's own regression-check scope)
  — the extra with-stack pass was added because this unit's whole premise is
  re-measuring under the full stack, so confirming losslessness survives it too seemed
  directly relevant and was cheap (6 extra renders).
- No production or benchmark source changes were needed (plan's own expectation, "flags
  exist since P06/P07d") — pure measurement, two ad hoc scratch driver scripts, not
  committed as shared assets, consistent with every prior unit in this thread
  (P07d/Q01/Q03/P14).
