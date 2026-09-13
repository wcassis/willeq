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

(executor fills)
