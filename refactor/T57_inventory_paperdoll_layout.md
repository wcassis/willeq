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

### The shared geometry source and both call sites

New header `include/client/graphics/ui/inventory_layout.h` (148 lines, header-
only — a pure function of `UILayout`, no state, so no new TU/CMakeLists
registration needed for it): `EQT::Graphics::InventoryLayout::slotRect(const
UILayout& layout, int globalIndex)` returns the screen rect for a global
inventory slot index (0-21 = equipment slot id from
`eqt::inventory::EquipmentSlot`, 22-29 = `EQUIP_SLOTS + generalIndex`),
computed against `layout.centerPopup`. Three `constexpr int16_t[]` tables
(`kLeftColumn`, `kRightColumn`, `kWeaponRow`) hold the slot-id-per-position
assignment; the general row is a direct index offset. This is the only place
inventory-popup slot geometry is computed.

Both call sites now contain zero slot-position arithmetic:
- **Renderer** — `src/client/graphics/ui/static_panels.cpp`,
  `renderInventoryPopup()`: loops `i` 0..21 calling
  `InventoryLayout::slotRect(layout, i)` for equipment, and 0..7 calling
  `InventoryLayout::slotRect(layout, EQUIP_SLOTS + i)` for general slots.
- **Hit test** — `src/client/graphics/ui/static_ui_input.cpp`,
  `StaticUIInput::processInput()`'s inventory-popup block: once the point is
  confirmed inside `centerPopup`, loops all 30 global indices calling
  `InventoryLayout::slotRect()` and checking point-in-rect, breaking on the
  first hit. `hitTestSlotGrid()` (the old col-major-grid hit tester) had no
  remaining callers after this change and was deleted from
  `static_ui_input.{h,cpp}`; `hitTestSlotRow()` is retained (still used by
  the hotbar).

### Layout — EQ-authentic body arrangement

Two 8-slot columns flank an empty center (no paperdoll model, per T54's
recorded gap — center is genuinely empty space, no placeholder outline was
added since the plan made it optional and the arithmetic left no vertical
budget to spare for one), a 6-slot weapon row sits below both columns, and
the 8-slot general row sits below that. All geometry is relative to
`centerPopup`'s own top-left corner and width, so it works at any position
`computeLayout()` puts the popup (`centerPopup` itself is a fixed 400x350
box — only its origin varies with screen size, per the plan).

Vertical budget (all values relative to `centerPopup`'s top edge, `SLOT=32`):
`CONTENT_TOP=20` (title clearance) → 8 column rows at pitch 32 (no vertical
padding within a column: `256`) → `GAP_COL_WEAPON=2` → weapon row (`32`) →
`GAP_WEAPON_GENERAL=6` → general row (`32`). Total: `20+256+2+32+6+32=348`,
2px inside the 350px box. Horizontal: left column at `popup.X+8`, right
column at `popup.X + (400-8-32) = popup.X+360`; the weapon row (6 slots,
2px pad, 202px wide) and general row (8 slots, 2px pad, 270px wide) are each
horizontally centered in the popup.

**Full slot-to-position table** (values shown for the 800x600 default
layout, where `centerPopup = {200,125,600,475}`; the table's *shape* — which
section/row/column each slot lands in — is resolution-independent, only the
absolute pixel numbers shift with `centerPopup`'s origin):

| Global idx | Slot | Rect (x1,y1,x2,y2) |
|---|---|---|
| 0 | SLOT_CHARM | (469, 403, 501, 435) |
| 1 | SLOT_EAR1 | (208, 145, 240, 177) |
| 2 | SLOT_HEAD | (208, 177, 240, 209) |
| 3 | SLOT_FACE | (208, 209, 240, 241) |
| 4 | SLOT_EAR2 | (208, 241, 240, 273) |
| 5 | SLOT_NECK | (208, 273, 240, 305) |
| 6 | SLOT_SHOULDERS | (208, 305, 240, 337) |
| 7 | SLOT_ARMS | (208, 337, 240, 369) |
| 8 | SLOT_BACK | (208, 369, 240, 401) |
| 9 | SLOT_WRIST1 | (560, 145, 592, 177) |
| 10 | SLOT_WRIST2 | (560, 177, 592, 209) |
| 11 | SLOT_RANGE | (560, 209, 592, 241) |
| 12 | SLOT_HANDS | (560, 241, 592, 273) |
| 13 | SLOT_PRIMARY | (299, 403, 331, 435) |
| 14 | SLOT_SECONDARY | (333, 403, 365, 435) |
| 15 | SLOT_RING1 | (560, 273, 592, 305) |
| 16 | SLOT_RING2 | (560, 305, 592, 337) |
| 17 | SLOT_CHEST | (560, 337, 592, 369) |
| 18 | SLOT_LEGS | (560, 369, 592, 401) |
| 19 | SLOT_FEET | (367, 403, 399, 435) |
| 20 | SLOT_WAIST | (401, 403, 433, 435) |
| 21 | SLOT_AMMO | (435, 403, 467, 435) |
| 22 | General[0] | (265, 441, 297, 473) |
| 23 | General[1] | (299, 441, 331, 473) |
| 24 | General[2] | (333, 441, 365, 473) |
| 25 | General[3] | (367, 441, 399, 473) |
| 26 | General[4] | (401, 441, 433, 473) |
| 27 | General[5] | (435, 441, 467, 473) |
| 28 | General[6] | (469, 441, 501, 473) |
| 29 | General[7] | (503, 441, 535, 473) |

(`weaponY=403`, `generalY=441`, `generalBottom=473` — 2px inside
`centerPopup`'s bottom edge at 475.) Item labels use
`eqt::inventory::getSlotName()` for empty equipment slots (no hardcoded
strings in the UI layer, per the plan) — populated slots show the item
name instead.

### Containment — exhaustive, all 30 slots

Verified two ways:
1. **On paper before writing the renderer**, per the plan's instruction —
   the arithmetic above, cross-checked with a standalone Python script
   before any C++ was written (all 30 rects computed and checked
   `x1>=0, y1>=0, x2<=400, y2<=350` relative to the popup, plus an
   all-pairs overlap check — both clean).
2. **In the test suite**, exhaustively: `StaticUIInputTest.
   InventoryAllThirtySlotsAreWhollyInsideCenterPopup` iterates all 30
   global indices and asserts all four edges of each `slotRect()` lie
   inside `layout.centerPopup` at the real 800x600 layout. **Result: all 30
   slots PASS — every equipment and general slot lies wholly inside the
   fixed 400x350 `centerPopup`.**

### Step 3 — tests (`tests/test_static_ui_panels.cpp`)

Added 9 new tests to `StaticUIInputTest`, replacing the 4 tests that
exercised the old 2x11 col-major grid geometry (which no longer exists) and
inverting the 2 tests that pinned the original bug:

- `InventoryEquipLeftColumnFirstSlotHoversEar1`, `InventoryEquipRightColumnHoversCorrectSlotId` —
  replace `InventoryEquipGridFirstCellHover` /
  `InventoryEquipGridSecondColumnUsesColMajorIndex` (the old col-major
  `col*rows+row` formula is gone — slot identity now comes from the lookup
  tables in `inventory_layout.h`, so these pin specific named slots by id
  instead of by formula).
- `InventoryWeaponRowPaddingGapStillConsumesButNoHover`,
  `InventoryGeneralRowPaddingGapStillConsumesButNoHover` — replace
  `InventoryEquipGridPaddingGapStillConsumesButNoHover` (same padding-gap
  invariant — any point inside `centerPopup` still consumes even with no
  slot hit — re-anchored to the new geometry's actual gaps).
- `InventoryEquipGridLastPartiallyReachableRow` had no equivalent in the new
  layout — the old "partially reachable last row" was purely an artifact of
  content overflowing a too-short popup; there is no such row anymore
  (every row fits), so nothing was ported forward for it.
- **Inverted, per the plan's explicit instruction (renamed, not deleted
  silently):**
  - `InventoryEquipGridRow10IsUnreachable` → **`InventoryEquipSlotsWaistAndAmmoAreNowReachable`** —
    was: the defect's finding that equipment slots 20/21 were drawn/hit-test
    past `centerPopup`'s bottom edge. Now: asserts `SLOT_WAIST` and
    `SLOT_AMMO` both hover-hit correctly (they're two of the six weapon-row
    slots now).
  - `InventoryGeneralSlotsAreWhollyUnreachable` → **`InventoryGeneralSlotsAreNowReachable`** —
    was: the defect's finding that all 8 general slots were entirely below
    `centerPopup`. Now: asserts General[0] and General[7] both hover-hit
    correctly.
- **New exhaustive shared-geometry regression tests** (Step 3's explicit
  asks, run against `InventoryLayout::slotRect()` directly so they pin the
  *class* of bug, not just today's specific layout):
  - `InventoryAllThirtySlotsAreWhollyInsideCenterPopup` — containment, all 30.
  - `InventoryAllThirtySlotsRoundTripThroughHitTest` — for every slot, the
    center of its `slotRect()` fed through `processInput()` returns that
    same slot index. This is the test that makes render/hit-test drift
    untestable-to-reintroduce (both consume the same function; this test
    checks all 30 of its outputs against themselves through the input path).
  - `InventoryNoTwoSlotRectsOverlap` — all-pairs overlap check over the 30
    rects (strict-inequality AABB test — touching edges, e.g. the 8
    vertically-touching slots within each column, do not count as overlap).

All other T54 tests (hotbar, spell gems, `processKey`, the
`UIRendererGraphicsTest` rendering smoke tests) are untouched and still
green. Test count for this file: **23 (T54) → 26 (net +3)** — 5 old
col-major-grid tests removed (`InventoryEquipGridFirstCellHover`,
`InventoryEquipGridSecondColumnUsesColMajorIndex`,
`InventoryEquipGridPaddingGapStillConsumesButNoHover`,
`InventoryEquipGridLastPartiallyReachableRow` [no replacement — see
Deviations], plus the same padding-gap concept split into 2 new tests for
the new geometry's two padding-bearing rows); 2 tests renamed/inverted in
place (`InventoryEquipGridRow10IsUnreachable`,
`InventoryGeneralSlotsAreWhollyUnreachable`); 7 new tests added (4 replacing
the removed grid-geometry ones + 3 exhaustive shared-geometry tests:
containment, round-trip, no-overlap). Confirmed by running the binary
directly: `StaticUIInputTest` 21→24, `UIRendererGraphicsTest` unchanged at
2, total 23→26.

### Step 4 — visual acceptance capture

**No existing capture path existed** (confirmed: `--dump-png-prefix`
belongs to `gles2_zone_benchmark`, zones only). Built the smallest tool that
works: `tools/hwcaps/ui_inventory_capture.cpp` (171 lines), registered in
`CMakeLists.txt` next to `p50_mask_plumbing_probe` (same `willeq_core`-link
idiom — no extra source enumeration needed). It creates a real headless
`EDT_OGLES2` device (the same surfaceless-EGL/llvmpipe backend the visual
gate and `gles2_zone_benchmark` use — confirmed by the runtime log: "DRM:
Failed to open /dev/dri/card* … falling back to surfaceless EGL (x86/Xvfb)
… GLES2 Driver: llvmpipe"), populates an `InventoryPanelState`, calls
`renderInventoryPopup()` between `beginScene`/`endScene`, and writes a PNG
via `driver->createScreenShot()` + `writeImageToFile()`. No screenshot flag
was added to the shipping client.

**Finding, fixed in the capture tool (test-harness-only, no production
code):** the very first content frame rendered on a freshly-created device
(immediately after a black "prime" frame) silently dropped every immediate-
mode `draw2DRectangle` call (panel/slot backgrounds) from the captured
screenshot while text still appeared — every capture *after* the first one
in a run was unaffected. Root cause not chased further (a double-buffer
swap-lag artifact was the working theory) since this is a capture-harness
issue, not `renderInventoryPopup()`/`UIRenderer` behavior — every other
capture, and all of `UIRendererGraphicsTest` in the ctest suite (which does
its own `beginFrame`/`endFrame` inside a fixture that persists across
`TEST_F`s), never showed it. Fixed by rendering the real "full" content once
and discarding that screenshot before capturing for real, at each
resolution — confirmed this produces a correct, non-garbled panel
background in the regenerated PNGs (re-inspected after the fix; the "before"
version is not shipped).

**Four PNGs produced, saved in `refactor/artifacts/T57/`** (force-added
past the repo's blanket `*.png` .gitignore rule, same convention as
`refactor/artifacts/H02c/`, `H02d/`):
- `inventory_800x600_full.png` — 800x600 (the shipped resolution), every
  equipment slot and every general slot populated (equipment slots show
  their canonical `getSlotName()`; general slots show a placeholder "Item"
  name + stack count 1-8).
- `inventory_1280x800_full.png` — 1280x800, same fully-populated state.
- `inventory_800x600_sparse.png` — 800x600, only 3 slots populated
  (`SLOT_PRIMARY`, `SLOT_CHEST`, General[0]) — shows the empty-slot
  rendering (canonical slot-name labels) for the rest.
- `inventory_800x600_hover.png` — 800x600, fully populated, `hoveredSlot =
  SLOT_CHEST`.

**Caveat for Will's review, stated rather than judged:** all four captures
render with `atlas=nullptr` (no `UIAtlas` loaded — same convention as T54's
`UIRendererGraphicsTest`), so every slot background/border uses
`UIRenderer`'s documented solid-color atlas-nullptr fallback. In that
fallback path, `drawSprite()` draws the identical dark rectangle for
`SlotBorderHover` and `SlotBorderNormal` alike (the fallback ignores
`spriteId`), so the hover capture's `SLOT_CHEST` is not visually
distinguished from any other slot in `inventory_800x600_hover.png` — this
is a property of the no-atlas fallback path itself, not something this
unit's layout change affects; the layout/containment claims do not depend
on it. **Per protocol, this unit does not assess whether the layout looks
right — Will judges that from the four PNGs above.**

### Verification (full protocol bar, run from `/mnt/projects/fable/wt/slot1`)

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` (GLES2 config) | PASS — clean build, no errors |
| 2. `ctest --test-dir build --output-on-failure` | **1941/1942 passed** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented host-ALSA exception, matches baseline). `visual_gate` ctest case: PASS |
| Containment (explicit, per plan) | **All 30 slots PASS** — `InventoryAllThirtySlotsAreWhollyInsideCenterPopup` green; hand arithmetic above cross-checked with a standalone script before writing the renderer |
| Flake check: new/touched UI tests under `ctest -j8`, twice | PASS both runs — `-R 'StaticUIInputTest\|UIRendererGraphicsTest'`, 26/26 both times, no flakes |
| 3. `bash scripts/build-gles2-x86.sh` | PASS — `build-gles2/bin/willeq` + `gles2_zone_benchmark` built clean |
| 4. Visual gate PASS, goldens untouched | PASS — `visual_gate` ctest case green (re-confirmed standalone after all changes); `git status -- tests/goldens` empty — this unit never touches zone rendering |
| 5. `bash scripts/build-arm-noble.sh` | PASS — `build-arm-noble/bin/willeq` (ARM ELF) built clean |
| 6. `check_file_sizes.sh fable` (run from slot1, on task branch) | PASS — 6 files checked (`inventory_layout.h` 148, `static_ui_input.h` 69, `static_panels.cpp` 830, `static_ui_input.cpp` 179, `test_static_ui_panels.cpp` 445, `ui_inventory_capture.cpp` 171 lines), 0 warn / 0 fail |

### Deviations from the plan

1. **No placeholder body outline was drawn in the empty center** — the plan
   offered this as optional ("or a simple placeholder outline if that reads
   better"). The vertical arithmetic left only a 2px margin inside
   `centerPopup`'s 350px height; the center is genuinely empty space between
   the two columns, matching T54's recorded "no paperdoll" gap.
2. **`InventoryEquipGridLastPartiallyReachableRow` has no replacement test**
   — it pinned an artifact specific to the old layout (a row straddling
   `centerPopup`'s bottom edge); the new layout has no row in that
   condition, so there's nothing equivalent to test. Noted here per the
   "deviations flagged, never silently absorbed" rule.
3. **The capture-tool double-buffer/first-frame artifact** (Step 4 above) —
   found and fixed entirely inside the new capture tool; does not touch
   `renderInventoryPopup()`, `UIRenderer`, or any shipping code path.
4. **PNGs committed to `refactor/artifacts/T57/`** rather than left in a
   scratch/tmp location — the plan said "put them somewhere self-contained
   and say exactly where"; this repo already has a convention
   (`refactor/artifacts/<unit>/`) for exactly this, force-added past the
   blanket `.gitignore` `*.png` rule the same way `H02c`/`H02d` did.

No STOP conditions were triggered: `centerPopup`'s size was never touched;
the spellbook/skills/hotbar code paths were not touched (confirmed by
`git diff` scope — only `static_panels.cpp`'s `renderInventoryPopup()` and
`static_ui_input.cpp`'s inventory block changed); no 3D character preview
was implemented or attempted; no slot-position arithmetic remains in either
`renderInventoryPopup()` or `processInput()` (both call
`InventoryLayout::slotRect()` exclusively); goldens/visual gate untouched.
