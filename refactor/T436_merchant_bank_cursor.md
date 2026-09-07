# T436 — the merchant, bank and cursor-item windows from the official XML (B429)

Class B (feature; "item icons seem critical" — Will 2026-09-07; three
producers fill an `iconId` nothing draws), slot1, no server (T428 holds
the lock). Read B429, T430's §Review (the icon sheets, `item_icon_sheets`,
the five draw sites, `IDW_IconButton`), T413/T415's XML-placement method
(`inventory_layout.h` rule: oracle placements scaled from the oracle
window size), T434's §Review (`options_layout.h` control table pattern,
`hitTest…` skipping unwired controls, the persistence writer), T342's
§Review (the measured buy/sell/coin behaviour: BuyItem on every
denomination, sell = BuyItem, combine silent), the merchant and bank
producers (`src/client/merchant/merchant.cpp`, `merchant_trade.cpp`,
the bank component), and the static-panel seam.

XML: `EQUI_MerchantWnd.xml`, `EQUI_BankWnd.xml` (NOT `BigBankWnd`/
`GuildBankWnd`/`BarterMerchantWnd`/`PointMerchantWnd` — post-Velious, the
era rule), `EQUI_CursorAttachment.xml`.

1. Merchant window: the slot grid (`A_RecessedBox` buttons), the item
   list, price, Buy/Sell buttons, quantity — every element quoted from
   the XML; the slot cells draw icons via T430's sheets; the list is fed
   by the existing merchant producers (`/vendorlist` is the seam). Buy
   and Sell buttons route to the existing `/buy` `/sell` handlers (no new
   protocol code). Opens when the server sends the merchant open packet
   (read the producer), closes with ESC/close button per T363/T368 shape.
2. Bank window: the 8 bank slots (+ shared? quote the era XML) drawing
   icons; the `/bank` seam; opens on the bank packet.
3. Cursor attachment: the item held on the cursor draws its icon at the
   mouse (`EQUI_CursorAttachment.xml`), which is what makes pick-up/
   drop visible at all. Read how fable models the cursor item today
   (inventory slot 0 / `cursorItem`) and draw that.
4. `include/client/graphics/ui/CLAUDE.md`'s panel table gains the three
   rows; `ui_panel_capture` gains the three panels (with real icon ids in
   the fixtures, T430's lesson) into `review/T436/`.
5. Tests pre-fix failing: each panel renders its XML elements at the
   scaled placements; the merchant list shows a fed item with its icon and
   price; Buy hit → the existing buy intent; cursor item draws at the
   mouse.

## Proof

Build (background), `ctest -j6` (no expected reds on this tip), pre-fix/
post-fix, G05 in place (three render passes are added), visual gate
standalone + ARM (no image golden should move), file sizes (new files
under 1,500; extract per panel as T434 did). Strike B429 with proof. Live
proof (a real vendor, the T342 rig) is OWED to a lock unit — say so in the
Review, do not fake it.

STOP: an image golden moves.

---

## Review

Branch `task/T436-merchant-bank-cursor`, base **e240964e**. No eqemu lock (T428
held it); **the live proof is OWED** and is stated as owed in the board strike,
in `review/T436/README.md` and again at the foot of this section.

### 1. What B429 actually was, and one correction to it

Three producers filled an `iconId` nothing could draw because the three windows
did not exist. All three now exist. **One thing B429 said is wrong and is
corrected here:** it quotes `A_RecessedBox` / `A_DragItem` / `<DecalSize>40x40`
and calls them "slot buttons", plural — a slot grid. `EQUI_MerchantWnd.xml` has
no slot grid. That template belongs to **one** button, `MW_SelectedItem`, the
box that shows the item under consideration. The merchant's stock is a
`<Listbox>` (`MW_ItemList`) with seven `<Columns>`, the first of which is a
24px cell with an EMPTY `<Heading>` — that is the icon column, and it is where
the icons the plan is about are drawn.

### 2. Element tables — every `<Pieces>` entry, quoted

#### `EQUI_MerchantWnd.xml` — `<Screen item="MerchantWnd">` 374x480 at (0,25),
`Style_Sizable true`

| XML element | state |
|---|---|
| `MW_MerchantName` | **drawn** — `AlignCenter true`, at the top of its box (see below) |
| `MW_ItemList` (7 `<Columns>`: 24 / 150 "Item Name" / 28 "Qty" / 48 Header_PlatCoin / 24 Header_GoldCoin / 24 Header_SilvCoin / 24 Header_CoppCoin) | **drawn + wired** — icon cell, name, qty, and the price split across the four coin columns; a row click selects |
| `MW_SelectedItem` (`A_RecessedBox` + `A_DragItem`, DecalOffset (2,2), DecalSize 40x40) | **drawn** — the selected row's icon, or the CURSOR item's in sell mode |
| `MW_SelectedItemLabel` | **drawn** — blank when nothing is considered (the oracle's own text is the placeholder "Item Being Considered") |
| `MW_SelectedPriceLabel` | **drawn** — the row's price; blank in sell mode, because a sale value is the server's to decide (T342: the whole exchange is one BuyItem) |
| `MW_Buy_Button` | **drawn + wired** → the existing `VendorBuyIntent{npcId, vendorSlot, 1}` |
| `MW_Sell_Button` | **drawn + wired** → the existing `VendorSellIntent{npcId, CURSOR_SLOT, qty}`. **It shares `MW_Buy_Button`'s rect byte for byte in the shipped file** (both `10 / 44-from-bottom / 74-from-left / 6-from-bottom`), so only one can ever show; fable shows Sell when the cursor holds an item. Board **B443** — which way the oracle swaps is unmeasured |
| `MW_Done_Button` | **drawn + wired** → the existing `VendorToggleIntent{}` (its handler closes an open session) |
| `MW_DoneButton` (note: no underscores, `ScreenID DoneButton`) | **not drawn, and not a gap** — it is declared with a full anchor set and appears in NO `<Pieces>` list, so the official client never instantiates it |
| `Style_Titlebar` / `Style_Closebox` | **not drawn** — fable's centre popups have no window chrome (the tradeskill window is the precedent); Done and ESC are the close controls |

#### `EQUI_BankWnd.xml` — `<Screen item="BankWnd">` 108x347 at (0,25),
`Style_Sizable false`

| XML element | state |
|---|---|
| `BW_BankerName` (0,1) 100x40 | **drawn** |
| `BW_BankSlot0..7` (40x40, `A_RecessedBox`, `ItemOffset` 2, `EQType` 2000..2007) | **drawn + wired** (icon + stack count; hover). Column-major: 0..3 left, 4..7 right. **Slot 3's x is 11, not 10** — the shipped file's own off-by-one, carried verbatim |
| `BW_Money0..3` (80x24, `A_PlatinumCoin`/`A_GoldCoin`/`A_SilverCoin`/`A_CopperCoin`, DecalSize 18x18) | **drawn** from the existing `BankCurrencyChanged` event (a LOG_TRACE stub with no consumer until now) |
| `BW_ChangeButton` (2,297) 50x24 | **drawn DISABLED, not hit-testable** — board **B441**. `/bank convert` needs a from/to/amount triple no single click supplies |
| `BW_DoneButton` (55,297) 40x24 | **drawn + wired** → `CloseBankIntent{}` (new) |
| `Style_Titlebar` + `Style_Closebox` | **drawn** — the bar holds `<Text>Bank</Text>`; the box pushes the same `CloseBankIntent`. `TITLE_H = 18` is fable's own and is flagged as such |
| `BW_SharedBankLabel`, `BW_SharedBankSlot0`, `BW_SharedBankSlot1`, `BW_SharedMoney0`, `BW_AltStorageButton` | **not drawn — because the ERA FILE gives them no geometry.** Each is in the `<Pieces>` list and carries a `<ScreenID>` and NOTHING else: no `<Location>`, no `<Size>`, no draw template. That is the answer to the plan's "(+ shared? quote the era XML)" question, quoted rather than guessed, and `BankLayoutOracle.TheEraFileDeclaresTheSharedBankWithNoGeometry` fails if a future file gives them one |

**Eight slots, not sixteen.** `inventory_constants.h` declares `BANK_COUNT = 16`;
that is a post-Velious bank and the constant is left alone (the wire range is
its). The window draws the eight the era file declares.

#### `EQUI_CursorAttachment.xml` — `<Screen item="CursorAttachment">` 50x50,
`Style_Transparent true`, `Style_Border false`

| XML element | state |
|---|---|
| `CA_Anim` (5,5) 40x40, `<Animation>BuffIcons</Animation>` | **drawn** — the held item's icon, a 1:1 blit at the sheets' own cell size |
| `CA_Anim2` (5,5) 40x40 | **not drawn** — no fable producer; board **B440** |
| `CA_SpellGem` (5,5) 40x32 | **not drawn** — fable's cursor holds an `ItemInstance` and nothing else; board **B440** |
| (no count label anywhere in the screen) | **nothing drawn** — the same fact T413 recorded when it deleted `CursorItemChangedData::queueSize` (B296) |

The window's `<Location>(0,0)` is a placeholder, not a placement: the official
client moves this screen to the mouse every frame, which is the whole point of
an attachment. The 50x50 box follows the pointer and is clamped at the screen
edge (the one number here that is not the oracle's, and it is a clamp).

### 3. How each window opens and closes, and from which producer

| window | opens | closes |
|---|---|---|
| Merchant | `VendorWindowOpened`, published by `MerchantComponent`'s `OP_ShopRequest` handling after `RequestOpenVendor()`; stock arrives item by item on `VendorItemAdded` from `ZoneProcessVendorItemToUI` (`ITEM_PACKET_MERCHANT`, type 100) | `MW_Done_Button` or ESC → `VendorToggleIntent` → `CloseVendorWindow()` → `OP_ShopEnd` → the client publishes `VendorWindowClosed`, and only THAT closes the panel. Never optimistic (T290's rule) |
| Bank | `BankWindowOpened`, published by `MerchantComponent::OpenBankWindow()` (reached from `BankerInteractIntent` — the ctrl+click gesture in `overlays_component.cpp` and `/bank`) | `BW_DoneButton`, the close box, or ESC → `CloseBankIntent` → `CloseBankWindow()` → `BankWindowClosed` |
| Cursor item | the enriched `InventorySlotChanged` for slot **30** (or the wire's 33) — published by `merchant.cpp`'s MoveItem arm, `merchant_trade.cpp`'s loot arms and `zone_dispatch.cpp`'s generic ItemPacket choke point | the same event with `hasItem=false` |

**ESC order** follows draw order exactly: Options → item display → bank →
merchant → the centre popups (`processKey`) → clear target.

### 4. The cursor-item model I found

`InventoryManager::cursorQueue_`, a `std::deque<std::unique_ptr<ItemInstance>>`
in `src/client/inventory/inventory_cursor.cpp`. `getItem()`, `getItemMutable()`
and `hasItem()` all special-case `CURSOR_SLOT` (30) and `SERVER_CURSOR_SLOT`
(33) to the queue's front, so slot 30 is a real addressable slot everywhere.

**What I did NOT use, and why.** `CursorItemChangedData` looks like the feed and
is not: T413 (B296) reduced it to a single `hasCursorItem` bool and pinned that
with `EXPECT_EQ(sizeof(CursorItemChangedData), sizeof(bool))`, so it carries no
icon and cannot gain one without breaking that pin. Its bridge arm stays the
LOG_TRACE it was. The feed is the ordinary enriched `InventorySlotChanged` for
slot 30, which has carried the icon since S2 — the bridge's arm covered 0..29
and the WORLD container range and dropped it on the floor. Same for 2000..2007.

### 5. Tests — names, pre and post

`tests/test_merchant_bank_cursor.cpp`, **32 cases**, registered in
`tests/cmake/ui_rendering.cmake`. No device, no database, no server.

**The pre-fix red.** Most of this unit's cases cannot exist before it (they name
files T436 creates), so the pre-fix evidence is the four-case **source census**,
which reads the tree at runtime through `WILLEQ_REPO_ROOT` and therefore runs
unchanged against the pre-T436 sources:

```
git diff -- src/client/merchant/merchant_vendor_bank.cpp \
            src/client/bridge/irrlicht_bridge.cpp \
            src/client/graphics/frame/renderer_frame_render.cpp > prefix.patch
git apply -R prefix.patch        # the three files as fable has them
build/bin/test_merchant_bank_cursor --gtest_filter='B429Census.*'
```

| case | pre-fix | post-fix |
|---|---|---|
| `B429Census.TheVendorProducerShipsTheIconItParsed` | **FAIL** | pass |
| `B429Census.TheFiveWindowBridgeArmsAreNotStubs` | **FAIL** | pass |
| `B429Census.TheCursorAndBankSlotsAreRoutedOffTheInventoryArm` | **FAIL** | pass |
| `B429Census.TheThreeWindowsAreActuallyDrawn` | **FAIL** | pass |

`0 PASSED, 4 FAILED` reverted; `4 PASSED` re-applied (`git apply prefix.patch`,
no stash — the shared-stash rule). The binary is the same one in both runs,
which is the point: those four cases are source parses, so they measure the
TREE, not the build.

The other 28 (all pass): `MerchantLayoutOracle` x3 (every element inside
`centerPopup` at 640x480 / 800x600 / 1024x768; Buy and Sell share one rect and
`MW_DoneButton` is in no `<Pieces>`; the seven columns tile with no gap),
`BankLayoutOracle` x3 (eight slots, column-major, slot 3's x of 11; unscaled and
wholly on screen at 640x480; the shared-bank stubs have no geometry),
`CursorAttachmentLayoutOracle` x2, `MerchantApplier` x5, `BankApplier` x3,
`CursorApplier` x1, `CommerceClickTest` x11.

**Two of those cases failed on their first run and found real defects**, both
fixed here:
1. `TheSevenColumnsTileTheListFromItsLeftEdge` — computing a cell as
   `scaled(x) + scaled(w)` opened a 1px gap between columns 1/2, 2/3 and 4/5
   under integer division. Both edges now go through one `scaledX()`.
2. `TheEraFileDeclaresTheSharedBankWithNoGeometry` — my first version matched an
   exact whitespace-bearing string. Rewritten to extract the element body and
   assert it holds no `<Location>` and no `<Size>`.

### 6. Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config) | **exit 0** |
| `ctest -j6` | **4523/4523**, 0 failures |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| Visual gate | **PASS**, `=== VISUAL GATE: PASS ===`; no image golden moved (`git status tests/goldens/` shows only `frame_render_order_g05.txt`) |
| `scripts/build-arm-noble.sh` | **exit 0**, `ELF 32-bit LSB pie executable, ARM, EABI5` |
| `check_file_sizes.sh` | 20 tracked ok / 0 fail; the ten NEW files checked explicitly, largest 566 (`test_merchant_bank_cursor.cpp`) and 329 (`merchant_layout.h`). One TU per panel, as the plan required |
| `FileSizeCeiling`, `DocCitations`, `DocCounts` | all pass |

**G05.** Learned IN PLACE: four pure insertions, no text change and no reorder
anywhere (verified by composing the list with the test's own normalisation and
diffing against the pre-edit golden — the only hunks are the four insertions).
320 → **328**: entries 262-266 (the two cursor-mouse writes plus the three lines
of `processCommerceInput`), 295 (`renderMerchantPopup`), 297
(`renderBankWindow`), 301 (`renderCursorAttachment`, last of everything).
`kGoldenSize` bumped with a comment block naming each position, per batch-G
correction 32.

**`DocCounts` went red and is fixed, not waived.** The new `CloseBankIntent`
took `RendererIntent` from 41 alternatives to 42, and
`include/client/bridge/CLAUDE.md` stated 41. Both that heading and the root
`CLAUDE.md`'s sentence now say 42, and the Commerce bullet lists the new intent.

### 7. Build-system incidents (not defects in this branch)

Two, both the killed-build trap, both recorded because the sweep in the protocol
did not catch the second:
- A first background launch used `nohup … &` inside a backgrounded tool call and
  was killed mid-configure with no exit file. Relaunched properly.
- One `cmake --build` hit the 600 s tool ceiling (exit 143). The sweep found a
  **3.4 GB `build/lib/stQvfwQT`** (a killed `ar` temp, T359's shape) and deleted
  it; all 331 binaries had valid ELF magic and `libwilleq_core.a` had 354
  members, so the documented sweep reported clean. It was NOT clean:
  `build/bin/test_asset_loading` was a valid 62 MB ELF that **segfaulted at
  `--gtest_list_tests`**, and cmake would not rebuild it because it was newer
  than its inputs. `ctest` reported it as a discovery error naming that test.
  Deleting and rebuilding the one binary fixed it. **A fifth shape for the
  post-143 list: valid ELF, plausible size, newer than its inputs, and it
  crashes.** The cheap detector is to run `--gtest_list_tests` over every
  `build/bin/test_*` in one pass (~2 min here) rather than letting ctest find
  them one per suite run. Two earlier ctest runs also died at discovery on
  `test_spell_manager` with a plain timeout (the documented cold-cache
  artefact — `--gtest_list_tests` on it exits 0).

### 8. Deviations, flagged

1. **The bank window is CENTRED**, not at the oracle's `<Location>` (0,25),
   because fable's top-left is the player status panel. The same deviation T434
   flagged for Options and T415 for combat abilities.
2. **The merchant window is SCALED onto `centerPopup`**; the bank is not. The
   discriminator is the oracle's own `Style_Sizable`, plus the fact that the
   merchant window is 505px tall from its `<Location>` and does not fit 640x480
   while the bank's 347 does. `defaults.ini` gives `[MerchantWnd]` no
   Width/Height keys, so T434's "the shipped size never changes" argument does
   not apply to it.
3. **Buy-vs-Sell is a design choice**, made to fit a shared rect the oracle
   itself created. Board **B443**.
4. **No window chrome on the merchant window** (`Style_Titlebar` /
   `Style_Closebox` undrawn); the bank window gets a title band whose height,
   18, is fable's own and is flagged in `bank_layout.h`. Same shape as T421's
   flagged `TITLE_H`.
5. **`MW_ItemList`'s row height (20 in oracle space) is fable's**, not the
   oracle's — a `<Listbox>` carries no row height and the official client
   measures it from the window font. Same flag `TradeskillLayout::ORACLE_ROW_H`
   carries.
6. **`MW_MerchantName`'s box really is 366x452 and really does lie under the
   item list.** That is what the anchors resolve to, it is written out in
   `merchant_layout.h`, and it is not a transcription slip. An EQ `<Label>`
   paints no background and draws at the top of its box, so the visible result
   is a centred name on the first row; fable does the same and fills nothing.

### 9. Board

- **B429 struck** (this commit), with the "slot grid" correction recorded on the
  strike and the live proof recorded as OWED.
- **Five new items, B440-B444.** I first allocated B435-B439 as dispatched, then
  renumbered: `fable` moved to `96a080d3` during this unit and its plan commit
  ("T437 plan (ground sentinels B435/B436)") had already taken B435 and B436.
  B440 CA_Anim2/CA_SpellGem unproduced; B441 (C) the greyed `BW_ChangeButton`;
  B442 no pick-up/put-down gesture, so a bank slot cannot be used and SELL is
  reachable only when something else fills the cursor; B443 Buy-vs-Sell
  unmeasured; B444 the zone-in snapshot loops `slot < 30`, so a cursor item and
  the bank's contents are invisible at zone-in until something moves them.

### 10. Live proof — OWED

T436 held no eqemu lock (T428 had it) and **opened no connection to
127.0.0.1:5998**. Every capture in `review/T436/` is the render path driven from
a hand-built panel state. A lock unit still has to show: `/vendor` at a real
merchant filling `MW_ItemList` off `OP_ShopRequest`; a Buy that moves a real
purse (the T342 rig); `/bank` at a real banker filling the eight cells; an item
picked up onto the cursor drawing at the pointer; and the B443 measurement of
which button the official client shows when.
