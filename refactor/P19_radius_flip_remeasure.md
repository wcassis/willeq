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
(executor fills in)
