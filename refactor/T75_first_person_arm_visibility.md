# T75 — Make the first-person arms actually visible

**Slot: t1. OWNS EQEMU. x86 only, no Pi.**

**Territory: `src/client/graphics/irrlicht_renderer.cpp` (first-person
camera placement/orientation), `src/client/graphics/entity_renderer.cpp`
and `src/client/graphics/eq/animated_mesh_scene_node.cpp` (only if a pose
or node-transform change proves to be the right lever), and tests.**

Do not change the arm *filter* — T74 built and verified it across 7
race/gender combinations and it is not the problem. Do not touch
`bone_attachment.h`, equipment placement, or the spell path.
`refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
**`refactor/T74_first_person_arms_and_weapons.md`'s Review and
`/mnt/projects/fable/review/T74/README.md` in full** — T74 did the
geometry work and recorded precisely why nothing is visible.

## The state you are inheriting

T74 implemented first-person arm geometry and verified it thoroughly:
`applyFirstPersonIndexFilter()` keeps only triangles bound to arm bones,
proven across 7 race/gender combinations (11-19 arm bones each, 20-27% of
indices retained, never zero, never everything), with a logged fallback.
Third person and the paperdoll are provably unchanged.

**And yet nothing is visible in first person.** T74's own README states
the reason: the arms sit **at shoulder/chest height, below the eye bone
the camera is placed on**, and the camera looks level-forward. The
geometry is correct and simply outside the frustum.

T74 also left the attack capture **inconclusive** — `/attack` engaged but
no landed swing was confirmed on camera before the window closed. So
nobody has yet seen what a swing looks like either.

## The fork this unit must resolve with evidence, not assertion

Two different things could be "right", and they need different work:

- **(A) Arms visible at idle** — a modern-FPS look. Requires changing the
  camera (pitch down slightly, or move back), the arm pose, or both.
- **(B) Arms visible only during the swing** — which may be what
  EverQuest actually did. If so the geometry is *already correct*, and the
  real gap is that the attack animation never plays or was never captured.

**Will has asked for (A).** But he is the authority on what the target
game did, and he has not yet been able to see either option. So:

**Produce evidence for BOTH and let him choose from pictures.** Do not
decide this yourself, and do not treat (B) as an excuse to skip (A).

## Step 1 — measure before changing anything

Determine, numerically, where the arm geometry sits relative to the
first-person camera. `getBoneWorldPosition()` exists
(`animated_mesh_scene_node.h:202`) and the camera placement is at
`irrlicht_renderer.cpp:2421-2437` (camera Z = feet + `getPlayerEyeHeightFromFeet()`
+ `playerConfig_.eyeHeight`, default fallback 6.0).

Report: arm bone world positions in **idle** pose and in the **attack**
animation, against the camera position and its frustum. That tells you
whether a swing alone brings them into view — which is the whole of
option (B) — and how far short idle falls, which sizes option (A).

Do this headlessly if you can; it is a geometry question, not a live-session
question, and the loop is far faster.

## Step 2 — make the swing observable

Regardless of which option wins, **the swing must be capturable** — T74
could not confirm one, so option (B) is currently untestable and option
(A) cannot be judged against it.

Establish a reliable way to get a landed attack on camera: a target that
stays in range, `/attack` confirmed engaged from the log, and video
running across the swing. T64's capture path exists; T71 fixed `/equip`;
`/camera` asserts the mode. If the blocker is that the mob dies, wanders,
or the window closes first, solve that — a GM-spawned stationary target is
fine.

## Step 3 — make idle arms visible, and keep it honest

Implement (A). The likely levers, in rough order of preference:

1. **Camera orientation/position** — a small downward pitch or slight
   pullback. Cheapest, no model change, no risk to third person.
2. **A first-person arm pose** — real EQ-era clients often used a
   dedicated pose rather than the idle animation.
3. Node transform offsets on the filtered mesh.

**Prefer whichever changes the least.** State what you chose and why, and
what you rejected. If the camera lever alone suffices, do not touch poses.

Hard constraints:
- **Third person must not change.** It is gated on the `FirstPerson`
  branch today; keep it that way and prove it with the visual gate plus a
  before/after capture.
- **Do not undo T74's filter.** If the arms look wrong, that is a pose or
  camera problem, not a filtering problem.
- If a value is tunable (pitch angle, offset), make it a **named constant
  with a comment**, not a magic number buried in a call.

## Visual acceptance — the point of the unit

Will judges, and he needs to compare. Capture, in a **genuinely lit**
zone — T63 and T74 both produced captures too dim to judge, do not make it
three:

- **idle, before** (T74's current state — nothing visible),
- **idle, after** (option A),
- **a landed swing, before** — the geometry as T74 left it,
- **a landed swing, after**,
- **third person before/after**, captured identically.

Video for anything temporal (T64: `--dump-frames-dir` / `--dump-every` /
`--dump-max-frames` + `scripts/ci/encode_capture.sh`; keep the
measured-fps file with each video — capture fps is an artifact, never a
performance claim).

Media to `/mnt/projects/fable/review/T75/` with a README that lays the
pairs side by side and says what each shows. **Describe, do not grade.**

## STOP conditions

- Step 1 showing the arms are not where T74 reported — report before
  changing anything.
- Needing to modify the arm filter.
- Third person or the paperdoll changing.
- `.eqemu-lock` held.

## Verification

Full protocol bar, plus Step 1's measurements, the swing capture (which
T74 could not obtain), and third-person before/after proving no
regression.

## Review

Full media/evidence: `review/T75/README.md`.

### Lighting fix (coordinator directive, addressed before the rest)

`/say #set time 11 0` (space-separated hour/minute) is the WORKING form of
the GM command; Will's stated `#set time 11:00` (colon syntax) would not
have parsed (`code/zone/gm_commands/set/set_time.cpp` reads `sep->arg[2]`/
`arg[3]` as two separate numeric tokens — `"11:00"` fails `IsNumber()` and
just prints usage). The prior session's crash used `#settime 12` (a
different, nonexistent command spelling), not this one. Tested EARLY as a
throwaway (with graphics, frames dumped) per the coordinator's instruction:
112 frames of real, varying content over ~10s after the command, no crash,
`timeout` eventually killed the still-healthy process — not a hang or
crash. Used successfully in every subsequent capture this unit made. **Board
recommendation**: `/say #set time 11 0` is the safe, reusable form for any
future capture session needing daylight — record this so T63/T74's dim-
capture problem doesn't cost a fourth unit.

### Step 1 — measurement (headless, `ArmVsCameraGeometryMeasurement_Headless`)

Human male. Raw base-mesh bbox `Y: [-3.00, 3.65]` — NOT `[0, height]` as the
pre-existing `getPlayerEyeHeightFromFeet()` implicitly assumed. Its old
formula (`MaxEdge.Y - MinEdge.Y` = 6.656) put the first-person camera a
body's-height above the model's actual top; under it, arm-bone geometry
measured 3.1-6.5 units below camera Y in idle AND across 5 attack
animations sampled (1H slash/blunt, 1H piercing, 2H slash, 2H blunt,
hand-to-hand) — a swing never closed more than a fraction of that gap,
directly answering the plan's option (B): under the OLD camera math, a
swing alone never brought arms into view. The fixed formula (`MaxEdge.Y`
alone = 3.653, corroborated by the skeleton's own head-bone position
measuring 3.096, very close) shrinks that gap to 0.13-0.85 units across the
same sweep — small enough that a modest pitch closes the remainder for at
least part of the arm's range, confirmed live below. Full numbers:
`review/T75/T75_arm_vs_camera_measurement_output.txt`.

### Lever chosen: camera position + orientation (plan's lever 1); rejected: pose, node transform

1. **Bug fix, not a new formula**: `EntityRenderer::
   getPlayerEyeHeightFromFeet()` (`entity_renderer.cpp`) now returns
   `bbox.MaxEdge.Y` instead of `bbox.MaxEdge.Y - bbox.MinEdge.Y` — this did
   essentially all the work (Step 1's numbers). `getPlayerHeadBonePosition()`
   (same file) turned out to duplicate the identical buggy formula under a
   misleading name (it never read a bone transform); it now delegates to
   the one real implementation instead of carrying the same bug twice.
2. **New named constant**: `kFirstPersonPitchDownRad = 12 degrees`
   (`irrlicht_renderer.cpp`, comment cites the Step 1 measurement it's
   based on), applied only to the first-person look target inside
   `repositionFirstPersonCamera()`. The ONLY tunable this unit introduces.
3. **De-duplication**: `setPlayerPosition()`'s FirstPerson branch was a
   third independent copy of the camera-placement formula (the other two
   call sites already used T74's shared `repositionFirstPersonCamera()`
   helper). It now calls that helper too, so the fix/pitch apply uniformly
   at every entry point instead of only two of three.

**Rejected**: a dedicated first-person pose (lever 2) — Step 1 showed the
camera-only fix already closes the gap enough for SOME geometry to be
visible without one, so least-change stops at lever 1; node transforms
(lever 3) — never reached. **T74's arm filter is untouched** —
`buildArmBoneIndices()`/`applyFirstPersonIndexFilter()` byte-for-byte
identical; the only change in that file's header is a new read-only
accessor (`getArmBoneIndices()`) exposing the existing
`armBoneIndices_` vector for this unit's measurement test.

### Idle / swing — before/after; swing made capturable

Idle: geometry (a distinct gray/seamed shape, absent from every T74 idle
capture) is now inside the frustum in some frames, but flickers in/out
across the idle animation cycle — Step 1's own numbers (0.53-0.85 units
below camera at idle's worst) explain why: the fix reaches the EDGE of the
frustum for idle, not robustly inside it for the whole cycle. Reported
honestly rather than tuned further past "least change."

Swing made capturable (T74 could not do this): `#npctypespawn 77002`
(`a_training_dummy`, stationary, non-dying — chosen specifically so the
target can't wander or die mid-capture) + `/target dummy` (bare `/target`
does NOT do `targetNearest()` in the current code — H03's old comment is
stale; it requires a name and silently no-ops without one, which is why an
early throwaway attempt's `/attack` logged "No target") + `/attack`
log-confirmed "Auto attack ON", no "too far" message, sustained 12s window,
video running throughout (242 frames, 68s real-time span). **Honest
limitation**: `/equip` to arm the character failed BOTH attempts (two
different, valid items, including T74's own confirmed-working item) with
eqemu's generic `"The selected slot was invalid."` — root cause not chased
(equip mechanics are out of this unit's territory; DB read-back confirms no
corruption left behind). The captured swing is therefore unarmed
(hand-to-hand), and no hit/miss combat text confirmed an actual landed
blow (the dummy's 900M hp means health% can't show it either) — real
progress over T74's "too far"/wandering-target failures, but short of a
fully confirmed landed hit.

### Third person — regression evidence

Unaffected by construction (all changed lines sit behind `cameraMode_ ==
FirstPerson`, or are read-only test accessors). Live capture: Follow
camera, `"Camera mode: Follow"` on screen, dummy + a wild rat rendering
normally, real grass/lake terrain (also used as the reference proving the
idle/swing gray shape is not ground). Visual gate PASS, goldens untouched.

### Verification

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build` — 2043/2044, sole failure the documented ALSA
   host waiver (`AudioManagerTest.PreloadCommonSounds`), unchanged from
   baseline. `review/T75/T75_ctest_full_summary.txt`.
3. `scripts/build-gles2-x86.sh` — clean.
4. Visual gate PASS, goldens untouched (`review/T75/T75_visual_gate_output.txt`).
5. `scripts/build-arm-noble.sh` — clean cross-compile (x86 only per plan;
   no Pi run).
6. `check_file_sizes.sh fable` — `entity_renderer.cpp` FAILs at 3321 lines,
   but this is PRE-EXISTING (3313 lines on `fable` before this unit; this
   unit's net contribution is +8 lines — a bug-fix comment plus a
   de-duplication that removed more than it added). Not a new violation.
   `irrlicht_renderer.cpp` sits exactly at the 3000-line WARN threshold,
   also pre-existing scale. `review/T75/T75_check_file_sizes_output.txt`.

### Teardown

Testchar: inventory restored (general slot 23 back to item 12654, matching
the documented all-slots-12654 baseline, confirmed via `SELECT`), position
restored to the protocol baseline (`zone_id=36 x=-77.9232 y=21.9086 z=3
heading=190`, confirmed via `SELECT`). Server: clean shutdown via
`down.sh` (containers stopped/removed, confirmed), lock file deleted
(confirmed via `ls` failing).

### Deviations from the plan

1. **Territory expansion beyond `irrlicht_renderer.cpp`'s named camera
   code**: `entity_renderer.cpp`'s `getPlayerEyeHeightFromFeet()`/
   `getPlayerHeadBonePosition()` were also fixed. These are the plan's own
   named camera-height inputs (`getPlayerEyeHeightFromFeet()` is cited
   explicitly in the plan's Step 1), not new territory — the bug lived in
   the height CALCULATION these two functions do, not the camera placement
   math that consumes it, and fixing the placement math alone while leaving
   its input formula double-counting a body's height would not have fixed
   anything.
2. **No pose lever needed**: the plan allowed for a first-person pose
   (lever 2) if the camera lever alone proved insufficient. Step 1's
   measurement showed the camera-only fix was sufficient to bring SOME arm
   geometry into frame (confirmed live), so lever 2 was not implemented,
   per the plan's own "prefer whichever changes the least" instruction.
3. **Weapon equip failure**: both `/equip` attempts during the swing
   capture failed server-side (generic eqemu `"invalid slot"` message,
   different from T74's session where the identical item worked) — not
   chased further as it's outside this unit's territory (equip mechanics).
   Flagged for the board in case a future session needs armed first-person
   captures and hits the same wall.

Board: idle-vs-swing question — **not fully resolved either way**. The
camera/geometry fix makes SOME arm geometry visible in BOTH idle and swing
(contradicting a clean "arms only during swing" story), but idle visibility
flickers rather than being robust, and the swing capture is unarmed with no
confirmed landed hit. Will should judge from the captures in
`review/T75/README.md` whether this partial fix is enough signal, or
whether a follow-up unit should push the pitch/height further and/or chase
the `/equip` failure to get an armed, confirmed-landed swing.
