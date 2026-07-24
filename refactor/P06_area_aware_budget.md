# P06 — Area-aware adaptive per-pixel lighting budget

Fix for the P05-diagnosed outskirts anomaly (root cause:
renderer_zone_draw.cpp:191-284 spends the per-pixel budget by region COUNT
— `regionsDrawn >= zonePPBudget` — so 1-6 huge outskirts regions never
exhaust it and the whole screen gets full per-pixel fill at 2.16-2.59x
lightweight cost, while dense city scenes cap almost immediately). Will's
standing direction: per-pixel STAYS enabled; headroom via technique.
APPROVED by Will 2026-07-24. Slot: wt/slot1, branch task/P06-area-budget.

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

## Implementation anchors (from P05 investigation)

- **Fix site**: `src/client/graphics/renderer_zone_draw.cpp:191-284`
  (`IrrlichtRenderer::drawZoneGeometrySorted`) — the decision is line 284:
  `bool regionUseLW = adaptiveLight && regionsDrawn >= zonePPBudget;`
- **Config**: `include/client/graphics/constrained_renderer_config.h:122`
  (`adaptiveLightingZoneBudget`). Add a mode field (count|area, enum or
  string; default count) + an area budget factor `k` (float, fraction of
  screen area; pick a starting default ~1.0-1.5 and tune against the P05
  poses — report the chosen value and why). JSON override plumbing:
  `src/client/graphics/constrained_renderer_config.cpp:499` block; preset
  defaults near `:307` (orangepi preset). Do NOT flip any preset default
  in this unit — see scope note below.
- **Benchmark parity**: `tools/hwcaps/gles2_zone_benchmark.cpp` — add
  `--pp-budget-mode count|area` + area factor flag next to
  `--zone-pp-budget` (arg parsing ~line 343, help ~240); mirror the
  decision logic in `zone_bench_loader.cpp` ~line 874 material-override
  region so benchmark numbers predict production behavior.
- **Ordering caveat**: the design assumes the accumulation runs
  nearest-first. Verify what order `sortedZoneDrawList_` actually is
  (it may be material-sorted); if it is not camera-distance-ordered,
  compute the per-region PP/LW decision in a cheap pre-pass over visible
  regions sorted by camera distance, then apply the precomputed flags
  during the draw loop. Note whichever path you took in the review.
- **Projection details**: project all 8 region-AABB corners with the
  current view-projection; if any corner lands behind the near plane,
  treat the region conservatively (full-screen area). Clamp the bounding
  rect to the viewport before computing area. Cheap and conservative —
  no per-triangle work.
- **Scope note**: this unit lands the mechanism flag-gated with default
  `count` everywhere, plus measurements. Flipping the orangepi preset
  default to `area` and the golden re-bless happen AFTER orchestrator
  review and Will's visual sign-off — not in this unit.

- **Primary measure**: the P05 outskirts poses (see
  refactor/P05_outskirts_anomaly.md review + CSVs in
  benchmarks/pi_runs/p05_outskirts/) — same benchmark config
  (`--zone qeynos2 --entities 200 --seed 42`, moving path, 1280x720),
  three variants A/B'd on the Pi under the P02 protocol: count-mode
  (production today), area-mode, all-LW (`--zone-pp-budget 0`).
  Acceptance: at the anomalous outskirts frames (558, 560-563), area-mode
  total_ms within ~20% of all-LW (i.e. closes most of the 2.2-2.6x gap);
  at the city frames (617-635 band), area-mode within noise of count-mode;
  whole-run avg fps ≥ count-mode. Reuse P05's manual-script approach
  built on `scripts/pi/pi_common.py` (GovernorPin, CmaMonitor,
  randomized interleave, /tmp-only I/O, session caps, leave Pi pristine).
- Verify draws/regions/polys stay byte-identical between count and area
  CSVs at the compared frames (only the lighting decision may differ) —
  same programmatic check P05 used.
- Full production bar: x86 GLES2 build (scripts/build-gles2-x86.sh),
  ctest, ARM build (scripts/build-arm-noble.sh from the task worktree).
  Visual gate must PASS unchanged (default is count everywhere, so
  goldens must not move in this unit).
- AFTER this unit (orchestrator + Will, not the executor): flip orangepi
  preset default to area, golden re-bless + contact sheet to Will
  (distant regions dropping to lightweight must not look wrong — that's
  exactly what his eyes are for), Pi tier-2 golden-zone baseline rerun.

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
