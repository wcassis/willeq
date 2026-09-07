# T454 — player corpses lie down (B473); the equipment_textures table nobody reads (B474)

Class A/B, slot2, no server. Read B473 (T431: the official client lays a
player corpse flat; fable spawns it as an upright standing entity —
captures `review/T431/probe/T431_sit_dwarf_sitting_h32.png` vs
`review/T431/sweeps/human_eye/eyeHeightFractionOfSkull_0.65_/BEST_frame_000051.png`),
T346's §Review (the death sequence matched to the oracle: which
animation the official client plays on death and what the last frame
is), the corpse spawn path (a corpse arrives as a spawn with the corpse
flag / `NPC type` — read `ZoneProcessNewSpawn` and how `isCorpse` is
carried), the animation code tables (`d05` death animations per race;
`getSkillAnimationCode`), `AnimatedMeshSceneNode`'s frozen-frame
support if any; and B474 (T452: `equipment_textures`, 2,623 rows, no
reader), T450's db recipe.

1. B473: a corpse entity (player or NPC) renders in the death pose's
   final frame from spawn — the same pose the death sequence (T346)
   ends in, held. Read what the official client does for an NPC corpse
   too (T346's captures) and treat both the same unless the captures
   differ (say which). Pre-fix failing test: a corpse spawn packet →
   the entity's animation state is the held death frame, not idle.
   Headless render proof if the panel/scene capture rig allows
   (`review/T454/`); the live A/B is OWED to the lock (add to the T449
   follow-up list in the Review).
2. B474: delete the `equipment_textures` writer, the table, and the
   docs naming it; rebuild and re-pin the db under T450's recipe (one
   commit: db + sha256; row diff = the table only). `DbReproducible`
   green. Check `metadata.total_entries` / any count that included it.
3. Doc nets green; CLAUDE.md's table list re-measured.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
visual gate standalone + ARM (no corpse is in a gate frame — if an image
golden moves, STOP with the sheet), file sizes. Strike B473/B474; new
items B476+ (T449 allocates from B474+ — collisions renumbered at merge;
say which you used).

STOP: an image golden moves; the death pose cannot be identified from
T346's evidence (then board the capture and do B474 only).

## Review

Branch `task/T454-corpse-pose`, base **`54fceff4`** (verified at checkout).
Slot2, `build/` RelWithDebInfo, GLES2 Irrlicht from the slot's own
`third_party/irrlicht-gles2-x86`. **No server**: T449 holds the eqemu lock and
nothing here opened a connection. Review evidence:
`/mnt/projects/fable/review/T454/README.md`.

### 1. B473 — the pose is the oracle's, and the defect was not where B473 put it

**The pose, from evidence, with nothing inferred.** Two independent oracle
measurements agree:

1. **The wire.** T346 read the official client's own answer to its own death
   in T311 Leg A's capture: `C->S OP_SpawnAppearance type=14 (AT_ANIMATION)
   param=115`, **byte-identical in all four self-death windows**. 115 is
   `ANIM_LYING` (`include/client/eq_enums.h:41`). That is the official client
   stating a dead body's pose, four times, on the wire.
2. **The picture.** T431's `review/T431/probe/T431_sit_dwarf_sitting_h32.png`
   shows the body flat — a face and a forearm horizontal under the client's own
   "…pha's corpse" nameplate.

fable renders "lying" as the LAST frame of `d05`, held —
`playAnimation("d05", loop=false, playThrough=false)` then
`SkeletalAnimator::setToLastFrame()`. That is not this unit's invention: it is
what `entity_mesh_build.cpp:365-395` and `entity_build_step.cpp:420-438` have
always done. **Nothing had ever tested it.**

**How a corpse is recognised on the wire, cited.** `Spawn_Struct`'s `NPC` byte
at offset **0083** (`include/common/packets/spawn.h:76`): 0=player, 1=npc,
2=pc corpse, 3=npc corpse. `EntitiesComponent::ZoneProcessNewSpawn`
(`src/client/entities/entities_component_updates.cpp:301`) stores it as
`Entity::npc_type`. eqemu writes **2 for every corpse, player and NPC alike** —
`/mnt/projects/akk-stack/code/zone/corpse.cpp:1809`, `Corpse::FillSpawnStruct`,
`ns->spawn.NPC = 2` unconditionally. So the plan's "treat both the same unless
the captures differ" resolves to: **the wire cannot even distinguish them**, and
this client's four corpse predicates all accept 2 and 3 alike.

**The measurement that redirected the unit.** B473 said fable "spawns a corpse
as an ordinary standing entity with no death pose". It does not. Driven through
the production path (`registerEntity()` then `buildEntityMesh()`) with a real
EDT_NULL device and the shipped `global_chr.s3d`, a Human male corpse comes out:

```
isCorpse=1 isAnimated=1 currentAnimation='d05'
  hasD05=1 hasD02=1 hasD01=1 anim='d05' state=Stopped frame=12 progress=1.000
LIVE (same race, not a corpse): animatorAnim='o01'
```

Held last frame, correct. So the reported defect is one hop later.

**The live defect, found and fixed.**
`EntityRenderer::updateEntityAppearance()`
(`src/client/graphics/entity_lifecycle.cpp:171`) — reached from the
`EntityAppearanceChanged` bridge arm
(`src/client/bridge/irrlicht_bridge.cpp:106`) — rebuilds the entity and then
restores its animation with `setEntityAnimation(spawnId, currentAnimation,
/*loop=*/true, false)`. For a corpse that animation is `"d05"`, and restoring
it as a LOOPING animation **restarts it at frame 0 — the standing pose — and
then loops the character falling over, forever.** Measured:

```
BEFORE: anim='d05' state=Stopped frame=12
AFTER : anim='d05' state=Playing frame=0  progress=0.000
```

The restore is now skipped for a corpse: the rebuild has already established
the held pose, so there is nothing to restore. One guard, `!isCorpse`, with the
reason written out at the call site.

**What is NOT claimed.** This unit did **not** reproduce T431's exact frame.
The synchronous build path is measured correct here, and the appearance-change
path is a real defect but fires only on an appearance change. Whether the
upright body T431 photographed came through this path, through the async build
copy (B477) or through something only reachable live is **not settled**, and
the live A/B is **OWED to the lock** — added to the T449 follow-up list in
`review/T454/README.md`. Saying so is the honest outcome; picking a plausible
second edit without a measurement is the "fix by argument" this project forbids.

**Tests** (`tests/test_corpse_death_pose.cpp`, new, 235 lines):

| test | pre-fix | post-fix |
|---|---|---|
| `CorpseSpawnWire.TheNpcByteIsWhereTitaniumPutsIt` | PASS | PASS |
| `CorpseSpawnWire.EqemuSendsNpcTypeTwoForEveryCorpse` | PASS | PASS |
| `CorpseSpawnWire.LyingIsTheOraclesOwnAnimationValue` | PASS | PASS |
| `CorpsePoseTest.ACorpseHoldsTheLastFrameOfTheDeathAnimation` | PASS (preservation pin — the path was already right, and untested) | PASS |
| `CorpsePoseTest.ALiveEntityOfTheSameRaceIsNotPutInTheDeathPose` | PASS (the negative) | PASS |
| `CorpsePoseTest.AnAppearanceChangeDoesNotStandTheCorpseBackUp` | **FAIL** — state Playing vs Stopped, frame 0 vs 12, progress 0 vs 1 | PASS |

Logs: `review/T454/logs/test_corpse_death_pose_PREFIX.log` (5 passed, 1 failed)
and `..._POSTFIX.log` (6 of 6).

### 2. B474 — the `equipment_textures` table

**The check B468's lesson demanded, made before the deletion.** The equipment
texture path resolves, and by a completely different route:
`EquipmentModelLoader::buildEquipmentIndex()`
(`src/client/graphics/eq/equipment_model_loader.cpp:916`) walks the same
seventeen `*equip*.s3d` archives LIVE at zone load, filling `textureIndex_`
(:248), and `getTextureRef()` (:978) is what the mesh build asks. `WilleqDatabase`
is the ONLY sqlite consumer in the tree and names the table in no SQL. So this
was **redundant, not broken** — the opposite of `s3d_assets`, and safe to drop.

Deleted from `tools/willeq_db/db_loaders_json.cpp` `loadEquipmentModels()`: the
`CREATE TABLE`, the prepared statement, the whole `"textures"` section parse and
`texCount`. The JSON's own `"textures"` section is left alone —
`data/equipment_models.json` is produced by `tools/equipment_index_builder` and
is not that file's to trim.

**The row diff is the table and nothing else** (`sqlite3 .dump`, before vs
after; full listing in `review/T454/logs/`):

| removed | 2,623 `INSERT INTO equipment_textures`, the 5-line `CREATE TABLE`, `metadata.total_entries='32414'`, `sqlite_stat1('equipment_textures',…,'2623 1')` |
| added | `metadata.total_entries='29791'` and the re-ANALYZEd `sqlite_stat1` line |

| | before | after |
|---|---|---|
| tables (excl. `sqlite_stat1`) | 12 | **11** |
| `metadata.total_entries` | 32,414 | **29,791** (exactly −2,623) |
| `metadata.input_digest` | `c39f5cfbb75e5744` | **UNCHANGED** |
| `metadata.version` | 7 | 7 — **`kWilleqDbExpectedVersion` NOT touched** |
| sha256 | `14f09de8…` | `d503b952…` |
| size | 3,788,800 B | 3,596,288 B |

`input_digest` not moving is the useful confirmation that T450's digest is over
the INPUTS and not the tool: the same ten inputs produced the same digest from a
recompiled builder. `data/willeq.db` + `data/willeq.db.sha256` are in ONE commit,
per T450's recipe. `DbReproducible` **Passed** (2.87 s in the suite run).

**The converse net, which is the point of the unit.**
`WilleqDatabaseShippedContent.EveryTableTheDatabaseShipsIsOneTheClientQueries`
(`tests/test_willeq_database.cpp`) takes the census from the shipped database
and from `src/client/willeq_database.cpp`'s own SQL, and fails on a shipped
table no query names. T452 added the forward direction; this is the direction
that would have caught B474 six months earlier. It carries **one** exemption,
`equipment_models`, boarded as **B476** — and the exemption is itself pinned
(if a consumer appears, the test fires and tells you to strike the board item).
A positive control asserts `equipment_textures` is gone.

### 3. Docs and nets

- `CLAUDE.md` — the recipe table's row count 32,414 → **29,791**; a new
  paragraph beside T452's naming what B474 was, why it was redundant rather
  than broken, the new converse test and its one exemption.
- `data/willeq.db.sha256` — new hash, the row count, and (found in passing) its
  header said "eleven inputs" while listing ten; T452 removed the eleventh
  (`--s3d-index`) and did not update the count. Fixed.
- `tools/willeq_db_builder.cpp` — table list and a T454 tombstone beside T452's.
- `refactor/BOARD.md` — B473 and B474 struck with their evidence; B476, B477,
  B478 added.
- `scripts/refactor/check_doc_counts.py` — **OK: every stated count matches the
  tree** (17 counts over 33 sites in 12 docs).

### 4. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (background, exit file) | **exit 0**. No exit 143, so no truncation scan owed. |
| `ctest --test-dir build -j6 --output-on-failure` | **4692 of 4692, 0 failed** (`review/T454/logs/ctest.log.gz`). Suite +6, all this unit's. `visual_gate` **Passed** inside the run (39.14 s). |
| `bash scripts/build-gles2-x86.sh` | exit 0 |
| visual gate, standalone | **PASS**, **no golden file is in the diff**. The FIRST standalone run FAILED on `gfaydark: p95 process-CPU ms 12.088 exceeds ceiling 10.672` at host load average 18–29 — the documented CPU-p95 load artefact, not a defect: no image golden moved and `characterModelsBytes` was healthy (60–61 %, not the stale-tree deficit shape). Re-run at load 14: PASS. Both logs kept. |
| `bash scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh fable` | 10 files checked, **10 ok, 0 warn, 0 fail**; plus `--files` on the new untracked TU: `tests/test_corpse_death_pose.cpp` 235 lines, ok. Largest touched: `tools/hwcaps/gles2_fog_volume_benchmark.cpp` 1,220 (not this unit's). |
| goldens | untouched — no golden file in `git status`. |

Host load across the runs: 14–49 (three slots active). Every classification
above is by EXIT STATUS except ctest, which is read from its summary line.

### 5. Board

Struck: **B473**, **B474**. Added: **B476** (`equipment_models` is the same
finding, still shipping, and is the converse test's one exemption), **B477**
(the async build path's duplicated copy of the corpse pose ladder is still
asserted by nothing — `processOneEntityBuildStep()` never reached `meshBuilt`
in an EDT_NULL harness with no prep worker), **B478** (corpse recognition is
derived FIVE times from the same two fields and the copies disagree; the loose
`name.find("corpse")` in three of them has no id check and would freeze a live
NPC named "a corpse beetle" in a death pose). **Numbers allocated from B476 —
T449 allocates from B474+ and T453 from B470+, so collisions are possible and
are renumbered at merge.**

### 6. Deviations and surprises

- **The plan's premise was wrong and the unit says so rather than fitting it.**
  B473's "fable spawns a corpse as an ordinary standing entity with no death
  pose" is refuted by measurement: the spawn/build path produces the held d05
  last frame. The STOP condition did not fire — the pose IS identified from
  T346's and T431's evidence — but the DEFECT was not where the board put it,
  and the T431 frame is not reproduced. Both facts are recorded, and the live
  A/B is owed.
- **The corpse pose had never been executed by a test in the project's
  history.** `git log -S"setToLastFrame"` returns three commits, the newest a
  move-only split; the ladder is original code that has never had a caller in a
  test. It was correct — but nobody knew.
- **`equipment_models` is dead too**, by the identical measurement, and was left
  alone on scope. Boarded rather than swept along, because B468's whole lesson
  is that a drop needs its own deliberate check.
- The db pin header's "eleven inputs" was stale from T452; found while editing
  it, fixed in passing.
