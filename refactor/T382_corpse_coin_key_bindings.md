# T382 — corpse coin banked and sounded (B172); window keys match the oracle (B170)

Class A + A, slot2, no server (both oracles are measured in
`review/T377/`). Read the B172 and B170 board text and T377's §Review
items 1 and 3.

## Leg A — B172

`CommandsComponent::ZoneProcessMoneyOnCorpse` (`src/client/commands/combat_actions.cpp`)
parses the four denominations, logs at debug level, and drops them. The
official client, the instant the loot window opens: plays `BuyItem.WAV`
(UI sound id 138) and prints `You receive 1 platinum, 8 gold, 3 silver
and 4 copper from the corpse.` (eqstr — find the id in `eqstr_us.txt`; it
takes the four counts; check what the official client prints when a
denomination is zero — T377's capture has one line; if the zero form is
unknown, print all four as the capture shows and say so).
1. Bank the four denominations into the player's purse through the
   existing money path (`InventoryManager` — find how `OP_MoneyUpdate`
   credits and reuse it; do not add a second purse writer).
2. Push `PlaySoundIntent{BUY_ITEM}` (T328 Leg B's intent) and the chat
   line.
3. Tests, pre-fix failing: after the packet, the purse holds +1/+8/+3/+4;
   one PlaySoundIntent with id 138 was published; the chat line matches.
   The bridge intent golden learns the reviewed statement IN PLACE.
4. `InventoryManager::pickupMoney()`'s BUY_ITEM (T328) still has NO
   production caller — leave it, note it on B036 clause 2.
5. Strike B172; fill B036's corpse-coin row.

## Leg B — B170

Official defaults (Options > Keys > UI, T377 screenshots): group window
`Alt+P`, buff window `Alt+B`, pet info window `Alt+F1`, no alternates.
Fable: group `G` (+ `Alt+P` alt), pet `P` (= the official Customer
Service key), buff `Alt+B`.
1. `registerDefaultBindings` (`src/client/input/hotkey_manager.cpp`):
   group → `Alt+P` only (delete `G`), pet → `Alt+F1` (delete `P`), buff
   unchanged. Fix every text mention (`README.md`, the CLAUDE.md key
   tables, help strings, T368's tests that press G/P). `DocCounts` nets the
   `HotkeyAction` count — the enum does not change, only the bindings.
2. Also the empty group window: the oracle paints ONLY its title bar
   (`Group`), no body — T368's titled frame draws a body; match the
   oracle (the static-panel test seam).
3. Tests, pre-fix failing: the binding table maps `Alt+F1` → pet, `Alt+P`
   → group, and `G`/`P` → nothing; the empty group panel draws title only.
4. Strike B170 (and B115: the screenshot exists now — strike it citing
   `review/T377/oracle/`).

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, goldens for the
TUs touched, file sizes; Leg B touches `src/client/graphics` (the panel)
→ visual gate standalone + ARM. The live proof (loot a corpse on fable,
hear 138, see the purse move) is OWED to the next lock unit — note it.

STOP: the money path has no single credit function (report the writers).
