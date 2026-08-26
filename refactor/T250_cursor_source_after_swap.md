# T250 — cursorSourceSlot_ is stale after a cursor swap; returnCursorItem() is dead (T249 findings)

Slot: slot3. x86. NO eqemu unless a bag-swap live check is cheap (noclobber).
Class A. Territory: src/client/inventory/inventory_manager.* (cursor
bookkeeping, `relocateBagContents`, `returnCursorItem`), tests/
test_eq_commands_equip.cpp + inventory tests. Do NOT touch combat/eq.cpp
(T247 slot1), audio (T248 slot2).

## Defects (T249 Review, review/T249/)
1. After `placeItem()` swaps the cursor item into an occupied slot, the
   DISPLACED item becomes the cursor front but `cursorSourceSlot_` still
   names the ORIGINAL pickup slot. `relocateBagContents(cursorSourceSlot_,
   …)` would then move the wrong bag's contents if the displaced item is a
   bag. Fix: on swap, set `cursorSourceSlot_` to the slot the displaced item
   came from (the destination), and make sure every consumer of
   `cursorSourceSlot_` agrees on the meaning ("where the current cursor
   front came from"). Test: pick up bag A from general 22, place on general
   23 holding bag B → cursor = B, source = 23; then placing B into 24 moves
   B's contents, not A's. Red/green.
2. `returnCursorItem()` has zero production callers. Either wire it where
   the official client returns the cursor (zone-out / auto-return on
   timeout — check eqemu's cursor handling) or delete it. Prefer delete
   unless a caller is obvious.

## Verification
Full ctest; Review; evidence review/T250/. Commit:
`T250: cursor source tracks the displaced item; dead returnCursorItem removed`.
