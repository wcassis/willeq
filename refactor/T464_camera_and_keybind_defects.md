# T464 — camera and key-binding live defects: zone load resets the camera (B532), hotkeys.json wipes defaults (B535), the ceiling clamp latches (B507)

Class A, **no lock, no Pi**, slot1. Read T461's Review
(`refactor/T461_camera_modes_orbit_f9.md`) — it built the camera set these
defects live in — and T459's B507 evidence (`review/T459/README.md`).

## B532 — every zone load forces Chase, and the preference save persists it

T461: `application_zone_loading.cpp` calls `setCameraMode(Chase)` on each
zone load (two sites), after which `FrameComponent::syncUiPreferences()`
sees a changed value and writes it over the player's saved `[Camera] View=`.
Confirm with a pre-fix failing test (load a preference of e.g. User 1 →
simulate zone load → the mode and the written ini are still User 1). Decide
where the saved camera is applied relative to the forced reset — the saved
choice must win at zone-in, including first person (T074/T300 reposition
rules). Do not change what a character with NO saved view gets (Chase,
pending the oracle's login-camera measurement B530).

## B535 — `config/hotkeys.json` clears the default bindings

T461: loading the shipped `config/hotkeys.json` clears the compiled-in
defaults, and the file has no entries for Alt+C, L, F10/F11, Shift+F10/F11,
so those keys do nothing in production (tests that use the defaults stay
green). Establish the intended contract (file overrides per action vs file
replaces all) from `src/client/input/CLAUDE.md`, the loader and git history;
then make production reach every default binding: either the file carries
them all (a test that every `HotkeyAction` default is present in the shipped
file) or the loader merges. **F11 is bound to both a volume action and
hide-UI** — do NOT pick a winner: keep today's effective production
behaviour for F11, board the conflict as oracle-owed (the official keymap,
T461 Leg B), and say which one the file currently yields.

## B507 — the Follow/Chase BSP ceiling clamp latches the camera low

T459: `CameraController::setFollowPosition()` clamps height to
`regionBoundsMap_[bspPlayerRegion_].MaxEdge.Y`; in nro that region's AABB
top is −172 with the player at z −11..+29, so the camera latched at
`eyeHeight*0.5` (3.0 instead of 10.0) for the rest of the run; reproduces on
x86 (`/move` along the same path near (2617,−183)). `bspPlayerRegion_` is fed
a frame late and only on region change. Find why the region's bounds are
below the player (wrong region id? a leaf AABB in a different coordinate
frame — z-up vs y-up, the T332/T437 z history? stale region after a
transition?) and fix the root, not the clamp's symptom. Pre-fix failing test
from real nro BSP data if the loader can run headless (the zone BSP is in the
EQ files at `/mnt/projects/EverQuestP1999`), else a constructed region map
reproducing the measured numbers. Also check whether this clamp can ALTERNATE
(region flipping at a boundary) — Will's still-unreproduced "camera jerks
between two perspectives running forward in nro at 1280x720" (B508) is in the
same zone; report whether the code allows a two-state flip and under what
condition, even if you cannot reproduce it.

## Proof

Standard verification bar; G-batch goldens for every TU touched (G06
`movement_order` and `renderer_frame_render` are pinned — reviewed edits update
goldens in place per G-correction 32, and say so); visual gate (no image golden
moves); ARM; file sizes. Board B560–B569; strike B532/B535/B507 with evidence
(or mark B507 narrowed).

File territory: `src/client/graphics/player/`, `include/client/graphics/player/`,
`CameraController`, `src/client/input/` (hotkeys), `config/hotkeys.json`,
`application_zone_loading.cpp` camera calls, `renderer_frame_preferences.cpp`,
tests. **Not** `src/client/graphics/ui/` or frame timing (T462, slot3), **not**
`src/client/graphics/environment/` or the texture cache (T463, slot2).

STOP: the B507 root cause is in the BSP loader shared with game-state water/
zone-line detection (report before changing it); an image golden moves.

## Review

Slot1, `task/T464-camera-keybind-defects`, base `b9d97f25`. **STOP conditions:**
none hit. B507's root cause is in the camera's READ of the region boxes, not in
the BSP loader shared with game state (nothing in `zone_bsp.*`, `wld_loader.*`
or `zone_load/`'s box computation changed); no image golden moved.

### B532 -- confirmed, fixed

- **Confirmed** (code + red test; not live). `Application::loadZoneGraphicsOnThread()`
  and `Application::loadZoneGraphics()` (`application_zone_loading.cpp`, step 7)
  each called `setCameraMode(Chase)` on every zone load.
  `FrameComponent::setUiPreferencesIdentity()` re-arms the ini load only on an
  identity CHANGE, so `FrameComponent::syncUiPreferences()` loaded the file once:
  the first zone-in was right (the load runs on the frame after the reset), every
  later zone left the player in Chase, and save-on-difference wrote `View=3`.
- **Where the saved camera is applied.** The zone load no longer chooses a mode.
  Both loaders call one entry point, `PlayerCameraComponent::placeCameraForZoneIn()`
  (facade `IrrlichtRenderer::placeCameraForZoneIn()`), which calls
  `setCameraMode(cameraMode_)`, re-entering the current mode, and then
  `setPlayerPosition()`. Re-entering keeps every side effect a new zone needs:
  the first-person name-tag record on the player entity the zone just
  registered, the projection, and the immediate first-person/rig placement
  (T74/T300). The Chase default for a character with **no** saved view moved to
  the identity load: `syncUiPreferences()` sets `kDefaultCameraMode` before it
  reads the file, so a file stating `View=` still wins. Without that move, a
  second character in the same process would have inherited the first one's
  camera, a regression the old per-zone reset had been hiding.
- `PlayerCameraComponent::setPlayerPosition()`'s Chase arm now also checks
  `ctx_.cameraController` for null. Production always has one; the bare
  test renderer does not, and T461's tests worked around the missing check by
  using Overhead.

### B535 -- confirmed, fixed; F11 left as it is

- **Contract** (loader + history). `HotkeyManager::loadFromFile()` returns
  `false` on an open or parse failure before it touches `m_bindings`, and T399
  logs that case as "the SHIPPED key bindings are not loaded; the built-in
  defaults are in effect". A file that does load clears every binding.
  `saveToFile()` writes the complete set, and the original commit `610ce623`
  shipped the file as the full table. So the file **replaces** the defaults,
  and the defaults are only a fallback. Fix: the file carries every default.
  Five were missing: `ToggleCombatAbilities` Alt+C, `CycleObjectLights` L,
  `MusicVolumeDown` F10, `EffectsVolumeDown` Shift+F10 and `EffectsVolumeUp`
  Shift+F11. All five are added to `config/hotkeys.json`.
- **F11, not decided.** `setupDefaults()` binds F11 to `ToggleAllUI` (Global)
  and to `MusicVolumeUp` (Player). `HotkeyManager::getAction()` checks Global
  bindings first, so **F11 yields ToggleAllUI** in both the defaults and the
  file. The file binds `MusicVolumeUp` nowhere. That behaviour is unchanged,
  exempted by name in the test, and boarded as oracle-owed (**B561**).
  B535's "six" was really five: F11 never reached music volume up.
- Converse drift (census by action name): the file binds eight actions the
  defaults do not. Boarded as **B562**.

### B507 -- root cause found, fixed; the clamp could alternate

- **Root cause: a frame mismatch.** `ZoneLoadComponent::loadZoneStep2_BSP()`
  builds `regionBoundingBoxes` from EQ vertices, so the boxes are EQ Z-up.
  `SimBridgeComponent::buildSimulationZoneData()` ("EQ Z-up coordinates") and
  the zone debug draw (G05 golden statements 168-171 swap `MinEdge.Z` into
  Irrlicht Y) read them that way. `CameraController::setFollowPosition()` read
  `MaxEdge.Y` under a comment saying "AABB is in Irrlicht Y-up", so its
  "ceiling" was a horizontal extent. **Measured from nro's own BSP**
  (`WldLoader` headless over `/mnt/projects/EverQuestP1999/nro.s3d`): at
  T459's pose (2617.87, -181.57, -9.27) the feet, the eye and the frame-3538
  camera are all in region 1217. That region's box max is (2761.75, -171.47,
  10.81), and -171.47 - 0.5 is the logged "ceiling at -172.0". The region was
  not wrong, not stale and not in another zone. The latch holds for as long as
  the player stays in a region whose Y top is below z + 10.5, which in nro
  (y around -180, z from -11 to +29) is always.
- **Fix** (`camera_controller.cpp/.h`). The clamp reads `MaxEdge.Z` of the
  player's EYE region, and a box top at or below the eye does not count as a
  ceiling. The axis fix alone would re-latch a player standing on a region's
  highest vertex, such as a hilltop. The controller now finds the region of
  each ray's origin itself: the eye for Chase, the look-at point for
  `placeRigCamera()`. `setBspPlayerRegion()`, `bspPlayerRegion_` and their two
  feeders are deleted: `SimBridgeComponent::applySimulationOutput()` and
  `ZoneLoadComponent::loadZoneStep12b` in `renderer_zone_load_env.cpp`.
  At T459's pose the camera is now 10.0 above the feet.
- **B508: yes, it could alternate between two states.** The region it was fed
  was the SimulationWorker's `currentPvsRegion`, looked up from the **camera**
  position (`SimulationWorker::calculateRegions()`, `input.camEqX/Y/Z`), and it
  arrived a frame late and only when the region changed. The clamp's input was
  therefore its own output. The condition for a flip: a BSP split with a
  vertical component lies between the clamped (3) and unclamped (10) camera
  heights, and the two regions' boxes fall on opposite sides of the threshold.
  Then 10 puts the camera in the clamping region, 3 puts it in the other, and
  the height flips every sim update. A constructed BSP gave 3,10,3,10,3,10
  before the fix. **On T459's own path the precondition holds at 0 of 201
  steps**, so this is not proven to be Will's jerk. The same feedback also set
  the PVS origin of the wall test, which could shift the distance; that was
  eased on recovery, so it produces a sawtooth rather than two states. After
  the fix the clamp depends only on the player's pose, so a flip needs the
  pose itself to alternate (B525 (b)). B508 is marked narrowed, not closed.
- Side effect worth knowing: rig cameras (Overhead, User 1/2, Tether) had BSP
  wall collision **off** until the sim first reported a region *change*
  (`bspPlayerRegion_` started at `SIZE_MAX`, and the step-12b hand-off only
  covered Chase at load). They now collide from the first frame
  (`TheRigWallTestUsesTheLookAtRegion`).

### Tests (red before, green after)

| test | before the fix | after |
|---|---|---|
| `test_camera_bsp_ceiling` `AHorizontalExtentIsNotACeiling` | FAIL: height 3 instead of 10 (pre-fix API, `setBspPlayerRegion(0)`) | PASS |
| `test_camera_bsp_ceiling` `TheClampReadsTheEyeRegionNotTheCameraRegion` | FAIL: heights 3,10,3,10,3,10 when the camera region was fed back as SimBridge did | PASS (six identical placements) |
| `test_camera_bsp_ceiling` `TheVerticalTopOverTheEyeIsTheCeiling`, `ABoxTopBelowTheEyeIsNoCeiling`, `TheRigWallTestUsesTheLookAtRegion` | new behaviour, written with the fix | PASS |
| `test_camera_bsp_ceiling` nro x3 | measurement stage printed region 1217, max (2761.75,-171.47,10.81), 0/201 | PASS (asserted) |
| `test_camera_zone_in_preference` User1 / FirstPerson / in-session User2 survive a zone | FAIL x3: mode 3 (Chase), ini lost `View=4` / `View=1` / `View=5` (entry point in place with the old body) | PASS |
| `test_camera_zone_in_preference` `ACharacterWithNoSavedViewGetsChase`, source pin | PASS (regression guards) | PASS |
| `test_hotkey_manager` `T464_TheShippedFileCarriesEveryDefaultBinding` | FAIL: the five bindings named above | PASS |
| `test_hotkey_manager` `T464_F11KeepsItsProductionMeaningPendingTheOracle` | PASS (pins today's F11) | PASS |

### Verification (on `b9d97f25` + this branch)

| Step | Result |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0, no `error:` (the red-first build was exit 0 too). No exit 143 anywhere in the unit |
| 2 `ctest -j6` | PASS: **4777 / 4777**, exit 0, 183 s, with `visual_gate` inside it |
| 3 `build-gles2-x86.sh` | PASS, exit 0, no `error:` |
| 4 visual gate | PASS in the suite (#4764) and standalone (20.71 s). `git status tests/goldens` is clean, and no image golden moved. The gate runs `gles2_zone_benchmark`'s fixed camera path, which never touches `CameraController` |
| 5 `build-arm-noble.sh` | PASS, exit 0, no `error:` |
| 6 `check_file_sizes.sh fable` | PASS: 17 changed files ok, plus `--files` on the two new tests, `camera_controller.cpp` and `irrlicht_renderer.h` (1,195 lines), all ok |
| G goldens / guards for every TU touched | 25 binaries PASS standalone: `test_movement_order` (G06), `test_frame_render_order` (G05), `test_application_init_order` (G04), `test_command_registration_order` (G03), `test_zone_dispatch_table`, `test_zone_opcode_literals`, `test_bridge_intent_table`, `test_self_death_sequence`, `test_reachability_sweep`, `test_first_person_camera_single_formula`, `test_autorun_arm`, `test_frame_component`, `test_player_moved_heading_units`, `test_player_camera_component`, `test_camera_mode_wiring`, `test_irrlicht_camera_command`, `test_ui_preferences`, `test_zone_in_z_conversion`, `test_sim_bridge_component`, `test_zone_lifecycle_component`, `test_data_path_routing`, `test_data_paths`, `test_camera_modes`, `test_slash_commands_component`, `test_debug_hotkey_arms`. **No golden was edited**: none of the changed statements is pinned |
| Censuses | `WriteOnlyFields`, `ZeroCallers`, `DocCounts`, `FileSizeCeiling` PASS (all in the suite, and in a targeted run) |

### Board

Struck: **B507** (closed, root cause and evidence), **B532** (closed),
**B535** (closed; census corrected from six to five). **B508** narrowed.
New: **B560** (A, unconfirmed live: a second character in the same process
inherits the first one's windows and user rigs), **B561** (C, oracle-owed: the
F11 double binding), **B562** (B: eight actions only the shipped file binds),
**B563** (P: the region-box loop should be a function the test can call).

### Deviations (flagged)

1. **Files outside the named territory**, each required by the change:
   `sim/renderer_sim_bridge.cpp` and `zone_load/renderer_zone_load_env.cpp` (the
   two `setBspPlayerRegion()` calls deleted, comment and log text only),
   `zone_stream/renderer_zone_stream.cpp` (a stale comment naming the deleted
   setter), `irrlicht_renderer.h` (the `placeCameraForZoneIn` forwarder, the K11
   rule), `include/client/input/hotkey_manager.h` (contract comment),
   `src/client/input/CLAUDE.md` and `include/client/graphics/ui/CLAUDE.md` (a doc row;
   no `src/client/graphics/ui/` source touched).
2. **B535 fix shape: the file carries every default**, rather than having the
   loader merge. The contract reads as replace-with-fallback, and a merge would
   have changed what a user-edited file means.
3. **The "box top at or below the eye is no ceiling" guard** goes beyond fixing
   the axis. Without it, the corrected axis re-latches a player standing on
   his region's highest vertex. It is pinned by
   `ABoxTopBelowTheEyeIsNoCeiling`.
4. **No live proof.** No lock was assigned. B532 and B507 are closed on code,
   red/green tests and (for B507) nro's real BSP, not on a live run. A
   re-run of T459's x86 `/move` path showing a steady 10.0 camera height, and
   a zone-through with `/camera user1`, are owed to the next lock holder.
5. `PlayerCameraComponent::setPlayerPosition()`'s Chase arm gained a null check
   on `ctx_.cameraController`, so the zone-in entry point can be tested on the
   bare renderer. Production always has a controller.
