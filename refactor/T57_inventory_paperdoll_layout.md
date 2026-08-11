# T57 — The inventory popup's slots fall outside the popup: EQ-authentic body layout, with one shared geometry source

**Slot: t1. x86 only. No Pi. NO EQEMU** — the popup renders from a
populated `InventoryPanelState` with no server and no session.

**Territory: `src/client/graphics/ui/static_panels.cpp`
(`renderInventoryPopup`), `src/client/graphics/ui/static_ui_input.cpp`
(the inventory block of `processInput`, `:64-86`),
`include/client/graphics/ui/` (for the new shared geometry declaration),
`tests/test_static_ui_panels.cpp`, and a capture harness under `tests/`
or `scripts/`.**

**Do NOT change `UILayout::centerPopup`'s size** (`ui_layout.h:131-132`).
It is shared with the spellbook and skills popups, which are
bottom-anchored (`static_ui_input.cpp:117`) and currently fit correctly —
growing the box to solve an inventory-only problem would reflow two
panels that have nothing wrong with them. The 400x350 box is the
constraint; the layout adapts to it. Do not touch the spellbook, skills,
hotbar, or spell-gem code paths.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/T54_inventory_view_test_reckoning.md`
**including its Review** — T54 wrote the tests that found this and its
`StaticUIInputTest` fixture is where your new tests belong.

No sibling units this round. `fable` tip at dispatch: `cbeeee4d`.

## The defect

`UILayout::centerPopup` is a fixed **400x350** box; only its origin is
centered, so this is **not** resolution-dependent. The inventory popup
lays out downward from the popup's top-left and runs past the bottom:

- Equipment, 2 columns x 11 rows, 32px slots on a 34px pitch from `+22`:
  the last row starts at **+362**, past the box's own **350**.
- General row: `genY = 22 + (11 * 34) + 8` = **+404**, +32 tall, so the
  content needs **436px of a 350px box**.

Both the renderer (`static_panels.cpp:279-280, 304`) and the hit test
(`static_ui_input.cpp:72-73, 80`) compute this with **identical
arithmetic**, so they agree with each other and both disagree with the
panel. Consequences, in every session at every resolution:

- Those slots are **drawn outside the popup's background panel**, over
  whatever is behind it.
- They are **unclickable** — `processInput` rejects any point outside
  `centerPopup` (`:66-69`) before per-slot hit-testing runs.

All 8 general slots plus equipment slots 20-21 (`SLOT_WAIST`,
`SLOT_AMMO`) are affected. It has been live since the static-panel UI
replaced the deleted window tree, unnoticed because nothing tested this
surface until T54.

Note the shape of it: the popup is **400px wide** and the equipment grid
uses **76px**. ~320px of width sits unused while the content overflows
vertically by ~94px. The layout was never adapted to its own container.

## The root cause is the duplication, and that is the primary fix

Two files independently compute the same slot rectangles from the same
magic numbers. That is why the render and the hit test could both be
wrong in exactly the same way, and it is why a naive fix would leave
them free to drift apart again later.

**Requirement: introduce ONE shared source of slot geometry, and have
both the renderer and the hit test consume it.** Shape it as a function
that, given a `UILayout` (or the `centerPopup` rect), returns the
rectangle for a given slot index — plus whatever the general row needs.
Put the declaration in `include/client/graphics/ui/` next to
`static_panels.h`. Neither `renderInventoryPopup` nor `processInput` may
contain slot-position arithmetic afterwards; both call the shared helper.

**This is the part that must not be compromised.** If the body layout
turns out harder than expected, a correct layout with duplicated
geometry is a FAILED unit; a plain layout with single-source geometry is
a successful one.

## The layout — EQ-authentic body arrangement (Will's choice, 2026-08-11)

Position equipment slots around a body outline the way the original EQ
client does, rather than as an abstract grid. Will picked this over a
4x6 grid reflow and over growing the popup.

**Use the canonical slot identities that already exist — do not invent
your own.** `include/client/inventory/inventory_constants.h:9-32` defines
`enum EquipmentSlot` (`SLOT_CHARM = 0` .. `SLOT_AMMO = 21`), and
`getSlotName()` at `:739-765` gives display names. `InventoryPanelState::equipSlots[22]`
(`static_panels.h:89`) is indexed by exactly those values, so index ==
slot id; label the slots from `getSlotName()` rather than hardcoding
strings in the UI.

Suggested arrangement, adapt as the geometry demands — the constraint is
that it be anatomically recognizable to someone who has played EQ:

- **Left column**: `SLOT_EAR1`, `SLOT_HEAD`, `SLOT_FACE`, `SLOT_EAR2`,
  `SLOT_NECK`, `SLOT_SHOULDERS`, `SLOT_ARMS`, `SLOT_BACK`
- **Right column**: `SLOT_WRIST1`, `SLOT_WRIST2`, `SLOT_RANGE`,
  `SLOT_HANDS`, `SLOT_RING1`, `SLOT_RING2`, `SLOT_CHEST`, `SLOT_LEGS`
- **Bottom weapon row**: `SLOT_PRIMARY`, `SLOT_SECONDARY`, `SLOT_FEET`,
  `SLOT_WAIST`, `SLOT_AMMO`, `SLOT_CHARM`
- **General row** (8 slots) beneath, as today.

**Every rectangle must lie wholly inside `centerPopup`** — that is the
whole point of the unit, and Step 3 asserts it exhaustively. Do the
arithmetic before you write the renderer: two 34px columns plus a centre
gap plus an 8-wide general row (272px) all fit comfortably in 400x350,
but only if you budget the title, the "General" label, and a bottom
margin first.

There is **no paperdoll model** to draw between the columns —
`CharacterModelView` was deleted in `0f25cb57` and T54 recorded its
absence as a deliberate gap. The centre is empty space, or a simple
placeholder outline if that reads better. **Do not implement a 3D
character preview**; that is a separate feature and an instant STOP.

## Step 3 — tests that make this class of bug impossible to reintroduce

In `tests/test_static_ui_panels.cpp`, extending T54's fixture:

- **Containment, exhaustive**: for all 22 equipment slots and all 8
  general slots, assert the slot rect is fully inside `centerPopup` —
  all four edges. This is the regression that would have caught the
  original bug on day one.
- **Round-trip**: for every slot, take the centre of its rect from the
  shared helper, feed it to `processInput`, and assert `hoveredSlot`
  comes back as that slot's index. This pins render and hit test to the
  same geometry — the duplication defect becomes untestable-to-reintroduce
  rather than merely fixed.
- **No overlap**: no two slot rects intersect.
- **Boundary**: the padding gaps between slots still return `-1`, and
  points outside the popup still return `-1` and do not consume input.
- Keep T54's existing cases green. Its two tests naming the OLD broken
  behaviour (`InventoryGeneralSlotsAreWhollyUnreachable`,
  `InventoryEquipGridRow10IsUnreachable`) now assert the bug — **invert
  them into the fixed expectation and rename them**; do not delete them
  silently, and say in the Review that you did it.

## Step 4 — visual acceptance (Will judges, you do not)

Will chose screenshot review at merge. **There is no existing path to
capture the UI**: `--dump-png-prefix` belongs to `gles2_zone_benchmark`,
which renders zones and never draws a popup. Build the smallest thing
that works — most likely a test or small tool that brings up the
surfaceless GLES2 device the gate already uses, renders the popup from a
populated `InventoryPanelState`, and writes a PNG via Irrlicht's
`writeImageToFile`. Do not add a screenshot flag to the shipping client
for this.

Produce PNGs at **800x600** (the shipped resolution) and one larger size,
each with a **fully populated** state — every equipment slot and every
general slot holding an item, so nothing is invisible by being empty —
plus one with a few slots filled to show the empty-slot rendering. Also
capture one with a slot hovered.

Put them somewhere self-contained and say exactly where in your Review.
**Do not self-assess whether it looks right** — per protocol, state what
you captured and let Will judge. If the capture harness cannot be made to
work in reasonable time, STOP and report with the tests passing rather
than shipping a layout nobody has seen.

## STOP conditions

- Any change to `centerPopup`'s size, or to the spellbook/skills/hotbar
  paths.
- Any move toward implementing a 3D character preview.
- Geometry arithmetic remaining in both files at the end.
- Goldens moving. The visual gate renders zones, not UI; if it moves,
  you have changed something you should not have.

## Verification

Full protocol bar. Additionally: state the containment test results
explicitly (all 30 rects inside the panel), and confirm the visual gate
is untouched.

## Review

(executor fills: the shared geometry API and both call sites; the final
slot-to-position mapping as a table; the inverted T54 tests; where the
PNGs are; per-step verification; deviations)
