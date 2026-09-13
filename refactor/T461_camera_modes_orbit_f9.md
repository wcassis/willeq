# T461 — the official camera set on F9, an orbit/zoom camera, and `/camera` exact placement

Class C → ruled (Will, 2026-09-13; `wills-standing-decisions`), **Leg A: no
lock**, slot1. Feature unit.

## Will's ruling

- Match the official client's camera set; **F9 cycles modes in the official
  order and REPLACES Ctrl+F5 (no alias).**
- Include the official orbit mode: the camera orbits the player and zooms
  in/out.
- Add a **`/camera` command for exact placement**: azimuth (around the
  player), pitch, distance, look-at height above the feet — set absolutely,
  nudge by an exact step, and read back (print the current values). Purpose:
  the held-item orientation defect needs goldens from the official client;
  the oracle rig will put the official client in one of its fixed cameras,
  measure that camera, and set fable's to match (near-match, not byte-exact).

## Today (orchestrator triage, confirm)

`PlayerCameraComponent` (`src/client/graphics/player/renderer_camera.cpp`):
modes Free → Follow → FirstPerson via `cycleCameraMode()`, bound Ctrl+F5
(`src/client/input/hotkey_manager.cpp`, `HotkeyAction::ToggleCameraMode`);
`/camera [free|follow|fp|cycle]` exists (`renderer_slash_commands.cpp`); zoom
+/- in steps of 2, Follow only. Free mode's WASD fly routine
(`CameraController::update`) has NO caller, so Free just freezes the camera.
`orbitAngle_` / `setOrbitAngle` / `adjustOrbitAngle` exist but
`setFollowPosition` overwrites the angle with the player heading every call.
`[Camera] Mode=` is persisted per character (T432, `ui_preferences.cpp`) as the
numeric mode — changing the enum changes what a saved ini means.

## Leg A (this dispatch, offline)

1. **Evidence for the official set, from the client files, before any code.**
   `/mnt/projects/EverQuestP1999`: `eqstr_us.txt` / `dbstr_us.txt` strings for
   the camera mode names and cycle messages; `eqclient.ini` `[Options]`
   `Camera1-*`/`Camera2-*` keys (what "User 1/User 2" store: distance,
   height, heading, pitch?); `uifiles/default/*.xml` Options-window camera
   controls; the keymap defaults (`[KeyMaps]`, F9 = cycle, and the keys that
   rotate/zoom a user camera). Write the evidence table into the Review with
   file + line. Where the files cannot settle a fact (exact order, orbit
   controls, default distances), list it as a Leg B oracle measurement — do
   NOT invent it. Existing oracle footage: `review/T307`, `review/T431`.
2. **Design** the mode enum to the official set (names and order from 1).
   Modes whose exact geometry needs the oracle get a documented placeholder
   parameter set clearly marked `ORACLE-OWED` in code comment + board.
   First person and Follow keep their measured behaviour (T307/T309/T431 —
   do not change first-person framing or eye height).
3. **Orbit mode:** azimuth/pitch/distance/look-at height state owned by
   `PlayerCameraComponent`, not overwritten by `setFollowPosition`; keyboard
   orbit + zoom per the official keymap if found, mouse wheel zoom; pitch and
   distance clamped; BSP camera collision reused. The player's movement must
   not reset the orbit (the official user cameras hold their offset relative
   to the player's heading or to the world — find out which from the files;
   if unsettled, make it relative to the player's heading and mark it
   ORACLE-OWED).
4. **`/camera`:** keep existing subcommands; add e.g.
   `/camera set az <deg> pitch <deg> dist <u> height <u>`,
   `/camera nudge az <deg>` (etc.), `/camera get` → prints all four plus the
   mode and the resulting world camera position/target. Units stated in the
   help text. Setting a parameter while not in the orbit mode switches to it
   (say so in the output). Update `include/client/graphics/ui/CLAUDE.md`
   slash-command and key-binding tables and `config/hotkeys.json`.
5. **F9 replaces Ctrl+F5** everywhere: hotkey defaults, `config/hotkeys.json`,
   docs, tests. Check F9 is not already bound to something else (report and
   STOP if it is bound to a feature Will may use).
6. **Persistence:** `[Camera] Mode=` must stay readable — map the old numeric
   values (0 Free, 1 Follow, 2 FirstPerson) onto the new enum explicitly, with
   a test; persist the orbit parameters only if the official client persists
   its user camera (the `eqclient.ini` keys say so) — in `[Camera]`, not a
   new file.
7. Tests: mode cycle order, F9 binding, `/camera` parsing (set/nudge/get,
   bad input), orbit geometry (camera world position for known
   az/pitch/dist/height/player pose, including the heading convention per
   `docs/CLAUDE.md`), orbit not overwritten by a player move, ini migration.

## Proof

Standard verification bar; G-batch goldens for every TU touched (CLAUDE.md
merge rule). Visual gate: goldens must not move (the gate uses the default
camera). Media for Will: x86 `build-gles2` captures of each mode and an orbit
sweep (e.g. az 0/90/180/270 at a fixed pitch/dist) + `/camera get` output,
`/mnt/projects/fable/review/T461/` with README — no live server needed if the
tree has an offline zone view path; if it needs a server, do NOT take the
lock (T459 holds it): leave the captures to Leg B and say so.

File territory: `src/client/graphics/player/`, `include/client/graphics/player/`,
`src/client/graphics/slash/`, `src/client/input/hotkey_manager.cpp`,
`config/hotkeys.json`, `ui_preferences.*` camera key, docs named above, new
tests, `refactor/BOARD.md` B530–B539. **Do NOT touch the cursor/texture-cache
code or `src/client/movement/` (T460), nor Pi/eqemu.**

STOP: the official set cannot be established from the files at all (report the
table and what Leg B must measure, implement only orbit + `/camera` + F9 over
the existing modes); F9 conflict; an image golden moves.

## Leg B (later, lock): oracle measurement

Mode order and names in the running official client, the geometry of each
fixed camera (A/B search with `/camera` against oracle stills, per
`match-the-oracle-dont-infer-it`), orbit controls — then the held-item
orientation goldens.

## Review

### Leg A (slot1, `task/T461-camera-modes-orbit-f9`, base `9c164693`)

**STOP conditions:** none hit. The official set WAS established from the files
(names, set and order); F9 was bound to nothing (`KEY_F9` appears only in the
key-name table and the RDP scancode map); no image golden moved.

#### 1. Evidence — what the client files settle (file + line)

| # | Fact | Evidence |
|---|---|---|
| E1 | The six official cameras and their names | `/mnt/projects/EverQuestP1999/eqstr_us.txt` lines 4200-4205: `9500 First Person Camera`, `9501 Overhead Camera`, `9502 Chase Camera`, `9503 User Defined Camera 1`, `9504 User Defined Camera 2`, `9505 Tether Camera` |
| E2 | The same six are keymap actions | `eqgame.exe` strings (`strings -t x`): `FIRST_PERSON_CAMERA` 0x240424, `OVERHEAD_CAMERA` 0x240414, `CHASE_CAMERA` 0x240404, `USER1_CAMERA` 0x2403f4, `USER2_CAMERA` 0x2403e4, `TETHER_CAMERA` 0x2403d4 (in the `KEYMAPPING_` action-name table, 0x24b040) |
| E3 | The cycle action and its label | `eqgame.exe` `TOGGLECAM` 0x240c34; `eqstr_us.txt` line 3122 `6536 Toggle camera view` |
| E4 | Zoom / pitch / center actions | `eqgame.exe` `ZOOMOUT` 0x240c40, `ZOOMIN` 0x240c48, `CENTERVIEW` 0x240c50, `PITCHDOWN` 0x240c5c, `PITCHUP` 0x240c68; `eqstr_us.txt` lines 3123-3124 `6537 Zoom in`, `6538 Zoom out` |
| E5 | User-camera orbit actions | `eqgame.exe` `PITCHDOWN_CAM` 0x2404e0, `PITCHUP_CAM` 0x2404f0, `LEFT_CAM` 0x2404fc, `RIGHT_CAM` 0x240508, `BACK_CAM` 0x240514, `FORWARD_CAM` 0x240520; `eqstr_us.txt` lines 3278-3284 `6692 Raise camera` .. `6698 Center camera` (incl. `6694/6695 Tether-slide camera right/left`, `6696/6697 Pitch camera up/down`) |
| E6 | The official client PERSISTS both user cameras | `/mnt/projects/EverQuestP1999/eqclient.ini` lines 417-430 (`[Options]`, section header line 415): `Camera1-Distance=30`, `Camera1-DirHeading=192`, `Camera1-Heading=0`, `Camera1-Pitch=0`, `Camera1-Height=5`, `Camera1-Zoom=90`, `Camera1-Change=1`; `Camera2-*` = 82 / 277 / 0 / 0 / 18 / 90 / 1. Identical in `eqclient-obian.ini`, `eqclient-seth.ini`, `eqclient-bilblow.ini`, `p1999-obian.ini`, `p1999-obian2.ini` (lines 416-429); `p1999-bilblow.ini` 416-417 has `Camera1-Distance=49`, `Camera1-DirHeading=333.300323` — a user-adjusted, fractional value |
| E7 | Mouse-wheel zoom exists and is on | `eqclient.ini` line 452 `MouseWheelZoom=1`; `uifiles/default/EQUI_OptionsWindow.xml` lines 2683-2698 `OMP_MouseWheelZoomCheckbox` "Mouse Wheel Zoom"; `eqgame.exe` `MouseWheelZoom` 0x24ab98 |
| E8 | Default KEYS are NOT on disk | `eqclient.ini` `[KeyMaps]` (line 526) holds only four movement overrides (`KEYMAPPING_FORWARD_2`..`DUCK_1`); across all 11 ini files the only `KEYMAPPING_` lines are movement ones. A keymap line exists only for a key the user changed (T382's finding, `tests/test_hotkey_manager.cpp`) |
| E9 | The keys live on an Options > Keys > Camera page | `EQUI_OptionsWindow.xml` line 2879 `<Choices>Camera</Choices>` in `OKP_KeyboardFilterCombobox` |
| E10 | No official "Free" camera | E1/E2 list six cameras; none is free-fly |

**ORACLE-OWED for Leg B (board B530, B531):** that F9 is TOGGLECAM's key and the
running client's cycle order; the login camera; Overhead / Chase / Tether
geometry; the user cameras' DirHeading/Pitch units and zero, and whether they
are heading- or world-relative; `Height`'s meaning; `Zoom=90` and `Change=1`;
the zoom and wheel steps; the keys of E4/E5.

#### 2. Design as built

- **Enum** (`include/client/graphics/player/camera_mode.h`, device-free, re-exported by
  `renderer_types.h`): `FirstPerson=1, Overhead=2, Chase=3, User1=4, User2=5, Tether=6`,
  names verbatim from E1, `nextCameraMode()` cycles in that order. **Free is gone**
  (E10; it never had a body — T319). **Follow is renamed Chase** with its geometry,
  collision and zoom byte-for-byte unchanged, so the default-camera goldens hold.
  First person untouched (T307/T309/T431).
- **Rig modes** (Overhead, User 1/2, Tether): a `CameraRig` {azimuth deg relative to
  the player's heading, pitch deg (+ = above), distance, look-at height above the
  feet} owned per mode by `PlayerCameraComponent` (`renderer_camera_modes.cpp`).
  `computeCameraRigPose()` (`camera_rig.cpp`) is the one formula; az 0 reproduces the
  Chase camera's "behind" (`setFollowPosition()`'s `(sin h, cos h)` forward) and is
  pinned at every heading. `CameraController::placeRigCamera()` applies the SAME BSP
  PVS collision (else the triangle selector) along the look-at -> camera ray.
  `FrameComponent` re-places the rig every frame after movement and after the lift
  carry; nothing that moves the player writes a rig. Clamps: pitch ±89, distance
  1..300, height ±100 (fable's, not measured). User 1/2 defaults quote E6's
  Distance/Height/Pitch; azimuth 0 is a placeholder. Overhead/Tether are
  `ORACLE-OWED` placeholder rigs.
- **Zoom:** +/- keys and — new, E7 — a mouse-wheel notch the static UI does not
  consume, both through `zoomCamera()` (Chase distance or the active rig's; first
  person does not zoom). Still one wheel read per frame.
- **Keyboard orbit:** `HotkeyAction::CameraOrbitLeft/Right`, `CameraPitchUp/Down`
  -> `RendererAction` -> arms in `processCommonInput()`, 5° per press on the ACTIVE
  user camera. **Unbound by default** (E8) — bindable in `config/hotkeys.json`.
- **`/camera`** (`slash/camera_command.h` grammar, device-free): no arg / `cycle` /
  `<mode>` / `set az <deg> pitch <deg> dist <u> height <u>` (any subset) / `nudge`
  (same, additive) / `get` / `help`. `set`/`nudge` outside a user camera switch to
  User Defined Camera 1 and say so; out-of-range values are clamped and the reply
  says so; bad input changes nothing and points at `help`. `get` prints
  `[CAMERA-GET]` lines: mode + view number, the four values with units, player pose
  and feet, the ideal rig pose, and the camera node's actual pose (all EQ client
  frame).
- **F9:** bound in the defaults and in `config/hotkeys.json`; Ctrl+F5 removed from
  both, no alias. **Ctrl+F5 had never worked:** the receiver queued
  `RendererAction::ToggleCameraMode` and `processCommonInput()` had no arm, so the
  plan's triage ("bound Ctrl+F5") described a dead key. T461 added the arm.
- **Persistence** (`ui_preferences.*`): `[Camera] View=<1..6>` (new key — the old
  `Mode=` numbering collides at 1 and 2) plus `User1Azimuth/Pitch/Distance/Height`
  and `User2*`, persisted because E6 shows the official client persists them. A
  legacy `Mode=` is read and mapped explicitly (0 Free -> Chase, 1 Follow -> Chase,
  2 FirstPerson -> First Person); `View=` wins if both appear; the writer emits
  `View=` only.

#### 3. Verification

| Step | Result |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0 (first run exit 2: one compile error in a new test — `HotkeyManager` has no `getAllBindings()`; fixed) |
| 2 `ctest -j6` | 4749 / 4750. The one failure, `WriteOnlyFields`, was CAUSED by this unit: deleting `CameraController::update()` left `mouseSensitivity_` write-only. Its writer is in `irrlicht_renderer.cpp` (T460's territory), so it is exempted in `check_write_only_fields.py` against B533. Standalone re-run after the exemption: `write-only census clean: 67 fields, all 67 classified in EXEMPT` |
| 3 `build-gles2-x86.sh` | PASS, exit 0 |
| 4 visual gate | PASS inside step 2 (`visual_gate` Passed, 34.49 s, against the rebuilt `build-gles2`); no file under `tests/goldens/*.png` changed |
| 5 `build-arm-noble.sh` | PASS, exit 0 (no `error:` in the log) |
| 6 `check_file_sizes.sh fable` | PASS: 27 changed files ok, and `--files` on the 8 new files ok (largest touched: `renderer_frame_input.cpp` 1,043) |
| G goldens for touched TUs | `movement_order_g06.txt` statement 401 learned IN PLACE (`CameraMode::Follow` -> `CameraMode::Chase`, the rename only; G-correction 32) — `test_movement_order` PASS; `test_frame_render_order` PASS; `test_first_person_camera_single_formula` PASS (the new TU writes no camera node and never branches on first person) |
| DocCounts | PASS — `HotkeyAction` 95 -> 99 updated in `src/client/input/CLAUDE.md` |

The table above is the ORIGINAL tip, `75f93130` on `9c164693`. It is superseded
by the rebased-tip table below.

#### 3b. Re-verification on the rebased tip (`41d42c64` on `fable` `cb9c3f3c`)

Rebased three times as `fable` moved: onto `2f0bd914` (T460), then `f70b6c97`
(T459), then `cb9c3f3c` (T456). Every rebase conflicted only in
`refactor/BOARD.md`, and each hunk was append-vs-append: T460's B520–B525,
T459's B505–B511 and T456's B479–B480 are all kept, with T461's B530–B535
after them. The auto-merged files were checked, not trusted: `CLAUDE.md` and
`src/client/graphics/CLAUDE.md` carry T460's `software_cursor_texture.cpp`
sentence and T461's `[Camera] View=` sentence; `docs/module_map.md` carries
T460's row and T461's three; `CMakeLists.txt` carries T460's and T456's sources
and T461's three; `application_zone_loading.cpp` differs from `fable` only by
T461's two `Follow` -> `Chase` lines.

Two changes made during the rebase, both flagged:

- **T459's per-frame camera trace** (`renderer_frame.cpp`, `LOG_TRACE(MOD_CAMERA,
  "T459 cam frame=...")`) printed `cameraMode()` as a bare integer. It compiles
  unchanged, but T461 renumbered the enum (Follow=1 became Chase=3,
  FirstPerson 2 -> 1, Free=0 gone), so the same number would mean different
  cameras before and after this merge. It now prints `mode=<n> (<official
  name>)`. Nothing else in the trace changed; T462 owns its future.
- **B533 narrowed.** The CameraController getters/orbit-angle residue it listed
  was already zero-caller before T461 and is **B494**'s CameraController group
  (`check_zero_callers.py`). B533 now covers only what T461's deletion of
  `update()` made write-only (`moveSpeed_`, `mouseSensitivity_` and their two
  init writes) and points at B494 for the rest.

The WriteOnlyFields exemption (`CameraController::mouseSensitivity_`, B533) is
still needed and still correct: `setMoveSpeed()` / `setMouseSensitivity()` are
still called from `irrlicht_renderer.cpp` (lines 284–285 after T460), and their
only reader stays deleted. T459's removal of `getActualFollowDistance` from
`check_zero_callers.py` does not touch it: that exemption is in
`check_write_only_fields.py`, and `getActualFollowDistance()` is now called by
T459's trace.

| Step | Result on `41d42c64` |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0 (no `error:` in the log) |
| 2 `ctest -j6` | 4761 / 4762. The one failure, `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`, printed NO client output inside its 5 s window under `-j6` — the load artefact `EXECUTOR_PROTOCOL.md` names for this test (T360). It is a headless, no-graphics client with no camera code in its path, and it was green in the pre-rebase full run. **Standalone: both `HeadlessShutdownTest` cases PASS, and PASS again under `--repeat until-fail:3`.** `WriteOnlyFields`, `ZeroCallers`, `DocCounts` and `FileSizeCeiling` all Passed inside the run |
| 3 `build-gles2-x86.sh` | PASS, exit 0 (rebuilt after T456's `data/willeq.db` re-pin) |
| 4 visual gate | PASS inside step 2 (`visual_gate` Passed, 19.65 s, against that rebuilt `build-gles2`); no file under `tests/goldens/*.png` changed |
| 5 `build-arm-noble.sh` | PASS, exit 0 (no `error:` in the log) |
| 6 `check_file_sizes.sh fable` | PASS: 36 changed files ok, 0 warn, 0 fail |
| G goldens / source guards for every TU either branch touched | All 16 PASS standalone: `test_movement_order` (G06, T461's statement 401), `test_frame_render_order` (G05), `test_application_init_order` (G04), `test_command_registration_order` (G03), `test_zone_dispatch_table` + `test_zone_opcode_literals` (G01), `test_bridge_intent_table`, `test_self_death_sequence` (G07/G08), `test_reachability_sweep`, `test_first_person_camera_single_formula`, `test_autorun_arm`, `test_frame_component`, `test_player_moved_heading_units` (T460), `test_frustum_debug_draw`, `test_scene_ambient_recompute`, `test_options_merchant_residue` |
| Censuses (standalone) | `check_write_only_fields.py --check`: clean, 67/67 classified. `check_zero_callers.py --check`: clean, 416/416 classified. `check_doc_counts.py`: every stated count matches |

New tests: `tests/test_camera_modes.cpp` (23: set/names/order, ini + legacy
mapping, rig geometry incl. the heading convention and Chase equivalence, clamps,
every `/camera` form and bad input), `tests/test_camera_mode_wiring.cpp` (8: the F9
arm walks the cycle, orbit arms move only the active user camera, zoom, a player
move leaves every rig alone, persisted rigs range-checked, receiver/frame source
pins), `tests/test_irrlicht_camera_command.cpp` rewritten (27: round trips for all
six names and short forms, cycle order, Free/Follow rejected, set/nudge/get/help,
clamping, bad input, orbit not overwritten by a move), `test_ui_preferences.cpp`
(+5 incl. an on-disk pre-T461 file migrating and being rewritten),
`test_hotkey_manager.cpp` (+4: F9 in the defaults, F9 unique, orbit actions named
and unbound, F9 in the shipped JSON with no Ctrl+F5).

#### 4. Board

B530 (fixed-camera geometry + user-camera units, ORACLE-OWED), B531 (camera keys,
ORACLE-OWED), B532 (A, unconfirmed live: every zone load forces Chase and the
persistence then saves it), B533 (B: what deleting `CameraController::update()`
left write-only -- `moveSpeed_`/`mouseSensitivity_` and their two init writes; the
getter/orbit-angle residue is B494's, narrowed during the rebase),
B534 (B: the output-layer `output::CameraMode` route is stale and unreached),
B535 (A: `config/hotkeys.json` clears the defaults and omits Alt+C, L, F10/F11 and
Shift+F10/F11).

#### 5. Review media

`/mnt/projects/fable/review/T461/README.md` — **no captures**: the client has no
offline zone-view path and the eqemu lock is T459's, so per the plan the mode stills
and the orbit sweep are Leg B's. The README gives Leg B the exact `/camera` recipe.

#### 6. Deviations (flagged)

1. **Files outside the named territory**, each required by the change and none in
   T460's (`renderer_init.cpp`, `irrlicht_renderer.cpp`, `src/client/movement/` untouched):
   `camera_controller.h/.cpp` (collision reuse; `update()` deleted),
   `renderer_types.h` (enum include, four `RendererAction`s),
   `frame/renderer_frame_input.cpp` (the F9 arm, orbit arms, zoom, wheel, per-frame
   rig), `frame/renderer_frame_preferences.cpp`, `renderer/renderer_event_receiver.*`,
   `application_zone_loading.cpp` (Follow -> Chase, two lines), `CMakeLists.txt`,
   `help/renderer_command_catalog.cpp`, `scripts/refactor/check_write_only_fields.py`
   (one EXEMPT row), `scripts/test_fkey_input.sh`, root `CLAUDE.md`,
   `src/client/graphics/CLAUDE.md`, `README.md`, `docs/module_map.md`.
2. **Free removed and Follow renamed, no alias** — `/camera free` and `/camera follow`
   are now errors. Not spelled out in the plan; follows from "the official set" and
   Will's no-alias precedent (T363, this plan's F9).
3. **The orbit keys are unbound**, not "per the official keymap": the keymap is not on
   disk (E8). `/camera nudge` is the scriptable equivalent meanwhile.
4. **Persistence key changed** (`View=` instead of `Mode=`), because the two numberings
   collide; the user-camera keys use fable's names and units, not `Camera1-*`, because
   the oracle's units are unsettled (B530).
5. **Mouse-wheel zoom added** (E7), which retires T313's "the static UI is the wheel's
   sole consumer" statement (comments and `ui/CLAUDE.md` updated; the one-reader
   invariant and its test are unchanged).
6. **Only the two user cameras are adjustable** (`set`, `nudge`, orbit keys). Overhead
   and Tether are fixed placeholder rigs until Leg B measures them.
