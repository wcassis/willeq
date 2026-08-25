# T236 — /equip swap-with-cursor: client applies a move the server rejects

Slot: slot3. x86. OWNS eqemu briefly (noclobber) — after T235 if contended.
Class A/B (T230 finding; same class as T82's "doMove swap branch never
validates the displaced item", deferred.md). Territory: InventoryManager
move/swap logic (src/client/inventory/*), the /equip command's pre-flight
(command_processor equip path — T141), eq_merchant.cpp MoveItem send only,
tests. Do NOT touch action/eq_action_handler* or test_eq_action_handler_*
(T234 unmerged), renderer_frame.cpp (T235).

## Defect
`/equip 30 22` (cursor → general slot 22, which was occupied) reported a
client-side swap; the server never applied it and the DB still showed the
old layout (`review/T230/` runs 3-4, db_* snapshots). T82 found the sibling:
`doMove()`'s swap branch never validates the displaced item against the
slot it lands in, the server rejects, and the client self-heals via
desync/resync (no corruption, but a lie in the UI until resync).

## Scope
1. Reproduce headless with a captured or constructed server rejection:
   cursor item + occupied target → client state after the move vs. the
   server's reply. Determine the exact rule eqemu applies
   (/mnt/projects/akk-stack/code/zone/inventory.cpp SwapItem / MoveItem:
   cursor rules, `IsValidSlot`, class/race/size checks for the displaced
   item).
2. Add the same validation to the swap branch's PRE-FLIGHT (T77 style:
   reject locally with the official message, send nothing) so the client
   never applies a move the server would reject. Keep the resync path.
3. Tests: displaced item invalid for its destination → local rejection, no
   packet; valid swap → packet as before. Red/green.
4. Live: the T230 fixture (`#summonitem` to cursor, `/equip 30 22` into an
   occupied general slot) → either the swap applies on BOTH sides (DB row
   moves) or the client refuses with a message and the DB is unchanged.

## Verification
Full ctest; Review; evidence review/T236/. Commit:
`T236: swap-with-cursor moves are validated before they are applied`.
