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
family is the one PART-FILLED sheet: 23 of 36 cells carry art, the empties
being ids 23..35 (row 3 col 5 plus the whole of rows 4 and 5) — a trailing
run. (Id 14, the diagonal cell row 2 col 2, carries art: alpha sum 77,520,
re-measured 2026-10-17; the first leg's "ids 14, 23, 24..35 empty" was a
wrong number.) The fill alone proves the cell ORDER — id 24 is an EMPTY cell
row-major but a POPULATED cell column-major — and `test_spell_icon_atlas.cpp`
pins `BuffFillProvesRowMajor` + `ColumnMajorReadingWouldBeWrong` from it.
`gem_icon` (field 144) indexes Gems, `spell_icon` (field 143) indexes Spells;
the buff window's `BW_Buff*_Button` decal is `BuffIcons` (`EQUI_BuffWindow.xml`'s
`ButtonDrawTemplate NormalDecal`). The export's own population (re-measured
2026-10-17 over data/willeq.db, 8,066 rows): gem_icon is -1 on 112 rows,
in-sheet on 7,826, and 128 rows carry one of 11 distinct ids (189, 190, 191,
193, 194, 195, 202, 204, 208, 211, 215) that outrun the 189-cell family —
those resolve to nothing, by design. The first leg's "26 ids (189..215)
outrun the gem family / 25 spells have such ids" + the dead
`kGemIdAboveSheets = 26` constant were wrong numbers and are dropped.

**Atlas + builder (step 2).** `tools/spell_icon_atlas_builder` lifts the
three families plus the `CW_SpellGemHolder` piece of classic_pieces01.tga
into `data/textures/spell_icons{gems,spells,buff}.bmp` (each 1024x1024,
POW2) + `spell_icon_holder.bmp` (40x40). One header shared by builder and
runtime loader (`spell_icon_sheets.{h,cpp}`, beside `ItemIconAtlas`). The
builder is the FOURTH recipe in `scripts/build_textures.sh` (alongside the
64 procedural, T417's spell-sprite sheet and T430's item-icon sheets); pins
updated (`data/textures.sha256`, `data/willeq.eqe1.sha256` — the pack grew
9,726,596 bytes in the first leg's holder; the fix leg re-cut the holder
piece, same pack size, new pin — measured at the bottom of this file).

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

**The holder (fix leg, finding 4) — the CW piece is shipped, not the A piece.**
The first leg shipped `A_SpellGemHolder` (window_pieces02.tga (208,216) 39x32,
EQUI_Animations.xml line 12495) even though the gem bar's own window,
`EQUI_CastSpellWnd.xml`, names AND locally defines a different one: all eight
`CSPW_SpellN` carry `<Holder>CW_SpellGemHolder</Holder>`, and the file's line 5
defines it as `classic_pieces01.tga` at (41,123), 36x28 — a socket frame 1:1
with the 36x28 gem cell. The A piece is 3 px wider / 4 px taller than the cell
and is the piece the oracle's `EQUI_CursorAttachment.xml` and HotButtonWnd use.
Per the oracle rule (copy the official client's UI/behavior; prefer the client
files), the fix leg moved the header holder constants + the builder + the test
+ the pack pins all to `CW_SpellGemHolder`; the ruling is one entry in
`docs/decisions.md` (2026-10-17, B1077 fix leg).

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
source TGAs, row-major proof, hand-read ids, holder piece — 11 cases);
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
- **Oracle still (acceptance E) captured on lane 1** (eq-oracle-1,
  2026-10-16): official Titanium client, Testcaster with 2 memorised
  spells, zoned to qeynos2. `06_main.png` (evidence dir
  `/home/user/kb-evidence/kb-b1077-spell-icon-atlas/oracle_capture/`)
  shows the **cast-spell (gem) window on the left edge**: top two cells
  carry the memorised spells' icons (a shield + one more), the rest of
  the 8-cell column is empty, and the **book button sits at the bottom**
  — matching `CastSpellWnd` (48x290, 8 gems 36x28, book 22x22 at
  (10,252)) and the layout pinned by the XML-geometry tests. The buff
  window and open spellbook were not open in this default session
  (the `/cast` line landed with the help popup holding focus), so a
  buff-window-with-live-buff and an open-book still remain; those
  serve #396 and are left open. Lane restored (DB seed rolled back,
  lock released, stack down).
- **visual_gate goldens (acceptance G) not proposed**: needs the oracle
  still + a rendered frame; propose after the lane capture. Nothing in
  `tests/goldens/` was touched.
- The `docs/decisions.md` entry and the root-CLAUDE.md count updates land
  with the MR merge (orchestrator applies the CLAUDE.md EDITS).

## Fix leg (2026-10-17, MR !210 review findings 2, 3, 4)

Review t_82c9a071 (MR notes 5418/5419) closed findings 2-4 in one leg; the
CODE and TESTS were already verified correct by QA + review, so this leg
touched only the three findings plus docs:

- **Finding 4 (oracle mismatch, oracle-answered) — ship the CW holder.**
  All eight `CSPW_SpellN` in `EQUI_CastSpellWnd.xml` carry
  `<Holder>CW_SpellGemHolder</Holder>` and the file DEFINES it locally
  (line 5): `classic_pieces01.tga` (41,123), 36x28 — 1:1 with the gem cell.
  The first leg shipped the 39x32 `A_SpellGemHolder` of
  window_pieces02.tga (EQUI_Animations.xml) instead. The header holder
  constants (`kHolderW/H/SrcX/SrcY`), the builder (`packHolder()` reads
  classic_pieces01.tga), the test (constants + `HolderEqualsTheCwWindowPiecesCut`
  re-reads the CW cut) and the pack pins all moved to the CW piece. Ruling
  in `docs/decisions.md` (2026-10-17, B1077 fix leg).
- **Finding 2 (wrong number) — the gem census.** Re-measured 2026-10-17
  over data/willeq.db (8,066 rows): 128 rows carry `gem_icon >= 189`,
  spanning 11 distinct ids {189,190,191,193,194,195,202,204,208,211,215}.
  The first leg's "26 ids (189..215) outrun the gem family" / "25 spells
  have such ids" and the dead `kGemIdAboveSheets = 26` constant are
  restated/dropped (spell_icon_atlas.h, this README).
- **Finding 3 (wrong number) — the buff-empty set.** Re-measured 2026-10-17
  over Spell_Icons.tga: the true empties are ids 23..35 (13 cells, a
  trailing run; kBuffPopulated=23 and the tests were already right). Id 14
  carries art (alpha sum 77,520); the header's "ids 14, 23 and 24..35
  empty" prose is corrected, and id 24 stays the order-proving cell, as
  `BuffFillProvesRowMajor` already used it.

Measured (2026-10-17, hermes-ext): the three family BMPs regenerate
bit-identical to their pins (only the holder input changed); the new
`spell_icon_holder.bmp` is 13de32369bcb38df... (the 36x28 CW cut on a
40x40 canvas); the shared pack was rebuilt from the same inputs — same
size, 13,923,652 bytes, new bytes (pin now
02a3d6e4e71fa1681291972eb40bae956a14153a9309a318b1831c0a193c0084) — and
`build_willeq_pack.sh --check` passes against the pin. `test_spell_icon_atlas`
runs green against the regenerated assets (11/11 on the ThinkPad, including
`HolderEqualsTheCwWindowPiecesCut`); `test_texture_alpha_census` (which names
the holder entry) compiles clean — its one line still calling the shipped
piece `A_SpellGemHolder` (besides `ui_atlas.h`'s loader doc) was corrected in
this same leg.

Pipeline 1540 at head f216bd70 (hygiene job 8134, read from its trace):
37/38 hygiene tests pass; the ONE failure is DocCounts, on exactly the 4
pre-existing root-CLAUDE.md rows from MR note #5414 (T417/T430's 67→71 BMPs
at :301/:320/:430 and 63→67 32-bit at :422) — identical to a local run of
`check_doc_counts.py` at the head, none of them in B1077's files. `build-test`
and `gate` are stage-gated behind hygiene (no `needs: []`), so they are SKIPPED
while those 4 rows stay red; `zone-budget-census` (gate stage, `needs: []`)
passed. `test_spell_icon_atlas` is 11/11 on the ThinkPad at both fix-leg
heads (including `HolderEqualsTheCwWindowPiecesCut`) and
`test_texture_alpha_census` (which names the holder entry) compiles clean;
the full build-test+gate green comes when the orchestrator applies
note #5414's 4 root-CLAUDE.md lines and the pipeline re-runs (the narrow QA
pass re-grades that head). The finish commit 01fac9e9 is code-touching
(two comment-only line fixes in ui_atlas.h + test_texture_alpha_census.cpp
that still named the SHIPPED piece as A_SpellGemHolder), so its MR pipeline
1542 is a code-change pipeline: hygiene first (same 4 rows), then
build-test + gate — the same outcome as 1540 unless the orchestrator
applies note #5414 in the meantime.
