# B1077 (GitLab #397) — spell-icon atlas + gem bar per EQUI_CastSpellWnd.xml

Will 2026-10-15 play session: "Spell icons were missing from the spellbook.
Spell gems on the spell gem hotbar are also missing." Fable has never had
spell-icon art; the ids were plumbed (`SpellGemState::iconId` ← `gem_icon`,
spellbook/buff rows ← `spell_icon`) and nothing drew them.

## What landed

**Census (step 1) — the three id spaces, proven, in the header.**
`include/client/graphics/ui/spell_icon_atlas.h` quotes `EQUI_Animations.xml`:
`A_SpellGems` (gemicons01..03.tga, 7x9 of 36x28, 189 cells), `A_SpellIcons`
(Spells01..07.tga, 6x6 of 40x40, 252 cells), `BuffIcons` (Spell_Icons.tga,
6x6 of 40x40, 36 cells). Row-major, zero-based — `<Vertical>false</Vertical>`
plus the salvage audit's arithmetic (the 2026-10-15 issue note). The Buff
family is the one PART-FILLED sheet: 23 of 36 cells carry art, scattered
(ids 14, 23, 24..35 empty), so it alone proves the cell ORDER —
`test_spell_icon_atlas.cpp` pins `BuffFillProvesRowMajor` +
`ColumnMajorReadingWouldBeWrong` from that fill. `gem_icon` (field 144)
indexes Gems, `spell_icon` (field 143) indexes Spells; the buff window's
`BW_Buff*_Button` decal is `BuffIcons` (`EQUI_BuffWindow.xml`'s
`ButtonDrawTemplate NormalDecal`).

**Atlas + builder (step 2).** `tools/spell_icon_atlas_builder` lifts the
three families plus the `A_SpellGemHolder` piece of window_pieces02.tga
into `data/textures/spell_icons{gems,spells,buff}.bmp` (each 1024x1024,
POW2) + `spell_icon_holder.bmp` (40x40). One header shared by builder and
runtime loader (`spell_icon_sheets.{h,cpp}`, beside `ItemIconAtlas`). The
builder is the FOURTH recipe in `scripts/build_textures.sh` (alongside the
64 procedural, T417's spell-sprite sheet and T430's item-icon sheets); pins
updated (`data/textures.sha256`, `data/willeq.eqe1.sha256` — the pack grew
9,726,596 → 13,923,652 bytes, +4,197,056: three ETC1 family atlases with
mips + their three ETC1 `_alpha` companions + the 40x40 holder piece).

**Blit (step 3).** `UIRenderer::drawSpellIcon(rect, sheet, id)` and
`drawSpellGemHolder(rect)`, same queued-command / false-when-nothing
contract as `drawItemIcon()`. `SpellIconAtlasLayout::indexForId()` is the
single bounds check (id < family count); its false return is the callers'
"no art" signal.

**Gem bar (step 4).** `include/client/graphics/ui/spell_gem_layout.h`
quotes `EQUI_CastSpellWnd.xml`: window 48x290 (Style_Sizable false), gems
CSPW_Spell0..7 at 36x28 (3, 10+30i), CSPW_Spell8 parked at -5000 (T483:
shown nowhere), CSPW_SpellBook 22x22 at (10,252). `renderSpellGemPanel()`
draws holder + gem icon at the oracle cell, state tints and the T272
memorise frame OVER the icon, 4-letter abbreviation as the NO-PACK
fallback only. The book button toggles the spellbook through the SAME
`FrameComponent::toggleSpellbookWindow()` body Ctrl+B and `/newbook` use
(returns the new open state; closes inventory on open per B1075
`closesOnOpen`). Hit tests come from the same table (`static_ui_input.cpp`);
a gem's click rect = its drawn rect.

**Buff bar / hotbar / spellbook (step 5).** `include/client/graphics/ui/buff_layout.h`
quotes `EQUI_BuffWindow.xml`: 15 live buttons (Buff0..14) 34x34, columns
3/48/93, rows 3/49/95/141/187, decal 30x30 at the button's (2,2) corner —
the 300px bar holds 8 (pitch 37), Buff15..24 park at (3,3) as the oracle
does. Hotbar Spell slots draw the spell's `gem_icon` (the only icon column
with art in Titanium — `spell_icon` is 0 everywhere in this export);
`HotbarModel::setSpellIconFor` resolves it at publish, so JSON-loaded slots
draw too. Spellbook rows draw `spell.iconId` through the Spells family at
26x26 at the row's left.

**Tests.** `test_spell_icon_atlas` (builder output cell-for-cell vs the
source TGAs, row-major proof, hand-read ids, holder piece — 10 cases);
`test_ninth_spell_gem` + `test_buff_click_off` + `test_ui_atlas_pixels` +
`test_static_ui_panels` + `test_spellbook_memorize` retargeted to the
oracle geometry; the two NEW geometry pin cases
(`SpellGemLayout.CastSpellWndQuotedFromTheOracleXml`,
`BuffWindowLayout.BuffButtonsQuotedFromTheOracleXml`) RE-READ the oracle
XML and compare the layout header's constants, so a skin change or a slip
in a quote is a red test.

## Verified here (2026-10-16, hermes-ext)

- 163/163 ctest green across the affected suites (SpellIconAtlas,
  NinthSpellGem, BuffWindowLayout/SpellGemLayout geometry pins, BuffBar
  hit/click, TextureAlphaCensus, UiAtlasPixels, SpellDisplay,
  SpellbookMemorize, StaticUIPanels, Hotbar, PanelVisibility,
  PetButtonsT419, FrameComponent).
- `UiLayoutCoOpen` 7/7 green (the B1075 co-open table with the new 48x290
  gem window at all four resolutions).
- `TexturesReproducible` + `PackReproducible` green; file-size ceiling
  1590/1590 ok; `ZeroCallers` clean (the dead `idCarriesArt` helper was
  deleted — `indexForId()` is the bounds check).
- `willeq` + all affected test targets build (`-j3`).

## Not verified / left open

- **DocCounts is RED on the protected root `CLAUDE.md`** (4 stale counts:
  lines 301, 320, 430 "67" shipped BMPs → 71; line 422 "63 of the 67"
  32-bit → 67). This worker cannot edit a CLAUDE.md; the exact replacement
  lines are in the completion summary under `CLAUDE.md EDITS:`. Hygiene is
  otherwise green.
- **Oracle stills (acceptance E) not taken here**: no eq-oracle lane is
  reachable from this host. The geometry is quoted from stock Titanium
  uifiles (the oracle sources the card names), and every drawn rect is
  pinned to that XML by test — but the visual-record still (gem bar with
  memorised spells, buff window with a buff, open spellbook; also serves
  #396) remains to be captured on an eq-oracle lane.
- **visual_gate goldens (acceptance G) not proposed**: needs the oracle
  still + a rendered frame; propose after the lane capture. Nothing in
  `tests/goldens/` was touched.
- The `docs/decisions.md` entry and the root-CLAUDE.md count updates land
  with the MR merge (orchestrator applies the CLAUDE.md EDITS).
