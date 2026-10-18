# B919 — the `game_state` owner rows: who owns the 2.4 MB the B941 README's "misc" swallowed

Issue: gitlab goldfarm/willeq **B919** / #238. Parent: **B941**
(`refactor/B941_game_state_memory.md`, its README at `refactor/B941/README.md`),
whose zone-load rows proved the mechanism and left the rest of the
`game_state` category unnamed.

## The problem, in B941's own numbers

B941's table (its README, "The four loads") ends:

> | **misc** | **2,398,196** | **79.2 %** | the rest of the `game_state` category at zone load (the packet handlers, entity state, the bridge queues — a B839 follow-up) |

The `misc` is the category minus the four named loads: **everything the game
thread touched while zoned in, unnamed.** B941's README even names its
members in prose — "the packet handlers, entity state, the bridge queues" —
but the table row that a shrink unit would actually shrink is still `misc`,
and a unit that shrinks `misc` cannot know which of those it shrank, or that
it shrank the right one.

B839's ruling (the B839 README "The mechanism") made the owner rows a
DIAGNOSTIC, off by default, opt-in per category via
`WILLEQ_LEDGER_OWNERS=<cat>`. B941 opted `game_state` in and named four rows.
B919 finishes the census: every allocation site that opens the
`game_state` category gets a named row, or an explicit, tested declaration
that it is the residual.

## The rules the rows must obey (from the mechanism, restated)

1. **The rows are a re-pointing, not a partition.** A
   `StartupScope owner("gs.x")` sets the *sub-label* for the *current*
   category; it does not change the category. So a label opened on the
   render thread does not move bytes out of `gpu_buffers`, and one opened
   on the game thread does not move bytes out of `game_state`.
2. **The rows must not leak into `gpu_buffers`.** `MemoryScope`'s
   constructor CLEARS the sub-owner (B839's "the scoping site itself").
   Consequence: a `StartupScope` opened BEFORE a `MemoryScope(GameState)` is
   DEAD — its bytes are captured by whatever scope is active after the
   MemoryScope (the category default, i.e. `misc`), not by the label. The
   label must open AFTER the MemoryScope, inside it.
3. **The residual must be named too.** A category with named rows and an
   unnamed `misc` is only as good as its residual's identity. B941's `misc`
   was unnamed; B919 names the game thread's DEFAULT owner `gs.thread`
   (its unlabelled bytes land on the category default, which the tables
   print as `misc` — the comment at the `setThreadCategory` site is the
   link, pinned by the census test, not by another row).
4. **The hot path must pay nothing off.** `GameStateBridge::pushEvent`
   runs on every event, every tick, on both threads. Its owner label is
   gated on `eqt::memory::ownerRowsEnabled()` — a `static const bool` that
   reads `WILLEQ_LEDGER_OWNERS` once per process — so an ordinary run (no
   table requested) pays one false branch, and even a capture run pays the
   label lookup only per push.

## The census (the table the tables must be able to quote)

| row | site (the opening line) | what it owns |
|---|---|---|
| `gs.entities` | `EntitiesComponent::ZoneProcessZoneSpawns` (`src/client/entities/entities_component.cpp`) | every spawn parsed into `ctx_.entities` and synced into the state `EntityManager` for the zone |
| `gs.doors` | `EntitiesComponent::ZoneProcessSpawnDoor` (`entities_component.cpp`) + `EntitiesComponent::ZoneProcessMoveDoor` (`entities_component_world.cpp`) | the `DoorStateManager` door table at spawn and at state updates |
| `gs.world_objects` | `EntitiesComponent::ZoneProcessGroundSpawn` (`entities_component_updates.cpp`) | the `m_world_objects` ground-spawn entries |
| `gs.event_queue` | `GameStateBridge::pushEvent` (`include/client/bridge/game_state_bridge.h`) | the base event queue's storage — the burst buffer B945 kept, at its peak |
| `gs.event_stage` | `GameStateSyncBridge::stagePendingEvents` (`src/client/bridge/game_state_sync_bridge.cpp`) | the game→render staging FIFO (`m_forwardQueue`) and its transient batch |
| `gs.dispatch` | `DispatchZonePacket` (`src/client/zone/zone_dispatch.cpp`), opened INSIDE the T491 `MemoryScope(GameState)` | the dispatch path's own game-state bytes: the zone-packet handlers that open no row of their own (the catch-all B941 called "the packet handlers") |
| `gs.navmesh` | `LoadZoneSources` (`src/client/zone/zone_entry.cpp`) — B941, unchanged | the navmesh buffer |
| `gs.collision_map` | `LoadZoneSources` — B941/B951, unchanged | the collision map |
| `gs.zone_lines` | `LoadZoneSources` — B941, unchanged | the zone lines |
| `gs.zone_sources` | `LoadZoneSources` — B941, unchanged | the zone source cache |
| `gs.thread` | **the game thread's DEFAULT** — `setThreadCategory(GameState)` in `Application::gameThreadLoop` (`src/client/application.cpp`), named in the comment there | the residual: the tick loop, the mode input, the intent processing, and every handler that opens no row. Printed as `misc` by the tables; the comment is the name |

Notes on placement:

- **`gs.dispatch` is the catch-all, and it is deliberately inside the
  T491 scope.** `DispatchZonePacket` opens `MemoryScope(GameState)` (T491),
  and the per-handler rows (`gs.entities`, `gs.doors`, ...) open
  `StartupScope`s inside it; a label wins per block, so a block allocated
  under `gs.entities` does not also count as `gs.dispatch`. What is left —
  the handlers with no label of their own — is `gs.dispatch`.
- **`gs.event_queue` and `gs.event_stage` are two different buffers on the
  same path.** `pushEvent` appends to the base `eventQueue_`
  (`gs.event_queue`); the game thread's `stagePendingEvents` drains it into
  the sync bridge's `m_forwardQueue` (`gs.event_stage`); the render thread's
  `forwardStagedEvents` drains that back into a fresh vector and pushes the
  events into the renderer-facing bridge — a second `pushEvent`, same
  `gs.event_queue` row, on the other thread. Both sides of the handoff
  land on the two named rows; nothing on the path is unnamed.
- **`gs.network` was considered and rejected.** `processNetworkEvents`
  opens no `game_state` scope of its own: `TickNetwork` re-scopes to
  `Network`, and the zone dispatch re-opens `GameState` (T491). A
  `StartupScope` there would open on the thread default (`GameState`), but
  every allocation it could capture gets re-scoped by the inner
  `MemoryScope` — the label would be dead by rule 2's mechanism, exactly
  like a label before a `MemoryScope`.

## What the measurement will show (the run)

The capture recipe is unchanged from B941/B839
(`scripts/client_ledger_capture.sh`):

```
WILLEQ_LEDGER_OWNERS=game_state WILLEQ_LEDGER_REPORT_AT_SHUTDOWN=1 \
  <run to zone-in ready, then /camp>
```

prints, for the ready point:

```
[memledger-startup] ready=... category=game_state bytes=... owners:
    gs.zone_sources <B>  gs.navmesh <B>  gs.collision_map <B>
    gs.zone_lines <B>  gs.entities <B>  gs.doors <B>
    gs.world_objects <B>  gs.event_queue <B>  gs.event_stage <B>
    gs.dispatch <B>  misc <B>
```

Expectations, to be checked against the actual run (the x86 tree's
`[memledger]` line already prints the category totals:
`game_state=2,908,984` in B941's run, of which the four loads were 510,788,
i.e. `misc` ≈ 2.4 MB — the numbers B919's rows must now account for):

- `gs.entities` is the biggest of the new rows (every spawn in the zone's
  spawn table, with the state-side `Entity` sync);
- `gs.event_queue` is the B945 burst buffer at its PEAK — the ready
  point's queue is the retained half, so the row reads the peak it drained
  to, not the steady state;
- `gs.dispatch` + `misc` (the `gs.thread` residual) together are the
  remainder — the shrink target for a follow-up unit, and now named.

## The nets

- **`tests/test_game_state_owners.cpp`** (ctest, hygiene-free: a source
  reader, no client link) re-takes the census every run and FAILS on
  (a) a `game_state` scoping site with no owner label in the same function,
  (b) the `gs.thread` name disappearing from the comment above the thread
  default, and (c) any of the B941 rows leaving `zone_entry.cpp`. It is the
  same shape as `test_data_path_routing.cpp` (T399): the sites are network
  handlers, not exercisable in-process, so the ratchet is textual, and the
  behavioural half is `tests/test_memory_ledger.cpp`'s
  `StartupScope` tests (a labelled allocation really lands on its row).
- **`ownerRowsEnabled()`** (in `memory_ledger.{h,cpp}`) is the hot-path
  gate; `test_memory_ledger.cpp` does not exercise it (it is a getenv
  reader), but its absence from an ordinary run is the visual gate's
  concern, not a test's.

## Scope

In: the six new rows (seven sites, `gs.doors` twice), the `gs.thread`
name, the hot-path gate, the census test, this README.

Out (boarded, not done here): labelling the render thread's buffer rows
(`gpu_buffers` already has its `gpu_buffers`/`gpu_driver`/`gpu_buffers_*`
rows from B839/B955); the follow-up SHRINK unit for whatever
`gs.entities`/`gs.dispatch`/`misc` turns out to be; other categories
(`network`, `config`) — their owner tables are one
`WILLEQ_LEDGER_OWNERS` export away, but their sites are not surveyed here.
