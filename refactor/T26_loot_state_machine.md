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

### The state machine as built

`LootSubState` (`include/client/combat.h`), nested under the existing
`CombatState::COMBAT_STATE_LOOTING` rather than a parallel top-level state,
per the plan's instruction to extend the existing machine:

```
Idle
  -> AwaitingCorpse   (auto-hunt kill only; polls GetEntities() each tick;
                        exit: corpse found -> BeginLootRequest(), or
                        kCorpseSearchDeadlineMs elapsed -> Idle)
  -> AwaitingItems    (entered by BeginLootRequest(), which both LootCorpse()
                        (shift-click/test-fixture path) and AwaitingCorpse's
                        find call; exit: quiescence (no new item for
                        kItemQuiescenceMs) -> LootingItems, hard deadline
                        (kAwaitingItemsHardDeadlineMs) -> LootingItems (if
                        items pending) or AbortLoot() (if none), or corpse
                        gone -> AbortLoot())
  -> LootingItems     (drains m_loot_items one slot per kInterItemDelayMs
                        tick via LootItem(); exit: drained -> Closing, or
                        corpse gone -> AbortLoot())
  -> Closing          (exit: kClosingDelayMs elapsed, or corpse already
                        gone -> CloseLootWindow() -> Idle)
```

Every non-Idle state is driven exclusively by `CombatManager::UpdateLootState()`
(new), a single switch dispatching to `TickAwaitingCorpse/TickAwaitingItems/
TickLootingItems/TickClosing` (all new, `src/client/combat.cpp`). No locks:
everything reads/writes only fields owned by `CombatManager` on one thread.

Deviation from the plan's suggested 6-state list: I merged "RequestingLoot"
into `AwaitingItems`'s entry (`BeginLootRequest()` sends `HC_OP_LootRequest`
then immediately calls `EnterLootSubState(AwaitingItems)`) rather than a
separate wait state — sending one packet is synchronous, there's nothing to
wait for between "decide to loot" and "packet queued." Every other state in
the plan's list is present.

Corpse-disappeared exit: `CorpseStillPresent(uint16_t)` (new) checks
`GetEntities().find(id) != end()`; checked at the top of every tick that has
a live corpse id (`AwaitingItems`, `LootingItems`, `Closing`). `AwaitingCorpse`
has no corpse id yet, so its "corpse never shows up" case is covered by the
deadline alone (matches the plan's own framing: "entered on kill; deadline;
exit on corpse seen").

`AbortLoot()` (new) reuses `CloseLootWindow()`'s existing cleanup (clears
corpse id/items, sends `HC_OP_EndLootRequest`, restores
`COMBAT_STATE_HUNTING`/`COMBAT_STATE_IDLE`) for every non-success exit —
sending `EndLootRequest` for an already-vanished corpse is harmless.

### Where the tick was wired, and headless-advancement evidence

`Application::updateGameState()` (`src/client/application.cpp`), immediately
after the existing unconditional `m_eqClient->UpdateMovement()` call (not
inside `UpdateMovement()` itself, to avoid its early-return branches for
deferred logout/zone-change):

```cpp
if (m_eqClient) {
    m_eqClient->UpdateMovement();
    if (CombatManager* cm = m_eqClient->GetCombatManager()) {
        cm->UpdateLootState();
    }
}
```

This is **not** inside the `if (m_eqClient && m_graphicsInitialized)` block
above it (that block gates `GameTick()` only) — it sits in the unconditional
`if (m_eqClient)` block, same as `UpdateMovement()`, so it runs identically
whether `--no-graphics` or not. Read directly from the file, not inferred:
`m_graphicsInitialized` appears nowhere in this code path.

Explicitly **not** wired into `CombatManager::Update()` (`m_update_thread`,
2 Hz) — confirmed by inspection that `Update()` and `UpdateLootState()` are
separate public methods with separate call sites; `Update()`'s call site is
still only `eq.cpp`'s `StartUpdateLoop()` lambda, untouched by this unit.

Live-session headless-advancement evidence: see below — the live client ran
with `--no-graphics` throughout (both tour attempts), connected, zoned in,
and later cleanly camped/exited, which requires `UpdateMovement()` (and thus
the now-adjacent `UpdateLootState()`) to have executed every tick for the
whole session; `m_graphicsInitialized` was false the entire run (no
`--egl`/graphics flags passed). I could not additionally exercise a full
kill+loot cycle live in headless mode — see "Live-session evidence" below
for why and what was substituted.

### Pacing constants kept and why

| Constant | Value | Replaces | Rationale |
|---|---|---|---|
| `kCorpseSearchDeadlineMs` | 2000 | blind 500ms single-shot wait (combat.cpp:1168, old) | Was a fixed delay before exactly one search attempt (silently gave up if the corpse spawned late). Now polls every tick from kill to deadline, so raising it to 2000ms costs nothing (no blocking) while covering slow corpse spawns the old code dropped entirely. |
| `kItemQuiescenceMs` | 500 | `CheckAutoLoot`'s "give a moment for all items to arrive" (combat.cpp:1861, old) | Same value, same intent — kept exactly since it's server-politeness pacing, not something this unit found reason to change. |
| `kAwaitingItemsHardDeadlineMs` | 5000 | none (new) | Safety net with no legacy equivalent: the old code never handled "auto-loot on, corpse present, items never fully quiesce or auto-loot got disabled mid-session" — it would have looped `CheckAutoLoot()` forever without ever hitting `LootAll()`. New behavior closes the session anyway once way past any plausible server response time. |
| `kInterItemDelayMs` | 100 | `LootAll`'s per-item sleep (combat.cpp:818, old) | Same value — preserves the observable "one item request roughly every 100ms" wire pacing exactly, just via tick-count instead of blocking. |
| `kClosingDelayMs` | 500 | `LootAll`'s pre-close sleep (combat.cpp:825, old) | Same value, same reasoning as above. |

No delay was judged pointless — all four legacy sleeps have a same-value
named-constant replacement; the fifth (see below) does too.

**A fifth sleep, not in the plan's four-site table:** `ProcessLootWindow()`
(combat.cpp:1843, old) had its own `sleep_for(300ms)` before calling
`LootAll()`. It is **dead code in production** — grepped for callers across
`src/client/` and `include/client/`; the only other reference is a comment
in the pre-existing T23 test file. I did not silently ignore it: since it's
still reachable from a public `CombatManager` method (and the plan's spirit
is "remove the sleeps, not just the four cited ones"), I converted it too —
it now populates `m_loot_items` and calls
`EnterLootSubState(LootSubState::AwaitingItems)`, routing through the same
quiescence/deadline/corpse-gone machinery as the live paths, rather than
leaving a stray blocking call sitting in the tree.

### Live-session evidence (and a testing-infrastructure finding)

Full protocol: server up via `scripts/eqemu/up.sh arena` + `seed.sh`, lock
taken/released per `EXECUTOR_PROTOCOL.md` (`/mnt/projects/fable/.eqemu-lock`
created with unit id, confirmed absent beforehand, removed after clean
`down.sh` shutdown — verified via `ls` before and after).

What I confirmed live, against the real server, headless (`--no-graphics`,
matching the built-and-tested config):
- Login/world/zone connect, character select, full zone-in to `arena`.
- GM `#npctypespawn 351043` (a_rat), `/list`, `/target <name>`, `/attack`
  all round-tripped correctly: `EnableAutoAttack` sent real
  `HC_OP_AutoAttack`/`AutoAttack2` packets, and the server replied with a
  continuous stream of real `HC_OP_Damage` packets (`ZoneProcessDamage`
  firing repeatedly, log-confirmed) — i.e. real combat happened against the
  live server.
- `/who`, `/stopattack`, `/camp` all processed promptly throughout, and the
  client exited cleanly (`Application shutdown complete`, exit code 0) both
  times I ran it — no freeze, no dropped connection, movement-sync packets
  (`POS C->S`/`POS S->C`) kept flowing continuously the whole session,
  which is itself evidence the game thread never stalled (that is exactly
  what the old bug would have interrupted).
- Testchar's DB position after the session (`zone_id=36, x=-77.875,
  y=21.875, z=3, heading=190`) was already at (within float rounding) the
  documented canonical rest point — no correction needed.

What I could **not** complete live: an actual kill -> auto-loot cycle.
Finding, not a T26 defect: `CombatManager::Enable()` (`m_enabled`, required
for `CombatManager::Update()` to run at all, and thus for
`ProcessCombatRound()`'s corpse-search/`AwaitingCorpse` entry to ever fire)
is reachable from exactly one place in the whole codebase —
`eq_commands_movement.cpp`'s `/combat` handler, itself only reachable via
`EverQuest::ProcessChatInput()`, which is **only called from live
chat-window keyboard input** (graphics UI). The headless `--script`
automation used for live testing (`Application::updateScript()` ->
`eqt::action::CommandProcessor`, `src/client/action/command_processor.cpp`)
is a **separate command registry** that does not expose `/combat`, `/hunt`
does not itself call `Enable()`, and neither does `/attack`'s
`EnableAutoAttack()` path — confirmed by reading `command_processor.cpp`,
`action_dispatcher.cpp`, and `eq_action_handler.cpp`, and by observing
`/combat on` produce `[System] Error: Unknown command: combat` live.
`action.cpp`'s own comment documents *why* this split exists (renderer vs.
game-logic command registries; forwarding unresolved commands to the
render-thread-drained `RendererCommand` queue) but that queue's consumer
(`IrrlichtRenderer::processSlashCommand`) is the *renderer's own* debug-
command handler, not `EverQuest::m_command_registry` either — so even a
graphics-mode `--script` run would not reach `/combat`. I attempted a
zero-source-modification workaround (attach `gdb` to the live headless
process and call `CombatManager::Enable()`/`SetAutoHunting()` directly via
expression evaluation) but the `Application` instance is a `main()` stack
local that the build's optimization level elides from the debug info
(`No symbol "app" in current context"` even from the correct frame/thread).
`command_processor.cpp` is not in this unit's territory (only
`combat.cpp`/`combat.h`, the tick wiring, and the T23 loot tests are), so I
did not add a `/combat` command there to unblock this.

Given that, the auto-hunt kill+loot cycle's correctness rests on: (1) the
code citation above proving `UpdateLootState()` is wired unconditionally,
identically to `UpdateMovement()`; (2) the 8 passing unit tests below, which
exercise the exact same production `LootSubState`/`UpdateLootState()` code
the live client would run, including the deadline/quiescence/corpse-gone
paths a short live session likely wouldn't hit anyway; and (3) the live
evidence above that packet dispatch/game-thread liveness holds throughout a
real session under real combat load. I'm flagging the `/combat`-headless
gap explicitly as a finding worth a follow-up unit (expose auto-hunt/combat
toggling to `command_processor.cpp` so future live-loot testing is
scriptable) rather than quietly declaring the live bar met.

### Tests re-pointed/added (`tests/test_combat_manager.cpp`)

- `CombatManagerLoot.BUG_CheckAutoLootBlocksCallingThreadForOverASecond` ->
  renamed `CheckAutoLootReturnsPromptlyAndAdvancesAcrossTicks`: asserts
  `CheckAutoLoot()` returns in <50ms (was: asserted >=1000ms), then drives
  the machine across explicit `UpdateLootState()` ticks (backdating
  `m_last_loot_item_time`/`m_loot_state_entered` via the file's established
  T17 `#define private public` technique, same idiom as the existing
  `ProcessConsiderResponse` fixture) through
  `AwaitingItems -> LootingItems -> Closing -> Idle`, asserting the item is
  actually sent and the combat state returns to non-LOOTING at the end.
- `AwaitingCorpseDeadlineExpiresWithoutHanging` (new): no corpse ever
  staged; confirms the machine sits in `AwaitingCorpse` before the deadline
  and exits to `Idle` (not a hang) once backdated past it.
- `AwaitingCorpseFindsCorpseAndBeginsLootRequest` (new): stages a matching
  corpse entity via `ReplayZone`; confirms one `UpdateLootState()` tick
  finds it and transitions to `AwaitingItems` with the right corpse id.
- `CorpseDisappearingMidLootAbortsWithoutHanging` (new): corpse id never
  staged as an entity (stands in for "despawned" — `CorpseStillPresent()`
  can't distinguish "never existed" from "removed"); confirms
  `AwaitingItems` aborts to `Idle` instead of hanging.
- `AwaitingItemsHardDeadlineClosesWhenAutoLootDisabled` (new): auto-loot
  disabled with a present corpse and a staged item — quiescence never fires
  (gated on `m_auto_loot_enabled`, matching the legacy `CheckAutoLoot`
  guard), so this is exactly the "would have hung forever pre-T26" case;
  confirms the hard deadline closes it anyway.

Verification bar results: `cmake --build build -j24` clean (after
rebuilding the worktree's stale patched Irrlicht via
`build-gles2-x86.sh --irrlicht-only` — unrelated pre-existing staleness,
not a T26 regression: confirmed by reproducing the same link failure on a
stashed/pre-T26 tree); `ctest --test-dir build` 1793/1794 (only the
documented `AudioManagerTest.PreloadCommonSounds` ALSA exception, above the
1789 baseline); `test_session_replay`'s
`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline` passes,
golden file untouched (`git status` confirms only the 4 intended files
changed); `check_file_sizes.sh fable` exit 0, no FAILs; visual gate PASS
(`qeynos2`/`gfaydark`/`befallen` all verified); `build-arm-noble.sh`
succeeds, binary produced.

### Deviations

- Merged the plan's suggested "RequestingLoot" state into "AwaitingItems"'s
  entry (see above) — flagged, not silent.
- Added a hard deadline (`kAwaitingItemsHardDeadlineMs`) with no legacy
  equivalent — a strict improvement in the direction the plan's design
  ruling already pointed ("every state needs both a deadline and a
  corpse-disappeared exit"), not a scope change to the loot path itself.
- Converted the fifth, plan-uncited `ProcessLootWindow()` sleep (dead code
  in production) for consistency — flagged above.
- `LootCorpse(corpse_id)` now also clears `m_loot_items` on entry (belt-
  and-suspenders; `CloseLootWindow()` already clears it at the end of every
  prior session, so this should be a no-op in practice, but it removes a
  latent "stale items from a previous session" edge case the sub-state
  machine doesn't otherwise guard against).
- Live verification of the actual kill+loot cycle is evidence-by-proxy
  (unit tests + code citation) rather than a live packet capture of a real
  auto-loot session — see the finding above for exactly why, and what I
  did verify live instead.
