# P07d — Radius-based per-pixel demotion (lossless by construction)

Approved by Will 2026-07-24. Slot: wt/slot2, branch task/P07d-radius.
Supersedes the P07c flip (branch task/P07c-default-flip parked UNMERGED —
its harness-alignment and baseline work will be reused when the flip is
redone on top of this unit).

## Motivation (P07c orchestrator review)

The area+floor mode blacks out frame-dominant geometry at ordinary
viewpoints (see benchmarks/contact_sheets/p07c_flip/ on the parked branch:
qeynos2 f0 building, f2 pool room) — the floor protects the nearest region
by DISTANCE, but the structure dominating the screen can be region #2-3,
by which point region 1's full-screen fallback has spent the budget.
P07b's city savings were partly earned by unlighting visible geometry.

The correct criterion falls out of the shader architecture: the per-pixel
and lightweight zone shaders differ ONLY in the player light (light[0] is
the sole per-pixel term; the other 7 point lights are per-vertex in both
paths). The player light has finite attenuation. A region lying entirely
beyond the light's effective range renders (near-)identically under either
shader — so demoting exactly those regions is visually lossless BY
CONSTRUCTION, with no screen-area estimate, no k, no floor, no near-plane
fallback.

## Design

1. **New mode**: `AdaptiveLightingBudgetMode::Radius` (third enum value;
   JSON string "radius"; benchmark `--pp-budget-mode radius`).
2. **Demotion rule**: region → lightweight iff the closest point of its
   world-space AABB to the PLAYER LIGHT position is beyond the light's
   effective range R. Compute R once per frame from the actual light[0]
   attenuation parameters as the distance where the maximum possible
   contribution falls below ~1/255 (account for the wrap-lighting term's
   direction-independent floor — wrap `(NdotL+0.5)/1.5` has a minimum of
   1/3, so use max contribution = attenuation * 1.0, conservative).
   Config: `adaptiveLightingRadiusMargin` (float, default 1.0) multiplies
   the computed R for headroom/tuning.
3. **Correctness-critical**: use CLOSEST-POINT-of-AABB distance, NOT
   region-center distance — the huge outskirts region's center is far
   away while its geometry is underfoot; center-distance would wrongly
   demote it and reintroduce the black-ground artifact. Check what
   `sortedZoneDrawList_` entries' `distanceSq` actually is (computed in
   sim_visibility.cpp) before reusing it; compute closest-point distance
   from the AABB if it is center-based.
4. **Light position/params source**: find where the renderer sets the
   per-pixel player light (zoneShader_ / light[0] uniforms) and read the
   same position + attenuation values the shader will use this frame.
   In the benchmark, mirror from its own player-light setup
   (zone_bench_loader hard-codes per-pixel lighting; find its light[0]).
5. `adaptiveLightingZoneBudget > 0` continues to gate adaptive lighting
   on/off; in radius mode the count value itself is unused (document in
   the field comment). Count and area modes untouched. Defaults stay
   `count` everywhere — no preset flips in this unit.
6. **Report the computed R and the demoted-region fraction** per pose in
   the review. If R turns out to cover the whole scene (tiny attenuation
   → no savings) or nearly nothing, STOP and report for orchestrator
   review rather than tuning unilaterally.

## Verification

- Build + ctest (known host exceptions) + x86 GLES2 build + visual gate
  PASS unchanged (defaults untouched) + ARM build + file-size check.
- **Losslessness check (the headline claim), x86 llvmpipe**: render the 9
  golden-manifest views with count-mode (production today, budget 20) vs
  radius-mode. Target: pixel-identical or near-identical (report max
  per-view pixel delta; small differences confined to the range boundary
  are acceptable — quantify them). Also render the P06/P07 contact-sheet
  poses (fr291/509/627/634 static poses in refactor/P07_pp_hybrid_floor.md)
  count vs radius. Contact sheets → benchmarks/contact_sheets/p07d_radius/
  (git add -f).
- **Pi measurement (P02 protocol, same qeynos2 config lineage:
  --entities 200 --seed 42, 1280x720)**: count vs radius vs all-LW.
  Moving-path runs for whole-run averages + static-pose captures for the
  P07 plan's 6 poses (path drift expected — 5 prior occurrences; static
  fallback is standard). Acceptance:
  - Anomaly poses (fr291/292): radius within noise of count (~17-18ms —
    the huge region is in light range and must stay per-pixel).
  - City poses (fr627/631/634): radius captures a majority of the
    area-floor1 saving measured in P07b (-19 to -24% vs count); report
    the fraction. If it captures little (most regions within R), that is
    a finding, not a failure — report for review.
  - Whole-run avg: radius <= count.
  - draws/regions_vis/regions_drawn/polys byte-identical count vs radius
    (full-file programmatic check).
  - CSVs → benchmarks/pi_runs/p07d_radius/ (git add -f).
- Pi rules: P02 protocol via scripts/pi/pi_common.py, /tmp-only I/O,
  stripped staged binary (ARM_BUILD_BIN_DIR trap — stage the slot2-built
  binary), drop_caches not reboot, leave Pi pristine, report blocked if
  the Pi goes unreachable.

## Out of scope

- Preset flips / golden re-bless / tier-2 re-capture (redone on top of
  this unit by reviving the parked P07c work if the numbers hold).
- Removing area/floor modes (they stay as flag-gated experiments).
- Any shader changes.

## Review

(executor fills in)
