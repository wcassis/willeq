# T39 — Finish the Phase 7.x state-layer migration for combat/inventory

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg (lock protocol
applies).** No sibling unit.

**Territory: `src/client/state/combat_state.cpp` +
`src/client/state/inventory_state.cpp` (and their headers), the call
sites in `src/client/eq*.cpp` that own the authoritative target /
inventory-slot flow, `src/client/action/command_processor.cpp`'s
`cmdHail`, and the T02/T28 tests in `tests/test_state_modules*.cpp`.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred.md entry **"Dead
parallel state copies in the state layer, WITH LIVE READERS"** — read it
to the end, it carries a 2026-08-04 correction that materially shrinks
this unit — then `refactor/T28_state_layer_readers.md`'s Review.

Will's ruling 2026-08-03: **"I still want the migration but doing b now
and a later sounds correct."** T28 did (b). This is (a).

## What is actually left, and what the correction changed

The "Phase 7.x — sync to XState" migration was left half-done: setters
were written but never wired, while readers were repointed at the new
home anyway. T28 stopped the bleeding by repointing four `eq_commands.cpp`
readers at the authoritative `CombatManager`, deliberately **without**
deleting the state-layer scaffolding, so this unit has something to build
on.

**Correction that shapes this unit** (orchestrator, 2026-08-04 — verified,
in the deferred entry): an earlier version of that entry warned that
letting `CombatState::fireTargetChangedEvent()` publish would
double-publish, because `TargetChanged` is already pushed from three
sites. **That was wrong.** Those three sites push onto the
**GameStateBridge** (consumed by `IrrlichtBridge` and `ConsoleBridge`);
`CombatState` would publish onto the **EventBus** (consumed by
`ConsoleRenderer`). Nothing else publishes `TargetChanged` to the
EventBus, the bridges do not subscribe to it, and nothing forwards
between the channels — the EventBus is fed exclusively by
`src/client/state/*.cpp`.

So this is **"populate the state and let it publish"**, not "consolidate
three publishers". Confirm that channel separation yourself early; if you
find any consumer reading both, STOP, because the whole shape changes.

## Scope

### 1. Populate `CombatState`'s target from the authoritative flow

`combat().setTarget()` / `clearTarget()` / `setAutoAttacking()` /
`updateTargetHP()` have zero production callers. Wire them where the real
target changes — the same places that already push `TargetChanged` onto
the bridge are the obvious candidates. Keep `CombatManager` authoritative;
the state layer mirrors it. Do not invert that in this unit.

### 2. Let the stubs publish

With state populated, `fireTargetChangedEvent()` and `fireCombatEvent()`
become meaningful. Publish real data and delete the T28 "deliberately
non-publishing" comments, which will no longer be true.

### 3. Same for `InventoryState`'s occupancy half

`setEquipmentOccupied()` / `setGeneralOccupied()` / `setBankOccupied()`
also have zero production callers. T28 already made
`fireInventoryChangedEvent()` report the real occupied value instead of a
hardcoded `true`, so this is the wiring half. If the authoritative
inventory flow turns out not to have a clean place to call these, say so
and leave them — do not invent a synthetic call site to make the arrays
look populated.

### 4. Claim the fifth reader

`action/command_processor.cpp:771` (`cmdHail`) reads
`m_state.combat().hasTarget()`. T28 left it deliberately untouched with a
comment, because `CommandProcessor` holds only `state::GameState&` and
`ActionDispatcher&` by design (T12 decoupled the action layer) and wiring
`CombatManager` in would have fought this migration. Once `CombatState`
is populated, that reader is correct as written — **verify that and
remove the T28 comment**. That is the payoff for having left it alone.

### 5. Tests

`CombatStateTest.KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent` (T02,
kept-and-renamed by T28) now describes fixed behaviour — re-point it.
Add coverage that the events carry real data, and at least one test that
would have failed before this unit.

## STOP conditions

- Any consumer turns out to read **both** the bridge and the EventBus for
  these events → STOP; the channel separation this unit relies on is
  wrong and the design needs a ruling.
- Populating the state requires making `CombatState` authoritative over
  `CombatManager` (rather than mirroring it) → STOP; that is a much
  larger change than Will approved.
- The inventory half has no clean authoritative call site → do items 1/2/4
  and report on 3; do not force it.
- Any gameplay behaviour change beyond event delivery → STOP. Populating
  state and publishing on a channel whose only consumer is
  `ConsoleRenderer` should be observable in headless output, not in play.

## Verification bar

1. `cmake --build <slot>/build -j24` clean; also build the headless
   configure (`EQT_GRAPHICS=OFF`) — `ConsoleRenderer` is the live consumer
   of the channel you are populating.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). **RDP is no longer
   waived (T35) and the golden replay no longer skips (T36)** — a failure
   or a `Skipped` on either is a real signal, not background noise.
3. Golden replay unchanged and confirmed to have RUN.
4. **Acceptance**: a live session (eqemu LOCK FIRST; `up.sh` only;
   restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190)
   driven by piped slash commands, showing (a) `/hail` now taking the
   target branch when a target is set — the concrete user-visible payoff
   — and (b) target changes reaching `ConsoleRenderer`'s output. Evidence
   under `benchmarks/audits/T39/`.
5. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that is a failed verification.

## Review

(Executor fills: where the setters were wired and why there, the
channel-separation confirmation, what the stubs now publish, the
inventory verdict, the `cmdHail` payoff, tests re-pointed, deviations.)
