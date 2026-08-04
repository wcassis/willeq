# T40 — Feed pet state from the bridge, delete `syncPetState()` (first cut of the Application→EverQuest decoupling)

**Slot: t1. x86 only. No Pi. eqemu only if a live pet leg proves
obtainable (see Verification).** No sibling unit.

**Territory: `src/client/application.cpp` (the sync + wiring),
`include/client/application.h`, a new bridge decorator under
`include/client/bridge/` + `src/client/bridge/`, the pet-event publish
site in `src/client/eq_entities.cpp`, and tests.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred.md entry **"TWO
`state::GameState` instances exist, unconnected"**, then this plan's
"Why" section — the *point* of this unit is the pattern, not the pet.

## Why this unit exists

Will's standing goal: game state separated from rendering, with message
routing between the sections rather than direct coupling that breaks the
intended isolation.

The project enforced that in **one** direction only —
`EverQuest` may not touch the renderer ("no `m_renderer` member, no
`GetRenderer()` accessor", CLAUDE.md). The **opposite** direction never
got the same discipline: `Application` holds `m_eqClient` and reaches
into it through ~20 distinct accessors across ~60 call sites, several of
which hand back another subsystem's manager object
(`GetCombatManager()`, `GetInventoryManager()`, `GetSpellManager()`).

The `sync*State()` family is the worst of it: it **re-derives by polling,
once per tick, information the `GameStateBridge` already carries as typed
events**. That is duplicate logic in the strict sense, and it is why
`Application::m_gameState` and `EverQuest::m_game_state` drifted into two
unconnected instances in the first place.

**The target invariant** (the thing this arc is buying, stated so it can
be checked later): *`Application` needs `m_eqClient` for lifecycle and
commands — `ConnectToLogin`, `GameTick`, `DispatchSlashCommand`,
`CreateZoneLoadSnapshot`, `InitGraphics` — and never for reading game
data.* This unit does not achieve that; it establishes the mechanism and
proves it on one sync.

**Pet is the first cut because the gap is smallest and measurable**:
three of the four pet events are already published
(`PetCreated`, `PetRemoved`, `PetButtonStateChanged` — all in
`eq_bridge.cpp`), so most of the work is deletion.

## The seam — read this before designing

The obvious idea ("add a third bridge consumer alongside `IrrlichtBridge`
and `ConsoleBridge`") **does not work**, and knowing why saves you a
rewrite: `GameStateBridge` is an abstract base with a **single,
destructively drained queue** (`drainEvents()`), and exactly one concrete
instance exists per run — `IrrlichtBridge` in graphics mode,
`ConsoleBridge` in headless. A second consumer would see nothing, because
the first drain empties the queue.

The natural fit is a **decorator**: a small bridge that holds a
`state::GameState&` and wraps the mode's bridge — its `applyEvent()`
updates the state, then delegates to the wrapped bridge. `Application`
constructs it around whichever mode bridge it already builds and hands
that to `EverQuest` as `m_bridge`. No change to `drainEvents()`, no
change to `EverQuest::ProcessBridgeEvents()`, no double-drain.

If you find a cleaner seam, take it — but state why, and do not
introduce a second drain of the same queue.

**Thread question you must answer before writing state:**
`EverQuest::ProcessBridgeEvents()` is driven from
`Application::updateGameState()` (`application.cpp:1264`) and the
comments in `eq_zone.cpp:286` / `application.cpp:1427` also reference
`PreRenderTick()` driving it. Establish **which thread(s) `applyEvent()`
actually runs on**, and confirm the state you write is read on the same
thread (`CommandProcessor`, `InputActionBridge`, `ConsoleRenderer`). If
it can run on two threads, STOP and report — the polling it replaces ran
on exactly one, so this would be a new hazard rather than a cleanup.

## Scope

### 1. The decorator + pet event application

Apply the four pet events into `Application::m_gameState`'s `pet()`:
`PetCreated` → `setPet(spawnId, name, level)`; `PetRemoved` →
`clearPet()`; `PetStatsChanged` → `updatePetStats(hp, mana)`;
`PetButtonStateChanged` → `setButtonState(button, state)`.

Keep the decorator's switch **narrow** — pet events only. Every event you
do not handle delegates untouched. Later syncs extend it; this unit does
not pre-build for them.

### 2. Publish the missing event

`PetStatsChanged` is **defined but never published** — the one genuine
gap. Publish it where the pet's HP actually changes: `eq_entities.cpp`
already pushes `TargetChanged` from the entity-HP-update site when the
changed entity is the current target (~line 1728). The pet case is the
same shape, one `if` alongside it.

**Carry real mana.** `Entity` has `cur_mana`/`max_mana`, and
`PetStatsChangedData` has a `manaPercent` field — whereas the poll being
deleted openly fakes it ("`GetPetManaPercent` doesn't exist, we'd need to
track separately") and passes a `m_lastPetManaPercent` that nothing ever
updates. If the pet's mana genuinely is not populated on the entity in
practice, say so plainly rather than shipping a confident wrong number.

### 3. Delete the poll

Remove `syncPetState()`, its `m_lastPetSpawnId` / `m_lastPetHpPercent` /
`m_lastPetManaPercent` members, and its call from
`syncGameStateFromClient()`. Remove the now-unused
`m_eqClient->GetPet*()` reads from `Application`.

If those `EverQuest::GetPet*()` accessors end up with **no** remaining
callers, say so in the Review — deleting them is the visible proof the
coupling is gone — but check for other callers first and do not delete
anything still in use.

### 4. Leave the other syncs alone

`syncCombatState`, `syncInventoryState`, `syncNPCInteractionState`,
`syncSpellState` stay exactly as they are. Their turn comes only if this
pattern proves out. Report anything you notice about them; change
nothing.

## STOP conditions

- `applyEvent()` can run on more than one thread → STOP (see above).
- Pet state turns out **not** to be fully reconstructible from the four
  events — e.g. an event fires at the wrong time, or carries too little,
  so the state would be wrong where the poll was right → STOP and report
  exactly which case. **This is the most valuable possible finding of
  this unit**: it tells us whether the whole `sync*` sweep is viable or
  whether the events need work first. Do not paper over it by keeping a
  partial poll.
- The decorator would require changing `GameStateBridge`'s queue or
  drain semantics → STOP.
- Any behaviour change beyond where pet state comes from → STOP.

## Verification bar

1. `cmake --build <slot>/build -j24` clean, **and** the headless
   configure (`EQT_GRAPHICS=OFF`) — the two modes use different concrete
   bridges, so both wrappings must work.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35)
   and the golden replay no longer skips (T36); a failure or `Skipped` on
   either is a real signal.
3. Golden replay unchanged and confirmed to have RUN.
4. **Unit tests on the decorator** — feed synthetic `PetCreated` /
   `PetStatsChanged` / `PetRemoved` / `PetButtonStateChanged` events and
   assert the resulting `GameState::pet()` values, plus that unhandled
   events delegate untouched. This is the deterministic core of the
   verification and does not need a server.
5. **A live pet leg if one is obtainable** — a GM-summoned or
   class-summoned pet in a live session, showing pet state arriving via
   events with the poll deleted. If you cannot obtain a pet with
   reasonable effort, **say so plainly and rely on item 4** rather than
   inventing a fixture that does not exercise the real path. An honest
   "could not test live, here is why" is acceptable; a fake live leg is
   not.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

**Bottom line: pet state proved fully reconstructible from the four
bridge events.** No case was found where an event fired at the wrong
time or carried too little to reproduce what `syncPetState()` polled.
`syncPetState()` and its three tracking members were deleted outright,
with no partial poll left behind. This is a real, if single-sample,
signal that the rest of the `sync*` sweep is viable — subject to each
remaining sync getting the same event-coverage audit `PetStatsChanged`
needed here.

### The seam

Used the decorator exactly as scoped: `eqt::bridge::GameStateSyncBridge`
(`include/client/bridge/game_state_sync_bridge.h`,
`src/client/bridge/game_state_sync_bridge.cpp`) wraps a
`GameStateBridge&` and holds a `state::GameState&`. `applyEvent()`
switches narrowly on the four pet event types, mutates
`m_state.pet()`, then unconditionally delegates to the wrapped bridge's
`applyEvent()` — every other event type falls through the `default:`
case untouched.

One thing not spelled out in the plan that mattered for correctness:
`GameStateBridge`'s `pushEvent`/`drainEvents`/`pushIntent`/`drainIntents`
are non-virtual and operate on **each instance's own private queues**.
Only `applyEvent()` is virtual. So the decorator is not just an
`applyEvent()` filter sitting beside the real bridge — it has to *be*
the single object that `EverQuest` pushes events onto **and** that the
renderer pushes intents onto **and** that `ProcessBridgeEvents()`
drains, or events/intents pushed onto one object's queue would never be
drained by another's `drainEvents()`/`drainIntents()`. Concretely, in
`Application::initialize()` (`src/client/application.cpp`) both wiring
sites now do:
```cpp
m_irrlichtBridge = std::make_unique<eqt::bridge::IrrlichtBridge>();
m_irrlichtBridge->setRenderer(m_renderer.get());
m_stateSyncBridge = std::make_unique<eqt::bridge::GameStateSyncBridge>(
    *m_irrlichtBridge, *m_gameState);
m_renderer->setBridge(m_stateSyncBridge.get());   // was: m_irrlichtBridge.get()
m_bridge = m_stateSyncBridge.get();               // was: m_irrlichtBridge.get()
```
and symmetrically for `ConsoleBridge` in the headless path. `m_bridge`,
the renderer's bridge pointer, and what's passed to
`EverQuest::InitGraphics()`/`setBridge()` are now all the *same*
decorator instance, so there is exactly one event queue and one intent
queue per run, same as before — `drainEvents()`/`applyEvent()` in
`EverQuest::ProcessBridgeEvents()` and `GameStateBridge`'s drain
semantics are untouched. Teardown drops `m_stateSyncBridge` before
`m_irrlichtBridge` (it holds a reference to it) to avoid leaving a
dangling reference sitting around.

### The thread answer

`applyEvent()` runs on exactly one thread per run, and it's the *same*
thread that reads the state it writes — but which thread that is
differs by mode, and working that out took more than the plan's
one-line framing suggested:

- **Headless** (`ConsoleBridge`): `EverQuest::ProcessBridgeEvents()` is
  called directly from `Application::updateGameState()`
  (`application.cpp`, gated `!m_graphicsInitialized`), which itself is
  called from `processInput()`+`updateGameState()` inside
  `gameThreadLoop()` — **game thread**. Readers
  (`InputActionBridge::update()` → `CommandProcessor::processCommand()`
  → `ActionDispatcher::petAttack()`/etc., which check
  `m_state.pet().hasPet()`) are driven from the same `processInput()`
  call in the same loop — **game thread**. Match.
- **Graphics**: `ProcessBridgeEvents()` only runs via
  `EverQuest::PreRenderTick()`, called from `Application::render()`
  (`m_eqClient->PreRenderTick(gfxDeltaTime)`), which only ever runs from
  `mainLoop()`'s `render(0.0f)` call — **render/main thread**. The
  question was then whether any pet-state *reader* runs on the game
  thread instead. Traced it down: `GraphicsInputHandler::consumeRawCommand()`
  always returns `std::nullopt` (`include/client/input/graphics_input_handler.h`),
  so `InputActionBridge`'s "legacy fallback" raw-command path into
  `CommandProcessor` (the one thing that would put pet-command reads on
  the *game* thread) is dead in graphics mode — `ConsoleInputHandler` is
  the only handler that ever produces raw commands, and it's gated to
  headless-only (`application.cpp`, comment at the `processInput()`
  gating block). The only live entry points into pet-reading code in
  graphics mode are `GraphicalRenderer` callbacks
  (`setChatSubmitCallback`, `setPetCommandCallback`,
  `connectRendererCallbacks()`), all of which fire synchronously inside
  `m_renderer->processFrame()` — called from `render()` — **render
  thread**. Match, but not a trivial one: the two readers
  (`InputActionBridge`'s raw-command path and the renderer callbacks)
  are on *different* threads in general, and it's specifically because
  the raw-command path is unreachable in graphics mode that this comes
  out safe. If a future unit ever makes `GraphicsInputHandler` produce
  raw commands (or adds any other game-thread caller into
  `ActionDispatcher`'s pet methods while graphics is active), that
  would reopen exactly the hazard this section ruled out — worth a
  one-line comment if anyone touches that path later, not done here to
  keep the diff to what T40 needs.

No STOP triggered: writer and reader are always the same thread within
a given run; they're just a different thread depending on mode, and
that was true before this change too (every other `sync*` writer also
only ran on the game thread; the difference here is graphics-mode pet
state now writes from the render thread instead — but nothing reads it
from the game thread in graphics mode, so no new race is introduced).

### `PetStatsChanged` publish site and mana

Added to `src/client/eq_entities.cpp`, in `ZoneProcessMobHealth()`
(the "line 1728" site the plan named), alongside the existing
`TargetChanged` push, guarded by `m_pet_spawn_id == spawn_id` (the same
shape as the existing `m_current_target_id == spawn_id` check):
```cpp
if (m_pet_spawn_id == spawn_id) {
    eqt::state::PetStatsChangedData pdata;
    pdata.spawnId = spawn_id;
    pdata.hpPercent = hp_percent;
    pdata.manaPercent = (it->second.max_mana > 0)
        ? static_cast<uint8_t>((it->second.cur_mana * 100) / it->second.max_mana)
        : 0;
    m_bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::PetStatsChanged, std::move(pdata)));
}
```
**Mana is not real, and this is by design, not an oversight.**
`Entity::cur_mana`/`max_mana` (`include/client/eq.h`) are populated at
exactly two sites (`src/client/eq_player_info.cpp` inside the
`ManaChange` packet handler, gated `if (m_my_spawn_id != 0)`, and
`src/client/eq.cpp`'s explicitly-named `self_entity.cur_mana = ...`) —
both player-only. `ManaChange` is a player-only server packet; nothing
in the EQ protocol or this client ever populates mana on a pet's
`Entity` record. So `manaPercent` in the published event is honest —
computed from the entity, not fabricated — but it will read `0` for
every pet, always, until something starts tracking pet mana
server-side-forward. This replaces the deleted poll's dishonesty
(`GetPetHpPercent()` was real, but `m_lastPetManaPercent` was reset to
100 on pet creation and never touched again, so the old UI showed a pet
permanently at "100% mana" regardless of reality) with an honest zero.
Flagging this loudly per the plan's instruction, not papering over it:
**pet mana is not live data anywhere in this client**, before or after
this unit.

### What was deleted

- `Application::syncPetState()` (full body, ~45 lines,
  `src/client/application.cpp`) and its declaration
  (`include/client/application.h`).
- `m_lastPetSpawnId`/`m_lastPetHpPercent`/`m_lastPetManaPercent`
  (`include/client/application.h`) — confirmed via repo-wide grep to
  have no readers/writers outside the deleted function.
- The `syncPetState();` call in `syncGameStateFromClient()`.

### `GetPet*()` accessors: two went dead, four didn't

Repo-wide grep before touching anything: `GetPetHpPercent()` and
`GetPetButtonState()` had exactly one call site each, both inside
`syncPetState()` — deleted along with it
(`include/client/eq.h`, `src/client/eq_group.cpp`). This is the visible
proof the plan asked for: two fewer ways `Application` can reach into
`EverQuest` for game data.

`GetPetSpawnId()`, `GetPetName()`, `GetPetLevel()`, `GetPetEntity()`
stay — they have live callers outside the deleted poll:
`GetPetSpawnId()` from `eq_action_handler.cpp` (`targetSelf()`
toggling target between self/pet) and `spell/spell_manager.cpp`
(`adjustTargetForSpellType`, resolving `SpellTargetType::Pet`);
`GetPetName()`/`GetPetLevel()` from `eq.cpp`'s state-snapshot publish
(builds `PetCreatedData` — a different call site than the one this unit
touched); `GetPetEntity()` is used internally by the others. None of
these are `Application` reaching into `EverQuest` for display state —
they're `EverQuest` reading its own data for its own logic — so they're
out of scope for the target invariant this arc is building toward, not
an oversight.

### Unit tests

`tests/test_game_state_sync_bridge.cpp`, registered in
`tests/CMakeLists.txt` (links `ACTION_LAYER_STATE_SOURCES` plus the new
`.cpp`, no Irrlicht, no EverQuest — matches the existing
`test_input_action_bridge`/`test_command_processor` pattern). A local
`RecordingBridge : GameStateBridge` fake stands in for
`IrrlichtBridge`/`ConsoleBridge`. Six tests, all passing:
`PetCreatedSetsPetState`, `PetStatsChangedUpdatesHpAndMana`,
`PetRemovedClearsPetState`, `PetButtonStateChangedTogglesButton`,
`HandledEventsStillDelegateToWrappedBridge` (asserts handled events
still reach the wrapped bridge, not just state), and
`UnhandledEventDelegatesUntouchedAndLeavesPetStateAlone` (feeds an
unrelated `PlayerMoved` event, asserts pet state is unchanged *and* the
event still reached the wrapped bridge).

### Live leg

Obtained a real pet against the live eqemu server (lock taken/released
per protocol; Testchar's position was unmoved by the test — verified
and re-set to the canonical restore values as a safety net anyway).
`Testbuddy` (the seeded magician on `willeq_buddy`) turned out to be
level 1 with no pet spell castable yet, so used the GM route instead
(Testchar is GM-status 200): `#makepet FamiliarFire` (a real eqemu GM
command, discovered via `strings` on the zone binary after `#castspell`
turned out not to exist as a command — `#makepet <type>` looks up the
`pets` DB table by its `type` column). Ran a scripted headless session
(`--script`, same pattern as `scripts/eqemu/record_tour.sh`) with debug
logging on, and observed, with `syncPetState()` fully deleted and pet
state coming *only* from the new decorator:

- `#makepet FamiliarFire` → `Pet detected: ... (ID: 178) ... owned by
  us` and `Pet window: Pet created - ...` (the existing `OnPetCreated`
  debug log, confirming the bridge event fired).
- `/pet follow` and `/pet guard` (before summoning: `"You don't have a
  pet"`; after: `"Commanding pet to follow."` /
  `"Commanding pet to guard here."`) — this is
  `ActionDispatcher::petFollow()`/`petGuard()`'s `hasPet()` guard
  passing, sourced from `Application::m_gameState`, which nothing but
  the new decorator populates anymore.
  `/pet attack` with no target: `"You must specify a target or have one
  selected"` — past the `hasPet()` check, failing on the *next* check,
  further confirming `hasPet()` was true.
  `/target a_rat` then `/pet attack`: `"Commanding pet to attack."`,
  `"Sent pet command: attack (target=134)"`.
- A live `MobHealth` packet for the pet's own spawn id arrived mid-fight
  and was logged via `[HP] Entity 178 (Testchar's_familiar000) health:
  100% -> 100%` — confirming the new `m_pet_spawn_id == spawn_id`
  branch in `ZoneProcessMobHealth()` fired for a real server packet
  (value happened to be unchanged, so this run didn't exercise an
  actual HP delta, but the code path — including the correct entity
  match — ran live).
- `/camp` in an earlier run produced `"Pet dismissed."`, confirming
  `PetRemoved` also round-trips correctly on pet loss.

Did not additionally chase a live HP-*delta* on the pet (would need a
longer/more damaging fight) or a live mana delta (established above
that mana is never populated regardless of how long you wait) — the
unit tests already cover both numerically, and the live leg's job was
to prove the real event-publish sites fire for a real pet, which they
did.

### Verification bar

1. `cmake --build build -j24` (graphics, `EQT_GRAPHICS=ON`) — clean,
   `willeq` linked, `test_game_state_sync_bridge` built.
2. `cmake --build build-nogfx -j24` (`EQT_GRAPHICS=OFF`) — clean,
   `willeq` linked, `test_game_state_sync_bridge` built.
3. `ctest --test-dir build --output-on-failure` — 1829/1830 passed;
   the one failure is the documented `AudioManagerTest.PreloadCommonSounds`
   ALSA host exception. RDP suite (61 tests, including
   `RDPServerFrameTest.*`) 100% pass. Golden replay
   (`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`) ran
   and passed, not skipped.
4. `test_game_state_sync_bridge` — 6/6 pass (see above).
5. Live pet leg — obtained and exercised, see above.
6. `scripts/refactor/check_file_sizes.sh --files <all touched paths>` —
   0 FAIL, 4 pre-existing WARN (`application.cpp`, `eq.h`,
   `eq_entities.cpp`, `CMakeLists.txt`, all already over threshold
   before this unit; this unit *shrank* two of them). New files
   (`game_state_sync_bridge.h`/`.cpp`, the test) are small and clean.

Territory note: deleting the two dead `GetPet*()` accessors touched
`include/client/eq.h`/`src/client/eq_group.cpp`, which aren't in the
plan's listed territory (`application.cpp`/`.h`, the new bridge files,
the `eq_entities.cpp` publish site, tests). Flagging as a deliberate,
narrow deviation: the plan explicitly asked to check whether these
accessors went dead and "say so... deleting them is the visible proof
the coupling is gone", so I took that as licensing the deletion once
confirmed dead via grep, rather than just reporting it and leaving dead
code behind.
