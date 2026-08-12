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

(executor fills: the measurements; which lever was chosen and what was
rejected; how the swing was made capturable; the constant(s) introduced;
third-person regression evidence; capture paths; teardown; deviations)

Board: record the idle-vs-swing question and which Will chose once he
judges, so the next person does not re-derive it.
