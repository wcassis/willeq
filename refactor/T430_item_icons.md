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
