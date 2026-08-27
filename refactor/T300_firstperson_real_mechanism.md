# T300 — First person: the REAL mechanism. Retire the viewmodel pass.

**Will approved the reversal (2026-08-27, this session).** The 2026-08-27
decision that chose a viewmodel pass over a near-plane change rested on T76's
"no camera lever can fix it", which was measured against a camera now known to be
wrong on two counts. T299 settled the mechanism in BOTH the classic client and
Titanium:

**First person is the ORDINARY player model, camera AT THE EYES, with a near clip
plane large enough to swallow the body.** Nothing renders at rest even with the
camera aimed straight down at feet 6.75 units below the eye — yet the same leg
reappears the instant a kick extends it. Geometry that reappears was CLIPPED,
not omitted. The weapon enters 22.9 deg off-axis and rotates about an off-screen
pivot, entering from different edges in different swings: world-space animation,
not a screen-locked viewmodel.

Read first: `refactor/T299_leg_a_video_forensics.md`,
`refactor/T299_leg_b_live_repro.md`, `review/T299/T299_B_README.md`, and the
memory-backed summary in
`/home/user/.claude/projects/-mnt-projects-fable/memory/eq-firstperson-draws-player-geometry.md`.

Target camera (T298 A3 stands; only its A2 null was wrong):
forward **0.0 +/- 0.4**, right **0.00 +/- 0.05**, up **6.75 +/- 0.20**,
pitch **3.65 deg down**, hFOV **62.8 deg**.

---

## Leg A (slot2) — MEASURE THE NEAR PLANE. Owns eqemu. Do this first; it is small.

Take the lock atomically; STOP if you cannot.
`bash -c 'set -o noclobber; echo "T300 $(date -I)" > /mnt/projects/fable/.eqemu-lock'`

Inherit T299 Leg B's working setup from `review/T299/T299_B_README.md` — do not
re-derive it (UI hidden with F10, DB-verified pri/sec, qeynos2, video capture off
the Xvfb display, and the crop-against-the-window trap: the client sits at
(-1,30), so a root-origin crop silently loses viewport rows).

**Deliverable: the near-plane distance, in EQ units from the eye, with error
bars.** Method: across one swing the weapon is truncated at a clean cut where it
crosses the plane. Track that cut over consecutive frames of a swing whose
world-space arc is known (a two-hander gives the longest lever and the best
signal-to-noise), and solve for the plane's distance. Cross-check against a
second attack type and against the kick's boot cut. State the method, its
assumptions and its error bars; if the geometry cannot support a number, say so
and give the tightest bracket the frames DO support — a bracket is a usable
result, a guess is not.

Also confirm fable's hFOV target: re-derive the official 62.8 deg independently
rather than inheriting T298's single figure.

Restore Testchar (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190), clean
shutdown, delete the lock. Deliver `refactor/T300_leg_a_near_plane.md` +
media in `review/T300/` prefixed `T300_A_`. Commit on `task/T300-legA`.

## Leg B (slot1) — THE CODE CHANGE. No server.

1. **Delete the viewmodel pass**: `viewmodel_renderer` and its own projection,
   its own near plane, its bounding-box fit (`fillFraction`), its cached arm box,
   and the arm bone filter added for it. Remove, do not deprecate. Strike EVERY
   mention — trackers, docs, tests, `refactor/deferred.md`'s FIRST PERSON
   sections — per the pin-every-fix standing rule.
2. **Stop withholding the player node from the render list** in first person
   (`entity_renderer.cpp` ~2807) and stop hiding the equipment (~2832-2836).
   The model and its equipment draw normally; the near plane does the hiding.
   Keep in mind WHY it was withheld rather than hidden: Irrlicht skips
   `OnAnimate` on invisible nodes, so a naive re-hide ships frozen geometry.
3. **`playerZ_` FIRST, before any eye-height edit.** It is named feet and holds
   model centre (body spans -3.05..+3.65; `entity_renderer.cpp:609` says server Z
   is model centre). Fix the naming and the reference frame explicitly, and state
   in the review whether fable's effective eye height (3.65 above centre = ~6.70
   above feet) already agrees with the official 6.75 above the server z. Do NOT
   "correct" 3.65 to 6.75 without settling which frame each is in — that is a
   3.05-unit regression waiting to happen.
4. **Pitch 12 deg -> 3.65 deg down.** Remove the 0.3 backward standoff (T76
   introduced it to clear in-world arms that no longer exist as a separate pass;
   the official measurement is 0.0 +/- 0.4). Check fable's hFOV against 62.8 deg
   and correct it if it differs.
5. **Near plane**: implement it as the single lever that hides the body, with the
   value from Leg A. Until Leg A reports, parameterise it and pick a documented
   placeholder; the orchestrator will relay the measured number.
6. Tests: pin the camera formula (offset, pitch, FOV) and the near plane in
   headless tests. **Note honestly that headless tests do not rasterize** — they
   cannot prove the body is clipped or that a swing is visible. That is the live
   gate's job, not theirs. Build by EXIT CODE, then `ctest -j6`.

Deliver `refactor/T300_leg_b_implementation.md`. Commit on `task/T300-legB`.

## Leg C (after A and B) — THE LIVE GATE. Dispatched separately.

Video capture of fable in first person, stepped frame by frame: body invisible at
rest at every pitch including straight down; weapon visible on swings; foot on a
kick. **Stills are not acceptable evidence in this unit** — see
`dont-sample-transients-with-stills` and T298's 25.2% false-null rate. Will
judges the look; no leg may self-declare it acceptable.
