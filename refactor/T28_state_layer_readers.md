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

(Executor fills: the Part 1 fix shape and why, evidence the EventBus
consumer now sees the right position, the four repointed readers with
before/after behaviour, confirmation the action-layer site was left
alone, what was made honest vs. deleted in Part 3, tests re-pointed,
deviations.)
