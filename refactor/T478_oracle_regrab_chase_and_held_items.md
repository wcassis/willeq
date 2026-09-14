# T478 — fresh oracle grabs: Chase at two headings on a clean spot (B683, B584), held-item sets A–C and attack (T472 Leg B), IT10 (B661)

Class A (match the oracle), **eqemu lock + eq-oracle rig**, slot1. Dispatch only
after T474 (Chase rig) and T476 (skinned held items) are MERGED — this unit
measures the post-T472/T474/T476 tree. Read the T466, T472, T474 and T476
Reviews and their `review/` READMEs. The T466 plan lists the oracle rig traps
(window-relative crops, video for transients, `enter-world.sh` offset, 'u'
key).

## Why a re-grab, not a re-fit of old stills

T474's h288 re-fit wanted Chase distance 3.5 against T466's 4.0 (B683). But
T474 used T466's stills, taken on a tree that still had the model-facing bug
T472 later fixed, at a spot with 13 stacked training dummies (B584). T474's own
fable still showed a dummy facing the camera. Clutter or facing may explain
the disagreement; measure again before changing any value.

## Tasks

1. **Clean the spot (B584).** Find and remove the 13 training dummies from the
   canonical qeynos2 measurement spot in the DB. Record which spawn rows were
   removed, restore them at the end, and confirm nothing is left in frame at
   either heading.
2. **Chase re-grab.** Take fresh oracle stills at h160 and h288, same pose,
   race and resolution, cropped against the window. Run T474's sweep for both
   headings on the merged tree. Report both fits with their sensitivity, and
   the default Chase score at each.
   - If both agree within the stated sensitivity, strike B683 and keep or
     adjust the values.
   - If they still disagree, STOP with both fits; do not pick one.
   - Also measure B680 (does the official Chase zoom with Insert/Delete or the
     wheel?) using video.
3. **Held items (T472 Leg B).** Kit a character per the gm-kit recipe (`#set gm
   on` first) and capture at the matched Chase:
   - set A: Short Sword + Gloomingdeep Lantern;
   - set B: 1H blunt + shield;
   - set C: a 2H weapon;
   - IT10 (B661).

   Capture both clients idle at 3 or more azimuths, and one attack animation
   each as stepped video. Solve B586's `/target`, or use a GM-spawned target in
   melee range and in front. Build side-by-side contact sheets and describe the
   frames; don't judge them.
4. **Model facing.** Confirm at the matched Chase that fable shows the back of
   the head (B587), for a character that has not moved since zoning in.

## Proof

- **Media:** `review/T478/` (`oracle/`, `fable/`, `sheets/`, a README).
- **Source changes:** any change (Chase constants only, if agreed) takes the
  standard bar.
- **eqemu protocol:** lock, dummies restored, poses restored, world down, oracle
  client closed.
- **Board:** B720–B729; strike or narrow B683, B584, B586, B587, B661, B680.

File territory: `camera_rig.h` Chase constants, camera tests,
`review/T478/`, `refactor/BOARD.md`. **Not** equipment/attachment code (a
held-item fix is a follow-up unit planned from these frames).

STOP: the two headings still disagree beyond sensitivity; an image golden
moves; the rig cannot remove or restore the dummies safely.

## Review

(executor fills)
