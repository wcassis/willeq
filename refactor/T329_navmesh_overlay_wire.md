# T329 — Wire the `NavmeshChanged` bridge arm so the navmesh overlay can draw (B041)

Class A, found by L03. Small. Runs in slot2 alongside L04 (slot3) and T328
(slot1); L04 edits `irrlicht_bridge.cpp` at other lines (`dumpScene`,
`getMemoryReport` call sites) — touch only the `NavmeshChanged` arm.

## Measured at `7d2efe6d`

- `src/client/bridge/irrlicht_bridge.cpp` `IrrlichtBridge::applyEvent`,
  `case state::GameEventType::NavmeshChanged:` — body is one `LOG_TRACE`.
- Payload: `state::NavmeshChangedData` (`include/client/state/event_bus.h`)
  carries the navmesh pointer (`PathfinderNavmesh*`; `{nullptr}` on zone
  leave, the real pointer on load — `src/client/movement/movement_state.cpp`
  publishes both; `src/client/zone/zone_component.cpp` publishes one).
- Sink: `IrrlichtRenderer::setNavmesh(PathfinderNavmesh*)` (inline
  forwarder to `OverlaysComponent::setNavmesh`), zero callers today.
- Consumer: `OverlaysComponent::updateNavmeshOverlay()` returns
  immediately when the pointer is null; Ctrl+N toggles `toggleNavmeshOverlay`.

## Steps

1. The arm: read the payload the way the neighbouring arms read theirs
   (`std::get<…>` / the variant accessor the file uses) and call
   `renderer_->setNavmesh(data.navmesh)`; keep the trace line. Nullptr on
   zone leave is correct and must pass through.
2. Test: `tests/test_navmesh_overlay_wire.cpp` (register next to the
   existing bridge tests — grep `tests/cmake/` for `test_irrlicht_bridge`
   or the nearest `IrrlichtBridge` test and copy its shape): push a
   `NavmeshChanged` event with a non-null `PathfinderNavmesh*` (a stub or a
   default-constructed one if constructible) through `applyEvent`, assert
   the overlays component's navmesh pointer is that value (add a
   `const PathfinderNavmesh* navmesh() const` getter on `OverlaysComponent`
   if none exists — one line); then push `{nullptr}` and assert it clears.
   Also extend `tests/test_reachability_sweep.cpp`'s consumer-arm scan if
   it classifies `LOG_TRACE`-only arms — read it; if it already lists this
   arm as a known no-op, remove it from that list (the assertion gets
   stricter, not weaker).
3. Live proof (owed, not blocking merge): a screenshot of Ctrl+N in a
   navmesh-enabled zone goes to `review/T329/` with a README — ONLY if the
   eqemu lock is free (T328 Leg A holds it now; if held, say so and leave
   the live leg on the board as owed).
4. BOARD: strike B041 with the commit; if the live leg is owed, say so in
   the strike.

## Territory

`src/client/bridge/irrlicht_bridge.cpp` (one arm), `include/client/graphics/overlays/overlays_component.h`
(+ one getter if needed), `tests/test_navmesh_overlay_wire.cpp` + its cmake,
`tests/test_reachability_sweep.cpp` (list edit only, if applicable),
`refactor/BOARD.md`, `review/T329/`. Nothing under `src/client/graphics` other
than the header getter — if that header changes, run the visual gate and ARM
standalone.

## Verification

Build, full ctest -j6 (ALSA quirk only), `check_file_sizes.sh --all`; visual
gate + ARM only if a graphics header changed.
