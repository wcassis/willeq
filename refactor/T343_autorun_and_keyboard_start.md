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

## Review

Branch `task/T343-autorun-keyboard-start`, cut from `fable` at `849db7a2`.
Both items resolved. Neither needed the STOP: the poll table answers the
ownership question outright.

### The poll table (measured, not inferred)

`readInputState()` has been renamed `readMovementInput()` — G06 phase 1,
`src/client/graphics/player/renderer_player_physics.cpp:58`, called
unconditionally as the first of `updatePlayerMovement()`'s eight phases every
render frame. It polls NINE `HotkeyAction`s through one `isActionHeld` lambda
(which returns false whole-sale while chat has focus) and writes NINE
`playerMovement_` fields plus two jump fields:

| polled `HotkeyAction` | local | `playerMovement_` field written |
|---|---|---|
| `MoveForward`  | `forward`         | `moveForward = forward \|\| autorun` |
| `MoveBackward` | `backward`        | `moveBackward` |
| `TurnLeft`     | `turnLeft`        | `turnLeft` (and `frame.turnLeft`, phase 2's turn) |
| `TurnRight`    | `turnRight`       | `turnRight` (and `frame.turnRight`) |
| `StrafeLeft`   | `strafeLeft`      | `strafeLeft` (forced false while swimming) |
| `StrafeRight`  | `strafeRight`     | `strafeRight` (forced false while swimming) |
| `Jump`         | `jumpPressed`     | `isJumping`, `verticalVelocity` (edge-triggered, land only) |
| `SwimUp`       | `swimUpPressed`   | `swimUp` |
| `SwimDown`     | `swimDownPressed` | `swimDown` |

Also read, not polled: `playerMovement_.autorun` (folded into `moveForward`),
`isSwimming`, `isJumping`.

**Verdict — B032: REFUTED, and the cluster deleted.** Forward, backward and
turn are driven by this poll, exactly the way swim is. That is the same shape
T334 measured when it refuted "vertical swim control has no producer" (B046),
and it makes `MovementComponent::StartMove*/StartTurn*` a SECOND, parallel
input route rather than a switch missing its on position. The T319 keep note
argued they were "the only game-side mover the client has" and had to survive
for a headless/scripted path; that premise does not hold either. The route
needs key-DOWN/key-UP pairs, and the only mode that produces them is the
graphical one, which polls instead. `ConsoleInputHandler` is line-based and
has no continuous movement keys to bind. No producer was ever possible, so
the honest fix was §5 as the contract specified.

### B002 — autorun: the toggle that had no writer

**Route today (pre-fix), traced end to end.** `config/hotkeys.json:32` and
`hotkey_manager.cpp:481-483` bind THREE keys (Grave, Numpad +, Num Lock —
Num Lock is the official client's default). `RendererEventReceiver::OnEvent`
(`renderer_event_receiver.cpp:193`) matched `HA::ToggleAutorun` and called
`pushBridgeAction({RA::ToggleAutorun})` — the BRIDGE queue, not
`actionQueue_`. `GraphicsInputHandler` (`graphics_input_handler.cpp:98`)
turned that into `InputAction::ToggleAutorun`; `InputActionBridge`
(`input_action_bridge.cpp:70`) consumed it and `reportAction`ed it as a
SUCCESS through `ActionDispatcher::toggleAutorun()`; and
`EqActionHandler::toggleAutorun()` was a `LOG_DEBUG` stub. Meanwhile
`playerMovement_.autorun`, the only thing that could have made the player
move, had ZERO writers in the tree. Three keys, five hops, a reported
success, and a stationary player.

**The change.** The shortest correct route is renderer-side, for the reason
T334 already recorded for swim: phase 1 rewrites the movement flags every
frame, so a game-thread writer loses the race, and a key-DOWN action has no
key-up to clear it on.

1. `player_camera_component.h` — `toggleAutorun()` / `autorun()` on
   `PlayerCameraComponent`, the smallest pair the other side needs.
2. `renderer_event_receiver.cpp` — `HA::ToggleAutorun` now pushes onto
   `actionQueue_` instead of the bridge queue. The receiver already swallows
   every non-function key while chat is focused, so no extra guard is needed.
3. `renderer_frame_input.cpp` — a `case RA::ToggleAutorun:` arm in
   `processCommonInput()`, the T330 shape.
4. `renderer_player_physics.cpp` — the cancel half, immediately before
   `moveForward` is computed: a translational movement key
   (forward/backward/strafe) drops autorun. **Deviation from the contract's
   wording, stated deliberately:** "any movement key" excludes TURNING here.
   Steering while autorunning is the entire point of the feature and the
   official client keeps running through a turn; cancelling on turn would make
   autorun unusable. Recorded in the code comment as well.
5. `graphics_input_handler.cpp` — the `RA::ToggleAutorun` arm was DELETED. It
   is now unreachable: nothing pushes that action onto the bridge queue.
6. `eq_action_handler.cpp` — `toggleAutorun()` KEPT, not deleted, and its
   comment made honest. It is `IActionHandler`'s pure virtual, and deleting it
   means touching `action_handler.h`, `action_dispatcher.{h,cpp}`,
   `input_action_bridge.cpp`, `console_input_handler.cpp`,
   `input_handler.h`'s enum and four test files — over-reach for this unit.
   It now survives for the console handler alone, which has no runner to
   toggle, and says so instead of "not yet implemented in EverQuest class",
   which invites someone to rebuild the route T316 proved cannot work.

**Pre-fix failure (measured — reverted the three production hunks, rebuilt,
ran):** `test_autorun_arm` 3 of 4 FAILED —
`AutorunArmTest.ToggleAutorunFlipsTheAutorunFlag`,
`AutorunArmSourceInvariants.TheReceiverQueuesAutorunOnTheRendererActionQueue`
("autorun is back on the bridge queue, which routes it to the game thread"),
`AutorunArmSourceInvariants.PhaseOneStillConsumesAndCancelsTheFlag`. Post-fix
4/4 pass.

### B032 — the keyboard-movement START half

**Deleted, as one cluster.** `StartMoveForward`, `StartMoveBackward`,
`StartTurnLeft`, `StartTurnRight`, `StopMoveForward`, `StopMoveBackward`,
`StopTurnLeft`, `StopTurnRight` and `UpdateKeyboardMovement()` from
`movement_state.cpp` + `movement_component.h`; the four `PlayerState` flags
`m_moveForward/m_moveBackward/m_turnLeft/m_turnRight` with their eight
accessors from `player_state.h`; and the `UpdateMovement()` branch in
`movement_update.cpp` that gated `UpdateKeyboardMovement()` on them. The
cluster was closed: the eight methods were the flags' ONLY writers, that one
branch was their ONLY reader, and the flags default false — so the branch
could never be entered. A T343 note at the end of `movement_state.cpp` records
the whole finding in place of T319's keep note.

**Its one reachable call site changed rather than disappeared, and was BROKEN.**
`EqActionHandler::stopAllMovement()` (the `/stop` command, and
`InputActionBridge`'s `MoveCommand::Type::Stop`) called the four `Stop*`
methods. Their entire net effect, given the flags no one could set, was
`setMoving(false)` + `ANIM_STAND` + a position update — **none of them cleared
the movement target or the path**, so `/stop` during a `MoveTo` or a `Follow`
left `targetX/Y/Z` set and the very next `UpdateMovement()` tick walked
straight back to it. It now calls `StopMovement()`, which clears both. The
deleted comment claiming "StopMovement() is private" was false: it has been
public in `movement_component.h:56` all along.

**Pre-fix failure (measured — restored the old net behaviour in
`stopAllMovement()`, rebuilt, ran):** `StopAllMovement.StopsAnActiveMovementTarget`
FAILED on all three target assertions (`targetX` 100 vs 0, `targetY` 200 vs 0,
`targetZ` 5 vs 0). Post-fix 2/2 pass.

### Golden: learned in place, both faults re-planted

`tests/goldens/movement_order_g06.txt` gained TWO statements at 27 and 28 (the
autorun cancel), 473 → 475, and every later line renumbered. `kGoldenSize` and
the comment above it in `test_movement_order.cpp` name the insertion, its
position and its reason — inserted, never regenerated from the tip.

Both faults re-planted and both detected:
- **Statement removed** (the production hunk reverted, golden untouched):
  `ComposedMovementOrderMatchesThePreG06Golden` FAILED, "movement order
  diverges at statement 27", pointing at the missing `if (playerMovement_.autorun && …)`.
- **Order swapped** (golden 27 and 28 exchanged, source correct):
  `ComposedMovementOrderMatchesThePreG06Golden` FAILED at 27.
- **Size changed** (one golden line deleted, 474):
  `GoldenIsPresentAndTheRightSize` FAILED with the never-regenerate message.
Golden restored; 4/4 pass.

### New tests

- `tests/test_autorun_arm.cpp` (4 cases; `tests/cmake/ui_rendering.cmake`,
  same `EQT_GRAPHICS AND IRRLICHT_LIBRARY` gate as `test_debug_hotkey_arms`).
  Behavioural through the production consumer on a device-less renderer, plus
  the two source-pinned ends the behavioural test cannot reach.
- `tests/test_stop_all_movement.cpp` (2 cases; `tests/cmake/packets_skills.cmake`).

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0**, 0 `error:` |
| `ctest -j6 --output-on-failure` | **3806 / 3807**, sole failure `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA quirk). `test_movement_order` 4/4, the three frame text-reader tests (`test_static_ui_scroll`, `test_ui_hide_command`, `test_debug_hotkey_arms`) pass, the T234/T237 transforms pass |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/ci/visual_gate.sh` | **PASS**, no pixel golden in `git status --porcelain` |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `check_file_sizes.sh --all` | 877 checked, **877 ok, 0 warn, 0 FAIL** |

One exit-143 during the first full build (10-minute tool timeout, not a build
failure). Protocol recovery applied: seven zero-byte files under `build/bin`
deleted, `ar t` member counts validated (`libwilleq_core.a` 321,
`libEtcLib.a` 15), rebuilt clean.

### Live proof: OWED

T342 holds the eqemu lock in slot3, so no live run was possible. The owed run:
a fable session in qeynos2, press Num Lock, capture two frames a second apart —
the position must change; then press W — autorun stops and normal forward
movement takes over; then `/moveto` a point and `/stop` — the character must
stay stopped rather than resume walking to the target. `review/T343/` when run.
