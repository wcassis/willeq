# T430 — item icons from the official sheets (B376, Will: critical)

Class A, no server. Read B376, T421's §Review (the item display's
`IDW_IconButton` decal and `ItemInstance::icon`), T417/T398's builders
(a texture atlas from official files, companion alpha, the pack cascade),
`/mnt/projects/EverQuestP1999/uifiles/default/dragitem*.tga` (the icon
sheets: 6x6 of 40x40 per sheet — verify; icon id → sheet/cell mapping is
the standard `(id-500)/36`, `(id-500)%36` — verify against
`EQUI_Animations.xml`'s `A_DragItem*` entries and quote them).

1. `tools/item_icon_atlas_builder`: packs every icon the sheets hold into
   a checked-in atlas (or several — count the sheets and size it; 32-bit
   with alpha) + a layout header; deterministic; `TexturesReproducible`
   and the pack cascade in ONE commit (atomic install, no live client
   running, old pack copied under `review/T430/`; report the mapped-bytes
   delta, never edit the manifest).
2. Draw icons: inventory slots (the 30 general/equipment cells), the
   cursor item, the item display's icon button, the loot window, the
   merchant window, bank, tradeskill container cells — every place the
   oracle draws an item icon (quote the XML decal element per window).
   `ItemInstance::icon` already carries the id.
3. Tests pre-fix failing: an icon id maps to the right sheet/cell (against
   `EQUI_Animations.xml`); a slot with an item draws its icon region;
   `ui_panel_capture` captures into `review/T430/`.
4. Strike B376.

## Proof

Build (background), `ctest -j6`, pre-fix/post-fix, `TexturesReproducible`
/`PackReproducible` green, visual gate standalone + ARM (no image golden
should move — no inventory in the gate), file sizes.

STOP: a moved image golden.

## Review

**Branch** `task/T430-item-icons`, base `72be9137`.

### 1. The sheet census, quoted not assumed

`EQUI_Animations.xml` holds exactly ONE `A_DragItem` animation
(`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_Animations.xml:6799`), and
it declares the whole geometry:

```xml
<Ui2DAnimation item="A_DragItem">
    <Cycle>false</Cycle>
    <Grid>true</Grid>
    <Vertical>true</Vertical>
    <CellHeight>40</CellHeight>
    <CellWidth>40</CellWidth>
    <Frames>
        <Texture>dragitem1.tga</Texture>
        <Location><X>0</X><Y>0</Y></Location>
        <Size><CX>256</CX><CY>256</CY></Size>
        <Hotspot><X>0</X><Y>0</Y></Hotspot>
        <Duration>1000</Duration>
    </Frames>
    ...   <!-- 34 Frames blocks: dragitem1.tga .. dragitem34.tga, in order -->
</Ui2DAnimation>
```

| fact | value | where it comes from |
|---|---|---|
| sheets | **34** (`dragitem1.tga`..`dragitem34.tga`) | the 34 `<Frames>` blocks above |
| sheet size | **256x256** 32-bit TGA | `<Size>` above; `ls -l` confirms 262,188 B for the uncompressed ones |
| cell | **40x40** | `<CellWidth>`/`<CellHeight>` |
| grid | **6 x 6 = 36** cells per sheet | 256/40 = 6 whole cells; the last 16 px of each axis are unused |
| id base | **500** | `indexForIconId()`; ids run 500..1723 |
| icons | **1224** | 34 x 36 |
| cells carrying art | **1200** | measured per-cell alpha over all 34 sheets |

**The mapping is COLUMN-major, and the plan's assumed `(id-500)%36` row-major
reading is wrong.** `<Vertical>true</Vertical>` says the grid walks DOWN a
column before moving right, so the cell for index `k` inside a sheet is column
`k/6`, row `k%6`. That was not taken on the tag's word: `dragitem34` is the
only partly-filled sheet, and a per-cell alpha census of it reads

```
[1, 1, 1, 0, 0, 0]
[1, 1, 0, 0, 0, 0]
[1, 1, 0, 0, 0, 0]
[1, 1, 0, 0, 0, 0]
[1, 1, 0, 0, 0, 0]
[1, 1, 0, 0, 0, 0]
```

— two whole COLUMNS plus one cell, thirteen icons. Row-major numbering would
have produced two whole ROWS plus one. `ItemIconAtlas.RowMajorReadingWouldBeWrong`
pins the negative too: icon 506's shipped cell equals dragitem1's (col 1, row 0)
and dragitem1's (col 0, row 1) holds different art, so a "cleanup" back to
row-major is a red test rather than 1224 wrong pictures.

**SURPRISE, and a deviation from the plan's arithmetic in a second place:**
the census is **1200**, not 1201, and the missing cell is **not** at the end.
`dragitem21` has a fully transparent cell in the MIDDLE of its grid — column 1,
row 4, column-major index 10, flat index 730, **icon id 1230**. So
`33 x 36 - 1 + 13 = 1200`. This is why `indexForIconId()` deliberately does not
treat "populated" as a bound: id 1230 maps to a real, addressable, empty cell
and draws nothing, exactly as the official client does with it.
`ItemIconAtlas.TheEmptyCellInsideTheRangeStillMaps` pins it.

### 2. The atlas layout and the pack delta

`tools/item_icon_atlas_builder` packs all 1224 cells at their NATIVE 40x40 —
the oracle draws them at 40x40 (`<DecalSize><CX>40</CX><CY>40</CY></DecalSize>`
on every A_DragItem button), so a rescale would be a fidelity loss chosen for
nothing — into **two 1024x1024 sheets**, 25 x 25 = 625 cells each (1250 cells,
26 spare). 40 and 1024 are both multiples of 4, so every base-level cell
boundary is an ETC1 block boundary (T398 Leg B's coin-pitch rule). One
2048x2048 sheet would cost 4x the texels for the same icons; 1024x2048 is the
same bytes as two 1024x1024 with no eviction granularity.

`include/client/graphics/ui/item_icon_atlas.h` is the ONE address book, included
by the builder and by the client's `ItemIconSheets` (the SpellSpriteAtlas
precedent, T417).

| | before | after | delta |
|---|---|---|---|
| `data/textures/` BMPs | 65 | **67** | +2 (`item_icons0.bmp`, `item_icons1.bmp`, 4,194,426 B each) |
| `willeq.eqe1` bytes | 6,930,116 | **9,726,596** | **+2,796,480** |
| `willeq.eqe1` sha256 | `ac78f333…a62abb` | `6549a368…386d11` | |
| pack entries | 127 tex / 62 alpha / 1 raw | 129 tex / 64 alpha / 1 raw | +2 ETC1 + 2 `_alpha` companions |
| **`eqe1MappedBytes` per zone** | — | — | **rises by exactly +2,796,480**, the pack's own byte delta (the T417/B357 shape) |

No `--rgba8888-texture` override was needed: the UI draws these through
`draw2DImage(..., useAlphaChannelOfTexture=true)` i.e. `UI2D_FS`, which since
T411 samples the ETC1 `_alpha` companion on unit 1, and nothing here minifies a
4x4 sprite onto a 1px rect the way `ui_atlas` does. Census entry added to
`tests/test_texture_alpha_census.cpp` (`itemIconSet()`), pinned count 65 -> 67.

**Install:** `scripts/build_willeq_pack.sh` (atomic temp + `mv`), **75.56 s**
wall, no `willeq`/ctest/gate process running at the time (`pgrep -a -x ctest`
empty; a `ctest -j6` against `main` had been running earlier and the install
was held until it finished). The pre-change pack is preserved at
`/mnt/projects/fable/review/T430/willeq.eqe1.pre-T430` (6,930,116 B, sha
`ac78f333…`). The manifest was NOT edited.

Builder determinism: two runs into different directories produced identical
sha256 (`c622aa37…` / `55364c74…`). The packing is a straight `memcpy` of 40x40
cells — no arithmetic that could differ between `-O0` and `-O2` (board B268's
rule).

### 3. Where icons now draw

`UIRenderer::drawItemIcon(rect, iconId)` returns **false** when there is
nothing to draw (no atlas, no pack entry, an id outside 500..1723), and every
caller keeps its pre-T430 text placeholder for that case — so a pack-less
client or a headless test looks exactly as it did.

| window | fable draw site | the oracle's own decal element |
|---|---|---|
| Inventory, 22 equipment slots | `renderInventoryPopup()`, `static_panels.cpp` | `EQUI_Inventory.xml`'s `<InvSlot item="InvSlot0">` … `<Size><CX>42</CX><CY>42</CY></Size>` — an InvSlot's decal is implicit (the client draws A_DragItem into it by `<EQType>`), so the explicit quotation is the Container/Merchant/Tradeskill form below |
| Inventory, 8 general slots | `renderInventoryPopup()` | same |
| Tradeskill container cells | `renderTradeskillPopup()`, `tradeskill_panel.cpp` | `EQUI_TradeskillWnd.xml:477` `<ButtonDrawTemplate><Normal>A_RecessedBox</Normal><NormalDecal>A_DragItem</NormalDecal></ButtonDrawTemplate>` + `<DecalSize><CX>40</CX><CY>40</CY></DecalSize>` |
| Loot rows | `renderLootPopup()`, `loot_panel.cpp` | `EQUI_LootWnd.xml:59` `<InvSlot item="LW_LootSlot0">` … `<Size><CX>40</CX><CY>40</CY></Size> <Background>A_RecessedBox</Background> <EQType>5000</EQType>` |
| Item display window | `renderItemDisplayPopup()`, `item_display_panel.cpp` | `EQUI_ItemDisplay.xml:40` `<ButtonDrawTemplate><Normal>RedIconBackground</Normal><NormalDecal>BuffIcons</NormalDecal></ButtonDrawTemplate>` on `<Button item="IDW_IconButton">` (the client swaps that decal for the item's own icon at runtime) |
| — reference for the explicit form — | | `EQUI_Container.xml:50-61` `<ButtonDrawTemplate><Normal>A_RecessedBox</Normal><NormalDecal>A_DragItem</NormalDecal></ButtonDrawTemplate>` … `<DecalSize><CX>40</CX><CY>40</CY></DecalSize>` |

**The loot window needed a wire fix, not just a draw.** `LootItemDisplay` had no
`iconId` at all, and the producer
(`src/client/merchant/merchant_trade.cpp`, the `OP_LootItem` reply parser) had
already PARSED the icon out of `field[22]` — logging it at TRACE — and then
thrown it away. `iconId` was added to `LootItemAddedData` and
`LootItemDisplay`, and `ldata.iconId = icon;` is the one-line producer fix.
The row is 22 px, not the oracle's 40, so the icon draws as a 20 px square at
the head of the row and the name follows it.

**DEVIATION — the merchant, bank and cursor-item windows in the plan DO NOT
EXIST.** A grep for a `MerchantPanelState` / `BankPanelState` / cursor-item
struct or render function under `src/client/graphics/ui/` and
`include/client/graphics/ui/` returns nothing, and the seventeen-panel table in
`include/client/graphics/ui/CLAUDE.md` lists no vendor, bank or cursor row —
the commerce vocabulary is reachable only as slash commands. The game side
already threads `iconId` through `merchant.cpp:103` and
`merchant_trade.cpp:405`/`:693` for windows that were never built. Boarded as
**B415**, not silently absorbed.

**A MEASURED design change mid-unit.** `ItemIconSheets` was written LAZY —
load on the first icon drawn, to keep 2 x 1024x1024 off a 512 MB Orange Pi that
never opens an inventory. Running `ItemIconDrawTest` against the pre-T430 pack
showed what that costs:
`ConstrainedTextureCache::getOrLoad()` logs FATAL and `std::exit(1)`s on a pack
miss (`constrained_texture_cache.cpp:235-239`, Will's all-or-nothing EQE1
rule), so the process died the moment a panel asked for an icon. **Lazy loading
converts a startup failure into a mid-SESSION exit**, which is strictly worse,
so the sheets now load eagerly beside `ui_atlas` in `UIAtlas::load()` and a
stale pack fails where every other stale-pack failure already does. Recorded in
the header so it is not re-litigated.

### 4. Tests

New file `tests/test_item_icon_atlas.cpp` (9 cases, files only — no device, no
GPU, no `willeq_core`; the TGA decode is the builder's own header-only reader,
so expected pixels are re-derived from the SOURCE rather than from a golden
nobody can re-check) and `ItemIconDrawTest` in `tests/test_ui_atlas_pixels.cpp`
(2 cases, real GLES2 device + the real pack).

**Pre-fix** (`data/textures/item_icons{0,1}.bmp` moved aside, i.e. the tree as
it stood before the builder existed):

```
[==========] 9 tests from 2 test suites ran.
[  PASSED  ] 6 tests.
[  FAILED  ] 3 tests, listed below:
[  FAILED  ] ItemIconAtlas.ShippedSheetsHaveTheDeclaredGeometry
[  FAILED  ] ItemIconAtlas.EveryCellEqualsItsSourceCell
[  FAILED  ] ItemIconAtlas.RowMajorReadingWouldBeWrong
```

**Post-fix**: `[  PASSED  ] 9 tests.`

`ItemIconDrawTest` against the **pre-T430 pack** did not skip and did not fail —
it **exited the process** (`FATAL ... Texture 'item_icons0' not found in any
EQE1 flat pack`, exit 1). That IS the pre-fix evidence, and it is what forced
the lazy -> eager change above. Against the installed pack both cases pass:

```
[       OK ] ItemIconDrawTest.OnlyIdsTheOfficialSheetsCarryResolve (188 ms)
[       OK ] ItemIconDrawTest.AGeneralSlotWithAnItemDrawsItsIconRegion (247 ms)
```

The second is the plan's "a slot with an item draws its icon region": it counts
lit pixels inside `InventoryLayout::slotRect()` for general slot 0 with and
without an item, and requires the icon to light at least 60 more than the empty
slot — a coverage assertion, so it distinguishes art from a three-glyph label
without pinning any particular item's colours.

| check | result |
|---|---|
| `cmake --build build -j24` | **exit 0** |
| `ctest -j6` | **4421 of 4422**; the one failure is `visual_gate` (see below) |
| `TexturesReproducible` (`build_textures.sh --check`) | **PASS** — 67 pinned, 67 byte-identical, 0 drifted, 0 exempt |
| `PackReproducible` (`build_willeq_pack.sh --check`) | **PASS** — rebuild byte-identical to the pin and to the installed pack |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| `scripts/build-arm-noble.sh` | see final report |
| `check_file_sizes.sh fable` | **34 checked, 34 ok, 0 warn, 0 fail** |

### 5. The visual gate — RED on three numbers, no image golden moved

Standalone (not under `-j6` load):

```
=== VISUAL GATE: FAIL ===
  - memory-coverage qeynos2:  eqe1MappedBytes mismatch (expected 124063684, got 126860164)
  - memory-coverage gfaydark: eqe1MappedBytes mismatch (expected 122729776, got 125526256)
  - memory-coverage befallen: eqe1MappedBytes mismatch (expected 117214272, got 120010752)
```

Each delta is **exactly +2,796,480**, the pack's own growth. Every CPU-p95 is
inside its ceiling (qeynos2 5.067/10.108, gfaydark 7.958/10.672, befallen
8.303/10.948), no image comparison differed, and `git status tests/goldens/` is
clean. **The manifest was not edited** — this is the T417/board B357 shape and a
bless is owed. The plan's STOP condition is a moved IMAGE golden; none moved.

### 6. Media

`/mnt/projects/fable/review/T430/` — `README.md`, `before/` and `after/`
(nineteen 800x600 panel PNGs each, captured identically by
`tools/hwcaps/ui_panel_capture`; the "before" binary is this tree with only the
four draw sites reverted to `HEAD`, so the pair differs in one thing), and
`willeq.eqe1.pre-T430`. The four item-bearing panels differ and the other
fifteen are byte-identical — `inventory_800x600.png` goes 15,114 -> 49,074 B,
`loot` 15,357 -> 18,242, `tradeskill` 27,410 -> 36,017, `item_display`
20,586 -> 21,135, while `hud_all`, `chat`, `player_status` and the rest are
unchanged byte for byte.

`ui_panel_capture`'s fixtures had no icon ids at all, so they were given real
ones (equipment `500 + i*7`, general `640 + i`, loot `500 + i*11`, tradeskill
`700 + i`) — without that the capture would have drawn the placeholder and
proved nothing.
