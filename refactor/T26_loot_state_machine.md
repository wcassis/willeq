# T26 — Event-driven loot state machine (removes the game-thread stall)

**Slot: t1. x86 + a live eqemu session required (see Verification).**
No sibling unit.

**Territory: `src/client/combat.cpp` + `include/client/combat.h`, the
game-thread tick wiring, and the T23 loot tests.** This is a
**source-touching, behavior-changing** unit — the first in the spell/
combat batch. Will approved the design 2026-08-01 (conversation);
this plan is the contract.

Read `refactor/EXECUTOR_PROTOCOL.md` first, then the T23 finding section
in `refactor/testing_progress.md`.

## The defect being fixed

Four `std::this_thread::sleep_for` calls block the **game thread** inside
packet dispatch:

| Site | Duration | Context |
|---|---|---|
| `combat.cpp:1168` | 500 ms | `ProcessCombatRound`, "wait for corpse to spawn" (auto-hunt only) |
| `combat.cpp:1861` | 500 ms | `CheckAutoLoot`, "give a moment for all items to arrive" |
| `combat.cpp:818` | 100 ms **per item** | `LootAll`, between items |
| `combat.cpp:825` | 500 ms | `LootAll`, before closing the window |

Reached via `TickNetwork()` → zone packet dispatch →
`ZoneProcessLootItem` (`eq.cpp:1021`) → `CheckAutoLoot`. While asleep the
client drains no packets, runs no game tick, and processes no renderer
intents. One kill with one item ≈ 1.1 s; auto-hunt ≈ 1.6 s.

**Severity nuance — get this right, my earlier framing overstated one
part and understated another.** `m_auto_loot_enabled` defaults **true**
but `m_auto_hunting_enabled` defaults **false**, so the auto-hunt flow
needs opt-in. However `CheckAutoLoot` does **not** require auto-hunting:
`EqActionHandler::lootCorpse` → `CombatManager::LootCorpse` sets
`COMBAT_STATE_LOOTING` while leaving `m_player_looting_corpse_id` at 0,
so dispatch takes the sleeping branch. That action is fed by
`InputActionBridge::processLootRequests` ← `consumeLootRequest()` —
documented as shift+click on a corpse. The loot-*window* path
(`EverQuest::RequestLootCorpse`, `eq_bridge.cpp:1494`) sets that id and
is unaffected.

## The design (Will's, 2026-08-01)

Replace timed sleeps with an **event-driven state machine with
deadlines**. Rationale: the right wait is variable (server load), so a
fixed sleep is both too long and too short. React to the events that
actually arrive; never block; and never assume an expected event will
come.

`CombatManager` already owns a `CombatState` machine — extend it with an
explicit loot sub-state rather than inventing a parallel mechanism.

Suggested states (name them as you see fit, but every one needs an exit):

```
Idle
  -> AwaitingCorpse    (entered on kill; deadline; exit on corpse seen)
  -> RequestingLoot    (send OP_LootRequest)
  -> AwaitingItems     (deadline + quiescence; items arrive one per packet)
  -> LootingItems      (one item per N ticks — preserves the current
                        pacing WITHOUT blocking)
  -> Closing -> Idle
```

**Every state needs both a deadline and a "corpse disappeared" exit.**
This is the crux: today the sleep is ugly but guarantees termination.
Remove the sleeps without deadlines and you trade a 1.1 s stall for a
silent permanent hang in LOOTING — worse, because `IsLooting()` also
gates the dispatch branch at `eq.cpp:1021`.

**Events available (verified):**
- Corpse appears: `is_corpse = true` set on the death packet
  (`eq.cpp:3164`) and via `OP_MobRename` (`eq.cpp:1370`).
- Each loot item: its own `OP_ItemPacket` → `ZoneProcessLootItem` →
  `AddLootItem`.
- **`HC_OP_LootComplete` is NOT reliable** — `eq.cpp:993` documents it as
  "rarely sent by P1999/Titanium servers." Do not gate completion on it.
  Use quiescence (no new item for N ticks) plus a hard deadline.

Fold the pacing constants (currently magic numbers at four sites) into
named constants on the machine so the "still polite enough?" question is
answerable later.

## Threading — the part most likely to go wrong

**Do NOT drive this from `CombatManager::Update()`.** That runs on
`m_update_thread` at 2 Hz (`eq.cpp:3535`, 500 ms sleep per iteration),
and the long comment at `eq.cpp:3530` documents that P36 fixed a
process-terminating self-join deadlock caused by exactly that thread
racing the game thread. Putting loot mutation there re-creates that
class of bug.

Drive it from the **game thread**, so packet ingestion and timeout
evaluation live on one thread and need no locks.

**Trap:** `EverQuest::GameTick()` (`eq_bridge.cpp:227`) looks like the
obvious home — it already ticks spell and buff managers — but it
**early-returns when graphics are not initialized**, and
`Application::updateGameState` only calls it under `m_graphicsInitialized`
(`application.cpp:937`). The loot path fires in **headless** mode, so
GameTick would never advance it there. Use the unconditional game-thread
path instead: `UpdateMovement()` is called with no graphics check
(`application.cpp:942`) and P36's comment confirms it runs "every game
tick (~60Hz) ... unconditionally (graphics or headless)". Add the loot
advance alongside it, or as its own unconditional call in
`updateGameState`. Verify headless advancement explicitly.

## Wire behavior must not change

The delays are deliberate server politeness ("simulate human behavior").
Preserve the *observable* pacing — same packets, same approximate
intervals — while never blocking. "Act on tick N+k" replaces "sleep
k ms". If you conclude a delay is pointless, say so in the Review; do
not silently drop it.

## Tests

- Re-point `CombatManagerLoot.BUG_CheckAutoLootBlocksCallingThreadForOverASecond`
  (T23, `tests/test_combat_manager.cpp`): it should become an assertion
  that the call **returns promptly** and the machine advances across
  ticks — a better regression guard than "this takes over a second".
- Add coverage for the new exits: deadline expiry with no corpse, corpse
  despawning mid-loot, item quiescence, and headless advancement.
- The T23 fixtures (spawn staging, `TestBridge`) are in
  `tests/zone_stream_test_utils.h`.

## STOP conditions

- If preserving wire pacing turns out to require blocking somewhere,
  STOP and report rather than shipping a stall in a new place.
- If the state machine cannot be advanced without touching
  `m_update_thread`, STOP — that is a design question for Will, not a
  judgement call to make mid-unit.
- Any behavior change beyond the loot path → STOP.

## Verification bar (FULL — this is source-touching and behavioral)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` flake family (T03b,
   perf territory — rerun isolated). Baseline currently 1789.
3. **Golden replay**: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq` — must
   pass with the golden file **unchanged**.
4. **Live eqemu session** — the thing that actually matters here, since
   loot behavior against a real server is what must keep working.
   **LOCK FIRST** per EXECUTOR_PROTOCOL.md's eqemu section: check
   `/mnt/projects/fable/.eqemu-lock` does not exist, create it with your
   unit ID, delete it after clean shutdown. Start only via
   `scripts/eqemu/up.sh`. Kill something, loot it both ways (shift+click
   and auto-hunt if you can enable it), and confirm: items actually
   loot, the client never freezes, and the game thread keeps processing
   packets throughout (a `/who` or chat during the loot sequence is a
   cheap liveness check). Restore Testchar's position afterward
   (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190).
5. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

## Review

(Executor fills: the state machine as built, where the tick was wired
and the headless-advancement evidence, which pacing constants were kept
and why, live-session evidence, tests re-pointed/added, deviations.)
