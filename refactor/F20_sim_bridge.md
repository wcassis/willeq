# F20 — Split simulation bridge out of irrlicht_renderer.cpp

Move the renderer↔SimulationWorker marshaling cluster out of
`src/client/graphics/irrlicht_renderer.cpp` (10,790 lines). Pure move — no
behavior change, no interface change, no reformatting.

## Scope (orig 8012–9054 @ a0c02cd, ~1,040 lines. Enumerate by NAME.)

Move to new `src/client/graphics/renderer_sim_bridge.cpp` the contiguous
simulation-bridge cluster: `buildSimulationZoneData`, the SimulationWorker
start/stop/registration methods, `postSimulationInput` (~340 lines), and
`applySimulationOutput` (~430 lines), plus contiguous sim-data update
helpers in that region.

In-range non-sim-bridge methods (judgment): leave + document.

## Steps

1. Locate the cluster by grep; produce the mapping in the review BEFORE
   moving.
2. Replicate namespace wrapper + include patterns (F14–F21 reviews).
   simulation_worker.h types flow through this file heavily — full includes
   likely needed.
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local statics: usual rule.
5. Add to `GRAPHICS_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/renderer_sim_bridge.cpp`
  — ~1,100 expected: WARN accepted, must not FAIL.
- irrlicht_renderer.cpp shrinks by ~1,000+ lines.

## Review

**Cluster boundaries (verified by grep of `IrrlichtRenderer::` definitions,
irrlicht_renderer.cpp @ merge-base commit `443fea3eb300207f827ac222a46c2086f1b936ea`
with `fable`, 10,790 lines before this move — `git merge-base fable HEAD`
confirms `fable` did not advance during this unit's work):**

Moved to `src/client/graphics/renderer_sim_bridge.cpp`, pre-move lines
7398–8443 (a single contiguous block, no `#ifdef` guards), 1,046 lines. 9
`IrrlichtRenderer::` methods plus their `// SimulationWorker Integration`
section-header comment:

- `buildSimulationZoneData()` (~155 lines) — builds the `SimulationZoneData`
  snapshot (BSP tree, region bounds, PVS lookup tables, occluder sets) handed
  to the worker at zone-load/zone-transition time.
- `startSimulationWorkerEarly()` — creates the worker early during loading and
  installs zone data via `buildSimulationZoneData()`.
- `registerSimulationWorkerTreeData()`, `registerSimulationWorkerVertexAnimData()`
  — the "registration" methods named in the plan; push tree/vertex-anim data
  into the worker.
- `stopSimulationWorker()` — worker teardown for zone transition (the
  "stop" method named in the plan).
- `postSimulationInput(float)` (~340 lines) — drains per-frame camera,
  entity-visual, particle/weather/boids/tumbleweed command state into a
  `SimulationInput` and posts it to the worker.
- `applySimulationResults()` — thin wrapper: swaps the worker's latest
  `SimulationOutput` and forwards to `applySimulationOutput`.
- `applySimulationOutput(const SimulationOutput&)` (~430 lines) — applies PVS
  visibility, mesh-load-queue, portal-occlusion, entity-visual, sky, weather,
  particle, boids, and tumbleweed results back onto renderer state.
- `getSimWorkerDebugInfo() const` — a small diagnostics accessor
  (`std::vector<std::string>` of worker stats: running state, frames
  computed/skipped, timing, offload summary) sitting immediately after
  `applySimulationOutput` and immediately before `processFrame` (a different,
  out-of-scope cluster). Not explicitly named in the plan's method list, but
  judged in-scope as a "contiguous sim-data helper in that region" — it reads
  only `simulationWorker_`/`treeManager_`/`vertexAnimatedMeshes_` state and
  has no other logical home; moving it avoids leaving an orphaned 29-line
  single-method island between two now-separated clusters.

**In-range non-sim-bridge methods, left in place per "when in doubt, leave":**
none — the plan's named cluster runs edge-to-edge from the section-header
comment to the boundary with `processFrame`; nothing sat between the named
methods that needed a leave/document call.

**Guard handling:** no `#ifdef`/`#endif` in the moved range — nothing to
preserve beyond ordinary verbatim copy (confirmed by grepping preprocessor
directives across the range before moving).

**Namespace + include handling (same pattern as F14–F16/F19/F21):**
- New file wraps its body in `namespace EQT { namespace Graphics { ... } }`,
  matching `irrlicht_renderer.cpp`.
- `simulation_worker.h` (and thus `SimulationZoneData`/`SimulationInput`/
  `SimulationOutput`) is already a full (non-forward-declared) include in
  `irrlicht_renderer.h` — no additional include needed for the core
  simulation-worker types.
- `client/graphics/irrlicht_renderer.h` — the class declaration itself.
- `client/graphics/constrained_mesh_cache.h` — `irrlicht_renderer.h` only
  forward-declares `ConstrainedMeshCache` (same gap F19 hit for
  `setupMinimalZoneCollision`); `buildSimulationZoneData()` doesn't actually
  call through it directly in this cluster, but the type is referenced in a
  member signature path exercised during the build — added per the
  belt-and-suspenders F19 pattern and confirmed necessary by the first build
  attempt.
- `client/graphics/weather_effects_controller.h`, `client/graphics/spell_visual_fx.h`,
  `client/graphics/sky_renderer.h` — `irrlicht_renderer.h` only
  forward-declares `WeatherEffectsController`, `EQ::SpellVisualFX`, and
  `SkyRenderer`; `postSimulationInput`/`applySimulationOutput` call methods on
  all three (`weatherEffects_->getLightningTimer()` etc.,
  `spellVisualFX_->isWorkerDriven()`/`applyWorkerResults()`,
  `skyRenderer_->isInitialized()`/`applyState()`, plus the nested
  `SkyRenderer::SkyState`/`WeatherEffectsController::WeatherEffectsState`
  types). Caught by the first build attempt ("invalid use of incomplete
  type"), fixed by adding all three full includes — the original single TU
  had these transitively via other clusters' code still resident there at the
  time, so the new TU needs them directly.
- `common/logging.h`, `<fmt/format.h>`, `<algorithm>`, `<cmath>`, `<cstdint>`
  — `LOG_INFO`/`LOG_DEBUG`, `fmt::format`, `std::min`/`std::abs`, `uint8_t` —
  full includes the original TU had unconditionally.
- `EntityRenderer`, `Detail::DetailManager`, `AnimatedTreeManager`,
  `CameraController`, particle/boids/tumbleweed/weather-system managers,
  and `RendererConfig` needed no extra include: all are fully (not
  forward-) declared in `irrlicht_renderer.h` or pulled in transitively via
  its existing includes.
- No bridge/intent usage (`bridge_->`, `pushIntent`, `pushEvent`) anywhere in
  this cluster — unlike F19's player-physics cluster, the sim-bridge methods
  don't talk to `GameStateBridge` directly, so no bridge-header includes were
  needed.

**Shared-helper analysis:** no file-local `static`/anonymous-namespace
symbols in `irrlicht_renderer.cpp` (the top-of-file `loadDisplaySettingsFromFile`,
`logDriverDetails`) are referenced anywhere in the moved range — confirmed by
grep before moving. No function-local statics inside the moved methods either.

**Line counts:**
- `irrlicht_renderer.cpp`: 10,790 → 9,744 lines (**-1,046**)
- `renderer_sim_bridge.cpp`: 1,063 lines (new; 1,046 moved lines + 17 lines of
  includes/namespace-wrapper boilerplate)
- Net change: +1,063 / -1,046 = +17 lines total (all boilerplate — no content
  duplicated or dropped)
- Deviation from the plan's "~1,040 lines" scope estimate: matched almost
  exactly (1,046 vs. ~1,040 estimated); deviation from the "~1,100 expected"
  file-size-check estimate: 1,063 actual, under estimate and well under the
  1,500-line WARN ceiling (`check_file_sizes.sh` reports `ok`, not `WARN`).

**Build/test verification:**
- `cmake --build build -j24`: clean after one fix found during the first
  pass (mechanical, not scope creep): missing full includes for
  `ConstrainedMeshCache`, `WeatherEffectsController`, `EQ::SpellVisualFX`, and
  `SkyRenderer` (all forward-declared only in `irrlicht_renderer.h`).
- `ctest --test-dir build`: 1191/1192 run tests passed. The one failure,
  `AudioManagerTest.PreloadCommonSounds`, is the documented pre-existing ALSA
  "cannot find card '0'" environment gap in `refactor/test_baseline.txt`
  (consistent with F02/F14/F15/F19 findings on this host). 9 tests skipped
  (asset/graphics/audio-dependent, pre-existing, unrelated to this unit).
- `git merge-base fable HEAD` → `443fea3eb300207f827ac222a46c2086f1b936ea`,
  confirming no rebase occurred and `fable` did not advance during this
  unit's work; this is the BASE used for the verifier below.
- `python3 scripts/refactor/verify_move_only.py 443fea3eb300207f827ac222a46c2086f1b936ea..HEAD --repo <slot2>`
  (run after committing — the script diffs commits, not the working tree):
  **PASS** ("Boilerplate: +14 / -0 lines (allowed)"). `CMakeLists.txt` (the
  additive `GRAPHICS_SOURCES` line) and `refactor/F20_sim_bridge.md` (this
  review) reported separately as non-C++, not verified — expected for a
  build-file edit and doc update.
- `bash scripts/refactor/check_file_sizes.sh --files <slot2>/renderer_sim_bridge.cpp`:
  **ok** (1,063 lines, well under the 1,500-line ceiling).

**Deviations from plan:** none in moved code itself (verbatim, no
reformatting, no scope creep). The only judgment call was including
`getSimWorkerDebugInfo()` in the moved cluster — not explicitly named in the
plan's method list, but a contiguous sim-data helper with no other logical
home, per the plan's own "contiguous sim-data update helpers in that region"
language.
