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
