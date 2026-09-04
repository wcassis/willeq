# T330 — Four debug hotkeys have never done anything: add the four switch arms (B043)

Class A, found by L04. Small. Runs in slot3 alongside T328 (slot1, eqemu
lock) and T329 (slot2, `irrlicht_bridge.cpp`). Territory is disjoint from
both.

## Measured at `347259db`

- `RendererEventReceiver::OnEvent` queues `RendererAction::ToggleWireframe`,
  `ToggleHUD`, `ToggleNameTags` (`src/client/graphics/renderer_event_receiver.cpp`);
  `ToggleOldModels` is in the same enum (`include/client/graphics/renderer_types.h`).
- `IrrlichtRenderer::processCommonInput()` (`src/client/graphics/renderer_frame_input.cpp`)
  has NO arm for any of the four → `default: break;`.
- Implementations exist and are live: `DiagnosticsComponent::toggleWireframe`,
  `toggleHUD`, `toggleNameTags`, `toggleOldModels`, reachable as
  `diagnostics_->toggleX()` inside `IrrlichtRenderer`.
- The second route (`GraphicalMode::toggleWireframe/toggleHUD/toggleNameTags`
  → `output::IOutputRenderer*` empty defaults) is ALSO dead because
  `IrrlichtRenderer` does not implement that interface. Out of scope here;
  leave it, note it.

## Steps

1. Four arms in `processCommonInput`'s switch, mirroring the
   `ToggleZoneLights` / `CycleObjectLights` arms next to them; one line
   each (`diagnostics_->toggleWireframe(); break;` …). If `ToggleOldModels`
   is not queued by the event receiver, check `hotkey_manager.cpp` for
   whether the binding exists; add the arm anyway (the enum value is live).
2. Test: `tests/test_debug_hotkey_arms.cpp` — the existing frame-input
   tests show how to push a `RendererAction` through the receiver queue and
   drive `processCommonInput` (grep `tests/` for `ToggleZoneLights`); assert
   each of the four flips its observable state (`ctx_.wireframeMode`,
   `isUsingOldModels()`, the HUD/name-tag flags — read the diagnostics
   header for the getters). ⚠ `tests/test_ui_hide_command.cpp` and
   `tests/test_static_ui_scroll.cpp` read `renderer_frame_input.cpp` AS
   TEXT — run them; do not restructure the switch.
3. BOARD: strike B043 with the commit; the `GraphicalMode` route stays
   open as a one-line note under B043's strike (class B, "interface not
   implemented").

## Territory

`src/client/graphics/renderer_frame_input.cpp` (four arms), the new test +
its cmake (`tests/cmake/renderer_guards.cmake` or wherever the frame-input
tests live — grep), `refactor/BOARD.md`. A file under `src/client/graphics`
changes: visual gate + ARM standalone are NOT skippable.

## Verification

Build, full ctest -j6 (ALSA quirk only), `build-gles2-x86.sh`, visual gate
standalone (goldens untouched), `build-arm-noble.sh`, `check_file_sizes.sh --all`.
