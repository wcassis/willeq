# T77 — `/equip` sends moves the server rejects; make it pick a valid item and slot

**Slot: t2. x86 only. No Pi. NO EQEMU** — sibling T76 owns the server.
See "The deferred live leg"; this is deliberate.

**Territory: `src/client/eq_commands.cpp` (the `/equip` handler and its
helpers), `include/client/eq.h` if an accessor is needed, and tests.**

Do not touch `InventoryManager`'s logic — you are consuming it, not
changing it. Do not touch `SendMoveItem`/`ZoneProcessMoveItem`: T71 proved
the wire path correct with three live round trips. Do not touch the
first-person or spell paths. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T71_equip_fix_and_first_person.md`'s Review (it fixed `/equip`'s
*client-model* half and proved the wire path is fine), then
`refactor/T66_camera_and_equip_reachability.md`'s Review (which added the
command).

## The defect

T75 tried to equip a weapon for its capture and the server refused:

```
Moving item: slot 23 -> Primary (13)
The selected slot was invalid.
Inventory Desyncronization detected: Resending slot data...
Source slot 24 resynchronized.
Destination slot 13 resynchronized.
```

Two things to read from that. First, **T71's desync-resync path worked
correctly** — the client detected the divergence and recovered. That is
not the bug. Second, `/equip` sent a move the server was always going to
reject, and it had everything it needed to know that beforehand.

**`/equip` validates only that the slot NUMBER is in a recognised range.**
`IsRecognizedInventorySlot()` (`eq_commands.cpp:307`) composes the
per-family predicates from `inventory_constants.h` — it answers "is 23 a
slot?" and nothing more. It never asks whether slot 23 *contains* an item,
or whether that item can legally go in slot 13.

## The validation already exists and nothing calls it from here

- `InventoryManager::canEquipItem(item, equipSlot)`
  (`inventory_manager.cpp:1208`) — full legality: slot-type fit, plus
  player restrictions.
- `ItemInstance::canEquipInSlot(slotId)`
  (`item_instance.h:160-177`) — the slot bitmask check, including the
  paired-slot cases (ears, wrists, fingers) that a naive implementation
  gets wrong.
- `InventoryManager`'s broader placement validation just above
  `canEquipItem`, covering world containers and cursor.

T71 already added `GetInventoryManager()` for exactly this kind of access.
**Reuse this validation. Do not write a second one** — this session has
repeatedly paid for parallel implementations of the same predicate (T57's
slot geometry, T60's bone attachment, both of which cost a unit to
converge).

## Part 1 — pre-flight, and report usefully

Before sending, check:
- the source slot actually holds an item,
- the destination is legal for that item, via `canEquipItem()`,
- the move is not a no-op.

On rejection, **say why in terms the caller can act on** — "slot 23 is
empty", "a Rusty Mace cannot go in Head (slot 2)" — not "invalid slot".
`/equip` exists to make scripted sessions possible; an opaque failure
costs a whole session to diagnose, which is exactly what happened to T75.

**Keep the server authoritative.** This is pre-flight to avoid sending
doomed packets, not a claim to know better than the server. If validation
passes and the server still refuses, that is correct behaviour and the
existing resync path handles it. Do not suppress or second-guess a server
rejection.

## Part 2 — let it pick the slot and the item

Will's ask: `/equip` should *select* a valid slot/item rather than
requiring the caller to guess numbers. A test script should not need to
know that a dagger is in slot 23 today.

Add a form that takes an **item name** and resolves the rest:
- find the item in inventory by name (substring match is fine; say what
  you chose),
- determine the destination from the item's own slot bitmask via
  `canEquipInSlot()` — for paired slots (ears, wrists, fingers) prefer an
  empty one, and say what happens when both are full,
- report what it resolved to, so a script can assert on it.

Keep the explicit `/equip <fromSlot> <toSlot>` form working — it is what
existing session scripts use, and T71 verified it live.

Also worth adding, and cheap: a way to **list what is equippable and where**
(`/equip list` or similar). T63, T71, T75 each burned session time
discovering what a character was actually carrying.

**Scope discipline**: this is command-layer convenience over existing
validation. Do not implement inventory management, auto-swapping, bag
traversal beyond what `InventoryManager` already exposes, or stat
comparison.

## The deferred live leg — state it, do not hide it

T76 holds eqemu. So unit-test everything against a populated
`InventoryManager` — empty source, illegal destination, paired-slot
selection, name resolution, name not found, both paired slots full — and
**say plainly in your Review that the live end-to-end leg is outstanding**,
recording it on the board for the next unit with server access.

This is the same shape as T66's deferral, which was the right call then
and was vindicated: the live leg found a real defect. Do not claim
`/equip` works live when nothing has watched a packet go out.

## STOP conditions

- Needing to change `InventoryManager` or the wire path to make this work
  — report; that changes the unit.
- Any urge to take the eqemu lock.
- Any urge to write a second copy of the legality logic.

## Verification

Full protocol bar, plus the unit tests above and an explicit statement of
what remains unproven without a server.

## Review

### Part 1 — pre-flight, and which existing API each check uses

All three of `/equip`'s forms now funnel through a single `doMove(fromSlot,
toSlot)` lambda (`eq_commands.cpp`, `RegisterEquipmentCommands()`) that
runs, in order:

1. `IsRecognizedInventorySlot()` on both slots (T66, unchanged).
2. **No-op rejection**: `fromSlot == toSlot` → "Source and destination are
   the same slot (...) — nothing to do." (new).
3. **Source-holds-an-item**: looks up the item at `fromSlot` (via
   `InventoryManager::getItem()`, or `getCursorItem()`/`hasCursorItem()`
   when `fromSlot` is the cursor) *before* any branch executes. Empty
   source → "Nothing to move: X is empty." This subsumes the old
   direct-slot-only empty check T71 had; it now applies uniformly to the
   cursor-source branch too.
4. **Destination legality**, only when `toSlot` is an equipment slot
   (`CheckEquipDestination()`, new free function): calls
   `ItemInstance::canEquipInSlot(toSlot)` first — if that fails, the
   message is "X cannot go in Y (N) — wrong slot type for this item."; if
   it passes but `InventoryManager::canEquipItem(item, toSlot)` still
   fails, the remaining possible cause is `meetsRestrictions()` (class/
   race/level), so the message is "X cannot go in Y (N) — your character
   does not meet its class/race/level requirements." **No new legality
   logic was written** — both checks call straight into the existing
   `InventoryManager`/`ItemInstance` methods the plan named; the only new
   code is the message split (canEquipInSlot vs. the remainder of
   canEquipItem) so the two failure modes read differently, per the plan's
   "say slot 23 is empty / a Rusty Mace cannot go in Head" bar. Moving into
   a non-equipment slot (general/bag/bank/cursor) always passes this check
   — there is no equip-legality question there, and the server remains the
   sole authority on everything else.

Only after all four pass does execution reach the unchanged T71 branch
logic (cursor→slot via `placeItem()`, slot→cursor via `pickupItem()`,
slot↔slot via the local `getItem()`/`setItem()`/`removeItem()` mirror, then
the same `MoveItemIntent`/`EquipmentChangedIntent` pushes). The existing
`SendMoveItem`/`ZoneProcessMoveItem` wire path (T71-verified) and
`InventoryManager`'s validation code are both untouched — confirmed by
`git diff --stat fable`, which shows only `eq_commands.cpp`,
`tests/test_eq_commands_equip.cpp`, and `refactor/deferred.md` changed.

### Part 2 — name resolution: rules, paired slots, ambiguity

`/equip <item name>` (`FindItemsByNameSubstring()` + `ResolveEquipDestination()`,
new free functions):

- **Search universe**: equipment slots (`getEquipmentItems()`), general
  slots 22-29 (`getGeneralItems()`), general-bag contents (via the
  already-exposed `getBagContentsSlots()` — no new bag traversal written),
  and the cursor (`getCursorItem()`). Deliberately excludes bank/shared-
  bank/trade — out of scope for "equip something you're carrying."
- **Matching**: case-insensitive substring against `ItemInstance::name`.
- **Not found**: "No carried item matches '...'." , no intent pushed.
- **Ambiguous**: more than one *distinct* item name matches → "'...'
  matches multiple items: A (slot), B (slot) — be more specific.", no
  intent pushed. Duplicate stacks of the identical item name (e.g. two
  Throwing Daggers in different slots) are explicitly **not** treated as
  ambiguous — any instance is interchangeable for equip purposes, so the
  first match is used silently.
- **Destination resolution**: `ResolveEquipDestination()` iterates every
  equipment slot (0-21) and calls `ItemInstance::canEquipInSlot()` on each
  — this is what makes paired slots (ears/wrists/fingers) fall out for
  free with no special-casing: both members of a pair independently
  satisfy the bitmask check, so both appear as candidates. Among
  candidates, an **empty slot is preferred**; if all candidates are
  occupied (e.g. both ears full), it falls back to the first candidate and
  reports "X would replace what's already in Y" rather than picking
  silently or refusing. An item that fits no equipment slot at all (a bag,
  a reagent) is rejected: "X cannot be equipped (fits no equipment slot)."
- **Reporting**: on a successful resolution the command prints "Resolved
  '<what-you-typed>' to <ItemName> (<sourceSlot>) -> <destSlot>." *before*
  the usual "Moving item: ..." confirmation, then runs through the same
  `doMove()` pre-flight/execution as the slot form (so an illegal
  resolution — e.g. restriction failure — is still caught and reported,
  not silently sent).
- **Dispatch rule vs. the numeric form**: a first token starting with a
  digit or `-` is always parsed as the strict `<fromSlot> <toSlot>` form
  (so a typo like `22abc` stays a slot-parsing usage error, not a name
  search for the literal text "22abc 13"); anything else — including
  multi-word text — is treated as an item name. `/equip list` (exact,
  case-insensitive, no second token) is intercepted before this dispatch.
  **Deviation, flagged**: this changes the error text for `/equip general
  13` (a first token that doesn't look numeric) from T66/T71's "Invalid
  source slot" usage error to "No carried item matches 'general 13'." —
  the input is no longer nonsensical, it's a (failing) name search. The
  one existing test asserting the old text
  (`NonNumericSlot_PrintsUsage_PushesNoIntent`) was renamed and rewritten
  to assert the new, equally-actionable behavior
  (`NonNumericFirstToken_TreatedAsItemNameSearch_NotFound`); no other
  existing test's expected behavior changed (`22abc 13` still starts with
  a digit and stays on the strict slot-parser path, so
  `TrailingGarbageOnSlot_PrintsUsage_PushesNoIntent` is unaffected verbatim).

### The listing form

`/equip list` prints two sections via `AddChatSystemMessage()` (one line
per message, matching the existing `/vendorlist` precedent):

1. "Equipped (N items):" + one line per occupied equipment slot
   (`getEquipmentItems()`), `<SlotName> (<N>): <ItemName>`, or "Nothing
   currently equipped." if none.
2. "Carried, equippable via /equip <name>:" + one line per carried item
   that resolves to *some* equipment slot (same search universe as the
   name form, filtered through `ResolveEquipDestination()`), each line
   `<ItemName> (<currentSlot>) -> <destSlot>[ (would replace)]`; skips
   carried items that aren't equippable at all (bags, reagents) rather
   than listing them as noise. "Nothing else carried that could be
   equipped." if the carried set is empty or nothing in it resolves.

Read-only: no intent is ever pushed by the listing form (asserted in
`ListForm_ShowsEquippedAndCarriedWithResolution`).

### The deferred live leg — stated plainly

**Nothing in this unit ran against a live server.** T76 held the eqemu
lock for the whole session (NO EQEMU per the plan), so none of the
following has been watched against a real character: a pre-flight
rejection that matches what the server would independently refuse, a
successful `/equip <item name>` resolving and equipping a real carried
weapon end to end, or `/equip list` against a real character's actual
live inventory contents. Everything below "unit-tested against a
populated `InventoryManager`" — the only kind of proof this unit could
produce. Recorded on the board (`refactor/deferred.md`, the "PATTERN: real
features reachable only through a UI gesture" entry, instance 9): the next
unit that takes the eqemu lock should run all three `/equip` forms live
before this instance is called closed, the same shape T66's deferral took
and T71 closed for the wiring leg.

### Test suite

`tests/test_eq_commands_equip.cpp`: 30 tests (was 12; 2 renamed/rewritten
for the dispatch-rule deviation above, 20 new), all passing:

- **Pre-flight** (new): `WrongSlotType_RejectedLocally_NamesItemAndSlot_PushesNoIntent`,
  `RestrictionFailure_RejectedLocally_PushesNoIntent`,
  `LegalDestination_StillMoves` (confirms pre-flight isn't overly strict),
  `NoOpMove_SameSlot_RejectedLocally_PushesNoIntent`.
- **Name form** (new): `NameForm_ResolvesSlotFromItemBitmask_MovesAndReports`,
  `NameForm_SubstringMatch_CaseInsensitive`, `NameForm_NotFound_PushesNoIntent`,
  `NameForm_AmbiguousAcrossDistinctItems_RejectedWithBothNamed`,
  `NameForm_DuplicateStacksOfSameItem_NotAmbiguous_TakesFirst`,
  `NameForm_PairedSlot_PrefersEmptyEar`,
  `NameForm_BothPairedSlotsFull_ReportsWouldReplace_StillResolves`,
  `NameForm_ItemFitsNoEquipSlot_RejectedWithReason`.
- **List form** (new): `ListForm_EmptyInventory_ReportsNothing`,
  `ListForm_ShowsEquippedAndCarriedWithResolution`.
- **Dispatch-rule deviation**: `NonNumericFirstToken_TreatedAsItemNameSearch_NotFound`
  (rewritten), `TrailingGarbageOnSlot_PrintsUsage_PushesNoIntent` (kept,
  confirmed still passes verbatim).
- All 18 pre-existing T66/T71 tests (usage/parsing/slot-range validation,
  the four wiring happy paths, the T71 local-model-sync tests) pass
  unchanged.

A new `SeedNamedItem()` test helper (alongside the pre-existing `SeedItem()`,
which deliberately uses `slots=0xFFFFFFFF` to fit any slot and isolate the
wiring tests from legality) gives full control over name/slots-bitmask/
reqLevel for the new legality/resolution tests.

### Verification bar

1. `cmake --build build -j24` — PASS (one transient, unrelated CMake
   `gtest_discover_tests` timeout on `test_spell_manager` during the first
   attempt — a pre-existing test-discovery flake, not a compile error; a
   clean re-run built everything with no errors or new warnings).
2. `ctest --test-dir build --output-on-failure` — **2057/2058 passed**; the
   sole failure is the pre-existing documented ALSA host exception
   (`AudioManagerTest.PreloadCommonSounds`). No regressions.
3. `bash scripts/build-gles2-x86.sh` — PASS.
4. Visual gate — PASS (`qeynos2`, `gfaydark`, `befallen`); goldens
   untouched (this unit made no rendering changes, confirmed by the
   `git diff --stat` above touching only `eq_commands.cpp`/tests/
   `deferred.md`).
5. `bash scripts/build-arm-noble.sh` — PASS, `willeq` and tool binaries
   built for ARM.
6. `env -C build check_file_sizes.sh` analog — run as
   `env -C <slot> bash scripts/refactor/check_file_sizes.sh fable` (the
   `env -C` substitutes for `cd`, which the hard rules forbid; the script
   itself resolves its repo root from `pwd`) — **2 files changed, 0 FAIL,
   1 WARN**: `src/client/eq_commands.cpp` at 1857 lines (> 1500; was
   already WARN pre-T77 at 1595 lines from T66/T71's own additions — this
   unit's growth is real code (pre-flight + two new command forms + their
   helper functions), not gratuitous, but is flagged per protocol rather
   than silently absorbed). `tests/test_eq_commands_equip.cpp` at 550
   lines: ok.

### Deviations

1. **Error-text change for a non-slot-looking first token** (`/equip
   general 13`, and generally any first token not starting with a digit or
   `-`): previously a slot-parsing usage error, now an item-name search
   (which fails to find anything and says so) — a direct, intended
   consequence of adding the name form, since item names are free text.
   One pre-existing test updated to match (see "Test suite" above); no
   other test's expected behavior changed.
2. `eq_commands.cpp` crossed further past the pre-existing WARN threshold
   (1595 -> 1857 lines) — flagged, not silently absorbed, per protocol.
   Same file T71 already flagged at the same threshold; no FAIL.
3. No STOP condition was hit: `InventoryManager`/the wire path were not
   touched (confirmed by the diff stat above); no eqemu lock was taken; no
   second copy of the legality predicate was written — `CheckEquipDestination()`
   and `ResolveEquipDestination()` both call straight into
   `ItemInstance::canEquipInSlot()`/`InventoryManager::canEquipItem()`
   rather than reimplementing either.

### eqemu / Pi discipline

No eqemu use (NO EQEMU per the plan; T76 held the lock). No Pi use (not
assigned to this unit). Neither was needed or touched.

Board (`refactor/deferred.md`, "PATTERN: real features reachable only
through a UI gesture" entry): recorded as instance 9 — same standing
pattern in a different shape ("the validation existed and the caller did
not use it," rather than "no producer at all") — with the live leg named
explicitly for whoever next holds the server, the same way instance 8's
live leg was named for T71 and then closed by it.
