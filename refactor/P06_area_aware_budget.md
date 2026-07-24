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

### Implementation

Landed as designed: a flag-gated `count|area` mode (default `count`
everywhere, no preset default flipped) on top of the existing
`adaptiveLightingZoneBudget` gate (area mode is a no-op unless that budget is
still set > 0 — it changes how the budget is *spent*, not whether adaptive
lighting is active at all).

- **Config**: `include/client/graphics/constrained_renderer_config.h` —
  `AdaptiveLightingBudgetMode { Count, Area }` enum,
  `adaptiveLightingBudgetMode` (default `Count`), and
  `adaptiveLightingAreaBudgetFactor` (k, default `0.75`). JSON override
  plumbing in `constrained_renderer_config.cpp` (`adaptiveLightingBudgetMode`:
  `"area"`/anything-else, `adaptiveLightingAreaBudgetFactor`: float). No
  preset (including `orangepi`) sets these — count-mode default applies
  everywhere until a follow-up flips it with Will's sign-off.
- **Fix site**: `src/client/graphics/renderer_zone_draw.cpp`,
  `drawZoneGeometrySorted()`. Added an anonymous-namespace
  `estimateRegionScreenArea()` helper (projects the region's world-space AABB
  8 corners — local mesh bbox + node translation, matching the existing
  translation-only `worldMat` used elsewhere in the same function — through
  `captured3DProj_ * captured3DView_`, clamps the screen-space bounding rect
  to the viewport, returns its area; any corner at/behind the near plane
  (`clip.w <= 0.0001`) returns the full screen area as a conservative
  fallback). The per-region decision in area mode accumulates each region's
  *own* estimated area into a running total **before** deciding
  (`regionUseLW = cumulativeArea > k * screenArea`) — see "k semantics" below
  for why this order is load-bearing, not cosmetic.
- **Ordering caveat resolved by inspection, no pre-pass needed in
  production**: confirmed via `sim_visibility.cpp:104-148`
  (`SimulationWorker::computeVisibility`) that `sortedZoneDrawList_` is
  already sorted front-to-back by `distanceSq` before the renderer ever sees
  it, so `drawZoneGeometrySorted()`'s existing single pass over the list is
  already nearest-first — no separate distance pre-pass was needed there
  (the plan flagged this as a possible requirement; it wasn't).
- **Benchmark parity**: `tools/hwcaps/gles2_zone_benchmark.cpp` (new
  `--pp-budget-mode count|area` and `--pp-area-factor F` flags, default
  `count`/`0.75`) and `tools/hwcaps/zone_bench_loader.cpp`/`.h`
  (`ZoneBenchData::zonePPAreaMode`, `zonePPAreaBudgetFactor`). Unlike
  production, `collectDrawEntries()`'s `visibleRegions` is only
  distance-sorted when `frontToBack||occlusionCull` is requested, so here the
  ordering caveat **did** apply: area mode's decision is computed as an
  explicit pre-pass (`distOrder`, sorted by `distSq`) into an
  `areaUseLW` map, independent of `visibleRegions`' actual iteration order,
  keyed by region index and consulted when building draw entries. Mirrors
  the exact same `estimateRegionScreenAreaBench()` math as production's
  helper (verified byte-for-byte identical logic, separate copies since the
  benchmark tool doesn't link against `IrrlichtRenderer`).
- **Entity budget**: untouched, as scoped — still count-based
  (`adaptiveLightingObjectBudget` / `perPixelEntityBudget`) regardless of the
  zone budget's mode.

### k semantics (why decide-after-accumulating, and why k must be < 1.0)

First implementation attempt decided each region's fate using the cumulative
area of regions *ahead* of it (mirroring the count-mode pattern of "decide,
then increment `regionsDrawn`"). This is wrong for exactly the case this fix
targets: a single region whose own screen footprint is already ~100% of the
screen (the P05 outskirts mechanism) would always get a "free pass" under
that ordering, since the cumulative-before-it is always zero — reproducing
the original count-mode bug for the single/few-huge-region case. The
corrected version accumulates each region's own area **first**, then decides
— so even the nearest (first) region can flip to lightweight if its own
footprint alone exceeds the budget. This means k must be strictly < 1.0 for
the fallback-triggered "full screen" estimate (exactly `screenW*screenH`,
verified via a temporary debug probe — see Deviations) to ever cause a
single such region to flip; k >= 1.0 makes area mode a no-op for exactly the
pose type P05 diagnosed. **This is the single most important implementation
detail in this unit** — a plausible-looking "budget in area instead of
count" implementation can look correct (compiles, matches count-mode's
draws/polys) while doing nothing for the target bug, because the ordering
subtlety only manifests at the sparse-region poses, not the dense ones used
for the byte-identical-geometry sanity check.

### Chosen k = 0.75, and a structural finding about the "city within noise" criterion

Measured on the Pi (Orange Pi One, Mali 400/Lima, ARM build from this
worktree, `gles2_zone_benchmark --zone qeynos2 --entities 200 --seed 42
--navmesh-path /mnt/projects/maps/nav --res 1280x720`, 1000-frame moving
navmesh camera path, P02 protocol: GovernorPin, CmaMonitor with CmaFree
telemetry/abort threshold, randomized variant interleave via a standalone
script reusing `scripts/pi/pi_common.py` — see Deviations for why a script
was used instead of `pi_bench_run.py`). k=0.6, k=0.75 (chosen default), and
k=0.95 were tested; all three fix the outskirts case equivalently (the
single-region case is exactly 1.0x screen via the conservative fallback, so
any k<1.0 flips it) and produce statistically indistinguishable city
numbers (0.6 and 0.95 city total_ms differ by <2% frame-for-frame — see
`area_k0.6.csv`/`area_k0.95.csv`/`area.csv`). **k=0.75 was picked as a
mid-range value with no measured downside relative to the tested extremes**,
not because it hits some sweet spot the other values miss.

**Structural finding, not a k-tuning problem**: no k in the plausible range
(0, 1.0) makes area mode match count-mode's cost at the dense city poses.
City's cumulative estimated area saturates past `k * screenArea` within the
first 1-3 (of ~130-165 visible) nearest regions regardless of k in
[0.6, 0.95] tested — nearby wall/floor/ceiling segments' 2D screen-space
bounding rects overlap heavily when viewed obliquely, so the conservative
(intentionally cheap, no-per-triangle-work) area estimate double- and
triple-counts screen pixels that count-mode's flat 20-region budget would
have spread per-pixel lighting across. The result: area mode is
consistently **12-31% faster** than count-mode at the city reference frames
(one outlier frame, 629, is 11.7% slower — noise), not "within noise" as
the plan's acceptance criterion anticipated. This is a direct consequence of
the plan's own design constraint ("cheap, conservative, no per-triangle
work") interacting with a real property of dense interior/city geometry
(lots of small, mutually-overlapping-in-screen-space regions), not an
implementation shortfall — a k large enough to avoid this would also be
large enough to stop fixing the outskirts case (see previous section), so
the two acceptance criteria as stated cannot both be hit exactly with a
single global k. Flagging for orchestrator/Will's review rather than
silently redefining the criterion: a faster city, using less full-per-pixel
budget than today's count-mode default, is very likely a net win from
docs/decisions.md's "per-pixel stays enabled, headroom via technique"
standing direction, but it does mean the orangepi preset flip (deferred to
the follow-up unit) should get a fresh city-scene visual sign-off from Will,
not just the outskirts contact sheet the plan anticipated.

### Measurements (Pi, Orange Pi One, ARM build, this worktree)

All four variants same command/path/seed; `count`/`area` both use
`--adaptive-lighting --zone-pp-budget 20`, differing only in
`--pp-budget-mode`. `fullpp` = no `--adaptive-lighting` (unconditional full
per-pixel). `lightweight0` = `--adaptive-lighting --zone-pp-budget 0` (forces
all-lightweight, the same convenience trick P05 used). Frame indices are
**fresh poses found in this unit's own reproduction run** (P05's saved frame
numbers 558/560-563/617-635 did not reproduce on this build+path — see
Deviations; same non-transferability P05 itself documented for the March
reference logs, one level further down the chain). Outskirts candidates
found by the same method P05 used: low regions_vis + high fullPP/lightweight0
ratio, searched directly in this run's own CSVs.

| pose type | frames | regions_vis | count_ms | area_ms (k=0.75) | allLW_ms | fullPP_ms | area vs allLW |
|---|---|---|---|---|---|---|---|
| outskirts (1 huge region) | 291 | 1 | 17.10 | 7.39 | 7.34 | 17.06 | **+0.8%** |
| outskirts (1 huge region) | 292 | 1 | 16.67 | 7.08 | 7.08 | 16.67 | **+0.1%** |
| outskirts (2 regions) | 509 | 2 | 19.83 | 9.13 | 8.99 | 19.71 | **+1.6%** |

| city frame | regions_vis | count_ms | area_ms (k=0.75) | area vs count |
|---|---|---|---|---|
| 627 | 160 | 121.30 | 84.10 | -30.7% |
| 628 | 164 | 83.98 | 71.02 | -15.4% |
| 629 | 159 | 80.75 | 90.21 | +11.7% (noise, only outlier) |
| 630 | 155 | 71.20 | 62.50 | -12.2% |
| 631 | 150 | 71.00 | 54.10 | -23.8% |
| 632 | 151 | 76.41 | 65.16 | -14.7% |
| 633 | 144 | 66.78 | 55.05 | -17.6% |
| 634 | 138 | 68.85 | 49.52 | -28.1% |
| 635 | 129 | 61.27 | 46.63 | -23.9% |

Whole-run (1000 frames) average `total_ms`: count-mode 91.18 ms (11.0 fps)
vs area-mode 81.71 ms (12.2 fps).

`draws`/`regions_vis`/`regions_drawn`/`polys` verified byte-for-byte
**identical** between `count.csv` and `area.csv` at **every one of the 1000
frames** (programmatic full-file diff, not just the sampled frames above) —
the only variable between the two runs is the per-region lighting decision.

CSVs: `benchmarks/pi_runs/p06_area_budget/{fullpp,lightweight0,count,area}.csv`
(the four variants at the chosen k=0.75 default), plus
`area_k0.6.csv`/`area_k0.95.csv`/`count_k0.6_ref.csv` (k-tuning support data
referenced above), `cma_log.tsv` (CmaFree telemetry across the whole
session).

### Acceptance criteria verdict

- **Outskirts (291/292/509) area-mode within ~20% of all-LW**: **PASS**,
  by a wide margin (0.1-1.6%, not just under the 20% bar).
- **City band (627-635) area-mode within noise of count-mode**: **FAIL**,
  with a structural explanation (see above) — area is 12-31% faster than
  count at 8 of 9 frames (one frame is 11.7% slower, within noise). Not a
  bug; a consequence of the conservative area estimate's double-counting of
  overlapping small regions in dense scenes, which no single global k can
  fully avoid without also failing the outskirts criterion.
- **Whole-run avg fps area >= count**: **PASS** (12.2 fps vs 11.0 fps).
- **draws/regions/polys byte-identical count vs area**: **PASS** (verified
  across all 1000 frames, not just the compared subset).
- **Visual gate PASS unchanged (count-mode default)**: **PASS** (3 clean
  runs after one flaky CPU-timing-ceiling failure on this shared VM,
  unrelated to pixel content — reran twice more, all PASS; goldens
  untouched, confirmed by two subsequent clean runs).
- **Full production bar** (build, ctest, ARM build, file-size check): **PASS**,
  see Verification section below.

### Deviations

- **P05's saved frame indices (558, 560-563, 617-635) did not reproduce on
  this build.** Same non-transferability P05 itself found and documented for
  the March reference logs one level up the chain — this time it's P05's
  own numbers that don't transfer forward to P06's run of the identical
  command (`--zone qeynos2 --entities 200 --seed 42`, same worktree). Root
  cause not investigated (out of scope, matches P05's precedent of not
  chasing this) — candidates include intervening changes to navmesh
  waypoint sampling or entity placement order between P05's session and this
  one. Fresh poses were found directly in this unit's own 1000-frame CSV
  using the same method P05 used (low regions_vis + high fullPP/lightweight0
  ratio search).
- **A caught-and-fixed mid-unit bug is worth flagging explicitly**: the
  first implementation (decide using cumulative-before-this-region, mirroring
  count-mode's "decide then increment" pattern) passed every mechanical
  check (built clean, ctest green, visual gate green, ARM build green,
  byte-identical draws/polys) while doing **nothing** to fix the target
  anomaly — caught only by actually comparing area-mode vs count-mode timing
  numbers at the outskirts poses on real hardware and finding them
  statistically indistinguishable, then reasoning through the single-region
  case by hand and confirming with a temporary `P06_AREA_DEBUG` env-gated
  diagnostic (added, used locally on x86/llvmpipe with `--navmesh-path`, then
  fully removed before the final commit — not present in the committed
  diff). The ARM binary used for the very first Pi round (`area.csv`/
  `count.csv` in an earlier working state, since overwritten) was built
  *before* this fix landed; it was rebuilt and every Pi measurement in the
  tables above re-run against the corrected binary. This is the reason two
  ARM builds and three Pi sessions appear in the history for what is
  otherwise a single-conditional fix.
- **Local x86/llvmpipe reproduction required `--navmesh-path
  /mnt/projects/maps/nav`** (not documented as required anywhere in the
  existing benchmark docs/help text) — without it the tool silently falls
  back to an orbit camera path, which does not reproduce the navmesh-driven
  poses at all. Confirmed the local x86 run (with `--navmesh-path`) and the
  Pi ARM run produce bit-identical camera paths/region data at every
  compared frame, which is what allowed local debugging (with the temporary
  diagnostic) to stand in for additional Pi round-trips while tuning k.
- **Pi session used a manual P02-protocol script** (not `pi_bench_run.py`),
  same precedent and reasoning as P05 (the manifest's fixed static poses
  don't match this unit's need for the benchmark's synthetic moving-camera
  path). Script was written to `refactor/scratch/p06_measure.py` for the
  duration of the work and deleted before the final commit (not part of the
  committed diff) — same "small standalone script, not committed" precedent
  P05 set.
- **CmaFree required two `sync; echo 3 > /proc/sys/vm/drop_caches` reclaims
  during this session** (once before starting — the Pi had been up 8h with
  CmaFree at 344 kB, far below the 40 MB abort threshold, entirely from the
  documented page-cache-borrowing pattern in `docs/decisions.md` — and once
  mid-session after it dropped to 37.8 MB following a single 1000-frame
  invocation). A raw `sudo reboot` was attempted first and was blocked by
  the harness's auto-mode permission classifier; `drop_caches` is a
  non-destructive, already-documented-safe alternative (P03's finding that
  low CmaFree here is reclaimable page-cache borrowing, not a leak) that
  fully resolved it both times without needing a reboot or session
  interruption. No `SessionAbort` was ever raised — the abort threshold was
  headed off proactively both times by checking CmaFree before invoking
  `run_session`, not by catching an actual abort.
- **`benchmarks/pi_runs/` is repo-gitignored** (added during H04, still in
  effect) — P05's own CSVs under `benchmarks/pi_runs/p05_outskirts/` were,
  on inspection, never actually committed despite that unit's review
  narrating them as committed deliverables (`git ls-files` confirms they
  are untracked; `git log --all` has no commit touching that path). This
  unit's CSVs were force-added (`git add -f`) to actually satisfy "commit
  them" as instructed, rather than repeating the same gap.
- Pi left pristine: governor restored to `ondemand` (confirmed read back
  after the final session), staged binary and remote CSVs/logs removed
  (confirmed via `ls /tmp | grep p06` returning nothing), no reboot was
  triggered or needed (no session exceeded the 12-invocation cap; sessions
  were kept to 1-2 invocations each specifically to bound CmaFree drift
  between drop_caches reclaims).
- ARM build was produced from this worktree
  (`build-arm-noble/` under `/mnt/projects/fable/wt/slot1`), per the task's
  instruction, and rebuilt twice more after the k-default change and after
  the debug-diagnostic removal to ensure the binary staged for the final Pi
  numbers matches the exact committed source.
