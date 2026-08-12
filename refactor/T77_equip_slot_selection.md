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

(executor fills: what pre-flight now checks and which existing API each
check uses; the name-resolution rules including paired slots and
ambiguity; the listing form; what is deferred to a live session;
deviations)

Board: record this as another instance of the standing pattern — the
validation existed and the caller did not use it — and note the live leg
for whoever next holds the server.
