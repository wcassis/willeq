# T471 — apply T466's camera measurements: login camera, world-relative user cameras, official camera keys, pitch sign, Chase re-entry pose

Class A (match the oracle — `match-the-oracle-dont-infer-it`), **no lock
(Leg A)**, slot1. Read T466's Review (`refactor/T466_oracle_cameras_and_held_items.md`)
and `review/T466/README.md`, T461's and T464's Reviews, BOARD B530, B531, B580,
B581, B582, B585.

## What the oracle measured (T466) and what changes

1. **Login camera = First Person.** fable starts a character with no saved
   view in Chase (`kDefaultCameraMode`, T464 moved it to the identity load).
   Change the default to First Person; a saved `View=` still wins. Update the
   T464/T467 tests that pin Chase-as-default with the oracle provenance.
2. **User 1, User 2, Tether hold a WORLD direction** (0.08–0.19 match after a
   90° player turn); First Person, Overhead and Chase turn with the player.
   fable's rigs are heading-relative for all (B580). Make the three hold a
   world azimuth: on entering the mode, capture the world azimuth from the
   current player heading + rig azimuth (so the entry frame does not jump —
   verify against T466 frames if they show entry), then keep it fixed while
   the player turns; Tether additionally follows a walking player (T466). Heading
   conventions per `docs/CLAUDE.md`; the 90° player/NPC offset is load-bearing —
   never harmonise it. `/camera get` must report the azimuth in the frame it is
   stored in, and say which.
3. **Camera keys (Options > Keys > Camera, measured):** Zoom in = NPAD7 or
   INSERT; Zoom out = NPAD1 or DELETE; Raise = Alt+NPAD8 or Alt+UP; Lower =
   Alt+NPAD2 or Alt+DOWN; Tether-slide right/left = Alt+NPAD6/Alt+RIGHT,
   Alt+NPAD4/Alt+LEFT; Pitch up = Alt+NPAD9 or Alt+PGUP; Pitch down = Alt+NPAD3
   or Alt+PGDOWN; F9 = Toggle camera view (already). **The official Pitch up
   tilts the VIEW up — the opposite of fable's `CameraPitchUp`; fix the sign**
   so the action name means what the oracle's does. Bind each in the defaults
   and in `config/hotkeys.json` (T464's shipped-file test enforces both) where
   fable has the action; add Raise/Lower (look-at height nudge) and
   Tether-slide (Tether azimuth nudge) if the rig supports them cleanly. First
   census every existing fable binding on these keys (Insert/Delete, numpad,
   Alt+arrows) — a collision with a movement or UI binding is reported, not
   silently overwritten; STOP on a collision with movement keys.
   **Do NOT change F10, F11, L or Alt+C** — Will is deciding whether fable
   matches the official UI-page keymap exactly (B582).
4. **B581 — entering Chase from another camera leaves the old camera pose.**
   Confirm with a failing test; fix so Chase places its camera on entry (the
   T074/T300 reposition rule for first person is the model).
5. **FOV / Overhead** are already applied (T466). Do not re-fit anything.
   The Chase-as-rig fit (pitch 6, dist 4.0, height 7.0) is NOT applied: record
   in the Review whether fable's Chase follow camera reproduces that geometry
   at its defaults (compute it), and board the difference if it does not.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
every TU touched (`movement_order`, `renderer_frame_render` pinned; in-place
updates per G-correction 32, say so); visual gate — the gate uses a fixed
camera; if a golden moves because the default mode changed, STOP and report
(do not re-bless). ARM; file sizes. Board B650–B659; strike/narrow B580, B581,
B531.

File territory: `src/client/graphics/player/`, `include/client/graphics/player/`,
`CameraController`, `renderer_frame_preferences.cpp` camera default,
`src/client/input/hotkey_manager.cpp` camera actions, `config/hotkeys.json`
camera rows, slash `/camera`, camera tests, `include/client/graphics/ui/CLAUDE.md`
key tables. **Not** entity/equipment attachment or model yaw (T472, slot2),
**not** `door_manager*` / `application_zone_loading.cpp` (T470, slot3) —
except T464's `placeCameraForZoneIn()` call if the default change needs it
(coordinate: report, don't edit, if T470 has not merged).

STOP: a key collision with movement bindings; an image golden moves.

## Review

Leg A, slot1, `task/T471-camera-oracle-followups`, base `4f94ba16`.
**STOP conditions: not hit.** No measured key collides with a movement binding
(census below), and no image golden moved. No eqemu lock and no Pi were used;
nothing here is live-proven.

### 1. Login camera = First Person

`kDefaultCameraMode` (`include/client/graphics/player/camera_mode.h`) is now
`CameraMode::FirstPerson`, quoting T466's measurement: the untouched first
frame after entering world scored First Person 0.232 and every other camera
>= 0.897 (`review/T466/logs/oracle_login_camera_scores.tsv`). Everything that
defaulted through it follows: `PlayerCameraComponent::cameraMode_`,
`UiPreferences::cameraMode`, and the per-identity default
`FrameComponent::syncUiPreferences()` applies before reading the ini. **A saved
`[Camera] View=` still wins** (`ASavedChaseWinsOverTheFirstPersonDefault`,
`ASavedUserCameraSurvivesASecondZoneLoad`). The legacy `Mode=0/1` (Free/Follow)
values stay mapped to Chase explicitly, because that is what those old files
meant; only an unknown value falls to the new default. T464's zone-in entry
point (`placeCameraForZoneIn()`) needed no change, so
`application_zone_loading.cpp` (T470's) is untouched.

### 2. World-relative User 1, User 2 and Tether (B580)

- `enum class CameraAzimuthFrame { PlayerHeading, World }` and
  `cameraAzimuthFrame(CameraMode)` (`camera_mode.h`/`camera_rig.cpp`): World
  for User 1, User 2 and Tether, PlayerHeading for Overhead (and First Person
  and Chase, which are not rigs). `computeCameraRigPose()` takes the frame (the
  default argument keeps the heading-relative form); a World azimuth ignores the
  heading. `updateRigCamera()` and `/camera get` pass the mode's frame.
- **World convention:** the numbers a heading-0 player would read. Az 0 puts the
  camera on the -Y side of the look-at point looking +Y (EQ client frame), az 90
  on the -X side, and positive turns the same way as a growing EQ heading. The
  90-degree player/NPC heading offset is not touched; the rig reads
  `ctx_.playerHeading` exactly as before, and only a World rig stops reading it.
- **How the entry azimuth is captured: it is NOT captured from the heading at
  entry (deviation from the plan's proposed shape, flagged).** The plan asked to
  capture it on entry and to verify against T466's frames. T466's marks
  (`review/T466/logs/oracle_marks_session.tsv`, 21:47-21:53) show every direct
  camera entered afresh through its own Ctrl+F binding at heading 160, then
  `#goto -74 428 3.751 288`, then every camera entered afresh again. User 1 /
  User 2 / Tether scored 0.084 / 0.120 / 0.193 between the two (a re-grab is
  0.03-0.11). An entry after a 90-degree turn gave the same view, so capturing
  from the entry heading would have put the h288 entry 90 degrees off the
  oracle. The rig STORES the world azimuth and holds it across entries. Which
  world direction the official cameras start at, and whether it is a constant or
  taken from some earlier heading, is unmeasured: **B651**.
- **`/camera get`** reports the azimuth in the frame it is stored in and says
  which: `[CAMERA-GET] az=... pitch=... dist=... height=...
  units=deg,deg,eq_units,eq_units az_frame=world` (User 1/2, Tether) or
  `az_frame=player_heading` (Overhead). The ideal-pose line uses the same frame.
  `/camera help` states both conventions.
- Tether keeps following a walking player: every rig is expressed around the
  player's position, which was already true.

### 3. Camera keys, with the collision census

Bound in `HotkeyManager::setupDefaults()` AND `config/hotkeys.json` (T464's
shipped-file test enforces the pair), all Player mode:

| official action (T466 screenshot) | fable action | primary | alternate |
|---|---|---|---|
| Zoom in | `CameraZoomIn` | Numpad7 | Insert (+ fable's Plus, Shift+Plus kept) |
| Zoom out | `CameraZoomOut` | Numpad1 | Delete (+ fable's Minus, NumpadSubtract kept) |
| Raise camera | `CameraRaise` (new) | Alt+Numpad8 | Alt+Up |
| Lower camera | `CameraLower` (new) | Alt+Numpad2 | Alt+Down |
| Tether-slide right | `CameraOrbitRight` (= `RIGHT_CAM`) | Alt+Numpad6 | Alt+Right |
| Tether-slide left | `CameraOrbitLeft` (= `LEFT_CAM`) | Alt+Numpad4 | Alt+Left |
| Pitch camera up | `CameraPitchUp` | Alt+Numpad9 | Alt+PageUp |
| Pitch camera down | `CameraPitchDown` | Alt+Numpad3 | Alt+PageDown |
| Toggle camera view | `ToggleCameraMode` | F9 (already) | -- |

**Census, taken before binding** (defaults and the shipped file):
Insert, Delete, Numpad1-9 and Alt with Up/Down/Left/Right/PageUp/PageDown/
Numpad digits were bound to NOTHING. The keys underneath: Up/Down/Left/Right =
MoveForward/MoveBackward/TurnLeft/TurnRight, PageUp/PageDown = ScrollUIUp/Down
(Global), NumpadAdd and NumLock = ToggleAutorun. **No collision.** Alt+Up is not
Up to either lookup: `HotkeyManager::getAction()` hashes key+modifiers and
`HotkeyBinding::matches()` compares modifiers exactly, and the per-frame
movement poll (`readMovementInput()`'s `isActionHeld`) requires
`altHeld == needsAlt`; `m_movementKeys` holds only modifier-less bindings and
`isMovementKey()` has no caller outside the manager. Pinned by
`T471_TheMeasuredCameraKeysCollideWithNothing` (each key+modifier reaches
exactly one binding; the bare keys keep their meaning). Two findings reported,
not fixed: fable's kept zoom keys collide with the official MEANINGS of Shift+- /
Shift+= (Clip plane) and Numpad- (Screenshot), and fable's F12 is Screenshot
where the official F12 is Toggle mouse look (**B652**); and the `--egl-x11`
device maps no numpad digit at all (`XLookupKeysym(..., 0)` returns the
NumLock-off keysym, which `x11KeysymToIrrlicht()` has no case for; DRM/evdev is
unaffected; **B654**, from code). **F10, F11, L and Alt+C are untouched.**

What each key moves: Pitch up/down pitch the ACTIVE USER camera (measured in
User 1 by T466). Tether-slide and Raise/Lower act on the TETHER camera only
(world azimuth; look-at height): T466 pressed both in User 1 and nothing moved
(0.047, 0.022) while Pitch up in the same session did, so the keys reached the
client and do not act on User 1. Tether is the camera the keys are named for.
That is a choice by name, not a measurement, and the steps (5 degrees, 1 unit,
2 units per zoom notch) are fable's (**B653**). "Right" is the viewer's right:
Irrlicht is left-handed and the renderer maps EQ (x, y, z) to (x, z, y), so the
screen's right for a horizontal view (fx, fy) is (fy, -fx), and sliding right
SUBTRACTS azimuth. FORWARD_CAM/BACK_CAM = Raise/Lower is inferred from the order
shared by `eqgame.exe`'s action table and eqstr 6692-6697, and is said so in
`hotkey_manager.h`.

### 4. The pitch sign: no fix was needed, and T466's claim was wrong

T466 (and B531) said fable's `CameraPitchUp` tilts the view DOWN. The arm has
been `orbitCamera(0.0f, -kCameraOrbitKeyStepDeg)` since T461 (commit `a91411bb`,
the only commit that touched it). A negative rig pitch LOWERS the camera around
a fixed look-at point, which tilts the view UP. That is what the oracle shows:
User 1 after Alt+PgUp x10, the scene moved DOWN the screen by the whole +60 px
search window (`oracle_adjust_keys_image_shift.tsv`). T466 read the action
name, not the arm. `PitchUpTiltsTheViewUpAsTheOfficialKeyDoes` measures the
view direction's vertical component through `processCommonInput()`. **It passed
before any T471 change** (so it is a pin, not a red/green pair), and B531 now
records the correction.

### 5. B581: Chase places its camera on entry

`PlayerCameraComponent::setCameraMode()` now calls
`CameraController::setFollowPosition(ctx_.playerX, ctx_.playerY, ctx_.playerZ,
ctx_.playerHeading)` when it enters Chase, beside T74's first-person placement
and T461's rig placement. The arguments are those of the per-frame movement
path (G06 statement 402). `CameraChaseEntryTest.EnteringChaseFromAnyOtherCameraPlacesTheChaseCameraAtOnce`
puts a real EDT_NULL camera node and controller into the bare renderer, stands
the player at T466's baseline pose, and requires the node to land on the follow
pose when Chase is entered from Overhead, First Person, User 1, User 2 and
Tether. It asserts first that each starting pose is more than 1 unit away, so
no case can pass vacuously. It failed before the fix and passes after.
`placeCameraForZoneIn()` re-enters the mode and then calls
`setPlayerPosition()`, which re-places Chase with the same controller state, so
the zone-in path adds one placement and no new state.

### 6. Chase-as-rig: fable's Chase does not reproduce the fit (B650)

Computed at `setFollowPosition()`'s defaults (preferred distance 20, eye 6,
+4 above the eye, look-ahead min(d/2, 20); no collision) for T466's Testchar,
whose collisionZOffset is 3.119 (`baseline_h160/camera_replies.txt`: z 3.119,
feetZ 0.000), against T466's fit (az 0, pitch 6, dist 4.0, height 7.0 above the
feet):

| | fable Chase | T466 Chase fit | difference |
|---|---|---|---|
| camera behind the player (horizontal) | 20.000 | 3.978 | 16.022 (x5.03) |
| camera above the feet | 13.119 | 7.418 | 5.701 |
| look-at point | 10.0 AHEAD, 9.119 above the feet | on the player's axis, 7.000 above the feet | -- |
| view pitch (down) | 7.595 deg | 6.000 deg | 1.595 deg |

As a rig about the point where its view ray crosses the player's axis, Chase
is pitch 7.595, dist 20.177, height 10.452. The fit's own sensitivity rejects a
distance 0.5 off (0.767 / 0.839 against 0.738), so the difference is well
outside it. Not applied (the plan said record and board): **B650**.

### 7. Tests: red before, green after

Red run: the test edits compiled against the UNCHANGED product code (full base
build exit 0, incremental exit 0), logs in the unit scratch `red/`.

| binary | red before (failed) | green after |
|---|---|---|
| `test_camera_modes` | `TheDefaultIsTheOfficialLoginCameraFirstPerson` | 26/26 |
| `test_camera_mode_wiring` | `TheF9ActionWalksTheOfficialCycle`, `TetherSlideRightMovesTheTetherCameraToTheViewersRight`, `EachAdjustKeyMovesOnlyItsOwnCamera`, `UserAndTetherCamerasHoldAWorldDirectionWhenThePlayerTurns`, `CameraChaseEntryTest.EnteringChaseFromAnyOtherCameraPlacesTheChaseCameraAtOnce`, `CameraModeWiringSource.TheReceiverQueuesTheCameraActionsForTheFrame` | 14/14 |
| `test_camera_zone_in_preference` | `AChoiceMadeThisSessionSurvivesAZoneLoad`, `ACharacterWithNoSavedViewGetsTheOfficialLoginCamera` | 6/6 |
| `test_irrlicht_camera_command` | 9: the default-mode, cycle and unknown-name cases, `GetPrintsTheModeTheFourValuesAndThePose` (az_frame), `GetReportsOverheadsAzimuthInThePlayerHeadingFrame`, `APlayerMoveDoesNotOverwriteTheOrbit` (world pose) | 28/28 |
| `test_player_camera_component` | the three default-mode cases | 9/9 |
| `test_ui_preferences` | `AnAbsentShowKeyKeepsTheDefault`, `CameraViewAcceptsOnlyTheSixOfficialCameras`, `ALegacyCameraModeIsMappedOntoTheOfficialSet`, `DefaultsMatchThePanelStructs` | 22/22 |
| `test_hotkey_manager` | `T471_TheMeasuredCameraKeysInTheDefaults`, `T471_TheMeasuredCameraKeysCollideWithNothing`, `T471_TheShippedHotkeysJsonCarriesTheMeasuredCameraKeys` | 62/62 |

28 red before, all green after. Passed in the red run too (pins, not new
behaviour): `PitchUpTiltsTheViewUpAsTheOfficialKeyDoes`,
`ASavedChaseWinsOverTheFirstPersonDefault`, `OverheadStillTurnsWithThePlayer`,
`ThePitchActionsMoveTheActiveUserCamera`. Written after the implementation
because they name API that did not exist (flagged; the behaviour each covers
was red above): `AWorldAzimuthIgnoresTheHeading` and
`TheAzimuthFrameIsTheMeasuredOne` (red via the world-direction case),
`RaiseAndLowerMoveOnlyTheTetherLookAtHeight` (red via the receiver pin and the
key tests). **Missed by the pre-change sweep, caught by the first full suite
(flagged):** three more expectations hard-coded the old default. They were
`UiPreferencesTest.ANullBindingMemberIsSkipped` (a literal `3`, now
`kDefaultCameraMode`) and both `RendererCommandEchoTest` echo cases (the
default `/camera` reply text, whose negative case would otherwise pass
vacuously). All three were updated and re-run.

### 8. Goldens

No golden file changed (`git status tests/goldens` clean), and none needed an
in-place update: `setCameraMode()` and `processCommonInput()`'s camera arms are
not pinned statements. `test_movement_order` (G06) and `test_frame_render_order`
(G05) pass standalone, with 14 other guards for the TUs this branch touched:
`test_application_init_order`, `test_command_registration_order`,
`test_first_person_camera_single_formula` (16), `test_debug_hotkey_arms`,
`test_autorun_arm`, `test_frame_component` (21), `test_reachability_sweep`,
`test_camera_bsp_ceiling`, `test_ui_preferences_identity`,
`test_slash_commands_component`, `test_zone_in_z_conversion`,
`test_player_moved_heading_units`, `test_bridge_intent_table`,
`test_data_path_routing`. Censuses standalone: `check_doc_counts.py` OK
(`HotkeyAction` 99 -> 101 in `src/client/input/CLAUDE.md`),
`check_zero_callers.py --check` clean (414), `check_write_only_fields.py
--check` clean (67).

### 9. Verification

| step | result |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0 (base 0; red 0; green 0 with 0 `error:` lines; the two test-fix rebuilds 0). No exit 143 anywhere |
| 2 `ctest -j6` | First run: 4766/4768 -- the two `RendererCommandEchoTest` cases hard-coded the old default reply (see 7). **Final run: PASS, 4768/4768, exit 0, 438.78 s**, including `FileSizeCeiling`, `ZeroCallers`, `WriteOnlyFields`, `DocCounts` and both `HeadlessShutdownTest` cases |
| 3 `build-gles2-x86.sh` | PASS, exit 0 (started only after the main build's exit file) |
| 4 visual gate | PASS inside both suite runs (final: `visual_gate` Passed, 42.77 s); `git status tests/goldens` clean, so no image golden moved. The gate renders through `gles2_zone_benchmark`'s own camera, which reads no camera mode |
| 5 `build-arm-noble.sh` | PASS, exit 0, 0 `error:` lines (started after the final suite; `build-arm-noble/bin/willeq` rebuilt 03:37) |
| 6 `check_file_sizes.sh fable` | PASS: 19 changed files ok, 0 warn, 0 fail (largest `tests/test_hotkey_manager.cpp` 1,187, `renderer_frame_input.cpp` 1,052). Re-run after the last test edit: 33 files, 0 warn, 0 fail. The extra files are `fable` moving underneath the branch (T470 merged as `a5f18512`, tip now `adf1985d`), because the checker diffs against the `fable` tip, not the merge base `4f94ba16` |

### 10. Board

Closed: **B580** (world azimuth), **B581** (Chase entry pose). Narrowed:
**B530** (login camera and world frame applied; Chase, starting directions and
steps still owed), **B531** (every measured key bound; the "mirrored Pitch up"
claim corrected). New: **B650** (A, Chase geometry differs from the fit),
**B651** (C -> ORACLE-OWED, the starting world direction of User 1/2/Tether;
old ini azimuths now read as world), **B652** (C, fable's own zoom keys and F12
against the official meanings), **B653** (A, owed: what Raise, Lower and
Tether-slide move, and every step), **B654** (A, unconfirmed: `--egl-x11`
drops the numpad digits).

### 11. Deviations (flagged)

1. **No entry capture of the world azimuth** (section 2): T466's own marks
   show that capturing from the entry heading would disagree with the oracle.
2. **No pitch sign change** (section 4): the sign already matched the oracle;
   the plan's premise came from T466's reading of the action name.
3. **`CameraOrbitLeft/Right` retargeted from the user cameras to Tether**, and
   `CameraRaise/Lower` act on Tether only (section 3). The effect is ORACLE-OWED
   (B653).
4. **fable's own zoom keys kept** beside the official ones (B652), rather than
   removed: the plan said bind where fable has the action, and the UI keymap
   question is Will's.
5. **Files outside the named territory**, each required by the change:
   `include/client/input/hotkey_manager.h` (two enumerators, comments),
   `include/client/graphics/renderer_types.h` (two `RendererAction`s),
   `src/client/graphics/renderer/renderer_event_receiver.cpp` (two queue
   cases), `src/client/graphics/frame/renderer_frame_input.cpp` (the arms),
   `src/client/input/CLAUDE.md` (the DocCounts number),
   `tests/test_renderer_command_echo.cpp` and
   `tests/test_player_camera_component.cpp` (default-mode text).
   Nothing of T472's (entity/equipment attachment, model yaw) or T470's
   (`door_manager*`, `application_zone_loading.cpp`) was touched.
6. **No live proof**: no lock was assigned. B580 and B581 are closed on
   red/green tests; a live `/camera overhead` -> `/camera chase` -> `/camera
   get` and a turn-in-User-1 capture are owed to the next lock holder.
7. **Board range:** B650-B654 used of B650-B659.
8. **`fable` moved during the unit, and the branch is NOT rebased (protocol:
   no merge).** T470 merged as `a5f18512` (tip `adf1985d`). Two files overlap
   with this branch. `refactor/BOARD.md` is append-against-append (T470's
   entries and T471's section; keep both). In
   `tests/test_camera_zone_in_preference.cpp`, T470 changed
   `BothZoneLoadersUseTheOneEntryPointAndNameNoMode` (2 -> 1
   `placeCameraForZoneIn(` calls) and T471 changed the test just above it
   (`ACharacterWithNoSavedViewGetsChase` ->
   `...GetsTheOfficialLoginCamera`) and added
   `ASavedChaseWinsOverTheFirstPersonDefault`. The hunks are adjacent, not
   overlapping, and a correct merge keeps T470's count of 1. **Merge rule:**
   re-run `test_camera_zone_in_preference`, `test_movement_order` and
   `test_frame_render_order` on the merged tip. T470 owned
   `application_zone_loading.cpp`, and this branch's green run saw its
   pre-T470 form.
