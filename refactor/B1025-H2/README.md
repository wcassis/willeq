# B1025-H2: hotbar layout (partial)

Source: stock Titanium `EQUI_HotButtonWnd.xml` at
`/mnt/projects/EverQuestP1999/uifiles/default/`.

## Acceptance at this checkpoint

| Criterion | Status | Evidence |
|---|---|---|
| Layout rects equal XML | Met | `HotbarLayout.StockTitaniumRects` compares the window, page controls, and every button/inventory/spell holder to the XML. |
| One window and pages render | Not met | `origin/fable` still has the unpaged `HotbarModel` and `HotbarPanelState`; H-1 MR !169 remains unmerged after its review found production-path failures. |
| Per-kind slot art | Not met | Rendering remains in `static_panels.cpp` pending the corrected H-1 state/event contract. |
| Old constants deleted | Not met | `UILayout` still owns the old horizontal geometry; replacing it now would break rendering before the paged model is available. |
| Proposed goldens attached | Not met | No render change or visual gate was run at this checkpoint. |
| Hygiene green | Met for this checkpoint | `ctest --test-dir build/hygiene -L hygiene --output-on-failure` passed; log: `/home/user/kb-evidence/kb-h2-hotbar-layout/hygiene-lasttest.log`. |
| No bless-only files touched | Met | Inspect this branch's diff against `origin/fable`. |

## Not verified / left open

- H-1 review at `c939c7f2306b8c02835d8a31a7ebe17820a21076` failed on
  cross-page cooldown leakage and invalid character-file overwrite. H-2 does
  not copy or silently depend on that unmerged revision. Resume the renderer,
  input, proposed frames and CI checks when the corrected H-1 contract lands.
- `include/client/graphics/ui/CLAUDE.md` is protected in this headless run.
  The handoff will provide its proposed replacement text.
