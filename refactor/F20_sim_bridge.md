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

(executor fills in)
