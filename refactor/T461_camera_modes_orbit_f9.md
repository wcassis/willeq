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

(executor fills)
