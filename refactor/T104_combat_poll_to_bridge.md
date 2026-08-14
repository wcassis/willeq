# T104 — Replace the combat per-frame poll with bridge events

**Slot: slot2. Headless first, then a LIVE leg. T103 runs in parallel and also
wants eqemu — do all headless work first, then contend for the lock; if held,
wait, then report the live leg outstanding rather than stealing it.**

**Territory: `src/client/application.cpp` (removing the poll),
`src/client/combat.cpp` / `include/client/combat.h`,
`include/client/state/`, `include/client/bridge/`,
`src/client/bridge/game_state_sync_bridge.*`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T104/`.**

**Will's ruling: Trade and Combat should go through the bridge.** This unit is
Combat. T103 is Trade.

Read T81's Review first — it is the model for this exact kind of migration —
then T78's, which deferred combat as *"still uses the old syncCombatState poll
and works."* Will has now called it in.

## The defect

`Application::syncCombatState()` (`application.cpp:1830`, called per tick from
~line 1827) **polls** rather than receiving events. It mirrors:

- `targetId` from `CombatManager`
- target `name`, `hp_percent`, `level` from **the entity map**
- subsequent target HP changes
- the auto-attack flag

and keeps its own dedupe state (`m_lastCombatTargetId`, `m_lastCombatTargetHp`,
`m_lastAutoAttacking`).

This is the last category of T78's Option B migration. Entities (T81) and group
(T99) are done; doors remain separately blocked on routing.

## The hard part — name it before you design

**The poll reads two domains together and events do not.** Target *identity*
comes from `CombatManager`; target *name, HP and level* come from the entity
map. A single poll gets both consistently for free. Event-driven, you must
correlate them:

- when the target changes, the entity may not be in the map yet (the poll
  already handles this, mirroring the id with placeholder fields);
- when the target's HP changes, that is an **entity** event, not a combat one,
  and only matters if that entity happens to be the current target.

Decide deliberately whether target HP should be projected by the combat domain
reacting to entity events, or read from the entity projection at the point of
use. **State your choice and why.** Getting this wrong produces a target whose
HP silently stops updating — which a poll would never do, and which a
casual test would not catch.

## Follow T81's discipline

- **Verify reconstructibility, do not assume it.** For each field the poll
  copies, prove the event stream carries enough to rebuild it. T81 did this by
  showing its source republished the complete struct at every call site.
- **Add typed events rather than widening existing ones.**
- **Apply the falsification test.** T81's plan said: if consumers need
  changing, the diagnosis was wrong and this was never a projection gap. State
  whether consumers needed changes.
- **Report field-loss cases** rather than glossing them.

## Remove the poll — that is the point

Deleting `syncCombatState()` and its three dedupe members is the deliverable.
A migration that leaves the poll in place alongside events has made things
worse, not better: two writers to the same projection.

**STOP and report** if the poll cannot be removed. Knowing precisely which
field defeats the event model is worth more than a half-migration.

## Live leg — the bar

Combat state is observable, so verify it honestly:

- Target something and confirm the projection reflects the target and its HP.
- Damage it and confirm HP **keeps updating** — this is the correlation risk
  above, and the single most likely thing to be quietly broken.
- `/combat on` and confirm the auto-attack flag tracks.
- Clear the target and confirm the projection clears.

Note T102's live experience: `/attack` drew a real "too far away" refusal and a
GM `#goto` to close distance did not work. Plan for getting genuinely adjacent
to something attackable, or use a pet.

Session discipline: binary freshness against HEAD first; daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`;
restore DB state and confirm by `SELECT` **after logout**; kill clients by real
binary PID, not `nohup`'s `$!`; clean teardown, delete the lock.

## What you may not do

- Do not touch trade state — that is T103, running in parallel. Overlapping
  edits to `game_state_sync_bridge.cpp` are likely, so keep changes narrow and
  additive.
- Do not change `CombatManager`'s own behaviour. It is authoritative; this unit
  changes how its state *reaches the projection*, not what it does.
- Do not leave both the poll and the events in place.

## Verification

Full build and suite (current total 2217; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Every new test
**proven able to fail in both directions**. Visual gate — goldens must not
move.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T104-combat-bridge`, off `fable` at `c76c93b9` (the checkout tip).
T103 (Trade) ran in parallel in slot1 over the same shared-lock window;
both were told to keep `game_state_sync_bridge.cpp` changes narrow and
additive — this unit's edits there are two new `case`s, nothing else
touched.

### Reconstructibility, per polled field

`Application::syncCombatState()` mirrored four things every tick:
`targetId` (from `CombatManager`), target `name`/`hp_percent`/`level`
(from the entity map), and the auto-attack flag. Traced, not assumed:

- **Target identity** (`targetId`): `CombatManager::m_current_target_id`
  is mutated by exactly two public methods, `SetTarget(uint16_t)` (which
  `SetTarget(const std::string&)` and every internal caller —
  `CycleTargets()`, `FindNextHuntTarget()`, `ProcessConsiderResponse()`,
  auto-hunt — funnel through) and `ClearTarget()`. Confirmed by reading
  `combat.cpp` in full and grepping every write site to
  `m_current_target_id`: no other method touches it. So these two methods
  are the **complete** choke point — reconstructible by construction, not
  by assumption.
- **Target name/HP/level**: at the moment `SetTarget(uint16_t)` succeeds,
  it already holds the matching `Entity` (it found it by iterating
  `m_eq->GetEntities()`), so the full record is available at the exact
  choke point that needs it — the same "sender already holds the complete
  struct" shape T81 verified for entity snapshots.
- **Auto-attack flag**: `m_auto_attack_enabled` is mutated by exactly two
  methods, `EnableAutoAttack()` and `DisableAutoAttack()` — grepped, no
  other write site exists.

All four polled fields are therefore reconstructible from events fired at
these four methods; no accumulation, no partial-state risk.

### The hard part: how target-HP correlation was resolved, and why

**Chose: the combat domain reacts to entity events, via the pre-existing
mechanism, not a new one.** Before touching anything, I read
`eq_entities.cpp`'s `ZoneProcessMobHealth()` (the handler for the
server's `MobHealth` opcode) and found the correlation already
implemented there, for the graphics/renderer target HUD: whenever an
entity's HP changes, it publishes `EntityStatsChanged` unconditionally,
**and**, if that entity's spawn id equals `m_current_target_id`,
re-publishes a **full** `TargetChanged` with the new HP — i.e. "is this
HP change about my current target?" is already answered at the one place
that owns both the entity map and the target id, published as a
cross-thread bridge event. `GameStateSyncBridge` just needed a `case` for
`TargetChanged` to pick it up; no new correlation logic was written.

Rejected alternative — "read HP from the entity projection at the point
of use" (drop `CombatState::updateTargetHP()`/`targetHPPercent()`,
have every consumer look up `entities().getEntity(combat().targetId())`
instead): this would touch every read site of `combat().targetHPPercent()`
(`cmdHail`, `ConsoleRenderer`'s target display, this unit's own new
tests) to change their read path, which is exactly the falsification
test's failure mode — "if consumers need changing, this was never a
projection gap." The chosen option needed zero consumer changes.

### Events added

- **`AutoAttackChanged`** (new: `GameEventType::AutoAttackChanged` +
  `AutoAttackChangedData{ bool enabled }`, `event_bus.h`) — no existing
  event meant "auto-attack toggled" (checked: `CombatEventData` models a
  combat-round outcome — Hit/Miss/Dodge/etc against a source/target pair
  — reusing it with a fabricated `Type` would be inventing data, exactly
  the trap `CombatState::fireCombatEvent()`'s own pre-existing comment
  already named for its own, different bus). Published from
  `CombatManager::EnableAutoAttack()`/`DisableAutoAttack()`.
- **`TargetChanged`** — reused unchanged (already carried
  `spawnId`/`name`/`level`/`hpPercent` plus renderer-only fields this
  projection ignores). Published from `CombatManager::SetTarget(uint16_t)`
  (full entity data on success) and `ClearTarget()` (`spawnId=0`) — new
  call sites in `combat.cpp`, not a new event type.
- **`TargetHPUpdatedData`/`GameEventType::TargetHPUpdated`** — left alone.
  Confirmed (grep) it has **zero production publishers**, before and
  after this unit; `TargetChanged` already carries HP and is what both
  the pre-existing re-publish and this unit's new choke points use. Not
  touched — publishing it would be scope creep.

Both new choke-point publishes live in `combat.cpp`/`combat.h` (in
territory) and reach the bridge via `m_eq->m_bridge` directly —
`CombatManager` is already `friend class CombatManager;` of `EverQuest`
(`eq.h`), so no change to `eq.h`/`eq_bridge.cpp` (out of territory) was
needed to reach the private `m_bridge` pointer, unlike `SpellManager`'s
own `m_bridge` member + `wireBridgeToSubsystems()` wiring (a different,
heavier pattern this unit did not need).

### Falsification test: consumers needed **zero** changes

`CommandProcessor` (`cmdHail`, `resolveCombatTarget()` for
assist/shield/duel/inspect) and `ConsoleRenderer` (its `TargetChanged`
subscription) both already read `m_state.combat()` / listened for
`TargetChanged` on `Application::m_gameState`'s bus — `git diff fable --
src/client/action/command_processor.cpp src/client/output/console_renderer.cpp`
shows zero changes to either file from this unit. The diagnosis holds:
this was a feed problem, not a consumer problem.

### The poll and its dedupe members are gone

`Application::syncCombatState()`, `Application::syncGameStateFromClient()`
(its only remaining caller, itself reduced to nothing once
`syncCombatState()` was removed), and the three members
`m_lastCombatTargetId`/`m_lastCombatTargetHp`/`m_lastAutoAttacking` are
all **deleted**, not stubbed — confirmed by `grep -rn
"syncCombatState\|syncGameStateFromClient\|m_lastCombatTargetId\|
m_lastCombatTargetHp\|m_lastAutoAttacking" src include tests`: the only
remaining hits are comments (historical/provenance notes, several
deliberately updated by this unit to stop citing the deleted functions
by name) and this plan/Review text itself. **No poll and no events
coexist for combat** — the STOP condition never triggered.

### Two writers to one field, resolved honestly: a real, documented double-publish

Moving the publish into `CombatManager`'s own choke points means the
pre-existing `TargetIntent`-handling code in `eq_bridge.cpp` (which
already manually pushes `TargetChanged` for the mouse-click path) and
`OnSpawnRemovedGraphics()` (which already pushes a manual clear on
despawn) now **also** get a second, identical `TargetChanged` from
inside `CombatManager::SetTarget()`/`ClearTarget()` for those same two
call paths. This is the same shape T81 found and accepted for entity
spawn/despawn: harmless (the second publish carries identical values;
`CombatState::setTarget()`/`clearTarget()` only re-fire their own
downstream event on an actual value transition, so no cascading
duplicate console output), not fixed (removing the now-redundant manual
pushes would mean touching `eq_bridge.cpp`, outside this unit's
territory), documented in `game_state_sync_bridge.h`'s docstring and
here for whoever next touches that file.

### Live evidence — full detail in `review/T104/README.md`, headline here

Session: lock taken/released cleanly (`T104-combat-poll-to-bridge
2026-08-14T12:20:16Z` through `down.sh` completing, lock file deleted,
confirmed absent), `willeq_test`/`Testchar`, `-d 4 --log-level=DEBUG`,
piped stdin (no keyboard), daylight forced early each round. Five rounds
run, four of them real findings kept in the record (not discarded):

1. **Round 1** — targeted the bare substring `wolf_pup`, which silently
   matched a **stale, unrelated leftover mob** (a prior session's
   never-cleaned-up spawn) instead of this run's own fresh spawn. Fixed
   by extracting and targeting the exact, unique spawn name.
2. **Round 2** — correct target, but a plain `/attack` produced **zero**
   `ZoneProcessDamage` packets in 30s.
3. **Round 3** — added `/pathfinding on` + `/hunt on` (matching T32's
   exact prior recipe) — regressed: auto-hunt's background target scan
   raced ahead of the `sleep 1` between `/hunt on` and `/attack`, leaving
   `HasTarget()` false when `/attack` ran (its `EnableAutoAttack()` guard
   silently no-op'd). Dropped `/hunt` — not needed for a manual
   `/target`+`/attack` test.
4. **Round 4** — a `grep -c ... || echo 0` shell bug (grep already prints
   `0` on no match *and* exits 1, so `|| echo 0` doubled the value to a
   two-line `"0\n0"`, breaking a string-equality check) silently skipped
   the GM-command fallback. Fixed.
5. **Round 5 — clean, full evidence.** Root cause of rounds 2-3 found via
   DB read: Testchar's inventory (`character_id=65`) has **zero items in
   any equippable slot** — real melee genuinely cannot land a hit,
   independent of anything this unit changed (an environmental gap, out
   of this unit's scope to fix). Substituted the sanctioned GM command
   `#set hp <amount>` — a real server round trip over the identical wire
   path (`OP_MobHealth` -> `ZoneProcessMobHealth()` -> `TargetChanged`
   re-publish) a real melee tick would use. Result: `/target` ->
   `TargetChanged` (hp=100%) -> `/combat on` + `/attack` ->
   `AutoAttackChanged(true)` -> **four** real, server-driven HP steps
   (100%->72%->36%->0%), each producing a fresh, correctly-valued
   `TargetChanged` publish -> at 0%, `ProcessCombatRound()`'s pre-existing
   `hp_percent<=0` branch fires `ClearTarget()` + `DisableAutoAttack()`
   automatically, producing both `TargetChanged(clear) -> projection
   cleared` and `AutoAttackChanged(false)` with **no script action** ->
   `/target Testchar` (retarget-self, a second identity-change proof) ->
   final `/who` liveness probe. Client exited cleanly on `/quit`
   (`ps aux | grep willeq` empty afterward, no PID kill needed).

Testchar's DB position, checked before and after: `zone_id=36,
x=-77.875, y=21.875, z=3, heading=128` — **unchanged**, confirmed by
`SELECT` after the client's own clean exit; nothing in this session moved
the character, so no restore was needed.

### Field-loss / gap found, not fixed

**No slash command reaches `EqActionHandler::clearTarget()`.**
`ActionDispatcher::clearTarget()`'s only production caller is
`InputActionBridge` (a keybinding) — confirmed by grep. This predates
this unit and is not a projection gap (the mechanism this unit built
works fine when `ClearTarget()` is called, as the live kill's automatic
clear proves) — it's a command-surface gap in a different layer. The
plan's "clear the target" live-leg bar was still met, just via the
target's death triggering `ClearTarget()` automatically rather than an
explicit clear command, which doesn't exist. Flagged in
`refactor/deferred.md`, not fixed (out of territory).

### Tests, and proof they can fail in both directions

12 new tests: 6 in `tests/test_game_state_sync_bridge.cpp`
(`TargetChangedSetsCombatTarget`, `TargetChangedWithZeroSpawnIdClearsTarget`,
`TargetChangedForSameTargetUpdatesHP` — the correlation risk, directly —
`AutoAttackChangedSetsFlag`, `AutoAttackChangedClearsFlag`,
`CombatEventsStillDelegateToWrappedBridge`), 6 in
`tests/test_combat_manager.cpp` (`SetTargetPublishesTargetChangedWithEntityData`,
`ClearTargetPublishesTargetChangedWithZeroSpawnId`,
`SetTargetMissDoesNotPublish`, `EnableAutoAttackPublishesAutoAttackChangedTrue`,
`EnableWithoutTargetDoesNotPublish`, `DisableAutoAttackPublishesAutoAttackChangedFalse`
— using the pre-existing `zonetest::TestBridge` fake, a real `EverQuest`
with `setBridge()`, and `drainEvents()` to assert). Two independent
both-directions proofs:

1. `CombatManager::PublishTargetChanged()` — inserted an unconditional
   early `return;` after the null-bridge guard. Reran
   `SetTargetPublishesTargetChangedWithEntityData`: **failed**
   (`events.size()` 0, expected 1). Restored (`diff` confirmed
   byte-identical), rebuilt, reran: **passed**.
2. `GameStateSyncBridge::applyEvent()`'s `TargetChanged` case — swapped
   the `setTarget()`/`clearTarget()` branches. Reran all three
   `TargetChanged*` tests: **all three failed** (wrong branch taken).
   Restored (`diff` confirmed byte-identical), rebuilt, reran: **all
   passed**.

### Deviations

1. **`tests/CMakeLists.txt`** — one line (`TARGET_LINK_LIBRARIES(
   test_game_state_sync_bridge GTest::gtest_main fmt::fmt)`), outside the
   plan's literal territory list. Required: adding `LOG_DEBUG` calls to
   `game_state_sync_bridge.cpp` (for live-session observability of the
   correlation risk, since `ConsoleRenderer`'s target-line print is
   gated on an actual identity change and would not otherwise reprint on
   every same-target HP update) needs `fmt::vformat`, which this test
   target didn't link before (it had no `fmt`-using code path in the one
   `.cpp` it compiles directly). Minimal, mechanical, no behavior change
   beyond making the existing `LOG_DEBUG` macro link.
2. **`src/client/state/combat_state.cpp` and
   `tests/test_state_modules_secondary.cpp`** — comment-only updates
   (not in the plan's literal territory list either) to stop citing
   `Application::syncCombatState()` by name now that it's deleted;
   zero behavior change.
3. **Comments in `src/client/action/command_processor.cpp`/`.h` and
   `src/client/output/console_renderer.h`/`.cpp` still cite
   `Application::syncCombatState()` by name** — left untouched
   deliberately: those files are outside this unit's territory, and
   `command_processor.cpp` specifically is likely to be touched by T103
   (Trade, running in parallel) in the same "Combat above" comparison
   paragraph — editing it risked a parallel-edit collision for a
   comment-only change. Flagged here rather than silently left for
   someone to trip over.
4. No change to `CombatManager`'s own decision logic — every addition is
   a publish call at an existing choke point, verified by `git diff
   fable -- src/client/combat.cpp` showing only additive `Publish*()`
   calls and their bodies, no line inside any pre-existing method's
   control flow changed.
5. No other deviations from the contract.

### Verification summary

1. `cmake --build build -j24` — clean (multiple rounds, including after
   the `fmt::fmt` link fix and both both-directions-proof round trips).
2. `ctest --test-dir build --output-on-failure` — **2228/2229**, sole
   failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   2229 = 2217 baseline + 12 new tests, exact match.
3. `bash scripts/build-gles2-x86.sh` — clean.
4. Visual gate (`visual_gate` ctest case) — PASS, ~21-28s across reruns;
   `git status` shows no golden touched.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile, `willeq` ELF
   produced and reconfirmed rebuilt (mtime-checked) after the final
   `game_state_sync_bridge.cpp` edit.
6. `scripts/refactor/check_file_sizes.sh fable` — 9 ok, 2 WARN
   (`src/client/application.cpp`, pre-existing over-1500 and **smaller**
   than `fable`'s copy — 2275 vs 2337 lines, net code removal;
   `src/client/combat.cpp`, pre-existing over-1500, grew by 78 lines),
   0 FAIL.
7. Live eqemu leg — lock taken/released cleanly, `down.sh` clean, lock
   file deleted (confirmed absent), Testchar's DB position confirmed
   unchanged after the client's own clean exit. Full evidence:
   `review/T104/README.md`.

## Board

This closes the last category of T78's Option B source→projection
migration apart from doors: entities (T81), group (T99), and now combat
are all migrated off polls onto typed bridge events. Doors remain the
sole open category — a third, unconnected `DoorStateManager` holds the
only real door data (T78's finding, unchanged) — its own unit, its own
reconstructibility argument, separately blocked on routing. Recorded in
`refactor/deferred.md`'s "TWO `state::GameState` instances exist,
unconnected" entry.
