# T334 — Wire the audio listener to the camera (B045); vision reset and swim controls (B046)

Class A, both found by L05 (zero-caller enable paths). Slot3. No eqemu
needed for the code + tests; a short live check is optional (owed if the
lock is held).

## B045 — audio listener

`PlayerCameraComponent::getCameraTransform(posX..upZ)` exists and is
correct; nothing calls it, so 3D audio positioning has no listener.
Find the audio update tick (`AudioManager::update` or the bridge's
audio tick in `src/client/bridge/bridge_component_ui_audio.cpp` — grep
`setListener`/`listener` in `src/client/audio/`). One call per frame
from the render side (the camera lives there): the frame pass that
already touches audio, or `PostRenderTick` → `Bridge()` → `audio_manager->setListenerTransform(...)`.
Match the existing seam shape; do not add a thread crossing (the audio
manager's own thread-safety is what it is — read it). Test: a recording
audio backend sees the listener set to the camera transform after one
frame.

## B046 — vision reset and swim

- `resetVisionToBase()` is the only downgrade path for `currentVision_`
  and nothing calls it: an Ultravision/Infravision buff is permanent for
  the session. Wire it where the buff FADES: the `BuffManager`'s expiry /
  the `SpellEffect` removal that set the vision (grep `setVisionType`
  publishers — `irrlicht_bridge.cpp:582` consumes a `VisionChanged`-shaped
  event; find its publisher and add the reset on the fade path). Test:
  apply → fade → vision is base.
- `setSwimUp()` / `setSwimDown()` are the sole writers of
  `playerMovement_.swimUp/.swimDown`, read live by `updatePlayerMovement`
  (now a G06 phase); no input path calls them. Add the two `RendererAction`
  arms in `processCommonInput` mirroring T330's four (the hotkey bindings
  may already exist — `hotkey_manager.cpp`; if not, bind to the official
  client's swim keys — read `docs/` for the keybinding table, or leave the
  binding to Will and wire only the arms; say which). Test: the arm flips
  the flag (as `test_debug_hotkey_arms.cpp` does).
- `setLevitating()` — third zero-caller writer, duplicated by
  `setSwimmingState()`'s parameter: delete it (§5, check for `pushEvent`).

Territory: `src/client/graphics/frame/renderer_frame_input.cpp` (two
arms), `src/client/graphics/player/*` (delete `setLevitating`), the audio
call site (bridge or frame pass), the buff-fade publisher (game side),
`include/client/input/hotkey_manager.*` only if a binding is added, tests,
`refactor/BOARD.md`. Graphics files change: visual gate + ARM NOT
skippable. ⚠ `test_debug_hotkey_arms.cpp` and the two UI text-reader
tests parse `renderer_frame_input.cpp` — run them; widen if a spelling
changes.

STOP: the audio call needs a new thread crossing; a buff-fade hook needs
a state-module change beyond a publish.
