# T282 — the client cannot put items into a world container (T278 hand-back)

Slot: slot3. x86. OWNS eqemu (noclobber) for the live combine. Class A.
Territory: src/client/inventory/inventory_manager.* (`isValidSlot` and the
world-container slot range 4000-4009), the `/equip`/move command path where
the destination is validated, tests. Do NOT touch the EQE1 index (T281
slot2), trainer (T280 slot1).

## Gap (T278 Review)
`InventoryManager::isValidSlot()` rejects 4000-4009, so `/equip <from> 4000`
never sends and the WORLD form of `/combine` (forge/oven on the ground)
cannot be driven from the client at all — T278 could only prove the carried-
container form end to end.

## Scope
1. Accept the world-container range where the server does. Read eqemu's
   rules first (`zone/inventory.cpp` / `object.cpp`: which slots a world
   container exposes, what it validates on a move in, and what it sends
   back) — T273/T277's lesson: verify against the server, do not trust a
   member name or a comment.
2. Make sure the container's contents model is populated and cleared on the
   right packets (T278 fixed `ZoneProcessClearObject`; T255 fixed the bag
   parse — reuse `takeBagContents`/`putBagContents` if the shape fits).
3. Tests: move into 4000-4009 sends the right packet; move out; the model
   follows the server's confirmation, not optimistically. Red/green.
4. Live: `/clickobject` a world forge (T278's qeynos2 drop_id 202), move two
   components in, `/combine`, product appears, `/closecontainer`. DB proof
   both sides. Restore rows. ⚠ eqemu empties a world container on the first
   open by a new user (T278) — do not pre-fill `object_contents`.

## Verification
Full ctest; Review; evidence review/T282/. Commit:
`T282: items can be placed into a world container`.
