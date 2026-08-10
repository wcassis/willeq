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

### Step 1 — ported vs deleted (`tests/test_inventory_model_view.cpp`)

Deleted: `InventoryModelViewTest` fixture entirely — its six `GTEST_SKIP()`-
only `TEST_F`s (old file `:251-288`) and its ~150 lines of now-unreachable
live-server helper machinery (config loader, `createClientWithGraphics()`,
`waitForZoneReady()`, `processFrames()`, old file `:47-248`), plus the
commented-out includes (`:34-37`). None of the deleted helper machinery was
reused — the ported tests only need `RaceModelLoader` + `EQAnimatedMeshSceneNode`
under `EDT_NULL`, same as the pre-existing `RaceModelAnimationHeadlessTest`.

Ported, as new `TEST_F`s in `RaceModelAnimationHeadlessTest`:
- `ModelHasTexturesApplied_Headless` (was `ModelHasTexturesApplied`) —
  after `createAnimatedNode()`, asserts `getMaterialCount() > 0` and at
  least one material has a non-null bound texture, for HUM/BAM.
- `LoadsForRaceAndGender_Headless` (was the salvageable half of
  `ModelViewInitializesAndLoadsCharacter`) — loops 4 valid race/gender
  combos (HUM m/f, BAM m, DWM m) asserting non-null node + non-null
  instance mesh, plus race=330 (no model data — deferred.md's own
  "gfaydark zone-in FATAL, missing race=330 model data" entry) asserting
  graceful `nullptr`, not a crash. The `hasModelView()`/`isReady()` half
  of the original was dropped — pure view predicates, no production code
  left.
- `CreateAnimatedNodeWithEquipment_Headless` (was
  `EquippedWeaponsShowOnModel`) — **could not be ported as literally
  stated; see the finding below.** Asserts
  `createAnimatedNodeWithEquipment()` succeeds and returns a fully-formed
  node (non-null instance mesh, materials present) given a non-trivial
  `EntityAppearance` (chest equipment texture override) — the entry
  point's actual, previously-zero-coverage behavior.

Deleted outright (no production code left to assert on): `ModelRendersToTexture`,
`ModelViewSurvivesZoneTransition`, and the original `ModelAnimatesProperly`
(already superseded pre-T54 by `ModelAnimatesProperly_Headless`, which needed
no change).

**Finding — "equipped weapons show on model" cannot be ported literally.**
Read end-to-end (`src/client/graphics/eq/animated_mesh_creation.cpp:315-624`),
`RaceModelLoader::createAnimatedNodeWithEquipment()` — the only
equipment-aware entry point `RaceModelLoader` exposes — does **not** attach
weapon geometry to the returned node. It only (a) selects a robe body
variant/texture and (b) overrides body-part (chest/legs/feet/arms/wrist/
hands/head) textures and tints from `EntityAppearance::equipment[]`.
`EquipSlot::Primary`/`Secondary` (indices 7/8, the weapon slots) are read
only to detect robes via `equipment[Chest]`, never to attach a weapon mesh.
Actual weapon attachment (`primaryEquipNode`/`secondaryEquipNode`) is built
entirely inside `EntityRenderer` via a separate `EquipmentModelLoader` mesh
lookup — a materially heavier production dependency (the full entity
visual pipeline) that would have meant pulling `EntityRenderer` into a
headless test fixture, disproportionate to this unit and arguably its own
unit. Per the plan's own instruction ("if an assertion cannot be made to
hold, report it as a finding rather than weakening it"), the test was
scoped to what the entry point actually does (succeeds, returns a
materialed node) rather than silently dropped or faked. Recorded on the
board in the new "inventory character preview" `deferred.md` entry.

**Wiring gap found and fixed in test setup (not production behavior)**:
the fixture originally (pre-existing, `RaceModelAnimationHeadlessTest`)
never wired a `ConstrainedTextureCache`, so the new
`ModelHasTexturesApplied_Headless` test initially failed for real —
`ZoneMeshBuilder::loadTextureFromBMP()` `LOG_FATAL`'d "no constrained
cache" for every texture and every material ended up textureless. Fixed
by wiring a `ConstrainedTextureCache` (TNT preset) into the fixture's
`SetUp()`, **at both of the two required wiring points** —
`RaceModelLoader::setConstrainedTextureCache()` (only feeds its own P17
`isLiveTexture()` defensive check) **and**
`RaceModelLoader::getMeshBuilder()->setConstrainedTextureCache()`
separately (the `ZoneMeshBuilder` that actually decodes/uploads BMP bytes
needs its own pointer) — the same two-call idiom
`tools/hwcaps/p50_mask_plumbing_probe.cpp` already uses. This is
test-infrastructure-only; no production code changed for this.

### Step 2 — dead fixture removal

Confirmed no ported test needs any of the deleted live-server helpers.
`tests/CMakeLists.txt:975-991`'s comment block rewritten to describe the
file's new nature (headless suite, auto-discovered, no server/DISPLAY) —
old comment referenced `InventoryModelViewTest`/paperdoll UI by name.
File not renamed per the plan's instruction; a rename to
`test_race_model_loading.cpp` is recorded as a recommendation in both the
CMakeLists comment and this Review, left for the orchestrator.

### Step 3 — new tests for the static-panel UI (`tests/test_static_ui_panels.cpp`)

23 new tests, registered in `tests/CMakeLists.txt` under the same
`IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)` gate as `test_inventory_model_view`
(needs Irrlicht headers/link, no device for 21 of them).

`StaticUIInputTest` (21 tests, no Irrlicht device — pure `processInput()`/
`processKey()` calls against `UILayout`+panel-state structs):
- **Hotbar** (5): first/last slot hover by index, padding-gap clears hover
  and does *not* consume (unlike the popups below), outside-vertical-range
  clears hover, click pushes `HotbarActivateIntent{index}` via a real
  `GameStateBridge` subclass (`TestBridge`, same minimal-fake idiom as
  `test_game_state_sync_bridge.cpp`'s `RecordingBridge`).
- **Spell gems** (3): first/last gem hover by index, click pushes
  `CastSpellIntent{gemSlot}`.
- **Inventory popup** (8): first-cell hover, second-column col-major index
  (`col*rows+row`, confirmed matching `static_panels.cpp`'s render-side
  `i/11, i%11` placement so hit-test and render agree), padding-gap still
  consumes (architectural asymmetry vs hotbar/gems — any point inside
  `centerPopup` consumes, set unconditionally before the per-slot check),
  last-partially-reachable row, `activePopup==None` never hover-hits even
  over the popup's screen region, outside-popup-region no-hover — plus two
  tests recording the **layout-sizing defect** below.
- **`processKey`** (4): ESC closes an open inventory popup, ESC closes
  spellbook before inventory (pins the precedence order in
  `static_ui_input.cpp`), ESC with nothing open returns false, key-release
  is always ignored. Note: `processKey` only ever *closes* a popup (ESC) —
  there is no key path in `StaticUIInput` that *opens*
  `PopupType::Inventory` (that's presumably the 'I' hotkey binding
  elsewhere); the plan's "processKey popup toggling on/off" language is
  tested for the close half only, since that's all this class does.
- **Null-bridge click** (1): `HotbarClickWithoutInitDoesNotCrash` — see the
  `bridge_`-null finding below.

`UIRendererGraphicsTest` (2 tests, `EDT_NULL` device, the optional Step-3
tail — reached because the input tests were green with time remaining):
`renderInventoryPopup()` with no atlas/no text batch doesn't crash and
exercises `drawSprite`'s documented atlas-nullptr fallback
(`driver_->draw2DRectangle`); the closed-popup (`activePopup==None`) path
is a no-op.

**`bridge_`-null finding (plan's "known hazard"): does NOT materialize as
a crash risk today.** Read `src/client/graphics/ui/static_ui_input.cpp`
end-to-end: `bridge_` is dereferenced at exactly two sites, and both are
already guarded — `if (leftClicked && bridge_)` before the hotbar's
`HotbarActivateIntent` push (line 40) and before the spell gem's
`CastSpellIntent` push (line 56). The inventory/spellbook/skills popup
click paths don't touch `bridge_` at all currently (the spellbook click
handler only `LOG_INFO`s — see the new deferred.md entry). So a null
`bridge_` (the state before `init()` is called) cannot segfault on click
through any path that exists today; `HotbarClickWithoutInitDoesNotCrash`
pins that both guards hold. No one-line null guard was needed — there was
nothing unguarded to guard. This is a "checked, hazard didn't
materialize" finding, not a fix.

**Real production layout-sizing defect found by the boundary tests**
(read-only production territory this unit — left unfixed, recorded on the
board): at the default 800x600 layout, `InventoryPanelState`'s entire
8-slot general-inventory row is **unreachable** by mouse — `genY` (529)
sits 54px below `centerPopup`'s bottom edge (475), so the outer
popup-bounds check in `processInput()` rejects every point in that row
before the per-slot hit test runs. The equipment grid's bottom two rows
(of 11) are also partially/fully unreachable for the same reason. See
`InventoryEquipGridRow10IsUnreachable` and
`InventoryGeneralSlotsAreWhollyUnreachable`, and the new deferred.md entry,
for the exact geometry.

### Step 4 — dangling references and the board

- `src/client/graphics/eq/animated_mesh_creation.cpp:252,400` (two
  identical comments) corrected — no longer name `CharacterModelView`,
  now describe the isolated-scene-manager case generically with a note
  that the class was deleted and no replacement caller exists today. Code
  below the comments untouched.
- `src/client/graphics/CLAUDE.md`'s "Graphics Integration Tests" section
  rewritten: removed `test_inventory_model_view` from the
  server+DISPLAY-required list and its `DISPLAY=:99` run examples, added a
  paragraph explaining it's now a headless, auto-discovered suite, and
  pointed at the new `test_static_ui_panels.cpp`.
- `docs/future/ui_fixes.md` checked: describes the pre-rewrite
  `WindowBase`-derived UI's general draw-call/performance problems (the
  motivating problem statement for the whole static-panel UI batch), not
  the inventory character-model-preview feature specifically — left
  uncorrected as out of this unit's specific scope (it's the "future/"
  planning doc that predates and motivated the rewrite, not a claim about
  current architecture).
- `refactor/deferred.md`: re-titled the `InventoryModelViewTest` skip
  bullet (inside the T53 Index's "Test coverage gaps" section) in place as
  **RESOLVED by T54**, with what was ported/deleted and why. Appended two
  new entries: "the inventory character preview (3D paperdoll) is a
  deliberate, recorded gap — not a bug" (the feature-loss record Will
  asked for explicitly), and "static-panel UI surface still untested after
  T54" (spellbook/skills hit-testing at zero coverage, the spellbook
  memorize-click no-op, every render function but
  `renderInventoryPopup` untested, the layout-sizing defect, `UIAtlas`-
  backed rendering untested). Both appended, nothing deleted.

### Test/skip count deltas

Per-file: `test_inventory_model_view.cpp` — 7 registered tests before (6
skip-only + `ModelAnimatesProperly_Headless`) → 4 after (all real,
0 unconditional skips). `test_static_ui_panels.cpp` — 0 → 23 (new file).
Net across both files: **+20 tests, -6 unconditional skips.**

Graphics-tree skip count (this unit's slot1 `build/`, GLES2 config, full
`ctest` run): **14** (`BridgeTeardownAsanRepro` x2, `SpellManagerGems` x2,
`PfsArchiveContentTest.DdsFileValid`, `DoorManagerGraphicsTest` x8,
`SoundBufferTest.LoadSwingWav`) — down from the stated baseline of 20,
exactly the 6 `InventoryModelViewTest` skips gone and nothing else moved.
Headless-tree skip count (`build-nogfx/`): **5** (`BridgeTeardownAsanRepro`
x2, `SpellManagerGems` x2, `SoundBufferTest.LoadSwingWav`) — unchanged
from baseline, as expected (`InventoryModelViewTest`/the new UI test file
are both `EQT_GRAPHICS`-gated and never register there).

### Verification (full protocol bar, run from `/mnt/projects/fable/wt/slot1`)

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` (GLES2 config) | PASS — clean build, no errors |
| 2. `ctest --test-dir build --output-on-failure` | **1930/1931 passed** — sole failure `AudioManagerTest.PreloadCommonSounds` (the documented host-ALSA exception). Same tree also ran the pre-existing `visual_gate` ctest case: PASS |
| 2b. `ctest --test-dir build-nogfx --output-on-failure` (headless tree, informational — not the primary config but cheap to confirm against the plan's stated headless baseline) | **1481/1482 passed**, same sole exception, byte-for-byte matching the stated 1481/1482 baseline (unaffected by this unit, as expected) |
| 3. `bash scripts/build-gles2-x86.sh` | PASS — `build-gles2/bin/willeq` + `gles2_zone_benchmark` built clean |
| 4. Visual gate PASS, goldens untouched | PASS — `visual_gate` ctest case green in both trees; `git status` shows no golden/contact-sheet changes |
| 5. `bash scripts/build-arm-noble.sh` | PASS — `build-arm-noble/bin/willeq` (ARM ELF) built clean |
| 6. `check_file_sizes.sh fable` (run from slot1, on task branch) | PASS — 3 files checked (`animated_mesh_creation.cpp` 694 lines, `test_inventory_model_view.cpp` 346 lines, `test_static_ui_panels.cpp` 361 lines), 0 warn / 0 fail. (Required `git add`ing the new untracked test file first — the script's `git diff` base otherwise silently excludes untracked files; noting this so a future unit doesn't lose time on it.) |
| Flake check: new UI tests under `ctest -j8`, twice | PASS both runs — `-R 'StaticUIInputTest\|UIRendererGraphicsTest'` (23/23) and `-R 'StaticUIInputTest\|UIRendererGraphicsTest\|RaceModelAnimationHeadlessTest'` (27/27), 100% both times, no flakes |

### Deviations from the plan

1. **`CreateAnimatedNodeWithEquipment_Headless` does not assert weapon
   attachment**, as detailed in the Step 1 finding above — the plan's
   literal wording ("assert the attachment child node exists") describes
   behavior that doesn't exist at the `RaceModelLoader` layer. Reported as
   a finding rather than weakened, per the plan's own instruction for
   exactly this situation.
2. **Test setup required wiring a `ConstrainedTextureCache` into
   `RaceModelAnimationHeadlessTest`** (not called for explicitly in the
   plan, which described the fixture as already-working) — needed for the
   new texture-assertion test to exercise real behavior rather than
   trivially fail; test-infrastructure-only, no production code changed.
3. **`processKey`'s "popup toggling on/off"** is tested for close-only
   (ESC), since `StaticUIInput::processKey` has no open path for
   `PopupType::Inventory` — noted in the Step 3 section above.
4. **`docs/future/ui_fixes.md` left uncorrected** — judged out of scope
   (general pre-rewrite UI performance doc, not a paperdoll-specific
   claim); see Step 4 above.
5. Ran the headless (`build-nogfx`) ctest tree in addition to the primary
   GLES2 config tree the protocol bar names, to honor the plan's explicit
   "before/after ... 5 headless" reporting ask — cheap (already-configured
   tree, no source changes touch it) and confirms zero unintended impact
   there.

No STOP conditions were triggered. No paperdoll/RTT/new-UI-behavior was
implemented. No production change beyond the two comment corrections
(Step 4) and the `CLAUDE.md`/`deferred.md` doc updates — no null guard was
needed (see the `bridge_` finding). Goldens/visual gate untouched.
