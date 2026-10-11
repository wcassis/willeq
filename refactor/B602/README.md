# B602 (#186): EntityManager's query and mutation API with no production caller

**Status: CLOSED by this unit** (`task/kb-b602-entity-manager-zero-callers`,
cut at `origin/fable` `06852f31`, 2026-10-11). Found by T458, boarded in
`refactor/BOARD.md` B602 (11 rows, all under the `"B602"` key in
`EXEMPT_TESTS_GROUPS` of `scripts/refactor/check_zero_callers.py` — i.e. the
tests-only bucket, since each had exactly one caller: a test).

## The table

Every row was grepped tree-wide (`src include tests tools benchmarks docker
scripts`) before deciding; "production caller" means any use outside `tests/`.
`EntityRenderer::getEntitiesInRange` (`include/client/graphics/entity_renderer.h:931`,
`src/client/graphics/entity_spatial.cpp:81`) is a DIFFERENT class and is live
(`entity_spatial`'s spatial-grid lookup); it is not one of these rows and is
untouched.

| owner::name | verdict | evidence |
|---|---|---|
| `EntityManager::findEntityByExactName` | **deleted** | decl `include/client/state/entity_manager.h:200`, def `src/client/state/entity_manager.cpp:218`; only uses: `tests/test_state_modules.cpp:390-392` (test deleted). T362 (B070) already made `findEntityByName` the one name resolver — this exact-match sibling was never wired. |
| `EntityManager::findPetByOwner` | **deleted** | decl h:293, def cpp:308; only uses: `tests/test_state_modules.cpp:487-488` (deleted). |
| `EntityManager::forEachEntity` | **deleted** | decl h:307, def cpp:329; only use: `tests/test_state_modules.cpp:507` (deleted). |
| `EntityManager::forEachEntityMutable` | **deleted** | decl h:313, def cpp:335; only use: `tests/test_state_modules.cpp:514` (deleted). |
| `EntityManager::getAllPets` | **deleted** | decl h:299, def cpp:317; only use: `tests/test_state_modules.cpp:490` (deleted). |
| `EntityManager::getEntitiesInRange` | **deleted** | decl h:231, def cpp:233; only uses: `tests/test_state_modules.cpp:400,408` (deleted). |
| `EntityManager::getNearestCorpse` | **deleted** | decl h:270, def cpp:287; only use: `tests/test_state_modules.cpp:470` (deleted). |
| `EntityManager::getPetOwnerId` | **deleted** | decl h:286, def cpp:300; only uses: `tests/test_state_modules.cpp:485-486,498` (deleted). |
| `EntityManager::markAsCorpse` | **deleted** | decl h:156, def cpp:87; only uses: `tests/test_state_modules.cpp:182,188` (deleted). |
| `EntityManager::updateEntityEquipment` | **deleted** | decl h:150, def cpp:70; only uses: `tests/test_state_modules.cpp:168-176` (deleted). |
| `EntityManager::updateEntityHP` | **deleted** | decl h:140, def cpp:56; only uses: `tests/test_state_modules.cpp:149-162` (deleted). |
| `EntityManager::fireEntityStatsChangedEvent` (private) | **deleted — cascade** | decl h:323, def cpp:387. Its ONLY caller was the deleted `updateEntityHP` (cpp:65); with that gone it is zero-caller (grep: declaration, definition and a comment in `tests/test_console_target_hp.cpp:16` — comment re-pointed to the event type). It was also the sole PUBLISHER of `GameEventType::EntityStatsChanged` on the state-layer EventBus. Deleting it loses no live behaviour because that enumerator is still published and consumed: `ReachabilitySweep` passes [measured, `test_reachability_sweep` 5/5 green at this tip] with the remaining publisher `ZoneProcessMobHealth`'s bridge `pushEvent(EntityStatsChanged)` (`src/client/entities/entities_component_appearance.cpp:207`) and the consumer `ConsoleRenderer`'s subscription (`src/client/output/console_renderer.cpp:113`) + `IrrlichtBridge`'s case (`src/client/bridge/irrlicht_bridge.cpp:97`). |

## The B468 trap — the live routes, named

"Nothing reads it" and "nothing CAN read it" look identical from a grep, so for
each MUTATOR the live route that updates the same fields is named here:

**`updateEntityHP` — entity HP percent.**
The live writer of a non-player entity's HP is `ZoneProcessMobHealth`
(`entities_component_appearance.cpp:163`), which writes the EverQuest-side
entity store `it->second.hp_percent = hp_percent` (line 179) and then:
- updates `CombatState::setTarget(...)` when the mob is the current target
  (line 189, which fires `TargetChanged` on the state EventBus —
  `combat_state.cpp:66`),
- updates `PetState::updatePetStats(...)` when it is the pet (line 195),
- pushes `EntityStatsChanged` / `TargetChanged` / `PetStatsChanged` onto the
  renderer bridge (lines 200-245).
The deleted setter published `EntityStatsChanged` on the state EventBus — the
`ConsoleRenderer` headless status line takes target HP off that same enumerator
(`console_renderer.cpp:113,193`), but the live feed of that enumerator in a
headless run is the MobHealth bridge path (single-stage `applyEvent` in
`GameStateSyncBridge::applyEvent`, `game_state_sync_bridge.cpp:6`), not the
EntityManager publisher, which fired only when `updateEntityHP` was called —
and it was called by nothing.

**`updateEntityHP` — the state-layer `Entity::hpPercent` field.**
Note: `ZoneProcessMobHealth` does NOT write the state-layer
`eqt::state::Entity::hpPercent`; that field is refreshed only when
`SyncEntityToGameState` runs (spawn: `entities_component.cpp:332`,
`entities_component_updates.cpp:434`; zone-in self: `zone_entry.cpp:672`;
corpse rename: `zone_dispatch.cpp:515`; death: `player_state.cpp:182`). That
staleness is pre-existing and unchanged by this deletion: the live HP readers
never read the state-layer field. `PlayerState` is the only store for the
player's own HP (`entities_component_appearance.cpp:270` "PlayerState is the
only store"), `CombatState::m_targetHPPercent` for the target,
`GroupState`/`PetState` for group/pet members — all of them written by routes
that do not go through `EntityManager::updateEntityHP`.

**`updateEntityEquipment` — `Entity::equipment[]` / `equipmentTint[]`.**
Live writes: `SyncEntityToGameState` copies all 9 slots from the
EQ-side entity (`player_info.cpp:155-158`) and `ApplyAppearanceToGameState`
merges them from the `EntityAppearanceChanged` event (`entities_component_appearance.cpp:489-492`),
the latter driven by `ZoneProcessWearChange` (`entities_component_appearance.cpp:409`).
`WriteOnlyFields` stayed green at 48/86 after the deletion [measured], so no
field became write-only.

**`markAsCorpse` — `Entity::isCorpse`.**
The live route is the MobRename handler: `zone_dispatch.cpp:512-515` sets
`it->second.is_corpse = true` on the EQ-side entity and then calls
`SyncEntityToGameState` (whose comment at line 513-514 literally says
"GameState kept the LIVING name + isCorpse=false forever (markAsCorpse had no
caller) -- /target and /loot missed it" — T268's fix, the one this board row's
name references). Death handling re-syncs the same way
(`player_state.cpp:182`). `isCorpse` itself is read live by
`IsCorpseByFields` (T456's one corpse predicate, `findEntityByNameRanked`
cpp:193, `command_processor_loot.cpp:93`) and the spawn-event payload
(cpp:248).

**Pet queries (`findPetByOwner`/`getAllPets`/`getPetOwnerId`) — `Entity::isPet` /
`petOwnerId`.**
The fields are written by `SyncEntityToGameState` (`player_info.cpp:164-165`)
from `Spawn_Struct` offsets 329/189 (`entities_component_updates.cpp:429-430`)
and read by the scenario lane (`scenario_world_app.cpp:73-120`, `Entity::
equipment[]` census comment) — the FIELDS stay; only the query functions go.
`isPet(uint16_t)` (the kept predicate, not on this list) — its tests
(`PetQueriesTrackOwnerRelationship`, `IsPetAbsentEntityReturnsFalse`) shared
callers with the deleted siblings, so those two TEST blocks go with the
siblings. `isPet` itself was not boarded (it sat in no EXEMPT table — the
census's name-match sees the `Entity::isPet` member writes/reads as uses), so
it stays declared without a row, as before [measured: tests-only census is
309 at GREEN, `isPet` in no bucket].

## What was deleted, file by file

- `include/client/state/entity_manager.h`: the 11 declarations + the
  `fireEntityStatsChangedEvent` declaration. (11 + 1 = 12 declarations.)
- `src/client/state/entity_manager.cpp`: the 11 definitions + the
  `fireEntityStatsChangedEvent` definition (12 bodies). `calculateDistance`
  (private, def cpp:289) is KEPT: it was already zero-caller BEFORE this unit
  (private members are outside the census's scope) and is not one of the 11,
  so it is out of scope — flagged in "Not verified / left open" below.
- `tests/test_state_modules.cpp`: the 11 TEST() blocks that were the only net
  (file 1087 → 952 lines; 83 → 69 TESTs in this file).
- `tests/test_console_target_hp.cpp`: header comment re-pointed — it named
  `fireEntityStatsChangedEvent` as the publisher; it now names the event type
  only.
- `scripts/refactor/check_zero_callers.py`: the 11 `"B602"` rows of
  `EXEMPT_TESTS_GROUPS` and the now-orphaned `BOARD_REASONS["B602"]` entry.
- `refactor/BOARD.md`: B602 struck (strikethrough + STRUCK note), one bullet.

`ZeroCallersSelfTest` (ctest, hygiene) was NOT touched and passes.

## RED / GREEN

RED — `scripts/refactor/check_zero_callers.py --repo . --check` after the
function deletions, BEFORE the table rows went (full output:
`/home/user/kb-evidence/b602-entity-manager-zero-callers/RED_zero_callers.txt`):

```
zero-caller census clean: 374 functions, all classified in EXEMPT (boarded 374).
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::findEntityByExactName -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::findPetByOwner -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::forEachEntity -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::forEachEntityMutable -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::getAllPets -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::getEntitiesInRange -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::getNearestCorpse -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::getPetOwnerId -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::markAsCorpse -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::updateEntityEquipment -- ... B602
STALE EXEMPT_TESTS exemption (no longer tests-only): EntityManager::updateEntityHP -- ... B602

tests-only: 0 unlisted, 11 stale.
An exemption whose function has changed bucket must be removed from EXEMPT_TESTS -- the table is not allowed to outlive its rows.
```
(exit 1 — 11 STALE, exactly the 11)

GREEN — after the rows and `BOARD_REASONS["B602"]` went [measured]:

```
zero-caller census clean: 374 functions, all classified in EXEMPT (boarded 374).
tests-only census clean: 309 functions, all classified in EXEMPT_TESTS (boarded 91, observer 169, seam 49).
```
(tests-only 320 → 309 = exactly the 11 deleted; zero bucket unchanged.)

`check_write_only_fields.py --repo . --check` green both at RED and at GREEN:
`write-only census clean: 48 fields` / `unused census clean: 86 fields` — the
deletions exposed no new write-only or unused field.

Local builds/tests [measured, this worktree, `-j3`, targets rebuilt after the
last edit]: `willeq`, `test_state_modules`, `test_state_modules_secondary`,
`test_console_target_hp`, `test_reachability_sweep` all build clean;
`test_state_modules` 69/69, `test_state_modules_secondary` 40/40,
`test_console_target_hp` 3/3, `test_reachability_sweep` 5/5 pass. Full-suite +
visual gate is CI's job (`task/*` pipeline on gitlab-runner-1); gate frames
must be identical — nothing a player reaches changed (every deleted function
was unreachable in production, and the only event-publish path removed,
`fireEntityStatsChangedEvent`, was never on a live call chain).

## Not verified / left open

- `EntityManager::calculateDistance` (private, `entity_manager.cpp:289`) is
  dead (zero callers tree-wide, pre-existing — private members are outside
  the census). NOT deleted here: it is not one of the 11 and is not made
  dead by this unit. Candidate for a follow-up zero-caller sweep that extends
  the census to private members.
- The state-layer `Entity::hpPercent` is refreshed only on the
  `SyncEntityToGameState` routes (named above); between them it is stale for
  non-player mobs. Pre-existing, unchanged, and no live reader of that field
  depends on it (HP readers use PlayerState/CombatState/PetState/GroupState).
- `refactor/T458_tests_only_callers.md:159` and `refactor/T362_warp_ranked_completions_dedup.md:111`
  still name some deleted functions in HISTORICAL unit plans — those are
  bannered history (DocClaims-exempt, struck-ledger text) and are left, per
  the B131/T373 rule that only LIVE board bullets are scanned.
