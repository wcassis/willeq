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

(Executor fills: the seam chosen and why, the thread answer, the
`PetStatsChanged` publish site and whether mana is real, what was
deleted, whether the `GetPet*()` accessors became dead, the unit tests,
the live leg or an honest account of why not, and — most importantly —
whether pet state proved fully reconstructible from events, since that
determines whether the rest of the `sync*` sweep is viable.)
