# T83 — Every entity spawns on the player, and 33k polys draw with nothing in frame

**Slot: t1. OWNS EQEMU. x86 only, no Pi.**

**Territory: `src/client/eq_entities.cpp`, the zone/camera placement path,
`refactor/`, `/mnt/projects/fable/review/T83/`. Read the STOP conditions
in Part 4 before you change any production code.**

Read `refactor/EXECUTOR_PROTOCOL.md` (eqemu section — lock first; daylight
directive; media section), then T82's Review, then this whole plan before
touching anything.

## Why this unit exists

T82 captured four live screenshots in `befallen` and its README described
them as showing "the fully-loaded indoor zone (textured floor/wall
geometry)". **Will looked at the images. There is no ground, no walls, no
floor — a flat blue field, a sky dome, one grey box and a stack of NPCs.**
The README was both wrong and, against protocol, graded.

That is the process failure. The technical findings below are what the
logs actually show, and they were sitting in T82's own captured evidence
the whole time.

**Do not repeat that mistake. Open every image you produce and describe
what is in it. If you cannot see something, say you cannot see it.**

## The measured facts (from `review/T82/testchar_session.log`)

1. **All 173 entities report position `(22, -78, 3)` and "0.0 units away".**
   The zone-in line records the player at `client=(21.88,-77.88,3.00)`.
   Every wolf pup, zombie, skeleton, plague rat and necro neophyte in
   Befallen is stacked exactly on the player.
2. Both `/list` (which reads the old raw entity map) and the graphics-side
   `[NPC]` dump agree on those coordinates. **Two independent consumers
   agree, so this is upstream of T81's projection.** T81 is not the cause;
   do not go looking there first.
3. `Geometry: Polys 32995/40000`, `manualDraw: 363 regions, 8 drawCalls`,
   `144 total scene nodes, 141 visible`. **Zone geometry is loaded and
   submitted every frame.** This is not a load failure.
4. A sky dome is visible in an indoor dungeon.
5. `[WARN] [MAP] Failed to load map for zone: befallen` — the collision/LoS
   map did not load at all. Nobody has explained this.
6. `race=457 gender=2 (not found in any S3D archive)` for four
   `SummonClockworkBanker` entities — the grey box. Race 457 is also well
   outside the Original/Kunark/Velious window.

**Will's read, which is the working hypothesis: these probably share one
cause.** A coordinate-space or origin error would plausibly produce both a
camera that sees no geometry and entities that all collapse onto one point.
Treat that as a hypothesis to test, not a conclusion to confirm.

## Part 1 — Why does every entity land on the player?

Two position decode paths exist and they are **already inconsistent**:

- `src/client/eq_entities.cpp:281` (bulk `ZoneSpawns`):
  `entity.z = static_cast<float>(z_raw) / 8.0f;`
- `src/client/eq_entities.cpp:1083` (single `NewSpawn`):
  `entity.z = static_cast<float>(z_raw);`  ← **no `/ 8.0f`**

That divergence is a real defect on its own regardless of what else you
find — the duplicated-then-drifted class this project keeps hitting. Fix
it once you understand which is right; do not "fix" it by making both
match the wrong one.

The bulk path reads packed 19-bit signed bitfields at `offset + 94/98/102/
106` and swaps server X/Y. Establish, with evidence:

- Are those offsets correct for the **Titanium** `Spawn_Struct`? Verify
  against the struct definition, not against what the code assumes.
- Does `offset` advance correctly per record in the bulk loop? Names,
  levels and HP% clearly differ per entity, so it advances for *some*
  fields — confirm whether the position fields specifically are being read
  from a per-record offset or from somewhere stale.
- Is the sign extension right (`0x40000` mask on a 19-bit field)?
- Decode a handful of records **by hand** from a captured packet and
  compare against what the server's DB says those spawns' coordinates are
  (`SELECT` from the spawn tables). That comparison is the deliverable
  here — it tells you whether the decode is wrong or the data never
  arrives.

Note the player takes its own position from its spawn record in this same
loop (lines ~411-421). Consider whether that is cause or consequence.

## Part 2 — Why is no zone geometry in frame?

33k polys are being submitted and none of it is visible. Establish which:

- Is the camera inside the zone shell, outside it, or below the floor?
  Get the actual camera world position and compare it against the zone
  geometry's bounding box. **State both numbers.**
- Is the geometry in the same coordinate space as the camera? Note the
  X/Y swap at `eq_entities.cpp:284-285` and the "server x/y are swapped
  relative to zone geometry" comment — if that swap is applied
  inconsistently between entity positions, player position and zone mesh,
  that is exactly the shared cause Will suspects.
- Why is a sky dome drawn in an indoor zone at all?

`befallen` is the reproduction case, but check at least one outdoor zone
too — if outdoor zones render ground correctly, the difference localises
the bug fast.

## Part 3 — The two smaller logged defects

- `Failed to load map for zone: befallen` — determine why. Does the file
  exist, is the path wrong, is it a format the loader rejects? Report the
  cause. Fix only if it is small and clearly scoped.
- Race 457 placeholder cubes: confirm the mechanism (race id past the
  model table's ceiling) and **report** the right shape of a fix. Do not
  implement an era-filtering policy in this unit — that is Will's call.

## Part 4 — STOP conditions

Measure first. **Do not fix anything in Parts 1 or 2 until you can state
the cause in one sentence and point at the evidence for it.**

You may fix: the `/8.0f` divergence, and the single root cause of the
entity stacking IF it is small and clearly scoped.

**STOP and report instead of fixing** if the fix requires changing the
coordinate convention broadly, touching the renderer's camera placement
architecture, or editing more than roughly a file's worth of production
code. A coordinate-space convention change affects every consumer in the
client and is its own unit with its own review.

## Verification

- Full build and suite (expect 2066/2067; sole failure is the documented
  ALSA `AudioManagerTest.PreloadCommonSounds`). Visual gate PASS, and if
  goldens change, **say so explicitly and explain why** — a golden change
  in this unit is a real signal, not noise.
- **Verify binary freshness before drawing any live conclusion.** T82
  burned a pass on a stale `build-gles2/bin/willeq` that predated three
  merged units and silently fell through to `Unknown command`. Check the
  binary's timestamp against HEAD.
- Live: re-run T82's Befallen scenario. Entities must report distinct,
  plausible coordinates matching the server's spawn table, and ground must
  be visible. **Capture before-and-after images of the same scene** and
  describe both.
- `-d 4` (numeric) for command output; `--log-level=INFO` did not surface
  it in T82.
- Restore any DB state and confirm with `SELECT`. Clean teardown, lock
  deleted.

## Review

(executor fills: the decoded-vs-database coordinate comparison; the one-
sentence cause of the entity stacking; camera position vs zone bounds and
the cause of the empty frame; whether the two share a cause; the map-load
and race-457 causes; what was fixed and what was left; deviations)
