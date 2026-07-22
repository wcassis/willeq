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

**Method boundaries** were determined from the file's own `// ====` section
banners (34 banners, one per logical cluster) rather than guessed from method
names — every `SimulationWorker::` definition sits inside exactly one banner
block, and the banners tile the file with no gaps, confirmed by diffing the
banner-derived line ranges against the full `grep -n "SimulationWorker::"`
method list (57 methods total). Original file state: `simulation_worker.cpp`
@ `a0c02cd`/current `fable` tip, 4,356 lines.

### Method-mapping table

| Method | Disposition |
|---|---|
| `SimulationWorker()`, `~SimulationWorker()` | stays (Constructor/Destructor) |
| `start()`, `stop()` | stays (Thread Lifecycle) |
| `setZoneData`, `updateTreeData`, `updateVertexAnimData`, `updateZoneLightData`, `updateObjectLightData`, `clearZoneData` | stays (Zone Data Registration) |
| `computeOnce`, `postInput`, `swapAndGetResults`, `getDebugInfo` | stays (Per-Frame Protocol) |
| `workerLoop` | stays (Worker Thread Loop) |
| `computeAll` | stays (Compute Functions — dispatcher) |
| `calculateRegions` | sim_visibility.cpp (Region Calculation) |
| `computeVisibility` | sim_visibility.cpp (PVS Visibility Computation) |
| `computePortalVisibility` | sim_visibility.cpp (Portal Visibility Computation) |
| `computeRegionDepthMap` | sim_visibility.cpp (PVS Depth Map) |
| `computeObjectVisibility` | sim_visibility.cpp (Object Visibility Computation) |
| `computeLightVisibility` | sim_visibility.cpp (Zone Light Visibility Computation) |
| `computeObjectLightVisibility` | sim_visibility.cpp (Object Light Visibility Computation) |
| `computeLightSelection` | sim_visibility.cpp (Light Selection Top 8) |
| `testFrustumAABB` | sim_visibility.cpp (Frustum Test Helper — non-contiguous, moved with its own banner block at orig 1475–1498; called from both sim_visibility.cpp and sim_particles.cpp, which is fine since it's a class member declared once in simulation_worker.h, not a file-local static) |
| `computeSoftwareOcclusion` | sim_visibility.cpp (Software Occlusion Culling — non-contiguous, orig 2552–2619) |
| `computeFireFlicker` | sim_animation.cpp (Fire Light Flicker) |
| `computeTreeWindDisplacement`, `computeTreeAnimation` | sim_animation.cpp (Tree Wind Animation) |
| `computeVertexAnimations` | sim_animation.cpp (Vertex Animations) |
| `computeDetailAnimation` | sim_animation.cpp (Detail Wind/Disturbance Animation) |
| `applyVisionWeatherToZoneLights` | sim_animation.cpp (Vision/Weather Zone Light Color Application — non-contiguous, orig 1499–1541, sits between the Frustum Helper and Zone Light Animation banners) |
| `computeLightAnimations` | sim_animation.cpp (Zone Light Animation Computation) |
| `computeSkyState` | sim_weather.cpp (Sky State Computation) |
| `computeWeatherEffectsState` | sim_weather.cpp (Weather Effects State Computation) |
| `computeParticles` | sim_particles.cpp (Particle System) |
| `allocateParticle`, `freeParticle`, `particleRandomFloat`, `particleRandomInt` | sim_particles.cpp (Particle System) |
| `spawnWeatherParticle`, `spawnSpellParticle` | sim_particles.cpp (Particle System) |
| `computeSpellVFX` | sim_particles.cpp (Spell VFX Computation — non-contiguous, orig 4062–4354, moved after the Particle System block, in file order) |
| `computeEntitySync` | sim_entity_sync.cpp (Entity Sync) |
| `computeEntityPendingUpdates` | sim_entity_sync.cpp (Entity Pending Updates) |
| `computeEntityInterpolation` | sim_entity_sync.cpp (Entity Interpolation) |
| `computeEntityVisibility` | sim_entity_sync.cpp (Entity Visibility) |
| `computeNameTagVisibility` | sim_entity_sync.cpp (Name Tag Visibility) |
| `boidsRandomFloat`, `boidsGetRandomSpawnPosition`, `boidsGetTypesForBiome` | sim_boids.cpp (Boids Simulation) |
| `computeBoids` | sim_boids.cpp (Boids Simulation) |
| `twRandomFloat` | sim_boids.cpp (Tumbleweed Simulation) |
| `computeTumbleweeds` | sim_boids.cpp (Tumbleweed Simulation) |
| `computeWeather` | sim_weather.cpp (Weather System) |
| `weatherCheckChange`, `weatherRollForWeather`, `weatherGetWindIntensity` | sim_weather.cpp (Weather System) |

### Static-helper / file-scope-constant disposition

No file-local `static` free functions or file-scope constants exist outside
the class in `simulation_worker.cpp` (confirmed via `grep "^\s*static\s"` and
`grep "^\s*constexpr\|^\s*const "` at file scope — all hits are inside
method bodies). `testFrustumAABB` is a `SimulationWorker::` member, not a
file-local static, so calling it from both `sim_visibility.cpp` (its home)
and `sim_particles.cpp` (`computeParticles`, orig line 2041) needed no
de-static-ing or header change — the declaration in `simulation_worker.h`
already makes it visible to any TU that includes the header. Two file-scope
`using` declarations moved with their sole-consuming cluster: `using namespace
Environment;` (orig line 1697) → `sim_particles.cpp`; `using TreeWeatherType
= ::EQT::Graphics::WeatherType;` (orig line 3846) → `sim_weather.cpp`.

### Include / namespace pattern

Followed the F14–F16 precedent: each new TU opens `namespace EQT { namespace
Graphics { ... } }` once (matching the single wrapper `simulation_worker.cpp`
already used) and replicates the full original top-of-file include block
(`simulation_worker.h`, `portal_system.h`, `eq/wld_loader.h`,
`environment/unified_particle.h`, `environment/spell_particle_types.h`,
`hc_map.h`, `common/logging.h`, `<algorithm>`, `<cmath>`, `<random>`) rather
than hand-trimming to only the types each cluster touches. Two of those
headers (`wld_loader.h`, `hc_map.h`) turn out to be unused by any cluster
(no `WLD*`/`HCMap::` symbol reference in the whole original file — `HCMap`
appears only in comments) but were already vestigial in the original TU, so
carrying them into every split file is a no-op, not a new deviation; trimming
them was out of scope for a verbatim move. `verify_move_only.py`'s
boilerplate allowlist (`#include`, namespace-open/close, comments) means
these extra include lines don't register as substantive additions. No new
includes were required beyond the original set — every type referenced in
every cluster (BSP tree, Portal, EmitterConfig, Environment::CreatureType,
glm types, irr:: types) already came in transitively via
`simulation_worker.h` or the replicated includes.

### Line counts (final)

| File | Lines |
|---|---|
| `simulation_worker.cpp` (lifecycle core, was 4,356) | 474 |
| `sim_visibility.cpp` | 714 |
| `sim_animation.cpp` | 538 |
| `sim_weather.cpp` | 305 |
| `sim_particles.cpp` | 1,168 |
| `sim_entity_sync.cpp` | 510 |
| `sim_boids.cpp` | 743 |
| **Total (incl. boilerplate)** | 4,452 |

(Total exceeds the original 4,356 by 96 lines — entirely replicated
include/namespace boilerplate across 6 new files, ~16 lines each; zero
substantive lines added, confirmed by the move-only verifier below.)

### Verifier summary

- `cmake --build build -j24`: clean, no warnings from the new/changed files.
- `ctest --test-dir build`: 1191/1192 passed; the 1 failure
  (`AudioManagerTest.PreloadCommonSounds`, ALSA card-0 resolution) is the
  documented pre-existing host-environment exception in
  `refactor/test_baseline.txt`. All graphics/simulation-adjacent tests
  (`test_graphics`, `test_zoning_graphics_integration`, etc.) passed.
- `verify_move_only.py` (BASE = `git merge-base fable HEAD` =
  `ebbf816dbd5184d6a9b098693f658ca23907ff16`, since `fable` advanced past our
  branch point via other executors' work on `eq.cpp`): **PASS**, move-only
  modulo boilerplate (see commit for exact counts).
- `check_file_sizes.sh` on all six new files: all `ok`, largest is
  `sim_particles.cpp` at 1,168 lines (< 1,500 ceiling).

### Deviations

None beyond the documented include-replication note above (not a deviation
from "verbatim," since no line of moved code was altered — only new
boilerplate lines were added, which the verifier allowlists explicitly).
