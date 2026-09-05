# T393 — the purse is drawn (B201)

Class A, no server. Read the B201 board text, T386 §1c
(`review/T386/fable/T386_fable_arena2_purse_01_after_1024x768.png` — the
inventory window with no coin counters), the `CurrencyChanged` arm in
`src/client/bridge/irrlicht_bridge.cpp`, `updateCharacterStats` in
`renderer_entities.cpp`, and the inventory popup layout
(`include/client/graphics/ui/ui_layout.h`, `static_panels.cpp`, the
T54–T56 static-panel test seam).

1. Oracle: the official client's inventory window shows the four coin
   counters (platinum/gold/silver/copper) in a column at the lower right
   of the paperdoll. T386's oracle screenshots under `review/T386/oracle/`
   or T351's inventory captures (`review/T351/` if present) show the
   exact placement — crop and record the pixel rectangle; if no oracle
   capture shows the window with coins, say so and place them where the
   official layout puts them per `UI_*.ini` / the default XML
   (`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml` —
   quote the coin element positions).
2. Carry the four counts through the bridge (`CurrencyChanged` →
   renderer state), draw them in the inventory popup, and update on every
   change (loot, buy, sell, bank). Delete the `(void)` discards.
3. Tests, pre-fix failing: after a `CurrencyChanged` event the popup's
   coin rectangles render the numbers (static-panel seam: text + rect);
   the rect matches the oracle placement within the layout's scale rule.
4. Strike B201. Live screenshot pair (fable vs oracle inventory with coins)
   OWED to the next lock unit.

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, file sizes;
graphics change → visual gate standalone + ARM (the gate's zones open no
inventory — no golden should move; STOP with the contact sheet if one
does).

STOP: none expected.
