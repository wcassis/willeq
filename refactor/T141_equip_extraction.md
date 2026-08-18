# T141 — `/equip`: extract the misplaced logic, then the migration is trivial

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands.cpp`, `include/client/inventory/` and its
sources, `include/client/eq.h` + the relevant `eq_*.cpp`,
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T141/`.**

Read T136's, T138's and T140's notes on `equip` — all three STOPped on it.

## Why this STOPped three times, and what to do differently

T136, T138 and T140 each concluded `equip` "needs its own design pass" and
deferred it. **All three framed it as a migration and hit the same wall**: the
handler is large and reaches deep into `InventoryManager`, which is private
with no `EqActionHandler` accessor.

**It is not a migration problem. It is a misplaced-logic problem.**

Measured while scoping: the handler spans **`eq_commands.cpp:751-1001` — 250
lines** (prior units said ~150; they undercounted), and it calls **12 distinct
`InventoryManager` methods** — `getItem`, `setItem`, `hasCursorItem`,
`getCursorItem`, `pickupItem`, `placeItem`, `removeItem`, `getGeneralItems`,
`getEquipmentItems`, `getBagContentsSlots` — plus **4 bridge pushes** and a
free helper `CheckEquipDestination()` (`eq_commands.cpp:349`) that already
takes a `const InventoryManager*`.

**250 lines of inventory manipulation living in a command handler is a layering
error regardless of which registry the command sits in.** Fix that, and the
migration falls out as a thin wrapper — the same shape as the other 16.

## The recommended shape — argue it if you disagree

`InventoryManager` **already owns the legality question**:
`canEquipItem(item, equipSlot)` (`inventory_manager.h:173`). The command
handler owns only the *orchestration* around it.

**Recommendation: move the orchestration into the inventory subsystem**, with
`CheckEquipDestination()` moving alongside it as the companion helper it
already is. Then give `EverQuest` a **thin public method** the action layer
calls, following T136's `Request*` recipe.

**If you conclude the orchestration belongs on `EverQuest` instead** — because
it coordinates bridge pushes and inventory together — that is defensible.
**Argue it in the Review; do not let the choice be made by whichever is easier
to type.**

## A constraint that dissolves if you get the shape right

T136 established that `action::EqActionHandler` **cannot push bridge intents**
— no `GameStateBridge*`. `equip` does **four** bridge pushes, which is a large
part of why it looked impossible from the action layer.

**If the logic lives in `EverQuest` (which already owns `m_bridge`), or in
`InventoryManager` called from `EverQuest`, that constraint stops applying.**
That is a second, independent reason the extraction is the right move rather
than a workaround. **Do not add a `GameStateBridge*` to `EqActionHandler` to
make this work** — that is a much larger architectural change and its own unit.

## Do it in two steps, in this order

**1. Extract, and prove behaviour is unchanged.** `tests/test_eq_commands_equip.cpp`
already exists — it is your safety net. Run it before and after; it should pass
against the extracted logic with only the call path updated.

**2. Then migrate**, atomically per the programme's rule: remove from
`eq_commands.cpp` in the same commit as the `CommandProcessor` registration,
and verify `registry=action` live with a `registry=game` control from the 7
`stay-game` commands.

**Separate commits for the two steps.** If the migration turns out to be
wrong, the extraction should still stand on its own merits — it is worth doing
either way.

## Test handling

`tests/test_eq_commands_equip.cpp` exercises the game-registry seam. Follow
**T140's precedent, not T136's**: **retarget it if the logic survives** in a
new home (T140 did this for `bank`/`train`), and only delete if the seam it
tests genuinely ceases to exist (T135/T136's case). **Say which and why** — a
deleted test file always deserves a stated reason.

## STOP conditions

- The extraction needs `EverQuest` private state **beyond inventory and the
  bridge**. That would mean the logic is more entangled than scoping found —
  report it with specifics.
- Behaviour changes in any way you cannot immediately justify. This is
  user-facing item handling; a subtle change here loses or misplaces items.
- You find yourself wanting to add a `GameStateBridge*` to `EqActionHandler`.

## Verification

- x86 suite (current total **2411**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **The existing equip tests must pass at both steps**, plus new coverage at
  the programme's density for the migration itself.
- **Visual gate goldens must not move** — equipment affects appearance; if one
  moves, **STOP and report**, that is a real signal.
- **Live leg**: actually equip and unequip items. A GM account is available for
  producing them. Item handling bugs are visible in the **DB** (T135/T136's
  lesson) — check `peq.inventory`, not just the zone log.
- **Report wire-verified versus dispatch-layer-only**, as every unit in this
  programme has.

## Review

(executor fills: the measured size of what moved and where it went; the
argument for that home; how `CheckEquipDestination` was handled; confirmation
the bridge-push constraint dissolved rather than being worked around; the
two-commit structure; the existing test's fate with reasoning; live evidence
including DB state; **measured wall-clock**; deviations)

## Board

Say whether `equip` is now closed. If it is, the command-migration programme
reduces to **7 `stay-game` (no work) and 3 `needs-decision` for Will** — say
that plainly. If the extraction revealed something that changes the picture,
say what.
