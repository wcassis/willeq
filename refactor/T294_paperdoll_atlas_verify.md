# T294 — does the paperdoll survive the UI atlas in the REAL client? (+ the dead `I` keybind)

Slot: slot1. x86. OWNS eqemu (noclobber) — this needs a live client with a
built player entity. Class A/B. Territory: `src/client/graphics/ui/ui_renderer.*`
(draw ordering only), `renderer_entities.cpp` `toggleInventory()`,
`static_panels.cpp` if the fix lands there, `src/client/graphics/CLAUDE.md`
(the stale paragraph), tests. Do NOT touch character_model_view internals or
the atlas builder.

## The finding (audit, 2026-08-27) — verify it before fixing it
The paperdoll works: `CharacterModelView` renders the player's live
`EntityAppearance` into a 304x256 RTT, blitted by `UIRenderer::drawTexture()`.
**But every paperdoll capture in the tree was taken through the ATLAS-LESS
fallback** (`ui.init(driver, nullptr, ...)` in `ui_inventory_capture.cpp:315`
and `test_static_ui_panels.cpp:468,486`), where `drawSprite` also draws
immediately and the ordering is accidentally correct.

In the real client the atlas IS loaded (`irrlicht_renderer.cpp:655-672` when
EQE1 is configured), and then:
- `drawTexture()` draws IMMEDIATELY (`ui_renderer.cpp:103-119`, and the header
  says so at `ui_renderer.h:66-72`);
- `drawPanel()`/`drawSprite()` ACCUMULATE into `atlasQuads_` and flush in
  `endFrame()` (`ui_renderer.cpp:28-31, 120-147`).
So the popup background is painted AFTER the paperdoll, over the same rect —
and the atlas quad is opaque in practice, because `panel_background` is
authored at alpha 180 but the bound texture is the RGB ETC1 entry and its
`ui_atlas_alpha` companion is never bound (T279). Live captures show panels as
solid slabs (`review/T276/t276_2_spellbook_scribed_1024x768.png`).
**Predicted symptom: with the atlas loaded, the paperdoll is invisible —
covered by its own panel background.**

## Scope
1. **Observe first.** Live client, `--eqe1-path` (atlas loaded), zone in, wait
   for the player entity to build, `/newinv`, screenshot. Is the paperdoll
   visible? Record the answer either way — if it IS visible, the inference was
   wrong and this unit closes as not-a-defect with the evidence.
2. If it is covered: fix the ordering. The minimal shape is to make the
   paperdoll blit participate in the same ordered flush as the atlas quads
   (or flush before it), NOT to special-case the paperdoll. State why the
   chosen shape cannot silently break other immediate-mode draws.
3. **Pin it**: extend the atlas-backed capture path
   `runAtlasPanelCapture()` (`ui_inventory_capture.cpp:490-540`) — which
   currently never sets `inv.paperdollTexture` — so the atlas path is covered
   at all, then a pixel test in T275's style (`tests/test_ui_atlas_pixels.cpp`)
   asserting paperdoll pixels survive. Red/green measured.
4. **The dead keybind**: `HotkeyAction::ToggleInventory` reaches
   `IrrlichtRenderer::toggleInventory()`, an EMPTY STUB left by U07c2
   (`renderer_entities.cpp:351-353`), so `I` does nothing and the popup is
   reachable only via `/newinv`. Wire it to the same path `/newinv` uses (one
   body). Test it.
5. Fix the stale claim in `src/client/graphics/CLAUDE.md:203-206` that the
   paperdoll "was deleted in 0f25cb57 and never rebuilt" — false since T60.

## Verification
Full ctest (`-j6`); visual gate; Review; evidence review/T294/ with the live
before/after screenshots. Commit: `T294: the paperdoll survives the UI atlas; I opens the inventory`.
