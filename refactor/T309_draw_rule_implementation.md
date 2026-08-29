# T309 — Implement the measured draw rule; resolve the legB/legCD conflict

## The rule, measured (T307, `refactor/T307_official_draw_rule.md`)

**In first person the official Titanium client draws the player's own model —
the WHOLE model, with its equipped weapon — for the duration of an ATTACK
animation, and at no other time.** The trigger is the animation actually
PLAYING, not the `/attack on` state: an out-of-range auto-attack that produces
no swing draws nothing. It is a **per-frame visibility predicate on the local
player's model**. Not a near plane (refuted T300 Leg A), not a viewmodel pass
(refuted T299), not a pitch clamp (refuted T300 Leg C + T301 Leg B).

Discriminators, so nobody re-opens them: eight emotes at the same camera pose
gave **2250 consecutive frames at 0.0000% max per-frame pixel difference** while
a third-person control shows those emotes plainly animating — so "any non-idle
animation" is dead. During a KICK the two-handed halberd is drawn in full — so
"only the animating limb" is dead. Duty cycle at the clamp is 22.5%; swings ran
1.13 s mean on the Halberd's 5.0 s timer, kicks 0.72 s with onset +0.27-0.50 s.

## Leg 1 (slot3) — IMPLEMENT IT. No server.

Land it at the seam T301 Leg B deliberately left open (`applyFirstPersonPitchClamp`
and the first-person hiding seam it kept). The clamp stays, but as a **control-feel
limit**, not a hiding mechanism — that is already documented; do not re-derive it
and do NOT reintroduce `90 - vFOV/2`.

**⚠ THE TRAP THAT WILL BITE: hide by WITHHOLDING FROM THE RENDER LIST, NOT by
`setVisible(false)`.** Irrlicht skips `OnAnimate` on invisible nodes (T296 found
this the hard way and it is why the player node was withheld rather than hidden).
Under this rule the model must keep animating **while hidden**, or the attack
animation will be at the wrong phase — or frozen — at the instant it becomes
visible. A naive `setVisible` toggle will look correct in every headless test and
produce a frozen or snapping weapon live.

Also: the predicate is per-frame and must be driven by **whether an attack
animation is currently playing**, not by `/attack` state, not by a timer the
client invents. Find what fable actually has that says "this animation is
playing, and it is an attack" and use it; if that signal does not exist, say so
and propose the smallest honest way to get it rather than inventing a parallel
state machine.

## Leg 1b — THE legB/legCD CONFLICT, AND WHY IT SHOULD NOW DISSOLVE

The merge (`dad8a308`) left `FirstPersonWeaponAttachmentTest.HeldWeaponIsHeading
InvariantInTheCameraFrame` FAILING. Leg C's own property — heading invariance —
passes to 5 decimals at all five headings. What fails is its second assertion,
`EXPECT_GT(ref.forward, 0.0f)`, actual **-0.1527**: Leg B pushes the eye forward
along the look ray (`kEyeForwardOffsetUnits = 1.0`, Human male net **+0.83**), so
the held weapon at the c01 attack frame now sits ~0.15 BEHIND the eye plane.

**The reason Leg B chose the FAR end of the range is now obsolete.** It picked
1.0 "to keep the player's own face out of frame" — but the face is not kept out
of frame by geometry at all; it is kept out by the DRAW RULE. With the predicate
implemented, the constraint that justified the extreme value is gone.

So: **re-evaluate the forward offset against the measurement rather than against
the face**. T301 Leg A measured **+0.9 ± 0.25**; T298 measured **0.0 ± 0.4**;
they disagree at ~1.9 sigma and the rotation-parallax test that would settle it
was never run. Pick with reasons stated, and note the live constraint that now
matters instead: in the official client the weapon IS visible during a swing, so
any offset that puts fable's weapon behind the eye at swing frames is wrong on
its face. Whether Leg C's assertion should be relaxed, re-framed, or kept as the
canary is part of this decision — do not simply delete it to get green.

## Leg 2 (slot1, AFTER Leg 1 lands) — THE LIVE GATE. Owns eqemu.

Re-run T300 Leg C's six questions plus the head-bob case against the new code,
and add the rule's own predictions:
- body **invisible at rest at every pitch INCLUDING the clamp**, and while running;
- body + weapon **visible for the duration of a swing**, absent between swings;
- **emotes draw nothing** (the discriminator — if emotes draw the body, the
  predicate is wired to the wrong signal);
- an out-of-range auto-attack that produces no swing draws nothing.

**VIDEO, stepped frame by frame. Stills are barred in this arc** — a still-sampled
null was wrong once already and the duty cycle is 22.5%. fable now HAS a UI-hide
command (`/hideui`, aliases `/toggleui`, `/ui`, or F11) from T301 Leg D — use it;
T300's gate could not rule out geometry under the opaque chat panel.

Compare frame for frame against `review/T307/` and `review/T299/`. **Will judges
the look; no leg may self-declare it acceptable.** A failed gate reported
accurately is worth more than a passed one asserted.

Oracle traps from T307 §6 if the official client is re-run: `x11grab -i :99+0+30`
silently means y=0 (use `:99+0,30`), and F10 hides the official UI but does NOT
disable it — clicks and typing still register mid-capture.

---

# Review — Leg 1 + Leg 1b (slot3, branch `task/T309`, base `af050571`)

## 1. How the predicate is driven, and what signal it reads

fable already had the signal; no parallel state machine was invented.

* **The code**: `EQAnimatedMeshSceneNode::getCurrentAnimation()` — the animation
  code the node's own per-instance `SkeletalAnimator` is running.
* **The edge**: `getAnimator().getState() == AnimationState::Playing`.

The rule is the pure pair in the new
`include/client/graphics/first_person_body_draw.h`:
`shouldDrawOwnBodyInFirstPerson(animCode, playing)` =
`playing && isAttackAnimationCode(animCode)`, with `isAttackAnimationCode`
accepting **c01..c11** (the combat family — kick, the five weapon swings,
throwing, bash, hand-to-hand, archery, swim attack, round kick) and **t07..t09**
(the monk special attacks, which `eq.cpp` dispatches identically to c01/c11;
included by that family argument and **not** by measurement, and the header says
so). Emotes, locomotion, poses, idles, **casting (t04-t06) and damage (d01-d05)**
are not attacks — the last two because T307 explicitly did not shoot them, and an
unmeasured case is left drawing nothing.

Driven from `EntityRenderer::updateFirstPersonBodyDrawRule()` (new TU
`src/client/graphics/entity_first_person_draw.cpp`), called once per frame at
the end of `updateMainThreadEntityState()` — which `renderer_frame.cpp` runs
before `smgr_->drawAll()`, and `drawAll()` is where `OnRegisterSceneNode()` reads
the flag. Also called directly from `setPlayerFirstPersonMode()` so entering
first person mid-swing is not a frame late. It is a no-op outside first person,
and it restores the node on the way out.

**Why the STATE and not the code — this is not decoration.**
`SkeletalAnimator::playAnimation()` sets `holdOnComplete_ = !looping_`, and
combat animations are played non-looping. A finished swing therefore **freezes
on its last frame and KEEPS the code `"c01"`**, merely leaving `Playing`;
production only gets back to idle because `renderer_player_physics` asks for
`"p01"` every frame once `isPlayingThrough()` goes false. A predicate reading
the code alone would have left the body **permanently drawn after the first
swing** — for exactly the 77.5% of the time T307 says it must be invisible.
`BodyIsWithheldAtRestAndDrawnForTheDurationOfAnAttack` asserts this hold
behaviour explicitly so the reasoning cannot silently rot.

Neither `/attack` state nor any timer of ours is involved, so an out-of-range
auto-attack that produces no swing draws nothing, as measured.

## 2. How the model keeps animating while hidden

`EQAnimatedMeshSceneNode::setWithheldFromRenderList(bool)`. In
`OnRegisterSceneNode()` a withheld node returns **before**
`registerNodeForRendering(this)` and before `ISceneNode::OnRegisterSceneNode()`,
so the node and **its children — i.e. the equipped weapon** — are skipped
together: the rule draws the whole model with its weapon or none of it.
`IsVisible` is never touched.

`OnAnimate()` is driven from `CSceneManager::drawAll()`'s separate animation
pass, which runs *before* registration and is independent of it, so a withheld
node still advances its animator, still updates its absolute transform, and
still recurses into its children.

Measured, not asserted: `TheModelKeepsAnimatingWhileHiddenAndSetVisibleWouldNot`
drives Irrlicht's real path (`getRootSceneNode()->OnAnimate(t)`) and prints

```
  WITHHELD: animator frame 0 -> 4, child animations 1 -> 9
  setVisible(false): child animations 9 -> 9  (the attached weapon stops being animated)
```

**A correction to the folklore, recorded because the next reader will hit it.**
"Irrlicht skips `OnAnimate` on invisible nodes" is not quite what happens to
*this* class. A parent calls its children's `OnAnimate()` **unconditionally**;
the `if (IsVisible)` gate is inside `ISceneNode::OnAnimate`'s own body. Since
`EQAnimatedMeshSceneNode::OnAnimate()` advances the animator *before*
delegating to the base, the skeleton itself would keep ticking under
`setVisible(false)`. What actually freezes is `updateAbsolutePosition()` and
**the entire child recursion — which is where the equipped weapon lives**: it
would hang at whatever pose it held when the body was hidden and snap on
reveal, at a 22.5% duty cycle. That is still fatal, and it also depends on an
override ordering nobody should have to preserve. The withhold has neither
question. The test asserts both halves so this stays honest.

**Proved by reverting** (`git apply`-style edit, no `git stash`):

| experiment | result |
|---|---|
| `withhold = false` (rule disabled) | **all 3** integration tests FAIL |
| `setVisible(!withhold)` instead of the withhold (the naive impl the contract warns about) | FAILS, and is caught by the `EXPECT_TRUE(node->isVisible())` assertion specifically — it is ordered first so a naive implementation trips *that* line and not an incidental precondition |

## 3. Leg 1b — the forward offset, and Leg C's assertion

### `kEyeForwardOffsetUnits`: **1.0 -> 0.0**

The two measurements still disagree (T301 Leg A **+0.9 ± 0.25**, T298
**0.00 ± 0.4**, ~1.9 sigma, rotation-parallax never run) and this unit did not
run a new one. What changed is the **tiebreaker**. Leg B's reason for taking the
far end was "keep the player's own face out of frame"; the face is not kept out
of frame by geometry at all, it is kept out by the draw rule, so that reason is
gone. Two constraints now point the other way, and both come from T307's
pixels rather than from an argument:

1. T307 filmed, **at the maximum down-pitch clamp**, the player's own body
   filling the upper frame during a swing, his own arm entering bottom-right and
   his own halberd across the view. An eye shoved 0.83-1.0 units out in front of
   the chest cannot see that — the torso would be behind the camera.
2. Measured headlessly here: the held weapon's grip at the c01 attack frame sits
   **+0.678** forward of the model's vertical axis. At 1.0 the Human male eye
   lands at +0.83 and the grip falls **0.153 behind** the eye plane; at Leg A's
   central +0.9 it is 0.22 behind. **The official client draws the weapon during
   the swing**, so an offset that puts fable's weapon behind the eye at swing
   frames is wrong on its face.

0.0 is T298's measured value and sits ~2.3 sigma from Leg A's — not comfortable,
and the header says so — but it is the only value in Leg A's own 0..1.0 range
consistent with the geometry the official client was filmed producing. The eye
is then exactly the head cluster's **own anatomical position**: 0.17 behind the
axis for a Human male (inside the skull, where an eye belongs), and still
forward-of-axis for the races whose anatomy puts it there (Iksar +0.84, Froglok
+1.48). If a rotation-parallax measurement is ever run and lands near +0.9, both
constraints above must be re-checked at the same time.

### Leg C's `EXPECT_GT(ref.forward, 0.0f)`: **KEPT, and promoted to the canary**

Not deleted, not relaxed. It is now the **cheapest automated guard on the draw
rule's premise**: T307 measured that the official client draws the weapon for
the whole duration of a swing, so a fable eye placement that leaves the weapon
behind the camera at the attack frame is refuted by the official client's own
pixels. If it fails again, the eye has moved forward past the hand and the rule
will reveal an empty view mid-swing — fix the offset, do not relax the
assertion. The test now prints the margin so a future change can see its room:

```
  DRAW-RULE CANARY: weapon grip is 0.848 units in FRONT of the first-person eye
  at the c01 attack frame
```

`FirstPersonWeaponAttachmentTest.HeldWeaponIsHeadingInvariantInTheCameraFrame`
**passes** on this branch; the merge's only failing test is closed.

The sibling test `ForwardOffsetIsLegAsMeasuredValueAndIsPurelyForward` was
renamed `ForwardOffsetIsInRangeAndHasNoLateralComponent` — it asserted a
rationale that is now dead. It still pins the two things neither measurement
disputes: inside Leg A's 0..1.0 range (so T76's 0.3 *backward* standoff stays
excluded by sign), and zero lateral component.

## 4. Stale claims struck

T300's "the down-pitch clamp is what hides the body" survived in five places and
would have been the next reader's first belief. Every one now names the draw
rule and re-states the clamp as a **control-feel limit**:
`irrlicht_renderer.cpp` (the `applyFirstPersonPitchClamp` banner and the
projection comment), `first_person_camera.h` (file header + the in-formula
comment), `first_person_eye.h` (the clamp block, which said the draw rule was
"deliberately NOT implemented"), `animated_mesh_scene_node.h`/`.cpp`, and
`entity_renderer.cpp`'s `setPlayerFirstPersonMode()`.

## 5. Files

New:
- `include/client/graphics/first_person_body_draw.h` — the rule, its
  discriminators, its unmeasured cases, the code table, and the withhold-not-
  setVisible warning.
- `src/client/graphics/entity_first_person_draw.cpp` — the per-frame driver
  (split out because `entity_renderer.cpp` is already over the ceiling).
- `tests/test_first_person_body_draw_rule.cpp` — 8 tests.

Changed: `entity_renderer.h/.cpp`, `eq/animated_mesh_scene_node.h/.cpp`,
`first_person_camera.h`, `first_person_eye.h`, `irrlicht_renderer.cpp`,
`tests/CMakeLists.txt`, `CMakeLists.txt`,
`tests/test_first_person_camera_single_formula.cpp`,
`tests/test_first_person_weapon_attachment.cpp`.

## 6. Verification (by EXIT STATUS)

| step | result |
|---|---|
| `cmake --build build -j24` | **PASS** (exit 0) |
| `ctest --test-dir build -j6` | **PASS** — 3287/3288; sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| new tests fail without the fix | **YES** — both experiments above |
| `scripts/build-gles2-x86.sh` | **PASS** (exit 0) |
| `scripts/ci/visual_gate.sh` | **PASS**, goldens untouched (the rule is first-person only; the goldens are third-person renders and a test pins that third person is untouched) |
| `scripts/build-arm-noble.sh` | **PASS** (exit 0) |
| `scripts/ci/check_build_tree_orphans.py` | **PASS** — 204/204 |
| `check_file_sizes.sh fable` | **no new FAILs** — `entity_renderer.cpp` 3255 (3250 at `fable`, +5 comment lines; the new code went to a new TU) and `irrlicht_renderer.cpp` 3148 (3136 at `fable`, comment only) were both already over |

Suite grew 3283 -> 3288 (5 new predicate tests; the 3 graphics tests are
counted per-binary discovery).

## 7. What this leg does NOT prove

**Nothing here rasterizes.** These tests measure the render list, the animator
and the scene graph. They cannot say the swing LOOKS right, that the weapon
enters the frustum, or that the reveal is not jarring. **Leg 2's video gate is
the only thing that can judge it, and nothing in this leg claims otherwise.**

Specifically owed to Leg 2, and unmeasurable here:
- that the body is invisible at rest **at every pitch including the clamp**;
- that body + weapon are visible **for the duration** of a swing;
- that the animation phase on reveal is right (the whole point of the withhold);
- that emotes draw nothing **live** (the discriminator).

Also still unmeasured by anyone: bash, ranged, **casting**, being hit, jumping,
sit/stand. Those are left drawing nothing, and the header says so.
