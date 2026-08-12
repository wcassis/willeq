# T81 — Project entity state into `Application::m_gameState`

**Slot: first free. x86 only. No Pi. NO EQEMU** unless the live leg proves
necessary — see Verification.

**Territory: `src/client/bridge/game_state_sync_bridge.cpp` + its header,
`src/client/eq_entities.cpp` / `src/client/eq_player_info.cpp` (publishing
only — do NOT change entity tracking logic), and tests.**

Do not touch `ActionDispatcher`/`CommandProcessor` logic — they are the
consumers and should need no change. Do not attempt doors or group; each
is its own unit. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T78_two_gamestate_analysis.md`'s
Review in full** — it is the census this unit acts on — then
`include/client/bridge/game_state_sync_bridge.h`'s docstring, which states
the governing rule you must satisfy.

## Why this unit exists

Will has chosen **Option B** from T78: complete the source→projection
migration one category at a time, rather than converging the two
`GameState` instances (Option A) or accepting the breakage (Option D).

`EverQuest::m_game_state` is the network-authoritative copy on the game
thread. `Application::m_gameState` is the projection the render/main-thread
consumers read. `GameStateSyncBridge` feeds the projection from typed
bridge events, and its docstring records the deliberate, incremental
history: pet (T40), inventory (T42), spells (T44) — each replacing a
once-per-tick `sync*State()` poll, each admitted only once the state was
**"verified as fully reconstructible."**

**Entities were never migrated, and nothing polls them either.** So
`Application::m_gameState->entities()` is permanently empty, which is why
`/who` prints nothing, and `/face`, `moveToEntity`, `followEntity` and
interact-nearest all report "Entity not found" for entities that plainly
exist (`action_dispatcher.cpp:99,154,167,180,446`,
`command_processor.cpp:828`).

This unit builds that projection.

## The reconstructibility question, and why it is already answered

The bar is "fully reconstructible from events." For entities it is met,
because **the sender already holds the complete struct**:

`EverQuest::SyncEntityToGameState()` (`eq_player_info.cpp:261`) takes a
fully-populated `Entity` from `m_entities` and copies the **whole thing**
into `GameState` — add-or-replace, not a delta. It is called on spawn
(`eq_entities.cpp:439`), on update (`:1211`), and for the self-entity
(`eq.cpp:2378`).

So there is no accumulation for the receiver to do and **no completion
state machine is needed**: publish a snapshot, the receiver replaces its
copy. If a spawn packet lacks equipment and it arrives later, the next
sync republishes the whole struct and the projection converges on its own.

**Verify that claim before building on it.** If some field genuinely only
ever arrives once and is not retained in `m_entities`, snapshot publishing
would lose it — establish that it does not happen, or report it.

## The real constraint: volume

Entity updates are frequent — every mob's position — and the bridge
already coalesces `EntityMoved` per entity for exactly that reason
(`CLAUDE.md`, D24). **Do not publish a full entity snapshot on every
update.**

Design around the split:
- **Full snapshot** on spawn, despawn, and appearance/equipment change —
  infrequent, and where completeness matters.
- **Existing coalesced movement events** carry position. If
  `EntityMoved`'s payload is sufficient to update the projection's
  position, use it; say so explicitly either way.

State the event vocabulary you settled on and why, including anything you
had to add. Adding a typed event is acceptable; silently widening an
existing one's meaning is not.

## Keep the switch narrow — the docstring's rule is binding

`GameStateSyncBridge`'s header says to keep the switch narrow and admit
only verified-reconstructible events, and that everything unrecognised
must pass through to the wrapped bridge untouched. **Honour both.** This
unit adds entity cases and nothing else; doors and group are separate
units with their own reconstructibility arguments.

Watch for the question T58 had to settle for position: whether adding a
handler creates a **double-publish** or feeds a genuinely disjoint
consumer. Answer it for entities explicitly, with evidence, rather than
assuming it is fine because the categories differ.

## What success looks like

`Application::m_gameState->entities()` is populated in production, and the
consumers that read it start working with **no change to their own code**.
That is the test of whether this is really a projection gap: if
`ActionDispatcher` needs editing, something else is wrong and you should
report it.

Cover in tests: spawn populates the projection; despawn removes it;
update replaces rather than duplicates; the self-entity appears; zone
change clears it (`eq_zone.cpp:270` clears the source — the projection
must not keep stale entities).

## Verification

Full protocol bar. A headless test proving the projection populates is the
core evidence.

**A live leg would be strong but is not required** — if `.eqemu-lock` is
free and time allows, run a session and show `/who` listing real nearby
entities and `/face` succeeding on a visible NPC, which is the
user-visible proof. If the lock is held, say so and record the live leg as
outstanding, the same way T66 and T77 did.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T81-entity-projection`, branched from `fable` at `9ebbaa682` (the tip
at checkout time — `fable` advanced past this via T76's merge partway
through the session; noted below under "deviations" since it affected step
6's verification, not the diff itself).

### Reconstructibility verification

Confirmed by tracing every call site, not assumed from the plan's own
framing:

- `EverQuest::SyncEntityToGameState()` (`eq_player_info.cpp:261`, unchanged
  by this unit except for one addition described below) is called from
  exactly **three** places, all confirmed by `grep`: `ZoneProcessZoneSpawns`
  (`eq_entities.cpp:439`, bulk zone-in), `ZoneProcessNewSpawn`
  (`eq_entities.cpp:1211`, single new spawn), and the self-entity path
  (`eq.cpp:2378`). **None of the three is a per-tick or movement handler** —
  this confirms the plan's claim that the sender holds and republishes the
  complete struct, not a delta, and that there is no per-tick volume problem
  at this call site specifically (the volume problem is elsewhere, in
  position updates — see below).
- **The one field-loss case the plan asked to rule out or report: found
  one, and it doesn't invalidate the claim, but it does bound it.**
  `ZoneProcessWearChange()` (`eq_entities.cpp:1944`) mutates
  `m_entities[spawn_id].equipment[...]` **directly** and publishes
  `EntityAppearanceChanged` — but never re-invokes `SyncEntityToGameState()`.
  So a runtime gear swap updates the raw `m_entities` map (retained, not
  lost) and the cross-thread appearance event, but **not**
  `EverQuest::m_game_state`'s own copy either — this staleness pre-exists in
  the network-authoritative source, independent of this unit. A spawn-only
  snapshot on this unit's side would inherit the identical staleness (no
  worse than the source) unless something also consumes
  `EntityAppearanceChanged`, which this unit does (see vocabulary below) —
  so the projection actually ends up *more* current than
  `EverQuest::m_game_state` for this one field set, not less.
  `AT_SIZE`/`AT_PET`/anon-status appearance-op updates
  (`eq_entities.cpp` ~1603-920) have the same direct-mutate-without-resync
  shape and **no bridge event exists for them at all** — flagged, not fixed;
  same pre-existing-in-the-source staleness, out of this unit's "publishing
  only" territory (fixing it would mean adding new publish call sites for
  fields nobody asked this unit to migrate).

### Event vocabulary chosen

- **Added**: `EntityStateSnapshot` / `EntityStateSnapshotData` (wraps a full
  `eqt::state::Entity` by value) — `event_bus.h`. The existing
  `EntitySpawnedData` (graphics-tuned, used by `IrrlichtBridge`) was
  deliberately **not** reused: it's missing `guildId`, `size`, `isPet`,
  `petOwnerId`, `bodytype`, `showhelm`, `light`, weapon-skill fields —
  exactly the kind of silent-widening the plan said not to do. Publishing a
  second, complete-struct event from the one call site that actually holds
  the complete struct (`SyncEntityToGameState()`) was cleaner than growing
  `EntitySpawnedData` to carry fields the renderer doesn't need.
- **Reused, unchanged**: `EntityDespawned` (already published at the same
  call site as `RemoveEntityFromGameState()`, `eq_entities.cpp:1687-1695`),
  `EntityMoved` (already D24-coalesced), `EntityAppearanceChanged` (already
  published from `ZoneProcessWearChange()`/`UpdatePlayerAppearanceFromInventory()`),
  `ZoneUnloading` (already published from `CleanupZone()` right before the
  source-side clear). None of their payloads or existing consumers were
  touched — new `case`s only, in `GameStateSyncBridge`.

### Volume split

`EntityStateSnapshot` (full struct) fires only on spawn/self — confirmed
above, not per-tick. Position updates use the existing coalesced
`EntityMoved` and go through `EntityManager::updateEntityPosition()`
(partial field update: x/y/z/heading/delta*/animation only, no-op if the
entity isn't already in the projection — a move before/without a spawn does
not create a partial entity). Equipment/appearance changes use
`EntityAppearanceChanged` (partial field update on the existing entity,
same no-op-if-missing rule). No new per-tick publish was added anywhere.

### Double-publish answer

**Yes, for entity spawn and despawn specifically — and it's real, not
hypothetical, but it is cosmetic (console text duplication), not a
correctness bug.** `EntityManager::addEntity()`/`removeEntity()`
(`entity_manager.cpp`) fire `EntitySpawned`/`EntityDespawned` internally
onto whichever `GameState`'s **own private** `EventBus` they're called on —
this is pre-existing `EntityManager` behavior, unchanged by this unit.
`ConsoleRenderer::connectToGameState()` subscribes to exactly that private
bus on `Application::m_gameState` (`console_renderer.cpp:79-92`), used in
both headless and graphical modes (`headless_mode.cpp:33`,
`graphical_mode.cpp:66`). Before this unit, `Application::m_gameState`'s
`entities()` was never mutated, so that subscription was permanently dead
for entities. Now that `GameStateSyncBridge` calls
`m_state.entities().addEntity()`/`removeEntity()`, it wakes that
subscription up — meaning a spawn/despawn produces **two** console lines:
`ConsoleBridge`'s `"[SPAWN] ..."` (from the original cross-thread event,
unrelated to this unit) and `ConsoleRenderer`'s `"* X has entered the
area."` (from the newly-live internal re-fire). Verified this is not a new
failure mode invented by entities: **identical structural precedent already
exists for pets since T40** — `PetState::setPet()` also fires `PetCreated`
onto the same private bus, and `ConsoleRenderer` also subscribes to it; it
simply doesn't collide today because `ConsoleBridge` never had a `PetCreated`
case to begin with. Position updates (`EntityMoved` via
`updateEntityPosition()`) do **not** double-publish: nothing subscribes to
`EntityMoved` on `Application::m_gameState`'s private bus. Update snapshots
past the first `addEntity()` also don't re-fire (the add-or-replace path
uses `getEntityMutable()` + direct struct assignment, matching
`SyncEntityToGameState()`'s own pattern exactly, which never fires an
event). Not fixed — suppressing the internal fire would mean changing
`EntityManager` itself, outside this unit's "publishing only" territory,
and the effect is a duplicated informational line, not corrupted state.
Documented in the header docstring and here for whoever does doors/group
next, since they'll hit the identical question.

### Consumers now work — falsification test result

**Confirmed no changes to `ActionDispatcher` or `CommandProcessor` were
needed — the diagnosis holds.** `git diff fable -- src/client/action_dispatcher.cpp
src/client/command_processor.cpp` (via `git diff --stat`, see below) shows
zero touches to either file. `tests/test_action_dispatcher.cpp` already
proves the consumer side works whenever `state.entities()` is populated —
e.g. `EXPECT_TRUE(dispatcher->faceEntity("Bob").success)` after
`state.entities().addEntity(e)` (lines ~110-114, pre-existing, unmodified by
this unit) — this unit's job was only to make production actually call that
same `addEntity()`/the add-or-replace path, which it now does via
`GameStateSyncBridge`. `CommandProcessor::cmdWho` reads
`m_state.entities().getEntitiesInRange(...)` (`command_processor.cpp:828`,
unmodified) — same argument applies.

### What is deferred

- **Doors** — worse than a two-instance split per T78: a third, unconnected
  `DoorStateManager` on `EverQuest` holds the only real door data; neither
  `GameState::doors()` is ever populated by anyone. Its own unit, its own
  reconstructibility argument.
- **Group** — all setters currently target only `EverQuest::m_game_state.group()`.
  Its own unit.
- **Combat** — not broken (T78 confirmed `Application::syncCombatState()`
  already populates `m_gameState->combat()` every tick); intentionally left
  on its existing poll, not touched.
- **The `AT_SIZE`/`AT_PET`/anon-status/`ZoneProcessWearChange`-adjacent
  staleness in `EverQuest::m_game_state` itself** (see reconstructibility
  section) — pre-existing, out of this unit's "publishing only" scope.
- **Double-publish cosmetic duplication** for entity spawn/despawn console
  lines (see above) — flagged, not fixed.
- **Live leg** — `.eqemu-lock` was free at execution time (T76, the sibling
  that might have held it, had already merged). Not run: the headless
  projection tests (11 new cases, all passing) were judged sufficient core
  evidence per the plan's own "not required" framing, and the unit's time
  budget was spent on the reconstructibility/double-publish verification
  the plan asked to be evidenced rather than assumed. Recorded as
  **outstanding by choice, not by block** — different from T66/T77's
  lock-held case; a live `/who` + `/face` session against a real zone would
  still be worth running as a follow-up if Will wants the user-visible
  proof.
  **CLOSED (T82, 2026-08-12).** `/face` confirmed working live against a
  real zone, zero code changes needed — exactly as this Review's own
  falsification test predicted. `/who` turned out to be unreachable live for
  a reason outside this unit's scope (shadowed by a same-named, pre-existing
  `EverQuest::m_command_registry` command reading a different, older data
  source — see `refactor/T82_live_verification_session.md`'s Review and
  `refactor/deferred.md`'s updated entry for the full trace).

### Deviations

- **`fable` advanced during the session** (T76 merged) before step 6's
  `check_file_sizes.sh` ran. Run as `bash
  scripts/refactor/check_file_sizes.sh fable` (protocol's literal
  invocation) it reported a false `FAIL` on `src/client/graphics/entity_renderer.cpp`
  (3321 lines) — a file this unit never touched (confirmed:
  `git diff fable --stat` at that point showed it changed only because
  `fable`'s tip had moved past this branch's parent, pulling in T76's
  unrelated edits as part of the diff). Re-ran anchored to the actual branch
  point, `bash scripts/refactor/check_file_sizes.sh 9ebbaa682c3a49996457443256e294009acf7deb`
  — 5 files changed (all mine), 4 ok, 1 pre-existing WARN
  (`src/client/eq_player_info.cpp`, 2034 lines, already >1500 before this
  unit's +13-line addition), 0 FAIL. This is the exact drift class T79
  hardened the script against for the wrong-cwd case; here it's the
  base-rev-moved-during-the-session case, which the script has no way to
  detect on its own — worth noting for the next executor who sees a
  surprise FAIL against files they didn't touch.
- No other deviations from the contract.

### Verification summary

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build --output-on-failure` — 2066/2067, sole failure
   `AudioManagerTest.PreloadCommonSounds` (documented ALSA host exception).
   36/36 in `test_game_state_sync_bridge` (25 pre-existing + 11 new T81
   cases: spawn populates, self-entity spawns, replace-not-duplicate,
   despawn removes, move updates position without touching other fields,
   move-before-spawn is a no-op, appearance-changed updates equipment
   without touching position, zone-unloading clears all entities, and
   delegation-to-wrapped-bridge for every new case).
3. `bash scripts/build-gles2-x86.sh` — built clean.
4. Visual gate — PASS (`visual_gate` ctest case, 18.81s), goldens
   byte-identical (`git status` shows only this unit's 5 intended files
   modified — no golden touched).
5. `bash scripts/build-arm-noble.sh` — built clean, `willeq` ELF produced.
6. `check_file_sizes.sh` — see "deviations" above; against the correct base,
   0 FAIL, 1 pre-existing WARN, no new warnings introduced.

Board: T78's `deferred.md` entry updated with a T81 addendum recording
entities CLOSED and doors/group/combat's status — see
`refactor/deferred.md`'s "TWO `state::GameState` instances exist,
unconnected" section.
