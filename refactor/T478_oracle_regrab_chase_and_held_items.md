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

Slot1, `task/T478-oracle-regrab`, base `fable` `09c5472f`. Media and every number: `review/T478/README.md`.
**STOP hit on task 2** (the two headings still disagree; no value picked, no source change). The other tasks were
completed as measurements. No image golden moved, because no source file changed.

### 1. The spot (B584)

The dummies were not in `spawn2`. They are runtime spawns that eqemu's `zone_state_spawns` table saves at zone
shutdown and restores at boot: 14 rows of npc 77002, `spawn2_id` 0, 13 of them at (-74, 428, 3.1). They were
deleted with the world down (zone 2: 200 -> 186 rows), and none is left on the player in the official Chase
frames at either heading. They were restored at the end from an exact `mysqldump` (section 6). Board B720 asks
whether to remove them for good.

### 2. Chase at h160 and h288 (B683) -- STOP

- **Oracle stills.** Three fresh grabs per heading, pose pinned by T310's `camera_match_pose.sh`.
  - Identity: 0.06-0.14 against T466's Chase stills, >= 0.77 against the other cameras.
  - Re-grab floor: 0.02-0.08.
- **Geometry.** The Wine window sits at root (-1, 30), size 1024x748; T466's stills are root grabs. So T474's
  scorer offset is the correct root-to-window mapping.
- **fable (x86 `build-gles2` from `09c5472f`).** Five sessions covering pitch 3-8.25, dist 1.5-5, height
  5.75-8. Pose and race scale (1.0000) were read back in every session.

| crop | h160 minimum | h288 minimum | default p6/d4/h7 (h160 / h288) |
|---|---|---|---|
| T474's wall crop `400x600+0+100` | p6 / d4 / h6.75, 0.662 (dist 3.5/4.5: 0.781/0.760) | p7.5 / d4 / h7.5, 0.695 (dist 3.5/4.5: 0.751/0.745) | 0.763 / 0.817 |
| player-centred crop `424x668+300+100` | p6 / d2.5 / h6.5, 0.766 (d2/d3: 0.770/0.790) | p4.5 / d2.0 / h6.5, 0.795 (d1.5/d2.5: 0.887/0.878) | 0.800 / 0.946 |

**Wall crop.** The headings agree on distance 4.0, but pitch and height sit two and three grid steps apart along
a diagonal valley.

**Player crop.** The wall crop leaves out the player model. The official head is about 1.6x the width of
fable's at the shipped Chase (`review/T478/sheets/T478_chase_h*_oracle_vs_fable_*`). On the player crop both
headings want a distance of 2.0-2.5, and they disagree on pitch.

**Verdict.** No rig is the minimum at both headings on either crop, and the same rig drifts by up to 0.033
between sessions. Per the plan, both fits are reported and `kChaseRig` is unchanged.

**What B683 was.** Its "dist 3.5" is the pitch-6 slice of the h288 wall-crop valley: at pitch 6 and height 7,
dist 3.5 scores 0.791 and dist 4 scores 0.817. The board now carries this as **B721** and marks B683 superseded.

### 3. B680 (video)

- **Chase:** Insert x5, Delete x10, wheel up x5 and wheel down x10 all score 0.002-0.141 against the pre-zoom
  still, which is the idle floor.
- **Wheel positive control (First Person):** wheel down x5 moves the view (0.787) and wheel up x10 returns it
  exactly (0.000). So the official Chase does not zoom on the wheel.
- **Insert/Delete positive control:** none. Those keys changed nothing in First Person either.

### 4. Held items and attacks (T472 Leg B, B586, B661)

- **Sets.**
  - A: 9998 Short Sword + 9979 Lantern.
  - B: 6011 Rusty Mace + 1523 Shield of the Forlorn (IT202).
  - C: 2894 Spiny Two Handed Sword (IT10004).
  - IT10: no item can hold it (IT10 items are all arrows), so `#wc 7 10` was used.
- **Setup.** Both clients stand 3 units behind one training dummy at h160; the official `/loc` was read back.
- **Captures.**
  - Oracle: idle in Chase, User 1, User 2 and Tether; attack videos in Tether and User 1.
  - fable: idle in Chase and at rig az 90/180/270; attack frames dumped at az 90 and az 270.
- **B586 solved.**
  - fable: `/target a_training_dummy` (the underscore name) works, and damage packets arrive.
  - Official: F8 + Q, and chat logs the swings.
- **Descriptions (not judged).** Board B722-B726; sheets under `review/T478/sheets/`.
  - Official sets A/B/IT10: no swing pose in any distinct rendered frame, although the chat logs the swings.
  - Set B: the official client shows no shield; fable draws a kite shield on the left arm.
  - Set C: the official 2H sword is roughly horizontal in front of the waist; fable's hangs vertically.
  - IT10: the official arrow hangs down along the leg; fable's is horizontal.
  - fable's sky is a tan/orange texture with white trees.

### 5. Model facing (B587)

At the default Chase, with Testchar not moved since zone-in, fable shows the back of the head and shoulders, as
the official frame does. **B587 closed live.**

### 6. eqemu protocol and restore

1. **Lock and session.** Lock taken as T478 on the eqemu VM. `up.sh` adopted one world.
2. **Snapshots.** Taken before any change (TSV tables plus exact SQL dumps of `zone_state_spawns` zone 2,
   inventory 65/74 and `character_data` 65/74).
3. **Oracle client.** Camped; wine `eqgame` ended; Xvfb `:99` stopped. `eqclient.ini` and the Oraclealpha UI
   ini were rewritten by the client and have been restored byte for byte (hashes in `review/T478/data/`).
4. **Restore v1 FAILED and was caught.** It deleted the rows, but `eq_docker exec -i ... < dump` imported
   nothing, because `eq_remote` uses `ssh -n` (board B729). The diffs were not clean, so the lock was kept.
5. **Restore v2** ran with the world down and mariadb alone up, importing over a plain ssh. Result: 200 zone-2
   rows including the 14 dummies, 7 inventory rows for character 74, and every snapshot table plus both SQL
   dumps IDENTICAL to before.
6. **Restore v3.** It retook the lock and replaced `character_data` rows 65/74 wholesale, because non-pose fields
   still differed. Those rows are now byte-identical.
7. **End state.** Stack down, no containers, lock released.

### 7. Verification

No source, test or golden file changed; only this plan and `refactor/BOARD.md` did. So the build, ctest,
visual-gate, ARM and file-size steps were not run. The `build-gles2` rebuild of `09c5472f` (exit 0, 345 s) was
used only to make the measured binary; its `cmp` against the local capability copy is identical.

### 8. Board

- B720-B729 added.
- B584, B586, B661 and B680 narrowed; B587 closed; B683 superseded by B721.

### 9. Deviations (flagged)

1. **The STOP was applied to task 2's value decision only.** Tasks 3 and 4 were measurement-only and continued;
   they change nothing.
2. **The Chase grid went beyond T474's sweep** (refined and close-range grids), and a second crop was added,
   because T474's grid and crop miss the minimum and the player model. T474's own grid points are a subset.
3. **Oracle sets B and C were changed through inventory rows** with the character camped (in-game `#equipitem`
   does not exist here, B727). IT10 used `#wc`, on both clients.
4. **The first oracle set A attack video had no target** (B728). The retry is the valid capture.
5. **The fable h288 wrapper exited 2** because I edited its script mid-run. The client had finished all 430
   steps; the replies file was rebuilt from `run.log`.
6. **Restore v1 failed** (section 6). Restores v2 and v3 are the evidence.
7. **"Idle at >= 3 azimuths" is not azimuth-matched between the clients.** The official User/Tether cameras are
   world-anchored and unfitted (B651).
