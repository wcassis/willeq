# B1066 (#386): a mouse press is owned by the UI or the camera, never both

Will, 2026-10-14 (XU4 play test, fable ac32a25e): "right clicking on an
inventory item is causing the view to change in response as though it's a
mouse look event. the item inspect should consume the event."

## Mechanism (pre-fix, base 0e661140)

- `src/client/graphics/renderer/renderer_event_receiver.cpp` (pre-fix
  :333/:348) set the HELD flags `leftButtonDown_` / `rightButtonDown_` on
  every `EMIE_*_PRESSED_DOWN`, wherever the press landed, and the
  `EMIE_MOUSE_MOVED` arm re-asserted them while the button was down
  (pre-fix :367-372).
- The static UI consumes only the CLICK latch (`wasRightButtonClicked()`,
  `renderer_frame_render.cpp:604`, B442) and the left click
  (`pendingUIClick_`). `include/client/graphics/ui/static_ui_input.h:104`
  said it outright: mouse-look reads `isRightButtonDown()`, not the latch.
- Mouse-look (`src/client/graphics/player/renderer_player_physics.cpp:167-169`)
  reads `isLeftButtonDown() || isRightButtonDown()`, so the same press
  opened the item display AND turned the camera.
- **The left button had the same leak**: a left drag on a window (here the
  inventory's background) turned the camera too (measured below).
- **A second defect on the same path**: the right press did not restart the
  look baseline (`lastMouseX_/Y_`), only the left press did, so the first
  look frame of a right drag turned the camera by ALL the pointer travel
  since the previous look ended. That is why the pre-fix ui_right gesture
  below turned the view by -227 heading units for a 20 px drag, and
  view_right by +175 instead of +10.

## Fix

`include/client/graphics/press_ownership.h` (header-only, device-free):
each button's press is `None / Pending / Camera / Ui`. A new press is
PENDING; once per frame, after the static UI's hit test,
`FrameComponent::processCursorGestureFrame()` settles every pending press
with `pointerOwnedByUI()` (`renderer_frame_cursor.cpp`): an entry point took
the position (`uiTook`, which is hover-based, so it covers every panel those
entry points hit-test), or a window whose entry point reports nothing holds
it (`pointOverSelfConsumingWindow()`: the dialogs, the Friends window, the HUD
panels; plus the trade / give and tip windows). A hidden UI owns nothing.
An owned press keeps its owner until release.

- `isLeft/RightButtonDown()` is now the **camera-owned look view** -- the
  look consumer's source lines are unchanged (they are pinned verbatim by
  `tests/goldens/movement_order_g06.txt`).
- `isLeft/RightButtonHeld()` is the physical hold: the item display's held
  lifetime (`updateHeldItemDisplay`, renderer_frame_cursor.cpp) and RDP's
  forwarded button mask (`rdp_component.cpp`).
- The right press restarts the look baseline like the left one; a press that
  resolves to the camera loses none of the motion made while pending.
- With no static UI (the legacy GUI path) `processFrameInput()` gives every
  press to the camera before `updatePlayerMovement()` -- the pre-B1066
  behaviour there.

## Pins

- `tests/test_press_ownership.cpp` (14 cases): the ownership logic (press on
  UI ignored by look until release, press on view looks and keeps looking
  across a window, release clears, buttons independent, both buttons), the
  receiver over real Irrlicht mouse events (incl. the baseline restart), and
  source pins (the item display reads the physical hold, mouse-look the look
  view, the frame settles pending presses before movement on the legacy path).
- `scripts/eqemu/press_ownership_check.sh` + `press_ownership_verdict.py`
  (docs/ci.md): real X button events via xdotool into an x86 GLES2 client on
  Xvfb (`--egl-x11`, llvmpipe), local lane, Testbuddy, heading and camera
  pitch from `--pose-log`.

## x86 runs (review/B1066/)

Same rig, same pixels (800x600 window: item 280,456 = Skin of Milk; window
background 300,250; view 650,80; cross into the window at 520,300).

| gesture | pre-fix binary (`baseline_prefix_b1065bin/verdict.txt`) | fixed (`fixed_run1/verdict.txt`) |
|---|---|---|
| ui_right (right drag 20 px on an item) | d_heading -227.0, d_pitch +1.20 -- FAIL | 0.0000 / 0.0000 -- PASS |
| ui_left (left drag 20 px on the window) | d_heading +10.0 -- FAIL | 0.0000 / 0.0000 -- PASS |
| view_right (right drag 20 px on the view) | +175.0 (stale-baseline jump) | +10.0 -- PASS |
| view_cross (view -> into the window) | -75.0 | -65.0 -- PASS |
| view_cross, after entering the window | +10.0 | +10.0 -- PASS (keeps looking) |

The pre-fix binary is `wt/b1065/build/bin/willeq` (task/B1065, which touches
no input code). `ui_right_held.png` in both runs shows the item display
(Skin of Milk) open while the button is held; `fixed_run1/ui_right_released.png`
shows it closed after release (the held-only lifetime still works).
`fixed_run2/` is a repeat of the fixed run. `explore/` is the first
screenshot (the Inventory Tip window that the rig's `--dismiss` closes);
`discarded_inventory_was_closed/` is a run thrown out because the shared
UI cache had remembered an open inventory (the rig now uses a fresh
`WILLEQ_CACHE_DIR`).

## Not verified / left open

- **The oracle was not re-measured.** "A drag that starts on the view keeps
  looking across a window" and "a press on a window never looks" are the
  official client's behaviour as #386 states it; the lane-5 B1048 driver was
  not used to capture a right-drag on the official client. Assumption, not
  measurement.
- Ownership is decided from the pointer's position at the first frame AFTER
  the press (the static UI's hit test runs once per frame), not the press
  pixel itself. A press on a window edge plus a fast flick inside one frame
  could be attributed by where the pointer is a frame later. Not seen in the
  runs; on the Pi's slower frames the window is wider.
- A camera-owned press starts looking one frame later than before (pending
  for one frame); no motion is lost (the baseline is the press).
- Not run on the XU4 or any ARM lane (Will's box, not touched); the change is
  platform-independent input code.
- The RDP path (`WITH_RDP`) was compiled but not exercised.
