# T296 — first-person viewmodel pass, STAGE 1: get arms on screen at all

Slot: slot3. x86. OWNS eqemu (noclobber) for the live capture. Class A,
feature. Decision: docs/decisions.md 2026-08-27 viewmodel entry (Will's).
Territory: a NEW viewmodel render pass (its own TU under
`src/client/graphics/`), the render-frame hook that invokes it
(`renderer_frame.cpp`), the first-person branch in `entity_renderer.cpp`
that currently un-hides the in-world player, tests. Do NOT touch
ui_renderer/paperdoll (T294 slot1) or the shared first-person camera helper
(T295 slot2 is de-duplicating it — coordinate: read its result before
touching camera code).

## Why the previous four attempts failed — do not repeat them
T71/T74/T75/T76 all rendered the IN-WORLD player model viewed from inside.
T76 measured the outcome precisely: in camera space the arm geometry spans
**idle Z=[-1.06, 1.11], 1H-slash Z=[-1.02, 2.18], hand-to-hand Z=[-0.04,
2.15]**. **Negative Z is behind the camera.** Pullbacks of 0/0.2/0.3/0.5/1.0
were each tested; none clears it, because the span exceeds any camera
offset. **No near-plane value and no camera position fixes geometry that is
partly behind the eye.** Will has ruled out lowering the global near plane.

## The shape (verify it before building it)
A viewmodel is drawn in VIEW space at a designed offset, not at its world
position:
1. Render the world as now.
2. **Clear the depth buffer** (viewmodel must not be clipped by world
   geometry, and must not write into the world's depth for later passes).
3. Render the arm geometry + the two bone-attached weapon nodes with a
   transform that places them at a fixed offset in front of the camera, with
   the viewmodel pass's OWN projection (a small near plane is safe here
   because it applies only to this pass).
4. UI draws after, unchanged.
**Step 0 of this unit is to confirm that reasoning against the actual
renderer** — Irrlicht scene-manager structure, whether a second camera or an
explicit transform is the cleaner hook, and how the GLES2 driver handles a
mid-frame depth clear. If the renderer makes a different shape obviously
better, say so with evidence and take it; do not force this one.

## STAGE 1 SCOPE — the bar is "arms are visibly on screen", nothing more
- The pass exists, is entered only in first person, and draws the T74
  arm-filtered geometry plus the primary/secondary equip nodes.
- **Acceptance: a live capture in which arms and a held weapon are
  identifiable as arms and a weapon.** Pose quality, offset tuning, animation
  polish and FOV are STAGE 2 — do not gold-plate.
- Headless proof that the pass runs and emits draw calls, plus a pixel test
  that the viewmodel region is non-background when first person is on and
  background when it is off.
- The world render must be unchanged when first person is OFF: visual gate
  PASS with goldens untouched is the guard.

## Reporting discipline — this is the part four sessions got wrong
**OPEN EVERY CAPTURE AND DESCRIBE WHAT IS ACTUALLY IN IT.** T76's README
called an unrecognizable smeared mass "recognizable... hair strands, eyebrow,
eye"; the frames show no arm, hand, or blade. If your capture does not show
identifiable arms, **say that plainly and report Stage 1 as not achieved** —
that is a useful result, not a failure to hide. **Never self-declare visual
acceptability; Will judges.** Put the frames in review/T296/ with a README
that says exactly what each one shows.

## STOP
If the pass cannot be added without restructuring the frame loop or breaking
the world render, stop and report the conflict rather than forcing it.

## Verification
Full ctest (`-j6`); visual gate PASS with goldens untouched; Review; evidence
review/T296/. Commit: `T296: first-person viewmodel pass, stage 1`.

## Review

Evidence: `review/T296/` (see its README.md for what each file is).

### Step 0 — the plan's shape, verified against the renderer, and taken

The plan's four steps were checked against the actual code before anything
was written. **The shape holds and was taken unchanged**, with one addition
the plan left open ("whether a second camera or an explicit transform is the
cleaner hook") resolved in favour of the explicit transform:

1. **Explicit transform + manual draw, NOT a second camera.** Three findings
   settled it. (a) `EQAnimatedMeshSceneNode::render()`
   (`animated_mesh_scene_node.cpp:767`) is already exactly a
   `setTransform(ETS_WORLD) / setMaterial / drawMeshBuffer` loop over the
   node's own `instanceMesh_`, so the pass reuses a known-good draw path
   rather than inventing one. (b) The same idiom is already load-bearing
   elsewhere in this renderer — `renderer_zone_draw.cpp`'s front-to-back
   manual zone draw. (c) A second `ICameraSceneNode` would need either a
   second `smgr_->drawAll()` (a full re-traversal of every scene node — the
   frame's dominant cost, `frameTimings_.sceneDrawAll`) or a mid-frame
   active-camera swap; both are far more invasive than three
   `setTransform()` calls. No frame-loop restructuring was needed, so the
   plan's STOP condition was never approached.
2. **The mid-frame depth clear is a raw `glClear(GL_DEPTH_BUFFER_BIT)`.**
   `COpenGLES2Driver` extends `CNullDriver` and does **not** implement
   `clearZBuffer()` (grep: the only hits in `COpenGLES2Driver.{h,cpp}` are
   the `bool clearZBuffer` parameter of `beginScene`), so the
   `IVideoDriver::clearZBuffer()` route would have been a silent no-op —
   the pass would have been clipped by world geometry with no error. Raw GL
   for exactly this kind of state work is the established pattern here
   (`renderer_zone_draw.cpp`'s stencil portal code,
   `renderer_overlays.cpp`). `glDepthMask(GL_TRUE)` is set first: `glClear`
   ignores the depth buffer when depth writes are masked off.
3. **Hook point: after weather, before the debug overlays and 2D UI**
   (`renderer_frame.cpp`, after POST-DRAW checkpoint 7). That is after ALL
   3D world content — zone, entities, particles, weather — so nothing can
   occlude the arms, and before the overlays/UI so those still draw on top.
4. **Deviation from the plan's step 2, stated:** the pass does **not** clear
   depth again on exit. Clearing on exit would destroy the WORLD's depth for
   the debug overlays that follow — a strictly larger behaviour change than
   leaving a viewmodel-shaped region of near depth behind, which nothing in
   a normal frame reads (what follows is opt-in debug overlays and 2D UI).

### The frame algebra — derived, then confirmed by measurement

The one thing that could silently ruin this unit is putting the arms and the
weapon in different spaces: the arms would float and the sword would hang off
somewhere else. The arm mesh and the equipment nodes genuinely live in two
different frames, so the relation was **derived from the code and then
measured**, not assumed.

- Derived from `applyBoneTransformsToMeshImpl()`
  (`animated_mesh_scene_node.cpp:210-223`): for the player node the vertices
  get an extra rotation of `(nodeRotationY + 90)` degrees **about the EQ Z
  axis**, applied before the `EQ(x,y,z) -> Irrlicht(x,z,y)` swap. Working
  that through the swap, in Irrlicht terms it is a Y rotation of
  `-(h + 90)`. So `baked = RotY(-(h + 90)) * nodeLocal`.
- Equipment nodes are **children of the player node**
  (`entity_equipment.cpp:79,111` — `addMeshSceneNode(mesh, parentNode)`) and
  carry no such baking, so they live in `nodeLocal` directly, via their
  relative transform.
- The character faces **+X in nodeLocal**, read off the real skeleton: the
  `humbi_r` / `humbi_l` shoulder bones land at Irrlicht Z = -0.60 / +0.62,
  so the lateral axis is Z (right = -Z) and forward is +X.
- The viewmodel's own view space has the eye at the origin looking down +Z,
  so nodeLocal is rotated by **-90 degrees** about Y. Composing:
  `RotY(-90) * RotY(h + 90) == RotY(h)`, i.e. the arms' transform is a plain
  `RotY(playerHeadingY)` on the baked vertices, and the equipment's is
  `RotY(-90) * relativeTransform`.

`BakedFrameIsNodeLocalRotatedByHeadingPlus90` pins this at four headings
(0/37/137/250) by asserting that after `RotY(h + 90)` the `R_POINT` hand bone
lands **inside** the arm geometry (< 0.20 model units from the nearest arm
vertex). Measured: 0.048 at every heading. Before the derivation, a naive
scan over candidate yaws had produced *inconsistent* winners across headings
(-217 looked best at h=37, 0 looked best at h=250) — a good reminder that a
nearest-vertex fit is a weak objective and that reading the transform code is
worth more than curve-fitting it.

### Framing: the arm box, and two measured corrections

- **The mesh's own bounding box is the WHOLE BODY, not the arms.** T74's
  filter rewrites index buffers and never touches vertex arrays, so
  `instanceMesh_->getBoundingBox()` still spans the full model. Measured on
  Human male: full box Y = [-3.05, 3.58], arm (indexed) box Y = [0.14, 2.56]
  — hip to shoulder, which is what arms should be. `measureIndexedBounds()`
  walks the index buffers instead, and
  `IndexedBoundsMeasureArmsNotWholeBody` asserts the arm box is under 75% of
  the model's height so this can never silently regress to framing a torso
  that is not drawn.
- **Correction 1 (measured):** fitting the box's *centre* to the frame let
  the arms spill off every edge — the box's NEAR face is what binds, not its
  centre. Measured NDC X reached ±1.73 (i.e. 73% off-screen on each side).
  Fixed by solving at the near face (`distance += extent.Z`); NDC X is now
  ±0.81.
- **Correction 2 (measured):** with the vertical bias expressed as a
  fraction of the visible half-height, the arms then dropped off the BOTTOM
  (NDC Y min -1.16). The bias is now a fraction of the **slack left over
  after the fit**, measured on the near face and clamped to [-1, 1], so no
  tuning value can push the arms out of frame. Final: NDC X = [-0.81, 0.81],
  Y = [-0.97, 0.73] — the arms sit low in the frame, whole.

### The T76 straddle is gone by construction

`PlacedArmGeometryIsEntirelyInFrontOfTheNearPlane` measures the placed arm
box across **6 races x 4 animations** (idle, three combat/damage anims) and
asserts every vertex is in front of the pass's near plane with the clearance
margin. Every case lands with min Z between 1.06 and 7.28 against a 0.05
near plane — compare T76's measured in-world spans of Z = [-1.06, 1.11]
(idle) and [-1.02, 2.18] (1H slash), where negative Z is *behind the eye*.
Full table: `review/T296/T296_headless_geometry_test_output.txt`.

### Keeping the arms out of the world render without freezing them

The trap here: Irrlicht's `ISceneNode::OnAnimate()` **skips invisible nodes
entirely**, so `setVisible(false)` on the player node would have kept it out
of `drawAll()` at the cost of freezing the skeleton at bind pose — still
arms, and a very easy thing to ship without noticing. Instead
`EQAnimatedMeshSceneNode::setDeferredToViewmodelPass(true)` makes
`OnRegisterSceneNode()` skip `registerNodeForRendering(this)` while the node
stays **visible**, so Irrlicht keeps animating it.
`DeferralKeepsTheNodeVisibleSoItKeepsAnimating` asserts both the visibility
invariant and that the arm geometry actually moves across 10 animation steps
while deferred. The equipment children ARE hidden (they would otherwise
register themselves and draw in world), which is safe because
`updateEquipmentTransforms()` writes their relative transform every frame
regardless of visibility, and the pass reads
`getRelativeTransformation()`, not the absolute transform.

### Territory

`entity_renderer.cpp` was touched only inside `setPlayerFirstPersonMode()`
(+18 lines net); `getPlayerViewmodelParts()` is deliberately **defined in
`viewmodel_renderer.cpp`** rather than `entity_renderer.cpp`, to keep the
viewmodel code in one place and off an already-oversized TU. T295 (slot2)
also touches `entity_renderer.h`/`.cpp` to delete
`updateFirstPersonWeapons()` — a different function and a different part of
the header, but flagged here since the orchestrator serializes the merges.
No camera code was touched (T295's territory); no ui_renderer/paperdoll
(T294's).

### Verification

All six re-run AFTER the last code change (the depth-clear guard fix and the
equipment guard), not carried over from an earlier state.

1. `cmake --build build -j24` — clean, EXIT=0.
2. `ctest --test-dir build -j6` — **3215/3216**, sole failure the documented
   ALSA host waiver `AudioManagerTest.PreloadCommonSounds`. Suite grew
   3208 -> 3216 (+8: this unit's 7 new cases; the 8th is the sub-case
   accounting of the new binary's discovery).
   `review/T296/T296_ctest_full.log`.
3. `scripts/build-gles2-x86.sh` — clean, EXIT=0.
4. **Visual gate PASS, goldens untouched** (`git status` shows no golden
   modified). `review/T296/T296_visual_gate_output.txt`.
5. `scripts/build-arm-noble.sh` — clean cross-compile, EXIT=0, binary
   rebuilt (verified by mtime, not just exit code).
6. `check_file_sizes.sh fable` — **no new FAIL from this unit.**
   `src/client/graphics/entity_renderer.cpp` FAILs at 3358 lines; it was
   already 3340 (and already FAILing) on the `fable` tip this branch was cut
   from, net +18 here. Pre-existing WARNs grew marginally:
   `irrlicht_renderer.h` 2352 -> 2375, `renderer_frame.cpp` 1677 -> 1683.
   `entity_equipment.cpp` 269 -> 285, still ok. All substantial new code
   went into new TUs (`viewmodel_renderer.cpp`, `viewmodel_transform.h`,
   `viewmodel_tuning.h`, `tests/test_viewmodel_pass.cpp`).
   **Note for the merge:** `fable` moved during this unit (T295 and T297
   merged), so the report now also lists files this branch never touched —
   `irrlicht_renderer.cpp` FAILs at 3066 on the current `fable` tip, and
   `renderer_player_physics.cpp` / `ui_renderer.*` / `test_ui_atlas_pixels.cpp`
   come from T294/T295. None of those appear in this branch's
   `git status`. `review/T296/T296_check_file_sizes_output.txt`.

### Merge note — territory overlap with T295, which merged mid-unit

This branch was cut from the pre-T295 `fable`. T295 has since merged and it
deleted `EntityRenderer::updateFirstPersonWeapons()` from
`entity_renderer.cpp` + `entity_renderer.h` — a different function and a
different part of the header than this unit's changes
(`setPlayerFirstPersonMode()`'s body, and a new `ViewmodelParts` struct +
`getPlayerViewmodelParts()` declaration), so the two should compose, but the
orchestrator should expect to resolve both files. This unit touched no
camera code at all (T295's subject) and no ui_renderer/paperdoll (T294's).

### Live capture — what the frames actually show

Evidence and a per-frame description: `review/T296/README.md`. eqemu owned
under the noclobber lock (taken only after T294 and T295 had each released
it — ~40 minutes of polling, never overwritten), qeynos2, daylight forced
with `/say #set time 11 0`, 800x600, GLES2 surfaceless, `--eqe1-path` on
every run. A throwaway probe run confirmed the client survives the time
command and several seconds of rendering before any capture sequence was
built on it (protocol requirement).

**Arms ARE visibly on screen and identifiable, and so is the weapon.** In
`T296_2_firstperson_idle_arms_and_weapon_800x600.png` and the two
auto-attack frames: two skin-toned forearms, one per side, each with a
visible elbow, a tapering forearm and a closed fist; the right fist is
closed around a shaft with a grey engraved head above it and a wrapped grip
and pale pommel below. **I am not declaring this acceptable — Will judges.**
What I am asserting is only that the shapes are identifiable as arms and a
weapon, which is Stage 1's stated bar, and the README describes them without
flattering them.

**Stated honestly, three things the captures do NOT show:**
1. **No visible swing.** The arm pose during 20 seconds of confirmed
   auto-attack (`Auto-attack enabled`/`disabled` in the run log, dummy
   targeted, health bar on screen) is very close to the idle pose in the
   stills. Whether the attack animation is reaching the arms is not answered
   by these frames; the video is included so it can be judged. This is a
   Stage-2 question and is flagged, not hidden.
2. The upper arms run off the top of the drawn shapes — the fit frames the
   arm bounding box, and at this size the shoulders are past the visible
   region. Framing polish is Stage 2.
3. Capture fps (measured 2.92 from frame mtimes) is an artifact of the
   per-frame glReadPixels+PNG dump, not a performance measurement; the
   client's own counter reads 30-31 in non-dumping runs.

### Two live findings, one of them a defect in this unit's own first cut

**1. The depth clear was compiled out (found by capture, fixed).** The first
live frames showed the arms present but sliced off by the terrain — the
lower two thirds of each arm was occluded by the ground. Cause: the raw GL
include and the `glClear(GL_DEPTH_BUFFER_BIT)` were guarded on
`EQT_HAS_DRM`, copying `renderer_zone_draw.cpp`'s preamble; this
configuration is `-DEQT_DRM=OFF`, so **the depth clear silently compiled to
nothing** and the viewmodel was depth-tested against the world. The correct
guard here is `EQT_HAS_GLES2` (with `EQT_HAS_DRM` as the desktop-GL
fallback), which is what `zone_shader.cpp` uses. Fixed, and the fallback
branch is now `#error` rather than a silent no-op, so this exact
compile-out cannot recur. **This is the reason a live capture was worth
doing: every headless test in this unit passed with the depth clear absent,
because none of them rasterize anything.**

**2. An unexplained dark teal wedge — hypothesis raised, then REFUTED by
its own control.** A dark teal obelisk sits near the bottom centre of the
first-person frames. I hypothesised it was the equipped weapon rendered a
second time in the world by an equipment node attached AFTER first person
was entered (`attachEquipment()` creates nodes visible by default and
nothing re-applied first-person visibility), and added a guard for that.
**The control run then refuted the hypothesis**: with the primary slot
emptied, the wedge is still there
(`T296_5_firstperson_no_weapon_control_800x600.png`), and it shifts across
the frame as the camera yaws while the arms stay fixed. **It is world
geometry in qeynos2, not a viewmodel defect.** The guard is nonetheless
**kept**, and this is stated rather than quietly dropped: the hole it closes
is real by inspection (`entity_renderer.cpp:1376` calls `attachEquipment()`
from the deferred entity-load path, which can run while first person is
already active, and nothing else would hide those nodes) — but there is no
evidence it fired in this session, and it should not be described as a fixed
bug.

### Deviations from the plan

1. **No exit depth clear** — see step 0 item 4 above.
2. **The "pixel test that the viewmodel region is non-background when first
   person is on and background when it is off" is NOT a framebuffer test.**
   A real pixel assertion needs a live server + a loaded zone + a player
   entity, none of which exist in ctest. What ships instead is the honest
   headless equivalent: the placed arm box is projected through the pass's
   own matrices and asserted to cover >0.5 of the NDC frame area while
   fitting inside it (`PlacedArmGeometryProjectsIntoTheFrame`), and the pass
   is asserted to emit one draw call per non-empty arm mesh buffer with
   first person on and **zero** with it off
   (`DrawListCoversEveryArmMeshBuffer`, `DrawListIsEmptyWithoutValidPlacement`).
   This is a proxy, and it is labelled as one; the framebuffer question is
   answered by the live capture, not by ctest. **The depth-clear bug above
   is the cost of that gap, stated plainly: no headless test in this unit
   could have caught it, because none of them rasterize.**
3. **`ViewmodelTuning` lives in its own header** (`viewmodel_tuning.h`)
   rather than in `viewmodel_transform.h`, so `irrlicht_renderer.h` can hold
   one by value without pulling in `entity_renderer.h`, which includes
   `irrlicht_renderer.h` back.
4. **A defensive guard was added on a hypothesis its own control then
   refuted** (the equipment-attach guard, above). Kept, and labelled as
   unproven rather than reported as a fix.
