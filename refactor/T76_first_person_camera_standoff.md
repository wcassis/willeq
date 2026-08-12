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

(executor fills: the measurements; the lever chosen and those rejected,
with numbers; the constants introduced; whether an armed swing was
achieved and how; third-person evidence; capture paths; teardown;
deviations)

Board: record the final camera geometry — height source, offsets, pitch —
so the next person does not re-derive it. Three units have now touched
this area and each found a defect the previous framing hid.
