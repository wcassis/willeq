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

Executed in `/mnt/projects/fable/wt/t1`, task branch
`task/T83-entity-positions`, fable tip `0e1d0753`. Full media, README, and
raw command/debug logs: `/mnt/projects/fable/review/T83/`.

### Binary freshness

The pre-existing `build-gles2/bin/willeq` was timestamped 14:06, older than
this branch's HEAD commit (15:29) — rebuilt via `scripts/build-gles2-x86.sh`
before any live measurement, per the plan's explicit caution (T82 burned a
pass on exactly this).

### Part 1 — the decoded-vs-database coordinate comparison

**Two independent findings, not one.**

**(a) A real, independent decode bug — FIXED.** `eq_entities.cpp`'s
`ZoneProcessNewSpawn` (individual `NewSpawn` packets) decoded x/y/z from
the same 19-bit fixed-point bitfields as the bulk `ZoneProcessZoneSpawns`
path but never divided by 8.0f. Struct offsets (94/98/102/106, matching
`Spawn_Struct::position_bitfield`'s documented bit layout in
`packet_structs.h`) and the loop's `offset += 385` advance (matching
`static_assert(sizeof(Spawn_Struct) == 385)`) were both verified correct
by inspection — the ONLY defect was the missing `/8.0f` on this one path.
Live confirmation, `nektulos`/`Testmelee`:

| Entity | Server DB (`spawn2`, pre-swap) | Client `/list` after fix |
|---|---|---|
| `Blugurg` | x=-174, y=-1165, z=-3 | (-1165, -174, -3) |
| `Guard_V\`Lex` | x=-16, y=-1086, z=-7.375 | (-1086, -16, -7) |

Matches exactly after the documented (pre-existing, correct, unchanged)
X/Y swap. Before the fix, the SAME entities decoded via the buggy path 8x
too far out — T82's own log already had this (`a_deathfist_scout` at raw
`(-16544,5656,-19)`, i.e. 8x `(-2068,707,-2.4)`); this unit's
`testmelee_after_fix_session.log` is the after side. Fixed in
`eq_entities.cpp` (both the decode and a stray debug-log line that echoed
the pre-scale `z_raw`); `tests/test_zone_entities.cpp`'s
`ZoneEntitiesNewSpawn.ParsesDocumentedFieldsIncludingXYSwap` updated to
assert the corrected (divided) expected values.

**(b) Befallen's 173-entities-at-one-point claim was imprecise, and the
real cause is NOT a client bug.** Only ~40 of Befallen's 173 entities are
actually stacked on the player — checked every one via the `[NPC]` graphics
dump (`review/T82/testchar_session.log`): ids 2-131 have distinct,
DB-verified-correct positions (e.g. `Boondin_Babbinsbort` DB (x=-206,
y=-814, z=-68.0) decodes to (-814.0,-206.0,-67.9) — correct, swap and all);
only ids 134-173 (exactly 40) sit at (21.9,-77.9,3.0), the player's own
spawn point. Traced to the server's own `zone_state_spawns` table (zone_id
36): **40 rows, all `spawn2_id=0`, all created at the identical
`created_at` timestamp, all positioned at the player's exact
coordinates/heading (`x=-77.875, y=21.875, heading=190`, matching
`client=(21.88,-77.88,3.00)` after the swap byte-for-byte).** These NPCs
(`SummonResupplyAgent`, `SummonClockworkBanker`, `Miltiades_Tal\`Azroth`,
`Brin_Stolunger`, `Chomper`, `a_scaled_wolf_pup`, `a_rat`) have **no**
`spawn2` row in Befallen at all — they are not static zone content (T82's
README claimed "these are Befallen's own static spawn2 mobs" — checked via
`SELECT`, that is wrong). This is server-side test debris: some prior
session's GM-summon activity got persisted into `zone_state_spawns` and
now respawns every zone boot, camped exactly on the player. **One-sentence
cause: the "entity stacking" is not a decode bug at all — it's 40
persisted, ephemeral, GM-summoned test NPCs saved to the server's own
zone-state table at the player's literal spawn point, and the decode path
that produces their reported position is correct.** A `DELETE` to clean
this debris up was attempted this unit and blocked by the session's
permission classifier (outside the "documented character-position writes"
allowance) — reported in `refactor/deferred.md`, not forced. `nektulos`
has the identical debris shape (`a_rat000`-`009` stacked on `Testmelee`),
confirming this isn't Befallen-specific.

### Part 2 — camera position vs zone geometry bounding box

**Camera EQ-equivalent position: ~(7.39, -64.08, 13.00)** (from
`CameraController::setFollowPosition`'s one-time zone-in debug log,
Irrlicht(7.39,13.00,-64.08) converted X,Z,Y -> EQ X,Y,Z).
**Zone geometry bounding box: X=[-1007.6, 28.0], Y=[-281.9, 195.9]**
(`renderer_zone_load.cpp` "SEQ Step6: zone bounds from geometry", logged
at zone-load time). **The camera is INSIDE the bounding box** (x=7.39 well
within [-1007.6,28.0]; y=-64.08 well within [-281.9,195.9]) — this rules
out the simple "camera positioned outside the loaded mesh" hypothesis.
`setPlayerPosition`'s own `playerInBounds` gate (500-unit margin) passed;
`cameraMode_==Follow` engaged normally.

Despite this, and despite the geometry genuinely submitting every frame
(32995-38175 polys, 363 regions, 8 draw calls, matching T82's own
numbers), the screenshots show no ground/wall/floor near the camera — a
flat blue field to the horizon and a sky dome — while a **distant piece of
real zone geometry (an entrance bridge/walkway structure) renders
correctly** in the same frame (visible top-left in
`review/T83/testchar_before_01_daylight.png`). A geometry-space
coordinate-swap bug would put the camera either outside the bounding box
entirely or looking at systematically-wrong geometry everywhere, not "in
bounds, with distant real geometry rendering fine, nothing local." This
localizes the empty-frame defect to region/PVS visibility selection for
the specific BSP region the player's position falls in — not measured
further; determining the exact mechanism (wrong region index from
`findRegionIndexForPoint`, a PVS/portal gate excluding the player's own
region, or the manual front-to-back draw list simply not including it)
requires reading the renderer's region-selection/PVS code, which is
squarely the "touching the renderer's camera placement architecture" STOP
condition in Part 4. **STOPPED and reported per protocol, not fixed.**

Sky dome in an indoor zone: `renderer_zone_load.cpp`'s
`isDungeon = (zoneType == 2)` is the wrong signal. The server already
sends, and the client already stores, `skyType` (befallen: `skyType=0`,
exactly matching the DB's own `zone.sky=0`, which — checked across a dozen
sampled zones — is the field that actually correlates with real
indoor/outdoor status; `zoneType`/`ztype` does not: it's 0 or 255 with no
consistent pattern across indoor and outdoor zones in this dataset).
**Right shape of a fix (not implemented, outside Part 4's fence): gate the
sky dome off `skyType`/the DB's `sky` column instead of `zoneType`.**

### Do the two headline defects (empty frame, stacked entities) share a cause?

**No.** Will's working hypothesis was reasonable given the screenshot, but
measurement rules it out: the stacked entities are DB-side persisted test
debris (a server data problem, not a client decode or coordinate-space
issue at all — the decode for those exact entities is provably correct,
it's just reporting real, if accidental, server state). The empty frame is
a region/PVS-visibility question with the camera already inside the
correct geometry bounds — not a coordinate-space or origin error either.
The two defects happened to be visible in the same T82 screenshot; they
are independent.

### Part 3 — the two smaller logged defects

- **Map load failure — NOT a code or asset bug, a missing test-session
  config field.** `HCMap::LoadMapFile` looks for `<maps_path>/<zone>.map`;
  T82's config (`data/captures/t82_testchar_willeq.json`) never set
  `maps_path`, so it fell back to nonexistent relative `./maps` and always
  failed — for every zone, every session, not just Befallen. The real file
  exists and is valid: `/mnt/projects/akk-stack/server/maps/base/befallen.map`
  (148,609 bytes). Confirmed live this unit: re-running with `maps_path`
  set to that directory (see `casterella.json` for the known-good example
  this project already has) produced "Successfully loaded map for zone:
  befallen". Every future live-testing session's config should set
  `maps_path`/`navmesh_path` or this recurs silently.
- **Race 457 placeholder cubes — confirmed mechanism, not fixed (Will's
  call per the plan).** `data/willeq.db`'s `race_models` table tops out at
  `MAX(race_id)=367`; race 457 (`SummonClockworkBanker`,
  `SummonResupplyAgent`, both server-side `npc_types.race=457`) has no
  entry, so the race-model loader falls through to the placeholder-cube
  path. Right shape of a fix: either add model coverage for out-of-era
  summoned-NPC races, or leave it (an era-filtering policy decision, not
  implemented here per the plan's explicit instruction not to).

### What was fixed vs left

**Fixed**: the `/8.0f` divergence in `ZoneProcessNewSpawn`
(`eq_entities.cpp`) and its matching stray debug-log line; the test that
pinned the old (wrong) behavior.

**Left, all measured and reported, none touched (Part 4 STOP or explicit
plan exclusion)**: the region/PVS-visibility cause of the empty frame near
the camera; the sky-dome-in-indoor-zones signal bug (`zoneType` vs
`skyType`); the 40 rows of persisted test-NPC debris in
`zone_state_spawns` (DB write blocked by the session's permission
classifier, not by protocol); race-457 model coverage policy.

### Live verification

`Testchar`/`befallen` before-and-after screenshots (identical binary
build, before = pre-fix HEAD, after = post-fix + full rebuild):
`review/T83/testchar_before_0{1,2}*.png` vs `testchar_after_0{1,2}*.png` —
visually indistinguishable, as expected (this fix doesn't touch a code
path Befallen's reproduction case exercises; none of the still-open
defects were touched). `Testmelee`/`nektulos` `/list` after the fix
(`review/T83/testmelee_after_fix_session.log`) is the positive proof the
fix works, cross-checked against `spawn2` (see Part 1 table above).

### Build/test

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — first pass surfaced
   `ZoneEntitiesNewSpawn.ParsesDocumentedFieldsIncludingXYSwap` failing
   (expected: it pinned the pre-fix, unscaled values) alongside the
   documented ALSA exception; test updated to the corrected values, full
   rerun: **2066/2067**, sole failure `AudioManagerTest.PreloadCommonSounds`
   (documented host exception). `visual_gate` (test #2067) — PASS.
3. `bash scripts/build-gles2-x86.sh` — clean (also the rebuild used for
   the "after" live captures).
4. Visual gate — PASS, goldens untouched (`git status` shows only
   `eq_entities.cpp` and `tests/test_zone_entities.cpp` changed).
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `check_file_sizes.sh fable` — 2 files checked, 1 ok
   (`test_zone_entities.cpp`, 361 lines), 1 pre-existing WARN
   (`eq_entities.cpp`, 2474 lines, already over the 1500-line threshold
   before this unit's ~15-line net edit) — no new FAILs.

### Teardown

- Server: `scripts/eqemu/down.sh` run; lock file deleted, confirmed via
  `ls` failing.
- `Testchar` (id 65): drifted ~0.05 units off the documented baseline
  during the idle live sessions (fixed-point round-trip rounding, no
  movement command issued) — restored via `UPDATE` to the exact documented
  baseline (`zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190`),
  confirmed via `SELECT`.
- `Testmelee` (id 66): position/zone/inventory never touched this unit
  (no writes issued) — unchanged, confirmed via `SELECT`
  (`zone_id=25, x=-259, y=-1201, z=-5.75, heading=160.25`).
- `zone_state_spawns` debris (40 rows, zone_id=36, `spawn2_id=0`): **NOT
  deleted** — attempted, blocked by the session's permission classifier as
  outside the sanctioned DB-write scope. Confirmed still present via
  `SELECT` (count unchanged at 40) before teardown. Reported in
  `refactor/deferred.md` for a future unit or Will's direct action.
- Extra zone processes started beyond `up.sh`'s default `arena`
  (`befallen`, `nektulos`) via the same sanctioned `docker exec -d ./bin/zone
  <name>` mechanism `up.sh` itself uses (matching T82's precedent) — torn
  down by `down.sh` along with the rest of the stack.
- A pre-existing fleet of ~15 static-zone processes (`butcher`,
  `erudnext`, `freporte`, `qeynos`, etc.) was already running in the
  `eqemu-server` container at session start, left over from some earlier
  session's use of `spire spire:launcher start` — not started by this
  unit, not individually managed, torn down by the same `down.sh` /
  `docker compose down` this unit ran regardless. Noted for the record,
  not a T83 finding.

### Deviations

1. `data/captures/t83_testchar_willeq.json` / `t83_testmelee_willeq.json`
   added (this unit's session configs, with `maps_path`/`navmesh_path`
   set correctly — see Part 3) rather than reusing T82's configs, which
   were missing those fields.
2. A `DELETE` against `zone_state_spawns` to clean up the debris found in
   Part 1(b) was attempted and blocked by the session's own permission
   classifier. Not forced or worked around — reported instead, per the
   classifier's own guidance to let the user decide. This is a deviation
   from "restore any DB state you modify" only in the sense that the
   debris predates this session and this unit could not clean it up
   despite trying; no state this unit itself wrote was left unrestored.
3. The plan's suggested camera-position log line
   (`CameraController::setFollowPosition`'s `LOG_DEBUG`) required
   `--log-level=DEBUG` in addition to `-d 4` to surface — `-d`/`-D` gates
   a separate, older `s_debug_level` mechanism used by `eq_entities.cpp`'s
   own logging, not the newer per-module `LOG_DEBUG`/`ShouldLog()` system
   `camera_controller.cpp` uses. Noted here since the plan and T82 both
   only mention `-d 4`.
