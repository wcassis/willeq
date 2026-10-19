# B999 (#319): the pet panel no longer covers the group panel's member rows

Issue #319, found by B992 (#312, MR !104) in the `hud_all` capture
(`tools/hwcaps/ui_panel_capture.cpp`'s `hud_all` scene, group + pet both
open): with both panels open at 800x600 a group member row sat under the pet
panel -- its HP bar hidden and its name text dimmed under the pet panel
(B992's z-order work made the dimming correct, which is how the overlap
became visible).

## Ruling applied

`docs/decisions.md` 2026-10-15 (B1075): the oracle governs a window's
CONTENT, the fable layout table governs its PLACEMENT. This unit does NOT
re-measure official window positions; it fixes the placement in
`include/client/graphics/ui/ui_layout.h` (the UnderPlayerColumn block) and
pins it. 2026-09-07: positions are static by design, so a layout-table fix
is the whole fix. No draw-order change, no per-window alpha change (the
card's explicit exclusions; B1002's alpha is separate and untouched).

## The measured defect

The group frame the layout table gave the group was

    gpH = 5 * (BAR_HEIGHT + MARGIN) + MARGIN = 94px

but `renderGroupPanel()` (src/client/graphics/ui/static_panels.cpp:800-837)
paints a `BAR_HEIGHT + MARGIN` title band above the first member row and the
same `BAR_HEIGHT + MARGIN` pitch per row -- 18 + 5*18 = 108px down to the
last name line, and the last row's HP bar (`BAR_HEIGHT - 2`) extends a
further 12px below it: 116px of content in a 94px frame. The frame's bottom
landed 22px inside the member rows, and the pet panel -- anchored `MARGIN`
below the frame's bottom -- painted over the overflow.

The card names "the third member row" (the B992-era reading of the capture).
The current render's row pitch puts the fully-covered row at row 5 (name +
HP bar) with the row-4 HP bar straddling the frame's old bottom -- same
defect class, same panel pair, same fix; which row number is the one the
capture shows is an artifact of how far the rows overflow. The numbers below
are the program's, not a re-reading of the capture.

### Rects at the parent (7648f151) -- from the program

`tests/test_ui_layout_rects`'s `PrintRectsAndResSupported` (left column;
full dump `red_dump.txt`, red run `red_run.txt`):

| window | 800x600 | 1024x768 | 1280x720 | 1920x1080 |
|---|---|---|---|---|
| player status | (4,4)-(164,70) | (4,4)-(164,70) | (4,4)-(164,70) | (4,4)-(164,70) |
| target | (4,74)-(164,108) | (4,74)-(164,108) | (4,74)-(164,108) | (4,74)-(164,108) |
| group | (4,112)-(164,206) | (4,112)-(164,206) | (4,112)-(164,206) | (4,112)-(164,206) |
| pet | (4,210)-(164,262) | (4,210)-(164,262) | (4,210)-(164,262) | (4,210)-(164,262) |
| buffs | (168,4)-(468,44) | (168,4)-(468,44) | (168,4)-(468,44) | (168,4)-(468,44) |
| chat | (4,436)-(304,596) | (4,604)-(304,764) | (4,556)-(304,716) | (4,916)-(304,1076) |
| hotbar | (308,486)-(756,570) | (308,654)-(756,738) | (416,606)-(864,690) | (736,966)-(1184,1050) |
| spell gems | (748,192)-(796,482) | (972,360)-(1020,650) | (1228,312)-(1276,602) | (1868,672)-(1916,962) |
| casting bar | (432,464)-(632,482) | (432,632)-(632,650) | (540,584)-(740,602) | (860,944)-(1060,962) |
| xp bar | (0,592)-(800,600) | (0,760)-(1024,768) | (0,712)-(1280,720) | (0,1072)-(1920,1080) |

Parent group content rows (the render's own pitch, group top = 112): row 1
name y=130 / HP y=144-156; row 2 name y=148 / HP y=162-174; row 3 name
y=166 / HP y=180-192; row 4 name y=184 / HP y=198-210; row 5 name
y=202-216 / HP y=216-228. Group frame bottom y=206; pet frame y=210-262.
The fifth row's name and HP bar are under the pet panel; the row-4 HP bar
straddles the frame's bottom.

## The fix (layout table only)

`include/client/graphics/ui/ui_layout.h`, UnderPlayerColumn block: the
group frame's height is now the content the render actually paints --
`BAR_HEIGHT + MARGIN` for the title band, then `BAR_HEIGHT + MARGIN` per
member row, with the last row's HP bar (`BAR_HEIGHT - 2`) ending on the
frame's bottom edge:

    gpH = (BAR_HEIGHT + MARGIN) * (1 + kGroupMaxMembers - 1)
          + BAR_HEIGHT + (BAR_HEIGHT - 2)                     // 116px

The pet panel's anchor is unchanged (`MARGIN` below the group frame's
bottom), so it moves down 22px with the frame. No row height changed (the
content is the oracle's), no width changed, no other anchor touched, no
draw-order change, no per-window alpha change.

### Rects at the head -- from the program

`PrintRectsAndResSupported` (full dump `green_dump.txt`, green run
`green_run.txt`):

| window | 800x600 | 1024x768 | 1280x720 | 1920x1080 |
|---|---|---|---|---|
| group | (4,112)-(164,228) | (4,112)-(164,228) | (4,112)-(164,228) | (4,112)-(164,228) |
| pet | (4,232)-(164,284) | (4,232)-(164,284) | (4,232)-(164,284) | (4,232)-(164,284) |

Every other window is byte-identical to the parent's table above (the group
frame's X, Y, width and every other anchor are untouched; the pet panel is
the only window that moved, and it moved because its anchor is the group
frame's bottom). At every supported resolution the last row's painted HP
bar (y=216-228) ends exactly on the frame's bottom edge (y=228), and the
pet frame starts 4px below (y=232) -- pinned by
`TheGroupRowsFitTheFrameAndClearThePet`.

The left column still clears everything below it: chat top is y=436
(800x600) / y=604 (1024x768), 150px+ under the pet frame's bottom (284).
No third panel needed to be moved -- 800x600 holds both panels with the
fix, so the card's fallback question ("if 800x600 cannot hold both without
moving a third one, which and why") answers: none.

## Red / green runs (real, logged)

`tests/test_ui_layout_rects` (new TU, registered in
`tests/cmake/ui_rendering.cmake`):

- RED at the parent (run 1, `red_run.txt`): 4 of 5 tests pass;
  `UiLayoutRectsB999.TheGroupRowsFitTheFrameAndClearThePet` fails at all
  four resolutions -- at 800x600 "row 5 (4,202)-(164,216) runs past the
  group frame (4,112)-(164,206)" and "group row 5 (4,202)-(164,216) is
  under the pet panel (4,210)-(164,262)", the same at 1024x768, 1280x720
  and 1920x1080.
- GREEN at the head (run 2, `green_run.txt`): all 5 tests pass, including
  `TheGroupRowsFitTheFrameAndClearThePet` and
  `EveryRequiredCoOpenSetIsCleanAtEveryResolution`.

The test pins: the card's minimum pair list (player, target, group, pet,
buff, hotbar, chat, spell gems) at every supported resolution (no
intersection, all on screen); the table's own required co-open sets; group
x pet named as a required set in the table (it already is -- the table's
"HUD" set -- so the test asserts the table keeps it); and the drawn group
rows fitting the frame and clearing the pet.

## Evidence stills (step 4)

`hud_all` captured through the B992 recipe (`tools/hwcaps/ui_panel_capture`,
headless x86 GLES2 through rbuild on the ThinkPad -- a tool capture, never
a hand screenshot): `hud_all_800x600.png` and `hud_all_1024x768.png` in
this directory, group joined (3 members: Aldric 100%, Brynn 71%, Cadoc 34%)
+ pet (Gynok, 88%) both open. In both, the group frame spans y=112-228 with
all three member rows fully inside it (the third member row's HP bar ends
at y=180, 48px above the frame's bottom), and the pet frame starts at y=232
-- a 4px gap, no group row under the pet panel. The stills' rects match the
green table above.

NOTE: the two PNGs are gitignored (the repo's `*.png` rule) and are NOT
committed -- they live on this worktree's disk at the two paths above,
re-runnable with:

    cd <worktree> && (build out of rbuild, then on the ThinkPad)
    build/bin/ui_panel_capture /tmp/b999 --eqe1-path /mnt/projects/EverQuest_ETC1 \
        --size 800x600 --panel hud_all
    build/bin/ui_panel_capture /tmp/b999 --eqe1-path /mnt/projects/EverQuest_ETC1 \
        --size 1024x768 --panel hud_all

The committed evidence for the stills is the green rect dump
(`green_dump.txt`) plus the two logged runs.

## Goldens

`tests/goldens/` untouched (empty grep: the MR's changed paths are
`include/client/graphics/ui/ui_layout.h`, `tests/test_ui_layout_rects.cpp`,
`tests/cmake/ui_rendering.cmake`, `refactor/B999/*`). The visual gate's zone
frames are 800x600 captures of zone geometry; the gate's benchmark runs
carry no open group/pet window state (both default to their persisted UI
state, and the benchmark character has no group or pet), so the pet frame's
22px move does not touch a golden. `frame_render_order_g05.txt` is text
(draw-call order), and `renderPetPanel`'s call site is untouched.
