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

(executor fills: the reconstructibility verification; the event vocabulary
chosen and anything added; how the volume split was handled; the
double-publish answer; which consumers now work, with evidence; what is
deferred; deviations)

Board: update the T78 entry — record which category this closed and which
remain (doors, group, and the still-polled combat). The migration's
progress should be readable at a glance rather than re-derived.
