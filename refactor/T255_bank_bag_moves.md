# T255 — bank bag moves carry no contents; only the front cursor bag's contents are parsed (T252 leftovers)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one live bank move.
Class A. Territory: src/client/inventory/inventory_manager.* (bank ranges,
`takeBagContents`/`putBagContents` generalisation, ItemPacket cursor-queue
parsing), tests/test_eq_commands_equip.cpp + inventory tests. Do NOT touch
combat/eq_movement (T254 slot2), eq_bridge.cpp (T253 slot1).

## Gaps (T252 Review)
1. general↔BANK bag moves do not relocate contents client-side (T252
   generalised relocation to general-or-cursor parents only). eqemu bank
   content ranges: `CalcSlotId` for bank slots (titanium.cpp — find the
   bank bag base; Titanium bank = 2000-2007, bag contents 2031+ in steps
   of 10 client-side; server-side 6010+... verify both numberings and the
   translation T141/T158 established).
2. Only the FRONT cursor item's contents fit the cursor range; a queued
   bag behind the cursor loses its contents in the client model (server
   keeps them). Decide: parse contents per queued bag into a per-item
   container (small vector on the cached ItemInstance) so nothing is lost,
   or document the limit if the official client also only shows the front.

## Scope
1. Extend `takeBagContents`/`putBagContents` to bank parents; `/bank`
   move of a bag with contents → contents follow (client and server
   agree). Test red/green.
2. Item 2 per the decision above, with a test either way.
3. Live: bag with contents general → bank slot → DB rows in the bank
   content range; back again.

## Verification
Full ctest; Review; evidence review/T255/. Commit:
`T255: bank bag moves carry contents; queued cursor bags keep theirs`.
