# L07 — `SimBridgeComponent` (family: `renderer_sim_bridge.cpp`, 1,113 lines, 9 bodies — the simulation-worker input/output bridge)

Batch: `refactor/L_renderer_component_extraction.md` (corrections 1–23 apply).
Wave 2, two-wide with L06. Measured at `1ea4a59c`.

Bodies and callers (word grep; the compiler confirms):
`applySimulationOutput` (`renderer_zone_stream.cpp`, `renderer_zone_merge.cpp`,
`renderer_zone_load_env.cpp`), `applySimulationResults` (`renderer_frame.cpp`,
`renderer_frame_input.cpp`; `simulation_worker.cpp` and `weather_system.h`
name it — read: a callback type or a comment), `bindFrameSimulationBuffers`
(`renderer_zone_lifecycle.cpp`, `renderer_zone_load_env.cpp`),
`buildSimulationZoneData` (`renderer_zone_load_env.cpp`),
`getSimWorkerDebugInfo` (`renderer_slash_commands.cpp` — L06's; if L06 has
merged by your tip the caller is `slash/…` and the call is
`owner_.SimBridge().getSimWorkerDebugInfo()` there), `postSimulationInput`
(`renderer_frame.cpp`, `renderer_frame_input.cpp`),
`registerSimulationWorkerVertexAnimData` / `startSimulationWorkerEarly`
(`renderer_init.cpp`), `stopSimulationWorker` (`irrlicht_renderer.cpp`,
`renderer_zone_load.cpp`, `renderer_zone_lifecycle.cpp`). All in-renderer:
zero forwarders expected (name a real external caller or there is none).

Members: 2 single-family (survey) — measure; the rest is the zone blob and
PVS/portal toggles (12 shared) reached via `ctx_`. ⚠ The simulation worker
runs on ANOTHER THREAD: `simWorker_`/`simulationWorker_` ownership, the
`SimulationOutput` lifetime (T116) and the `SimulationOutputLifetime` test
fixtures that are `friend`s of `IrrlichtRenderer` (6 of the 11 friends) —
read `include/client/graphics/simulation_worker.h` and
`tests/test_simulation_output_lifetime.cpp` BEFORE moving anything. If a
fixture reaches a member you move, the fixture is retargeted at the call
site (assertions untouched) and the friend line moves to the component; if
that changes what the test can observe, STOP and report.

Text readers: `tests/test_object_node_texture_guard.cpp:8` names the TU in
a comment — verify; `test_player_camera_component.cpp:122` likewise.

Predicted header delta: −9 declarations −2 members −banners +(scaffolding
4..6) ≈ **−8 or better**.

Territory: `include/client/graphics/sim/sim_bridge_component.h` (new),
`src/client/graphics/sim/` (git mv), `irrlicht_renderer.h`, `renderer_init.cpp`
(ctor line + 2 call sites), root `CMakeLists.txt`, `tests/test_sim_bridge_component.cpp`
+ cmake, `tests/test_simulation_output_lifetime.cpp` (call sites only, if
forced), allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the
renderer TUs named. Visual gate + ARM NOT skippable.

STOP: a `friend` fixture's observable changes; a thread-ownership change
of any kind.
