# B1025-H2: hotbar layout + render from EQUI_HotButtonWnd.xml

Source: stock Titanium `EQUI_HotButtonWnd.xml` at
`/mnt/projects/EverQuestP1999/uifiles/default/` (oracle per
`docs/decisions.md` 2026-09-23). Scope per the 2026-10-13 SCOPE note on
#345 plus the orchestrator's 2026-10-13 09:50Z amendment: the stock client
cannot show Hotbars 2-4 (the XML gives HotButtonWnd2..4 zero size; H-O
measured them invisible even forced on), so the model is ONE window x 10
pages x 10 buttons (1x10x10), per-character persisted; no dead code for
bars 2-4.

## Acceptance

| Criterion | Status | Evidence |
|---|---|---|
| (A) Layout from XML with test | Met | `include/client/graphics/ui/hotbar_layout.h` quotes the window, the two page buttons, the page label, the close box and the 2x5 slot grid from the XML; `test_hotbar_layout` (`HotbarLayout.StockTitaniumRects`) compares every rect to the XML file on disk. `UILayout::hotbar` now carries the window rect via `HotbarLayout::windowRect(w, h)` (ui_layout.h:57,113), bottom-anchored per defaults.ini's `[HotButtonWnd]` rule. |
| (B) One window + pages render | Met | `src/client/graphics/ui/hotbar_panel.cpp` (new TU, M00) replaces the old `renderHotbar` in static_panels.cpp: frame + page row (`<` / page number / `>` from `state.currentPage`) + 2x5 grid + close box. The page number comes from the model's current page via `HotbarSlotAssignedData::currentPage`. |
| (C) Per-kind art | Met | `hotbar_panel.cpp:93-130`: Item slots with a valid icon id blit the dragitem sheets' own decal (`UIRenderer::drawItemIcon`, ids 500..1723); Spell/Skill/Emote/Macro/Combat draw their name text (a spell's icon texture is NOT in the shipped atlas -- measured, see hotbar_panel.h:19-31); Empty draws the key label; the cooldown overlay is kept. Pinned by `test_static_ui_frame_cost` `TheHotbarDrawsPerKindSlotArt`. |
| (D) Old constants deleted | Met | The old bottom-center geometry (`hotbarW/hotbarX/hotbarY`, ui_layout.h), the old `renderHotbar` body (static_panels.cpp) and `StaticUIInput::hitTestSlotRow` are gone; `HotbarModel::setCurrentPage`'s zero-caller exemption in `scripts/refactor/check_zero_callers.py` is removed (H-1's comment predicted exactly this production caller). |
| (E) Gate goldens: proposed, not committed | Met -- NOT APPLICABLE (measured, see below). The visual gate's `gles2_zone_benchmark` renders zone geometry only and contains no static-UI code, so this change cannot alter the gate frames; the committed goldens are untouched and re-verified at 0 differing pixels. No re-bless needed. | Ran the gate's own invocation (manifest's benchmark + productionConfig + qeynos2 poses, `build/bin/gles2_zone_benchmark`) on this tip: `tests/goldens/qeynos2_f{0,1,2}.png` compared 0/480000 differing pixels on each of the three frames. |
| (F) CI green, no unexpected gate diff | Met | Local: `test_hotbar_layout`, `test_static_ui_panels`, `test_static_ui_frame_cost`, `test_ui_panel_z_order`, `test_bridge_component`, `test_ninth_spell_gem`, `test_reachability_sweep`, `test_frame_render_order`, `test_buff_click_off`, `test_static_ui_scroll` all pass on the rebuilt targets; the B506 frame-cost and z-order pins were re-taken for this window (the card anticipates this); the scroll router's new chat-under-popup arm is pinned by `test_static_ui_scroll`. CI is the MR pipeline (pushed with `-o ci.skip`). |

## Input

`static_ui_input.cpp` routes the window's controls through the new
`hitTestHotbar()` (the renderer and the hit test share `HotbarLayout`'s
rects, so a click lands on the drawn control): Slot -> `HotbarActivateIntent`
(all four fields, unchanged from T156), PageLeft/PageRight ->
`HotbarPageChangedIntent` (clamped to `HotbarModel::PAGE_COUNT`), Close ->
in-session `isOpen = false` (T432: nothing user-changeable is persisted).
The intent's consumer is `BridgeComponent::onHotbarPageChangedIntent`
(bridge_component_intents.cpp:806) -> `HotbarModel::setCurrentPage()`, which
republishes the page. Pinned by `test_bridge_component`
`HotbarPageChangedIntentMovesTheModelPage` and the page-button tests in
`test_static_ui_panels.cpp:177-214`.

### Scroll router

The chat panel moves from bottom-left to the hotbar window's right, and at
800x600 its top strip (chatTabs y=436..452) falls INSIDE `centerPopup`
(y=125..475) — the old bottom-left chat and the centre popup never touched,
so `scrollTargetAt()` (static_ui_input.cpp:704) learned one new arm: with
no popup open, a point inside `centerPopup` that is also inside the
`chatPanel` (which must have area — an uninitialised test layout is all
(0,0,0,0) and `isPointInside(0,0)` answers true there) is Chat, restoring
wheel scrolling over the chat's tab strip. An open loot/skills popup,
drawn on top, still wins. Pinned by `test_static_ui_scroll`
(`ChatPanelIsATargetAnywhereInsideIt`, the overlap point, plus
`SlashScrollWithNoTargetSaysSoRatherThanSilentlyDoingNothing` for the
zero-area case).

## Not verified / left open

- The card's premise "the hotbar is in the gate frames -- the gate goldens
  WILL change" does not hold, and the gate was RE-RUN at this tip to prove
  it (not only argued from the source): `scripts/ci/visual_gate.py` over
  `tools/hwcaps/gles2_zone_benchmark`, whose 104-file source list
  (CMakeLists.txt:2300-2477) contains no static-UI TU (`ui/`, `UIRenderer`,
  `TextBatch`, `renderHotbar` -- the only tools that reference those are
  `ui_panel_capture.cpp` and `ui_inventory_capture.cpp`, neither in the
  benchmark's source list), and its frame loop (gles2_zone_benchmark.cpp:659-1039)
  draws zone regions, doors and entities, never `renderStaticUI()`. The
  committed golden `tests/goldens/qeynos2_f0.png` is visually pure 3D
  geometry (no HUD). With the benchmark rebuilt from this tip
  (`build/bin/gles2_zone_benchmark`, all 102 TUs re-linked against the
  changed `ui_layout.h`/`static_ui_input.cpp`), `visual_gate.py --zones
  qeynos2` returned `VISUAL GATE: PASS` (2026-10-14 16:31Z) — all three
  frames pixel-identical to the committed goldens, zero differing pixels.
  The gate stays GREEN, so there is no proposed-goldens set to attach and
  no re-bless for Will. The hotbar's rendered pixels are covered by the
  ctest suite instead (`test_static_ui_frame_cost`, `test_ui_panel_z_order`,
  `test_ninth_spell_gem`).
- Spell slots cannot show real art today: the spell-icon texture the XML's
  SpellGem holder refers to is not in the shipped UI atlas (the gem bar
  draws the same text placeholder). Text is the only truthful thing a spell
  slot can draw; a spell-icon source is a FOLLOW-UP, not this card.

## CLAUDE.md EDITS

`include/client/graphics/ui/CLAUDE.md:49` -- replace the hotbar row:
`| Hotbar | Stock Titanium HotButtonWnd (one visible window, paged 2x5 slots) | `HotbarPanelState` | `renderHotbar()` |`

`include/client/bridge/CLAUDE.md:46` -- `## Intent Types (49)` -> `## Intent Types (50)` (`HotbarPageChangedIntent`, B1025-H2, is the 50th variant; DocCounts measures 50).
