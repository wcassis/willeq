# T298 — Official-client FIRST-PERSON camera baseline (oracle) + fable camera audit

**Trigger.** Will reviewed `review/T296/T296_2_firstperson_idle_arms_and_weapon_800x600.png`
and rejected it: the view reads as being **from BEHIND the player**, not from the
player's eyes. Both upper arms are visible with the elbows pointing at the viewer
and the shoulders running off the top of frame — the geometry a camera set back
behind/above the shoulders would see, not an eye-point view.

**This unit does NOT change the viewmodel.** It produces the reference that any
later fix must be measured against, and it establishes where fable's camera
actually is. Nothing here is a fix; nothing here may be self-declared acceptable.

---

## Leg A (slot1) — OFFICIAL CLIENT BASELINE. Owns eqemu + the oracle client.

Take the lock ATOMICALLY first (executor protocol):
`bash -c 'set -o noclobber; echo "T298 $(date -I)" > /mnt/projects/fable/.eqemu-lock'`
Non-zero exit means someone else owns it: STOP and report. Delete it after a
clean shutdown. `source /mnt/projects/eq-oracle/env.sh` — it honours the same lock.

Drive the OFFICIAL Titanium client under Wine/Xvfb with
`/mnt/projects/eq-oracle/scripts/` (login-as.sh, enter-world.sh, ui-lib.sh).
Character: the SAME test character fable uses — account `willeq_test`,
`Testchar`. It MUST have items in BOTH primary and secondary; verify by reading
`inventory` for slots 13 and 14 in the DB and, if empty, equip via `#si` and
re-verify from the DB (never from the command echo — see the
`#setskill` trap in memory). Record the item ids used.

Put the client in FIRST PERSON (the official client's camera cycle key) and
confirm the mode from the screen, not from an assumption about which key it is.

### A1 — the four required states
Capture a screenshot in each, all same zone, same daylight (`#set time 11 0`),
same window size, all with pri/sec equipped:
1. **idle** (standing still)
2. **walking** (walk speed, moving forward)
3. **running** (run speed, moving forward)
4. **combat** (auto-attack ON against a spawned training dummy, several frames
   across at least one full swing cycle so a swing animation, if the official
   client draws one, cannot be missed)
Also capture a **third-person control** frame in the same run for scale.

For each frame record, alongside it, the character's `/loc` output and heading.

### A2 — the question this unit exists to answer
**Does the official client draw ANY part of the player in first person?**
Arms? A weapon? Nothing at all? Report what is in the pixels; do not infer it
from EQ folklore, and do not report "no viewmodel" without a frame that shows
the empty view while the DB confirms both weapon slots are filled.

### A3 — camera position relative to the player, MEASURED
Reason from pixels, not from opinion. Suggested method (use it or a better one,
but state the method and its error bars):
- Spawn two NPCs as survey markers at KNOWN, separated locs (`#npctypespawn`,
  then read their spawned position back from the server, not from the eye).
- With the player at a known `/loc` and heading, measure each marker's pixel
  (x, y) in the first-person frame. With the client's horizontal FOV, two
  bearings solve the camera's XY; a marker's pixel-y plus its known z solves
  camera Z. Cross-check camera Z against the horizon row.
- Derive the FOV from the frame itself where possible (two markers of known
  angular separation) rather than assuming 45/60 degrees.
Deliver: **camera offset relative to the player's feet position and facing,
as (forward, right, up) in EQ units, with the pitch**, and say plainly whether
the camera is AT the eyes or BEHIND the model. Do the same measurement in the
running frame — if the offset changes with movement state, that is a finding.

### A4 — deliverables
`/mnt/projects/fable/review/T298/` (outside the checkout) with a README that
describes what is in each frame, the DB-verified equipment, every `/loc`, the
measurement working, and the derived offsets. State uncertainty where it exists.

STOP conditions: lock not obtainable; the client will not reach first person;
equipment cannot be verified from the DB. Report narrowed evidence, do not thrash.

---

## Leg B (slot2) — WHERE IS FABLE'S FIRST-PERSON CAMERA, ACTUALLY. No server.

Source-and-log audit only; touch no file outside `refactor/` and produce no fix.

1. From T295's single camera formula and T296's viewmodel transform, derive the
   camera's position and orientation **relative to the player node's origin and
   facing** as a number: standoff, eye height, pitch, and which frame each term
   is expressed in. T295 shipped a 0.3 standoff and a -12 degree pitch and
   `[FP-EYE] source=BBOX_FALLBACK eyeHeight=3.6527`; state what those add up to
   and, critically, **in which direction the 0.3 standoff points** — behind the
   player is the hypothesis Will's review implies.
2. Test the competing explanation: the camera may be correct and the VIEWMODEL
   may be wrong — arms placed at the wrong depth/scale so that upper arms and
   elbows fall inside the frustum. T296 fixed the arm transform to `RotY(h)` and
   equipment to `RotY(-90) * relativeTransform`; check whether anything positions
   the arm mesh relative to the camera at all, or whether the arms are simply the
   player's own body mesh drawn from a camera that sits behind it.
3. Predict, from the numbers alone, what SHOULD be visible at eye-point with the
   T296 transform, and compare against the T296 frames in `review/T296/`.
4. Report a ranked list of candidate root causes with the file:line evidence for
   each, and say which one Leg A's baseline will discriminate.

Deliverable: `refactor/T298_leg_b_fable_fp_camera_audit.md`. No code changes.
