# T301 — First person: put the eye where the eyes are

**Status: WRITTEN, NOT DISPATCHED.** Will approved the shaping (2026-08-27) and
asked for the plan to be written first.

## What T300 settled, and what it got wrong

SETTLED (do not re-litigate):
- Official first person draws the **ordinary player model**, camera at the eyes,
  and **clips nothing special** — `near < 1 unit`, world and NPC geometry render
  at point-blank, ground renders at max down-pitch (T300 Leg A).
- T296's viewmodel pass is the wrong mechanism and is **deleted** (T300 Leg B).
- Official hFOV **62.71 +/- 0.25 stat, +/- 0.9 syst**, re-derived twice by
  different methods. fable had never called `setFOV()` at all — it ran at
  Irrlicht's default 72 deg VERTICAL (~88 deg horizontal). Fixed.
- fable's first person now clips identically to third (`near=1.000` measured in
  both modes in one run) — T300 Leg C Q5, the unit's one PASS.
- Official max down-pitch ~65 deg, bracket 55-75 (T300 Leg A).

**REFUTED — the clamp derivation `90 - vFOV/2` is backwards.** It is by
construction the angle at which the frustum's LOWER EDGE REACHES THE NADIR, so it
is the condition that **admits** the body's cone of directions, not one that
excludes it. At that clamp the frustum spans 40.8-90.0 deg below the horizon and
the body occupies exactly that cone (Human male: nose 51.1 deg, feet 90 deg).
The orchestrator inferred it from its agreement with Leg A's 55-75 bracket
without checking which side of the inequality it fell on. **A future unit must
not reintroduce it.** Its claimed race-invariance fails too: the required angle
is ~27 deg for a Human male and ~61 deg for a Troll, because the angle from a
crown-mounted eye to the nearest geometry is a property of mesh PROPORTIONS, not
size. Evidence: `review/T300/T300_C_12_humanmale_maxpitch_own_face_RAW_1024x768.png`.

**Also refuted:** T300 Leg B's headless per-race entry-angle table. Live vs
headless is 26.5 vs 39.5 (Human male) and 60.6 vs 0 (Troll) — **the error changes
sign between races**, so the assumption behind it (that the baked player mesh's
+Z is camera-forward at heading 0) is broken in some way that is not an offset.
Do not reuse those numbers. Re-derive live or not at all.

## The hypothesis this unit tests

**fable places the eye at the STATIC bounding-box crown** —
`eyeAboveOrigin = bbox.MaxEdge.Y = 3.6527`, on top of the skull rather than
inside it. Two consequences, both observed live:
- The face sits forward of and below that point (51 deg depression) and is inside
  any plausible frustum — hence `T300_C_12`.
- The run cycle lifts the animated head ABOVE the static crown, so the head bobs
  into frame at LEVEL pitch while running (`T300_C_20/21`). **No clamp value can
  fix that**; it is an eye-placement defect, not a pitch defect.

**Predicted mechanism when the eye is correct:** with the eye inside the head at
true eye level, the rest of the body hangs at near-zero horizontal offset and so
occupies only directions close to the nadir, which a ~65 deg clamp excludes; the
head itself is then within 1 unit and is swallowed by the ORDINARY 1.0 near
plane. That reproduces "clips nothing special" exactly. **This is a hypothesis.
The live gate decides it.**

Note the frame trap that produced the error: 6.75 (official) is measured to an
**eye**; 3.6527 (fable) is a **bbox crown**. T300 Leg B's "one eye, two frames"
reconciliation was right in arithmetic and wrong in kind.

## Prerequisite / decision for Will

T300's branches are unmerged. Recommendation: **merge T300 anyway.** The viewmodel
deletion, the camera formula, the FOV fix and the clipping parity are all correct
and independently valuable, and the surviving clamp (65.4 deg) is close to the
official ~65 deg and does no harm. The body-visible defect stays open and becomes
this unit. Merge order: `task/T300-legA`, `task/T300-legB`, `task/T300-legC`.
Merger's traps, from Leg C: rebuild `build/` after checkout or two
`FirstPersonCameraSingleFormula` source-parsing tests assert Leg-B-deleted symbols
and fail; and `rm build/bin/test_viewmodel_pass` (cmake never prunes bin/) or the
orphan check fails.

---

## Leg A (slot1) — MEASURE THE OFFICIAL EYE, RELATIVE TO THE MODEL. Owns eqemu.

The number this unit needs is not "eye height above server z" (T298's 6.75, whose
z reference is not pinned down) but **where the eye sits inside the model**:
its height as a fraction of the model's own standing height, and its forward
offset relative to the head. Measure it, do not derive it from a bbox.

Method is yours; the two-client protocol is the obvious lever
(`refactor/TESTING_CHARTER.md`, `two-client-live-testing` memory) — an observing
second client sees Testchar's model while the tested client reports what its own
camera sees, so the eye can be located against the model rather than against the
server's z. Cross-check against a fixed-height world landmark.

Also required:
1. **Empirically measure the official max down-pitch** rather than inheriting the
   55-75 bracket. It is one number and it now matters.
2. **At that clamp, does ANY of the official player's own body appear** — a
   sliver at the extreme bottom edge, anything? The predicted mechanism says
   "almost, but not quite". Confirming a near-miss would be strong evidence;
   confirming a clean miss by a wide margin would mean something else is also at
   work. Step video frame by frame; stills are barred (`dont-sample-transients-with-stills`).
3. **Does the official head bob into frame while running?** fable's does. If the
   official's does not, say what the difference is — a head-bone-anchored eye, or
   an eye that does not follow the animation at all.

Deliver the eye position as a model-relative spec fable can implement, with error
bars. `review/T301/` prefixed `T301_A_`, plus `refactor/T301_leg_a_official_eye.md`.

## Leg B (slot2) — ANATOMICAL, POSE-STABLE EYE PLACEMENT IN FABLE. No server.

1. **Find a real head anchor.** `getBoneIndexByName("HEAD")` returns **-1** on
   Testchar's model (T295, `[FP-EYE] source=BBOX_FALLBACK`) — that lever has never
   fired live, so do not simply re-enable it. Establish what the skeleton actually
   names its bones across several races, and pick an anchor that exists. If no
   bone anchor is reliable, derive the eye from head GEOMETRY (the head submesh or
   the vertex cluster near the crown) — but derive it per race from the mesh, never
   from a hand-written table, and never from the whole-body bbox.
2. **Decide, and justify in the review, whether the eye follows the animation.**
   Leg A Q3 answers what the official does. A pose-following eye tracks a bobbing
   head (possibly correct, possibly nauseating); a rest-pose eye is stable but
   lets the head bob through it — which is exactly today's defect. This is the
   central design decision of the unit; do not make it silently.
3. Apply Leg A's model-relative spec. Keep the clamp as a separate, named,
   measured constant — **not** re-derived from FOV.
4. Tests: pin that the eye is inside the head (not on the crown), that it is
   derived per race from the mesh, and that at max down-pitch no body geometry
   enters the frustum for several races **including Troll and Iksar**, the harshest
   cases live. **Say plainly that headless tests do not rasterize** and cannot
   settle any of it.

`refactor/T301_leg_b_eye_placement.md`.

## Leg C (slot3) — WHY IS THE WEAPON NEVER VISIBLE? No server initially.

T300 Leg C: 285 frames, 7 auto-attack damage exchanges with a two-handed Halberd,
**zero weapon geometry**, and 6 kicks with no foot — while the SAME script at the
SAME capture rate in `/camera follow` shows both plainly (`T300_C_33`). So the
animation plays and the equipment draws; something about first person loses them.
This is not a camera-tuning question and must not be assumed to fall out of the
eye fix.

Candidates to test, not to assume: equipment still hidden or detached on the
first-person path (T296 added a late-attach hide — verify its removal actually
reaches the live path); the swing arc genuinely staying outside a frustum whose
eye is wrong (in which case the eye fix does resolve it — prove it, do not assert
it); attachment-point or bone-frame differences between the two camera modes; the
weapon node being culled by a per-node test that the body escapes.

Deliver a diagnosis with file:line evidence and a fix if the cause is in fable's
control. `refactor/T301_leg_c_weapon_visibility.md`.

## Leg D (small, any free slot) — A UI-HIDE COMMAND. No server.

fable has **no F10 equivalent**; the chat box and hotbar are opaque in every
capture, and T300 Leg C could not rule out a weapon sliver hidden beneath them.
Add a UI-hide toggle. It is small, it unblocks every future visual gate, and the
official client's having one is why the oracle legs could rule out what fable's
could not. Pin it with a test that a user-reachable path toggles it — per the
pin-every-fix rule; several features in this codebase have shipped with no caller.

## Leg E — THE LIVE GATE. Dispatched after A-D.

Re-run T300 Leg C's six questions against the new code, same standard: **video,
stepped frame by frame; stills are not acceptable evidence.** Add: does the head
bob into frame while running at level pitch? Compare frame for frame against
`review/T299/` and `review/T300/`. **Will judges the look; no leg may
self-declare it acceptable.** A failed gate reported accurately is worth more
than a passed one asserted — T300 Leg C is the model for how to report one.
