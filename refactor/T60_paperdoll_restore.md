# T60 — Restore the inventory paperdoll, and converge the bone-attachment logic it was duplicating

**Slot: t1. x86 first; ONE Pi verification leg (see Step 5). NO EQEMU** —
the doll renders from an `EntityAppearance` with no server.

**Territory: a restored `character_model_view.{h,cpp}` (new home:
`src/client/graphics/ui/` + `include/client/graphics/ui/`),
`include/client/graphics/ui/inventory_layout.h` (add the paperdoll rect),
`src/client/graphics/ui/static_panels.cpp` (draw the RTT),
`src/client/graphics/irrlicht_renderer.*` (ownership + wiring),
`src/client/graphics/entity_equipment.cpp` (bone-logic convergence only —
see Step 2), `tools/model_viewer.cpp` (same), and tests.**

Do not change `UILayout::centerPopup`'s size. Do not touch the
spellbook/skills/hotbar paths. Do not change how in-world entities are
rendered beyond the convergence in Step 2 — **that convergence must be
behaviour-preserving for in-world entities and you must prove it.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T57_inventory_paperdoll_layout.md` including its Review (it
reserved the space you are filling and established the shared-geometry
pattern you will follow), then `refactor/T54_inventory_view_test_reckoning.md`'s
Review (it recorded the paperdoll's absence and found that
`createAnimatedNodeWithEquipment()` does not attach weapon geometry).

No sibling units at dispatch. `fable` tip: see the dispatch message.

## Why this unit exists

The inventory popup has a large empty centre. That space is not a layout
mistake — it is where the character paperdoll goes, so the player sees
their equipped appearance change as they equip items. T57 deliberately
reserved it.

**This is a restoration, not a new feature.** `0f25cb57` ("U07c2: Delete
WindowManager + all old UI window files") deleted exactly two files —
`include/client/graphics/ui/character_model_view.h` (200 lines) and
`src/client/graphics/ui/character_model_view.cpp` (835 lines) — as
collateral damage when the old UI framework went. Recover them with:

```
git show 0f25cb57^:include/client/graphics/ui/character_model_view.h
git show 0f25cb57^:src/client/graphics/ui/character_model_view.cpp
```

**Verified before this plan was written:**
- The implementation has **zero** references to `WindowManager`,
  `WindowBase`, or `InventoryWindow`. Its interface is framework-free:
  `init(smgr, driver, w, h)`, `setCharacter(raceId, gender, appearance)`,
  `renderToTexture()`, `getTexture()`, `markDirty()`, plain-int mouse
  handlers for drag-rotation.
- **Every dependency still exists**: `RaceModelLoader`,
  `EquipmentModelLoader` (alive and used by `entity_renderer.cpp` /
  `entity_equipment.cpp`), `InventoryManager` (moved to
  `include/client/inventory/`), `animated_mesh_scene_node`,
  `skeletal_animator`, `s3d_loader`.
- It already does the things a naive rewrite would get wrong: an isolated
  scene manager (`smgr->createNewSceneManager(false)`), `markDirty()` for
  render-on-change, and graceful degradation when RTT is unavailable.

**Correction to an earlier orchestrator assumption, recorded so nobody
repeats it:** RTT was previously believed unproven in production. It is
not — the paperdoll shipped and performed acceptably, on Mali400 and x86,
before the static-UI and GLES2 work. The genuine open question is
narrower and is Step 5.

## Step 1 — restore and re-home

Bring both files back. Adapt ownership only:

- The old `init()` was driven by `WindowManager`. The owner is now
  `IrrlichtRenderer`, which already owns `inventoryState_` and the static
  panels. Give it a `std::unique_ptr<CharacterModelView>`, constructed
  lazily when the inventory popup first opens — **not at renderer init**;
  an RTT allocated for a panel the player never opens is exactly the kind
  of silent memory cost this project is trying to stop.
- Wire `setRaceModelLoader` / `setEquipmentModelLoader` /
  `setInventoryManager` from the renderer's existing instances. Do not
  create second instances of any of them.
- `MOD_UI` may no longer exist as a log module — map to whatever the
  static-panel code uses.

Keep the file name and class name. Do not "modernize" the implementation
while restoring it; a restore diff that is mostly rewrite is unreviewable.
Behaviour changes belong in later steps, called out individually.

## Step 2 — converge the bone-attachment logic (the architectural requirement)

**Do not restore the third copy.**

The deleted paperdoll carried its own `extractBoneRotation` and
`findBoneIndex`, and its own comments admit the provenance: "copied from
model_viewer.cpp" and "Matches entity_renderer.cpp implementation for
comprehensive bone lookup". Two copies survive today:

- `src/client/graphics/entity_equipment.cpp:153` — production, what you
  see in 3rd person
- `tools/model_viewer.cpp:1954` — the tool you would naturally debug in

They are **separate implementations** of the same fragile thing: a
fallback chain over bone-name spellings (`R_POINT`, `R_POINT_DAG`,
`BO_R_DAG`, `TO_R_DAG`, upper/lower/underscore/`_DAG`-stripped variants)
plus a `weaponOffset` heuristic derived from the mesh bounding box and a
hardcoded 180° X rotation. Known symptom, reported by Will: **equipped
items do not always appear correctly held in a character's hand.** With
two divergent implementations, debugging in the viewer does not reliably
predict production.

**Extract ONE shared implementation** — `findBoneIndex`,
`extractBoneRotation`, and the weapon offset/rotation calculation — into
a header both production and the tool consume, and have the restored
paperdoll consume it too. This is the same move T57 made for slot
geometry, for the same reason.

**Behaviour preservation is mandatory and must be proven.** The two
copies may have already drifted. Diff them first and **report the
differences before merging them** — if they differ, say which one you
kept and why. In-world entity weapon placement must not change: that is a
visual regression Will would have to re-accept, and it is not this unit's
purpose. If convergence would change in-world placement, STOP and report
rather than shipping a silent visual change.

Add unit tests for the shared bone lookup — the fallback chain is pure
string logic over a skeleton and is directly testable with a synthetic
skeleton. Nothing tests it today.

**Fixing the held-item misalignment is explicitly OUT of scope.** Converge
the logic, test it, leave behaviour identical. The fix needs visual
iteration with Will, and the paperdoll is the instrument that makes it
tractable — which is the next unit, not this one.

## Step 3 — wire it into the panel

- Add the paperdoll rect to `include/client/graphics/ui/inventory_layout.h`
  alongside `slotRect()`. **The centre region must come from the same
  shared source as the slots** — never computed independently in the
  renderer. T57's whole point.
- `renderInventoryPopup` draws `getTexture()` into that rect. `UIRenderer`
  currently only does atlas sprites, bars and solid rects; add the minimum
  needed to blit a texture, following the existing method shapes.
- Assert in tests that the paperdoll rect lies wholly inside
  `centerPopup` and **does not overlap any of the 30 slot rects** — extend
  T57's exhaustive containment/no-overlap tests to cover it.

## Step 4 — appearance source and update triggers

The doll must show **the same character the player sees in 3rd person**,
or the feature actively misleads.

In 3rd person the player is an ordinary `EntityVisual` with
`isPlayer = true`, built from an `EntityAppearance`
(`entity_renderer.cpp:350-373`). Feed the doll **that same
`EntityAppearance`**. Note `entity_renderer.cpp:546`:
`compositeEligible = !isPlayer && ...` — the player deliberately uses the
per-part path, not composite sheets. **The doll must follow the same
branch**; if it renders through the composite path it can disagree with
the in-world model, which is worse than showing nothing.

Update triggers, all via `markDirty()` — **never re-render per frame**:
- equipment/appearance change (the point of the feature),
- popup opened,
- drag-rotation in progress.

When the popup is closed, nothing should render. State that explicitly in
the Review, with evidence.

## Step 5 — the one genuinely open question: RTT on the current GLES2 driver

The paperdoll's RTT worked on Mali400 historically, but the ARM/x86
renderer has since been rewritten to the custom `COpenGLES2Driver`
(P30/P32). The RTT path there is real and Mali-aware —
`addRenderTargetTexture` (`docker/irrlicht-drm/COpenGLES2Driver.cpp:1534`),
FBO + depth renderbuffer + MSAA via
`GL_EXT_multisampled_render_to_texture`, completeness check with fallback
— and it is exercised by `tools/hwcaps/gles2_shader_diagnostic.cpp` and
`gles2_composite_audit.cpp`. But **no code under `src/` uses RTT**, so
this would be its first production use on the rewritten driver, on the
render thread, during live frames.

**Verify on the Pi before declaring the unit done** (Pi protocol per
`EXECUTOR_PROTOCOL.md`): the doll renders correctly on real Mali400
hardware, and the cost of a dirty-frame re-render is measured and stated
in ms. If RTT misbehaves on the real driver, that is the unit's headline
finding — report it; do not paper over it with a software fallback.

Also account for the memory: register the render target with `/pmem`'s
tracked total (`renderer_diagnostics.cpp`). A 256² RGBA target plus depth
is roughly 380KB, and untracked RSS is a standing project concern.

## STOP conditions

- Step 2's convergence would change in-world weapon placement.
- RTT fails or is unacceptably slow on real Mali400 hardware.
- The restore turns into a rewrite.
- Any urge to fix held-item alignment in this unit.
- Goldens moving — this changes UI only; the gate renders zones.

## Verification

Full protocol bar, plus the Pi leg (Step 5). Additionally: the extended
containment/no-overlap tests including the paperdoll rect; the new bone-
lookup unit tests; and PNG captures via T57's
`tools/hwcaps/ui_inventory_capture.cpp` (extend it) showing the popup
with a rendered character — **fully populated equipment and a bare
character, at 800x600**. As always: capture, state what you captured, and
let Will judge. Do not self-bless the visuals.

## Review

### Step 1 — restore diff vs the recovered originals

Recovered with `git show 0f25cb57^:...` into scratch files and diffed
against the final restored files.

**`include/client/graphics/ui/character_model_view.h`** (201 → 212 lines,
25-line diff, all comments): the class interface, member layout, and every
method signature are byte-identical to the original. The only changes are
doc comments — a T60 provenance note at the top, and a reworded comment on
`setInventoryManager()` explaining it no longer drives appearance
reconstruction (Step 4, below).

**`src/client/graphics/ui/character_model_view.cpp`** (836 → 695 lines).
Every adaptation, in full:
1. **Include paths** — `client/graphics/ui/inventory_manager.h` /
   `inventory_constants.h` → `client/inventory/...` (T57's Review recorded
   `InventoryManager` moved there) — no behavior change, just a
   post-restore-era path.
2. **`setInventoryManager()`** no longer calls `updateCharacterModel()` —
   see Step 4 below; this is a deliberate behavior change, not a restore
   artifact.
3. **`updateCharacterModel()`**: the ~46-line block that reconstructed
   `appearance` from `inventoryManager_->getItem()` per-slot materials
   (the original's own per-slot `SLOT_HEAD/CHEST/.../SECONDARY` mapping
   table) is deleted outright — replaced by a comment explaining why
   (Step 4). `createAnimatedNodeWithEquipment()`/`createAnimatedNode()`
   calls gained an explicit `, -1, false` (`compositeAllowed=false`) —
   Step 4.
4. **`attachWeapons()`/`updateWeaponTransforms()`**: the local
   `findBoneIndex`/`extractBoneRotation` static functions (67 lines) are
   deleted; call sites now use the shared `bone_attachment.h`
   implementations. The bone-name suffix lists passed at each call site
   were narrowed to match `entity_equipment.cpp`'s actual production
   lists exactly (e.g. primary went from 6 variants
   `{"r_point","R_POINT","R_POINT_DAG","BO_R_DAG","TO_R_DAG","r_point_dag"}`
   down to production's 4:
   `{"R_POINT_DAG","R_POINT","r_point_dag","r_point"}`) — a deliberate
   Step 2 requirement (the doll must use the exact same lookup production
   uses), not an oversight; `BO_R_DAG`/`TO_R_DAG` were only ever used by
   `model_viewer.cpp`'s now-superseded initial-placement code (Step 2,
   below), never by production. The weapon/shield offset math (~35 lines
   of inlined bbox arithmetic) is replaced by
   `computeWeaponGripOffset()`/`computeShieldGripOffset()` calls. Verbose
   per-bone `LOG_DEBUG` bone-listing dumps (kept in `entity_equipment.cpp`'s
   sibling code as debug-only logging, not present there either) are
   trimmed to match production's logging density.
5. One unused-variable warning (`maxExtent` in `adjustCameraForModel()`,
   pre-existing in the original — the original computed it and never used
   it either) silenced with `(void)maxExtent;` since the restore now
   builds with the project's current, stricter warning set.

**No other line differs.** `setupScene()`, `setupCamera()`,
`setupLighting()`, `adjustCameraForModel()`, mouse-drag handling,
`renderToTexture()`'s dirty-flag gating, `hasTextures()`/`isAnimating()`/
`getMaterialCount()` are byte-identical to the deleted original.

### Step 2 — the two bone-logic copies, diffed, and what was kept

Read end-to-end before writing anything: `entity_equipment.cpp:139-190`
(production, `findBoneIndex`/`extractBoneRotation`) vs
`tools/model_viewer.cpp:1954-2015` (the tool's `findBoneIndex`/
`extractBoneRotation`, plus its separate `updateEquipmentPositions()` at
`:2017-2070` and initial-placement code in `attachEquipmentModels()` at
`:2073-2335`). They had already drifted in three ways:

1. **`findBoneIndex`'s fallback chain differs.** Production tries
   uppercase, lowercase, one underscore-stripped lowercase variant, and a
   `_DAG`-stripped uppercase/lowercase pair (no underscore-joined
   variants). The tool's version instead builds an explicit list of
   `{upper, lower}` pairs per suffix, plus `_DAG`-stripped upper/lower —
   structurally similar but not textually identical, and (per the new
   unit tests) production's `_DAG`-stripped check is case-sensitive
   (`suffix.substr(...) == "_DAG"`, literal uppercase) so a suffix ending
   in lowercase `"_dag"` — which is exactly what production itself passes
   half the time (`"r_point_dag"`) — never even enters that branch. The
   tool's version doesn't have this specific bug (different code, doesn't
   mean better — not audited here since production's behavior is what
   must be preserved).
2. **Weapon grip-offset formula for the primary/secondary weapon slots
   matches** between the two (`length > 3.0f ? length*0.25f :
   -bbox.MinEdge.X*0.8f`) — this part had NOT drifted.
3. **Shield grip offset differs outright.** Production
   (`entity_equipment.cpp:314`, secondary-slot shield branch) uses a flat
   `itemOffset = 0.3f` constant, completely ignoring the shield mesh's
   bounding box. The tool's `attachEquipmentModels()` initial-placement
   code (and the deleted `CharacterModelView`'s own copy) instead computed
   `shieldHeight * 0.175f` from the mesh bbox. These are genuinely
   different formulas that happen to produce visually-similar results for
   typical shield sizes, not a formatting difference.

**Kept: production's exact behavior**, verbatim, in
`bone_attachment.h` — `findBoneIndex`/`extractBoneRotation` are direct,
byte-for-byte transcriptions of `entity_equipment.cpp`'s versions
(confirmed via `diff` while writing the header), and
`computeShieldGripOffset()` returns the flat `0.3f` constant, not the
bbox-derived formula. This was the only choice consistent with the plan's
mandatory in-world-behavior-preservation requirement — production's
existing behavior wins by construction, not by judgment call.

**Evidence in-world placement is unchanged**: `entity_equipment.cpp`'s
`updateEquipmentTransforms()`/`attachEquipment()` bodies were mechanically
edited to call the new shared functions with the identical arguments the
inlined code previously computed inline — no formula, threshold, or
fallback-chain member was altered at the production call site (see the
`git diff` of `entity_equipment.cpp`: the only changes are the two
`static` helper deletions and three inlined-arithmetic-block →
one-line-shared-call replacements). This is a mechanical extraction, not a
rewrite, and it is provable by inspection (`git diff
src/client/graphics/entity_equipment.cpp`).

**Honest limitation, stated rather than glossed over**: there is no
automated test in this repo, before or after T60, that exercises
`EntityRenderer::attachEquipment()`/`updateEquipmentTransforms()` with
real assertions on the resulting weapon transform for a live entity — T54
already found this exact gap (`createAnimatedNodeWithEquipment()` doesn't
even attach weapon geometry at the `RaceModelLoader` layer; attachment
lives entirely in `EntityRenderer`, which no headless test builds). The
visual gate (`visual_gate` ctest case) renders static zone-geometry poses
from `gles2_zone_benchmark`, not live equipped entities, so it does not
exercise this path either. The behavior-preservation claim above rests on
the mechanical nature of the extraction (verified by direct code diff),
not on a regression test proving pixel-identical output — because no such
test exists to run. `tests/test_bone_attachment.cpp`'s 17 new unit tests
cover the shared function's logic in isolation (given a synthetic
skeleton) but do not close this gap; closing it would mean building an
`EntityRenderer`-level equipment-attachment test, which is out of this
unit's scope. Recorded on the board.

Two real, previously-undocumented quirks in the shared (production)
fallback chain were found and are now pinned as tests rather than
"fixed": (1) the `_DAG`-stripped fallback is case-sensitive and never
triggers for a lowercase `"_dag"` suffix (`DAGStrippedBranchNeverTriggersForLowercaseDagSuffix`);
(2) the `_DAG`-stripped "lowercase" candidate is actually mixed-case
because the stripped substring is never independently re-cased
(`FallsBackToDAGStrippedVariantKeepsSuffixCase`). Both are existing
production behavior, preserved verbatim — see the STOP-condition
discussion below for why neither was touched.

**STOP condition check**: convergence did **not** change in-world weapon
placement — the shared functions are a verbatim transcription of
production's existing formulas/fallback chain, confirmed by inspection.
No STOP was triggered.

### Step 3 — where the paperdoll rect is defined

`include/client/graphics/ui/inventory_layout.h`'s
`InventoryLayout::paperdollRect(const UILayout&)` — same shared-geometry
module T57 introduced for the 30 slot rects, same pattern (a pure function
of `layout.centerPopup`, computed from the same `detail::` constants
`slotRect()` uses, so it can never drift independently). Occupies the
space between the two equipment columns and above the weapon row (304x256
px, constant across resolutions since only `centerPopup`'s origin varies).
`renderInventoryPopup()` (`static_panels.cpp`) draws
`InventoryPanelState::paperdollTexture` into this rect via a new
`UIRenderer::drawTexture()` method — the minimal RTT-blit addition to
`UIRenderer`, alongside the existing `drawRect`/`drawSprite`/`drawBar`/
`drawPanel`.

New tests in `tests/test_static_ui_panels.cpp`:
`PaperdollRectIsWhollyInsideCenterPopup` (all four edges, plus non-zero
area) and `PaperdollRectDoesNotOverlapAnySlotRect` (checked against all 30
slot rects) — both green.

### Step 4 — appearance source and render triggers

**Appearance source**: `IrrlichtRenderer::updatePaperdollForFrame()`
(`irrlicht_renderer.cpp`) finds the player's own `EntityVisual` (the
`isPlayer` flag, via `entityRenderer_->getEntities()`) and feeds
`CharacterModelView::setCharacter()` that entity's `raceId`/`gender`/
`appearance` directly — the SAME `EntityAppearance` the in-world 3rd-person
model uses, per `entity_renderer.cpp:350-373`. `createAnimatedNodeWithEquipment()`/
`createAnimatedNode()` are called with `compositeAllowed=false`, matching
`entity_renderer.cpp:546`'s `compositeEligible = !isPlayer && ...` —
players are never composite-eligible, so the doll must not be either (Step
4's explicit requirement). This is why the original's
`inventoryManager_`-driven appearance reconstruction was removed rather
than restored (see Step 1, #3): it was a *second*, independently-derived
appearance that could disagree with the entity's actual one, which is
exactly the failure mode the plan warned against. `setInventoryManager()`
is kept on the interface (unused) — **wiring finding**: S2's bridge
decoupling removed `IrrlichtRenderer`'s own `InventoryManager` pointer
entirely (`irrlicht_renderer.h:997`'s comment: "was
setInventoryManager(InventoryManager*)..."), so there is no first instance
left to wire in the first place; this is architecture, not an oversight.

**Render triggers, all via `markDirty()`** (`characterModelView_->markDirty()`
or the internal call inside `setCharacter()`, which itself only runs when
appearance/race/gender actually changed — see the `appearanceEquals()`
comparison in `updatePaperdollForFrame()`):
- **Popup opened** — `/newinv` command handler in `irrlicht_renderer.cpp`
  calls `markDirty()` when transitioning `None → Inventory`.
- **Appearance/equipment change** — `IrrlichtBridge`'s
  `InventorySlotChanged` handler (`irrlicht_bridge.cpp`) calls the new
  `IrrlichtRenderer::markPaperdollDirty()` for every equip-slot (0-21)
  change.
- **Drag-rotation** — `CharacterModelView::onMouseMove()`'s existing
  `dirty_ = true` (restored verbatim, Step 1).

**Proof of no per-frame rendering**: `updatePaperdollForFrame()` is only
called from `renderer_frame.cpp` when `inventoryState_.activePopup ==
PopupType::Inventory` — when the popup is closed, `characterModelView_`
still exists (once constructed, never destroyed) but nothing touches it,
so `renderToTexture()` never runs and `dirty_` never changes. When the
popup IS open, `renderToTexture()` runs every frame (cheap call) but its
own `if (!dirty_) return;` guard (restored verbatim from the original,
Step 1) makes every non-dirty call a no-op. This is proven, not just
argued: the x86 and Pi capture-tool runs below measured `noop_render`
(second `renderToTexture()` call, `dirty_` already false) at
**0.000–0.002ms** on both platforms — negligible, confirming the guard
holds. `IrrlichtRenderer::ensurePaperdollInitialized()` is also lazy —
called only from `updatePaperdollForFrame()`, itself only reached when the
popup is open — so a player who never opens the inventory never
constructs the doll or its RTT at all (Step 1's explicit ask).

**Memory accounting** (Step 5's ask): no separate `/pmem` registration was
needed. `driver_->addRenderTargetTexture()` creates a real
`COGLES2Texture` registered in the driver's own `Textures[]` array; `/pmem`'s
`[GPU Textures]` line already sums every driver texture via
`COpenGLES2Driver::getGpuTextureMemoryUsage()` (confirmed by reading
`docker/irrlicht-drm/COpenGLES2Driver.cpp:2034-2042`), so the paperdoll's
RTT (304x256 RGBA + depth, ~380KB per the plan's own estimate) is
automatically included the moment it's created — verified by inspection,
not by a live `/pmem` run (no server/session to drive one in this unit's
scope).

### Step 5 — Pi verification (real Mali400)

Deployed to the Pi (`user@10.0.30.192`) per protocol: `ARM_BUILD_BIN_DIR`
pointed at **this slot's** `build-arm-noble/bin` throughout (never `main`).
`ui_inventory_capture` was not previously in the ARM build's target list —
added one `cmake --build . --target ui_inventory_capture` line and one
`cp` entry to `docker/Dockerfile.arm-noble`'s `ENTRYPOINT` (not originally
in this unit's listed territory, but the minimal, low-risk, same-pattern
addition needed to get the Pi-verification instrument onto ARM at all;
called out here as a deviation). Staged the ~48MB ARM binary +
`data/willeq.db` + `data/item_models.json` to **Pi-local `/tmp/T60_capture`**
(never NFS) via `scp`; EQ zone/model assets read from the NFS mount already
shared between the VM and the Pi (`/mnt/projects/EverQuestP1999`, per
`env.sh`). Governor pinned to `performance` for the run, restored to
`ondemand` after (confirmed via `cat .../scaling_governor` before and
after). One invocation this session (well under the 12-invocation cap).
`/tmp/T60_capture` removed after retrieving results — Pi left pristine
(confirmed: `ls /tmp` post-cleanup shows only pre-existing systemd private
dirs).

**Device confirmed real Mali400, not a fallback**: the run log shows `DRM:
Opened /dev/dri/card0`, real mode negotiation (`DRM: Found matching mode
800x600@60Hz`), and `GLES2 Driver: Mali400 (Mesa) [OpenGL ES 2.0 Mesa
25.2.8-0ubuntu0.24.04.1]` — not the x86 llvmpipe/surfaceless-EGL fallback
path. RTT worked correctly: both paperdoll PNGs were written without error
(`setRenderTarget()`'s `success` check inside `renderToTexture()` never
short-circuited).

**Timings** (from the tool's own instrumentation, `TIMING:` lines,
`performance` governor):

| Capture | dirty render after `setCharacter()` | no-op re-render (not dirty) | `markDirty()` + re-render (pure RTT cost) |
|---|---|---|---|
| bare (x86/llvmpipe, for comparison) | 7.444ms | 0.000ms | 0.295ms |
| equipped (x86/llvmpipe) | 1.362ms | 0.001ms | 0.263ms |
| **bare (Pi/Mali400)** | **7.768ms** | **0.002ms** | **2.412ms** |
| **equipped (Pi/Mali400)** | **1.360ms** | **0.002ms** | **1.339ms** |

The "`markDirty()` + re-render" column is the plan's specific ask — the
cost a drag-rotation frame pays repeatedly, isolated from one-time model
construction. On real Mali400: **1.3–2.4ms**, well inside a 33ms (30 FPS)
frame budget, and small relative to the renderer's existing ~6ms
`sceneDrawAll` baseline (`src/client/graphics/CLAUDE.md`'s Orange Pi
numbers) — this RTT is not a performance concern. The no-op path is
confirmed cheap (0.002ms) on real hardware too.

**RTT bug found and fixed on the way to these captures** (this is the
unit's headline finding, per the plan's explicit instruction to report
rather than paper over any RTT misbehavior): the first paperdoll captures
rendered the character **upside-down and cut off**, on BOTH x86/llvmpipe
and, before the fix was in place, would have on the Pi too. Root cause:
`UIRenderer`'s new `drawTexture()` blitted the RTT texture with a
top-to-bottom source rect, which is correct for ordinary loaded images but
wrong for a render-target texture — standard GL FBO convention has V=0 at
the BOTTOM of what was rendered, the opposite of `draw2DImage`'s
assumption for normal images. Fixed by using an inverted source rect
specifically in `drawTexture()` (`ui_renderer.cpp`); `drawSprite()`'s
atlas path is unaffected (ordinary loaded textures only). This is a bug in
the NEW code this unit wrote (`drawTexture()`), not in the restored
`CharacterModelView` or in `entity_equipment.cpp` — confirmed by the fact
that the 3D scene itself (verified via `adjustCameraForModel()`'s
unchanged framing math) was never wrong; only the 2D blit was. Both x86
and Pi captures below are POST-fix.

### PNG paths (captured, not judged — Will judges)

All in `refactor/artifacts/T60/` (force-added past the blanket
`*.png` .gitignore, same convention as T57's `refactor/artifacts/T57/`):
- `inventory_800x600_full.png`, `inventory_1280x800_full.png`,
  `inventory_800x600_sparse.png`, `inventory_800x600_hover.png` — T57-style
  captures re-run unchanged (no paperdoll wired in this code path; confirms
  the new `drawTexture()` call is correctly conditional on a non-null
  texture and doesn't regress the T57 layout).
- `inventory_paperdoll_bare.png` — x86/llvmpipe, HUM race, default
  appearance, no equipment.
- `inventory_paperdoll_equipped.png` — x86/llvmpipe, HUM race, primary
  weapon (item 1000) equipped.
- `pi/inventory_paperdoll_bare.png`, `pi/inventory_paperdoll_equipped.png`
  — same two states, captured on real Orange Pi/Mali400 hardware.

**Stated, not assessed**: the equipped captures (both platforms) show the
attached weapon rendering as an untextured white shape near the
character's hand rather than a recognizable weapon model, and a small
gap/offset from the hand — this is very likely the SAME held-item
misalignment Will already reported (explicitly out of scope for this unit
to fix; see the plan's Step 2/STOP-condition language and the deferred.md
entry below). It is reported here as exactly what the instrument shows,
not fixed. **Per protocol, this unit does not judge whether the layout,
character rendering, or misalignment look acceptable — Will judges from
the PNGs above.**

### Verification (full protocol bar, run from `/mnt/projects/fable/wt/slot1`)

| Step | Result |
|---|---|
| 1. `cmake --build build -j24` (GLES2 config) | PASS — clean build, no errors or new warnings from this unit's code |
| 2. `ctest --test-dir build --output-on-failure` | **1960/1961 passed** (99%) — sole failure `AudioManagerTest.PreloadCommonSounds` (documented host-ALSA exception, matches baseline). `visual_gate`: PASS. Net test delta from the pre-unit baseline (1941/1942): **+19** (17 new `BoneAttachmentTest` cases + 2 new `StaticUIInputTest` paperdoll-geometry cases) |
| Flake check: new/touched UI tests under `ctest -j8`, twice | PASS both runs — `-R 'StaticUIInputTest\|UIRendererGraphicsTest\|BoneAttachmentTest'`, 45/45 both times, no flakes |
| 3. `bash scripts/build-gles2-x86.sh` | PASS — `build-gles2/bin/willeq` + `gles2_zone_benchmark` built clean |
| 4. Visual gate PASS, goldens untouched | PASS — `visual_gate` ctest case green standalone; `git status -- tests/goldens tests/contact_sheets` empty |
| 5. `bash scripts/build-arm-noble.sh` | PASS — `build-arm-noble/bin/willeq` (ARM ELF) + `ui_inventory_capture` (newly added to the ARM target list) built clean |
| 5b. Pi verification (Step 5) | PASS — real Mali400 confirmed, RTT works, timings above, one bug found and fixed (the V-flip) |
| 6. `check_file_sizes.sh fable` (run from slot1, on task branch) | The `fable` ref advanced (`77bea02c`) after this branch was cut from `7fd9ee40` (a sibling unit's work, not mine) — a bare `check_file_sizes.sh fable` run therefore includes files I never touched (`src/client/eq.cpp`, `tests/test_zone_position_convention.cpp`). Re-ran with `--files` naming exactly the 19 files this unit's `git status` shows changed/new: **14 ok, 4 warn (all pre-existing large files this unit added modest amounts to: `CMakeLists.txt`, `irrlicht_renderer.h`, `irrlicht_renderer.cpp`, `renderer_frame.cpp` — none newly crossed the WARN threshold because of this unit), 1 FAIL** — `tools/model_viewer.cpp` at 3217 lines (> 3000). This FAIL is **pre-existing**: the file was already 3257 lines at the `fable` tip this branch was cut from (over the threshold already); T60's changes to it (bone-logic convergence, Step 2) **reduced** it by 40 lines (3257 → 3217), not increased it. No new FAIL was introduced. |

### Deviations from the plan

1. **`docker/Dockerfile.arm-noble`** was touched (added `ui_inventory_capture`
   to the ARM build's target/copy list) — not in the plan's listed
   territory, but necessary to get the Pi-verification instrument (Step 5)
   onto ARM hardware at all. Minimal, additive, same pattern as every
   other tool already in that list.
2. **A real bug was found and fixed in this unit's own new code**
   (`UIRenderer::drawTexture()`'s RTT vertical flip) — see Step 5 above.
   Not a "fix the held-item misalignment" instance (different bug, in
   different code, found and fixed before any PNG was captured) — the
   held-item misalignment itself was left untouched, per the plan.
3. **`CharacterModelView::setInventoryManager()` is now effectively unused**
   (kept for interface parity only) — see Step 4's "wiring finding": S2's
   bridge decoupling left `IrrlichtRenderer` with no `InventoryManager`
   pointer to wire in the first place. Not an oversight; architecture.
4. **The bone-name suffix lists passed at each `attachWeapons()` call site
   were narrowed** from the deleted original's richer lists to match
   `entity_equipment.cpp`'s actual production lists exactly (dropping
   `BO_R_DAG`/`TO_R_DAG` and similar tool-only variants) — deliberate, not
   an oversight; the doll must use the exact bone lookup production uses
   (Step 2's requirement), not a superset that could find bones production
   itself would never find.
5. **No `EntityRenderer`-level equipment-attachment regression test was
   added** — the behavior-preservation claim for in-world placement rests
   on the mechanical nature of the code extraction (verified by direct
   diff), not on a new automated test, because building one would mean
   pulling the full `EntityRenderer` pipeline into a test fixture
   (T54 already found this disproportionate for a narrower ask). Recorded
   as an honest limitation above and on the board.
6. **`check_file_sizes.sh fable` required `--files`** — the shared `fable`
   ref moved during this unit's execution (another session's work); see
   the Verification table.

No STOP condition was triggered: convergence did not change in-world
weapon placement (verbatim extraction, confirmed by diff); RTT works
correctly and cheaply on real Mali400 (no STOP on Step 5); the restore
stayed a restore (25-line comment-only header diff, cpp diff scoped
exactly to the four called-out adaptations); no urge to fix the held-item
misalignment was acted on; goldens/visual gate untouched.

Board (`refactor/deferred.md`): re-titled the T54 "inventory character
preview is a deliberate, recorded gap" entry in place as **RESOLVED by
T60**, in the same style T54 used for its own T53 re-title — full
restore-vs-original accounting, the bone-copy divergence found (shield
offset formula, suffix-list differences), which was kept and why, the two
real fallback-chain quirks pinned as tests, and the held-item misalignment
recorded as still open with the paperdoll now available as a diagnostic
instrument. Nothing deleted.
