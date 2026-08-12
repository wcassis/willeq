# T78 — Two `state::GameState` instances: produce the analysis that makes the decision possible

**Slot: t2. x86 only. No Pi. NO EQEMU.**

**This is an ANALYSIS unit. Production code is READ-ONLY.** Territory is
`refactor/` and a throwaway analysis script under a scratch path. You will
change no behaviour. If you find yourself editing `src/`, you have
misread the unit.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/deferred.md`'s entry
**"TWO `state::GameState` instances exist, unconnected"**, then
`refactor/T55_position_state_propagation.md` and
`refactor/T58_playerprofile_propagation.md`'s Reviews — T58's Step 1 trace
is where the consequence was first proven.

## Why this unit exists

The board has carried this for months as "needs a decision, not a
drive-by", and it is now the highest-leverage open item — but nobody can
decide it, because nobody has laid out what the decision actually costs.

The facts established so far, which you should verify rather than assume:

- `EverQuest::m_game_state` (`include/client/eq.h:1210`) is a **value
  member**.
- `Application::m_gameState` (`include/client/application.h:437`) is a
  **separate `unique_ptr`**, constructed at `application.cpp:180`.
- **Both production `subscribe()` sites attach to Application's**:
  `console_renderer.cpp` and `automated_mode.cpp`. Nothing subscribes to
  EverQuest's.
- Consequence, proven by T58: `SetPosition`/`SetHeading` fire `PlayerMoved`
  on EverQuest's private EventBus, **which has no production listener**.
  T55 and T58 made the state-layer position writes correct and they still
  have no reader.
- `application.cpp:1793-1797` already carries a comment acknowledging the
  split.

## What to produce

A written analysis, in the plan's Review, that a decision can be made
from. Specifically:

**1. A complete census of both instances.** For each: who constructs it,
who holds references, every reader and every writer. Distinguish
*production* paths from tests and tools. This is the part nobody has
done, and it is most of the unit's value.

**2. What each instance is actually used for today.** They are not
symmetric — EverQuest's appears to back a handful of "Phase 7.x" getters,
while Application's backs the mode/dispatcher/command layers. Say what
each genuinely serves.

**3. The options, with costs.** At minimum:
   - **Converge on one instance** — which one, what has to move, and what
     breaks. Note that `EverQuest` is deliberately graphics-free and
     communicates only via the bridge (`CLAUDE.md`'s "Key separation");
     any convergence must not violate that.
   - **Connect them** — forward events from one bus to the other, or share
     the bus. Cheaper, but say honestly whether it creates two sources of
     truth rather than resolving them.
   - **Delete one** — if one is genuinely vestigial, that is the cleanest
     outcome. Establish whether it is.
   - **Leave them split deliberately**, documented, with the state layer's
     unread writes accepted as cost. This is a legitimate answer and must
     be presented fairly, not as a straw man.

For each: what changes, roughly how much, what regression risk, and what
it unblocks.

**4. What is currently blocked by the split.** T55 and T58's writes are
the known case. Find the others — code written against the state layer
that silently does nothing today. That list is the real argument for
acting, and it may be longer than anyone expects.

## Discipline

**Do not recommend by assertion.** Every claim needs a file:line. This
item has survived months precisely because it has been easier to restate
than to investigate.

**Do not pick for Will.** Lay the options out with costs and say which you
would choose and why — but the decision is his, and a unit that quietly
forecloses options by framing is worse than one that presents them
plainly.

If the census turns up something that changes the shape of the question —
for instance that one instance has no production readers at all — **say so
prominently**. That would make this a much smaller decision than it looks.

## STOP conditions

- Any edit to `src/` or `include/`.
- Any urge to prototype a fix "to see if it works".

## Verification

Build and test only to prove you changed nothing (`git status` should show
only `refactor/`). The deliverable is the analysis.

## Review

All line numbers verified against this unit's checkout (`task/T78-two-gamestate-analysis`
off fable `735f4914`), not assumed from prior units. Two small drifts from the
plan's own citations, noted so nobody chases a stale line: `EverQuest::m_game_state`
is declared at `eq.h:1214` (plan said `:1210`); `Application::m_gameState` is
declared at `application.h:458` (plan said `:437`) and constructed at
`application.cpp:196` (plan/T58 said `:180`). Same members, same semantics — just
a few lines of drift from code added since those citations were written. Nothing
below depends on the old numbers.

### THE HEADLINE — this changes the shape of the question

**Both instances have live production readers and live production writers.
Neither is vestigial.** That rules out "delete one" as a clean option (Option C
below explains why). But the picture is worse than "two populated copies of the
same data": **most of what `Application::m_gameState` is read for is never
written into it at all.** `entities()`, `doors()`, `group()`, and player
`position`/`heading` are all read by production code through
`Application::m_gameState` and are **never populated** there by anything — not
by `GameStateSyncBridge`, not by `Application::syncCombatState()`, not by
anything else. The result is a list of commands and input paths that are
silently broken in production **today**, independent of and larger than the
T55/T58 position-write finding the plan named as "the known case." See
"What's blocked" below — it is longer than T55/T58 alone, exactly as the plan
warned it might be.

### Census 1 — `EverQuest::m_game_state` (value member, `eq.h:1214`)

**Construction**: default-constructed as part of `EverQuest`'s own
construction (`m_eqClient = std::make_unique<EverQuest>(...)`,
`application.cpp:203`) — no explicit init, it just exists whenever an
`EverQuest` exists. Owns its own `state::EventBus` value member
(`game_state.h:161`, confirmed: "The GameState owns all state objects and the
EventBus").

**Who holds a reference**: only `EverQuest` itself (private member); exposed
via `EverQuest::GetGameState()` (`eq.h:1073-1074`, non-const + const
overloads).

**Writers** (all inside `EverQuest`'s own domain TUs, i.e. `this->m_game_state`
from methods of the class that owns it):
- Player: position/heading/speed/moving (`eq.cpp:3120,3127,3136,3143`),
  HP (`eq_entities.cpp:1797`), mana/endurance (`eq_player_info.cpp:493,515`),
  currency (`eq_merchant.cpp:972,1064`), level (`eq.cpp:3545`), bind point
  (`eq.cpp:2303`), AFK/anon/roleplay/camping/sneak (`eq_player_info.cpp:787,
  802,817,833,850,870`, `eq.cpp:3810`), weight (`eq_bridge.cpp:1488`),
  spawnId (`eq_entities.cpp:372,803,1168,1443`), bank currency
  (`eq_player_info.cpp:1588-1591`), vendor/loot corpse
  (`eq_bridge.cpp:1533,1658`, `eq_entities.cpp:1641`), practice points
  (`eq.cpp:1195`).
- Entities: `addEntity`/`getEntityMutable`/`removeEntity`/`clear`
  (`eq_player_info.cpp:308-321` via `SyncEntityToGameState()`/
  `RemoveEntityFromGameState()`; called from `eq_entities.cpp:439,1211,1695`
  spawn/despawn handling, `eq.cpp:2378` self-entity, `eq_zone.cpp:270` zone
  clear).
- Combat (target name/stop-distance/movement flags, not `CombatState`):
  `eq_movement.cpp:245-248,283-285,305`, `eq_zone.cpp:264-265`,
  `eq.cpp:3163,3315`.
- Group: `setMember`/`clearGroup`/`setPendingInvite`/`setInGroup`/
  `setLeaderName`/`setIsLeader`/`clearPendingInvite`/`recalculateMemberCount`
  — all in `eq_group.cpp` (297,355,395-402,439-541,594) via
  `SyncGroupMemberToGameState()` (`eq_player_info.cpp:358`).
- World: zone name/id/connected/pending-zone/zone-line-trigger/time-of-day
  (`eq.cpp:738,1982,3466,3661,3669`; `eq_zone.cpp:133,337,367,541-544,
  774-798`; `eq_entities.cpp:503,1542`).

**Readers**:
- *Production*: `EverQuest`'s own Phase 7.x public accessor methods
  (`eq_player_info.cpp`'s `GetLevel()`/`GetClass()`/`GetRace()`/.../
  `GetPlatinum()`/... — 25+ simple getters, `:114-254`), `GetHeading()`/
  `IsMoving()` (`eq.cpp:3107,3113`), AFK/anon/roleplay/camping getters
  (`eq.cpp:3172-3187`), `GetCurrentZoneName()`/`GetTimeOfDay()` (`eq.h:1171-
  1172`) — these are the "Phase 7.x getters" the plan anticipated.
  **Plus a second, previously-uncatalogued production reader**:
  `EqActionHandler` (`eq_action_handler.cpp`, constructed with `*m_eqClient`
  at `application.cpp:283`, wired as `ActionDispatcher`'s `IActionHandler` at
  `application.cpp:289`) calls `m_eq.GetGameState()` directly in
  `targetNearestPC()`/`targetNearestNPC()` (`:165,181`, reads
  `entities()`/`player()` for nearest-PC/NPC targeting), `hailTarget()`
  (`:325`, entity name lookup), `clickNearestDoor()` (`:342-347`, reads
  `doors()` — **but see "blocked" below, this one is itself dead**),
  `inviteTarget()` (`:405`, entity name lookup), `declineGroupInvite()`
  (`:419`, reads `group().pendingInviterName()`). These execute for real
  key-bound/dispatched actions in every build.
- *Tests only*: 3 test files reference `GetGameState()`/`m_game_state.` —
  not enumerated line-by-line here since the plan's ask is production
  paths, but confirmed present (`grep -rl` count = 3) so nothing was missed
  by assuming zero.

**Subscribers to its private `EventBus`**: **none in production.** Confirmed
by `grep -rn "\.subscribe(\|->subscribe(" src/ include/` excluding tests: the
only two production `subscribe()` call sites are `console_renderer.cpp:84-123`
and `automated_mode.cpp:25`, and both attach to `Application::m_gameState`'s
bus (see Census 2). Nothing subscribes to `EverQuest::m_game_state.events()`
in production, ever.

### Census 2 — `Application::m_gameState` (`std::unique_ptr<state::GameState>`, `application.h:458`)

**Construction**: `application.cpp:196`, before `EverQuest` is constructed
(`:203`) — so `Application`'s instance exists first, and it is what
`ActionDispatcher`, `CommandProcessor`, the game mode, and `InputActionBridge`
are all built against, all before `EverQuest` even exists.

**Who holds a reference**:
- `ActionDispatcher` — `explicit ActionDispatcher(state::GameState& state)`,
  constructed with `*m_gameState` (`application.cpp:288`).
- `CommandProcessor` — constructed with `*m_gameState`
  (`application.cpp:294`).
- `InputActionBridge` — constructed with `*m_gameState`
  (`application.cpp:340`).
- The active `IGameMode` (`HeadlessMode`/`GraphicalMode`/`AutomatedMode`) —
  `initialize(*m_gameState, modeConfig)` (`application.cpp:330`); each mode
  stores it as a raw `state::GameState*` (`headless_mode.h:94`,
  `graphical_mode.h:140`, `automated_mode.h:114`).
- `GameStateSyncBridge` (x2: graphics path and headless/console path) —
  constructed with `*m_gameState` (`application.cpp:555,632`); this is the
  decorator `EverQuest::m_bridge` actually points at (`setBridge()`), so
  every event `EverQuest` publishes passes through here first.
- `Application` itself, directly, in `syncCombatState()`
  (`application.cpp:1830-1875`) and the zone-connected flags
  (`application.cpp:759,780`).

**Writers**:
- `GameStateSyncBridge::applyEvent()` (`game_state_sync_bridge.cpp:9-135`) —
  a **narrow, deliberately-scoped switch** (per its own T40 comment: "every
  other event delegates untouched"): `PetCreated`/`PetRemoved`/
  `PetStatsChanged`/`PetButtonStateChanged` → `pet()`; `PlayerSpawnIdSet` →
  `player().setSpawnId()`; `SpellGemChanged`/`SpellCastStarted`/
  `SpellCastComplete` → `spells()`; `VendorWindowOpened/Closed`,
  `BankWindowOpened/Closed`, `TrainerWindowOpened/Closed` → `player()`'s
  vendor/banker/trainer fields; `InventorySlotChanged` →
  `inventory()`'s occupancy bitfields. **Everything else, `default: break`.**
- `Application::syncCombatState()` (`:1830-1875`, called every tick from
  `syncGameStateFromClient()`) — mirrors `CombatManager`'s target/HP/
  auto-attack into `m_gameState->combat()`, reading from
  `EverQuest::GetEntities()` (the raw `std::map`, **not** either
  `state::GameState::entities()`) for display name/level.
- `Application::update()`'s zone-connected flags — `world().setZoneConnected()`
  (`:759,780`).

**Not written by anything, anywhere, in production** (verified by grepping
every mutator call site across `src/` and `include/`, excluding tests):
`entities()` (`addEntity`/`removeEntity`/`clear` — the *only* call sites in
all of `src/` target `EverQuest::m_game_state.entities()`, none target
`Application::m_gameState->entities()`), `doors()` (**zero** production call
sites populate `state::GameState::doors()` on *either* instance — see
"blocked" below, this one is worse than a split), `group()` (all setters
target `EverQuest::m_game_state.group()` only), and `player()`'s
position/heading/moving/moveSpeed (the T55/T58 finding — `SetPosition`/
`SetHeading` write `EverQuest::m_game_state.player()` only;
`PlayerSpawnIdSet` is the one player-state field the bridge switch does
carry across).

**Readers** (all production, this is the "live" side of the split):
`ActionDispatcher` (~14 methods across `action_dispatcher.cpp`, detailed in
"blocked" below), `CommandProcessor` (`cmdWho`, `cmdInvite`/`cmdDecline`
gating, status display — `command_processor.cpp:698,771,828-834`),
`ConsoleRenderer` via its `EventBus` subscription (chat/spawn/despawn/
combat/zone/target/pet/casting — `console_renderer.cpp:84-123`),
`InputActionBridge` (`m_state.player().heading()`,
`input_action_bridge.cpp:219,253` — see "blocked", this read is stale by
construction), `AutomatedMode`'s catch-all subscriber
(`automated_mode.cpp:25`).

### What each instance actually serves

They are **not** two copies of the same thing, and not "one dead, one live."
They are two *different, overlapping, each-genuinely-populated* projections
of game state, wired to two disjoint sets of consumers:

- **`Application::m_gameState`** backs the mode/dispatcher/command layer —
  everything reachable from a slash command, a hotkey, or the headless
  console — for the narrow slice of state the bridge-event switch and
  `syncCombatState()` actually feed it (pets, spells, vendor/bank/trainer
  windows, inventory occupancy, combat target). For everything outside that
  slice (entities, doors, group, position), it is read from but never
  written to — a hole, not a hidden second copy.
- **`EverQuest::m_game_state`** is the far more completely populated of the
  two — it receives direct writes from nearly every packet handler in
  `EverQuest`'s domain TUs (entities, group, world, most of player). It
  backs the Phase 7.x public getters (used internally by `EverQuest` and,
  transitively, anywhere `EverQuest`'s public API is called) **and**
  `EqActionHandler`'s entity/group lookups — which is the sole bridge that
  lets a subset of `Application`-side actions (targeting, hail, invite,
  decline) reach real data at all, by routing around `Application`'s empty
  copy through the handler layer instead of through `m_state` directly.

So the honest one-line description: `Application::m_gameState` is what the
action/command layer *checks against*; `EverQuest::m_game_state` is where
most of the *real data* actually lives. The split doesn't just leave writes
unread (T55/T58's finding) — it also leaves reads permanently unfed for four
entire state categories, independent of any single write path being wrong.

### What's blocked — code written against the state layer that silently does nothing today

T55/T58's position writes are one item on a longer list. All verified by
tracing the exact write/read pair, not asserted:

1. **Position/heading writes never reach a listener** (T55/T58, confirmed
   present after this unit's read-only pass, unchanged): `SetPosition`/
   `SetHeading` (`eq.cpp:3136,3120`, called from `eq.cpp:2175-2176`
   `ZoneProcessPlayerProfile`, `eq.cpp:3503-3504` `ZoneProcessZonePlayerToBind`,
   `eq_login_world.cpp:897,899` `WorldProcessZoneServerInfo`, **and a fourth
   site not named in the plan's framing**: `eq_action_handler.cpp:92`
   `EqActionHandler::setHeading()`, the dispatcher-driven turn/face path) all
   fire `PlayerMoved`/write into `EverQuest::m_game_state.player()`, which has
   no production subscriber (Census 1). The renderer still gets position via
   the separate hand-rolled bridge push T58 documented as independent and
   unaffected — so nothing user-visible is broken by this specific gap, but
   every one of these four call sites is writing into a receiver that cannot
   hear it.

2. **`ActionDispatcher`'s entity-lookup gates always fail.**
   `Application::m_gameState->entities()` is never populated (Census 2). Every
   `ActionDispatcher` method that checks it before delegating to the handler
   unconditionally returns failure in production, regardless of whether the
   named entity is actually nearby:
   - `faceEntity(name)` — `action_dispatcher.cpp:99` — always
     `"Entity not found: <name>"`.
   - `moveToEntity(name)` — `:154` — same.
   - `moveToEntityWithinRange(name, distance)` — `:167` — same.
   - `followEntity(name)` — `:180` — same.
   - `interactNearest()`'s NPC half — `:446` — `entities.getNearestNPC()`
     always returns null, so the NPC branch of the nearest-interact switch
     (`:459-465`) can never be taken.
   These are commands a player can actually type (`/face`, presumably bound
   `/follow`, the "interact with nearest" hotkey) — not internal plumbing.
   Each one always fails with a plausible-sounding error message
   ("Entity not found") that is indistinguishable from the entity genuinely
   not existing, which is exactly the "setters run, state looks populated,
   readers see nothing" bug class the deferred.md entry already names for
   `/assist` and `cmdHail` — this is the same class, in different call sites,
   not previously catalogued.

3. **`ActionDispatcher`'s door checks always fail, and the deeper cause is
   worse than the two-instance split**: `state::GameState::doors()` (an
   `EQT::DoorStateManager` value member owned by `GameState` itself,
   `game_state.h:96-97`) has **zero production call sites populating it on
   either `GameState` instance** — confirmed by grepping every `addDoor`
   call site in `src/`: the only one found is
   `EverQuest::m_door_state_manager.addDoor()` (`eq_entities.cpp:574`), and
   `m_door_state_manager` (`eq.h:1530`) is a **third, separate**
   `DoorStateManager` instance living directly on `EverQuest`, entirely
   outside either `GameState`. So this isn't "two instances, wrong one
   populated" like entities/group — it's **three instances of the door data,
   and only the one neither `GameState` owns is ever written.** Consequences:
   - `ActionDispatcher::clickDoor(doorId)` — `:385` — always "Door not found".
   - `ActionDispatcher::clickNearestDoor()` — `:399` — always "No door nearby".
   - `ActionDispatcher::interactNearest()`'s door half — `:432` — same dead
     check as the NPC half above.
   - `EqActionHandler::clickNearestDoor()` (`eq_action_handler.cpp:342-347`)
     itself reads `m_eq.GetGameState().doors()` — `EverQuest`'s `GameState`
     copy — which is *also* never populated (same zero-call-site finding
     applies to both `GameState.doors()` instances equally). So even if
     `ActionDispatcher`'s gate above were bypassed, the handler's own
     door-click-nearest logic would still find nothing. The one door
     instance that is actually correct and populated
     (`EverQuest::GetDoorStateManager()`) is used only by
     `eq_bridge.cpp:368` (publishing door snapshots to the renderer for
     rendering) and `eq_entities.cpp:2206,2307` (open/close state) — never
     by the action layer.
   This is the single strongest argument in the entire census that "connect
   the two `GameState`s" (Option B below) is not sufficient by itself: doors
   would still be broken afterward, because the live data was never routed
   through `GameState` at all, on either side.

4. **`ActionDispatcher`'s group gates always fail.**
   `Application::m_gameState->group()` is never populated (Census 2; all
   `group()` setters target `EverQuest::m_game_state.group()` only). So:
   - `ActionDispatcher::acceptGroupInvite()` — `:575` — always "No pending
     group invite", even with a real pending invite.
   - `ActionDispatcher::declineGroupInvite()` — `:587` — same.
   - `ActionDispatcher::leaveGroup()` (`/disband`) — `:599` — always "Not in
     a group", even while grouped.
   `CommandProcessor::cmdDecline`/`cmdInvite` (`command_processor.cpp:698,
   811-816`) route through this dispatcher path, so `/decline` and the
   argument-less `/invite` (accept) are affected. `EqActionHandler`'s own
   `declineGroupInvite()`/`acceptGroupInvite()` implementations *do* read
   real data (`EverQuest`'s copy, Census 1) — but they are never reached,
   because `ActionDispatcher`'s gate in front of them fails first on the
   empty copy.

5. **`CommandProcessor::cmdWho`** (`command_processor.cpp:828-834`) reads
   `m_state.entities().getEntitiesInRange(...)` — `Application`'s empty
   copy. `/who` always prints "=== Nearby Entities ===" with no entries,
   regardless of who is actually nearby.

6. **`InputActionBridge`'s mouse-look heading read is stale by
   construction, unconfirmed whether still live.** `processMouseInput()`
   (`input_action_bridge.cpp:239-265`, called every frame from `update()`,
   `:43`) reads `m_state.player().heading()` (`:253`) as the base for
   horizontal mouse-look rotation, then calls `m_dispatcher.setHeading(...)`,
   which (via `EqActionHandler::setHeading()`) writes only
   `EverQuest::m_game_state.player()` — never back into
   `Application::m_gameState->player()`. Since nothing else populates that
   read either, every call reads the same stale value (constructor default
   `0.0f`, never advancing) rather than accumulating. **Flagged, not
   asserted as an active visible bug**: `src/client/graphics/camera_controller.cpp`
   exists and looks like the actual, current mouse-look implementation — it
   is plausible this `InputActionBridge` code path is itself superseded/dead
   for reasons unrelated to the `GameState` split (worth a runtime check, out
   of this unit's read-only scope and adjacent to T76's camera territory,
   which this unit does not touch). Included because the static fact (the
   read is permanently stale) is real and file:line-verified regardless of
   whether the path is presently exercised.

### The options, with costs

**A — Converge on one instance.** Which one: `EverQuest::m_game_state` is the
better-populated one (Census 1/2), but it is owned by `EverQuest`, and
`ActionDispatcher`/`CommandProcessor`/the modes/`InputActionBridge` are all
constructed *before* `EverQuest` exists (`application.cpp:196` vs `:203`) and
hold `state::GameState&`/`state::GameState*`, not `EverQuest&` — so converging
onto `EverQuest`'s instance means either constructing `EverQuest` first and
handing its `GameState&` to everything downstream (a real reordering of
`Application::initialize()`, touching every one of Census 2's five
constructor call sites), or having `Application` hand its `unique_ptr` into
`EverQuest` at construction time (inverting who owns the canonical instance).
Converging onto `Application`'s instance instead means routing all of
`EverQuest`'s ~90 direct-write call sites (Census 1) through
`EverQuest::m_bridge` events instead of local mutation — which is the
graphics-free boundary the plan flags: `EverQuest` may not hold a
`GameState&` that lives in `Application` without violating "no `m_renderer`
member, no `GetRenderer()` accessor" the same way (`CLAUDE.md`'s "Key
separation") — actually this direction is *compatible* with that boundary,
since `GameState` is not a graphics type; it is `EqActionHandler` (which
already holds `EverQuest&`) that would need to start taking `GameState&` too,
which is fine, it already crosses that seam. **Regression risk: high** —
Census 1 lists ~90 write call sites and ~40 read call sites across 10 files;
retargeting all of them is a large, mechanical-but-error-prone diff, and any
missed site silently reintroduces exactly this bug class in a new shape.
**What it unblocks**: all of items 2, 4, 5 above outright (single populated
instance means `ActionDispatcher`'s gates start seeing real data); item 3
(doors) only if `GameState::doors()` is *also* wired to
`EverQuest::m_door_state_manager`'s data — a second, independent fix bundled
into the same unit or it stays broken.

**B — Connect them** (forward events from one bus to the other, or share the
bus; e.g. `EverQuest::m_game_state.events()` gets a forwarding subscriber
that republishes onto `Application::m_gameState->events()`, or vice versa).
Cheaper: touches `application.cpp`'s bridge-construction block and possibly
`GameStateSyncBridge`, not ~90 call sites. **But it creates a third source of
truth, not two**: forwarding events doesn't retroactively populate whichever
side wasn't listening at the moment of the forwarded event, and it does
nothing for state that is *written directly* rather than published as an
event (Census 1's write list is mostly direct mutation, not event-driven —
`SetPosition` writes `m_game_state.player()` directly and *separately* fires
`PlayerMoved`; forwarding the event does not make the direct write visible on
the other side, only the event). Honestly: this option resolves the position
write (item 1) if the forward target is `Application`'s bus, because
`PlayerMoved` already fires — it does **not** by itself resolve items 2-5,
which are about `entities()`/`doors()`/`group()` never being *written* into
`Application`'s copy at all, not about an unheard event. Those would need
their own new event types/forwards (e.g. an `EntitySpawned` handler in
`GameStateSyncBridge` that actually calls `entities().addEntity()`, which
does not exist today — `console_renderer.cpp` subscribes to
`EntitySpawned`/`EntityDespawned` only to print a message, never to mutate
state). So "connect them" is really "extend `GameStateSyncBridge`'s switch to
cover the categories it currently skips" — a bounded, additive change per
category, each independently scoped and each leaving the two instances
formally still separate (documented two-sources-of-truth, narrowed by
whichever categories get added). **Regression risk: low per category**,
compounding if many categories are added carelessly (the same "double-publish
vs disjoint-consumer" question T58 had to resolve for position would need
re-answering for each new category). **What it unblocks**: whichever
categories are actually added — could be scoped to unblock items 2, 4, 5
without touching item 1 (already benign) or item 3 (needs its own,
unrelated door-data-routing fix regardless of this option).

**C — Delete one.** Not available as a clean option: **neither instance is
vestigial** (the headline finding). `EverQuest::m_game_state` has direct
production writers across ~10 files and two classes of production readers
(`EverQuest`'s own accessors, `EqActionHandler`). `Application::m_gameState`
has production writers (`GameStateSyncBridge`, `syncCombatState()`) and five
constructor-level production readers (`ActionDispatcher`, `CommandProcessor`,
the modes, `InputActionBridge`). Deleting `EverQuest`'s instance breaks every
Phase 7.x getter and `EqActionHandler`'s targeting/hail/invite/decline logic
outright (no fallback data source for those specific reads — `Application`'s
copy doesn't have entities/group populated either, per item 2/4 above, so
deleting `EverQuest`'s side removes the *only* populated copy of that data
without providing a replacement). Deleting `Application`'s instance breaks
five live constructor dependencies and the entire pet/spell/vendor/inventory
bridge-sync path. This option only becomes available if Option A's
convergence work is done first — at which point it isn't really "delete one"
anymore, it's the tail end of Option A.

**D — Leave them split, documented.** Legitimate, and the honest cost
accounting: the position-write gap (item 1) is already benign (renderer gets
position through the independent bridge push T58 verified) — so leaving that
piece split costs nothing beyond a wasted write.  Items 2, 4, 5 are **not**
benign — they are real, present-tense broken commands (`/face`, `/who`,
`follow`/`moveToEntity`-driven actions, `/disband`, dispatcher-gated
`/decline`/accept-invite, "interact nearest") that a player hitting them
today gets a wrong, misleading answer from ("Entity not found" when the
entity exists). Item 3 (doors) is broken regardless of this option's
resolution — it needs its own fix either way. So "leave it split" is not
cost-free the way it might look if you'd only read the T55/T58 position
history: it is currently *shipping* broken user-facing commands, and every
week left split is a week those stay broken and a week more code can be added
against the wrong instance without anyone noticing (the exact failure mode
that produced items 2-5 without anyone flagging them until this census).
Choosing D honestly means writing that down at both declarations — not just
"these are two instances" (already true today, `application.cpp:1793-1797`)
but "here is the current list of commands this silently breaks," so the next
person doesn't have to re-derive items 2-5 from scratch, and so it reads as a
tracked, accepted cost rather than an invisible one.

### Recommendation

I would **not** pick D as written today, because items 2, 4, 5 are not a
future risk to be accepted — they are current breakage a player can trigger
right now, and "leave it split" without fixing those specific items means
choosing to ship broken commands rather than choosing to defer an
architectural cleanup. I would split the work: **fix Option B narrowly**
(extend `GameStateSyncBridge`'s switch to add `EntitySpawned`/
`EntityDespawned` → `entities()` mutation and the group event types the
bridge doesn't yet forward — `GroupMemberUpdate`-shaped events would need to
be introduced or an existing polling call added the way `syncCombatState()`
already does it for combat) to close items 2, 4, 5 cheaply and now, **plus a
standalone, unrelated fix for item 3** (doors) that routes
`EverQuest::m_door_state_manager`'s real data into whichever `GameState`
copy the action layer reads, since that item is broken independent of
whatever else is decided here. I would leave the full Option A convergence
(single canonical instance) as a separate, larger, deliberately-scoped unit
of its own — not bundled into this decision — because its regression
surface (~90+ call sites) is large enough that doing it under the same unit
as the narrow, safe fixes above would risk the safe fixes on the large one's
correctness. That said: this is Will's call, not mine to make by default,
per the plan's own instruction — the above is a preference with reasoning,
not a foreclosure of B, C being off the table, or D done honestly with the
now-documented cost list.

### Verification

Read-only pass: no `src/`/`include/` edits, confirmed by `git status
--porcelain` at unit end showing only the two files this unit intentionally
touched:

```
 M refactor/T78_two_gamestate_analysis.md
 M refactor/deferred.md
```

No production build/test run was needed or attempted — this unit changes no
behavior, and the plan's own verification bar narrows to proving that (`git
status` should show only `refactor/`). `scripts/refactor/check_file_sizes.sh
--files refactor/T78_two_gamestate_analysis.md refactor/deferred.md`: the
plan file is 535 lines (ok); `deferred.md` reports FAIL at 4384 lines
(>3000) — **pre-existing, not a regression from this unit**: `deferred.md`
is a markdown research record the file's own header declares append-only
("this file only ever grows by append"), not a C++ translation unit the
Batch F line-count ceiling was written for (`docs/decisions.md`), and it was
already far over 3000 lines before this unit's two additions (an Index
one-liner update and a ~20-line addendum to one section). Same
already-over-ceiling pattern T55/T58 each documented for `eq.cpp` at their
own unrelated files.

### Deviations from the plan, flagged rather than absorbed

- The plan's own line citations for both declarations (`eq.h:1210`,
  `application.h:437`) and the construction site (`application.cpp:180`)
  drifted by a handful of lines from this unit's verified numbers
  (`eq.h:1214`, `application.h:458`, `application.cpp:196`) — cosmetic, from
  code added since those citations were written; corrected at the top of
  this Review, nothing substantive depends on the old numbers.
- The plan named T55/T58's position writes as "the known case" to extend
  from. This unit found a fourth production write site to that same dead
  bus not named by either prior unit (`eq_action_handler.cpp:92`,
  `EqActionHandler::setHeading()`) and four additional, independently
  broken categories (`entities()`, `doors()`, `group()` never populated on
  `Application`'s copy) affecting real slash commands/hotkeys — this is the
  "may be longer than anyone expects" list the plan anticipated but did not
  presume, called out prominently per the plan's own instruction.
- No STOP condition was hit — no urge to prototype a fix, no edit to
  `src/`/`include/` was made or needed.
