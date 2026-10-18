# B1076 — the spellbook window per EQUI_SpellBookWnd.xml (GitLab #396)

Will's 2026-10-15 play session: "the spellbook in willeq does not match the
official client". This unit rebuilds the spellbook popup from the oracle's own
`uifiles/default/EQUI_SpellBookWnd.xml` (stock Titanium, the source the layout
table quotes) and the oracle session capture, and carries the book art from
the client's own `default_spellbook01-05.tga`.

Commits on `task/kb-b1076-spellbook-window` (MR !223, `Closes #396`):

- Step 2 layout: `include/client/graphics/ui/spellbook_layout.h` +
  `tests/test_spellbook_layout.cpp` — every piece of the 510x360 window quoted
  from the XML (the sixteen 44x44 slot buttons, two 2x4 grids per spread with
  the irregular row Ys 24/97/169/242; the 84x16 name text under each; the
  LeftPageNum/RightPageNum; the 18x32 edge arrows; the 100x8 memorize gauge at
  (204,312); the 13x13 Done X at (449,5); the 1x1 MemPage buttons; the
  256x256/256x107 backdrop tiles).
- NEW: `tools/spellbook_backdrop_builder` + `include/.../spellbook_backdrop.h`
  — composes the official `default_spellbook01-05.tga` (4 backdrop tiles +
  the Done X) into ONE 512x363 BMP. Wired into `scripts/build_textures.sh`
  (fifth recipe), pinned in `data/textures.sha256` (71→72), and the pack
  rebuilt with the new pin in `data/willeq.eqe1.sha256` (14,173,156 B).
  Loaded eagerly and protected by `SpellBookBackdropLoader` beside UIAtlas
  (the item-icon-sheet rule: first-use loading would turn a pack miss into a
  mid-session `std::exit(1)` the moment a player opens the book).
- Step 3 render: `renderSpellbookPopup()` draws from the table through
  `UIRenderer::drawSpellBookBackdrop()`: the composed backdrop, 16
  icon+name slots (B1077's `drawSpellIcon()`, no level), the book's
  page-number pair, the Done X, the memorise + scribe gauges.
  `SPELLS_PER_PAGE` is 16 (a page = the two-page spread). Headless keeps a
  flat-panel fallback.
- Step 4 input: the hit test moved from the old 28px text rows to the same
  `SpellBookLayout` table. The gestures are the ones MEASURED on the
  official client (oracle capture 4 — the ledger, not the XML), below.
  `ScribeSpellFromScrollIntent` (new) carries the two-click scribe's spell
  id + scroll slot; the consumer resolves the destination book slot
  (`SpellManager::findFreeSpellbookSlot()`, T276) and runs `/scribe`'s exact
  wire sequence. `scrollSpellId` now travels on `InventorySlotChangedData`
  (fed at the five publish sites) so the render thread can arm the gesture
  from its own slot cache.

## Oracle capture (Step 1) — the gesture specification

Lane 1 official client, `Testcaster` (level 69, 16 scribed spells), qeynos2,
run ONCE through `scripts/eqemu/oracle/oracle_session.sh` with
`refactor/B1076/plan4.tsv`. Ledger `oracle_capture4/steps.tsv` (47 steps),
stills per step, `marked.mp4` + `video.mp4`, `capture.pcap` with
per-step opcode tables, `ledger_check.txt`: **0 broken references**.
Evidence dir: `/home/user/kb-evidence/kb-b1076-spellbook-window/oracle_capture4/`.

| Gesture | Ledger steps | What the oracle does |
|---|---|---|
| Left click a book slot | 14-16 | MEMORISE — server chat: "You are not high enough level to memorize that spell" (the attempt fired; Testcaster's 16 scribed spells are all level-69-reachable, so the gauge never ran; opcode table 14: 123 rows) |
| Right click a book slot | 11-13 | Spell MOVE/SWAP — chat: "Right click on another Scribe Slot in your Spell Book to swap this Spell position". The step's opcodes are EMPTY of a swap packet: the move is client-local until a second slot is clicked |
| Drag a slot onto a gem | 17-19 | MEMORISE (same level-gated refusal; the gem window settles unchanged) |
| Scribe (scroll → open slot) | 38-43 | TWO-STEP: click the spell scroll in the inventory (32-34), then click an open slot in the book (38-41 LEFT / 41-43 LEFT, right-drag 38 also tried) — the oracle's own Spellbook Tip window says exactly this. NOT a drag |
| Page arrows | 20-25 | PageUp/Down flip the spread. Testcaster has exactly 16 scribed spells = ONE spread, so the arrows did not visibly turn — the stills show the same 16 slots; the direction is the XML's (PageDown at the left edge, PageUp at the right) |
| Done X | 26-28 | Closes the book |
| MemPage 1x1 buttons | — | Not separately probed; they are the whole-page hit targets and are present in the table |

Fable's implementation: left click = memorise (T272 gem-pick contract kept),
right click = the client-local swap, two-click scroll→slot = scribe, Done X =
close (the shared toggle body, same as ESC/`/book`), arrows = page. Shift+click
keeps T425's spell display.

## Not verified / left open

- The memorise GAUGE running to completion: level-gated at Testcaster, so the
  capture proves the attempt fired, not the gauge fill. The gauge renders at
  the quoted (204,312) with the quoted tints (render leg).
- A page turn with a 2nd spread: not reachable with 16 scribed spells (the
  plan's >= 17 ideal was noted and not met). The arrow geometry and the
  16-spread paging are what the capture covers.
- The scribe gauge likewise unmeasured (the scroll scribe is level-gated too).
- Empty-slot name label: the capture's book has 16/16 slots filled; fable
  renders an empty slot as icon-less with no name text (the old behaviour),
  not re-measured.

## Acceptance

- (A) oracle ledger + stills + video attached, gestures for all three
  inputs recorded, 0 broken refs — MET (evidence dir above).
- (B) layout table equals the XML — MET (`test_spellbook_layout.cpp` green).
- (C) book renders both pages with icons, names, page numbers, arrows, Done,
  gauges, book art — MET (render leg; headless flat-panel fallback).
- (D) gestures match the ledger — MET (table above cites the steps).
- (E) co-open test green at all four resolutions — MET
  (`test_ui_layout_coopen.cpp`, spellbook+gems and spellbook+HUD sets).
- (F) hygiene + build-test green at the MR head — CI (see MR pipeline).
- (G) no bless-only file touched; golden proposed, not committed — MET
  (no `tests/goldens/` or `manifest.json` changes; the visual_gate golden of
  the open book is proposed in the MR, not committed).
- (H) this "Not verified / left open" — as above.

## Finish leg (2026-10-17, after the fable merge)

The 23:01 `origin/fable` merge brought in B1077's CW-holder
`spell_icon_atlas_builder` source while the rbuild tree still held the
pre-merge binary; the stale binary regenerated the old `spell_icon_holder.bmp`
and `TexturesReproducible` went red against B1077's pin. Rebuilding the
builder from the merged source clears it (not a source regression).

The render/input step also made the twelve `SpellBookLayout` accessors
production-called, which the `ZeroCallers` census caught as stale exemptions
(retired in `check_zero_callers.py`), and the old scaled
`spellbookMemorizeGaugeRect()` became dead (the window now draws at the
oracle's own 510x360, so the gauge is `SpellBookLayout::memorizeGaugeRect()`
blitted 1:1); the function and its three scaled-placement tests went, and the
placement stays pinned by `test_spellbook_layout`.

Local (rbuild, ThinkPad) at the finish-leg tip: the targeted ctest set
(SpellBookLayout, SpellbookMemorize, UiLayoutCoOpen, TextureAlphaCensus,
CastingBarT413, TexturesReproducible, PackReproducible) 45/45, and hygiene
38/39 with the only red the five `DocCounts` rows on the protected
CLAUDE.md files (see the completion summary's `CLAUDE.md EDITS` block; the
MR pipeline's hygiene job is red on exactly that until the orchestrator
applies them, per B1077's precedent).

## Lane record

Lane 1 (eqemu lane, oracle box): lease `t_304d99ff/1156` taken 2026-10-17
~23:05 UTC; capture 4 ran 22:30–22:55 (run 1144) exit 0; the stack was
stopped with `down.sh` and the lease released with the recorded identity
(lease_release.sh) — `eqemu_lock_show` = `state=FREE` after.
