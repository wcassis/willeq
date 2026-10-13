# B533 (issue #180): what T461's deletion of the free-fly camera left write-only

Card: B533, worked together with B494's CameraController rows (issue #169).
Branch `task/kb-b533-camera-fly-residue`, base `1dc42487` (origin/fable).
2026-10-11, class B hygiene: dead-state removal only, no behaviour change
(F9 cycle, chase, first person, the user-camera orbit all untouched).

## Background

T461 deleted `CameraController::update()`, the Free-mode free-fly body
(`refactor/T461_camera_modes_orbit_f9.md`). `update()` was the only reader of
`moveSpeed_` and `mouseSensitivity_`, so the two setters that wrote them
(`setMoveSpeed` / `setMouseSensitivity`, called once each from
`IrrlichtRenderer::setupCamera()`) became the write-only half — board B533,
with `mouseSensitivity_` exempted against B533 in
`scripts/refactor/check_write_only_fields.py`. The getter half of the same
residue was already zero-caller before T461 and was B494's CameraController
group in `scripts/refactor/check_zero_callers.py`. Card: work the two
together, one change to `camera_controller.*` + the two init lines + the
exemption rows.

## The per-name table (grep tree-wide before deletion:
src include tests tools benchmarks docker scripts)

All greps word-bounded; every hit outside `camera_controller.{h,cpp}` and
the two check scripts listed here.

| Name | Verdict | Evidence |
|---|---|---|
| `setMoveSpeed` | DELETED | only caller was `irrlicht_renderer.cpp:284` (setupCamera init) — deleted; the `setMoveSpeed` hits at `movement_state.cpp:288/291/294`, `player_state.cpp:83` and `player_state.h:118` are `GameState::player().setMoveSpeed` / `PlayerState::setMoveSpeed`, a different class (KEPT, untouched) |
| `getMoveSpeed` | DELETED | no occurrence outside the deleted set (KEPT nothing) |
| `setRotateSpeed` | DELETED | no occurrence outside the deleted set |
| `getRotateSpeed` | DELETED | no occurrence outside the deleted set |
| `setMouseSensitivity` | DELETED | only caller was `irrlicht_renderer.cpp:285` (setupCamera init) — deleted |
| `getYaw` | DELETED | no occurrence outside the deleted set |
| `getPitch` | DELETED | the one other hit, `docker/irrlicht-drm/COGLES2Texture.h:48`, is `COGLES2Texture::getPitch` (texture row pitch), a different class (KEPT, untouched) |
| `setOrientation` | DELETED | no occurrence outside the deleted set |
| `getHeadingEQ` | DELETED | no occurrence outside the deleted set (the EQ-heading names elsewhere in the tree are `HeadingEQ` in comments/wire structs, not this member) |
| `setOrbitAngle` | DELETED | no occurrence outside the deleted set |
| `getOrbitAngle` | DELETED | no occurrence outside the deleted set |
| `adjustOrbitAngle` | DELETED | no occurrence outside the deleted set |
| `moveSpeed_` | DELETED | reader: none (the `m_moveSpeed` in `player_state.h` is a different member) |
| `rotateSpeed_` | DELETED | reader: none |
| `mouseSensitivity_` | DELETED | reader: none — its reader was T461's deleted `update()` |
| `yaw_` | DELETED | only reader was the deleted `getHeadingEQ()` (cpp:146 pre-deletion); written by the ctor and `setOrientation()`, both deleted |
| `pitch_` | DELETED | only reader was the deleted `getPitch()`; written by the ctor and `setOrientation()`, both deleted |
| `MIN_PITCH` / `MAX_PITCH` | DELETED | only user was `setOrientation()`'s clamp |
| `orbitAngle_` | DELETED — checked per card | the live orbit path does NOT read it: `PlayerCameraComponent::orbitCamera()` (`renderer_camera_modes.cpp`) moves the user camera's rig pose in place and never names `orbitAngle_` (grep: no occurrence of the name outside the deleted set). Only readers were the deleted `setOrbitAngle`/`adjustOrbitAngle`/`getOrbitAngle` |

Kept, still live (for the record): `placeRigCamera`, `placeChaseCamera`,
`getLastPlacedDistance`, `getPositionEQ`, `setBspCollision`,
`setCollisionManager`, `isBspPointVisible`, `bspFindCollisionDistance` —
their callers are in `renderer_camera_modes.cpp` /
`renderer_player_lift.cpp` / the zone load path, unchanged by this unit.

The constructor no longer computes `yaw_`/`pitch_` from the camera's initial
direction; it now only binds `camera_`.

## Changes

- `include/client/graphics/camera_controller.h` — 12 members + 6 fields
  (`moveSpeed_`, `rotateSpeed_`, `mouseSensitivity_`, `yaw_`, `pitch_`,
  `orbitAngle_`) + 2 clamp constants + the ctor-init state gone; banner updated
- `src/client/graphics/camera_controller.cpp` — `setOrientation()`,
  `getHeadingEQ()`, `setOrbitAngle()`, `adjustOrbitAngle()` bodies and the
  ctor's yaw/pitch init gone
- `src/client/graphics/renderer/irrlicht_renderer.cpp` — the two
  `setupCamera()` init lines gone (`cameraController_` creation kept)
- `scripts/refactor/check_write_only_fields.py` — the
  `("CameraController", "mouseSensitivity_")` row gone (B533), comment names
  the closure
- `scripts/refactor/check_zero_callers.py` — the ten CameraController rows
  gone (B494's group, closed early for this class; B494 stays open with its
  other groups)
- `refactor/BOARD.md` — B533 struck (every mention; the T461 plan file is
  historical and keeps its record)

## Verification

- RED (deletions in, rows still present):
  - `check_write_only_fields.py --check` exit 1:
    `STALE exemption (no longer write-only): CameraController::mouseSensitivity_`,
    `0 unlisted, 1 stale`
  - `check_zero_callers.py --check` exit 1: 10 `STALE EXEMPT exemption
    (no longer zero-caller)` lines, one per deleted member,
    `zero-caller: 0 unlisted, 10 stale`
- GREEN (rows gone):
  - `check_write_only_fields.py --check` exit 0:
    `write-only census clean: 47 fields, all 47 classified in EXEMPT`
  - `check_zero_callers.py --check` exit 0:
    `zero-caller census clean: 364 functions, all classified in EXEMPT`
  - `ctest --test-dir build -L hygiene` green locally at the push head
    (CI re-runs it in the `hygiene` stage)
- Baselines for the deltas: the branch tip before this unit's deletions
  (`06852f31`, the pre-`origin/fable`-merge tip) read 48 write-only / 374
  zero-caller (`/home/user/kb-evidence/b533-camera-fly-residue/baseline_*.txt`)
  — so this unit's delta is exactly 1 (the `mouseSensitivity_` row) and 10
  (the CameraController group). The three `*Reproducible` hygiene tests first
  failed locally only because the partial `-j3` build had not built their
  generator tools (`generate_textures`, `eqe1_builder`, `willeq_db_builder`,
  `ui_atlas_builder`); built those and the full label is 28/28 (my change
  touches none of that path).
- Build: local `willeq` + `test_camera_bsp_ceiling` +
  `test_camera_mode_wiring` + `test_chase_camera_fit` built green at
  1dc42487-based tip (local, `-j3`); the full suite and the visual gate
  (frame identity) are CI's job on the pushed head.
- Not verified / left open: the Pi and the eqemu lanes (card is offline);
  the visual-gate frame comparison is asserted in CI, not re-blessed here
  (no golden touched, and nothing a player reaches changed).

## Issue notes

- #180 (B533): closed by this MR.
- #169 (B494): NOT closed — only its CameraController group is done; the
  other B494 groups remain open.
