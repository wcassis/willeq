# T416 — the UI now draws its atlas with alpha: capture every panel and fix what is visibly wrong (B316)

Class B, slot3, no server. Read B316 (77% of `ui_atlas` texels are
non-opaque and 64% fully transparent; the UI drew them opaque until
T411; no gate golden photographs UI), T411's §Review, the headless UI
capture tool (`tools/hwcaps/ui_inventory_capture.cpp` — T393/T409 used
it) and the static-panel list (`static_panels.cpp`: inventory, spellbook,
skills, group, pet, buff bar, casting bar, chat, hotbar, target, …).

1. Extend (or sibling) the capture tool to render EVERY static panel and
   popup headlessly at 800x600 into `review/T416/after/<panel>.png`;
   build the same binary with the pre-T411 shaders? — no: instead render
   the same panels against the PRE-T411 pack (`review/T411/` holds it) via
   `--eqe1-path` for `review/T416/before/`. Contact sheet before/after per
   panel.
2. For each panel, compare against the oracle: the official window's
   look from its XML pieces (`EQUI_*.xml` + `window_pieces01.tga`
   decals) and any oracle screenshot under `review/T3xx/oracle/`
   (T377 has Options/Keys, T386 the inventory, T401 the group window).
   Table: panel → before/after → oracle → verdict (matches / visibly
   wrong / no oracle).
3. Fix the visibly-wrong ones that are mechanical (a sprite region whose
   alpha now punches a hole where the oracle draws a filled frame — pick
   the right atlas region / add a filled backing; a text over a now-
   transparent box that the oracle draws opaque). Each fix pinned by the
   static-panel seam (pre-fix failing) and re-captured. Anything that
   needs a design call → board (B350+) with the crop.
4. Add a `UiPanels` capture to the visual gate? NO — board it as a
   proposal (B350+): the gate is Will's to extend. Strike or annotate
   B316.

## Proof

Build (background + exit file), `ctest -j6`, pre-fix/post-fix, visual
gate standalone + ARM (no image golden should move — the gate opens no
UI), file sizes; the contact sheets in `review/T416/`.

STOP: none expected.
