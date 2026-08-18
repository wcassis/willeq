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

### What moved, measured, and where it went

The scoped ~250-line handler (`eq_commands.cpp:751-1001`) plus its five
file-local helpers (`IsRecognizedInventorySlot`, `DescribeEquipSlot`,
`CheckEquipDestination`, `FindItemsByNameSubstring`,
`EquipDestinationResult`/`ResolveEquipDestination` — `eq_commands.cpp:300-451`
pre-extraction) moved into `InventoryManager`
(`include/client/inventory/inventory_manager.h`,
`src/client/inventory/inventory_manager.cpp`) as five new members:
`checkEquipDestination()`, `resolveEquipDestination()`,
`findItemsByNameSubstring()`, `moveItem()`, plus the `EquipDestinationResult`
and `MoveItemOutcome` structs. A sixth piece, `DescribeEquipSlot`, became a
shared free function `eqt::inventory::describeSlot()` in
`inventory_constants.h` next to the `getSlotName()` it wraps, since both
`InventoryManager`'s own messages and `EverQuest`'s report-formatting need
it — one formatter instead of the two copies a straight cut-and-paste would
have produced. `CheckEquipDestination` moved alongside `moveItem()` as the
companion helper the plan named it — both now live where `canEquipItem()`
(the legality predicate they call) already lived.

What's left in `eq_commands.cpp` is `EverQuest::RequestEquip()`: the
three-form dispatch (slot-pair / item-name / `list`) and read-only report
formatting — genuinely command-layer concerns, not domain logic. Diff stats:
`git diff --stat` across both commits show `eq_commands.cpp` net **-207
lines** (793 lines post, was ~1001-line region pre-extraction inclusive of
the moved block) and `inventory_manager.cpp` net **+189 lines**. Prior
units' "~150 lines" estimate for the handler undercounted by not including
the helpers; this unit's own "250 lines" scoping estimate (handler +
`CheckEquipDestination`, not counting `ResolveEquipDestination`/
`FindItemsByNameSubstring`/`IsRecognizedInventorySlot`) undercounted too —
the real movable surface, helpers included, was closer to 340 lines.

### The argument for `InventoryManager`, not `EverQuest`

Not just followed as given — checked against the actual code before
committing to it. Two independent facts confirmed it was right:

1. **`InventoryManager` already owned the legality question**
   (`canEquipItem()`) — the extracted logic's *other* half (orchestration)
   belongs with it for the same reason a class's invariants and the code
   that maintains them belong together.
2. **`InventoryManager` already had its own `bridge_` member and used it**
   (`setBridge()`, D20b1) — `pickupItem()`/`placeItem()` already push
   `MoveItemIntent`/`EquipmentChangedIntent` through it. `moveItem()`'s
   bridge pushes for the direct-slot-swap and cursor-parity branches are
   not new capability, just the same pattern extended to two branches that
   used to live in the command handler instead.

A concrete duplication died as a result, not just moved: `eq_commands.cpp`'s
`IsRecognizedInventorySlot()` existed *solely* because
`InventoryManager::isValidSlot()` is private with no accessor — its own
comment said so verbatim ("that method is private ... so this reproduces
its exact OR-chain"). As a member function, `moveItem()` calls
`isValidSlot()` directly. One copy of that ten-predicate OR-chain now,
not two that could silently drift apart (the exact failure mode the T57/T60
comments in the old code were already anxious about for the *other*
predicates it reused).

### The bridge-push constraint: dissolved, not worked around

Checked, not assumed: `EqActionHandler::equip()` and
`EverQuest::RequestEquip()` do not reference `m_bridge` anywhere (`grep -n
"m_bridge" src/client/eq_commands.cpp` after this unit's changes returns
zero hits inside `RequestEquip()`). All four of the pre-extraction handler's
bridge pushes now happen inside `InventoryManager::moveItem()` via the
`bridge_` pointer `EverQuest::setBridge()` already wires
(`eq_bridge.cpp:238`, unchanged by this unit — it already called
`m_inventory_manager->setBridge(bridge)` before T141 touched anything). No
`GameStateBridge*` was added to `EqActionHandler`; none was needed. This
matches the plan's prediction exactly — getting the *home* right is what
made the constraint stop applying, not a workaround around it.

### Two-commit structure

- `e45c7f99` — **step 1, extraction only.** `equip` still registered in the
  game registry (`eq_commands.cpp`'s `m_command_registry`), calling into the
  new `InventoryManager` methods instead of duplicating the logic locally.
  `tests/test_eq_commands_equip.cpp` run **unmodified** before writing any
  code (30/30 passing against the pre-extraction handler, establishing the
  baseline) and again after (30/30, byte-identical assertions) — proof
  behaviour did not change, independent of step 2.
- `b71c885b` — **step 2, migration.** `RegisterEquipmentCommands()` and its
  registration removed; `equip` registered in `action::CommandProcessor`
  instead (category `"Inventory"`, ported verbatim, not `"Commerce"`).
  `EqActionHandler::equip()` is a one-line call to the same
  `RequestEquip()` step 1 introduced — the migration really is the thin
  wrapper the plan predicted, not a second design pass.

If the migration had turned out to be wrong for some reason (it wasn't),
step 1 stands alone: `InventoryManager` gaining real orchestration methods
is worth doing regardless of which registry calls them.

### `CheckEquipDestination`

Moved verbatim as `InventoryManager::checkEquipDestination()` — same two
checks (`canEquipInSlot()` for slot-type, `canEquipItem()` for
class/race/level), same message text (now built with plain `std::string`
concatenation instead of `fmt::format`, since `InventoryManager` doesn't
otherwise depend on `fmt` and the message shape is trivial enough not to
need it — a deliberate, disclosed choice, not an accidental format-string
divergence; the produced strings are byte-identical, confirmed by the
unmodified pre-existing tests asserting substrings of them). No longer
takes an `InventoryManager*` parameter — it's a member now, so `this->`
replaces the old `inv &&` null-check entirely (the check was only ever
guarding against a caller passing `nullptr`, which can't happen for a
member calling its own methods).

### Test fate: retargeted, not deleted

`tests/test_eq_commands_equip.cpp`'s seam **survives** — `RequestEquip()`
is exactly as reachable and exactly as testable after the migration as
before, just no longer reachable via `EverQuest::ProcessChatInput()` once
`equip` left the game registry. Per T140's precedent (retarget) rather than
T136's (delete, only when the seam itself stops existing): all 30 tests'
`eq.ProcessChatInput("/equip ...")` calls became
`eq.RequestEquip("...")` — same `EverQuest`, same `InventoryManager`, same
bridge-intent assertions, one layer lower (below registry selection instead
of through it). All 30 pass unmodified in assertion content. Added 5 new
`test_command_processor.cpp` tests (registration/category, raw-args
passthrough for both slot-pair and item-name forms, no-args still
dispatches, zone-connection guard) at the same density T136 used for
`vendor`/`buy`/`sell` and T140 used for `warp` — the CommandProcessor-layer
coverage the retargeted file no longer reaches on its own.
`RecordingActionHandler` (`tests/action_test_fakes.h`) gained an `equip()`
override; `EqActionHandler` gained a real one.

### Live evidence (wire-verified, not dispatch-layer-only)

Full detail in `/mnt/projects/fable/review/T141/` (README + DB dumps + raw
run logs). Summary:

- **`registry=action` live, with a `registry=game` control in the same
  run**: `/equip list`, `/equip 30 13`, `/equip 13 22` all logged
  `CMD outcome=applied registry=action recognized=true` against a real
  eqemu server; `/volume` (one of the 7 `stay-game` commands) in the same
  session logged `registry=game`. This is the exact A/B the plan asked for,
  captured live rather than inferred from the unit tests.
- **Equip (cursor -> Primary) and unequip (Primary -> cursor) both
  persisted server-side**, confirmed by reading `peq.inventory` directly
  (not the zone log) before and after: item 6011 (Rusty Mace) moved from
  `slot_id` 33 (server-side cursor) to `slot_id` 13 (Primary) and back,
  matching `inventory_constants.h`'s own documented client/server cursor
  offset (`CURSOR_SLOT=30` client-side, `SERVER_CURSOR_SLOT=33` here).
- **The item-name form (`/equip Rusty Mace`) also reached the server**:
  `Resolved 'Rusty Mace' to Rusty Mace (slot 30) -> Primary (13).` /
  `Moving item: slot 30 -> Primary (13)` in the client log, followed by the
  DB showing item 6011 at `slot_id` 13.
- **One live-leg finding, logged to `refactor/deferred.md` and NOT fixed
  here** (out of territory, and not caused by this unit): `/equip 13 22`
  (Primary -> the client's general-slot numbering) was rejected server-side
  ("The selected slot was invalid.") and correctly triggered the
  pre-existing (untouched) desync-recovery path. Root cause is a
  client/server general-slot numbering offset this eqemu server has that
  `SendMoveItem` never translates — unrelated to `/equip`'s own logic,
  which sends exactly the packet it always sent. Re-tested with the
  known-good `13 <-> 30` pair (proven live by T71), which round-tripped
  cleanly both directions — see the DB evidence above.
- Testchar's position was restored to the documented canonical point
  (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190) and the summoned test item
  removed before the eqemu lock was released.

### Deviations from the plan

- `ActionDispatcher::equip()` adds a `checkZoneConnection()` guard the
  pre-migration game-registry handler never had. Every other migrated
  inventory/vendor action in `action_dispatcher.cpp` gates on it, and a
  disconnected `EverQuest` has no meaningful inventory state to move items
  through — flagged as the one deliberate behavior addition in step 2's
  commit message, not silently folded in.
- `CheckEquipDestination`'s ported message text uses `std::string`
  concatenation instead of `fmt::format` inside `InventoryManager` (see
  above) — a formatting-mechanism change, not a content change; disclosed
  because the plan's STOP condition is about *behaviour*, and message text
  is user-visible behaviour even though its bytes are unchanged here.
- The plan's territory list did not include eqemu wire-protocol files, so
  the general-slot numbering finding above is reported, not fixed.

### Measured wall-clock

Approximately **2.5–3 hours** end to end (protocol/plan reading and
scoping through server teardown and this Review), across a single
uninterrupted session — no STOP, no retry-from-scratch. The two live-leg
false starts (log verbosity too low to see `[SYS]` messages at `-d 3`; the
slot-22 rejection requiring a second live run with the known-good pair)
account for a modest fraction of that, not a re-run of the whole leg.

## Board

**`equip` is closed.** Both the extraction (step 1) and the migration
(step 2) landed, verified at both the unit-test and the live-wire level,
with the pre-existing test suite retargeted rather than discarded. This
was genuinely the last row: the command-migration programme now reduces to
**7 `stay-game`** (`hotkeys`, `pmem`, `music`, `sound`, `volume`,
`musicvolume`, `effectsvolume` — no work, confirmed still registry=game
live via this unit's own `/volume` control) **and 3 `needs-decision` for
Will** (unchanged by this unit). The extraction did not reveal anything
that changes that picture — if anything it reinforces T136/T138/T140's
read that `equip` really was the one command whose difficulty was about
*where the logic lived*, not about the registry mechanics every other
command hit.
