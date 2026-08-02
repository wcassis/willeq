# T28 — Fix the live ZoneChanged position bug + repoint the dead-state readers

**Slot: slot1. x86 only. No eqemu server, no Pi.** Sibling unit: T26 is
running in t1 and owns `src/client/combat.cpp`, `include/client/combat.h`,
the game-thread tick wiring, and `tests/test_combat_manager.cpp` — stay
out of those files. If you need to READ `combat.h` for
`CombatManager::GetTargetId()`'s signature, that is fine; do not modify it.

**Territory: `src/client/state/world_state.cpp`,
`src/client/state/inventory_state.cpp`, `src/client/state/combat_state.cpp`,
`src/client/eq_commands.cpp`, and the T02 pinned tests in
`tests/test_state_modules*.cpp`.**

Read `refactor/EXECUTOR_PROTOCOL.md` first, then the deferred.md section
**"Dead parallel state copies in the state layer, WITH LIVE READERS
(orchestrator, 2026-08-02)"** — that entry is the verified ground truth
for this unit and its grep commands are re-runnable. Do not re-derive it;
do spot-check it.

Will's ruling 2026-08-02, in his words: **"I still want the migration but
doing b now and a later sounds correct."** So this unit does the (b) half
— stop the live breakage, remove the double-publish trap — and must leave
the (a) migration (making the state layer authoritative) *easier*, not
harder. **That is the governing constraint: do not delete scaffolding the
migration will need.**

## Part 1 — `WorldState::fireZoneChangedEvent()` publishes the wrong position (REAL LIVE BUG)

Unlike Parts 2 and 3, this code genuinely runs in production.
`world().setZone()` is called at `eq.cpp:1909`, and
`fireZoneChangedEvent()` (`world_state.cpp:105-116`) fills
`data.x/y/z/heading` from the **pending-zone** members
(`m_pendingZoneX/Y/Z/Heading`) rather than the actual arrival position.
`setZone()` takes no position arguments, so calling it alone publishes
whatever the pending fields happen to hold — `(0,0,0)` on the common path.

It has survived because there are **two channels**: immediately after,
`eq.cpp:1912-1922` pushes its own `ZoneChanged` onto the **bridge** with
the correct `m_safe_x/y/z`. `irrlicht_bridge`/`console_bridge` consume
that correct copy; the **EventBus** subscriber
(`console_renderer.cpp:102,199`) consumes the wrong one.

Fix it so the EventBus payload carries a defensible position. Your call
on shape, justified in the Review — the obvious candidates are: have
`setZone()` take the position (updating the single caller), or publish
from the authoritative current-position members rather than the pending
ones. **What is NOT acceptable is leaving two publishers disagreeing
about the same event.** If you conclude the right answer is that
`WorldState` should not publish this event at all because `eq.cpp`
already does, say so with evidence and STOP for a ruling rather than
deleting a publisher unilaterally.

Verify the fix by observing the event, not by reading the code: the
`console_renderer` subscriber is the live consumer.

## Part 2 — repoint the four broken `/assist`-class readers (the (b) fix)

`combat().setTarget()`/`clearTarget()`/`setAutoAttacking()` have **zero
production call sites**, so `CombatState`'s target fields are permanently
zero. Four production sites read them anyway:

| Site | Reads | Effect today |
|---|---|---|
| `eq_commands.cpp:887` | `combat().targetId()` | `/assist` with no args always prints "You don't have a target." |
| `eq_commands.cpp:945` | `combat().targetId()` | same class |
| `eq_commands.cpp:974` | `combat().targetId()` | same class |
| `eq_commands.cpp:1020` | `combat().targetId()` | same class |

All four live in `EverQuest`, which owns `m_combat_manager`. Repoint them
at the authoritative source: `CombatManager::GetTargetId()` (this is what
`eq_bridge.cpp` uses throughout — ~lines 763, 832, 906, 958, 1252, 1292,
1373). Null-check `m_combat_manager` the way the existing `eq_bridge`
call sites do.

**Confirm the behaviour change is real before and after** — with a target
set, `/assist` must stop claiming there is no target. Read each of the
four sites' surrounding logic rather than pattern-replacing: they are not
guaranteed to be identical, and one of them may want `hasTarget()`
semantics rather than a raw id.

### The fifth reader is deliberately OUT of scope — do not "finish the job"

`action/command_processor.cpp:771` (`cmdHail`) reads
`m_state.combat().hasTarget()`. `CommandProcessor` holds only
`state::GameState&` and `ActionDispatcher&` (`command_processor.h:67,161`)
— it has **no access to `CombatManager`, by design**: T12 established the
action layer as pure logic decoupled from `EverQuest`. Wiring
`CombatManager` into it to fix one branch would undo that decoupling and
fight the (a) migration Will still wants.

**Leave that site's behaviour unchanged.** Add a brief comment naming the
deferred.md entry and the fact that it is waiting on the state-layer
migration. Its current effect is mild (`/hail` always takes the
non-target branch). **If you find yourself adding a constructor parameter
or an include to the action layer, STOP — that is the (a) unit's call,
not yours.**

## Part 3 — make the dead scaffolding honest, but DO NOT delete it

`InventoryState::fireInventoryChangedEvent()` hardcodes
`event.hasItem = true; event.itemId = 0` (`inventory_state.cpp:204-205`)
regardless of the slot state passed in. Its three callers
(`setEquipmentOccupied`/`setGeneralOccupied`/`setBankOccupied`) have zero
production call sites and no live readers were found, so this is
currently unreachable — the *live* `InventorySlotChanged` events, enriched
by S2, come from the `eq_bridge` path instead.

Because Will wants the migration later, **do not delete these setters or
the occupancy arrays** — they are what (a) will populate. Instead:

- Fix the payload so it reports the actual state it was given
  (`hasItem` from the occupancy value; leave `itemId` alone if the state
  layer genuinely does not know it, and say so in a comment rather than
  emitting a confident wrong value).
- Same treatment for `CombatState::fireTargetChangedEvent()` /
  `fireCombatEvent()` (`combat_state.cpp:65-77`): **leave them as
  non-publishing**, but replace the stale comment. It currently claims
  "we don't have a corresponding data struct", which is false —
  `TargetChangedData` is at `event_bus.h:384`. The honest comment is that
  `TargetChanged` is already published by `eq_bridge.cpp:589`/`:771` and
  `eq_entities.cpp:1728`, that `eq_action_handler.cpp:21` records a
  deliberate no-duplicate decision, and that publishing here would
  double-fire until the (a) migration consolidates ownership.

**Making these stubs publish is explicitly NOT this unit's job.** That is
the trap the deferred entry documents.

## Tests

The three T02 pins in `tests/test_state_modules*.cpp` assert the broken
behaviour and will now be wrong in part:

- `WorldStateTest.ZoneChangedEventPositionComesFromPendingZoneNotSetZoneArgs`
  — re-point to assert the corrected position.
- `InventoryStateTest.InventorySlotChangedEventAlwaysReportsHasItemTrue`
  — re-point to assert the payload reflects real state.
- `CombatStateTest.KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent`
  — **keep it**, but rename/re-comment so it reads as an intentional,
  documented gap owned by the migration rather than an unexamined one.

Add coverage for the repointed `/assist`-class readers if a test binary
can reach them; if `eq_commands.cpp` is not in one, say so in the Review
rather than inventing a seam — that is a finding, not a task.

## STOP conditions

- Fixing Part 1 would require changing which component publishes
  `ZoneChanged` → STOP and report; that is a ruling, not a judgement call.
- Any change to the action layer's dependencies (Part 2's fifth reader)
  → STOP.
- Any of the four `eq_commands.cpp` sites turns out NOT to be broken as
  described (e.g. a sync you find that the deferred entry missed) → STOP
  and report; the entry's grep evidence would then be wrong and I want to
  know before more is built on it.
- Any behaviour change beyond the targets named here → STOP.

## Verification bar (FULL — source-touching and behavioural)

1. `cmake --build <slot>/build -j24` clean. **Note (T27 finding): this
   slot may carry a stale `third_party/irrlicht-gles2-x86/libIrrlicht.a`
   predating `3328570`, which breaks linking of `willeq` and every
   `willeq_core` test binary with missing `eqtGPUPhaseBegin` symbols. If
   you hit that, rebuild it with
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only` — it is a
   stale artifact, not your bug.**
2. `ctest --test-dir <slot>/build --output-on-failure` — known exceptions:
   `AudioManagerTest.PreloadCommonSounds` (ALSA host quirk) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (a known unfixed
   product use-after-free in perf territory, NOT a flake — rerun isolated
   and say explicitly that is what you saw).
3. **Golden replay**: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq` — must
   pass with the golden file **unchanged**. If the corrected `ZoneChanged`
   position moves the golden, that is a real finding: report it, do not
   re-bless.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new FAILs.

## Review

**Part 1 — fix shape.** Chose "have `setZone()` take the position" (the
plan's first candidate), not "publish from authoritative current-position
members," because `WorldState` had no such members — only the *pending*
zone fields (a different, still-needed concept: an in-flight zone request)
and `m_lastZoneCheckX/Y/Z` (zone-line-detection bookkeeping, semantically
unrelated to arrival position). Added `x, y, z, heading` params to
`WorldState::setZone()`; it stores them in new `m_zoneArrival{X,Y,Z,Heading}`
members, and `fireZoneChangedEvent()` now reads those instead of
`m_pendingZone*`. The single production caller, `eq.cpp:1909` (now
`:1915-1916`), was updated to pass `m_safe_x/y/z` — the exact same values
its very next block (`eq.cpp:1912-1922`, unchanged) already uses for the
bridge publish — and heading `0.0f`, matching that same bridge publish's
hardcoded `data.heading = 0.0f` (this client doesn't track arrival heading
yet; that's pre-existing and out of this unit's scope). The two channels
now agree by construction, not by coincidence. Did NOT change which
component publishes `ZoneChanged` — `WorldState` still publishes on the
EventBus, `eq.cpp` still publishes on the bridge; STOP condition not
triggered. One other call site existed, a test at
`test_state_modules_secondary.cpp:648` (`GameStateTest.
PlayerPositionAndCurrentZoneNameConvenienceAccessors`) plus
`SetZoneFiresOnlyOnChange` (lines 270/272/274) — both updated to the new
signature with explicit `0.0f` args (they don't assert position, only
name/id and fire-on-change semantics, so this is a mechanical signature fix,
not a behavior change).

**Part 1 — verification, "observe the event, not the code."** Ran
`test_session_replay`'s `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`
directly (see Verification table below) — that test drives a real
`console_renderer`-style EventBus consumption path via
`WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq`. Golden
checksum before and after: `4d55b40a672cec0ae6022f60a21b7b25` (unchanged) —
the corrected position did NOT move the golden. This is consistent with the
recorded session's zone transitions landing at a position the golden
snapshot doesn't independently re-derive from the EventBus `ZoneChanged`
payload (i.e., the golden is not sensitive to this specific field), not
proof the fix is a no-op — the repointed unit test
(`WorldStateTest.ZoneChangedEventPositionComesFromSetZoneArgsNotPendingZone`,
below) is the direct evidence the payload itself changed.

**Part 2 — repointed readers, before/after.** All four `eq_commands.cpp`
sites read `m_game_state.combat().targetId()` — the dead `CombatState`
target copy, permanently 0 in production — and are now
`(m_combat_manager && m_combat_manager->HasTarget()) ? m_combat_manager->GetTargetId() : 0`,
matching the `eq_bridge.cpp` idiom named in the plan.
  - `/assist` (no args, `eq_commands.cpp:883-904`): before, ALWAYS printed
    "You don't have a target." even with a real target set via
    `CombatManager`. After, only prints it when `CombatManager` genuinely
    has no target. New test
    `EqCommandsAssistTest.AssistNoArgs_WithCombatManagerTarget_DoesNotPrintNoTargetMessage`
    proves this — it fails against the pre-fix code (verified by construction:
    the pre-fix read was unconditionally 0).
  - `/shield` (`:939-952`): same class, message "You don't have a target to
    shield." — same before/after, covered by
    `ShieldWithCombatManagerTarget_DoesNotPrintNoTargetMessage`.
  - `/duel` (`:968-981`): same class, message "You don't have a target." —
    covered by `DuelWithCombatManagerTarget_DoesNotPrintNoTargetMessage`.
  - `/bandage` (`:1013-1026`, `hasTarget()` semantics per the plan's warning
    — this one does NOT print an error; it silently falls back to
    self-target `m_my_spawn_id` when `target_id == 0`). Before, it always
    self-targeted even with a real target set. After, it bandages the real
    target when one exists. Covered by
    `BandageWithCombatManagerTarget_RunsCleanly` (can't independently observe
    the packet's target-id argument without a live connection, so this
    confirms clean execution with a target set; the shared repointing logic
    is directly proven by the other three tests).

  None of the four turned out to already be synced by some mechanism the
  deferred entry missed — confirmed by reading each site's full surrounding
  logic (not pattern-replaced blind), and independently by the new tests:
  every "no target" test still gets the no-target message with no
  `CombatManager` target set, so the old dead-read behavior for the
  "genuinely no target" case is preserved exactly, only the "have a target"
  case changed. STOP condition ("any of the four turns out NOT broken as
  described") not triggered.

**Part 2 — fifth reader.** `action/command_processor.cpp:771` (`cmdHail`)
left behaviourally untouched — added only a comment naming the
deferred.md entry and why it's out of scope (T12 decoupling). No
constructor parameter, no include added to the action layer. Confirmed by
`git diff` on that file: the added lines are a comment block only, the
`if (m_state.combat().hasTarget())` line itself is unmodified text.

**Part 3 — InventoryState.** `fireInventoryChangedEvent()` gained a second
parameter (`bool hasItem`); its three callers (`setEquipmentOccupied`/
`setGeneralOccupied`/`setBankOccupied`) now pass their `occupied` argument
straight through instead of the hardcoded `true`. `itemId` is deliberately
left at 0 with a comment explaining this state layer only tracks occupancy,
not item identity — not silently claiming a real value. Setters/arrays NOT
deleted (migration-preserving, per governing constraint). Confirmed still
zero production call sites (unchanged from the deferred.md finding) — this
fix is honest-but-still-unreachable, exactly Part 3's brief.

**Part 3 — CombatState.** `fireTargetChangedEvent()`/`fireCombatEvent()`
left as non-publishing stubs (bodies still just comments, `m_eventBus`
still unused in the body — this was checked, not "fixed" into publishing).
Replaced the stale "we don't have a corresponding data struct" comment
with the real reasoning: `TargetChangedData` exists, three live publishers
already cover `TargetChanged`, `eq_action_handler.cpp:21`'s no-duplicate
decision, and that publishing here would double-fire pending the
migration. No production behavior change (setTarget/clearTarget/
setAutoAttacking still have zero production callers, confirmed unchanged).

**Tests re-pointed** (all three T02 pins, per plan):
- `WorldStateTest.ZoneChangedEventPositionComesFromPendingZoneNotSetZoneArgs`
  → renamed `...ComesFromSetZoneArgsNotPendingZone`, now asserts the
  corrected position/heading come from `setZone()`'s new args and that a
  differently-valued pending zone does NOT leak into the published event
  (the regression this bug used to cause).
- `InventoryStateTest.InventorySlotChangedEventAlwaysReportsHasItemTrue` →
  renamed `...ReportsActualOccupiedState`, now asserts `hasItem` tracks the
  real occupied value both ways (true→true, false→false).
- `CombatStateTest.KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent` →
  renamed `DELIBERATE_GAP_SetTargetDoesNotPublishPendingMigration`, same
  assertion (0 events), re-commented to explain WHY this is correct
  (double-publish risk) rather than reading as an unexamined gap.

**New coverage added**: `tests/test_eq_commands_assist.cpp` (new file,
new CMake target `test_eq_commands_assist` in `tests/CMakeLists.txt`) — 7
tests covering all four repointed readers' before/after behavior via
`EverQuest::ProcessChatInput()` (the same public entry point production
chat input uses) + a `TestBridge` to observe `AddChatSystemMessage()`'s
`SystemMessage` event, same idiom as `test_zone_chat.cpp`. Confirmed
`eq_commands.cpp` IS reachable from a test binary (compiled into
`willeq_core`, already linked by `test_combat_manager`) — this was a real
finding worth recording since the plan said to report rather than invent a
seam if none existed; one did exist (`ProcessChatInput` + `MakeEq()`, the
T23 seam), so coverage was added rather than skipped.

**Deviations**: none from the plan's instructions. One documented judgment
call (Part 1's fix shape, `setZone()` taking position args vs. some other
shape) was explicitly left to the executor by the plan and is justified
above.

### Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | Clean, after rebuilding `libIrrlicht.a` per the T27 note (slot carried the stale artifact; rebuilt via `scripts/build-gles2-x86.sh --irrlicht-only`, then reconfigured with the exact CMake invocation from the protocol to pick up the new `test_eq_commands_assist` target). Full build green. |
| `ctest --test-dir build --output-on-failure` | 1796/1797 passed. Sole failure: `AudioManagerTest.PreloadCommonSounds` — reran isolated, confirmed the known ALSA host quirk (`ALSA lib ... Unknown PCM dmix:0`, no card 0 on this host), not a new regression. `RDPServerFrameTest`/`RDPServerLifecycleTest` family: all 9 passed in the full run AND reran isolated (`-R "RDPServerFrameTest\|RDPServerLifecycleTest"`) — 9/9 passed both times, so the known use-after-free did not manifest this run; noting explicitly per protocol that this is what was observed. |
| Golden replay | `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq ./build/bin/test_session_replay --gtest_filter=*SessionReplayGolden*` → PASSED. `md5sum /mnt/projects/fable/captures/session.golden` before and after: `4d55b40a672cec0ae6022f60a21b7b25` both times — golden UNCHANGED. |
| `scripts/refactor/check_file_sizes.sh fable` | One pre-existing FAIL: `src/client/eq.cpp: 3727 lines (> 3000)`. Verified NOT new — `git show fable:src/client/eq.cpp \| wc -l` = 3720 (already over the 3000 threshold on the fable baseline before this unit; this unit's comment additions to the single `setZone()` call site added the remaining 7 lines). No other FAILs; all touched files well under threshold (`eq_commands.cpp` 1124, `command_processor.cpp` 1200, state-layer files all under 250). |

New test binary `test_eq_commands_assist` also run standalone
(`./build/bin/test_eq_commands_assist`): 7/7 passed.
