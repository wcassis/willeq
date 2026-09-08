# T456 — one corpse predicate (B478), the async build copy pinned (B477), the equipment_models table (B476)

Class A/B, slot2, no server. Read B476–B478 (T454's finds: corpse
recognition is derived FIVE times and the copies disagree — three use a
loose `name.find("corpse")` with no id/npc_type check, which would
freeze a live NPC named "a corpse beetle" in a death pose; the async
entity-build path duplicates the pose ladder and nothing asserts it;
`equipment_models`, 1,000 rows, has no reader), T454's §Review (the
wire truth: `Spawn_Struct.NPC == 2` for every corpse, cited at
`/mnt/projects/akk-stack/code/zone/corpse.cpp:1809`; the death pose
`d05` last frame held; `EquipmentModelLoader::buildEquipmentIndex()`
indexes the archives live), T450/T454's db recipe.

1. B478: one predicate (`eqt::entities::isCorpse(const Entity&)` or the
   existing best copy) keyed on `npc_type == 2`, used by all five sites;
   the name heuristic is deleted. Pre-fix failing test: a live NPC named
   "a corpse beetle" (npc_type 1) is NOT a corpse at every site; a
   player corpse (npc_type 2, name "…'s corpse") IS.
2. B477: the async build path's pose ladder shares the sync path's code
   (one function) and is pinned by the same test through both paths.
3. B476: delete the `equipment_models` writer and table, rebuild and
   re-pin the db (one commit: db + sha256; row diff = the table), the
   converse net's exemption removed so it pins zero exemptions.
4. Docs/nets green; CLAUDE.md table list re-measured.

## Proof

Build (background), `ctest -j6` (no expected reds), pre-fix/post-fix,
`DbReproducible` green, visual gate standalone + ARM (no golden should
move — STOP with the sheet if one does), file sizes. Strike B476–B478;
new items B479+ (T449/T455 allocate from B474+/B476+ — collisions are
renumbered at merge; say which you used).

STOP: an image golden moves.

## Review (PAUSED)

Branch `task/T456-corpse-recognition`, base **`02054a2d`** (verified at
checkout). Slot2, `build/` RelWithDebInfo, GLES2 Irrlicht from the slot's own
`third_party/irrlicht-gles2-x86`. **No server** — T449 holds the eqemu lock and
nothing here opened a connection; **live proof is OWED**.

**The session was paused by the orchestrator mid-verification.** All three
board items are implemented and proven by tests (pre-fix red / post-fix green
shown below); the *full* verification bar is NOT finished. What is missing is
listed under "Not done" at the end.

### 1. B478 — one corpse predicate, and the board's census undercounted

The plan and B478 said corpse-ness was derived FIVE times (the canonical
`IsCorpseByFields()` plus four copies). The source census written for this unit
found **EIGHT — the canonical one plus SEVEN copies.** All seven now call
`IsCorpseByFields()`, or `IsCorpseEntity()`, its `Entity` overload
(`src/client/combat_hunting.cpp:327`).

| # | site (pre-fix) | predicate before | predicate after |
|---|---|---|---|
| 1 | `src/client/bridge/bridge_component.cpp:463` `OnSpawnAddedGraphics` | `npc_type==2\|\|npc_type==3`, then `name.find("corpse")!=npos` | `IsCorpseByFields(entity.is_corpse, entity.npc_type, entity.name)` |
| 2 | `src/client/application_zone_loading.cpp:239` (threaded zone load) | same two-part form | same call |
| 3 | `src/client/application_zone_loading.cpp:349` (sequential zone load) | same two-part form | same call |
| 4 | `include/client/state/entity_manager.h:74` `Entity::isAnyCorpse()` | `npcType==2\|\|npcType==3\|\|isCorpse` — **no name test at all** | `IsCorpseByFields(isCorpse, npcType, name)` |
| 5 | `src/client/combat_hunting.cpp:251` `IsTargetSuitableForHunt()` | `name.find("'s corpse")!=npos` | `IsCorpseEntity(entity)` |
| 6 | `src/client/combat_hunting.cpp:482` `ListHuntTargets()` | `class_id>16 \|\| name.find("'s corpse")!=npos` | `class_id>16 \|\| IsCorpseEntity(entity)` |
| 7 | `src/client/combat.cpp:724` | three chained substrings: `"'s corpse"`, `` "`s corpse" ``, `"corpse"` | `IsCorpseEntity(entity)` |

Sites 5–7 were **not** in B478 and are the more interesting find, because they
are wrong in *two* directions at once: `"'s corpse"` is only the PLAYER-corpse
spelling, so every npc corpse (`` a rat`s_corpse17 ``) walked past the hunt
filter as a live target, while a live npc named "a corpse beetle" would have
been rejected by site 7's bare `find("corpse")`.

**The wire truth, cited absolutely.** `Spawn_Struct`'s `NPC` byte at offset
0083 (`include/common/packets/spawn.h:76`). eqemu's `Corpse::FillSpawnStruct`
writes `ns->spawn.NPC = 2` **unconditionally**, player corpse and npc corpse
alike — `/mnt/projects/akk-stack/code/zone/corpse.cpp:1809`. A name heuristic
is therefore never the authority; `IsCorpseByFields()` asks the flag, then that
byte, then eqemu's three real name forms **with their mandatory trailing id**,
which is the guard the loose copies lacked.

### 2. B477 — the async pose ladder, merged AND driven

The two textually duplicated d05/d02/d01 + `setToLastFrame()` blocks
(`entity_mesh_build.cpp:365-395`, `entity_build_step.cpp:420-438`) are now one
function, `EQT::Graphics::ApplyCorpseDeathPose()` —
`include/client/graphics/corpse_death_pose.h` (38 lines) +
`src/client/graphics/corpse_death_pose.cpp` (53 lines), added to `willeq_core`.
Both call sites call it.

**And the async path is now driven to `Built` and asserted**, which T454 could
not do. The reason it could not: `processOneEntityBuildStep()`'s selection
gates reject an entity whose `cachedBspRegion` is unset *and* whose background
prep never ran, and neither is settable from outside `EntityRenderer`. The
lever that works is the player-entity priority — `setPlayerSpawnId()` bypasses
those gates for one step, after which the entity is "in progress" and needs no
gate; `setPlayerSpawnId(0)` then clears the `isPlayer` flag it set. Separately,
the `Placeholder` phase returns immediately unless the race model is already
cached, so the harness calls `preloadModelData()` — the same call
`EntityPrepWorker::processRequest()` makes. Both are written out at the
fixture.

These two assertions are a **preservation pin, not a bug repro**: the duplicate
was already correct, and they passed pre-merge as well as post-merge. What they
buy is that the duplicate cannot come back and drift, and that the staged path
is executed by a test at all.

### 3. B476 — `equipment_models` dropped, db rebuilt and re-pinned

**The B468 check was made deliberately first, and the route is HEALTHY, not
broken** — redundancy, exactly like T454's textures:
`EquipmentModelLoader::buildEquipmentIndex()`
(`src/client/graphics/eq/equipment_model_loader.cpp:180`) fills
`equipmentModelIndex_` live from the seventeen `*equip*.s3d` archives at zone
load, and `getModelRef()` (:972) is what
`EntityPrepWorker::prepEquipmentModels()`
(`src/client/graphics/entity_prep_worker.cpp:406`) asks. `WilleqDatabase` is
the only sqlite consumer in the tree (`grep -rl sqlite src/client
include/client` → that TU and its header, nothing else) and names the table in
no SQL.

Deleted: the whole `loadEquipmentModels()` body, its `CREATE TABLE`, its
prepared statement, its declaration in `tools/willeq_db/db_builder.h`, its call
in `tools/willeq_db_builder.cpp`, and **its entry in the builder's input set**.

**Row diff — the table and nothing else** (`sqlite3 .dump`, before vs after):

| removed | 1,000 `INSERT INTO equipment_models`, the 7-line `CREATE TABLE`, `sqlite_stat1('equipment_models',…,'1000')`, `metadata.total_entries='29791'`, `metadata.input_digest='c39f5cfbb75e5744'` |
| added | `metadata.total_entries='28791'`, `metadata.input_digest='956f69a5686c98bb'`, the re-ANALYZEd `sqlite_stat1` line |

| | before | after |
|---|---|---|
| tables (excl. `sqlite_stat1`) | 11 | **10** |
| `metadata.total_entries` | 29,791 | **28,791** (exactly −1,000) |
| `metadata.input_digest` | `c39f5cfbb75e5744` | **`956f69a5686c98bb` — MOVED** |
| `metadata.version` | 7 | 7 — **`kWilleqDbExpectedVersion` NOT touched** |
| builder inputs | 10 | **9** |
| sha256 | `d503b952…` | `e9fc387f…` |
| size | 3,596,288 B | 3,518,464 B |

**`input_digest` moving is the deviation from T454 and it is correct.** T454
changed only the loader, so its nine remaining inputs hashed identically; T456
removes `data/equipment_models.json` from the input set itself, and a digest
over the inputs must move when one is dropped. Flagged rather than absorbed.
`data/willeq.db` and `data/willeq.db.sha256` are in ONE commit.
`scripts/build_willeq_db.sh --check` → **PASS** (rebuild byte-identical to both
the pin and the checked-in file).

The converse net's exemption list is now **EMPTY**:
`WilleqDatabaseShippedContent.EveryTableTheDatabaseShipsIsOneTheClientQueries`
has `kUnreadButShipped = {}`, asserts that it is empty, and carries positive
controls for both dropped tables.

### 4. Tests — pre-fix and post-fix, both runs shown

Pre-fix was produced with an exported patch (`git diff` → `git apply -R` over
the seven production TUs, never `git stash`), rebuilt, run, then `git apply`
to restore. Logs: `build/T456_prefix.log`, `build/T456_postfix.log`.

| test (`tests/test_corpse_death_pose.cpp`) | pre-fix | post-fix |
|---|---|---|
| `CorpseSpawnWire.*` (3, T454's) | PASS | PASS |
| `CorpseRecognition.ALiveNpcNamedCorpseBeetleIsNotACorpse` | PASS (the old `isAnyCorpse()` had no name test, so it got this one right by accident) | PASS |
| `CorpseRecognition.TheStateLayerAgreesWithTheOnePredicate` | **FAIL** — `isAnyCorpse()` said "not a corpse" for a rename-only corpse that `IsCorpseByFields()` calls one | PASS |
| `CorpseRecognition.NoProductionSiteDerivesCorpsenessFromANameSubstring` | **FAIL** — named five offending files: `bridge_component.cpp`, `application_zone_loading.cpp`, `combat.cpp` (×2 patterns), `combat_hunting.cpp` | PASS |
| `CorpsePoseTest.*` (3, T454's) | PASS | PASS |
| `CorpsePoseTest.TheAsyncBuildPathHoldsTheSameDeathPose` | PASS (preservation pin — see §2) | PASS |
| `CorpsePoseTest.BothBuildPathsLandOnTheSameFrame` | PASS (preservation pin) | PASS |
| **binary total** | **9 passed, 2 FAILED (exit 1)** | **11 of 11 (exit 0)** |

The census net is comment-filtered (a tombstone naming the deleted pattern must
not read as the pattern returning) and carries **one** exemption,
`entity_renderer.cpp`, boarded as **B479** with the reason written at the skip.

| test (`tests/test_willeq_database.cpp`) | pre-fix | post-fix |
|---|---|---|
| `…EveryTableTheDatabaseShipsIsOneTheClientQueries` | **FAIL** — the new test against the OLD database (restored from `HEAD:data/willeq.db`): `equipment_models` shipped with no reader, and the positive control fired | PASS |
| binary total | — | **20 of 20 (exit 0)** |

Also green standalone: `DbReproducible`'s recipe (`build_willeq_db.sh --check`,
PASS), `check_doc_counts.py` (**OK: every stated count matches the tree**),
`check_file_sizes.sh fable` (19 checked, 19 ok, 0 warn, 0 fail) plus `--files`
on the two new untracked files (53 and 38 lines, ok).

### 5. Docs and board

- `CLAUDE.md` — recipe table row 29,791/eleven tables/ten inputs → **28,791 /
  ten tables / nine inputs**; a new paragraph beside T454's recording what
  B476 was, the deliberate B468 check, the emptied exemption list and why
  `input_digest` moved this time.
- `data/willeq.db.sha256` — new hash, new row/table/input counts, and the
  `input_digest` explanation.
- `scripts/build_willeq_db.sh` — header input list, the "seven of the eleven
  inputs" arithmetic (twice) and the `--check` failure message.
- `tools/willeq_db_builder.cpp` — table list, "ELEVEN tables" → "TEN", T456
  tombstone folded into T454's.
- `tools/willeq_db/db_loaders_json.cpp` — banner and the B474/B476 note.
- `include/client/corpse_predicate.h` — the "do not write a second corpse test"
  note now records that it was ignored seven times and what each copy got
  wrong.
- `refactor/BOARD.md` — **B476, B477, B478 struck** with their evidence;
  **B479** and **B480** added. Numbers allocated from **B479** (T449 allocates
  from B474+, T455 from B476+ — collisions are renumbered at merge).

### 6. Not done (the pause)

The following steps of the verification bar were **not run**, and nothing here
claims them:

1. `ctest --test-dir build -j6` — the FULL suite. Only the two directly
   affected binaries were run (`test_corpse_death_pose` 11/11,
   `test_willeq_database` 20/20). The tree builds clean at `-j24` (exit 0, no
   exit 143, so no truncation scan owed), so a green suite is expected, but it
   is **unmeasured**.
2. `bash scripts/build-gles2-x86.sh` — **not run.** ⚠ The database bytes moved,
   and a stale `build-gles2` binary does NOT refuse this change (the version is
   unchanged at 7), but the tree must still be rebuilt before the gate is read.
3. Visual gate, standalone — **not run.** No golden file is in `git status`, so
   nothing was self-blessed; the STOP condition never fired because the gate
   never ran.
4. `bash scripts/build-arm-noble.sh` — **not run.**
5. Live proof — **OWED** (T449 holds the lock). Nothing in this unit opened a
   connection.

**Next step on resume:** rebuild `build-gles2` and `build-arm-noble` (the db
moved), then run `ctest -j6`, the standalone visual gate and the ARM build, in
that order. No source change is expected to be needed.

### 7. Deviations and surprises

- **B478's census was short by three.** The board named four copies; there are
  seven. Two of the three it missed (`combat_hunting.cpp`) are wrong in the
  opposite direction from the ones it found — they let npc corpses through as
  live hunt targets — which is why a *census net* rather than a list of edits
  is the actual close-out.
- **`metadata.input_digest` moved**, unlike T454's drop. Correct, and explained
  in three places (pin file, `CLAUDE.md`, board) so the next reader does not
  read it as non-determinism.
- **A new board item fell out of the census exemption (B479).** The client
  synthesises a corpse display name without the entity id, so a corpse whose
  death it witnessed carries a name the player cannot type — while
  `MakeCorpseName()` (T185) exists to build the real one. Wants an oracle
  measurement before being touched.
- **`data/equipment_models.json` and `tools/equipment_index_builder` now feed
  nothing** (B480). Left in the tree deliberately: dropping a shipped INPUT is
  a different question from dropping a shipped TABLE, and B468's lesson is that
  each gets its own check.
