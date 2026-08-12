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

### Stage 1 — diagnostic result (checkpoint; stage 2/3 findings follow in a later commit)

**Method.** Temporary throwaway hack in `EntityRenderer::setPlayerEntityVisible()`
(`kT74Stage1Diagnostic`, still in this commit, reverted before stage 2/3):
when hiding the player for first person, keep `primaryEquipNode`/
`secondaryEquipNode` visible instead of hiding them with the rest of the
player. Also added temporary `LOG_INFO(MOD_ENTITY, "T74-DIAG ...")`
instrumentation to `EntityRenderer::updateEquipmentTransforms()` (also
reverted before stage 2/3) to print the resolved bone index, local/absolute
weapon-node position, and visibility flags for the player's primary weapon
node every frame. eqemu live, `qeynos2` (lit outdoor zone — daylight at
zone-in, though the simulated clock drifted to night partway through later
runs; noted, not chased, since it did not affect what the diagnostic needed
to see), account `willeq_test`, character `Testchar` (race=1 Human,
gender=0, class=1 Warrior, level 2). `/equip` (T66/T71) used for every
equip in this stage — no direct item-slot DB writes were needed for the
equip action itself; direct DB writes were used only for staging a weapon
into a general inventory slot first (see finding 1) and for character
position (both sanctioned, same as T63/T65's precedent), never for the
equip action. `/camera` (T66) used to enter first person and
`getCameraModeString()`'s chat echo confirmed the mode on-screen. Command
tour via `--script` (H03), frames via `--dump-frames-dir`/`--dump-every`
(T64).

**Finding 1 (test-setup bug, not a placement bug): item 6011 (Rusty Mace)
has no entry in `data/item_models.json`.** The first several diagnostic
captures (against item 6011) showed nothing at all in first person even
with the stage-1 hack forcing equipment nodes visible. Root-caused by
adding the temporary logging above and by tracing `attachEquipment()`:
its `LOG_DEBUG "attachEquipment: Entity N (Testchar) primary=6011
secondary=0"` entry line fired, but the follow-up `"Attached primary
equipment ... to entity N"` confirmation line never appeared —
`equipmentModelLoader_->getEquipmentMesh(6011)` returned `nullptr`
because `6011` is outside `item_models.json`'s populated key range
(500-8456, sparse, 3661 entries; confirmed by direct lookup — no `"6011"`
key). No `primaryEquipNode` was ever created, so
`updateEquipmentTransforms()`'s own `if (!visual.primaryEquipNode &&
!visual.secondaryEquipNode) return;` short-circuited it every frame — the
diagnostic hack had nothing to make visible. This was purely my test item
choice, not a code defect; switched to item 5140 (Iron Cudgel of the
Petitioner, itemtype 3 blunt, present in `item_models.json`) for the rest
of stage 1.

**Finding 2 (real bug, in-territory, folded into stage 3's "wire it"
work rather than a separate STOP): entering first person via
`/camera firstperson` does not itself move the camera.** With item 5140
equipped and confirmed attached (`attachEquipment` completed, node
created), the T74-DIAG logging showed the weapon transform math is sane:
`updateEquipmentTransforms(primary)` resolved `raceCode=HUM boneIndex=15`
(a real R_POINT-family bone, 25 bones total on the HUM skeleton) and
computed `localPos=(0.62,1.51,-0.83)` relative to the player's own scene
node, i.e. `absPos≈(428.62,5.51,-74.83)` against a player world position
of `(428,4,-74)` — a small, plausible hand-offset, not a wild or NaN
value. Despite that, the captured first-person frames still showed
nothing: static, unmoving views that turned out to be the **stale
third-person Follow camera transform**, not a first-person one. Root
cause, read directly from `irrlicht_renderer.cpp`: `setCameraMode()`
(:2637) and `cycleCameraMode()` (:2647) only set `cameraMode_` and call
`entityRenderer_->setPlayerEntityVisible(...)` — neither repositions
`camera_`. The ONLY places that ever move the camera to a first-person
eye position are `setPlayerPosition()` (fires on server position
updates) and `setPlayerSpawnId()` (fires once at zone-in, and only helps
if `cameraMode_` already equals `FirstPerson` at that moment). A
stationary player who issues `/camera firstperson` therefore keeps
looking through whatever the Follow camera's transform happened to be —
confirmed by grepping the full run log for the two log lines that would
prove a first-person reposition happened
(`"Camera mode=FirstPerson"`, `"First-person camera:"`) and finding
zero occurrences across the entire session, even though `/camera
firstperson`'s own chat echo confirmed the mode flag flipped. The
Follow camera's own log line for this run
(`"[ZONE-IN] ... preferredDist=20.00 actualDist=20.00 firstPerson=false
... Camera pos: Irrlicht(428.00,7.00,-94.00)"`) sits ~19-20 units away
from the weapon's resolved absolute position — matching `actualDist=20`
almost exactly, i.e. the frames were the frozen 20-unit-back follow shot,
not a first-person one, which is why nothing recognizable was in frame.

**Verdict: not a STOP.** The weapon transform math itself is not shown to
be wrong (finding 2 shows sane, plausible numbers once a real weapon mesh
was attached) — the failure to observe it was a test-item bug (finding 1)
compounded by a genuine but in-territory, fixable gap in
`setCameraMode()`/`cycleCameraMode()` (finding 2), exactly the file/
functions this unit's territory already names for stage 3. Folding the
camera-reposition fix into stage 3 rather than treating this as its own
STOP/unit, since fixing it is required to visually verify stage 2's
geometry filter at all (without it, no first-person capture can ever show
anything, arms or otherwise, until the player happens to move). Stage 2/3
implementation and their own verification (arm-bone table, fallback,
`triggerFirstPersonAttack()` disposition, third-person regression
evidence) follow in a later commit on this branch; the throwaway stage-1
hack code (`kT74Stage1Diagnostic` and the `T74-DIAG` logging) is reverted
there.

Board: this closes **instance 9** of the "implemented but no producer"
pattern — and note in that entry that it was the first instance where the
production code was hollow rather than merely unreachable, since that
distinction is what made "just add the callers" wrong.
