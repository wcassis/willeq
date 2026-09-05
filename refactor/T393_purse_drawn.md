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

## Review

**B201 is closed in code. The live screenshot pair is still OWED** — T393 held
no eqemu lock (T391 has it), so nothing here was seen on a running client.
Branch `task/T393-purse-drawn`, base `572c58ad`.

### 1. The oracle's placement, quoted

Source: `/mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml`
(the official Titanium/P1999 default UI, unmodified). Verbatim:

```
<Screen item="InventoryWindow">      Location 100,50   Size CX=428 CY=460
<Screen item="IW_CharacterView">     Location 166,57   Size CX=85  CY=168
<Button item="IW_Money0">            Location 303,121  Size CX=70  CY=24
     ButtonDrawTemplate/NormalDecal  A_PlatinumCoin
     DecalOffset 1,3   DecalSize 18x18   Text 9999
<Button item="IW_Money1">            Location 303,147  Size CX=70  CY=24
     NormalDecal A_GoldCoin      (same decal box)
<Button item="IW_Money2">            Location 303,171  Size CX=70  CY=24
     NormalDecal A_SilverCoin    (same decal box)
<Button item="IW_Money3">            Location 303,197  Size CX=70  CY=24
     NormalDecal A_CopperCoin    (same decal box)
```

`IW_Money0..3` are also listed in `InventoryWindow`'s `<Pieces>` block, so all
four are always present — the oracle draws four rows whatever the counts are,
which is why a zero denomination still gets a row on fable.

For context, the general-inventory slots (`InvSlot22..29`) sit at X=303/346,
Y=228..357 — directly BELOW the coin column and sharing its left edge. So the
oracle's purse is the top of the window's right-hand strip: right of the
right equipment column (X=253), right of the character view (166..251), above
the general grid.

**No oracle screenshot of the window with coins exists.** `review/T386/oracle/`
holds only the blocked zero-denomination attempt (§1d: Oraclealpha died of
`#goto` fall damage), and `review/T351/` does not exist. The XML is therefore
the source of record for this unit, as the plan's step 1 allows.

### 2. The scale rule and the resulting rects

`centerPopup` is a fixed 400x350 at every resolution (T351 measured five), and
every rect in `inventory_layout.h` is expressed relative to that frame. So the
coin column is the oracle's rect mapped proportionally from 428x460 onto
400x350, by integer division — no rounding policy that can drift:

| denom | oracle | offset in centerPopup | rect at 800x600 (popup origin 200,125) |
|---|---|---|---|
| platinum | 303,121 70x24 | 283,92 65x18 | {483,217,548,235} |
| gold     | 303,147 70x24 | 283,111 65x18 | {483,236,548,254} |
| silver   | 303,171 70x24 | 283,130 65x18 | {483,255,548,273} |
| copper   | 303,197 70x24 | 283,149 65x18 | {483,274,548,292} |

All four lie wholly inside `centerPopup` and overlap none of the 30 slot rects
(the nearest is the right equipment column at popup-x 360; the purse ends at
348). `coinDecalRect()` maps the oracle's DecalOffset 1,3 / DecalSize 18x18 the
same way, and lies inside its own row.

The geometry lives ONLY in `inventory_layout.h`, next to `slotRect()` and
`paperdollRect()`, under the same T57 rule: `renderInventoryPopup()` contains
no coin-position arithmetic of its own.

### 3. How the counts travel

```
PlayerState purse
  -> CurrencyChangedData {platinum,gold,silver,copper}   (5 publishers)
       core/everquest.cpp            OP_MoneyUpdate
       commands/combat_actions.cpp   corpse-coin grant (T382/B172)
       merchant/merchant_trade.cpp   buy / sell
       bridge/bridge_component.cpp   } the two stat pushes
       bridge/bridge_component_ui_audio.cpp
  -> GameStateBridge
  -> IrrlichtBridge::applyEvent, case CurrencyChanged      <-- WAS a LOG_TRACE
       EQT::Graphics::applyCurrencyChanged(
           renderer_->Frame().inventoryState_, d)
  -> InventoryPanelState::coins[4]
  -> renderInventoryPopup() -> InventoryLayout::coinRect(denom)
                            +  coinRowText(inv, denom)
```

The applier is a free function in `static_panels.cpp`, the
`applyLootWindowOpened()` / `applyHotbarSlotAssigned()` idiom, so a test drives
the real publisher's payload struct through the real applier with no Irrlicht
device. Counts are ABSOLUTE, never deltas: every publisher reads the whole
purse off `PlayerState` and sends it, so a second event replaces the first
(pinned by a test). That covers loot, buy, sell and the stat pushes.

**Bank money is deliberately NOT drawn here.** `BankCurrencyChanged` is a
separate event carrying the bank's purse, not the carried one, and the oracle's
`IW_Money0..3` show carried money; that arm is untouched.

`updateCharacterStats()`'s four `(void)` discards are deleted and the counts
now land on the same panel state — but **that function has ZERO callers**:
grepping `src/ include/ tests/ tools/ benchmarks/ docker/` for
`updateCharacterStats` returns only the declaration, its three
`OutputRenderer` overrides, one 6-arg test-fixture override, and three
comments. The reaching path is the bridge arm. It is wired anyway because a
silent discard is exactly what B201 was, and a future caller must not
reintroduce it; the code says so at the site.

### 4. Tests — 12 new, 7 red pre-fix

`tests/test_static_ui_panels.cpp`, suites `PurseT393`, `PurseT393Resolutions`,
`PurseT393Render`.

The pre-fix run is a genuine red, not a compile error: the API (`coinRect`,
`coinDecalRect`, `applyCurrencyChanged`, `coinRowText`,
`InventoryPanelState::coins`) was left in place and its BODIES reduced to the
pre-fix behaviour — degenerate rects, an applier that discards its payload, no
coin drawing in `renderInventoryPopup()`, the bridge arm back to its
`LOG_TRACE`, the four `(void)` discards restored. Staged with exported file
copies in the session scratchpad and restored by copy-back (never `git stash`,
never `git checkout --`); the restored tree was verified byte-identical to the
exported fix patch (`diff full_fix.patch <(git diff)` → identical).

| test | pre-fix | post-fix |
|---|---|---|
| `PurseT393.EachCoinRowIsTheOracleRectScaledIntoThePopup` | **FAIL** | PASS |
| `PurseT393.TheFourRowsRunPlatinumToCopperTopDownAndDoNotOverlap` | pass (degenerate rects trivially satisfy it) | PASS |
| `PurseT393.EveryCoinRowLiesWhollyInsideThePopupFrame` | **FAIL** | PASS |
| `PurseT393.NoCoinRowOverlapsAnyInventorySlot` | pass (degenerate) | PASS |
| `PurseT393.TheDecalBoxSitsInsideItsOwnRow` | pass (degenerate) | PASS |
| `PurseT393.AnOutOfRangeDenominationIsDegenerate` | pass | PASS |
| `PurseT393.StartsWithAnEmptyPurse` | pass | PASS |
| `PurseT393.ACurrencyChangedEventPutsTheFourCountsOnThePanel` | **FAIL** | PASS |
| `PurseT393.ASecondEventReplacesTheFirstRatherThanAccumulating` | **FAIL** | PASS |
| `PurseT393.AZeroDenominationStillPrintsItsRow` | **FAIL** | PASS |
| `PurseT393Resolutions.TheRowsAreIdenticalRelativeToThePopupOrigin` | **FAIL** | PASS |
| `PurseT393Render.TheInventoryPopupEmitsTwoTextEntriesPerCoinRow` | **FAIL** (24 vs 32) | PASS |

`12 tests ran, 5 passed, 7 FAILED` pre-fix; `12 tests ran, 12 passed` post-fix.

The last one is the one that actually pins "the popup renders the numbers":
an EDT_NULL device + a real `TextBatch`, rendering the real
`renderInventoryPopup()` and counting text entries — 1 title + 22 equipment
labels + 1 "General" + 2 per coin row = 32, against 24 before. The counts used
are T386's measured post-loot purse, 8/18/8/16.

The four `PurseT393.*` cases marked "pass (degenerate)" are containment /
non-overlap assertions that an all-zero rect satisfies vacuously; they are
kept because they are what stops a FUTURE placement edit from putting the
purse over a slot, which is the regression the geometry rule exists to prevent.

### 5. Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **exit 0** (one earlier invocation hit the 600 s ceiling, exit 143 — post-143 sweep run: no zero-byte files, no `build*/lib/st??????` temps, both archives present with 15 / 322 members, every `build/bin/*` a valid ELF; the rebuild then linked cleanly and `--gtest_list_tests` on the affected binary was checked before any ctest) |
| `ctest --test-dir build -j6` | **4050 / 4051**, exit 8. Sole failure `AudioManagerTest.PreloadCommonSounds` — the known ALSA host quirk. (`FileSizeCeiling` **Passed**.) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate, standalone | **PASS** (`=== VISUAL GATE: PASS ===`); no golden moved — `git status` shows only this unit's six files. As the plan predicted, the gate's zones never open an inventory popup, so the purse is invisible to it |
| `scripts/build-arm-noble.sh` | **exit 0**, `build-arm-noble/bin/willeq` ELF 32-bit ARM EABI5 |
| `check_file_sizes.sh fable` | 8 files checked, **8 ok, 0 warn, 0 fail** (largest touched: `static_panels.cpp` 1043, `test_static_ui_panels.cpp` 998) |

Note on the ALSA waiver: `fable` moved past this branch's base while T393 ran,
and T392 (board B215) reports that waiver RETIRED — the test green. This branch
is cut from `572c58ad`, before that, so the failure above is this tree's
expected one. Re-read the header of `BOARD.md` at merge time.

### 6. Deviations and surprises, flagged

1. **fable's atlas has no coin sprite.** The oracle blits `A_PlatinumCoin` /
   `A_GoldCoin` / `A_SilverCoin` / `A_CopperCoin` into the 18x18 decal box.
   `UISprite` has no coin entry and the atlas generator emits none, so T393
   reproduced the BOX and put the denomination letter (`p`/`g`/`s`/`c`) in it,
   in the coin's own colour. Boarded **B216**.
2. **The purse lands ON the paperdoll rect, and that is a fable-side
   proportion problem, not an oracle one.** The oracle's `IW_CharacterView` is
   85 of 428 px wide (20%) and its coin column at X=303 clears it. fable's
   `paperdollRect()` fills the entire gap between the two equipment columns
   (popup-x 48..352 of 400, 76%), so the oracle-scaled coin rows (283..348)
   sit over its right-hand margin. They are drawn AFTER the paperdoll blit, so
   they are on top (`UIRenderer` submission order is draw order, T294), and
   the character model is centred in the rect — but nobody has seen the two
   together. **Not resolved here, because the plan says the placement is the
   oracle's and moving either rect is a design call.** Boarded **B217** (class
   C) for Will's eye on the owed live pair.
3. **`updateCharacterStats()` has zero callers.** Named in B201 as one of the
   two defect sites, and it is one — but fixing it changes nothing a player
   sees. The live path is the bridge arm. Recorded rather than absorbed.
4. **No oracle capture of the coin rows exists**, and none could be taken this
   unit (no lock). T386's oracle folder holds only the failed
   zero-denomination attempt. The XML is the whole placement record.

### 7. Owed

The live screenshot pair — fable's inventory window with a non-zero purse
beside the official client's — goes to the next lock unit, together with
B217's judgement call. `review/T386/` §1d still has the working recipe for a
zero denomination (levelled character, safe Z 51, `#set invulnerable on`
before the first `#goto`).
