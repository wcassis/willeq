# T351 — The inventory popup frame is hardcoded 400×350 at every resolution (B004)

Class A, unconfirmed since the sweep; measure first. Slot2. No eqemu.

## Measure

`ui_layout.h` `centerPopup` fixes `popW/popH` at 400/350. The static panels
(`static_panels.cpp` — inventory popup: general slots, equipment
paperdoll, bags) lay their slots out from their own constants. At each
supported resolution (`-r 800 600`, `1024 768`, `1280 1024`, and the
orangepi `800 600` constrained preset — read `application_config.cpp` for
the list), compute with the existing pixel harness (`tests/test_ui_atlas_paperdoll.cpp`
/ `test_inventory_toggle.cpp` show how a popup is rendered and hit-tested
headless) the bounding box of every drawn inventory slot and the popup
frame rect; report per resolution which slots fall outside the frame and
whether `StaticUIInput`'s hit-test reaches them (the archive says general
and equipment slots were drawn outside and unclickable). Paste the table.
If NO slot falls outside at any resolution, B004 closes as refuted with
the table — do not touch code.

## Fix (only if the table shows a defect)

`centerPopup` derives the frame from the popup's content extent
(`static_panels` already knows its slot rects — expose the content size
or compute it in the layout) so the frame encloses every slot at every
resolution; clamp to the screen. Match the official client's proportions
where a screenshot exists in `review/` (T57/T60 captured the paperdoll —
check) rather than inventing a size. Test: the per-resolution table as a
test — every slot rect inside the frame rect, every slot hit-testable.
Pre-fix failure pasted. Goldens: the UI atlas pixel goldens may change
if the frame moves — `git status --porcelain tests/goldens` must be
empty; if the fix must move pixels the gate compares, STOP and report
which goldens (re-blessing is Will's call, never yours).

Territory: `include/client/graphics/ui/ui_layout.h`, `src/client/graphics/ui/static_panels.cpp`
(content extent only), the new test, `refactor/BOARD.md`. Graphics files
change: visual gate + ARM NOT skippable.

STOP: a golden would have to change; the fix needs the popup redesigned
rather than resized.

## Review

**Verdict: B004 is REFUTED. No code changed.** The archive's finding was
real, but it describes the defect **T57 fixed** (2026-08-11), not the tip.
`centerPopup`'s size really is a hardcoded 400x350 at every resolution —
that part of B004 is accurate — but it is not a defect, because the
inventory popup's slot geometry is relative to the popup's own origin and
width, so containment cannot vary with resolution. Both the renderer
(`renderInventoryPopup()` in `src/client/graphics/ui/static_panels.cpp`)
and the hit-test (`StaticUIInput::processInput()` in
`src/client/graphics/ui/static_ui_input.cpp`) read the single shared
source `InventoryLayout::slotRect()` /
`InventoryLayout::paperdollRect()` in
`include/client/graphics/ui/inventory_layout.h`, and neither file contains
slot-position arithmetic of its own. The hit-test is gated on the click
landing inside `centerPopup`, which is exactly why containment and
reachability are the same question — and why the measurement checks both.

### The measurement

`tests/test_static_ui_panels.cpp`, new test
`InventoryPopupFrameT351.EverySlotIsInsideTheFrameAndHitTestableAtEveryResolution`.
For each resolution it builds a fresh `UILayout`, prints the frame rect
and, for all 30 global slot indices (0-21 equipment by `EquipmentSlot`
id, 22-29 general) plus the T60 paperdoll rect: the rect, whether it lies
wholly inside `centerPopup`, and whether the CENTRE of that rect
hit-tests back through `StaticUIInput::processInput()` to that same slot
index (`consumed && hoveredSlot == i`). It also asserts the frame itself
lies wholly on screen — a frame that encloses its slots perfectly but
hangs off the display would still be unreachable.

Resolutions: the three the contract names, plus the default/orangepi
display (800x600 is both) and two more for breadth. `--constrained
orangepi` is a RENDERING preset (texture budget), not a resolution — see
`src/client/application_config.cpp:228`; the display size comes only from
`-r/--resolution` or the JSON `renderer.width/height`, defaulting to
800x600 (`include/client/application.h:54`). So the orangepi preset's row
IS the 800x600 row.

### The table

Every cell reads `yes`. Full output of the measurement:

```
=== 640x480  (below the documented minimum, checked for breadth) ===
frame centerPopup = {120,65,520,415}  400x350  onScreen=yes
slot | rect                      | inside | hit
   0 | { 389, 343, 421, 375}       |  yes   | yes
   1 | { 128,  85, 160, 117}       |  yes   | yes
   2 | { 128, 117, 160, 149}       |  yes   | yes
   3 | { 128, 149, 160, 181}       |  yes   | yes
   4 | { 128, 181, 160, 213}       |  yes   | yes
   5 | { 128, 213, 160, 245}       |  yes   | yes
   6 | { 128, 245, 160, 277}       |  yes   | yes
   7 | { 128, 277, 160, 309}       |  yes   | yes
   8 | { 128, 309, 160, 341}       |  yes   | yes
   9 | { 480,  85, 512, 117}       |  yes   | yes
  10 | { 480, 117, 512, 149}       |  yes   | yes
  11 | { 480, 149, 512, 181}       |  yes   | yes
  12 | { 480, 181, 512, 213}       |  yes   | yes
  13 | { 219, 343, 251, 375}       |  yes   | yes
  14 | { 253, 343, 285, 375}       |  yes   | yes
  15 | { 480, 213, 512, 245}       |  yes   | yes
  16 | { 480, 245, 512, 277}       |  yes   | yes
  17 | { 480, 277, 512, 309}       |  yes   | yes
  18 | { 480, 309, 512, 341}       |  yes   | yes
  19 | { 287, 343, 319, 375}       |  yes   | yes
  20 | { 321, 343, 353, 375}       |  yes   | yes
  21 | { 355, 343, 387, 375}       |  yes   | yes
  22 | { 185, 381, 217, 413}       |  yes   | yes
  23 | { 219, 381, 251, 413}       |  yes   | yes
  24 | { 253, 381, 285, 413}       |  yes   | yes
  25 | { 287, 381, 319, 413}       |  yes   | yes
  26 | { 321, 381, 353, 413}       |  yes   | yes
  27 | { 355, 381, 387, 413}       |  yes   | yes
  28 | { 389, 381, 421, 413}       |  yes   | yes
  29 | { 423, 381, 455, 413}       |  yes   | yes
doll | { 168,  85, 472, 341}       |  yes   | n/a

=== 800x600  (default (-r 800 600), also the orangepi preset's display) ===
frame centerPopup = {200,125,600,475}  400x350  onScreen=yes
slot | rect                      | inside | hit
   0 | { 469, 403, 501, 435}       |  yes   | yes
   1 | { 208, 145, 240, 177}       |  yes   | yes
   2 | { 208, 177, 240, 209}       |  yes   | yes
   3 | { 208, 209, 240, 241}       |  yes   | yes
   4 | { 208, 241, 240, 273}       |  yes   | yes
   5 | { 208, 273, 240, 305}       |  yes   | yes
   6 | { 208, 305, 240, 337}       |  yes   | yes
   7 | { 208, 337, 240, 369}       |  yes   | yes
   8 | { 208, 369, 240, 401}       |  yes   | yes
   9 | { 560, 145, 592, 177}       |  yes   | yes
  10 | { 560, 177, 592, 209}       |  yes   | yes
  11 | { 560, 209, 592, 241}       |  yes   | yes
  12 | { 560, 241, 592, 273}       |  yes   | yes
  13 | { 299, 403, 331, 435}       |  yes   | yes
  14 | { 333, 403, 365, 435}       |  yes   | yes
  15 | { 560, 273, 592, 305}       |  yes   | yes
  16 | { 560, 305, 592, 337}       |  yes   | yes
  17 | { 560, 337, 592, 369}       |  yes   | yes
  18 | { 560, 369, 592, 401}       |  yes   | yes
  19 | { 367, 403, 399, 435}       |  yes   | yes
  20 | { 401, 403, 433, 435}       |  yes   | yes
  21 | { 435, 403, 467, 435}       |  yes   | yes
  22 | { 265, 441, 297, 473}       |  yes   | yes
  23 | { 299, 441, 331, 473}       |  yes   | yes
  24 | { 333, 441, 365, 473}       |  yes   | yes
  25 | { 367, 441, 399, 473}       |  yes   | yes
  26 | { 401, 441, 433, 473}       |  yes   | yes
  27 | { 435, 441, 467, 473}       |  yes   | yes
  28 | { 469, 441, 501, 473}       |  yes   | yes
  29 | { 503, 441, 535, 473}       |  yes   | yes
doll | { 248, 145, 552, 401}       |  yes   | n/a

=== 1024x768 ===
frame centerPopup = {312,209,712,559}  400x350  onScreen=yes
slot | rect                      | inside | hit
   0 | { 581, 487, 613, 519}       |  yes   | yes
   1 | { 320, 229, 352, 261}       |  yes   | yes
   2 | { 320, 261, 352, 293}       |  yes   | yes
   3 | { 320, 293, 352, 325}       |  yes   | yes
   4 | { 320, 325, 352, 357}       |  yes   | yes
   5 | { 320, 357, 352, 389}       |  yes   | yes
   6 | { 320, 389, 352, 421}       |  yes   | yes
   7 | { 320, 421, 352, 453}       |  yes   | yes
   8 | { 320, 453, 352, 485}       |  yes   | yes
   9 | { 672, 229, 704, 261}       |  yes   | yes
  10 | { 672, 261, 704, 293}       |  yes   | yes
  11 | { 672, 293, 704, 325}       |  yes   | yes
  12 | { 672, 325, 704, 357}       |  yes   | yes
  13 | { 411, 487, 443, 519}       |  yes   | yes
  14 | { 445, 487, 477, 519}       |  yes   | yes
  15 | { 672, 357, 704, 389}       |  yes   | yes
  16 | { 672, 389, 704, 421}       |  yes   | yes
  17 | { 672, 421, 704, 453}       |  yes   | yes
  18 | { 672, 453, 704, 485}       |  yes   | yes
  19 | { 479, 487, 511, 519}       |  yes   | yes
  20 | { 513, 487, 545, 519}       |  yes   | yes
  21 | { 547, 487, 579, 519}       |  yes   | yes
  22 | { 377, 525, 409, 557}       |  yes   | yes
  23 | { 411, 525, 443, 557}       |  yes   | yes
  24 | { 445, 525, 477, 557}       |  yes   | yes
  25 | { 479, 525, 511, 557}       |  yes   | yes
  26 | { 513, 525, 545, 557}       |  yes   | yes
  27 | { 547, 525, 579, 557}       |  yes   | yes
  28 | { 581, 525, 613, 557}       |  yes   | yes
  29 | { 615, 525, 647, 557}       |  yes   | yes
doll | { 360, 229, 664, 485}       |  yes   | n/a

=== 1280x1024 ===
frame centerPopup = {440,337,840,687}  400x350  onScreen=yes
slot | rect                      | inside | hit
   0 | { 709, 615, 741, 647}       |  yes   | yes
   1 | { 448, 357, 480, 389}       |  yes   | yes
   2 | { 448, 389, 480, 421}       |  yes   | yes
   3 | { 448, 421, 480, 453}       |  yes   | yes
   4 | { 448, 453, 480, 485}       |  yes   | yes
   5 | { 448, 485, 480, 517}       |  yes   | yes
   6 | { 448, 517, 480, 549}       |  yes   | yes
   7 | { 448, 549, 480, 581}       |  yes   | yes
   8 | { 448, 581, 480, 613}       |  yes   | yes
   9 | { 800, 357, 832, 389}       |  yes   | yes
  10 | { 800, 389, 832, 421}       |  yes   | yes
  11 | { 800, 421, 832, 453}       |  yes   | yes
  12 | { 800, 453, 832, 485}       |  yes   | yes
  13 | { 539, 615, 571, 647}       |  yes   | yes
  14 | { 573, 615, 605, 647}       |  yes   | yes
  15 | { 800, 485, 832, 517}       |  yes   | yes
  16 | { 800, 517, 832, 549}       |  yes   | yes
  17 | { 800, 549, 832, 581}       |  yes   | yes
  18 | { 800, 581, 832, 613}       |  yes   | yes
  19 | { 607, 615, 639, 647}       |  yes   | yes
  20 | { 641, 615, 673, 647}       |  yes   | yes
  21 | { 675, 615, 707, 647}       |  yes   | yes
  22 | { 505, 653, 537, 685}       |  yes   | yes
  23 | { 539, 653, 571, 685}       |  yes   | yes
  24 | { 573, 653, 605, 685}       |  yes   | yes
  25 | { 607, 653, 639, 685}       |  yes   | yes
  26 | { 641, 653, 673, 685}       |  yes   | yes
  27 | { 675, 653, 707, 685}       |  yes   | yes
  28 | { 709, 653, 741, 685}       |  yes   | yes
  29 | { 743, 653, 775, 685}       |  yes   | yes
doll | { 488, 357, 792, 613}       |  yes   | n/a

=== 1920x1080 ===
frame centerPopup = {760,365,1160,715}  400x350  onScreen=yes
slot | rect                      | inside | hit
   0 | {1029, 643,1061, 675}       |  yes   | yes
   1 | { 768, 385, 800, 417}       |  yes   | yes
   2 | { 768, 417, 800, 449}       |  yes   | yes
   3 | { 768, 449, 800, 481}       |  yes   | yes
   4 | { 768, 481, 800, 513}       |  yes   | yes
   5 | { 768, 513, 800, 545}       |  yes   | yes
   6 | { 768, 545, 800, 577}       |  yes   | yes
   7 | { 768, 577, 800, 609}       |  yes   | yes
   8 | { 768, 609, 800, 641}       |  yes   | yes
   9 | {1120, 385,1152, 417}       |  yes   | yes
  10 | {1120, 417,1152, 449}       |  yes   | yes
  11 | {1120, 449,1152, 481}       |  yes   | yes
  12 | {1120, 481,1152, 513}       |  yes   | yes
  13 | { 859, 643, 891, 675}       |  yes   | yes
  14 | { 893, 643, 925, 675}       |  yes   | yes
  15 | {1120, 513,1152, 545}       |  yes   | yes
  16 | {1120, 545,1152, 577}       |  yes   | yes
  17 | {1120, 577,1152, 609}       |  yes   | yes
  18 | {1120, 609,1152, 641}       |  yes   | yes
  19 | { 927, 643, 959, 675}       |  yes   | yes
  20 | { 961, 643, 993, 675}       |  yes   | yes
  21 | { 995, 643,1027, 675}       |  yes   | yes
  22 | { 825, 681, 857, 713}       |  yes   | yes
  23 | { 859, 681, 891, 713}       |  yes   | yes
  24 | { 893, 681, 925, 713}       |  yes   | yes
  25 | { 927, 681, 959, 713}       |  yes   | yes
  26 | { 961, 681, 993, 713}       |  yes   | yes
  27 | { 995, 681,1027, 713}       |  yes   | yes
  28 | {1029, 681,1061, 713}       |  yes   | yes
  29 | {1063, 681,1095, 713}       |  yes   | yes
doll | { 808, 385,1112, 641}       |  yes   | n/a
```

Summary: 5 resolutions x (30 slots + paperdoll) = 155 rects. Inside the
frame: 155/155. Hit-testable (30 slots x 5): 150/150. Frame on screen:
5/5. **No slot falls outside at any resolution**, so per the contract
B004 closes as refuted and no code is touched.

### Why this needed measuring at all

`tests/test_static_ui_panels.cpp` already had
`InventoryAllThirtySlotsAreWhollyInsideCenterPopup`,
`InventoryAllThirtySlotsRoundTripThroughHitTest` and
`PaperdollRectIsWhollyInsideCenterPopup` from T57/T60 — but all three run
on the fixture's single `layout.computeLayout(800, 600)`. They could not
have distinguished "inside at 800x600" from "inside at every resolution",
which is precisely what B004 asserted was false. The new test is the
resolution sweep those tests lack; it is the only change in this unit.

### Change

No production change. One test added (+107 lines,
`tests/test_static_ui_panels.cpp` 505 -> 612 lines) and two includes
(`<cstdio>`, `<iostream>`) for the table output. There is no pre-fix
failure to paste because there is no fix: the test passes at
`5601a3ed` on its first run, which IS the refutation.

### Steps

- `cmake --build .../build -j24` — **exit 0**.
- `ctest --test-dir .../build -j6 --output-on-failure` — **3847/3848
  passed**, the single failure being the sanctioned
  `AudioManagerTest.PreloadCommonSounds` (ALSA).
- `scripts/refactor/check_file_sizes.sh --all` — **exit 0**, 880 files
  checked, 880 ok, 0 warn, **0 FAIL**.
- `git status --porcelain tests/goldens` — **empty**. No golden moves;
  no pixel the visual gate compares changes, because no rendering code
  changed.
- GLES2 / visual gate / ARM: not run. The contract makes them
  non-skippable "if a graphics file changes" and explicitly states that a
  refutation with no code change needs "only the measurement test ... the
  build + ctest". Nothing under `src/client/graphics/` or
  `include/client/graphics/` is modified — `git status --porcelain`
  shows exactly `tests/test_static_ui_panels.cpp` and
  `refactor/BOARD.md`.

### STOPs

None. No golden would have to change, and the popup needs neither a
resize nor a redesign.

### Board

B004 struck on `refactor/BOARD.md` as REFUTED by T351, with the table's
verdict and the pointer to the new test. `refactor/K00_truth_reset.md`
lines 231 and 452 still carry B004 as "unconfirmed"; that file is the
K00 truth-reset SNAPSHOT of its own moment, so it is left as written —
`BOARD.md` is the live board.
