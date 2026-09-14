# T466 — T461 Leg B: measure the official cameras and keys on the oracle; then held-item orientation goldens

Class A/C-ruled, **eqemu lock + the eq-oracle rig** (official Titanium client
under Wine/Xvfb, `/mnt/projects/eq-oracle/`), slot1. Measurement unit first:
it produces oracle evidence, sets the ORACLE-OWED camera values it measures,
and captures held-item goldens. The held-item orientation FIX is a follow-up
unit planned from your evidence (say what you found; fix only if it is a
constant the evidence pins exactly and a test proves it).

Read: T461's Review (`refactor/T461_camera_modes_orbit_f9.md`, esp. the
evidence table and Leg B list), T464's Review, BOARD B530, B531, B561, B508,
`scripts/camera_match_sweep.sh` + `camera_match_score.py` (T310's A/B harness —
gradient correlation; a raw pixel diff between two renderers is meaningless),
`review/T307`/`review/T310`/`review/T431` (prior oracle camera work), and the
eq-oracle scripts (`login-as.sh`, `enter-world.sh`, `say.sh`, `capture-*.sh`,
`two-client-capture.sh`; `source /mnt/projects/eq-oracle/env.sh`; the lock is
the SAME one as `scripts/eqemu` — on the eqemu VM since 2026-09-12). Oracle
rig traps already recorded: `enter-world.sh` y offset (T428), the 'u' key not
firing under the rig (T438), crop against the WINDOW not the root, and never
sample a transient with stills (record video).

## Will's rulings in force

F9 cycles the official cameras in the official order (fable binds F9);
`/camera set|nudge|get` exists for exact placement; near-match goldens, not
byte-exact. The official client is the golden (`match-the-oracle-dont-infer-it`).

## Tasks

1. **Keys (B531, B561).** Screenshot the official Options > Keys page, Camera
   category (and every other category showing F9, F10, F11, Alt+C, L), T377's
   method. Record: the TOGGLECAM key, the zoom in/out keys, the user-camera
   orbit/pitch/raise/lower/tether-slide/center keys, what F11 is. Then press the
   cycle key repeatedly and record the mode ORDER from the on-screen/chat
   messages (eqstr 9500–9505), and which camera a freshly logged-in character
   starts in. Bind fable's unbound `CameraOrbitLeft/Right`, `CameraPitchUp/Down`
   to the measured keys only if they do not collide with an existing fable
   binding (report collisions; do not re-bind movement keys).
2. **Geometry (B530).** Same server-set pose + heading + race + resolution in
   both clients (a flat spot next to a distinctive wall — T310's qeynos2 wall is
   proven). For Chase, Overhead and Tether (and User 1/2 at the oracle's
   `eqclient.ini` defaults): capture the oracle still, then A/B search fable's
   rig parameters with `/camera set` (runtime-settable, no rebuild) using the
   T310 harness until the score peaks; report the fit ± its sensitivity. Also
   settle: does a user camera hold its offset relative to player heading or to
   the world (turn the character, compare); the unit/zero of
   `Camera1-DirHeading`/`Pitch`; what `Height`/`Zoom=90` mean (vary the ini,
   relaunch). Replace the `kOverheadPlaceholderRig` / `kTetherPlaceholderRig`
   placeholders and the Chase defaults with measured values; tests pin them with
   the provenance.
3. **Held-item goldens.** Kit Testcaster (or a melee class via
   `create-character.sh` if a wizard cannot hold a representative set) with a
   1H blunt, 1H slash, 2H, a shield, a held non-weapon (e.g. a torch/lantern)
   — `#gearup`/`#summonitem`, `#set gm on` first. With the MEASURED matching
   camera in both clients, capture the character idle and in one attack
   animation (video, stepped) from ≥3 azimuths. Produce side-by-side contact
   sheets per item and describe (not judge) the orientation difference per
   item: rotation axis/sign/grip offset as far as the frames show. Board the
   defect(s) with the frames (B570+... use B580–B589).
4. **Owed live proofs (x86 `build-gles2`, same session):** T464's B507 fix —
   T459's nro `/move` path, camera height over the player steady at 10.0
   (CameraFrame TRACE); B532 — `/camera user1`, zone through a zone line,
   `/camera get` and the per-character ini still say User 1; T462 on x86 at
   1280x720 with `--eqe1-path` — a still of the UI with the inventory open for
   Will.

## Proof

Media + README `review/T466/` (`oracle/`, `fable/`, `sheets/`). Any source
change (rig constants, key defaults) takes the standard bar (goldens for
touched TUs, gate, ARM). eqemu protocol: lock, world down at the end, poses
restored, oracle client closed. Board B580–B589; strike/narrow B530, B531,
B561, B508 with evidence.

File territory: `include/client/graphics/player/camera_rig.h` constants,
camera tests, `config/hotkeys.json` + `hotkey_manager.cpp` defaults (camera
actions only), `refactor/BOARD.md` (your range), `review/T466/`.
**Not** `src/client/graphics/ui/`, text/chat, frame timing (T462 merging),
not the shutdown test (T465).

STOP: the oracle rig cannot drive the Options window or the cycle key under
Xvfb (report exactly what failed; do the geometry at the default camera only);
an image golden moves.

## Review

Slot1, `task/T466-oracle-cameras-held-items`, base `e60f8f7e`, rebased onto
`6905c80f` (T458/T462/T465/T467 merged meanwhile; one append-vs-append conflict
in `refactor/BOARD.md`, both sides kept). Media and every number:
`review/T466/README.md`.

**STOP conditions:** not hit. The rig could drive the Options window, F9 and
the direct-camera bindings. No image golden moved (see Verification).

### 1. Keys (B531, B561) — measured

Oracle Options > Keys, Camera category: **Toggle camera view = F9**; Zoom in
NPAD7 / INSERT; Zoom out NPAD1 / DELETE; Raise Alt NPAD8 / Alt UP; Lower Alt
NPAD2 / Alt DOWN; Tether-slide right Alt NPAD6 / Alt RIGHT, left Alt NPAD4 /
Alt LEFT; Pitch up Alt NPAD9 / Alt PGUP, down Alt NPAD3 / Alt PGDOWN; the six
direct cameras unbound. UI category: **F10 = Toggle UI Display, F11 = Toggle
netstat, L = Toggle Leadership Window, Alt C = Toggle Combat Ability Window**.
No volume key on the Camera, UI or Commands pages.

**Cycle order** (each direct camera bound to Ctrl+F2..F7, then F9, scored against
all six; diagonal 0.026–0.114, off-diagonal ≥ 0.66): First Person → Overhead →
Chase → User 1 → User 2 → Tether → First Person — fable's order. The client
prints no camera name (chat, and a 2 fps OCR of the whole cycle video).
**Login camera: First Person** (untouched login frame 0.232 vs First Person,
≥ 0.897 vs the rest).

**fable's orbit/pitch actions were NOT bound (deviation from the task's intent,
flagged).** Alt+Left/Right and Alt+PgUp/PgDn collide with nothing (both lookups
match modifiers exactly), but under the rig only Pitch up moved User 1, and it
tilts the view UP where fable's `CameraPitchUp` (+pitch) tilts it down;
Tether-slide, Raise and Zoom scored at re-grab noise, so their effect is not
measured. Binding them would ship one mirrored key and three guesses.

### 2. Geometry (B530) — partly measured

| camera | result | fit ± sensitivity | applied |
|---|---|---|---|
| FOV | Chase vs First Person far scene at scale 1.00 (0.804; ≥ 0.91 at ±5 %) | one FOV, 62.8° horizontal | **yes**: `applyCameraProjectionForMode()` (was 72° vertical in third person) |
| Overhead | heading-relative (h160 vs h288 0.689) | pitch 83 ± 3, dist 45 ± 5, height 3 ± 3; min 0.774, neighbours 0.788–0.803 | **yes**: `kOverheadRig` |
| Chase | heading-relative (0.680) | as a rig: az 0, pitch 6, dist 4.0 ± 0.3, height 7.0 ± 0.3; min 0.738 (reproduced to 4 d.p. in two sessions), dist ±0.5 → 0.767/0.839, height −0.5 → 0.779 | **no**: fable's Chase is `CameraController::setFollowPosition()` (eye 6 + 4 up, 20 behind, look-ahead), not a rig and not in territory — B530 |
| User 1 | **world-anchored** (0.084) | coarse az sweep flat: best 0.836 at az 90 p20, six steps within 0.03 (crop floor 0.031) | no — ORACLE-OWED |
| User 2 | **world-anchored** (0.120) | best 0.794 at az 120 p20, on the pitch edge | no — ORACLE-OWED |
| Tether | **world-anchored** (0.193), follows a walking player | best 0.749 at az 300 p5 d20, on two edges | no — placeholder kept |

**User-camera frame:** heading-relative for First Person, Overhead, Chase;
world-relative for User 1, User 2 and Tether (B580; fable's rig is
heading-relative, so a fitted user azimuth would only hold at one heading).
**Ini semantics:** the official client does not read `[Options]` `Camera1-*`/
`Camera2-*` at launch — a variant (`Camera1-Heading` 90, `Camera2-DirHeading`
0) was rewritten to 0/277 during login and changed no frame (0.094 / 0.136) —
and does not write key-driven camera changes on exit (B585). DirHeading/Pitch/
Height/Zoom units are therefore not learnable from the ini.

### 3. Held items — partly captured

Oracle set A (Short Sword primary, Gloomingdeep Lantern secondary), idle from
Chase, User 1, User 2, Tether; fable set A idle from Chase, the Chase-fit rig,
Overhead and az 90/180/270. Descriptive (B588): oracle blade hangs DOWN from the
right fist, lantern in the left hand at the hip; fable blade stands UP, no
lantern drawn. B587: at the rig that reproduces the oracle's Chase view, fable
shows the character's face where the oracle shows the back of the head. **Not
captured:** any attack animation (oracle auto-attack produced no swing;
fable's `/target` by name found nothing — B586), sets B (1H blunt + shield)
and C (2H).

### 4. Owed live proofs (x86 `build-gles2` from the rebased tree, local capability-set copy, cmp-identical)

| proof | result | evidence |
|---|---|---|
| T464 **B507** — T459's nro `/move` path, camera height steady at 10.0 | **PASS.** Testcaster in nro, Chase, both `/move` legs executed. `/camera get` node camPos z minus player z: **10.000** at the start (2723.25, −41.75), **10.000** at the old clamp spot (2613.68, −186.69, where T459 logged "ceiling at −172.0"), **10.000** after the return leg. **0** "BSP camera ceiling" lines at DEBUG. Deviation: `/camera get` at each leg instead of a per-frame CAMERA TRACE (the client's `--log-level` is global; no module filter) | `review/T466/fable/proofs/b507_nro_move/run.log` |
| T464 **B532** — `/camera user1`, zone through a zone line, `/camera get` and the ini still User 1 | **Zone-line crossing NOT achieved; the zone load itself PASSES via `#zone`.** Two `/move` runs to the qeynos2→qeynos zone line (`data/zone_lines/qeynos2.json`, box x −35.96..−34.71): the first stopped 0.24 short, the second walked through the plane to server x −30.76 — one "Zone connection complete", no zone. Substitute: `/say #zone qeynos` → a second "Zone connection complete" and "Graphics loading complete"; `/camera get` in qeynos: `mode="User Defined Camera 1" view=4`, rig az 270 pitch 5 dist 12 height 4 unchanged; `UI_Testchar_WillEQ.ini` still `View=4`, mtime unchanged (no save of Chase). Owed: a real zone-line crossing (the rig's `/move` does not trigger the server zone point) | `fable/sessions/proof_b532_user1_zone_line`, `proof_b532_retry_through_plane`, `proof_b532_gm_zone_qeynos` |
| **T462** on x86, 1280x720, `--eqe1-path`, inventory open | **Captured** for Will (not judged): Testchar in qeynos2, daylight, Chase, `/inventory` → "New UI inventory: OPEN", frame 30 dumped 6.4 s after the open | `review/T466/fable/T466_fable_x86_build_gles2_1280x720_eqe1_inventory_open_T462_frame30.png` |

### 5. Verification (on the rebased tree, `c9f15701` on `fable` `6905c80f`; builds run strictly in sequence)

| step | result |
|---|---|
| red/green | `EveryCameraModeUsesTheOneMeasuredFov` **FAILED** with T466's `renderer_camera.cpp` diff reverse-applied (exported patch), passed restored; `OverheadIsTheOracleFit` **FAILED** with `kOverheadRig` at the old 70/80 placeholder, passed restored; `test_first_person_camera_single_formula` 16/16, `test_camera_modes` 24/24, `test_camera_mode_wiring` 8/8 (`review/T466/logs/redgreen_*.log`) |
| 1 `cmake --build build -j24` | PASS, exit 0, 0 `error:` lines. No exit 143 anywhere in the unit |
| 2 `ctest -j6` | PASS, **4741 / 4741**, 253 s, exit 0 — including `visual_gate` (22.15 s), `FileSizeCeiling`, `WriteOnlyFields`, `ZeroCallers`, `DocCounts`, and the camera/golden guards for the touched TUs (`test_first_person_camera_single_formula`, `test_camera_modes`, `test_camera_mode_wiring`, `test_player_camera_component`, `test_irrlicht_camera_command`, `test_movement_order`, `test_frame_render_order`) |
| 3 `build-gles2-x86.sh` | PASS, exit 0 (after step 1, per the new protocol) |
| 4 visual gate | PASS inside step 2; `git status tests/goldens` clean — **no image golden moved** |
| 5 `build-arm-noble.sh` | PASS, exit 0, 0 `error:` lines |
| 6 `check_file_sizes.sh fable` | PASS: 6 changed files ok, 0 warn, 0 fail (largest `test_first_person_camera_single_formula.cpp` 737) |
| eqemu protocol | lock taken as T466; world DOWN (`down.sh` exit 0) before every restore; restored with mariadb only; `character_data`, the two characters' `inventory`, account status, corpses and qeynos2 weather all **IDENTICAL** to the pre-session snapshot (`review/T466/data/*_before.tsv` vs `*_after.tsv`); mariadb down; lock released; oracle client exited and its Xvfb stopped; oracle `eqclient.ini` restored byte for byte |

### 5b. Server state the session changed and restored

Testchar (pose and two DB inventory rows), Testcaster (pose, moved by the B507
run), Oraclealpha (pose; a `#summonitem` Rusty Mace left on the cursor; two food
items one charge each consumed while standing in world; row guids renumbered by
the server's saves) — all restored and diffed identical. Runtime only, gone with
the zone processes: `#set time`, `#set weather`. Not restored, deliberately: the
oracle clients' `eqhost.txt` now point at the live eqemu VM (B583).

### 6. Board

Narrowed: B530, B531, B561. New: B580 world-anchored user/tether cameras,
B581 stale Chase entry pose, B582 official F10/F11/L meanings, B583 oracle
`eqhost.txt`, B584 dummies stacked on the canonical spot, B585 ini keys not
read, B586 attack capture owed, B587 player model yaw at the Chase view, B588
held-item orientation.

### 7. Deviations (flagged)

1. **`src/client/graphics/player/renderer_camera.cpp` and
   `tests/test_first_person_camera_single_formula.cpp` are outside the named
   territory.** The FOV is a measured camera value and every rig fit depended on
   it; the visual gate renders through `gles2_zone_benchmark`'s own camera, which
   never reaches that function. `kWorldVerticalFovRadians` now has no user.
2. **Chase defaults not replaced** (see §2): the measured pose is on the board.
3. **Orbit/pitch keys not bound** (see §1).
4. **Held items were equipped on Testchar with a DB inventory write** (two rows,
   character 65 had none; removed at the end), not with `#summonitem`: typing into
   the oracle while its UI was hidden did nothing, and the in-game swap would
   have cost a click-verified inventory drag per item.
5. **Oracle rig repaired:** `eqhost.txt` repointed at the eqemu VM (B583).
