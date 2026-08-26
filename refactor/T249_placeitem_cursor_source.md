# T249 — placeItem() validates against cursorSourceSlot_, which the server never sees (T236 finding)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one live swap. Class A.
Territory: src/client/inventory/inventory_manager.* `placeItem` swap
branch, tests/test_eq_commands_equip.cpp + inventory tests. Do NOT touch
combat/eq.cpp (T247), audio (T248).

## Divergence
T236 (`review/T236/`, Review): for a cursor item picked up FROM an
equipment slot, `placeItem()`'s swap branch validates the displaced item
against `cursorSourceSlot_` — but eqemu validates displaced items only when
the SOURCE of the move is an equipment slot (`inventory_profile.cpp:365`),
and the cursor is not one. So the client refuses swaps the server would
apply (client stricter than server). Decision (orchestrator): match the
server — the client must never refuse what the server accepts, nor apply
what it rejects.

## Scope
1. Make `placeItem()`'s displaced-item check use the server's rule: the
   move's source slot is the CURSOR (30), so `IsSlotAllowed` on the
   displaced item is trivially true; keep the check that the CURSOR item
   fits the destination (that one the server does enforce).
2. Tests: cursor item from an equipment slot placed onto an occupied
   general slot → swap applies (was: refused); cursor item that does not
   fit the destination → refused as before. Red/green.
3. Live: pick up a weapon from Primary to cursor, place on an occupied
   general slot → server applies (DB rows swap), client shows the same.

## Verification
Full ctest; Review; evidence review/T249/. Commit:
`T249: cursor placements follow the server's swap rule`.
