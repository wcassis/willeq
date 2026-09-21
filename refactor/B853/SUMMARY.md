# B853 sweep summary — live zone-in over every Kunark and Velious zone B852 swept clean

Run: 2026-09-21, eqemu VM lane (172.16.200.99, driven over ssh by `scripts/eqemu/*`),
`build/bin/willeq` from `task/kb-b853-live-zonein-sweep` (origin/fable 3fbe3a48),
`--scenario zone_in --timeout 300`, one lock hold + one `up.sh` + one snapshot,
heavy-first (s3d_bytes), per-zone watchdog 150 s. Driver: `scripts/scenario_zone_sweep.sh`.

This lane proves what B852's offline benchmark cannot: a LOGGED-IN client
reaches `IsFullyZonedIn()` in each zone with the server's own spawns, doors
and objects. It runs the Automated lane, which does NOT initialize the GPU
render pipeline (`initGraphics` gates to `GraphicalInteractive`), so the
placeholder-model and late-open-EQE1 columns are structurally 0 here — they
are recorded for parity with `results_118.tsv` only.

**Clean definition (this lane):** exit 0, the scenario's own verdict line
("zoned in to '<short>'") names the seeded zone, zero FATAL, zero non-MAP
ERROR, and `LoadZoneSources: Loaded zone '<short>'` present (the game-thread
zone-geometry marker: a missing zone S3D FATALs there). `[MAP]` ERRORs
(missing optional `<zone>.map` pathfinding asset — 2 per zone, all 46) are
counted in `error_map_count` but not charged against clean.

## Result [measured]

| | rows | clean | failed |
|---|---|---|---|
| kunark | 28 | 28 | 0 |
| velious | 18 | 18 | 0 |
| **total** | **46** | **46** | **0** |

46/46 clean. **No distinct failure line, so no B853-<zone> issue was filed
(deliverable 3 is vacuously satisfied).** The snapshot restored
byte-identically with the world down; the lane exited 0 and released the lock.

Server content actually received (the point of this card): 1,270 NPC spawn
rows and 1,582 doors across the 46 zones. Per zone: 0–100 spawns, 0–226
doors (skyfire 226; charasis/frozenshadow/kurn/velketor/warrens 100 spawns).

## Zones the server sent with no NPC spawns [measured]

9 of 46 zones sent a zero-spawn batch (client's own `Loaded 0 spawns in zone`
confirmation, ZONE module): timorous, citymist, skyfire, frontiermtns,
burningwood, droga, nurga (kunark) and greatdivide, sirens (velious). 5 sent
no doors (cabwest, sirens, citymist, droga, nurga; sirens/citymist/droga/nurga
sent neither). This is the server's spawn-table content as it stands on the
lane, not a zone-load defect: every one of these zoned in clean with its
geometry, doors (where present) and zero FATAL/ERROR.

## Traps measured on this lane (pinned in the script's comments)

1. **`character_data.zone_id` stores the stable `zoneidnumber`, not the `zone`
   table's PK `id`.** Seeding the PK sends the character to a DIFFERENT zone:
   measured, seed `zone_id=370` (stonebrunt's PK) → zoned into `relic`
   (370 is relic's zoneidnumber; `relic.s3d` is not a client asset → FATAL
   exit 2); seed `zone_id=174` (iceclad's PK) → zoned into `dawnshroud`
   (174 is dawnshroud's zoneidnumber; exit 0, wrong zone). This was the
   selftest RED before the fix; after it, the scenario's own "zoned in to
   '<short>'" line matched the seeded zone in all 46 runs.
2. **The world's zone-port range is small and every left-running zone
   process holds its port.** Left running, ~18 of the sweep's own zone
   processes plus the 11 launcher-fleet processes exhausted it: new zone
   processes booted, printed their marker, then logged `World did not have a
   port to assign from this server, the port range was not large enough`
   and shut down — 36 of 46 rows in the first full run (archived:
   `/home/user/kb-evidence/b853-live-zonein-sweep/full_run1_portexhausted/`).
   `kill_zone` stops each zone process after its run (after the linkdead
   save window) so the port is free for the next one.
3. **The zone's LINKDEAD client cache** (the second trap
   `scripts/camera_match_sweep.sh` documents): a zone process started before
   the seed never re-reads the seeded pose. `boot_zone` bounces only the
   target zone's process (bouncing the whole fleet raced the respawner);
   the seed's write-readback-retry loop survives the late linkdead save.

## Skipped zones (the card's convention: B852's red rows)

None in the default set — B852's two red zones (soldunga #100, hole #101)
are both CLASSIC zones, so the Kunark/Velious default set (B852 clean==1)
is exactly these 46. A live soldunga zone-in would hit #100's known FATAL;
it is out of scope by construction.

## Evidence

- Committed: `refactor/B853/results_46.tsv` (46 rows, byte-identical to the
  live run), `refactor/B853/selftest_2zones.tsv` (the `--selftest` net:
  stonebrunt + iceclad, 2/2 clean).
- Bulk (this host, attached to the card): per-zone client logs + boot logs,
  the stack up/down logs, both snapshot files, run transcripts
  (`selftest_run.log` = RED, `selftest_run3.log` = GREEN,
  `full_run2.log` = the 46-zone run; `full_run1_portexhausted/` = the
  port-exhaustion first run). Path:
  `/home/user/kb-evidence/b853-live-zonein-sweep/`.

## Selftest (deliverable 4)

`bash scripts/scenario_zone_sweep.sh --selftest` — the two-zone regression
(stonebrunt Kunark + iceclad Velious) into
`/home/user/kb-evidence/b853-live-zonein-sweep/selftest`. RED proof
(2026-09-21, pre-fix): stonebrunt `exit 2` + `FATAL: ... relic.s3d ... does not
exist`, iceclad `exit 0` but zoned into `dawnshroud`; verdict 0/2. GREEN
post-fix: 2/2 clean (stonebrunt 3 spawns/91 doors, iceclad 11/41), restore
byte-identical, lane exit 0. The CLAUDE.md scenario-section paragraph
deliverable 4 asks for is in the MR description (the worktree's CLAUDE.md
write was approval-gated in this headless session) — paste it in on merge or
a follow-up.
