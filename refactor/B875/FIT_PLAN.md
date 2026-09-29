# B875 — plan for the one-time fit of the official client's Chase camera constants from manual correspondences

Status: **PLAN. Nothing in this file has been run against the official
client.** Issue #219 stays open until it has. What exists today is a pin on
fable's own constants (`refactor/B875/README.md`) and an offline-validated
solver (`tools/chase_fit/`).

Ruling this implements: `docs/decisions.md` 2026-09-23 (camera-pose oracle
verification is numeric pose, not pixel similarity; B875 = fit the official
constants ONCE from official stills at logged poses, pin by ctest; a fit that
shows fable wrong is a separate card and a product call). Owner's later
ruling: the correspondences are **MANUAL** — hand-picked image points on
static geometry.

## 1. What is fitted

The official Chase camera, modelled as fable's rig (`camera_rig.h`
`computeCameraRigPose()`), plus the projection:

| parameter | meaning | fable today |
|---|---|---|
| azimuth | degrees from straight behind the heading | 0 |
| pitch | degrees above the look-at, looking down | 6 |
| distance | EQ units, look-at to camera | 4.0 |
| height | EQ units, look-at above the feet (see 3.3 for "feet") | 7.0 |
| horizontal FOV | degrees | 62.8 |
| eye-scale exponent | rig scaled by `(eye / 6.023)^k`; fable assumes k = 1 | 1 |

The exponent is only identifiable with **two or more eye heights** (two
races); with one race it is not fitted and the solver refuses to pretend
otherwise. Distance and height are fitted per the reference eye (the Human
male's 6.023, T431).

Assumptions the model makes, each checked by the residual rather than argued:
the look-at is on the player's vertical axis; no roll; square pixels; the
principal point is the image centre; no lens distortion; no collision
pull-in at the capture spot (3.1). A model that is wrong on any of these
shows up as an RMS above the bound or a heading-dependent azimuth, and the
fit is then refused, not tuned.

## 2. Inputs required

1. **Official-client stills at known COMMANDED poses**, >= 5 stills
   (solver minimum `kMinPoses`; plan for 8), >= 3 distinct headings
   (`kMinDistinctHeadings`; plan for 6 spread over 0-512), >= 2 player
   positions, one resolution for the whole set (1024x768, the T466 rig's),
   client area only, UI hidden if the client allows it.
2. **Server-side pose for the official client at each still** — issue #218
   deliverable A (the server `MOD_MOVEMENT` log / `#loc` / `character_data`,
   as `refactor/B874/live/` did for fable on branch `task/pose-live-capture`).
   The pose used is the one the SERVER logged at the still's timestamp, never
   the one that was commanded. Converted to the client frame with the
   documented transform (`pose_log_format.h`): `playerX = server_y`,
   `playerY = server_x`, `heading0_512 = (128 - server_heading) mod 512`.
3. **Static, corner-rich geometry in view** at every pose: wall/floor
   junctions, door frames, pillar and stair corners, window corners. qeynos2
   near T466's spot is the default; any zone works if its WLD vertices can be
   read (3.2).
4. **A second race** (for the exponent): the same set again with a
   character whose eye is far from 6.023 (T431's Dwarf male, 3.58), same
   positions and headings.
5. **Re-grabs for the noise floor** (section 5): at one commanded pose, >= 3
   stills taken without moving.

## 3. Procedure

### 3.1 Capture

- Pick positions with open space of >= 3x the expected distance behind the
  player in every captured heading, so no collision pull-in is possible.
- Enter Chase from another camera mode for every still (a fresh entry; the
  wheel does not zoom Chase, B680, but do not rely on it).
- Command the heading on the SERVER side, then wait >= 2 s and confirm from
  the server pose log that the pose has settled and the character is idle
  (movestate idle). A still is captured only after that confirmation. (B874's
  live run found fable's `/turn` reaching the server but not the rendered
  pose; the official client is not assumed to be better.)
- Record per still: file name, sha256, image size, capture wall-clock time,
  the server pose row joined by time, the character and its eye height.

### 3.2 Manual correspondences

For each still, a person (or an agent acting as one, with the image open at
1:1 in a viewer that shows pixel coordinates) picks >= 8 points
(`kMinPointsPerPose`; plan for 15-20) on **static geometry only**:

- **The pixel** is the clicked pixel centre, `(i + 0.5, j + 0.5)` with the
  origin at the top-left corner. Every image coordinate in the file is
  therefore on the 0.5 px grid; the solver refuses anything else.
- **The world point** is read from the zone geometry itself: the zone WLD's
  vertex (e.g. via `wld_dump`), converted to the client EQ frame, recorded
  with a `geometryRef` of the form `zone:fragment:vertex`.
- The pixel and the world point are paired by looking at the still and at the
  geometry. **No camera is involved in pairing them** (section 6).

File format (TAB-separated; `#` lines are comments), read by
`chase_fit::readCorrespondenceTsv()`:

```
pose   <id> <still file> <width> <height> <playerX> <playerY> <feetZ> <heading0_512> <eyeAboveFeet>
point  <pose id> <worldX> <worldY> <worldZ> <u> <v> <geometryRef>
```

### 3.3 "Feet"

`feetZ` is the server z converted to the client frame, unadjusted. The fitted
`height` is therefore "look-at above the server z". fable's `height` is above
its own feet (`playerZ - collisionZOffset`), and B874's live run measured
fable's playerZ 0.6306 BELOW the server z for its Human male. Comparing the
two heights must apply that measured offset; it is recorded here so nobody
absorbs it into a "fitted" height silently.

### 3.4 Solve

```
build/bin/chase_rig_fit corr.tsv                    # one race
build/bin/chase_rig_fit corr.tsv --fit-eye-scale    # two races
build/bin/chase_rig_fit corr.tsv --check 0 6 4 7 62.8   # does fable's rig explain the stills?
```

Levenberg-Marquardt over the reprojection error of every point in every still
at once (`tools/chase_fit/chase_rig_fit.cpp`), numeric Jacobian, covariance
from the residual. The tool runs two generic starts from opposite sides of the
plausible range — neither is fable's rig — and refuses the fit if they reach
different minima.

## 4. Acceptance (all must hold, or the fit is not used)

| check | bound |
|---|---|
| inputs | >= 5 stills, >= 3 headings, >= 8 points per still, every point on the 0.5 px grid with a geometry reference, every still with a file and a server pose |
| RMS reprojection | >= 0.1 px (below it the points were not clicked) and <= **2x the session noise floor** (section 5); the tool's own 2.0 px cap is only a sanity ceiling |
| two starts | agree to 0.05 deg / 0.01 units |
| 1-sigma | azimuth, pitch <= 0.1 deg; distance <= 1 % of fitted; height <= 0.05; FOV <= 0.2 deg |
| leave-one-still-out | every parameter moves by <= 3 sigma whichever still is dropped |
| heading independence | azimuth fitted on each heading subset agrees within 3 sigma (a heading-convention error shows as a heading-dependent azimuth) |
| re-click audit | an independent picker re-picks >= 10 randomly chosen points on >= 3 stills without seeing the first picks; median disagreement <= 1.5 px, max <= 3 px |

`chase_rig_fit` automates the input checks, the RMS floor and cap and the
two-start agreement, and prints the sigmas. Leave-one-still-out and heading
independence are run by re-running it on subsets of the TSV; the noise floor
and the re-click audit are separate TSVs. All of them are recorded with the
fit.

Then the comparison with fable (B876's thresholds): |delta azimuth|,
|delta pitch| <= 0.5 deg and |delta distance| <= 1 % of the fitted distance.
A miss is a **finding for a separate card and a product call**, not an edit to
`kChaseRig` inside B875.

## 5. The self-calibrating noise floor (2026-09-23 ruling)

No hand-typed band. Per capture session: >= 3 re-grabs at one commanded pose,
the same N >= 10 static points picked on each (blind to the other picks). The
per-point scatter across re-grabs, pooled, is that session's floor sigma_f
(click error + render/present jitter). The fit's RMS bound is 2 sigma_f. If
the re-grabs themselves disagree by more than a pixel or two, the session is
not stable enough to fit from and is re-taken.

## 6. Circularity traps (each one has already happened once)

1. **Never generate correspondences with the camera being solved for.** MR
   !31 projected its points with the reference camera and "recovered" the
   reference: zero delta on every axis, which is what a circular fit always
   reports. Neither half of a pair may come from a camera: no pixel by
   projecting a vertex (not even "to find it faster" with fable's camera), no
   world point by ray-casting a pixel into the zone. The solver refuses off-grid
   pixels and a sub-0.1 px RMS; a projection ROUNDED to the grid defeats both,
   which is why the re-click audit (section 4) exists.
2. **Never cite a pose that was not captured.** MR !31's golden cites qeynos2
   (131, 30, -28), where no capture was taken, with "camera_pose" rows that
   were computed, not measured. Every pose row must join to a still file (sha256)
   and a server pose row whose timestamp is inside the capture window.
3. **Idle frames are not camera evidence.** The character's idle animation
   moves pixels without the camera moving (the 2026-09-23 ruling's h288
   finding). No point may be on the character, an NPC, a door, a particle or
   the sky; stills are taken only once the server says the pose is settled.
4. **"The solver did not move from the reference" is not confirmation.** A fit
   that starts at the answer and stays there proves nothing; the tool starts
   from two generic points and the tests start one AT kChaseRig on data from a
   different camera and require it to leave.
5. **A parameter that is not identifiable is reported as not fitted**, never
   "assumed at reference" and counted as confirmed (MR !31 did this with the
   height). The exponent without two races is the live case.
6. **No automatic matching against a fable render.** Feature matches (ORB or
   otherwise) between a fable frame rendered at fable's camera and an official
   still carry fable's camera into the data.

## 7. Deliverables when this runs

- `refactor/B875/` gains the stills' sha256 list, the server pose rows, the
  correspondence TSV(s), the re-click audit TSV, the noise-floor TSV and the
  `chase_rig_fit` output, all byte-for-byte from the run.
- A ctest that re-runs `chase_rig_fit` on the committed TSV and requires the
  committed fitted values (so the fit is reproducible and a solver change is
  red), plus a `--check` of fable's rig against it, whose verdict is recorded,
  not tuned.
- Only then does `tests/chase_rig_expected.h` gain an "official fit" section;
  its current values stay fable's until a product call moves them.

## 8. Not covered here

Other races' Chase geometry beyond the exponent (B681), sitting/crouching
(B681), whether the official Chase zooms (B680), wall pull-in behaviour, and
the B876 checker itself.
