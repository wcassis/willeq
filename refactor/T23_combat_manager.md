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

(Executor fills: coverage per priority, fixture techniques used, the
loot-thread investigation result, bugs pinned, behaviors skipped as
unreachable and why, deviations.)
