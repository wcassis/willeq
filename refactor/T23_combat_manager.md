# T23 — CombatManager coverage (spell/combat batch, unit 1 of 3)

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY.** No `src/`, no `include/`. Will chose
**Option A** from `refactor/T23_spell_combat_seam_scoping.md`: cover this
layer through the surface that already exists, do NOT extract an
interface. If you conclude a source change is unavoidable, STOP and
report — that report is itself the evidence that would justify Option B.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly, then
read the scoping doc above — it contains the verified facts this plan
builds on and you should not re-derive them.

## Why this unit

`src/client/combat.cpp` is 1864 lines with no direct test binary. It is
the largest single uninstrumented non-graphics TU left after T13–T22.

## The seam question is already settled — do not re-open it

Verified by probe (scoping doc): a bare
`EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar")`
constructs a **live** `CombatManager`, reachable via the existing public
`eq.GetCombatManager()`. No seam, no source change, no network.

## Fixture recipe (assembled from landed units — reuse, don't reinvent)

1. **Entities**: `CombatManager::SetTarget` scans `EverQuest::GetEntities()`
   and returns false for an absent id (`combat.cpp:221`) — verified. Stage
   the entity table by replaying spawn packets. `tests/test_zone_entities.cpp`
   already has `WriteSpawnStruct` / `NewSpawnPayload` / `EncodePos`;
   promote what you need into a shared header rather than copying it
   (`tests/zone_stream_test_utils.h` is the natural home, but do NOT
   change its existing contents).
2. **Player position / level / HP**: these come from the server in
   production. Reach them the same way — replay the relevant zone packets
   (see `test_zone_entities.cpp` for `HPUpdate`, and `eq.cpp`'s
   PlayerProfile handler for the profile path). If a needed precondition
   has no reachable packet path, say so in the Review rather than
   inventing an accessor.
3. **Outbound sends**: there is no socket, so sends are suppressed and
   counted. `EverQuest::GetReplaySuppressedSendCount()` (`eq.h:720`) makes
   "this action attempted to send a packet" assertable. Use deltas around
   a call, not absolute values.
4. **Bridge events**: `TestBridge` in `tests/zone_stream_test_utils.h`
   captures game→renderer events for assertions.

## Priorities (stop when full; a follow-on unit is fine)

1. **Targeting.** `SetTarget(id)` hit and miss (the empty-table no-op is
   REAL behavior — pin it), `SetTarget(name)`, `ClearTarget`,
   `HasTarget`/`GetTargetId`, and the `OP_TargetMouse` send attempt.
2. **Auto-attack.** Enable/disable/toggle and `IsAutoAttackEnabled`;
   auto-fire is a separate flag pair — check they do not alias.
3. **Target cycling.** `CycleTargets(true/false)` over a staged
   multi-entity table: order, wrap-around, and behavior with 0 or 1
   candidates. `m_cycle_index` starts at -1; pin what that means.
4. **Consider.** `ConsiderTarget()` and `ProcessConsiderResponse(...)` —
   the response path is a pure state update, ideal to assert.
5. **Combat state machine.** `SetCombatState`/`GetCombatState` across the
   7 `CombatState` values, plus the derived predicates (`IsFleeing`,
   `IsLooting`) and `SetFleeThreshold`/`ShouldFlee`.
6. **Config round-trips.** Assist range, aggro radius, combat range,
   auto-loot, auto-movement — cheap, and they pin the accessor wiring.

## Timing — read this before writing a single test

`CombatManager` reads `steady_clock::now()` in ~12 places and there is
**no injectable clock**. `Update()` is wall-clock gated (e.g. target
scans only every 1000ms, `combat.cpp:146`).

**Do NOT write sleep-based tests.** Drive the deterministic entry points
directly. If you need to exercise a time-gated branch, the T17 precedent
is available: `#define private public` around the include and backdate
the private timestamp members (see `tests/test_daybreak_state_machine.cpp`
for how that was done cleanly). Prefer the direct entry points; use the
backdating technique only where a behavior is otherwise unreachable, and
say which you used.

## Known hazard — blocking sleeps in the loot path (likely finding)

`LootAll()` calls `std::this_thread::sleep_for` **100ms per item** plus
**500ms** at the end (`combat.cpp:818/825`), and `ProcessLootWindow()`
sleeps another **300ms** before auto-looting (`combat.cpp:1843`). These
are real blocking sleeps on the calling thread.

Two consequences:

- **Test cost**: a 10-item `LootAll()` burns ~1.5s of wall clock. Keep
  loot fixtures to 1–2 items so the binary stays far under the
  5-second-per-binary bar, and report the runtime you measure.
- **Probable real defect**: if this runs on the game thread, looting
  stalls the game loop for over a second — network ticks included.
  **Investigate which thread reaches these calls** (trace back from
  `EqActionHandler::lootAll` / the action layer) and if it is the game
  thread, **pin it with a clearly-named test and report it prominently.
  Do NOT fix it.** This is the strongest bug candidate in the file.

## STOP conditions

- Any `src/`/`include/` change needed → STOP and report (see header).
- A real bug found → pin with a `BUG_`-named test documenting current
  behavior, report prominently, do NOT fix.
- A behavior that cannot be reached without a live server or a new
  accessor → skip it, name it in the Review. Do not invent fixtures
  under `data/`.

## File layout

`tests/test_combat_manager.cpp` (≤800 lines; split by topic if it grows).
Note `tests/test_combat.cpp` already exists and covers combat *enums and
structs* only — do not disturb it. Wire the new target into
`tests/CMakeLists.txt`; it needs `willeq_core` (follow
`test_zone_connection_flow`'s block, including the Irrlicht include-dir
guard).

## Verification bar (tests-only unit, x86)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — full suite;
   known exceptions: `AudioManagerTest.PreloadCommonSounds` (no ALSA)
   and the `RDPServerFrameTest`/`RDPServerLifecycleTest` flake family
   under parallel load (root-caused in T03b, perf territory, NOT yours —
   rerun isolated before reporting it). Baseline currently 1626/1628.
3. New binary's own runtime: report it; must be well under 5s.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

## Review

**Status: DONE.** New binary `tests/test_combat_manager.cpp` (611 lines),
37 tests, all passing. Base commit `1d69419` (fable tip at start).

### Verification bar

1. `cmake --build build -j24` — clean, no warnings from new code.
2. `ctest --test-dir build --output-on-failure` — 1664/1665 passed. The
   single failure is the documented known host exception
   (`AudioManagerTest.PreloadCommonSounds`, no ALSA). No
   `RDPServerFrameTest`/`RDPServerLifecycleTest` flake observed this run.
   `test_combat_manager`'s own 37 tests: 37/37 passed both standalone and
   under ctest.
3. New binary runtime: **1.1–1.2s total** (measured both via direct
   invocation and `ctest -R CombatManager`). Effectively all of it is the
   single intentional `BUG_` sleep-pin test (~1.13s); every other test
   completes in <1ms. Well under the 5s bar.
4. `check_file_sizes.sh fable` — exit 0, no new FAILs.

### Coverage per priority

1. **Targeting** — full: `SetTarget(id)` hit/miss (including the
   empty-table no-op, pinned as real behavior), `SetTarget(name)` exact and
   partial match and miss, `ClearTarget`, `HasTarget`/`GetTargetId`,
   `ConsiderTarget` with/without a target.
2. **Auto-attack** — full: enable requires a target (no-op otherwise),
   enable sets `COMBAT_STATE_ENGAGED`, disable clears the flag and returns
   to idle only if state was engaged (pinned: disable has no "already off"
   guard, unlike enable). Auto-fire: requires target, forcibly disables
   melee auto-attack when enabled (pinned as real coupling, not aliasing —
   both flags independently readable and one doesn't silently mirror the
   other), toggle flips correctly.
3. **Target cycling** — full: 0/1/N candidates, forward order (nearest
   first) with wraparound, backward wraparound, and what `m_cycle_index ==
   -1` means in each direction (forward advances -1→0 i.e. nearest;
   backward wraps -1→-2→size-1 i.e. farthest). Self-exclusion and
   corpse-exclusion both pinned with real fixtures (corpse via an actual
   `HC_OP_Death` packet — `is_corpse` is never set by spawn parsing itself,
   only by the `HC_OP_Death`/`HC_OP_MobRename` handlers, so a synthetic
   "is_corpse-flagged spawn" isn't reachable and a genuine death replay was
   used instead).
4. **Consider** — full: `ConsiderTarget` (send-path only, see the send-
   counter deviation below), `ProcessConsiderResponse` as a pure state
   update verified through `GetHighestPriorityTarget()` (con_color mapping,
   faction, cur_hp/max_hp, has_consider_data flip), and the unknown-id
   no-op. This needed the T17 `#define private public` backdating
   technique to force `Update()`'s 1000ms scan gate open without sleeping
   (see Timing below) — the only test in the file that uses it.
5. **Combat state machine** — full: all 7 `CombatState` values round-trip,
   same-state re-set is harmless, `IsFleeing`/`IsLooting` track state
   exactly, `ShouldFlee` default-false at full HP, `SetFleeThreshold` and
   `UpdateCombatStats` both feed it correctly.
6. **Config round-trips** — partial, see deviation below: auto-loot (both
   `EnableAutoLoot` and the duplicate `SetAutoLootEnabled` setter, both
   confirmed to hit the same underlying flag) and auto-movement fully
   round-trip. Assist range / aggro radius / combat range do **not** — see
   below.

### Fixture techniques used

- Spawn injection via `ReplayZone(eq, HC_OP_NewSpawn, ...)`, promoted from
  `test_zone_entities.cpp` (T18) into `zone_stream_test_utils.h` as
  `MakeEq`/`EncodePos`/`WriteSpawnStruct`/`NewSpawnPayloadAt`. Deliberately
  NOT deduplicated against `test_zone_entities.cpp`'s own (internal-linkage,
  anonymous-namespace) copies — that file is untouched, only new content
  was appended to the shared header, per the plan's "do NOT change its
  existing contents."
- A genuine `HC_OP_Death` packet to stage a real corpse (see priority 3
  above) rather than inventing an `is_corpse` accessor.
- T17-style `#define private public` / `#define protected public` around
  the *first* inclusion of `client/combat.h` (before `eq.h` transitively
  re-includes it — `#pragma once` makes the first inclusion's access
  specifiers stick for the whole TU), used for exactly one thing: backdating
  the private `m_last_target_scan` timestamp so a single `Update()` call
  runs `ScanForTargets()` instead of being gated out, for the
  `ProcessConsiderResponse` test. No other test needs private access.

### Deviation from plan: `GetReplaySuppressedSendCount()` does not observe CombatManager's sends

The plan (and the scoping doc) expected `EverQuest::GetReplaySuppressedSendCount()`
to make "this action attempted to send a packet" assertable for
CombatManager's sends (`SetTarget` → `HC_OP_TargetMouse`, `EnableAutoAttack`
→ `HC_OP_AutoAttack`/`HC_OP_AutoAttack2`, etc.), the same way it does for
T04/T05/T18's S->C-handler-triggered replies. **Verified empirically, this
does not hold.** The counter is only incremented by the internal
reply-helper methods used inside `ZoneProcess*` S->C handlers
(`eq.cpp:1630` and its ~9 siblings). CombatManager (and `TradeManager`,
etc.) instead call the generic public
`EverQuest::QueuePacket(uint16_t opcode, DynamicPacket*)` overload
(`eq.cpp:3672`), which silently `return`s with **no side effect at all**
when `m_zone_connection` is null (the always-true case in these tests) —
no counter, no log, nothing observable. First discovery run had 5 tests
fail on this exact assertion with `GetReplaySuppressedSendCount()` staying
at 0 across calls that unquestionably build and attempt to queue a packet
(confirmed by reading `SetTarget`/`EnableAutoAttack`/`ConsiderTarget`
source). Fixed by dropping the send-count assertions in those 5 tests
(`SetTargetById_Hit`, `ClearTarget`, `ConsiderTarget_WithTargetDoesNotCrash`
[renamed], `EnableWithTarget`, `DisableIsHarmlessRegardlessOfPriorState`
[renamed]) and keeping only the state-based assertions, with a comment
explaining why. This is a real accessor gap, not a design flaw in this
unit's tests — if future units need to assert CombatManager/TradeManager
packet attempts, `GetReplaySuppressedSendCount()` needs a matching
increment added to `EverQuest::QueuePacket(opcode, packet)` (a `src/`
change, out of this unit's territory).

### Deviation: assist range / aggro radius / combat range have no getters

`SetAssistRange`/`SetAggroRadius`/`SetCombatRange` are setter-only — there
is no public accessor to round-trip against, and their only observable
effect is inside the wall-clock-gated `Update()`→`ScanForTargets()` path
(itself only reachable via the same backdating technique used for
`ProcessConsiderResponse`, which would be a disproportionate amount of
machinery for what the plan called "cheap"). Per the plan's STOP condition
("cannot be reached without ... a new accessor -> skip it, name it in the
Review"), these three are skipped as true round-trips; only a
call-does-not-crash smoke test is included
(`CombatManagerConfig.SettersWithoutGettersDoNotCrash`). No `src/`/`include/`
change was made or is being requested — this is a note, not a STOP.

### Loot-thread investigation (the flagged hazard)

**Confirmed: this is a real, live, game-thread-reachable defect.** Full
trace (code-read, not inferred):

1. `ProcessCombatRound()` (`combat.cpp:1192`) calls
   `CombatManager::LootCorpse()` directly when an auto-hunted target dies
   with auto-loot enabled (the default). This path does **not** set
   `EverQuest::m_player_looting_corpse_id` — that variable belongs to the
   separate UI-driven loot-window flow.
2. When the server's loot-item response (`OP_ItemPacket`) arrives,
   `eq.cpp`'s zone dispatch switch (~`eq.cpp:1020-1023`) branches on
   `m_player_looting_corpse_id == 0`: with it still 0 (auto-hunt path),
   and `m_combat_manager->IsLooting()` true, it takes the "Headless mode
   looting" branch and calls `ZoneProcessLootItem(p)` directly, synchronously,
   inline in the packet dispatcher.
3. `ZoneProcessLootItem` (`eq_combat_actions.cpp:527`) calls
   `m_combat_manager->CheckAutoLoot()` — also direct, synchronous, no
   thread hop.
4. `CheckAutoLoot()` sleeps 500ms, then calls `LootAll()`, which sleeps
   100ms per item plus 500ms at the end (`combat.cpp:818/825/1843`).
5. Per `CLAUDE.md`'s threading model, all network packet handling
   ("Network events (TickNetwork)") runs on the **Game Thread**, not the
   Render Thread. There is no dispatch to another thread anywhere in this
   chain.

**Net effect**: an auto-hunting client with auto-loot enabled (the
default) stalls its own game thread — including further zone packet
processing — for >=1.1s per kill with even a single loot item, worse with
more. Pinned, not fixed, per instruction:
`CombatManagerLoot.BUG_CheckAutoLootBlocksCallingThreadForOverASecond`
(measures the calling thread's wall-clock block directly around
`CheckAutoLoot()`, asserts >=1000ms and <3000ms as a two-sided pin).

**Side finding, not pinned separately (noted for the record)**: the
*modern* UI-driven "loot all" click (`EverQuest::LootAllFromCorpse`,
`eq_bridge.cpp:1573`, the D18a game-side loot-tracking path) is
**non-blocking** — it queues one slot at a time and re-requests per server
round trip, and does not call `CombatManager::LootAll()` at all. So the
sleep-based `CombatManager::LootAll()`/`ProcessLootWindow()`/`CheckAutoLoot()`
family is dead code for the UI path but very much alive for the
auto-hunting path — worth flagging to Will as a candidate fix (route
auto-hunting's post-kill loot through the same non-blocking
`LootAllFromCorpse` sequencing instead) even though this unit does not
implement it.

### Behaviors skipped as unreachable, and why

- `SetAssistRange`/`SetAggroRadius`/`SetCombatRange` true round-trips — no
  getters (see deviation above).
- Packet-attempt assertions for any CombatManager-initiated send — no
  observable counter for the `QueuePacket(opcode, packet)` overload (see
  deviation above); only the resulting state changes are asserted.
- Spell casting (`CastSpell`, `InterruptCast`, `MemorizeSpell`), abilities
  (`UseAbility`, `Taunt`), fleeing mechanics beyond the state
  predicate/threshold (`InitiateFlee`, `SelectBestFleePath`), auto-hunting
  (`SetAutoHunting`/`UpdateAutoHunting`/`FindNextHuntTarget`), and
  `GetNearbyHostiles`/`GetNearbyAllies`/`GetHighestPriorityTarget` beyond
  what the Consider tests exercise — all out of scope per this unit's
  priority list (T24/T25 territory or not listed), not because they're
  unreachable.
- `LootItem`/`LootCorpse`/`LootAll`/`CloseLootWindow`/`ProcessLootWindow`
  beyond the single `CheckAutoLoot` BUG_ pin — deliberately minimal per the
  plan's "keep loot fixtures to 1-2 items" guidance; the pin itself already
  demonstrates the mechanism end to end.

### Deviations summary

- `GetReplaySuppressedSendCount()` doesn't cover CombatManager's sends
  (see above) — 5 tests adjusted, noted inline and here.
- Assist/aggro/combat range have no getters — skipped as true round-trips,
  smoke-tested only.
- `zone_stream_test_utils.h` gained new promoted helpers (additive only,
  existing content untouched) rather than a fully deduplicated single
  source of truth with `test_zone_entities.cpp` — matches the plan's
  explicit instruction not to touch that file's existing contents.
- No `src/`/`include/` change was made or found necessary — the Option A
  seam ruling holds up under actual test-writing pressure, with the one
  accessor-gap caveat above (a possible, but not urgent, future addition
  to `EverQuest::QueuePacket`).
