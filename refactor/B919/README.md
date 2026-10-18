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

## The measurements (step 3)

### x86 (rendered client, live eqemu lane 1, `scripts/client_ledger_capture.sh
--modes game_state`, the B941 recipe — a live zone-in is what populates
`game_state`; the offline `gles2_zone_benchmark` loads only the geometry, so
its `gs.entities`/`gs.event_queue`/`gs.dispatch` rows would be empty)

`ready` point, MB. The entity/door counts are the client's own snapshot line
(`snapshot created (zone=..., entities=N, doors=N)`), taken at the same instant.

| zone | entities | doors | `game_state` total | `gs.navmesh` | `gs.collision_map.*` (sum) | `gs.entities` | `gs.doors` | `gs.world_objects` | `gs.event_queue` | `gs.event_stage` | `gs.dispatch` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| emeraldjungle | 266 | 3   | 26.22 | 15.78 | 10.21 | 0    | 0.0004 | —      | —          | —          | 0.22 |
| overthere     | 437 | 23  | 26.32 | 21.97 | 4.79  | 0.003 | 0.003 | 0.001  | —          | 0.27     | 0.33 |
| timorous      | 303 | 47  | 25.23 | 19.07 | 7.47  | —    | 0.007 | 0.001  | —          | 0.21     | 0.24 |
| nurga         | 2   | 0   | 2.56  | 0.61  | 2.94  | —    | —     | —      | —          | 0.02     | 0.06 |

(`gs.collision_map.*` is the B951 sub-row sum: `.verts` + `.tris` + `.nodes`
+ `.refs` + `.leaves` + `.flags` + the map's own 400-byte header block.
`—` = no row at `ready` (a zero-byte block prints no line). `gs.entities`/
`gs.doors`/`gs.world_objects` are all ≤ 7 KB; `gs.event_queue` is absent
because the B945 halving rule frees the burst buffer before `ready` in
emeraldjungle/nurga and it is < 1 block in the other two.)

### The finding (step 4, the mechanism)

**The `game_state` category is the zone GEOMETRY, not the zone INHABITANTS.**
`gs.navmesh` + `gs.collision_map.*` are 24.7–26.0 MB of the 25–26 MB total in
every populated zone — 94–99 % — and they are the four B941/B951 rows this
card does not re-measure. The per-spawn state this card names
(`gs.entities`, `gs.doors`, `gs.world_objects`, `gs.event_queue`,
`gs.event_stage`, `gs.dispatch`, and the `misc`/`gs.thread` residual) is
**kilobytes**: the largest is `gs.dispatch` at 0.33 MB (overthere, 437
entities), the rest are < 300 KB.

**The prediction that was checked:** "the bytes inside `game_state` scale
with the entity count." **It is false.** The entity count ranges 2 → 437
(218×) while the per-spawn rows range 0 → 0.33 MB (a 330 KB ceiling, and
`gs.entities` itself is 3,328 bytes for overthere's 437 entities ≈ 7.6
bytes/entity — one `state::Entity` sync struct's worth, not a table). What
scales is the geometry: overthere's navmesh is 22.0 MB (emeraldjungle's
`.nav` is 7.89 MB on disk, overthere's the largest in the set) and its
collision map 4.8 MB; nurga's navmesh is 0.61 MB and its map 2.94 MB, and
nurga is the one zone the B854 sweep found under the 64 MB yardstick.
**Bytes per entity for the top owner ≈ 0; the top owner is the navmesh, at
~1.4 MB per MB of on-disk `.nav` (B941's own inflation factor), and it does
not look at the entity count.**

So the B854 "who owns `game_state`" question resolves to: **the answer is
already known — it is the B941/B951 geometry (navmesh + collision map), and
the per-spawn share is too small to be the 4.6→51 MB driver.** The 4.6→51 MB
range in the B854 table is the geometry's range (a small zone's navmesh +
map vs a large zone's), not an entity-count range. The shrink queue below
therefore points at the geometry owners, not the spawn owners.

### What the Pi leg must check (the open number)

The B854 table measured `game_state` on the Pi at `steady` at up to 51.2 MB
(emeraldjungle) — ~2.5× the 25–26 MB this card just measured on x86 for the
same zones at `ready`. The owner breakdown on the Pi is the number this card
still owes (acceptance B): it must show whether the Pi's larger `game_state`
is the same geometry (then the x86 finding holds and the 2.5× is a
measurement-path difference — the Pi's rendered zone-in keeps more, or the
B854 numbers pre-date the B941/B951 geometry shrink) or a different owner
(the Pi's entity/model state is charged to `game_state` where x86's is
charged elsewhere). The Pi run is the arbiter; until it lands, the
`game_state` owner table is the x86 one above and the Pi's is "pending, lane
1, `PI_LANE=1`".

## The proposed shrink queue (step 5, ordered by bytes)

This card PROVES; it does not shrink (PROVE-THEN-FIX,
`docs/local_model_cards.md`). Will plans the actual queue from this —
`docs/decisions.md` is not edited here. The owners, in the order the bytes
say to attack them, with the mechanism and the risk of each:

| # | owner | measured (x86, MB) | mechanism | proposed change (for the FIX card) | risk |
|---|---|---|---|---|---|
| 1 | `gs.navmesh` | 0.61–22.0 | Detour tiles are the on-disk `.nav` inflated by ~1.4× (B941's own factor); kept for the zone's life for pathfinding | shrink the DETOUR TILES (LRU-evict tiles far from the player — the navmesh is queried only on path requests, not per frame; or a quantised-vertex re-encode) | path quality/latency at an eviction boundary; a wrong eviction = "Pathfinding failed" for a live `/follow` |
| 2 | `gs.collision_map.verts`+`.tris` | 4.6–10.2 | the placed-object collision triangle soup: B941 deduped the per-placement vertices (549,559 placeable tris → 366,338 verts in emeraldjungle) but the soup is still ~10 MB; kept for the zone's life for LOS/FindBestZ | shrink the COLLISION MAP (a quantised-vertex/normal encoding of the placeable subset, or a coarser per-leaf AABB BVH) | ray-query identity — the `GameStateIdentity` digest test is the net; a change that moves a triangle value is red |
| 3 | `gs.collision_map.nodes`/`.refs`/`.leaves` | 1.9–3.5 | the raycast tree's internal structure (one node per spatial bucket) | a tighter tree (a BVH over the placements instead of the per-leaf flat lists) | same identity net as #2 |
| 4 | `gs.dispatch` + `gs.event_stage` + `misc`/`gs.thread` | 0.05–0.55 | the zone-packet handlers' transient state (the B945 burst buffer's retained half is in `gs.event_stage`; the handlers' scratch in `gs.dispatch`/`misc`) | none proposed — < 1 MB in every zone; not the 4.6→51 MB driver | low payoff |
| — | `gs.entities`/`gs.doors`/`gs.world_objects` | ≤ 0.007 | the per-spawn state (3,328 bytes for 437 entities ≈ 7.6 B/entity) | **none** — not the driver; do not burn a unit on it | — |

The queue's shape is therefore a **two-item geometry queue** (navmesh, then
collision map), NOT the "entity table, spawn structs, per-entity
appearance/equipment" list the card's step 2 hypothesised — the measurement
overturned the hypothesis, which is the point of a PROVE card. The per-spawn
owners are named and pinned (the census test), so a future unit cannot
re-attribute them by accident, but they are not where the bytes are.

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

In: the six new owner rows (seven scoping sites, `gs.doors` twice), the
`gs.thread` name, the hot-path gate, the census test, the x86 measurement
of the four zones, and this README.

Out (boarded, not done here): the Pi's owner tables for the same four zones
(acceptance B's other half — `PI_LANE=1`, the ARM build + rendered sweep,
in flight when this README was last committed); the follow-up SHRINK unit
for the geometry owners (navmesh first, then collision map — the two-item
queue above); other categories (`network`, `config`) — their owner tables
are one `WILLEQ_LEDGER_OWNERS` export away, but their sites are not
surveyed here.
