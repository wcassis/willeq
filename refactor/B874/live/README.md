# B874 / #218 -- the first `--pose-log` from a ZONED-IN client

Every file here was written by one real run and copied byte-for-byte; none
was typed, generated or edited. `pose.tsv` is the complete log (header + 539
rows, sha256 `609605c45137ab627a1256cb5ecb2008f0f110d823304f80ca2df19cc9c4803f`,
the value `run_meta.txt` recorded at the end of the run).

- **Run**: `run-20260929T022314Z`, started by
  `ctest --test-dir build -R PoseLogLiveCapture` (i.e.
  `scripts/pose_live_capture.sh`), client window 2026-09-29 02:24:28Z to
  02:24:44Z.
- **Host**: `dev-u24` (x86, Mesa llvmpipe through surfaceless EGL), eqemu
  LOCAL lane (`EQEMU_HOST=local`, client config host `10.0.30.23`, checked
  against `hostname -I` before launch).
- **Commit**: `ab582ef52ab8d1171f56673edc046f32f824df47` (branch
  `task/pose-live-capture`), Release build of that commit.
- **Command**: `command_line.txt`; the driving script is `tour.txt`.
- **Client log**: `client_log_excerpt.txt` is every line of the run's
  `client.log` matching `Script:`, `CAMERA-GET`, `pose-log`, `Camera mode`,
  `XYZ:`, `saved` or `Turned to`, with its line number. The pose-log open line
  is `pose-log: writing '...pose.tsv' every 1 frame(s), capped at 600 row(s)`.
  The whole log (218 KB) and the rig's own files are in
  `/mnt/projects/fable/review/B874-live/run-20260929T022314Z/`.
- **Verdict**: `comparison.tsv` (one row per check), `rig.log` (the same
  table as printed). 37 PASS, 2 FAIL -- both are findings below, not rig
  errors.

## How the rendered client was driven

`--scenario` forces the headless Automated lane and `--pose-log` is refused
without a renderer (MR !38), so the capture uses the other supported driver:
`--script` (H03/T29), which runs `wait N` and slash-command lines through the
same unified dispatch as stdin, holding every command until the client is
fully zoned in. The client renders on the default surfaceless-EGL device --
the route `scripts/camera_match_sweep.sh` already takes. Nothing in the
client was changed.

## Independent sources, and how they were obtained

| source | how | units |
|---|---|---|
| seed | `UPDATE character_data` with the eqemu-server container STOPPED, read back, container started, then login | server frame; heading 0-512 (`m_Position.w`, `EQ12toFloat` = raw/4) |
| `#loc` x3 | `/say #loc` (Testchar is GM status): the zone's live `GetPosition()`, printed to the client | server frame, 2 decimals, heading 0-512 |
| after quit | `character_data` read straight after the client exited, after a self-targeted `#save` (`Client::Save(2)`) | as above |
| final | `character_data` after `docker stop` of the eqemu-server container | as above |
| kChaseRig | `camera_rig.h` (az 0, pitch 6, dist 4.0, look-at 7.0 above the feet) x the eye scale / feetZ the client printed in `/camera get` | client frame |

Transform applied: `server_x = playerY`, `server_y = playerX`,
`server_heading = (128 - heading0_512) mod 512`; the server heading in every
source above is in 0-512 units (not degrees, not 0-256).

## What agreed

- Seed / first `#loc` vs the 129 stationary Chase rows: x, y exact (delta
  0.0000), heading 28.0 -> server 100.0, delta 0.0000.
- `#loc` after `/move` vs the last row before it: server_y 438.47 vs playerX
  438.4667 (-0.0033), server_x -74 exact, heading 0 vs 0.
- `#save`d `character_data` (after quit and after the container stop):
  438.467 vs 438.4667 (-0.0003), heading 0 vs 0.
- Chase camera while stationary vs kChaseRig x scale 1.0000: worst delta
  0.0003 over 129 rows; pitch 6.0000, azimuth 0.0001 deg, distance 4.0002.
- `ts` unix ms inside the host-clock window, non-decreasing; `frame` 1..539
  strictly increasing; 539 rows over 10.59 s.

## Findings (numbers from this run unless named)

1. **`/turn 90` reaches the server but not the rendered pose.** `#loc` 1.0 s
   after `/turn 90` reports heading 0.00; every row in that window still has
   heading0_512 28.0 (server-equivalent 100.0): delta 100.0 units. The
   rendered heading changes only when `/move` starts (128.0 -> server 0).
2. **movestate stays 0 for a `/move`-driven walk.** The player moves 10.467
   units while every row reports movestate 0 (idle).
3. **Constant z offset.** Client playerZ is 0.6306 BELOW the server's z in
   every comparison: seed 3.0 vs 2.3694, `#loc` 3.44 vs 2.8094, `#save`d
   3.44004 vs 2.8094. Rows 1-2 carry playerZ 3.000000 (the server's value)
   before the client settles to 2.369373 at row 3.
4. **`/move 443 -74 3` stops short.** Client and server agree the walk ended at
   client x 438.467, 10.467 of the 15 units commanded.
5. **The first `/camera chase` is lost at zone-in.** Rows 1-2 are
   `First Person Camera` although the log says `Camera mode: Chase Camera`
   2 ms before row 1; the tour therefore sends it twice. In the earlier
   `run-20260929T021344Z` (single `/camera chase`) all 600 rows were First
   Person and `/camera get` 5 s later read First Person.
6. **Nothing but `#save` wrote the moved pose to `character_data`.** In
   `run-20260929T021741Z` (no `#save`) the row was still the seed both after
   the `/q` exit and after the container stop, while `#loc` had reported the
   moved position -- so "the zone writes the character back on shutdown" did
   not happen on this path.
7. The first two rendered frames after zone-in took 881 ms and 785 ms.

Findings 1 and 2 are the two FAILs and are reproducible: the identical
37/2 split was measured in the uncommitted rehearsal `run-20260929T022021Z`.

## Still owed

- **Deliverable A**: a server-authoritative pose log for the OFFICIAL client
  (B875/B876). This run proves fable's own log against the server; it says
  nothing about the official client.
- Keyboard-driven movement (the path that sets movestate) is not exercised:
  the only driver for a rendered client is `--script`, whose movement
  commands run on the game thread.
- The watchdog's kill paths (no open in 120 s, no progress for 60 s, 300 s
  overall) were not exercised by a failing run.
