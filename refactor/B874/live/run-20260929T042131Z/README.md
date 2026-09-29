# B911 / B912 -- the live pose capture after the fix (run-20260929T042131Z)

Every data file here was written by one real run and copied byte-for-byte;
none was typed, generated or edited. `client_log_excerpt.txt` is the output
of `grep -n -E "Script:|CAMERA-GET|pose-log|Camera mode|XYZ:|saved|Turned to"`
over the run's `client.log` (the same selection as the first run's excerpt).
`pose.tsv` is the complete log: header + 512 rows, sha256
`671fe831f72c0e61d7ead35f75563f2d05b163555f9dade4cc39b8c735dce260`, the value
`run_meta.txt` recorded at the end of the run. The whole client log and the
rig's own files are in
`/mnt/projects/fable/review/B874-live/run-20260929T042131Z/`.

The first run (`../`, run-20260929T022314Z) is the record of the defects and
is left untouched.

- **Command**: `EQEMU_HOST` unset (the eqemu VM lane, 172.16.200.99),
  `bash scripts/pose_live_capture.sh --owner b911-b912`; the client command is
  `command_line.txt`, the driving script `tour.txt` (identical to the first
  run's).
- **Lane**: VM, client config host `172.16.200.99`, checked against the
  selected lane before launch (`rig.log` line 2). Lock `b911-b912` taken at
  04:21:32Z and released by the rig's teardown; the snapshot restored
  byte-identically with the world down; `down.sh` ran.
- **Binary**: a Release build of branch `task/B911-B912-command-movement-render`
  -- the working tree that became the B911/B912 commit, on top of `5a13f1ab`
  (the hash `run_meta.txt` records, because the fix was not yet committed when
  the run started).
- **Verdict**: `comparison.tsv` / `rig.log`: **39 PASS, 0 FAIL**, rig exit 0.
- **Rehearsal**: `run-20260929T034054Z` (same tree, but linked against a stale
  copied Irrlicht whose only difference is the B654 X11 keysym map, a path a
  surfaceless run never enters) also gave 39/0. It is not committed; its files
  are in `/mnt/projects/fable/review/B874-live/`.

## The two checks that were red

| check | run-20260929T022314Z (before) | this run (after) |
|---|---|---|
| `#loc (after /turn)`: (128 - heading0_512) mod 512 vs the server's heading | FAIL: rendered 100.0 (heading0_512 28), server 0.0, delta 100 | PASS: rendered 0.0 (heading0_512 128), server 0.0, delta 0 |
| movement: movestate after `/move` | FAIL: `[0]` in every row | PASS: `[0, 1]` |
| total | 37 PASS / 2 FAIL | 39 PASS / 0 FAIL |

Measured in `pose.tsv` (not only the checker's verdict):

- `/turn 90` executed at 04:22:55.092 (client log); the first row with
  heading0_512 128 is row 106, ts 1790655775099 = 04:22:55.099, 7 ms later.
  The server's `#loc` 1.1 s later read heading 0.00.
- movestate is 1 on exactly rows 199-213 (15 rendered frames, 229 ms), the
  frames in which playerX goes 428.0 -> 438.515, and 0 on every other row --
  including all 299 rows after the walk ended.

## The "14 distinct positions" (issue #231's second half)

Not coarse stepping. In both runs the position changes on every rendered frame
of the walk and the walk is simply short: 10.47 units in 14 frames (first run),
10.52 units in the 15 movestate-1 frames here (14 distinct playerX values:
row 199 is movestate 1 at playerX 428.0 -- a published tick whose step was
zero-length, consistent with the integrator's first delta time being 0 --
and every row after it advances), ~0.8
units per ~16.5 ms frame -- `PlayerState::moveSpeed()` 48.5 units/s times the
game tick's delta time. The game thread integrates a command walk once per game
tick and publishes each step; at this host's ~60 Hz tick and ~60 fps render
every frame got a new position. Whether the renderer should interpolate between
game ticks when it renders FASTER than the game ticks is a behaviour decision
and was not implemented (refactor/BOARD.md, B-unassigned).

The walk ends 4.49 units short of the commanded 15 (first run: 4.53) because
the path follower accepts a waypoint within 5 units
(`MovementComponent::UpdateMovement`, `waypoint_reached`); client and server
agree on where it stopped. Recorded, not changed.

## Not verified here

- Keyboard-driven movement live: the rig drives the client only with
  `--script`. The keyboard answer comes from code and from
  `tests/test_command_movement_render.cpp` (a real renderer with a held
  MoveForward key reports movestate 1, before and after the fix).
- Two-client observation of the new heading (docs/CLAUDE.md: orientation
  correctness is only observable from a second client).
