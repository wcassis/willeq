# T76 — First-person camera is inside the arm geometry

**Slot: t1. OWNS EQEMU. x86 only, no Pi.**

**Territory: `src/client/graphics/irrlicht_renderer.cpp`'s
`repositionFirstPersonCamera()` and the first-person camera constants,
`src/client/graphics/entity_renderer.cpp`'s camera-height accessors, and
tests.**

Do not change T74's arm filter. Do not touch equipment placement,
`bone_attachment.h`, or the spell path. `refactor/deferred.md` is
append-only.

Read `refactor/EXECUTOR_PROTOCOL.md` (**note the new daylight directive in
the media section**), then **`refactor/T75_first_person_arm_visibility.md`'s
Review and `/mnt/projects/fable/review/T75/README.md`** — T75 found the
underlying bug and left this specific problem.

## The state you are inheriting, and what is already settled

Do not re-litigate any of this:

- **T74** built the arm-geometry filter and verified it across 7
  race/gender combinations. It is correct and off limits.
- **T75** found the real reason nothing was visible:
  `getPlayerEyeHeightFromFeet()` used `bbox.MaxEdge.Y - bbox.MinEdge.Y`,
  but the bbox is `Y: [-3.00, 3.65]`, so it added a whole body-height. The
  camera sat a full body above the model's top. Fixed, de-duplicated out
  of `getPlayerHeadBonePosition()`, and a third duplicate camera-placement
  copy consolidated into `repositionFirstPersonCamera()`.
- T75 also added `kFirstPersonPitchDownRad` (12°) on the look target, and
  established the working daylight command.

**The remaining problem is standoff.** See
`review/T75/T75_idle_after_firstperson_1_arm_geometry_in_frustum_800x600.png`
and the swing still: geometry is now inside the frustum but fills the
lower half of the frame as an unrecognisable mass. **The camera is not
looking at arms; it is inside them.**

## What to investigate — measure, do not guess

T75 left a headless measurement test,
`ArmVsCameraGeometryMeasurement_Headless`
(`tests/test_inventory_model_view.cpp`). **Extend it rather than starting
over** — it already reports arm bone world positions against camera
position across idle and five attack animations.

Establish numerically:
- The **distance** from the camera to the nearest arm geometry, not just
  the vertical relationship. That is the number this unit exists to fix
  and nobody has measured it.
- Whether arm geometry is **behind or straddling the near plane**. If
  triangles intersect the near plane you get exactly the smeared mass the
  captures show, and no amount of pitch will help.
- What the **near plane distance** actually is for this camera.

Candidate levers, in order of least change:
1. **Camera height** — T75 used `bbox.MaxEdge.Y` (3.65, the top of the
   head). The skeleton's own head bone is at **3.10**. Eye height should
   plausibly be at or slightly below the head bone, not the crown; that
   alone moves the camera and may be the whole fix.
2. **Forward offset** — push the camera slightly forward of the model
   origin so the torso and upper arms fall behind it.
3. **Near plane** — only if the geometry genuinely cannot be cleared
   otherwise; changing it affects all rendering and is the riskiest lever.
4. Pitch adjustment on top of T75's 12°.

**State what you chose and what you rejected, with the measurements behind
each.** Prefer the smallest change that produces a recognisable result.

## Constraints

- **Third person must not change.** Gate on the `FirstPerson` branch,
  prove it with the visual gate and an identical before/after capture.
- **Every tunable is a named constant with a comment** explaining what it
  compensates for — T75 established that pattern with
  `kFirstPersonPitchDownRad`; follow it. A bare number here will be
  unexplainable in a month.
- Do not undo T75's bbox fix. If something looks wrong, it is standoff,
  not height arithmetic — that part is measured and tested.

## Visual acceptance — this is the deliverable

**Force daylight first** (`/say #set time 11 0`, per the protocol) and
confirm the client survives before building the sequence.

Will needs to see whether arms are now recognisable. Capture:
- **idle, before and after**, identically framed,
- **a swing, before and after** — use a GM-spawned non-dying dummy, the
  approach T75 proved works,
- **third person before/after**.

**Equip a weapon if you can.** T75's `/equip` attempts failed server-side
("The selected slot was invalid") so its swing was unarmed — sibling unit
T77 is fixing exactly that, but it will not have merged in time. If
`/equip` still fails, fall back to the documented direct-DB approach T63
established, restore afterwards, and say so. An armed swing is what Will
actually needs to judge.

Media to `/mnt/projects/fable/review/T76/` with a README pairing
before/after. Describe, do not grade.

## STOP conditions

- The measurement showing arm geometry cannot be cleared without changing
  the near plane — report before doing it; that affects all rendering.
- Third person or the paperdoll changing.
- Any urge to modify T74's filter or T75's bbox fix.
- `.eqemu-lock` held.

## Verification

Full protocol bar, plus the distance/near-plane measurements, the
before/after captures, and third-person regression evidence.

## Review

Full media/evidence: `review/T76/README.md`.

### Measurement (headless, extended `ArmVsCameraGeometryMeasurement_Headless`)

Extended T75's test to walk the arm-FILTERED instance mesh's own triangle
VERTICES (not just bone joints — what the GPU actually rasterizes). Near
clip = 1.0 (`setupCamera()`). In EVERY sampled pose (idle + 5 attacks), arm
triangle vertices straddle the near plane simultaneously (e.g. idle
Z=[-1.06, 1.11], 1H-slash Z=[-1.02, 2.18], hand-to-hand Z=[-0.04, 2.15]) —
exactly the plan's predicted failure mode, confirmed by measurement. Tested
5 candidate camera pullback distances (0, 0.2, 0.3, 0.5, 1.0) against the
measured vertex data: **no candidate eliminates the straddle for any pose**
— the mesh's own depth span (~2-3 units) exceeds what any camera-position
lever can clear while the camera stays near the body. At pullback=1.0,
every pose's near-side vertex enters the `[-0.1, near)` perspective-divide
danger band — often worse than doing nothing. Full numbers:
`review/T76/T76_arm_vs_camera_measurement_output.txt`.

### Lever chosen: standoff pullback (0.3, direction opposite the plan's guess) + live head-bone height; near plane NOT touched

1. **`kFirstPersonCameraStandoffUnits = 0.3f`** (`irrlicht_renderer.cpp`) —
   pulls the camera BACK along the look axis. DEVIATION: the plan suggested
   pushing FORWARD; measured, that's the wrong sign — several poses' forward
   reach barely clears the near plane already (idle max=1.106, 2H-slash
   max=1.035, near=1.0), so forward would make them fully invisible.
   Backward increases clearance instead. 0.3 is the largest of the 5 tested
   candidates keeping every armed-swing pose's near-side vertex out of the
   danger band while giving the marginal cases real headroom (idle max
   1.106→1.406, 2H-slash max 1.035→1.335). Confirmed live in every capture's
   log (`standoff=0.30`).
2. **Live HEAD-bone eye height** (`EntityRenderer::getPlayerEyeHeightFromFeet()`)
   — the plan's concrete lead: prefers the skeleton's live HEAD bone Y
   (measured 3.10) over T75's static bbox top (3.65), falling back to the
   bbox if no HEAD bone resolves. Headless measurement confirms it
   re-centers idle's vertical frustum window onto the arm's actual Y range
   (top-edge delta -0.53 → +0.03). **Not confirmed live in these captures**
   — the captured sessions logged the bbox fallback (3.65), not 3.10; see
   Board note.

**Rejected**: pushing forward (plan's literal wording, wrong sign,
measured); near plane reduction (the only lever that fully resolves the
straddle, but the plan's own STOP condition — affects all rendering,
reported not implemented); further pitch beyond T75's 12° (pitch cannot
fix a depth/Z straddle).

### STOP point (near clip plane) — reported, not implemented

Measurement is unambiguous: no camera-position lever clears the straddle
for every pose while the camera stays near the body. Full resolution needs
a smaller near clip (`setupCamera()`, `irrlicht_renderer.cpp:1517`,
currently 1.0f) — affects ALL rendering. Per the plan's explicit STOP
condition, not changed; reported for Will/next unit's decision.

### Idle / swing — before/after, armed swing achieved

Both idle and swing show the same qualitative jump: T75's dark/blurred
unrecognizable mass becomes a recognizable (though still close-up)
head/hair-region view with visible features (hair strands, eyebrow, eye).
Not full "arms and weapon at comfortable distance" — consistent with the
straddle finding above.

**Armed swing**: `/equip 23 13` reproduced T75's exact server-side failure
(`"The selected slot was invalid."`) fresh in this unit's own session (not
just cited). Fell back to T63's documented direct-DB approach: item 1062
(Kerran Fishingpole, the same item T74 confirmed renders correctly) written
directly into `inventory` primary slot 13, confirmed via `SELECT`, BEFORE
launching the capture client (avoids T75's live-resync gap). All captures
in this unit are with an equipped weapon. Dummy target (`#npctypespawn
77002`, T75's proven approach) + `/target dummy` + `/attack`, log-confirmed
`Auto attack ON` sustained for the full 14s window in both before/after.

### Third person — regression evidence

Before/after frames visually indistinguishable, as expected: every changed
line sits behind `cameraMode_ == CameraMode::FirstPerson` or is an accessor
only the first-person path consumes. Visual gate PASS, goldens untouched.

### Constants introduced

- `kFirstPersonCameraStandoffUnits = 0.3f` (`irrlicht_renderer.cpp`) — the
  standoff pullback, this unit's core deliverable.
- No new constant for the head-bone height lever — it's a live bone lookup
  with a documented fallback, not a tunable scalar.

### Verification

1. `cmake --build build -j24` — clean.
2. `ctest --test-dir build` — 2043/2044, sole failure the documented ALSA
   host waiver (`AudioManagerTest.PreloadCommonSounds`), unchanged from
   baseline. `review/T76/T76_ctest_summary.txt`.
3. `scripts/build-gles2-x86.sh` — clean.
4. Visual gate PASS, goldens untouched (`review/T76/T76_visual_gate_output.txt`).
5. `scripts/build-arm-noble.sh` — clean cross-compile (x86 only per plan;
   no Pi run).
6. `check_file_sizes.sh --files ...` (run via `--files` with absolute paths
   since the harness's cwd isn't the worktree and `git rev-parse
   --show-toplevel` can't resolve `fable` from there) —
   `entity_renderer.cpp` FAILs at 3341 lines, pre-existing (3321 on `fable`
   before this unit, itself already over the ceiling per T75; net
   contribution +20 lines). `irrlicht_renderer.cpp` lands EXACTLY at the
   3000-line WARN threshold after several rounds of trimming this unit's own
   comment additions — deliberately kept at WARN, not pushed into a NEW
   FAIL. `review/T76/T76_check_file_sizes_output.txt`.

### Teardown

`willeq_test`/`Testchar`: inventory restored (primary slot 13's staged
weapon deleted, general slots 23-30 confirmed unchanged at item 12654 —
confirmed via `SELECT`), position restored to the documented baseline
(`zone_id=36 x=-77.9232 y=21.9086 z=3 heading=190`, confirmed via
`SELECT`). Server: clean shutdown via `down.sh` (confirmed), lock file
deleted (confirmed via `ls` failing).

### Deviations from the plan

1. **Standoff direction opposite the plan's literal wording** — measured,
   documented in-code and above.
2. **Near clip plane not touched** — plan's own STOP condition, measured
   necessary for full resolution, explicitly deferred.
3. **Head-bone lever's live benefit not confirmed in these captures** — the
   headless measurement supports it; the captured sessions show the bbox
   fallback value in the log instead. See Board.
4. **Server was brought down and back up mid-session** (docker compose
   `down`/`up`, data volume persists, confirmed via `SELECT` before and
   after) after the client stopped progressing past login under heavy
   `--log-level=DEBUG` log volume compounded by CPU contention from a
   concurrent sibling unit's build in another worktree slot — not a defect
   in this unit's code (confirmed by a clean `--log-level=INFO` run
   succeeding immediately after).

Board: **Camera geometry, for the next person** — height source: live HEAD
bone Y (headless value 3.10, human male), falls back to `bbox.MaxEdge.Y`
(3.65) if no HEAD bone resolves. XZ standoff: 0.30 units backward along the
look axis. Pitch: 12° down on the look target only (T75, unchanged). Near
clip: 1.0, UNCHANGED and the strongest remaining candidate for a real fix
— this unit's measurement shows no position/pitch combination clears the
straddle while it stays this large. **Live head-bone timing**: the captured
sessions show the bbox fallback firing instead of the head-bone value even
well after zone-in; a quick diagnostic (temporary DEBUG logging, removed
before commit) was inconclusive under this session's own log-volume/CPU-
contention stalls — re-run with a quiet server/CPU budget to root-cause.
**Idle-vs-swing**: still not closed — both now show the same qualitative
improvement (recognizable vs. unrecognizable), neither is unambiguously
"arms and weapon at comfortable distance," consistent with the straddle
finding. Four units have now touched this area and each found a defect the
previous framing hid; the near clip plane is the next, most likely
candidate.
