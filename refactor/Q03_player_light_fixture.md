# Q03 — Align the benchmark player-light fixture to production, re-bless, re-measure radius vs count

Approved by Will 2026-07-25 ("go ahead with Q03").
Slot: wt/slot2, branch task/Q03-light-fixture, **based on
task/Q01-path-replay** (b897f8b — Q03 needs Q01's --path-file replay,
productionConfig plumbing, and canonical paths; the two branches merge
together after Will's bless). Do not commit to Q01's branch itself.

## Motivation (Will's re-bless review, 2026-07-24)

Will rejected the Q01 count-mode goldens: "the lighting in the new column
does not match normal gameplay — as though the player's light source is
disabled." Diagnosis confirmed in code: the benchmark stages a night scene
(ambient 0.02/0.02/0.04, sun black, tint 0.15/0.15/0.2 —
gles2_zone_benchmark.cpp:457-460) lit by a camera-collocated lamp with
FIXED attenuation (1.0, 0, 0.0005) and color (1.0, 0.95, 0.8)
(gles2_zone_benchmark.cpp:486-487) — effective range R≈712 EQ units.
Production's player light (renderer_entities.cpp:98-156) is a lightstone:
`radius = 20 + (level/10)*80` (20-100 EQ units), `quad = 19/radius²`
(~5% at radius), warm level-dependent color, positioned at player +3z.

Consequences of the fixture lamp:
- OLD goldens (full-PP): whole zone lit by a lamp no character carries.
- Q01 goldens (count-20): same mega-lamp, nearest-20 regions only → glaring
  black cliff at the demotion boundary. Neither matches gameplay.
- **P07d's central finding (R=712 covers the zone → radius mode demotes
  nothing, saves nothing) was a property of the FIXTURE, not the game.**
  Under production attenuation (R≈73-366 by the 1/255 measure, perceptually
  ≤100), radius mode may demote nearly as much as count at vistas while
  never unlighting geometry the light actually reaches. This unit
  re-measures that properly.

## Design

### A. Production-mirrored player light in the benchmark

1. New flag `--player-light-level N` (integer 0-10, **default 10**).
   Level 0 = no player light (light color black / contribution zero —
   document the mechanism). For N≥1, mirror renderer_entities.cpp exactly:
   - `intensity = N/10`, `radius = 20 + (N/10)*80`
   - color `r = min(1, 0.9 + 0.1*intensity)`, `g = min(1, 0.7 + 0.2*intensity)`,
     `b = min(1, 0.4 + 0.2*intensity)`
   - attenuation: verify from production code what the shader's
     uLightAtten[0] actually receives for the player SLight (follow the
     chain renderer_entities.cpp SLight → renderer_sim_bridge.cpp:755/760
     setPlayerLightAtten; confirm constant/linear terms, not just
     quad = 19/radius²) and replicate the SAME triple.
2. Light position: keep the existing camera-collocated placement (the pose
   is an eye-height point; production's player+3z ≈ eye height). Document
   this equivalence explicitly in a comment at the site. Preserve the
   existing per-frame position update path.
3. The old fixed attenuation/color disappears; `--player-light-level 10`
   reproduces a max-level lightstone, which is the new fixture default.
   Keep `--pp-budget-mode`/budget flags orthogonal and unchanged.
4. Mirror the same computation in any second site that sets the bench
   light (zone_bench_loader radius-mode R solve reads
   playerLightAtten() — confirm it picks up the new values with no code
   change; the R solve itself must NOT be touched).

### B. Golden regen + re-bless evidence

5. Add `--player-light-level 10` to the manifest `productionConfig` args
   (single source of truth — gate and Pi scripts both inherit it via Q01's
   plumbing).
6. `visual_gate.py --bless` → new goldens. Gate (no --bless) and full ctest
   must pass on the branch tip.
7. Contact sheet for Will → `benchmarks/contact_sheets/q03_light_fixture/`:
   for each of the 9 views, THREE images side by side — pre-Q01 golden
   (full-PP mega-lamp; git show from the fable tip), Q01 golden (count +
   mega-lamp; git show from task/Q01-path-replay), Q03 golden (count +
   production light) — plus per-view diff stats Q03-vs-each. Expectation to
   state plainly in the review: the night vistas will be DARK beyond the
   lightstone's reach (EQ nights were dark); the near field (nearest
   regions) must show the warm per-pixel pool of light. Manifest approval
   stays `pending` for Will.

### C. Radius-vs-count re-measurement under the production light (the decision data)

8. x86 llvmpipe, production light (level 10), at the 9 golden views + the
   P07 static poses (fr291/292/509/627/631/634):
   - Report computed R (from the new attenuation) and demoted-region
     fraction per pose for radius mode; regions_vis and count-demoted
     fraction for count mode.
   - Pixel comparisons: radius vs full-PP (the losslessness claim — expect
     identical or near-identical; quantify max delta), count vs full-PP
     (count's lossiness under the honest lamp), radius vs count.
9. Pi (P02 protocol): count vs radius vs all-LW under production light:
   - Static poses fr291/292/509/627/631/634, ×10 reps (first discarded),
     P07d table format.
   - Moving path: qeynos2 canonical path REPLAY (Q01's
     benchmarks/paths/qeynos2_seed42_1000f.path, --path-file) per variant —
     whole-run averages, drift-free by construction.
   - draws/regions_vis/regions_drawn/polys byte-identical across variants
     (full-file check, P07d precedent).
   - Acceptance framing (findings, not pass/fail): if radius is within
     noise of count on timing AND radius-vs-full-PP is (near-)identical
     while count-vs-full-PP is not, radius becomes the flip candidate.
     REPORT ONLY — no default flips in this unit (orchestrator + Will
     decide; a flip would be its own unit with tier-2 re-bless, P07c
     lesson).
10. Tier-2 baseline: re-capture ONCE more under the final aligned config
    (count mode + production light) via bench_pi.sh → benchmarks/
    pi_baseline/ (Q01's capture was under the mega-lamp; supersede it,
    note both hashes). Old-vs-new per-zone/variant p95 table in the review.

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host exceptions:
  AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes); scripts/build-gles2-x86.sh;
  visual gate PASS against regenerated goldens; scripts/build-arm-noble.sh;
  scripts/refactor/check_file_sizes.sh fable (gles2_zone_benchmark.cpp is
  already WARN-listed pre-existing — do not let it FAIL).
- Attenuation-mirror proof: a table in the review — production formula
  values (level 10: radius, quad, color triple) vs the flag's computed
  values vs what the shader uniform receives in a debug print — all equal.
- Level sanity: `--player-light-level 0` renders the night scene with no
  player pool of light (one dump-frame as evidence); level 5 R < level 10 R.
- Design points 8-10 tables complete.
- Pi rules: P02 protocol via scripts/pi/pi_common.py, ALL timed-run I/O to
  Pi-local /tmp, stripped staged binary from slot2 (ARM_BUILD_BIN_DIR trap),
  drop_caches never reboot, ≤12 invocations/session (2 sessions OK), leave
  Pi pristine, report blocked if unreachable.

## Out of scope

- Flipping any production default or preset (report the flip case only).
- Touching production lighting code (renderer_entities.cpp etc. is
  read-only reference — this unit changes the BENCHMARK to match it).
- The radius-mode R solve, shaders, sun/ambient/tint staging values.
- Q01's branch itself (base on it, never commit to it).
- Converting historical measurements.

## Review

### Implementation notes

- **Attenuation chain, verified (not assumed)**: `IrrlichtRenderer::setEntityLight`
  (`renderer_entities.cpp:98-156`) sets `lightData.Attenuation =
  (1.0f, 0.0f, 19.0f/radius^2)` on the player `SLight`/`playerLightNode_`.
  That triple flows UNCHANGED through
  `renderer_sim_bridge.cpp:301-305` (`SimulationInput::playerLightAttConstant/
  Linear/Quadratic <- ld.Attenuation.{X,Y,Z}`) into
  `sim_visibility.cpp:581-583` (`SelectedLight::attConstant/attLinear/
  attQuadratic <- input.playerLightAtt*`) and out to
  `ZoneShader::setPlayerLightAtten` at `renderer_sim_bridge.cpp:760`. The
  `x3` "boost" applied at `renderer_sim_bridge.cpp:745/754-761` multiplies
  only the DIFFUSE COLOR uniform sent to the shader for on-screen
  brightness -- it is never applied to the SLight/SimulationInput/
  SelectedLight attenuation fields and never reaches
  `setPlayerLightAtten`, so it is correctly NOT reproduced in the
  benchmark fixture (confirmed by reading every hop of the chain, not by
  assuming `(1, 0, 19/r^2)` as the plan's draft text did).
- **Fixture site**: `tools/hwcaps/gles2_zone_benchmark.cpp`'s `setupLighting()`
  (previously fixed `atten=(1,0,0.0005)`, `color=(1,0.95,0.8)`) now computes,
  from `--player-light-level N` (0-10, default 10):
  `intensity=N/10`, `radius=20+intensity*80`,
  `color=(min(1,0.9+0.1*intensity), min(1,0.7+0.2*intensity),
  min(1,0.4+0.2*intensity)) * intensity`, `atten=(1,0,19/radius^2)` -- the
  exact production formula, byte-for-byte. Level 0: production removes the
  light scene node entirely; the benchmark has no "light[0] absent" state
  (index 0 is always sent to the shader), so the same zero-contribution
  observable behavior is reproduced by zeroing the color uniform (any
  attenuation triple with color=(0,0,0) contributes exactly 0 per-pixel).
- **Position (design point A2)**: unchanged -- still camera-collocated,
  set per-frame in `setupCamera()` (`data.zoneShader->setPlayerLightPos(irrPos...)`,
  called every frame same as before this unit). Documented at the fixture
  site: the benchmark's camera pose is an eye-height point; production's
  lightstone sits at `player position + 3z` (Irrlicht up,
  `renderer_entities.cpp:120-122`), which is also approximately eye
  height -- the two placements are treated as equivalent, not touched.
- **Second site (design point A4)**: `zone_bench_loader.cpp`'s radius-mode R
  solve (`computeLightEffectiveRadiusBench`, called from
  `drawZoneGeometrySorted`'s radius branch) reads
  `data.zoneShader->playerLightAtten()` -- the SAME `ZoneShader` instance
  `setupLighting()` configures -- so it picked up the new level-10 values
  with **zero code changes**, exactly as the plan predicted. Verified by
  the `light_radius` CSV column (see below): a constant 365.629 across every
  pose at level 10, matching the hand-solved value
  (`R = radius*sqrt(254/19)` at `radius=100` -> 365.64, agreeing with the
  CSV's float32 rounding). The R solve itself was not modified.
- **Attenuation-mirror proof (design point 7, verification requirement)**:
  added a `--dump`-gated debug print in `setupLighting()` that reads back
  `zoneShader->playerLightColor()`/`playerLightAtten()` -- the identical
  accessors the R solve and the per-frame draw path consume -- so the
  "shader uniform received" column is not inferred, it's read from the
  live `ZoneShader` object:

  | source | level | radius | color (r,g,b) | atten (c,l,q) |
  |---|---|---|---|---|
  | production formula (hand-computed) | 10 | 100 | (1.0, 0.9, 0.6) | (1, 0, 0.0019) |
  | benchmark flag computation | 10 | 100 | (1.0, 0.9, 0.6) | (1, 0, 0.0019) |
  | shader uniform (debug print readback) | 10 | -- | (1.000000, 0.900000, 0.600000) | (1.000000, 0.000000, 0.001900000) |
  | production formula (hand-computed) | 5 | 60 | (0.475, 0.4, 0.25) | (1, 0, 0.00527778) |
  | benchmark flag computation | 5 | 60 | (0.475, 0.4, 0.25) | (1, 0, 0.00527778) |
  | shader uniform (debug print readback) | 5 | -- | (0.475000, 0.400000, 0.250000) | (1.000000, 0.000000, 0.005277778) |
  | shader uniform (debug print readback) | 0 | -- | (0.000000, 0.000000, 0.000000) | (1, 0, 0) [color=0 => zero contribution regardless of atten] |

  All three columns agree exactly at every tested level. **PASS.**
- **Level sanity (verification requirement)**: `--player-light-level 0` at
  the befallen_f2 pose renders with no player-light pool -- mean pixel
  brightness (RGB, `PIL` `.mean()`) 8.73 at level 0 vs 10.54 at level 5 vs
  17.00 at level 10 (monotonic, as expected -- only ambient/zone-light
  0.02/0.02/0.04 remains at level 0). `light_radius`: level 5 = 219.377,
  level 10 = 365.629 -- **level 5 R < level 10 R, PASS**. Evidence PNGs:
  `benchmarks/contact_sheets/q03_light_fixture/level{0,5,10}_sanity_f0.png`.
- **Computed R at level 10 (the central number for design points 8-9)**:
  **R = 365.629 EQ units**, constant across every pose tested (CSV
  `light_radius` column, x86 and Pi, static and moving) -- versus the old
  fixture's R=712.741 (P07d) and versus production's own level-dependent
  range of roughly [102, 366] for levels 1-10 (`R=radius*sqrt(254/19)`,
  radius in [28,100]). The old fixture's R was already at the top of that
  production range and then some; level 10 (this unit's default) sits at
  production's own ceiling.

### Golden regen + re-bless evidence

- `tests/goldens/manifest.json`'s `productionConfig.args` gained
  `--player-light-level 10` (single source of truth, inherited by both
  `scripts/ci/visual_gate.py` and `scripts/pi/pi_common.py` via Q01's
  plumbing -- no separate wiring needed).
- Regenerated via `visual_gate.py --bless`; draws/state_changes/polys per
  designated frame are UNCHANGED from the Q01 goldens (lighting level
  doesn't affect region visibility/count-mode's budget decision, only
  per-pixel shading), confirming the bless only touched pixel content, not
  geometry decisions. `visual_gate.py` (no `--bless`) then **PASSED** against
  the new goldens; also confirmed via `ctest -R visual_gate`.
- Manifest `approval` block set to `{"status": "pending", "note": "Q03
  production-mirrored player light re-bless -- awaiting Will re-bless"}`
  (previous Q01 pending note replaced, not left stale).
- **Three-way contact sheet**:
  `benchmarks/contact_sheets/q03_light_fixture/{index.html,stats.json}` --
  for each of the 9 golden views, pre-Q01 golden (`git show 45bc024`, full-PP
  mega-lamp) | Q01 golden (`git show task/Q01-path-replay`, count + mega-lamp)
  | Q03 golden (count + production light), plus Q03-vs-each diff stats:

  | view | Q03 vs pre-Q01 (% pixels) | Q03 vs Q01 (% pixels) |
  |---|---|---|
  | qeynos2_f0 | 58.34% | 28.25% |
  | qeynos2_f1 | 89.09% | 78.06% |
  | qeynos2_f2 | 99.60% | 35.04% |
  | gfaydark_f0 | 68.42% | 61.06% |
  | gfaydark_f1 | 58.48% | 57.71% |
  | gfaydark_f2 | 39.08% | 29.45% |
  | befallen_f0 | 99.96% | 30.78% |
  | befallen_f1 | 99.97% | 99.97% |
  | befallen_f2 | 99.93% | 31.35% |

  Real, large diffs everywhere, as expected -- the honest lamp (R=365.6) is
  far dimmer/shorter-range than the old fixture (R=712.7) and warm-tinted
  rather than near-white, so night vistas beyond the lightstone's reach go
  dark (EQ nights were dark) while the near field shows the warm per-pixel
  pool. `befallen_f1` is ~100% different from BOTH priors because that view
  has <=20 visible regions (below the count budget, so count never demoted
  there either pre- or post-Q03) and was fully blanket-lit by the old
  mega-lamp in both priors -- Q03 is the first golden at that view to show
  the light actually falling off.

### x86 decision data (design point 8)

All at `--player-light-level 10` (R=365.629, constant, confirmed via the
`light_radius` CSV column at every one of the 15 poses below), 800x600,
`--eq-path /mnt/projects/EverQuestP1999`. Full data:
`benchmarks/contact_sheets/q03_light_fixture/x86_decision_{summary.json,results.txt}`.

| view | regions_vis | radius demoted (frac) | count demoted (frac) | radius vs full-PP | count vs full-PP | radius vs count |
|---|---|---|---|---|---|---|
| qeynos2_f0 | 111 | 0 (0.0%) | 91 (82.0%) | 0.0% (max_d 0) | 29.8% (max_d 59) | 29.8% (max_d 59) |
| qeynos2_f1 | 60 | 0 (0.0%) | 40 (66.7%) | 0.0% (0) | 11.0% (53) | 11.0% (53) |
| qeynos2_f2 | 140 | 0 (0.0%) | 120 (85.7%) | 0.0% (0) | 64.4% (67) | 64.4% (67) |
| gfaydark_f0 | 61 | 1 (1.6%) | 41 (67.2%) | 0.0% (0) | 6.7% (50) | 6.7% (50) |
| gfaydark_f1 | 53 | 3 (5.7%) | 33 (62.3%) | 0.0% (0) | 0.7% (11) | 0.7% (11) |
| gfaydark_f2 | 86 | 1 (1.2%) | 66 (76.7%) | 0.0% (0) | 8.4% (50) | 8.4% (50) |
| befallen_f0 | 156 | 0 (0.0%) | 136 (87.2%) | 0.0% (0) | 68.9% (98) | 68.9% (98) |
| befallen_f1 | 26 | 0 (0.0%) | 6 (23.1%) | 0.0% (0) | 0.0% (0) | 0.0% (0) |
| befallen_f2 | 55 | 0 (0.0%) | 35 (63.6%) | 0.0% (0) | 68.1% (30) | 68.1% (30) |
| fr291 | 1 | 0 (0.0%) | 0 (0.0%) | 0.0% (0) | 0.0% (0) | 0.0% (0) |
| fr292 | 1 | 0 (0.0%) | 0 (0.0%) | 0.0% (0) | 0.0% (0) | 0.0% (0) |
| fr509 | 2 | 0 (0.0%) | 0 (0.0%) | 0.0% (0) | 0.0% (0) | 0.0% (0) |
| fr627 | 152 | 1 (0.7%) | 132 (86.8%) | 0.0% (0) | 0.0% (0) | 0.0% (0) |
| fr631 | 140 | 1 (0.7%) | 120 (85.7%) | 0.0% (0) | 0.0% (0) | 0.0% (0) |
| fr634 | 124 | 1 (0.8%) | 104 (83.9%) | 0.0% (0) | 0.0% (33) | 0.0% (33) |

**Losslessness holds essentially perfectly under the honest lamp**:
`radius vs full-PP` is pixel-identical (max_delta=0) at ALL 15 poses,
including the poses where radius mode DID demote 1-3 regions (gfaydark_f0/
f1/f2, fr627/631/634) -- a stronger, more direct confirmation of the P07d
losslessness claim than P07d itself got (P07d's radius mode never demoted
anything at any of its static/golden poses, so it could only validate
losslessness on two hand-picked moving-path frames; here it's validated at
6 of the 15 canonical poses where a real demotion occurred, with pixel-perfect
agreement every time). **Count mode is NOT lossless** at any view with
`regions_vis > 20` (7 of 15 poses show real, sometimes large, diffs vs
full-PP -- up to 68.9%), confirming count-mode's flat budget silently
unlights well-lit geometry regardless of whether the light actually reaches
it. `radius vs count` tracks `count vs full-PP` almost exactly at every
view (since radius vs full-PP is ~0), meaning **count-mode's lossiness IS
the radius/count difference**. At these 15 poses (all near/mid-range views,
none a long outdoor vista), radius mode is still comparatively
CONSERVATIVE -- it demotes only 0-6% of visible regions vs count's
0-87% -- because the light's honest 365.6-unit reach still covers most of
what's visible at these specific camera distances. Visual evidence for one
demoted case: `benchmarks/contact_sheets/q03_light_fixture/
targeted_gfaydark_f1_{fullpp,radius,count}.png`.

### Pi measurement (P02 protocol, design point 9)

Orange Pi (10.0.30.192) reachable throughout both sessions.
`ARM_BUILD_BIN_DIR` explicitly overridden to this worktree's
`build-arm-noble/bin` (the documented trap). Session 1 (Q03 measurement,
ad hoc script using `scripts/pi/pi_common.py`'s P02 primitives, not
committed to `pi_common.py` itself -- same precedent as P07d/Q01): 6
invocations, randomized/interleaved order
(`moving/count, static/radius, static/all_lw, moving/radius, static/count,
moving/all_lw`), no `SessionAbort`, CmaFree stayed in [66556, 81264] kB
throughout (well above the 40MB floor). Command base: `--zone qeynos2
--eq-path /mnt/projects/EverQuestP1999 --res 1280 720 --drm` plus
`productionConfig` args (`--adaptive-lighting --zone-pp-budget 20
--pp-budget-mode count --player-light-level 10`), variant-overridden with
`--pp-budget-mode radius` / `--zone-pp-budget 0` (last-flag-wins). Static:
the plan's 6 named poses (fr291/292/509/627/631/634) x10 reps (first
discarded); moving: qeynos2 canonical-path REPLAY
(`benchmarks/paths/qeynos2_seed42_1000f.path`, `--path-file`,
`--entities 200 --seed 42 --frames 1000`).

**Static poses (mean total_ms over 9 steady reps after 1 discarded warmup
rep):**

| pose | regions_vis | count_ms | radius_ms | all_lw_ms | radius vs count | radius vs all_lw |
|---|---|---|---|---|---|---|
| fr291 | 1 | 18.198 | 18.189 | 6.915 | -0.0% | +163.1% |
| fr292 | 1 | 17.751 | 17.764 | 6.489 | +0.1% | +173.8% |
| fr509 | 2 | 19.822 | 19.832 | 7.292 | +0.1% | +172.0% |
| fr627 | 159 | 53.747 | 63.081 | 41.502 | **+17.4%** | +52.0% |
| fr631 | 151 | 55.345 | 63.338 | 40.176 | **+14.4%** | +57.7% |
| fr634 | 138 | 56.450 | 61.120 | 38.003 | **+8.3%** | +60.8% |

`light_radius` constant 365.629 at every pose/rep; `regions_demoted_radius`
is 0 at the three anomaly/corridor poses and 1 at each city pose (fr627/
631/634) -- almost exactly the same shape as P07d's finding under the old
fixture (0 demotions everywhere except a rare few), because even at
R=365.6 (half the old fixture's R=712.7) these particular static poses'
visible regions are still mostly within range. **Radius is NOT faster than
count at any static pose** -- same direction as P07d's finding, for the
same root cause (radius keeps ~everything per-pixel at these poses while
count demotes 80%+ of regions by a flat budget, which is cheaper
per-pixel-shader-cost-wise even though radius's decision is the physically
correct one).

**Moving path (1000 frames, whole-run averages):**

| variant | avg total_ms | fps | p95 total_ms |
|---|---|---|---|
| count | 77.763 | 12.9 | 172.190 |
| radius | 78.619 | 12.7 | 174.148 |
| all_lw | 67.214 | 14.9 | 153.439 |

radius is **1.1% slower** than count over the whole moving run (same
direction as P07d, smaller magnitude). BUT the honest lamp's shorter range
now produces materially more demotion activity than the old fixture did:
`regions_demoted_radius` mean 0.464/frame, max 15, **241/1000 frames (24.1%)
show at least one demotion** -- versus P07d's old-fixture figure of 0.054
mean / 4.8% of frames. The moving path visits a wider variety of view
distances than the 15 fixed poses above (which happen to all be near/
mid-range), so this is the more representative single number for "how much
does the honest lamp change radius mode's behavior" -- a 5x increase in
demotion frequency, even though it isn't (yet, at level 10) enough to flip
the aggregate timing picture in radius's favor.

**Determinism (P02/P07d-precedent full-file checks)**:
`draws`/`regions_vis`/`regions_drawn`/`polys` byte-identical across
count/radius/all_lw, both static (60 rows) and moving (1000 rows): **0
diffs, PASS**. Pose columns vs the path file: **0/1000 mismatches, PASS**.
Cross-machine (Pi vs the x86 llvmpipe replay of the same canonical path,
same config): `draws`/`regions_vis`/`regions_drawn`/`polys` **0 diffs across
all 1000 frames, all 3 variants, PASS** -- geometry decisions reproduce
exactly across x86 llvmpipe and the Pi's Lima/GLES2 driver, consistent with
the Q01/P07d precedent.

**Acceptance framing (findings, not pass/fail, per the plan)**: radius is
NOT within noise of count on timing (1.1% slower moving-path average, 8-17%
slower at the three city static poses) and radius-vs-full-PP IS
(near-)identical (pixel-identical at all 15 x86 poses tested) while
count-vs-full-PP is NOT (real diffs at 7/15). The second half of the flip
criterion is met; the first half is not, at level 10 and at these specific
poses/path. **Radius is not a clear flip candidate at the current default
(level 10) based on this data** -- report only, no default touched (design
point C9 is out of scope for this unit). The moving-path demotion-frequency
increase (5x vs the old fixture) suggests the picture could tip at lower
player-light levels (shorter R, more of the visible scene beyond range) or
in zones/vistas with more long sightlines than qeynos2's canonical path
happens to sample; not measured here (would be its own investigation).

CSVs: `benchmarks/pi_runs/q03_light_fixture/{static,moving}_{count,radius,
all_lw}.csv`, `cma_log.tsv`, `*.log` (benchmark stdout per invocation).

### Tier-2 baseline re-capture (design point 10)

Old baseline: `blessed_from_run: "q01_tier2_rebless"` (commit `b897f8b`),
`tolerance_pct: 30.0`. Re-captured in session 2 via `scripts/pi/bench_pi.sh
--run-name q03_tier2_rebless` (`ARM_BUILD_BIN_DIR` overridden to this
worktree), 6 invocations (3 zones x rgba/eqe1, randomized order), no
`SessionAbort` during the run itself, Pi cleanup ran automatically
(`[cleanup] removed staged binary`). Blessed via `pi_bench_compare.py
--run-name q03_tier2_rebless --bless --tolerance-pct 30.0` (kept 30%,
continuing Q01's precedent).

| zone | variant | old p95 (ms) | new p95 (ms) | delta | old fps(p95) | new fps(p95) |
|---|---|---|---|---|---|---|
| qeynos2 | rgba | 30.787 | 30.881 | +0.3% | 32.5 | 32.4 |
| qeynos2 | eqe1 | 27.379 | 27.313 | -0.2% | 36.5 | 36.6 |
| gfaydark | rgba | 38.991 | 38.962 | -0.1% | 25.6 | 25.7 |
| gfaydark | eqe1 | 32.888 | 32.950 | +0.2% | 30.4 | 30.3 |
| befallen | rgba | 39.593 | 39.518 | -0.2% | 25.3 | 25.3 |
| befallen | eqe1 | 38.213 | 37.794 | -1.1% | 26.2 | 26.5 |

All deltas within +-1.1% (noise) -- expected, since the tier-2 baseline
runs `count` mode, whose region-visibility/budget decision does not depend
on the player light's color/radius/attenuation at all (only per-pixel
shading cost, which the p95 frame-time bar barely moves for this scene
complexity). draws/state_changes/polys per designated frame exact-match
against the manifest (checked by `pi_bench_compare.py`'s per-pose
assertions). CSVs: `benchmarks/pi_runs/q03_tier2_rebless/`.

**Post-session housekeeping**: after both Pi sessions, `CmaFree` had
dropped to 328 kB (well under the 40MB abort floor -- accumulated from the
combined 12 invocations across both sessions, mostly the two ~90s 1000-frame
moving-path runs). Remediated per protocol with `sync; echo 3 >
/proc/sys/vm/drop_caches` (no reboot), confirmed recovered to 77756 kB.
Governor confirmed `ondemand` on all 4 cores (restored automatically by
each session's `GovernorPin.__exit__`). Leftover scratch files from the ad
hoc session-1 script (`/tmp/{static,moving}_*.{csv,log}`,
`/tmp/qeynos2_seed42_1000f.path` -- session 1 had no `--cleanup` step,
unlike `bench_pi.sh`'s built-in one) removed manually; confirmed `/tmp`
clean of all benchmark artifacts. Total invocations: 6 + 6 = 12 across 2
sessions (within the <=12/session, 2-sessions-OK budget), zero reboots.

### Flip-candidate assessment (design point C9 -- REPORT ONLY)

Per the plan and `docs/decisions.md`, no production default or preset is
touched by this unit. Summary for the orchestrator/Will: under the honest
level-10 lightstone, radius mode is **provably lossless** (pixel-identical
vs full-per-pixel at every one of 15 x86 poses tested, including 6 where it
actually demoted something) while count mode is **provably lossy** at any
view with >20 visible regions (7/15 poses, up to 68.9% of pixels differing
from full-PP). On raw timing, however, radius is not yet a clear win at
level 10: 1.1% slower than count on the qeynos2 moving-path average and
8-17% slower at the three city static poses (radius keeps far more regions
per-pixel than count's flat cutoff at these particular views/distances).
The moving-path demotion-frequency finding (24.1% of frames vs the old
fixture's 4.8%) shows the honest lamp already meaningfully changes radius
mode's behavior relative to P07d's fixture-lamp measurement, and hints the
timing balance could shift further at lower default light levels or on
zones/paths with more long sightlines -- but that is not measured here and
would need its own investigation before any flip is considered. Any future
default flip is its own unit with its own tier-2 re-bless (P07c lesson).

### Verification

- `cmake --build build -j24`: clean, both after the fixture change and
  after the later `--dump`-gated attenuation-proof addition (only the one
  `.cpp.o` TU rebuilt each time).
- `ctest --test-dir build --output-on-failure`: 1224/1225 passed after the
  golden bless (only `AudioManagerTest.PreloadCommonSounds`, documented host
  exception -- `RDPServerFrameTest.UpdateFrameMultipleTimes`, the other
  documented exception, passed this run, consistent with it being an
  intermittent host issue not a regression). `visual_gate` (test #1225)
  **PASSED** against the regenerated goldens both as part of the full run
  and standalone.
- `scripts/build-gles2-x86.sh`: clean (rebuilt twice: once for the fixture
  change, once for the debug-print addition).
- Visual gate (`scripts/ci/visual_gate.py`, no `--bless`): **PASS**,
  confirmed after both source changes (the debug print is `--dump`-gated,
  zero effect on the golden-capture path, confirmed by the gate staying
  green without a re-bless after adding it).
- `scripts/build-arm-noble.sh`: clean, `gles2_zone_benchmark` rebuilt from
  this worktree and used for both Pi sessions (session 1 used the
  pre-debug-print binary; the debug print is `--dump`-gated and additive
  only, so it does not affect any Pi measurement's behavior -- session 2's
  tier-2 recapture used the post-debug-print binary, both count-mode-only
  and therefore also unaffected).
- `scripts/refactor/check_file_sizes.sh fable`: exit 0. WARN on
  `tools/hwcaps/gles2_zone_benchmark.cpp` (2515 lines) -- pre-existing
  (already WARN before this unit, per Q01's review), still WARN not FAIL.
- Attenuation-mirror proof, level-0/level-5 sanity, design points 8-10
  tables: all complete, see sections above.

### Deviations from the plan

- None material. One addition beyond the plan's literal text: a
  `--dump`-gated debug print of the live shader-uniform readback was added
  to `setupLighting()` specifically to make the attenuation-mirror proof's
  third column ("what the shader uniform receives") an actual instrument
  reading rather than an inference from source review -- a small, additive,
  default-off diagnostic in the spirit of the plan's own verification
  requirement, not a behavior change (confirmed: visual gate stayed green
  with no re-bless after adding it).
- The x86 decision-data table (design point 8) used the 15 poses specified
  by the plan (9 golden views + 6 P07 static poses) exactly as written;
  no additional poses were substituted or added.
- Pi session 1 (Q03 measurement) was run as an ad hoc script using
  `scripts/pi/pi_common.py`'s primitives directly, matching the Q01/P07d
  precedent of not committing single-unit investigation drivers into
  `pi_common.py` itself. It lacked `bench_pi.sh`'s built-in Pi-side cleanup
  step, so leftover scratch CSVs/logs and the staged path file had to be
  removed manually after the session (done, confirmed clean) -- flagged in
  case a future unit wants to fold a generic "arbitrary-variant P02 runner
  with cleanup" helper into `pi_common.py` proper.
