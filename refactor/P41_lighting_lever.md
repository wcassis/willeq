# P41 — Lighting-model lever: characterize for Will's decision (+ Pi ride-alongs)

**Slot: slot1. OWNS THE PI (P02 protocol; budget ≤10 invocations).
x86 for builds/stills. No eqemu.** Sibling P42 (slot2) changes
driver/benchmark/config files — you build YOUR OWN binaries from YOUR
branch; no coordination needed.

P38 measured lightweight-vs-per-pixel lighting = −15% total / −22%
GPU, the largest lever found. But per-pixel adaptive lighting is
BLESSED visual territory (Q03: GLS level 9, ×3 boost, count-mode —
Will personally tuned this). This unit CHARACTERIZES the trade for
Will's decision — it does NOT flip anything.

## Work
1. **Pi numbers, both loads**: lightweight vs per-pixel A/B at
   realistic (e5) AND stress (e205) on the canonical befallen replay,
   shipped config otherwise. (~4 invocations.)
2. **Visual A/B for Will**: x86 stills at the Q03 golden poses
   (befallen GLS fixture — the exact scenes Will blessed: lightstone/
   greater-lightstone lighting) rendered per-pixel vs lightweight,
   same-pose by construction. Honest crops of where they differ most.
   Stills to /mnt/projects/fable/videos/p41_lighting/ + committed
   manifest under benchmarks/audits/p41_lighting/. You do NOT declare
   acceptability; note objectively where lightweight loses (per-vertex
   banding on large polys is the 1999-era signature — Will may
   actually LIKE that era-wise; report, don't spin).
3. **Ride-along A (P40 closure, 2 inv)**: clean 400x300 vs 800x600
   A/B, shipped flags, canonical replay — draws/tris counters. Closes
   the resolution question definitively.
4. **Ride-along B (audit item 5, 1-2 inv)**: eglSwapInterval(0)
   diagnostic (local prototype, reverted) — quantify the swap/pacing
   floor's share at realistic load.
5. Deliverable: numbers tables + stills manifest + a short options
   note (keep per-pixel / flip to lightweight / per-tier knob),
   appended to refactor/perf_truth_audit.md as a dated section.

## Rules
- NO preset/config flips; NO golden changes (gate PASS); all
  prototypes local+reverted; commit = docs, manifest, CSVs, plan
  Review.
- Pi protocol absolute; pristine exit.

## Verification
- Bar narrowed (measurement unit): builds clean, ctest baseline, gate
  PASS, ARM clean. Tables + stills + invocation count reported.

## Review

Executed 2026-07-30 on `wt/slot1`, branch `task/P41-lighting-lever`, cut
from `fable` at `33bf633`. Full results, tables, and options note appended
to `refactor/perf_truth_audit.md` as a dated section ("2026-07-30 — P41");
visual A/B manifest at `benchmarks/audits/p41_lighting/EVIDENCE_MANIFEST.txt`
(stills on NFS at `/mnt/projects/fable/videos/p41_lighting/`, gitignored
per the P20 convention). Per-invocation scripts committed at
`refactor/scratch/p41_pi_run.py`, `p41_pi_run2.py` (e205-lightweight-only
continuation after a SessionAbort), `p41_visual_diff.py`.

### Headline findings

1. **e5 lighting A/B reused from P38** (same replay/config/methodology —
   re-measuring would only reproduce it): -15.1%/-16.5% total, -22.1%/
   -26.7% GPU (avg/p95). **e205 (new, this unit)**: -6.4%/-12.1% total,
   -11.2%/-18.1% GPU — **the lever's relative importance roughly halves
   under stress load** because the absolute GPU savings (~1.3ms) stay
   nearly constant while submission/collect-cull cost (entity-count-driven)
   grows and dilutes it. P38's "largest lever" framing was an e5-only
   finding; this unit adds the e205 counterpoint.
2. **Visual A/B**: doorway/archway pose (f1) shows 89.9% of pixels differ
   (mean channel delta 11/255) — the clearest 1999-era per-vertex-banding
   signature on large flat polys; the other two poses show smaller,
   localized differences (~29-30%). Objective report only, no acceptability
   call.
3. **Ride-along A (P40 closure) — reopens, does not close, the question**:
   the resolution-dependent visibility anomaly P38 found and P40 could NOT
   reproduce on x86/llvmpipe **DOES reproduce on the real Pi** (draws
   8.7→10.6, triangles 1,379.6→1,917.8 at 400x300 vs 800x600, GPU time
   >3x WORSE at the lower resolution) — a Pi/Lima-driver-specific
   mechanism P40's x86-only investigation couldn't see. Flagged as a new,
   higher-priority follow-up (P40's root-cause method, run against the
   actual hardware).
4. **Ride-along B**: `eglSwapInterval(0)` changes nothing measurable
   (swap_ms 3.805 vs control 3.873, total/GPU also unchanged, within
   noise) — a decisive null. Rules out EGL's default swap-interval as the
   source of the ~14.4-15.0ms wall-clock floor; the DRM page-flip's own
   explicit `waitForFlipComplete()` wait is the more likely remaining
   candidate, named for a future investigation, not proven here.

### Pi protocol / invocation count

4 timed Pi invocations this unit (well under the ≤10 budget): e205
per-pixel, e205 lightweight, 400x300 resolution ride-along, swap-interval-0
diagnostic. 2 additional pre-flight CmaFree gate trips were caught and
aborted cleanly by `pi_common.py`'s `SessionAbort` BEFORE any benchmark ran
(zero wasted Pi time) — one mid-session after the e205 per-pixel run
drained CmaFree to 0, one at the start of the 400x300 call when a prior
`drop_caches` hadn't been re-run. Both remediated with `sync; echo 3 >
/proc/sys/vm/drop_caches` (never reboot), confirmed recovered each time
(0→76,660 kB; 40/112→70,716 kB). Governor `ondemand` on all 4 cores at
final exit (confirmed); `/tmp` swept clean of all staged binaries/CSVs/
logs/path files at final exit (confirmed via `ls`); final CmaFree 77,428 kB
(healthy). Full telemetry: `benchmarks/pi_runs/p41_lighting_lever/cma_telemetry.tsv`.

### Verification

- `cmake --build build -j24`: clean (no source changes landed — the
  swap-interval prototype in `docker/irrlicht-drm/CIrrDeviceFB.cpp` was
  added, used for ride-along B's ARM build, then fully reverted; `git diff`
  confirmed empty before the final ctest/gate pass).
- `ctest --test-dir build --output-on-failure`: 1487/1488 passed. Failures:
  `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception)
  and `RDPServerFrameTest.UpdateFrameMultipleTimes` (documented RDP-race
  exception, SEGFAULT this run — intermittent host issue per prior units'
  precedent, not a regression). `visual_gate` (test #1488) passed in this
  run and in 2 additional standalone `ctest -R visual_gate` runs (it
  failed once, alone in the full-suite run BEFORE any of this unit's
  changes, alongside AudioManagerTest only — re-ran standalone immediately
  after and it passed; not reproduced again across 3 total checks,
  consistent with resource contention during full-suite parallel
  execution, not a regression from this unit, which touched no rendering
  source).
- `scripts/build-arm-noble.sh`: clean, twice (once with the swap-interval
  prototype for ride-along B's Pi run, once after reverting it — both
  succeeded, confirming the revert is source-clean and does not affect the
  ARM build).
- No golden regenerated (`visual_gate.py --bless` never invoked); no
  preset/config file touched; no production default flipped.

### Deviations from the plan

- Ride-along A's 800x600 leg reused P38's existing e5-shipped-preset
  control CSV rather than re-measuring — same replay file, same shipped-
  preset flags, same `--gl-counters` methodology, so a fresh measurement
  would only reproduce the existing number at the cost of Pi budget better
  spent elsewhere (the 400x300 leg and the e205 pair were genuinely new
  data). Flagged explicitly in the perf_truth_audit.md table rather than
  presented as fresh.
- Ride-along A's finding is the opposite of "closing the question" the
  plan's title implied — it reopens P40's resolution anomaly as
  Pi/driver-specific, not resolved. Reported honestly rather than forcing
  the "definitively closed" framing the plan's Work section 3 used.
- One SessionAbort mid-plan (e205 lightweight leg) required a second,
  separate script invocation (`p41_pi_run2.py`, committed) to complete
  after Pi remediation — the original `lighting_e205` plan bundled both
  legs in one `SessionRunner` call; splitting them was the smallest fix
  once the abort happened mid-session, per the panic-safe protocol (never
  retry inside an aborted session).
