# T259 — trade-window slots are not bag parents (T255 leftover)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for one two-client trade
of a bag with contents. Class A. Territory: src/client/inventory/
inventory_manager.* (trade-slot parent ranges), TradeState/trade_manager
only where contents must be surfaced, tests. Do NOT touch
willeq_db_builder / data/willeq.db (T258 slot1), combat/eq_movement (T254).

## Gap
T255's bag-content relocation covers general, cursor and bank parents;
trade-window slots (Titanium trade slots 3000-3007, contents 3031+ in
steps of 10 — verify in titanium_limits.h / titanium.cpp CalcSlotId) are
not treated as bag parents, so a bag placed in the trade window shows
empty client-side and its contents are not relocated on completion.

## Scope
1. Add the trade parent range; `putBagContents`/`takeBagContents` for
   trade slots; on trade completion, contents follow the bag to the
   partner's general slot as the server's post-trade ItemPackets dictate
   (verify what eqemu sends: likely full ItemPackets for received items —
   if so, the client only needs to not lose the OWN side's contents when
   they leave).
2. Tests: bag with contents into trade slot → contents in 3031+; cancel →
   back to general with contents; complete → own side cleared cleanly.
   Red/green.
3. Live: Testchar trades a bag with two items to Oraclerogue; DB rows
   land under Oraclerogue's general range; both clients' `/equip list`
   agree with the DB.

## Verification
Full ctest; Review; evidence review/T259/. Commit:
`T259: trade-window bags keep their contents`.
