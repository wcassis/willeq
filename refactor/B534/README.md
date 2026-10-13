# B534 (issue #181): the output-layer camera route is a second, stale camera vocabulary

Card: B534. Branch `task/kb-b534-output-camera-route`, base `0136137b`
(origin/fable). 2026-10-11, class B hygiene: dead-route removal only, no
behaviour change — the live camera route (F9 / `/camera`, the official
six-camera set, T461) is untouched and pinned by its own tests.

## Background

The client has ONE live camera vocabulary: `EQT::Graphics::CameraMode`
(`include/client/graphics/player/camera_mode.h`, the official six modes)
owned by `PlayerCameraComponent`. The OUTPUT layer kept a SECOND one from
before the renderer split: `eqt::output::CameraMode { Free, Follow,
FirstPerson }` in `output_renderer.h`, with a cycle that nothing a player
reaches can drive. T461 found it: `GraphicalMode` consumes
`InputAction::ToggleCameraMode` and calls
`IOutputRenderer::cycleCameraMode()`, but `IrrlichtRenderer` does not DERIVE
from that interface (L03 correction 13), so in a real run the call lands on
the interface's EMPTY default body. The same shape B043 recorded for
wireframe/HUD/name tags.

## The route, producer → consumer (grep tree-wide before deletion:
src include tests tools benchmarks docker scripts)

| Link | Verdict | Evidence |
|---|---|---|
| `InputAction::ToggleCameraMode` (enumerator, `input_handler.h`) | DELETED | PRODUCER half: no input handler ever set it — `GraphicsInputHandler`'s RA→InputAction map and `ConsoleInputHandler`'s key/command tables both have zero `ToggleCameraMode` hits (the net's `NoInputHandlerProducesToggleCameraMode` pins that). CONSUMER half: exactly one site, `GraphicalMode::processInput()`'s consumeAction arm (graphical_mode.cpp) |
| `GraphicalMode::setCameraMode` / `cycleCameraMode` / `getCameraMode` (h + cpp) | DELETED | `cycleCameraMode()` calls `m_renderer->cycleCameraMode()` through `std::unique_ptr<output::IOutputRenderer>` — the live renderer never reaches the override below because it is not an `IOutputRenderer`; `setCameraMode` had NO caller at all; `getCameraMode`'s only other references were test-only, on the live `PlayerCameraComponent`/`IrrlichtRenderer` names — different classes |
| `IOutputRenderer::setCameraMode` / `cycleCameraMode` (empty virtual defaults) + `enum class CameraMode` | DELETED | `cycleCameraMode`'s empty default was the only body the live path could reach (the no-op); no other `IOutputRenderer` implementer (`ConsoleRenderer`, `NullRenderer`) overrides either — both were zero-caller on the class |
| `GraphicalRenderer::m_cameraMode` / `setCameraMode` override / `getCameraMode` / `cycleCameraMode` / `getCameraModeString` | DELETED | the class's ONLY two non-virtual methods (the cycle body and the string form) were the camera bodies; nothing derives from the class (T241) |
| `graphical_renderer.cpp` | kept as an empty TU | its whole content was those two bodies; a tombstone comment + the namespace is all that remains |

The live route, UNTOUCHED (pinned by the net's negative control and by
`test_camera_mode_wiring`, `test_irrlicht_camera_command`,
`test_slash_commands_component`, `test_player_camera_component`):
F9 → `HotkeyAction::ToggleCameraMode` (hotkey_manager.h:57) →
`RendererAction::ToggleCameraMode` (queued by `RendererEventReceiver`) →
`FrameComponent::processCommonInput`'s arm (renderer_frame_input.cpp) →
`PlayerCameraComponent::cycleCameraMode()` (T461). `/camera` reaches the same
component (renderer_slash_commands.cpp). Note the name collision:
`HotkeyAction::ToggleCameraMode` and `RendererAction::ToggleCameraMode` are
the LIVE names and survive; only the `InputAction` enumerator was deleted.

## Consequential dead code found by the hygiene sweep (the same class B533
cleaned — a deleted reader orphans its sole caller)

- `GraphicalMode::graphicalRenderer()` (h + cpp) — its ONLY caller was the
  deleted `GraphicalMode::getCameraMode()` (the `dynamic_cast` back to the
  concrete base). `ZeroCallers` flagged it NEW; deleted with the route.
- `IrrlichtRenderer::getCameraMode()` — NOT deleted: it is the live const
  accessor (`return PlayerCamera().cameraMode()`, irrlicht_renderer.h:354)
  used by six camera test suites as their observation window. It merely
  became TESTS-ONLY because the name-token `getCameraMode()` in the deleted
  `graphical_mode.cpp` was what `ZeroCallers` had been counting as its
  production caller. Classified `observer` in `EXEMPT_TESTS_GROUPS`
  (T458), with the reason naming B534.

## Changes

- `include/client/output/output_renderer.h` — enum + two virtual defaults
  gone (header no longer names `CameraMode` at all)
- `include/client/output/graphical_renderer.h` — member + four methods gone,
  doc line "Camera mode support" gone
- `src/client/output/graphical_renderer.cpp` — reduced to the tombstone
  comment and its namespace
- `include/client/mode/graphical_mode.h` + `src/client/mode/graphical_mode.cpp`
  — three declarations, three bodies, the consumeAction arm, doc bullet gone;
  plus `graphicalRenderer()` (h + cpp), whose only caller was the deleted
  `getCameraMode()`
- `include/client/input/input_handler.h` — `ToggleCameraMode,` line gone
  (34 → 33 enumerators; `src/client/input/CLAUDE.md`'s count is the
  authoritative DocCounts measurement and must move with it — see
  "Outstanding")
- `scripts/refactor/check_zero_callers.py` — `("IrrlichtRenderer",
  "getCameraMode"): "observer"` (T458) with the B534 reason; the live
  accessor itself is untouched
- Stale comments corrected: `player_camera_component.h` (both call sites that
  explained the empty-body no-op), `irrlicht_renderer.h` (the alias comment)
- Net added: `tests/test_b534_output_camera_route.cpp` (+ `tests/cmake/
  commands.cmake` target), 6 cases: five absence scans over the seven route
  files + one producer scan, plus a negative control that the live route's
  tokens (`HotkeyAction`/`RendererAction::ToggleCameraMode`, the F9 arm line,
  the component's `cycleCameraMode`) keep existing

## Verification

- RED (net added, route present, tip `0136137b`): 4 FAIL / 2 OK — the two
  negatives (live route intact, no producer) pass, the four absence scans
  fail exactly as designed (evidence: `red_output.log`)
- GREEN (route deleted): 6/6 pass (evidence: `green_output.log`)
- `test_input_action_bridge` (41 tests, owns the T368 enumerator-count net):
  pass. `test_input_action_bridge.cpp`'s own `ToggleCameraMode` references
  are in its absence-assertion tables and stay correct: it scans the enum for
  absent names, not present ones
- Hygiene: `check_zero_callers` GREEN after the two consequential findings
  (364 zero-caller / 293 tests-only, all classified), `check_write_only_fields`
  GREEN, `check_doc_claims` GREEN, `check_comment_claims` GREEN,
 `check_board_dir_numbers` GREEN. At the original head `ac7f4da5`,
 `check_doc_counts` was RED: the protected `src/client/input/CLAUDE.md`
 still stated 34 instead of the measured 33 enumerators. Pipelines 813/814
 failed hygiene, so build/test/gate did not run. The review fix updates
 that line and the checker's reason string (see "Outstanding").
- Affected binaries rebuilt (willeq_core, willeq, test_command_processor,
  test_camera_mode_wiring, test_irrlicht_camera_command) and all pass, along
  with test_headless_command_refusal, test_autorun_arm,
  test_ranged_attack_wire, test_eq_action_handler_enum_guards and
  test_read_and_reset_ownership — 0 failures across 552 test cases (the ten named binaries; re-run independently by the reviewer at ac7f4da5)

## Outstanding (must land before merge)

DONE in the review-fix commit containing this section, immediately after
merged head `248e53fa5791b9aa9de6dca1bf0b69856ab59c2e` on
`task/kb-b534-output-camera-route`: the protected
`src/client/input/CLAUDE.md:10` now states **33** and explains B534's deletion,
and `scripts/refactor/check_doc_counts.py` names that deletion in its reason
string. The resulting full head SHA and exact-head pipeline status are
recorded in kanban task `t_1c202493`'s handoff (a commit cannot embed its own
SHA). The earlier hygiene failures above remain historical failures, not
successful build/test/gate evidence.
