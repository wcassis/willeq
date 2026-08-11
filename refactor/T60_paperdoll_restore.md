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

(executor fills: restore diff vs the recovered originals, with any
adaptation called out line by line; the two bone-logic copies diffed,
which was kept, and the evidence that in-world placement is unchanged;
where the paperdoll rect is defined; the appearance-path branch taken and
why it matches the player entity; render-trigger evidence including
proof of no per-frame rendering; Pi results with timings; PNG paths;
deviations)

Board (`refactor/deferred.md`, append, never delete): re-title T54's
"inventory character preview is a deliberate, recorded gap" entry —
it is no longer a gap. Record the bone-logic convergence and that the
held-item misalignment remains open, now with a usable instrument for
diagnosing it.
