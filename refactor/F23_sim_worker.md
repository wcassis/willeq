# F23 — Split simulation_worker.cpp compute passes into sim_* TUs

Split `src/client/graphics/simulation_worker.cpp` (4,356 lines) into
per-domain compute-pass TUs. Structurally the cleanest split in the codebase:
every pass reads `SimulationInput` and writes `SimulationOutput`, methods all
belong to `SimulationWorker`. Pure move — no behavior change, no interface
change, no reformatting.

## Scope — six new files under `src/client/graphics/`

Core lifecycle STAYS in simulation_worker.cpp: ctor/dtor, start/stop,
setZoneData / update*Data / clearZoneData, workerLoop, computeOnce/computeAll,
swapAndGetResults.

Compute passes move, grouped by domain (assign each `SimulationWorker::`
method to exactly one file; if a pass doesn't obviously fit, choose the
closest and document the mapping in the review):

| File | Clusters (orig line refs @ a0c02cd) |
|---|---|
| sim_visibility.cpp | region/visibility/portal (477–1082, 2556–2623): calculateRegions, computeVisibility, computePortalVisibility, RegionDepthMap, Object/Light/ObjectLightVisibility, LightSelection, softwareOcclusion; frustum pass (~1479+) |
| sim_animation.cpp | lighting/fire (computeFireFlicker, computeLightAnimations, applyVisionWeather ~1083–1135, 1503–1628) + tree/vertex/detail animation (1136–1478) |
| sim_particles.cpp | particles (1699–2555: computeParticles, allocate/free, random, spawnWeather/SpellParticle) + spell VFX pass (4066–4356) |
| sim_entity_sync.cpp | entity sync/interp/visibility (2624–3117): computeEntitySync, PendingUpdates, Interpolation, Visibility, NameTagVisibility |
| sim_boids.cpp | boids (3118–3570) + tumbleweeds (3571–3847) |
| sim_weather.cpp | weather sim (3848–4065): computeWeather, weatherCheckChange, RollForWeather, GetWindIntensity; sky/weather-state pass (~1629–1698) |

## Steps

1. grep the full `SimulationWorker::` method list; assign every method to
   "stays" or one of the six files; put the complete mapping table in the
   review BEFORE moving.
2. Check the namespace wrapper and include patterns of simulation_worker.cpp;
   replicate per new TU (see F14–F16 reviews for the renderer equivalents).
3. Move verbatim; preserve #ifdef guards exactly. File-local static helpers
   and file-scope constants: move to the TU whose methods use them if
   exclusive; if shared across new TUs, leave in simulation_worker.cpp and
   de-static via a small internal header (document each).
4. Add all six files to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run isolated).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <six abs paths>` — each ok (<1,500).
- simulation_worker.cpp shrinks to roughly its lifecycle core (~600–800 lines).

## Review

(executor fills in)
