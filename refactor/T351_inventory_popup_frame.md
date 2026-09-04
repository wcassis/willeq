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
