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

(executor fills in)
