# T74 — Implement first-person arm and weapon rendering

**Slot: t1. OWNS EQEMU for the acceptance captures. x86 only, no Pi.**

**Territory: `src/client/graphics/eq/animated_mesh_scene_node.cpp` +
its header (the arm filtering), `src/client/graphics/entity_renderer.cpp`
(`setPlayerFirstPersonMode`, `setPlayerEntityVisible`),
`src/client/graphics/irrlicht_renderer.cpp` (`setCameraMode` /
`cycleCameraMode` wiring), and tests.**

Do not touch `bone_attachment.h` or equipment *placement* — T60/T65
settled it and this unit consumes it. Do not touch the spell/visual path.
`refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T71_equip_fix_and_first_person.md`'s Review (it established that
nothing of the player renders in first person and why), then
`refactor/T65_secondary_weapon_invisible.md`'s Review (the last unit to
work on player equipment rendering, and a caution about bone-name
heuristics).

## The premise correction — read this first

The obvious framing of this unit is "the three first-person functions have
no callers, so add the callers." **That is wrong and would produce nothing
visible.**

Verified before this plan was written:

```
firstPersonMode_   (animated_mesh_scene_node.cpp) — written :1229, :1233. READ: nowhere.
armBoneIndices_    — cleared :1245, assigned :1346, size logged. READ: nowhere.
```

`setPlayerFirstPersonMode()` delegates to
`animatedNode->setFirstPersonMode()`, which sets a flag and builds an arm
bone list that **nothing consumes**. `triggerFirstPersonAttack()` is a
logging no-op by its own comment. `updateFirstPersonWeapons()` only calls
`updateEquipmentTransforms()`.

So the feature is a shell with a plausible interface — which is why it
survived review for so long. **The work is implementing arm-only
geometry, not connecting calls.** Will chose this over the cheaper
alternatives.

## What makes it tractable

- **EQ uses rigid vertex-piece binding** — `applySkinning` operates "based
  on vertex pieces" (`s3d_loader.h:310`), so each vertex belongs to
  exactly one bone. Filtering is a selection problem, not weight blending.
  **Confirm this holds for the models you test** before designing around
  it.
- **The player is already excluded from composite rendering** —
  `entity_renderer.cpp:546`, `compositeEligible = !isPlayer && ...`. The
  player uses the per-part/multi-buffer path, which is the easier one to
  filter. You do not have to solve this for composite entities.
- **Weapons are separate scene nodes** (`primaryEquipNode`,
  `secondaryEquipNode`), hidden today only because
  `setPlayerEntityVisible()` (`entity_renderer.cpp:2698-2723`) hides
  everything at once.

## Stage 1 — the diagnostic, before building the filter

**Show the weapons alone: hide the body mesh and name tag, keep the
equipment nodes visible.** This is a throwaway diagnostic, not a shipping
outcome — floating weapons with no arms will read as broken.

Its purpose is to answer a question that arm filtering cannot fix if the
answer is bad: **are the weapon transforms even correct when the camera
sits at the player's own position?** Capture a swing and look. If the
weapon is behind the camera, inside the head, or oriented wrongly, that is
a placement problem to understand *now* rather than after building the
filter.

T65 found this codebase's hand-bone offsets wrong in a way nobody noticed
for months. Do not assume they are right just because third person looks
correct — third person views them from outside at distance.

**Commit stage 1's findings before starting stage 2**, and say plainly
what the diagnostic showed.

## Stage 2 — arm-only geometry

Make `firstPersonMode_` and `armBoneIndices_` actually do something: when
first-person mode is on for the player's node, render only geometry bound
to arm bones.

Where that filter belongs — instance-mesh generation
(`createInstanceMesh()`) versus draw time — is yours to determine from the
code; state the choice and why. Prefer whichever keeps third-person and
every other entity **provably untouched**.

**The arm bone heuristic is the most likely thing to be wrong.**
`buildArmBoneIndices()` selects bones by name substring — `"ARM"`,
`"POINT"`, `"DAG"`, `"SHIELD"` — plus their children. That is the same
class of fragile per-race name matching that T60 had to converge and T65
had to correct. **Verify it against real skeletons for several races and
both genders**, and report per race what it selected. A heuristic that
picks nothing gives an invisible player; one that picks too much gives a
floating torso. Both are worse than the current behaviour.

**Decide and state the fallback**: if a race's skeleton yields no usable
arm bones, first person should fall back to hiding the player entirely
(today's behaviour) rather than rendering something wrong. Make that
explicit and logged, not accidental.

## Stage 3 — wire it, narrowly

`setCameraMode()` / `cycleCameraMode()` currently call
`setPlayerEntityVisible(false)` for `FirstPerson`
(`irrlicht_renderer.cpp:2643, :2662`). Replace that with the
first-person path.

Constraints:
- **Only the player, only in first person.** Nothing about third person,
  other entities, or NPCs may change.
- **The paperdoll must not be affected.** `CharacterModelView` builds
  nodes of the same type; if the mode is per-node this is safe, but
  confirm it and add a test if the seam allows one.
- `triggerFirstPersonAttack()` is a no-op whose own comment says attack
  animation arrives via `setEntityAnimation()`. **Either give it a real
  job or delete it** — do not leave a third hollow function behind. Say
  which you chose.

## Visual acceptance

**Will judges. This is temporal — record video, not stills** (T64:
`--dump-frames-dir` / `--dump-every` / `--dump-max-frames` +
`scripts/ci/encode_capture.sh`; keep the measured-fps file with each
video, and remember capture fps is an artifact, never a performance
claim).

Capture in a **lit** zone — T63 wasted a session on a near-black one:
- stage 1's weapons-only diagnostic,
- first person with a primary weapon, swinging at a live target,
- first person dual-wielding (T65's secondary-weapon fix has never been
  seen in first person),
- **third person before and after**, captured identically, proving it is
  unchanged.

Media to `/mnt/projects/fable/review/T74/` with a README. Describe what
each shows; do not grade it.

Use `/camera` (T66) to enter first person and assert the mode from
`getCameraModeString()` rather than assuming, and `/equip` (T66, fixed by
T71) to equip — you should not need direct DB writes for equipment. If you
do, that is a finding.

## STOP conditions

- Stage 1 showing weapon transforms are wrong in first person — report
  before building the filter; that may be its own unit.
- The arm heuristic proving unreliable across races in a way that needs a
  design decision.
- Third person or the paperdoll changing in any way.
- `.eqemu-lock` held.

## Verification

Full protocol bar. Additionally: the per-race arm-bone selection table;
proof third person is unchanged (before/after captures **and** the visual
gate); the fallback behaviour demonstrated; and the disposition of
`triggerFirstPersonAttack()`.

## Review

(executor fills: stage 1's diagnostic result; where the filter landed and
why; the per-race heuristic results; the fallback; what happened to the
no-op function; third-person regression evidence; capture paths;
teardown; deviations)

Board: this closes **instance 9** of the "implemented but no producer"
pattern — and note in that entry that it was the first instance where the
production code was hollow rather than merely unreachable, since that
distinction is what made "just add the callers" wrong.
