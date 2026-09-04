# T343 — Autorun does nothing (B002) and the keyboard-movement START half has no caller (B032)

Two class-A input gaps, one unit, measure first — T334 found the swim
"gap" was a false premise (the physics phase polls the hotkey every frame),
and these two sit on the same input path. Slot2.

## Measure

1. `readInputState()` (`src/client/graphics/player/renderer_player_physics.cpp`,
   G06 phase 1) polls `HotkeyAction::*` every frame into
   `playerMovement_`: list every action it polls (forward/back/turn/strafe/
   jump/swim…) and every `playerMovement_` field it writes. Then answer:
   - **B032:** are forward/back/turn driven by that poll (like swim)? If
     yes, `MovementComponent::StartMove*/StartTurn*` are a SECOND, dead
     input route (the game-side one), and the honest fix is to delete
     Start*/Stop* together (§5) and strike B032 as "refuted: movement is
     the renderer's per-frame poll". If no — the renderer path only
     handles the graphical client and headless/scripted movement needs the
     game-side Start* — then wire the `RendererAction`/`HotkeyAction` arms
     that should call them, mirroring T330. Decide from the measurement;
     record the poll table.
   - **B002:** `playerMovement_.autorun` is read in the physics phase and
     has no writer. `EqActionHandler::toggleAutorun` is a `LOG_DEBUG`
     stub. Find the hotkey (`hotkey_manager.cpp`; the official client's
     autorun is Num Lock by default — read `config/hotkeys.json`) and the
     route it takes today (game-side action handler → intent → renderer, or
     renderer-side `RendererAction`). Wire the shortest correct route:
     the toggle flips a renderer-side flag the physics phase reads (a
     `RendererAction::ToggleAutorun` arm in `processCommonInput`, like the
     four T330 arms, is probably the whole fix), and `toggleAutorun` on the
     game side either publishes the intent that reaches it or is deleted
     if the renderer route is complete (§5 — say which). Autorun semantics:
     forward held until any movement key or the toggle again.

## Proof

Per fix a test that fails before and passes after (the frame-input tests
`test_debug_hotkey_arms.cpp` show the arm-drives-flag shape; for B032 if
wired, the same). ⚠ `test_movement_order`'s golden pins
`renderer_player_physics.cpp` statement by statement: if `readInputState`
gains a statement, the golden learns it IN PLACE (T337/T335b shape) with
both faults re-planted. The three frame text-reader tests run. Live proof:
OWED unless the lock is free (T342 holds it) — a fable session in qeynos2:
press Num Lock, capture two frames a second apart, the position must
change; then press W, autorun stops. `review/T343/` if run.

Territory: `src/client/graphics/frame/renderer_frame_input.cpp` (arms),
`src/client/graphics/player/renderer_player_physics.cpp` (only if the
autorun read needs a touch), `include/client/graphics/player/player_camera_component.h`,
`src/client/action/eq_action_handler.cpp` (`toggleAutorun`),
`src/client/movement/movement_component.cpp` + header (Start*/Stop*
deletion if refuted), `include/client/input/*` only if a binding is
missing, tests + goldens (in-place learn only), `refactor/BOARD.md`.
Graphics files change: visual gate + ARM NOT skippable.

STOP: the two routes disagree on who owns movement input (renderer poll
vs game-side Start*) in a way that needs a design ruling — report both
with their callers.
