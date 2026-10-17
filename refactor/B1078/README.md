# B1078 (GitLab #398): item icons are never stretched

Will's 2026-10-15 play session: "the vendor window item icons are squashed
and appear unnaturally square". Measured root cause (issue #398,
`/home/user/kb-evidence/kb-b1078-icon-squash/`): the three pair windows
mapped their oracle geometry into fable's fixed 400x350 popup with two
DIFFERENT factors (x by the width ratio, y by the height ratio), and the
merchant additionally blitted the 40x40 dragitem decal into the full list
cell -- 25x14 at every resolution.

The fix, per the issue's Done-when and the B1075 ruling ("windows keep
their size, the oracle's", `docs/decisions.md` 2026-10-15): the merchant
(374x480), inventory (428x460) and tradeskill (400x525) windows now draw at
the oracle's OWN size, anchored by the layout table, with ONE uniform
factor (the min of the two ratios, capped at 1) only where the screen
cannot fit them. Every `drawItemIcon()` call site blits a square through
the one helper `UIRenderer::squareIn(cell)` (`include/client/graphics/ui/
ui_renderer.h`: the centred square of side min(w,h); a cell that is
already square comes back unchanged).

Commits: d7ebdc38 (the squareIn helper + its arithmetic cases), dfd7fbf9
(merchant at its own 374x480, ORACLE_ROW_H measured 20 -> 24, ROWS_VISIBLE
follows), 3db93d76 (inventory 428x460 and tradeskill 400x525, one factor
each), then the fix-leg commits on this branch (findings 1-4, this README).

## The one uniform factor per window

`scalePct()` in each layout header: the min of the available-width and
middle-band-height ratios, capped at 1. The band is 384 tall at 800x600,
which is why only that resolution scales at all:

| window | oracle | 800x600 | 1024x768 | 1280x720 | 1920x1080 |
|---|---|---|---|---|---|
| merchant | 374x480 | 80% (384/480) | 100% | 100% | 100% |
| inventory | 428x460 | 83% (384/460) | 94% (width-bound) | 100% | 100% |
| tradeskill | 400x525 | 73% (384/525) | 100% | 96% (width-bound) | 100% |

Measured from the headers at this tip (the `m=`/`i=`/`t=` lines of the
dump table below; pinned by `tests/test_item_icon_rects.cpp` and
`tests/test_ui_layout_coopen.cpp`).

## Window rects, before / after

Before: the three windows' oracle rect mapped by separate x/y factors into
the 400x350 popup (merchant x 400/374 = 1.07 stretch, y 350/480 = 0.73
squash; inventory x 400/428, y 350/460; tradeskill x 1.0, y 350/525 = 0.67).
After: the oracle's own size at ONE factor, top-anchored at the middle
band, in its own slot of the pair (merchant left, inventory right,
tradeskill the centre slot). Coordinates are screen-space, measured from
`windowRect()` at this tip:

| window | before (all resolutions) | 800x600 | 1024x768 | 1280x720 | 1920x1080 |
|---|---|---|---|---|---|
| merchant | 400x350 (in partnerPopup) | (0,48) 299x384 | (174,48) 374x480 | (302,48) 374x480 | (622,48) 374x480 |
| inventory | 400x350 (in inventoryPopup) | (400,48) 355x381 | (578,48) 402x432 | (706,48) 428x460 | (1026,48) 428x460 |
| tradeskill | 400x350 (in centerPopup) | (264,48) 292x383 | (376,48) 400x525 | (504,48) 384x504 | (824,48) 400x525 |

Unschanged windows (already at the oracle's own size, the precedent): bank
108x347, container 100x366, trade 236x365, give 108x375, options 400x452;
the loot popup stays the 400x350 partnerPopup (its own shape, not one of
the three).

## Item-icon decal rects, before / after

The decal is the oracle's own 40x40 dragitem cell (`item_icon_atlas.h`).
"Before" is the cell the decal was stretched into; "after" is the rect
`drawItemIcon()` actually receives (every site except the merchant row
hands over a square cell directly, so squareIn() is the identity there;
the merchant row hands over squareIn(cell) explicitly -- see below):

| site | before | 800x600 | 1024x768 | 1280x720 | 1920x1080 |
|---|---|---|---|---|---|
| merchant list-row icon | 25x14 (stretched) | 19x19 | 24x24 | 24x24 | 24x24 |
| merchant selected-item decal | 44x30 (in a 44x30 box) | 32x32 (in 33x33) | 40x40 (in 42x42) | 40x40 (in 42x42) | 40x40 (in 42x42) |
| inventory equipment/general slot | 32x32 box -> stretched decal | 26x26 | 30x30 | 32x32 | 32x32 |
| tradeskill container cell | 42x42 -> stretched | 30x30 | 42x42 | 40x40 | 42x42 |
| loot row icon | 20x20 in the 25x14-squashed popup -> stretched | 20x20 | 20x20 | 20x20 | 20x20 |
| hotbar item slot | 40x40 | 40x40 | 40x40 | 40x40 | 40x40 |
| bank slot art | 36x36 | 36x36 | 36x36 | 36x36 | 36x36 |
| container cell / icon decal | 40x40 | 40x40 | 40x40 | 40x40 | 40x40 |
| trade/give slot art | 36x36 | 36x36 | 36x36 | 36x36 | 36x36 |
| item display IDW_IconButton decal | 40x40 | 40x40 | 40x40 | 40x40 | 40x40 |
| cursor attachment CA_Anim | 40x40 | 40x40 | 40x40 | 40x40 | 40x40 |

The site rows were measured from the layout functions at this tip (dump
table below); the "before" column is the pre-B1078 mapping quoted in the
issue (#398 root cause: 25x14 merchant row, 44x30 selected-item box) and
the same two-factor arithmetic the old headers used for the other cells.
Every "after" rect is square -- that is the acceptance, pinned per site at
per resolution by `tests/test_item_icon_rects.cpp`.

The ONE non-obvious cell: the merchant list-row icon column is 24px in
the XML, and under the 800x600 80% factor it comes back 19x19 (both the
column's endpoints and the row pitch scale to 19: `24*80/100 = 19`).
Two independent integer divisions (the column's x endpoints, the row's y
endpoints) can still disagree by a pixel at some future resolution or
column width, so `merchant_panel.cpp` hands `drawItemIcon()`
`squareIn(cell)` EXPLICITLY rather than the raw cell, and the test
asserts cell == decal whenever the factor is 100% -- so a change that
breaks the 24x24 square fails the test rather than quietly stretching the
decal.

## Nets

- `tests/test_item_icon_atlas.cpp` (the 4 `ItemIconAtlasSquareIn` cases):
  squareIn()'s own arithmetic -- a square cell unchanged, a non-square one
  the centred square of the short side. **Gated at registration**
  (`tests/cmake/graphics.cmake`, `IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)`)
  because its include chain reaches `<irrlicht.h>` (B1038: an ungated
  target broke the `EQT_GRAPHICS=OFF` configure, pipeline 1519
  build-headless).
- `tests/test_item_icon_rects.cpp` (new, 10 cases, gated the same way):
  every `drawItemIcon()` call site's decal rect is square at all four
  `kSupportedResolutions`, through the same layout functions the render
  bodies call; also pins the merchant's measured 24px row pitch and the
  cell == decal rule at 100% factor.
- `tests/test_ui_layout_coopen.cpp` (unchanged, still green): the co-open
  sets (inventory+merchant, inventory+trade/bank, ...) do not overlap at
  any supported resolution with the new sizes.
- `tests/test_merchant_bank_cursor.cpp` (unchanged, still green): the
  oracle XML census, the 24px icon column, the seven-column tile.
- The fix-leg's live-defect fix: `static_ui_input.cpp`'s tradeskill
  editbox-defocus hit test now reads `TradeskillLayout::windowRect()` --
  the rect `renderTradeskillPopup()`'s `drawPanel` blits -- instead of
  `layout.centerPopup`, so draw and click cannot drift (T57's rule).

## Numbers dump (measured, this tip)

Every `windowRect()` / icon rect at the four resolutions, printed by a
temporary binary against these exact headers (the binary was removed
afterwards; re-derive from the headers rather than trusting the table):

```
== 800x600 m=80% i=83% t=73%
merchant 0,48 299x384    merchantRowCell 3,89 19x19   merchantSel 67,395 33x33
merchantDecal 68,396 32x32
inventory 400,48 355x381  invSlot0 634,278 26x26       invSlot22 465,310 26x26
tradeskill 264,48 292x383  tskSlot0 467,135 30x30      tskSlot9 498,258 30x30
loot 0,65 400x350       hotbar 308,486 448x84 cols=10  hotbarSlot0 310,506 40x40
bank 292,65 108x347     bankArt0 304,117 36x36
container 350,100 100x366  contCell0 356,202 40x40    contIcon 376,156 40x40
trade 164,65 236x365    tradeArt0 296,117 36x36
give 292,65 108x375     giveArt0 324,117 36x36
itemDisplayDecal 332,112 40x40   cursor 405,305 40x40
== 1024x768 m=100% i=94% t=100%
merchant 174,48 374x480   merchantRowCell 178,100 24x24  merchantSel 258,482 42x42
merchantDecal 260,484 40x40
inventory 578,48 402x432   invSlot0 844,309 30x30        invSlot22 652,345 30x30
tradeskill 376,48 400x525  tskSlot0 655,168 42x42        tskSlot9 697,336 42x42
loot 174,149 400x350   hotbar 308,654 448x84 cols=10     hotbarSlot0 310,674 40x40
bank 466,149 108x347   bankArt0 478,201 36x36
container 350,100 100x366  contCell0 356,202 40x40      contIcon 376,156 40x40
trade 338,149 236x365   tradeArt0 470,201 36x36
give 466,149 108x375    giveArt0 498,201 36x36
itemDisplayDecal 332,112 40x40   cursor 517,389 40x40
== 1280x720 m=100% i=100% t=96%
merchant 302,48 374x480  merchantRowCell 306,100 24x24   merchantSel 386,482 42x42
merchantDecal 388,484 40x40
inventory 706,48 428x460  invSlot0 989,326 32x32         invSlot22 785,364 32x32
tradeskill 504,48 384x504  tskSlot0 771,163 40x40        tskSlot9 812,324 40x40
loot 302,125 400x350   hotbar 416,606 448x84 cols=10     hotbarSlot0 418,626 40x40
bank 594,125 108x347   bankArt0 606,177 36x36
container 350,100 100x366  contCell0 356,202 40x40      contIcon 376,156 40x40
trade 466,125 236x365   tradeArt0 598,177 36x36
give 594,125 108x375    giveArt0 626,177 36x36
itemDisplayDecal 332,112 40x40   cursor 645,365 40x40
== 1920x1080 m=100% i=100% t=100%
merchant 622,48 374x480  merchantRowCell 626,100 24x24   merchantSel 706,482 42x42
merchantDecal 708,484 40x40
inventory 1026,48 428x460 invSlot0 1309,326 32x32        invSlot22 1105,364 32x32
tradeskill 824,48 400x525  tskSlot0 1103,168 42x42       tskSlot9 1145,336 42x42
loot 622,305 400x350   hotbar 736,966 448x84 cols=10     hotbarSlot0 738,986 40x40
bank 914,305 108x347   bankArt0 926,357 36x36
container 350,100 100x366  contCell0 356,202 40x40      contIcon 376,156 40x40
trade 786,305 236x365   tradeArt0 918,357 36x36
give 914,305 108x375    giveArt0 946,357 36x36
itemDisplayDecal 332,112 40x40   cursor 965,545 40x40
```

Sources: `include/client/graphics/ui/{merchant,inventory,tradeskill,
bank,container,trade,give,hotbar,cursor_attachment,item_display}
_layout.h` (the windowRect/scalePct/cell functions),
`src/client/graphics/ui/{merchant_panel,static_panels,tradeskill_panel,
container_panel,hotbar_panel,trade_panel,bank_panel,loot_panel,
cursor_attachment,item_display_panel}.cpp` (the drawItemIcon call sites),
`tests/test_item_icon_rects.cpp` (the square-net), the oracle's own
`/mnt/projects/EverQuestP1999/uifiles/default/EQUI_*.xml`, and the
UIGAP-ORACLE/152 captures for the measured 24px row pitch
(`/home/user/kb-evidence/kb-b1078-icon-squash/`).
