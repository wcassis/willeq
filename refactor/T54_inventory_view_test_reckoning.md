# T54 — Six permanently-skipped tests for a deleted class, and zero tests for the UI that replaced it

**Slot: t1. x86 only. No Pi. NO EQEMU** — everything here runs headless
or under an `EDT_NULL` device. If you find yourself wanting a server,
you have misread the unit: STOP and report.

**Territory: `tests/test_inventory_model_view.cpp`, one NEW test TU for
the static-panel UI, and `tests/CMakeLists.txt`.** Production code is
**read-only in this unit** with a single exception noted in Step 4
(dangling comments). Do not touch `src/client/graphics/ui/*.cpp`
behaviour, do not implement a paperdoll, do not modify
`irrlicht_renderer.*`.

Sibling units this round: T55 (`src/client/eq*.cpp`, state layer) and
T56 (`main.cpp`, `willeq_database.cpp`). **No file overlap with either.**
The one shared file is `refactor/deferred.md` — append only, expect a
trivial merge, never reflow another unit's section.

Read `refactor/EXECUTOR_PROTOCOL.md` first, then `refactor/deferred.md`'s
Index (heading anchors) and its test-skip inventory section.

## Why this unit exists

`refactor/deferred.md` flagged that all six `InventoryModelViewTest`
cases are unconditionally `GTEST_SKIP()`'d — "a fixture nobody
re-enabled after a dependency was deleted", unlike the other
environment-gated skip families. Orchestrator scouting turned that into
a sharper and less comfortable finding.

**The skips are not stale gating. The subject is gone.**
`WindowManager`, `InventoryWindow` and `CharacterModelView` were all
deleted in `0f25cb57` ("U07c2: Delete WindowManager + all old UI window
files (~35K lines)", 2026-03-15) — the *same commit* that added the
skips, whose own message says "Updated tests: test_inventory_model_view
tests GTEST_SKIP (need new UI)". The bodies were gutted in that commit
(307 deletions); each `TEST_F` is now nothing but the skip line.
`getWindowManager()` is gone (`include/client/graphics/irrlicht_renderer.h:1072`),
and the old entry points are no-op stubs
(`src/client/graphics/renderer_entities.cpp:328-360` — `isInventoryOpen()`
literally `return false`).

So this is **not** a re-enable job. Three outcomes, per subject:

1. **Assertions with live production code behind them** (model loads,
   has textures, equipped weapons show) — the loader/node layer
   (`RaceModelLoader`, `EQAnimatedMeshSceneNode`) is alive and unchanged.
   These get **ported headless**.
2. **Assertions about the view itself** (`getTexture()` render-to-texture,
   `hasModelView()`, view-survives-zoning) — no production code exists.
   Testing them would require *implementing a paperdoll first*. These get
   **deleted**, with the feature loss recorded on the board.
3. **The replacement UI has no tests at all.** No file under `tests/`
   includes `static_panels.h`, `ui_renderer.h`, `ui_layout.h`, or
   `static_ui_input.h`. `renderInventoryPopup()` and the hit-testing in
   `static_ui_input.cpp` are untested production code. **Closing that is
   the most valuable half of this unit** — Will chose this scope
   explicitly over the cheaper tidy-up.

Note what the arithmetic says: we are deleting six tests that assert
nothing and writing tests for code that has never been asserted on at
all. Do not let the deletion feel like the achievement.

## Step 1 — port the salvageable assertions into the headless fixture

`RaceModelAnimationHeadlessTest` (`tests/test_inventory_model_view.cpp:309-451`,
added by P12b `dfac1e53`) already works in CI today: `EDT_NULL` device,
`RaceModelLoader::createAnimatedNode()`, `playAnimation("l01"/"o01")`,
step `OnAnimate()`, assert vertices move. **Use it verbatim as the shape** —
same asset gating (`global_chr.s3d` presence), same device setup.

Port these three, as new cases in that fixture:

- **Textures/materials** (was `ModelHasTexturesApplied`): after
  `createAnimatedNode()`, assert `getMaterialCount() > 0` and that
  material texture pointers are non-null.
- **Loads for race/gender** (was the salvageable half of
  `ModelViewInitializesAndLoadsCharacter`): loop several `RaceCase`
  entries; assert non-null node and non-null instance mesh. Drop the
  `hasModelView()`/`isReady()` half — those were view predicates.
- **Equipped weapons** (was `EquippedWeaponsShowOnModel`): use
  `RaceModelLoader::createAnimatedNodeWithEquipment()`
  (`include/client/graphics/eq/race_model_loader.h:145`) with a weapon
  id, assert the attachment child node exists.

Expect ~80-150 lines. If an assertion cannot be made to hold, **report it
as a finding rather than weakening it to `EXPECT_TRUE(true)`** — a
vacuous test here is exactly the failure mode this unit exists to clean up.

## Step 2 — delete the dead fixture

Delete the `InventoryModelViewTest` fixture entirely: the six skip-only
cases (`:251-288`) **and** its ~150 lines of now-unreachable live-server
helper machinery (`:101-247` — config loader, `createClientWithGraphics()`,
`waitForZoneReady()`, `processFrames()`). Check before deleting whether
the new headless cases want any of those helpers; if so, keep only what
is used and say so in the Review.

Also drop the commented-out includes at `:34-37`. Update the CMake
comment at `tests/CMakeLists.txt:976-992`, which currently documents the
skip state.

The file ends up as the headless model-loading suite. Rename it in the
Review's recommendation only — **do not rename the file in this unit**
(it churns CMake and git history for no test value); note the suggestion
and let the orchestrator decide.

## Step 3 — NEW: first tests for the static-panel UI

New TU, `tests/test_static_ui_panels.cpp`, registered in
`tests/CMakeLists.txt` alongside the other graphics-gated tests.

**Start with input/hit-testing — it is the highest-value, lowest-
friction surface.** `StaticUIInput::processInput()`
(`include/client/graphics/ui/static_ui_input.h:42`) is public and takes
every piece of state by reference, so it is directly drivable with no
device at all. The private `hitTestSlotGrid`/`hitTestSlotRow` (`:62-65`)
are reachable through it.

Cover at minimum:
- Hover: mouse inside a given inventory slot sets
  `InventoryPanelState::hoveredSlot` to that index; mouse in the gap
  between slots (the padding) sets `-1`; mouse outside the popup sets `-1`.
- Boundary correctness: first slot, last slot, and the exact slot/padding
  edges — off-by-one in a hit test is the classic defect and nothing has
  ever checked it here.
- Consumption: `processInput` returns true when the popup owns the point
  and false when it does not (this is what gates click-through to the 3D
  scene).
- `processKey` popup toggling (`PopupType::Inventory` on/off), and that
  `activePopup == None` means no slot ever hover-hits.

**Known hazard — `bridge_` is null unless `init()` is called**
(`static_ui_input.h:59`). Hover paths are likely safe; **click paths may
dereference it**. Read `src/client/graphics/ui/static_ui_input.cpp`
before writing the click tests. If clicks require a bridge, use the same
bridge test double the existing bridge tests use — see
`tests/test_game_state_sync_bridge.cpp` for the established pattern.
**If a null `bridge_` segfaults on click, that is a production
robustness finding: report it, add the guard only if it is a one-line
null check, and record it on the board either way.**

Then, if and only if Step 3's input tests are green and time remains,
add a *rendering* smoke test: `EDT_NULL` device → `UIRenderer::init()`
→ `beginFrame()` → `renderInventoryPopup()` with a populated
`InventoryPanelState`, asserting it emits a plausible non-zero quad
count and does not crash with a null atlas (`UIRenderer::drawSprite`
documents an atlas-nullptr fallback — verify that fallback actually
holds). **This is the optional tail of the unit. Do not let it sink
Steps 1-3.** Follow `tests/test_door_loading.cpp:223-410`
(`DoorManagerGraphicsTest`) for graphics-object-under-`EDT_NULL` setup.

## Step 4 — dangling references and the board

- `src/client/graphics/eq/animated_mesh_creation.cpp:252,400` carry
  comments referencing "isolated scene managers (e.g.,
  CharacterModelView)" — a class deleted five months ago. **Comment-only
  correction is in territory**; do not touch the code they sit above.
- Check whether `src/client/graphics/CLAUDE.md` or `docs/` still
  describes an inventory paperdoll; correct if so.
- `refactor/deferred.md`, **append, never delete** (this file's history is
  load-bearing — see T53):
  - Re-title the `InventoryModelViewTest` skip entry to **resolved**,
    stating what was ported and what was deleted and why.
  - **New feature entry**: the inventory character preview (3D paperdoll,
    `CharacterModelView`) was removed with the old UI in `0f25cb57` and
    never reimplemented in the static-panel UI. Record it as a deliberate
    known gap, not a bug — so the intent is not lost now that its last
    trace (the skipped tests) is gone. Will asked for this explicitly.
  - **New entry**: remaining static-panel UI surface still untested after
    this unit (be honest about what you did not cover).

## STOP conditions

- Any urge to implement a paperdoll, an RTT path, or new UI behaviour.
- Any production change beyond comment corrections and (at most) a
  one-line null guard justified in Step 3.
- Goldens or the visual gate needing a re-bless — this unit must not
  change rendering. If the gate moves, you broke something: STOP.

## Verification

Full protocol bar (build, ctest, gles2 script, visual gate, ARM,
check_file_sizes from your slot on your branch). Additionally:

- State the test count delta explicitly: tests deleted, tests added, net.
  Baseline is 1913/1914 graphics, 1481/1482 headless; the only permitted
  failure in either tree is `AudioManagerTest.PreloadCommonSounds`.
- Report the **skip count** before and after (was 20 graphics / 5
  headless). Six unconditional skips should be gone.
- Run the new UI tests under `ctest -j` at least twice — the new TU must
  not introduce a port/resource flake (see the T35 note in the protocol).

## Review

(executor fills: what was ported vs deleted with line evidence, the new
UI test list and what each actually asserts, the `bridge_`-null finding,
test/skip count deltas, per-step verification results, deviations)
