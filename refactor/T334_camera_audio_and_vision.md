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

## Review

Both board entries were wrong about WHERE the break was, and in both cases the
real defect was found by following the wire to the far end instead of trusting
the "zero callers" reading of the near end. Neither contracted fix (a second
listener call site; two swim input arms) was made, because neither was correct;
one genuine bug was fixed on each item instead, and one new class-A gap fell
out and is STOPped.

### B045 — the audio listener

**The premise is refuted.** `AudioManager::setListenerPosition()` is not
unwired: `BridgeComponent::PostRenderTick()` has called it every frame since
D20c2, and its comment states the design — the listener sits at the PLAYER, not
the camera, because in third person the camera can be far behind and would make
a nearby NPC sound distant. `getCameraTransform()`'s zero callers are a
consequence of that decision, not evidence of a gap.

**The real bug is the orientation it passes.** `ctx_.self.HeadingDeg()` returns
DEGREES — every other consumer in the tree converts it with `* 512.0f / 360.0f`
or `* M_PI / 180.0f` — but the listener conversion read it as 0-512 wire units
and then negated cos:

    float headingRad = ctx_.self.HeadingDeg() * (2.0f * M_PI) / 512.0f;
    float forwardX = std::sin(headingRad);
    float forwardY = -std::cos(headingRad);

So the listener faced a mirrored direction turning 1.42x too fast, and
`SfxManager::playSpatial()` panned every positioned sound by it. Three
independent witnesses give the correct convention (+X east, +Y north, heading 0
= north, forward = (sin h, cos h, 0)): `computeMovementVector()`
(renderer_player_physics.cpp), `HandlePlayerPositionUpdate()`'s
backward-motion test (bridge_component.cpp:733-735), and
`MovementState`'s own "+X = East, +Y = North" comment (movement_state.cpp:882).

* **Call site.** The block moved verbatim, apart from the forward vector, into
  the new `BridgeComponent::UpdateAudioListener(float)` in
  `src/client/bridge/bridge_component_ui_audio.cpp` — the audio family's TU,
  and the file this unit was told it owns. `PostRenderTick()` in
  `bridge_component.cpp` is now a single guarded call to it (that file is
  T332's; the edit there is a 25-line delete plus one call line, nothing else).
  The conversion is one shared helper,
  `EQT::Audio::eqHeadingToListenerForward(float headingDeg)`
  (`audio_manager.{h,cpp}`). One call per frame, same seam, same thread — the
  game thread, as before. No thread crossing added; no STOP.
* **Test.** `tests/test_spatial_audio.cpp`, new `ListenerForwardTest` (2 tests,
  no audio device needed): the four cardinal headings, the degrees-vs-wire-units
  wrap at 360, and the pan relation `playSpatial()` computes — the same relative
  bearing must give the same pan from any facing, the opposite side must flip
  the sign, and ahead/astern must both be centred.
* **Pre-fix failure** (helper first carrying the old formula verbatim, then
  corrected):

      [ RUN      ] ListenerForwardTest.HeadingDegreesMapToTheClientsForwardConvention
      north.y evaluates to -1, 1.0f evaluates to 1
      east.x evaluates to 0.89322429895401001, 1.0f evaluates to 1
      east.y evaluates to -0.44961130619049072, 0.0f evaluates to 0
      south.y evaluates to 0.59569936990737915, -1.0f evaluates to -1
      west.x evaluates to -0.17096199095249176, -1.0f evaluates to -1
      wrapped.x evaluates to -0.95694035291671753, 0.0f evaluates to 0
      wrapped.y evaluates to 0.29028454422950745, 1.0f evaluates to 1
      [ RUN      ] ListenerForwardTest.PanningTurnsWithTheListener
      a evaluates to 1, -1.0f evaluates to -1
      b evaluates to -0.89322429895401001, -1.0f evaluates to -1
      c evaluates to 0.59569936990737915, -1.0f evaluates to -1
      panFor(0.0f, -50.0f, 0.0f) evaluates to -1, 1.0f evaluates to 1
      [  FAILED  ] 2 tests

  Both pass after. Note `b` and `c`: under the old formula the pan for the SAME
  relative bearing differed between headings, which is the 1.42x error showing
  up on its own.
* **`getCameraTransform()` is DELETED** (§5), from the component and from
  `renderer_camera.cpp`, with the reason recorded in
  `player_camera_component.h` and `irrlicht_renderer.h`. Leaving it would leave
  B045 open forever on a knob whose stated purpose is already served by design.
  If a camera-based listener is ever wanted it is a listening measurement
  against the official client, not an inference from a dead accessor.

### B046 — vision

**Half real, and the diagnosis was one hop short.** The buff-fade hook is not
missing: `EverQuest::InitializeSpellSystem()` installs a `BuffManager` fade
callback (`src/client/core/everquest.cpp:309-331`) that, on an
UltraVision/InfraVision buff expiring, recomputes vision from every buff still
up and publishes the ABSOLUTE result as `VisionChanged`. The break is at the
consumer: `irrlicht_bridge.cpp`'s arm called `setVisionType()`, whose rule was
"only upgrade, never downgrade", so the recomputed value could raise vision and
never lower it. The fade publish was a no-op — which is exactly the symptom the
board described, reached by a different route.

* **Call site.** `PlayerCameraComponent::setVisionType()`
  (`src/client/graphics/player/renderer_camera.cpp`) now calls
  `resetVisionToBase()` first and then applies the payload on top. That is an
  absolute apply with the racial floor intact, and it gives
  `resetVisionToBase()` its production caller. The bridge arm is unchanged
  apart from a comment saying why the payload is absolute. No state-module
  change; the publisher was already there, so the STOP did not trigger.
* **Test.** New `tests/test_vision_buff_fade_wire.cpp` (3 tests), registered in
  `tests/cmake/renderer_guards.cmake` next to `test_navmesh_overlay_wire` and
  using the same bare never-Init()'d renderer + real `IrrlichtBridge` idiom: a
  Human's Ultravision fading returns him to Normal; a Dwarf's fading leaves him
  at his own Infravision, not Normal; and one of two stacked buffs fading
  applies the genuine downgrade to Infravision.
  `PlayerCameraComponentTest.VisionOnlyUpgradesAndResetsThroughTheComponent`
  asserted the old rule and is rewritten as
  `VisionAppliesTheBuffValueOverTheRacialBase`.
* **Pre-fix failure:**

      [ RUN      ] VisionBuffFadeWireTest.AFadeToNoVisionBuffsReturnsAHumanToNormal
        renderer.getVisionType()  Which is: <02-00 00-00>   (Ultravision)
        VisionType::Normal        Which is: <00-00 00-00>
      [ RUN      ] VisionBuffFadeWireTest.AFadeNeverDropsBelowTheRacialBase
        renderer.getVisionType()  Which is: <02-00 00-00>
        VisionType::Infravision   Which is: <01-00 00-00>
      [ RUN      ] VisionBuffFadeWireTest.OneOfTwoStackedBuffsFadingLeavesTheOther
        renderer.getVisionType()  Which is: <02-00 00-00>
        VisionType::Infravision   Which is: <01-00 00-00>
      [  FAILED  ] 3 tests

  All three pass after.

**⚠ STOPped, boarded as B055: the GRANT side does not exist.** The only
publisher of `VisionChanged` tree-wide is that fade callback.
`BuffManager::applyBuff()` has no apply-side callback, so casting Ultravision
never reaches the renderer at all — the feature is still end-to-end dead, one
hop earlier than B046 said. Wiring it needs a new callback on `BuffManager`,
which is a state-module change beyond a publish, and this contract STOPs on
exactly that. Not done; boarded.

### B046 — swim: REFUTED, no arms added, three writers deleted

"`setSwimUp()`/`setSwimDown()` are the SOLE WRITERS of
`playerMovement_.swimUp/.swimDown` … vertical swim control has no producer"
(L03 correction 17, carried into B046) is **false**. `readInputState()`
(`src/client/graphics/player/renderer_player_physics.cpp:97-101`, G06 phase 1)
polls `HotkeyAction::SwimUp`/`SwimDown` and assigns both flags every frame,
exactly as it does for WASD:

    bool swimUpPressed = isActionHeld(eqt::input::HotkeyAction::SwimUp);
    bool swimDownPressed = isActionHeld(eqt::input::HotkeyAction::SwimDown);
    playerMovement_.swimUp = swimUpPressed;
    playerMovement_.swimDown = swimDownPressed;

Those two lines are pinned by the G06 golden
(`tests/goldens/movement_order_g06.txt` lines 23-24). Vertical swim control has
worked all along.

**Binding decision: nothing to decide, and no binding added.** The bindings
already exist in the tree, in two places: `hotkey_manager.cpp:472-474` binds
`SwimUp` to `[` (`KEY_OEM_4`) and `SwimDown` to `]` (`KEY_OEM_6`) in Player
mode, and `config/hotkeys.json:28-29` ships the same as `BracketLeft` /
`BracketRight`. So the contract's fallback ("wire the arms and leave the
binding to Will") does not apply — there is no unbound feature.

**Adding the contracted arms would have been a regression**, so they were not
added: a `RendererAction` arm setting `swimUp = true` is overwritten by the
poll on the very next frame, and a key-DOWN action has nothing to clear the
flag on key-up. `setSwimUp()`, `setSwimDown()` and `setLevitating()` are
DELETED under §5 (none had a `pushEvent`); `setLevitating()` was a plain
duplicate of `setSwimmingState()`'s levitating parameter, which is live at
`irrlicht_bridge.cpp:629`. `renderer_frame_input.cpp` is therefore UNCHANGED,
and the three text-reader tests that parse it needed no widening.

### Steps

| step | result |
| --- | --- |
| `cmake --build build -j24` | exit 0 |
| `ctest -j6 --output-on-failure` | **3764 / 3765**, 17 skipped. Only failure is the sanctioned `AudioManagerTest.PreloadCommonSounds`. `test_debug_hotkey_arms`, `test_ui_hide_command` and `test_static_ui_scroll` all pass unmodified (`renderer_frame_input.cpp` untouched). |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` (standalone) | `=== VISUAL GATE: PASS ===`, exit 0; no golden in `git status --porcelain` |
| `scripts/build-arm-noble.sh` | exit 0 — first run hit the 10-minute tool ceiling (exit 143); per the protocol, zero-byte artifacts under `build-arm-noble/bin`, `lib` and any zero-byte `.a` were deleted and the build re-run to completion |
| `scripts/refactor/check_file_sizes.sh --all` | `867 ok, 0 warn, 0 fail` |

Live check: not run. No eqemu lock taken (slot1 may hold it), and neither fix
needs a server — the fade path is pinned at the bridge arm and the listener
math is arithmetic. A live confirmation of the pan improvement would be a
listening test, not a screenshot, and is owed only if Will wants it.

### Slot hygiene

`bridge_component.cpp` is T332's file; the edit there is confined to
`PostRenderTick()`'s `#ifdef WITH_AUDIO` block (25 lines deleted, one call line
added) because the per-frame audio tick has no other home. Nothing else in that
file, and nothing in `bridge_component_intents.cpp` or `overlays/`, was
touched.

### Board

B045 and B046 struck on `refactor/BOARD.md` with the refutations recorded (a
silent strike would have re-buried both false premises). B055 added for the
missing grant-side publisher. One stale cross-reference corrected in passing:
B041's closing note said the navmesh DRAW half "is now B045" — B045 was already
the audio entry and the draw half was actually filed as B048.
