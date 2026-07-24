# P06 — Area-aware adaptive per-pixel lighting budget

Fix for the P05-diagnosed outskirts anomaly (root cause:
renderer_zone_draw.cpp:191-284 spends the per-pixel budget by region COUNT
— `regionsDrawn >= zonePPBudget` — so 1-6 huge outskirts regions never
exhaust it and the whole screen gets full per-pixel fill at 2.16-2.59x
lightweight cost, while dense city scenes cap almost immediately). Will's
standing direction: per-pixel STAYS enabled; headroom via technique.
AWAITING WILL'S GO before dispatch.

## Design (P05 fix candidate #1 — highest confidence)

1. **Budget in estimated screen area, not region count.** Per visible
   region, estimate projected screen coverage (project the region AABB's
   corners to screen space, take the clamped bounding-rect area as the
   estimate — cheap, conservative, no per-triangle work). Accumulate over
   the front-to-back-sorted region list; regions get the per-pixel material
   until cumulative estimated area exceeds `k x screen_area` (k tunable,
   default from measurement), then lightweight. Nearest-first order means
   the foreground keeps per-pixel — same intent as today, correct units.
2. **Flag-gated with the old behavior preserved**: config field + CLI
   (`--pp-budget-mode count|area`), default `count` until verified, then
   flip the orangepi preset default to `area` as part of this unit's
   conclusion (with Will's sign-off on the golden re-bless).
3. Mirror the logic in the benchmark tool's adaptive path
   (zone_bench_loader.cpp ~line 874 material-override region) so
   measurement parity holds.
4. Entity PP budget: unchanged this unit (entity budget is per-entity and
   roughly area-proportional already; note for follow-up if P05's poses
   show entity-side effects).

## Verification

- **Primary measure**: the P05 outskirts poses (see
  refactor/P05_outskirts_anomaly.md review + CSVs in
  benchmarks/pi_runs/p05_outskirts/) — before/after gpu_ms and p95 at
  area-mode vs count-mode vs all-LW. Target: close most of the 2.2-2.6x
  gap at outskirts poses; city poses within noise of count-mode.
- Entity-laden zl10-style config (Will's March methodology, 200 entities,
  moving path) — avg fps must not regress vs count-mode; expect gains on
  outskirt path segments.
- Golden zones baseline compare (Pi tier-2) — expect legitimate
  frame-time improvements; re-bless with justification.
- **Visual gate**: area-mode changes WHICH regions get per-pixel lighting →
  legitimate pixel changes expected in goldens when the mode flips on.
  Re-bless + contact sheet to Will (lighting-quality judgment: distant
  regions dropping to lightweight must not look wrong — that's exactly
  what his eyes are for).
- Full production bar: ctest, replay oracle, ARM build. P02/P04 Pi
  protocol throughout.

## Context for a fresh session (post-compaction pickup)

The whole thread: H04 (Pi baseline, all zones <30fps) → P01/P02/P03
(EQE1 saga: resolved — format kept, loader fixed in P04, -110MB RSS) →
P05 (this anomaly, root-caused) → P06 (this fix). Standing decisions in
docs/decisions.md (2026-07-24 entry); open items in refactor/deferred.md;
Pi access + protocol in refactor/H04/P02 reviews and scripts/pi/;
kmsg follower runs on the VM logging to
/mnt/projects/fable/pi_console/kmsg_follow.log.

## Review

(executor fills in)
