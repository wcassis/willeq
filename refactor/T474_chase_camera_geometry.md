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

(executor fills)
