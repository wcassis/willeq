# T474 — fable's Chase camera does not match the official Chase geometry (B650)

Class A (match the oracle), **Leg A offline + Leg B lock**, slot1. Read
T466's Review (`refactor/T466_oracle_cameras_and_held_items.md`, the Chase
row and the FOV finding), T471's Review (the B650 comparison table),
`review/T466/README.md`, and memory rule `match-the-oracle-dont-infer-it`
(summarised in T466's plan). The official client is the golden.

## The gap (T466 fit vs fable, T471's computation)

| | fable Chase | oracle fit (T466) |
|---|---|---|
| distance behind the player | 20.0 | 3.978 (± 0.3) |
| camera height above feet | 13.119 | 7.418 |
| look-at | 10 units ahead, 9.119 up | on the player, 7.0 up |
| view pitch down | 7.595° | 6.0° |

fable's Chase is `CameraController::setFollowPosition()` (eye 6 + 4 up, 20
behind, look-ahead), not a rig. T466's fit reproduced to 4 d.p. in two
sessions, and a distance 0.5 off already scores clearly worse.

## Tasks

**Leg A (offline).**
1. Make Chase's geometry match the fit: distance, camera height, look-at point
   ON the player, pitch — as runtime-settable parameters (not `constexpr`), so
   Leg B can A/B them with `/camera` without a rebuild. Decide whether Chase
   becomes a rig in `camera_rig.h` (heading-relative, per T466) or keeps
   `setFollowPosition()` with new constants; justify. Preserve what Chase has
   that a rig lacks: BSP wall pull-in and the T464 eye-region ceiling clamp;
   zoom (+/-, wheel, Numpad7/1, Insert/Delete) must act on Chase distance.
2. Define how the Chase fit scales with race (T466 fitted one character; the
   eye/height of T431's per-race work applies) — if the oracle evidence covers
   one race only, scale by the existing per-race eye height and board the
   unmeasured races, do not invent per-race numbers.
3. Tests: Chase camera world position/target for a known player pose and race
   equal the fit; zoom changes distance; wall pull-in and the ceiling clamp
   still act; heading-relative.
4. The visual gate: if a golden uses Chase and moves, STOP and report (Will
   blesses). Capture before/after x86 stills at a fixed pose for Will
   (`review/T474/`).

**Leg B (lock, same unit if Leg A lands clean):** on the oracle rig, re-run
T466's Chase A/B at the same qeynos2 spot with the new runtime parameters
(`scripts/camera_match_sweep.sh` + `camera_match_score.py`), confirm fable's
default Chase now scores at the fit's minimum, and repeat at a second spot and
for a second race (`create-character.sh` — e.g. a Dwarf or Ogre) if the rig
allows. Take the lock per protocol; world down, poses restored, oracle closed.

## Proof

Standard verification bar, builds strictly in sequence; G-batch goldens for
touched TUs (`movement_order`, `renderer_frame_render`, `renderer_player_physics`
pinned; in-place per G-correction 32, say so); visual gate (STOP on a moved
image golden); ARM; file sizes. Board B680–B689; strike/narrow B650.

File territory: `src/client/graphics/player/`, `include/client/graphics/player/`
(incl. `camera_rig.h`), `CameraController`, `/camera`, camera tests,
`review/T474/`. **Not** entity/equipment attachment or model yaw (T472, slot2),
**not** doors (T473, slot3).

STOP: an image golden moves; the Leg B re-fit disagrees with T466's by more than
its stated sensitivity (report both, do not pick).

## Review

Slot1, `task/T474-chase-camera-geometry`, base `8128640f`. Leg A done; Leg B ran
under the eqemu lock and **STOPPED** on the plan's STOP condition (section 7, board
**B683**). No image golden moved. Media and every number: `review/T474/README.md`.

### 1. Design: Chase became a rig

**Rig, not new follow constants.** The fit is a rig by construction: T466 measured
it with `/camera set` on a rig (az 0, pitch 6, dist 4.0, height 7.0), and
`computeCameraRigPose()` reproduces the measured geometry exactly. A tuned
`setFollowPosition()` would have to re-express the fit in its own terms: an
eye offset, a height above the eye, and a look-ahead point that the fit does
not have (the fit looks AT the player). It would also keep a second geometry
path beside the rig path for no measured reason. So:
- `camera_rig.h`: `kChaseRig{0, 6, 4.0, 7.0}` with the T466 provenance and bracket.
  `isRigCameraMode(Chase)` is true.
- `PlayerCameraComponent::chaseRig_` is runtime state. `/camera set|nudge` in
  Chase now edits it in place (before T474 it switched to User 1), and that is
  Leg B's A/B lever.
- `CameraController::setFollowPosition()`, `setFollowDistance()`,
  `adjustFollowDistance()`, `getFollowDistance()`, `getActualFollowDistance()` and
  `enableZoneInLogging()` are **deleted**. Their three callers now go through
  `updateRigCamera()`: movement, the lift carry, and the relocation/entry paths.
- **What Chase had that a rig lacked is kept.** The wall pull-in is
  `placeRigCamera()`'s ray from the look-at point. T464's eye-region ceiling
  clamp moved into `CameraController::placeChaseCamera(pose, eyeZ)`: the same
  vertical axis (`MaxEdge.Z`), the same eye region looked up from the pose, and
  the same "a top at or below the eye is no ceiling" guard. It clamps both the
  camera and the look-at, never pushing either below the eye.
- **Zoom** (+/-, wheel, Numpad7/1, Insert/Delete) acts on the Chase rig distance
  through the common rig path in `zoomCamera()`.
- Heading-relative, like the old Chase, with the renderer's forward vector
  (sin h, cos h). The 90° player/NPC heading offset is untouched, and so is
  first person.

### 2. The Chase parameters and the race scaling

| | pre-T474 Chase | T474 Chase (T466 fit) |
|---|---|---|
| camera behind the player | 20.0 | 4.0 · cos 6° = **3.978** |
| camera above the feet | 13.119 at defaults (**11.500 live** at T466's spot, where the eye-region ceiling clamped it) | 7.0 + 4.0 · sin 6° = **7.418** |
| look-at | 10 ahead, 9.119 up | **on the player's axis, 7.0 up** |
| pitch down | 7.595° | **6.0°** |

**Race scaling:** T466 fitted one character, so no per-race number was invented.
The rig is scaled **uniformly about the feet** by eyeAboveFeet / 6.023:
distance and height scale, azimuth and pitch do not (`scaleChaseRigForEye()`).
6.023 is T431's oracle-solved Human male eye, the fitted character's.
eyeAboveFeet is the head-bone eye fable already derives per race (T301/T431),
taken only while the player is standing and cached (`chaseEyeAboveFeet()`), so a
sit does not rescale. The fitted character therefore gets the fit exactly
(live: `scale=1.0000`). Every other race and every posture is unmeasured
(**B681**), and fable's Dwarf male eye (4.53) is still T431's +0.95 above the
oracle's.

### 3. Zoom, wall pull-in and ceiling clamp — as implemented and tested

- **Zoom**: +2 → the camera sits 6.0 from the look-at along the same ray, and the
  look-at is unchanged. −4 → 2.0. The zoom survives a player move.
- **Wall**: a wall at y = −2 behind a heading-0 player (fitted camera at
  y = −3.978, PVS-invisible) pulls the camera in to 1.5 (≥ 1.0) on the fit's ray,
  in front of the wall.
- **Ceiling**: an eye-region box top at 7.2 (above the eye at 6.023) puts the
  camera and look-at at 6.7 with the fit's horizontal offset kept. A top at
  eye − 1, or at 50, changes nothing. B507's case (a horizontal `MaxEdge.Y` is no
  ceiling), B508's case (the camera's own region is not read, six stable frames)
  and nro's real BSP (not latched; the full 4.0 used) are ported to the rig.

### 4. Tests: red before, green after

Red run: the new test file compiled against the UNCHANGED product code (build
exit 0 both times); logs in the unit scratch (`red_test_chase_camera_fit.log`,
`red2_...`).

| binary | red before | green after |
|---|---|---|
| `test_chase_camera_fit` (new) | first run 6 of 7 failed. The ceiling case passed because the old clamp also gave 6.7 and the case checked only heights; it was tightened to require the fit's horizontal position and look-at, and the second red run failed **7/7** | **7/7** |
| `test_camera_bsp_ceiling` | ported to `placeChaseCamera()` (the old API is deleted) | 8/8 |
| `test_camera_mode_wiring` | `EnteringChaseFromAnyOtherCameraPlacesTheChaseCameraAtOnce` now expects the fit; Chase added to three "no key moves it" checks | 14/14 |
| `test_camera_modes` | `RigAndUserModeClassification` (Chase is a rig); new `ChaseIsTheOracleFit`, `TheChaseFitPutsTheCameraWhereT471ComputedIt` and three `ChaseRaceScale` cases, **written with the API they name, so no red run of their own (flagged)** | 31/31 |
| `test_irrlicht_camera_command` | `SetFromChaseSwitchesToUserCamera1AndSaysSo` → `SetFromOverheadSwitchesToUserCamera1AndSaysSo` (Chase is edited in place; `CameraSetInChaseEditsTheChaseCamera` pins that) | 28/28 |
| `test_player_camera_component`, `test_camera_zone_in_preference`, `test_first_person_camera_single_formula` | — | 9/9, 6/6, 16/16 |

### 5. Goldens

`tests/goldens/movement_order_g06.txt` **learned an edit in place (G-correction 32)**:
two statements deleted from `publishPositionAndCamera()`, nothing inserted,
nothing moved, 456 → 454, both named above `kGoldenSize` in
`tests/test_movement_order.cpp`:
`if (cameraMode_ == CameraMode::Chase && ctx_.cameraController) {` and
`ctx_.cameraController->setFollowPosition(..., deltaTime);`. Chase is placed by
the `updateRigCamera()` FrameComponent calls right after movement.
`test_movement_order` 4/4 and `test_frame_render_order` 4/4. The CameraFrame
trace in `renderer_frame.cpp` is not a pinned statement. **No image golden
moved** (`git status tests/goldens` shows only the text golden). The visual gate
renders through `gles2_zone_benchmark`'s own camera.

### 6. Leg A live stills (x86 `build-gles2`, lock T474)

Before (pre-T474 binary) and after, captured identically: Testchar at T466's pose,
1024x768, `--eqe1-path`, `#set time 11 0`, weather 0, `/hideui`, `/camera chase`.
Scored on T466's own crop and oracle still: **before best 0.9951** (median
0.9962), **after best 0.7380** (median 0.7389), against T466's minimum of 0.7379.
The after node pose equals the fit to 3 d.p. Deviations: the first "after" run
dumped one frame, because the load outlasted the script's wait under host
contention; the steps gained 45 s and **both** stills were re-captured with the
same steps. The "before" binary is slot1's pre-T474 `build-gles2` from T471's
tree (provenance by string markers), not a build of `8128640f`.

### 7. Leg B — the re-fit, and the STOP

**The crop.** T466 did not record its Chase crop. Scoring T466's surviving fit-4
frames over ~7,500 crops, `400x600+0+100` reproduces T466's three published
edges to within 0.00005 (`review/T474/scripts/t474_recover_t466_chase_crop.py`).

| view (qeynos2 −74 428 3.751) | default Chase | re-fit minimum | T466 fit | verdict |
|---|---|---|---|---|
| **h160** (T466's view), vs T466's oracle still | **0.7383** | pitch 6 / dist 4.0 / height 7.0, **0.7384** (0.7381 repeated) | pitch 6 / dist 4.0 / height 7.0, 0.7379 | **agrees**; bracket reproduced to ≤ 0.001 (dist 3.5/4.5 0.7669/0.8388; height 6.5/7.5 0.7780/0.9225; pitch 3/9 0.9375/0.8797) |
| **h288**, vs T466's h288 oracle still | 0.8038 | pitch 6 / **dist 3.5** / height 7.0, **0.7847** (height 6.5: 0.7879) | dist 4.0 (0.8038 here) | **disagrees on distance by 0.5 > T466's ±0.3 → STOP** |

The h288 detail: dist 2.0 / 2.5 / 3.0 / 3.5 / 4.0 at pitch 6, height 7 →
0.8966 / 0.8883 / 0.8585 / 0.7847 / 0.8038; pitch 4.5 / 7.5 at dist 3.5 →
0.9401 / 0.8790. Per the plan both fits are reported and neither is picked;
`kChaseRig` stays at T466's fit, which the h160 re-fit reproduces. The h288 bowl
is shallow (dist 3.0 at height 6.5 is 0.8039, equal to dist 4.0), and the crop was
recovered for the h160 view. That is context, not a resolution. **Not run
because Leg B stopped:** a fresh oracle grab (the oracle client was never
launched; both views used T466's stills), a second spot, and a second race.

### 8. Verification (builds strictly in sequence; no exit 143 anywhere)

| step | result |
|---|---|
| 1 `cmake --build build -j24` | PASS, exit 0 (red 0, red2 0, green 0); 0 `error:` lines |
| 2 `ctest -j6` | 4784/4785, 646.55 s. The one red was `ZeroCallers`: `PlayerState::setFollowDistance` was unmasked by the deleted `CameraController::setFollowDistance` (a name collision, B684), and `EntityRenderer::getEntityPoseState`'s exemption went stale because T474 calls it. Fixed in `check_zero_callers.py`; then `ctest -R` ZeroCallers/WriteOnlyFields/DocCounts/FileSizeCeiling **4/4 PASS**, and the census runs clean standalone (414 exempt, 0 unlisted, 0 stale). The full suite was not re-run after that script-only change (flagged) |
| 3 `build-gles2-x86.sh` | PASS, exit 0 (started after the main build's exit file) |
| 4 visual gate | PASS standalone on the fresh `build-gles2` (`visual_gate` Passed, 40.89 s; it also passed inside step 2). No image golden moved |
| 5 `build-arm-noble.sh` | PASS, exit 0, 0 `error:` lines |
| 6 `check_file_sizes.sh fable` | PASS: 33 files ok, 0 warn, 0 fail. The extra files are `fable` moving underneath the branch (below) |
| eqemu protocol | lock taken as T474 (eqemu VM); `up.sh` adopted a running world (exactly one `bin/world`); every pose pinned and read back. Restore: the first attempt killed the world in the running container, found `/tmp/spire-launcher` had respawned `bin/world` (B483), and **stopped before writing**. The second ran `down.sh`, then mariadb only, then Testchar back to the pose it was FOUND at (qeynos2 −0.833, 102.717, −495.401 h231.75, not the documented restore pose), then `down.sh`. `character_data` IDENTICAL to the pre-session snapshot; lock released |

### 9. Board

Narrowed **B650** (the fit applied, live-scored, T471's arithmetic corrected).
New: **B680** (C → ORACLE-OWED: does the official Chase zoom — T466 saw no change
after Insert/Delete in Chase), **B681** (C → ORACLE-OWED: race and posture
scaling), **B682** (B: zoom-recovery easing gone; ceiling floor now the eye),
**B683** (A, the STOP: the h288 view disagrees on distance), **B684** (B: an
unmasked zero-caller in `PlayerState`). Range used: B680–B684.

### 10. Deviations (flagged)

1. **Files outside the named territory**, each required by the change:
   `src/client/graphics/frame/renderer_frame.cpp` (the CameraFrame trace read the
   deleted follow distance; it now prints the last placed rig distance),
   `renderer_frame_input.cpp` (two comments), `scripts/refactor/check_zero_callers.py`
   (census rows), `docs/module_map.md`, `src/client/graphics/CLAUDE.md`,
   `include/client/graphics/ui/CLAUDE.md`, `tests/test_movement_order.cpp` and its
   golden (in place), `tests/test_player_camera_component.cpp` and
   `tests/test_player_moved_heading_units.cpp` (comments).
2. **`/camera set|nudge` in Chase edits Chase** instead of switching to User 1
   (Leg B needs it); one test renamed.
3. **Zoom-recovery easing removed** and the ceiling floor changed (B682).
4. **Race-scale tests written with the API they name** (no red run of their own);
   the geometry, zoom, wall, ceiling and `/camera set` behaviour were red first.
5. **Leg B stopped at the h288 view** (B683); no fresh oracle grab, no second
   spot, no second race.
6. **The "before" binary is not a build of `8128640f`** (section 6).
7. **`fable` moved under the branch and it is NOT rebased:** T473 `807afccf` and
   T472 Leg A `a4783eb7`. The only file both touched is
   `src/client/graphics/frame/renderer_frame_input.cpp`: fable's hunk is at
   line ~884, and this branch changed two comments at ~110 and ~924. **Merge
   rule:** on the merged tip, re-run `test_movement_order`,
   `test_frame_render_order`, `test_chase_camera_fit`, `test_camera_bsp_ceiling`,
   `test_camera_mode_wiring` and `ZeroCallers`.
8. Local capability copies of both binaries remain in `/home/user/T474bin/`.
