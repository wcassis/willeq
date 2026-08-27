# T299 — CORRECTION: the official client DOES draw the player in first person

**T298 Leg A concluded "the official client draws nothing of the player in first
person." Will says that is WRONG and supplied the reference:
`/mnt/projects/fable/oldeverquest.mp4` (640x480, 155 s). From ~1:30 the player
attacks; on a SWING the main-hand weapon becomes visible, and on a KICK the foot
is briefly visible.** T298's frames must not be used as a baseline until this is
settled — and the conclusion drawn from them ("delete the viewmodel pass") is
withdrawn pending this unit.

**Working hypothesis to test, not to assume:** first person in the official
client is not a viewmodel at all. The player's ordinary world model is drawn, and
the camera sits at the eyes; you see nothing most of the time because the body is
below and behind the frustum, but an animation that swings a weapon forward — or
raises a foot — brings that geometry INTO the frustum. If true, the correct fable
implementation is "render the player normally, put the camera at the eyes," not a
separate viewmodel pass, and T296's whole approach is misconceived.

Why T298 missed it is itself a finding to report: candidate explanations include
sampling stills rather than video (weapon visible for only a few frames of a
swing), captures landing between swings, misses vs hits animating differently,
a near-plane that clips the geometry, or the animation not playing at all.
**Do not settle for a plausible story — demonstrate the cause.**

---

## Leg A (slot1) — VIDEO FORENSICS on Will's reference. No server.

1. Establish that the video's view IS first person (the body is absent in normal
   play, and no chase-camera geometry is present). State the evidence.
2. **Note the client version.** The UI in this video is the classic pre-Luclin
   frame, not the Titanium UI the oracle drives. Record what that does and does
   not license us to conclude about Titanium.
3. Scan from 85 s to the end and find EVERY frame in which the player's own
   geometry enters the frame. Frame-difference or colour-segment to find
   candidates; do not eyeball 2000 frames. For each occurrence report the
   timestamp, how many frames it persists, and what body part it is.
4. For each occurrence produce a labelled crop at full resolution. The weapon
   swing at ~1:30 and at least one kick are REQUIRED deliverables.
5. From the geometry itself, infer what it says about the camera: where does the
   weapon enter from (edge, direction of travel, how far across frame), where
   does the foot appear, and is what is drawn consistent with the ordinary player
   model seen from an eye-point camera? Give the reasoning; flag what the video's
   resolution cannot support.

Deliverables in `/mnt/projects/fable/review/T299/` with a README, plus
`refactor/T299_leg_a_video_forensics.md`. Commit on `task/T299-legA`.

## Leg B (slot2) — REPRODUCE IT LIVE, and explain T298's miss. Owns eqemu.

Take the lock atomically (executor protocol); STOP if you cannot.
`bash -c 'set -o noclobber; echo "T299 $(date -I)" > /mnt/projects/fable/.eqemu-lock'`

1. Re-read `review/T298/README.md` and its raw frames first — inherit the working
   setup (UI hidden with F10, DB-verified pri/sec, the F9 cycle map, qeynos2) and
   do not re-derive it.
2. **Capture VIDEO, not stills**, of first-person auto-attack at the highest
   frame rate the harness can sustain, long enough to cover many swing cycles,
   and also capture a KICK (a warrior's kick skill) and a bash if available.
   Then step the video frame by frame looking for weapon/hand/foot geometry.
3. If it appears: report the frames, and measure what it implies about the eye
   position — this is the real baseline T298 was supposed to produce.
4. If it does NOT appear in Titanium: that is a substantive finding about a
   client difference, and it must be evidenced (animation demonstrably playing,
   equipment demonstrably drawn in third person, geometry demonstrably absent),
   not asserted from a null result. Say what would distinguish "Titanium differs"
   from "our capture path is losing it."
5. Explain T298's miss with a demonstration, not a story.

Restore Testchar (zone_id=36, x=-77.9232, y=21.9086, z=3, h=190), shut down
cleanly, delete the lock. Deliverables in `review/T299/` (coordinate filenames
with Leg A: prefix `T299_B_`), plus `refactor/T299_leg_b_live_repro.md`.
Commit on `task/T299-legB`.
