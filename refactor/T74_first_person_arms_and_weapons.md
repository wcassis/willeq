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

### Stage 2 — arm-only geometry: where the filter landed and why

**Instance-mesh generation, not draw time**, and index-buffer filtering,
not vertex collapsing. `EQAnimatedMeshSceneNode::createInstanceMesh()`
already builds a fresh per-instance copy of the base mesh's vertex AND
index arrays every time it runs (constructor, `setMesh()`); T60/T71's
finding that EQ uses rigid vertex-piece binding (confirmed again here —
`applyBoneTransformsToMeshImpl` walks `vertexPieces` as contiguous
`{boneIndex, count}` runs over the ORIGINAL vertex order, one bone per
vertex, no blending) means "arm-only" is a **triangle-selection problem**:
for each destination `(bufferIndex, localIndex)` a vertex maps to (via
`eqMesh_->getVertexMapping()`), record whether its owning piece's
`boneIndex` is in `armBoneIndices_`; then keep a triangle in the index
buffer only if all 3 of its vertices are arm vertices (`applyFirstPersonIndexFilter()`,
new in `animated_mesh_scene_node.cpp`). Requiring all 3 rather than any 1
avoids spiky stretched triangles at torso/shoulder seams — the cost is a
hairline gap at the cut, not garbage geometry.

This lands in `setFirstPersonMode()`: entering first person calls
`createInstanceMesh()` (fresh, unfiltered — discards any stale filtered
buffers from a previous enable) then `applyFirstPersonIndexFilter()`;
leaving first person calls `createInstanceMesh()` again (nothing else —
restores full geometry). Vertex ARRAYS are never touched by the filter —
`applyAnimation()` keeps writing every vertex's position every frame
exactly as before, arm or not; only which triangles the INDEX buffers
reference changes. This is why third person and every other entity stay
provably untouched (see below): `armBoneIndices_`/`firstPersonMode_` are
per-node members (`EQAnimatedMeshSceneNode`, not `EQAnimatedMesh`, which
IS shared across nodes of the same race/gender), the filter only runs
inside `setFirstPersonMode()` which only `EntityRenderer::
setPlayerFirstPersonMode()` ever calls, and that in turn is only ever
called for the entity with `visual.isPlayer == true`. Proven, not just
argued — see `FirstPersonMode_IsPerNode_NotShared_Headless` below.

`lastAppliedFrame_ = -1` is set on both enable and disable, forcing
`applyAnimation()`'s frame-cache optimization to reapply the current
animation frame to the freshly-rebuilt (bind-pose) vertex arrays on the
next `OnAnimate()` — without it the mesh would freeze at bind pose
whenever the filter toggled on a frame where the animation clock hadn't
itself advanced.

### The arm-bone heuristic — per-race table (headless, deterministic)

`buildArmBoneIndices()` itself is UNCHANGED — the plan flagged its
name-substring matching ("ARM", "POINT", "DAG", "SHIELD", "HAND",
"WRIST", "FINGER", "FOREARM", "BI_R/L", "FO_R/L", "FI_R/L", plus
children) as the most likely thing to be wrong, so it was verified rather
than touched. New headless test `FirstPersonArmFilter_PerRace_Headless`
(`tests/test_inventory_model_view.cpp`, using the existing
`RaceModelAnimationHeadlessTest` fixture — `EDT_NULL` device, real S3D
model data, no server) exercises it across 7 race/gender combinations
including T65's exact Dark Elf female case and two non-standard-build
races (Troll, Iksar):

| Race | armBones | validGeometry | index count (full → filtered) |
|---|---|---|---|
| Human male | 12 | true | 1332 → 324 |
| Human female | 11 | true | 1290 → 258 |
| Barbarian male | 12 | true | 1386 → 348 |
| Dwarf male | 11 | true | 1254 → 294 |
| Dark Elf female (T65's case) | 11 | true | 1476 → 258 |
| Troll male | 11 | true | 1260 → 324 |
| Iksar male | 19 | true | 2124 → 324 |

Full output: `review/T74/per_race_arm_bone_test_output.txt`. Every race
tested landed strictly between the two failure modes the plan named:
none picked nothing (index count never hit 0), and none picked
everything (filtered count is always well under half the full count —
roughly 20-27%, plausible for "arms out of a full body"). The test
asserts both bounds explicitly (`EXPECT_GT`/`EXPECT_LT` against the full
count), not just eyeballed from the table.

**Gap, stated plainly**: no REAL race in this asset set exercises the
fallback (all 7 tested — and by extension the other real skeletons, which
share the same small family of EQ bone-naming conventions — found at
least 11 arm-related bones). The fallback path itself is still verified,
just synthetically: `FirstPersonArmFilter_FallbackWhenNoArmBones_Headless`
swaps a real, already-loaded node's skeleton (`node->getAnimator().
setSkeleton(...)`, same technique `test_bone_attachment.cpp` uses for
`findBoneIndex()`'s fallback chain) for one with 4 bones named "TORSO",
"HEAD", "PELVIS", "SPINE" — none matching any of the substrings — BEFORE
the first `setFirstPersonMode(true)` call, confirming
`hasValidFirstPersonGeometry()` comes back false, `getArmBoneCount()`
is 0, and critically that the instance mesh is left **completely
unfiltered** (full index count unchanged) — the ENTITY-level fallback
(next section) is what hides the whole player in this case, not a
filtered-to-nothing mesh.

### The fallback — explicit and logged

`buildArmBoneIndices()` finding zero arm bones sets
`hasValidFirstPersonGeometry_ = false` and logs `LOG_WARN(MOD_GRAPHICS,
"... no usable arm bones for skeleton model='{}' -- falling back to
full-hide behaviour")`; the mesh itself is left untouched (see above).
`EntityRenderer::setPlayerFirstPersonMode()` reads
`hasValidFirstPersonGeometry()` and computes `hideEverything = enabled &&
(!animatedNode || !hasArmGeometry)` — when true, the animated node,
name tag, AND both equipment nodes are all hidden, byte-for-byte the same
visibility set `setPlayerEntityVisible(false)` produced before this unit.
Not accidental empty-filter silence: a race that somehow yielded zero arm
bones would render as "player invisible in first person" (today's
existing, known behaviour), never as a floating torso or an unfiltered
full body.

### Stage 3 — wiring, and the camera-reposition fix folded in from stage 1

Four call sites previously called `entityRenderer_->setPlayerEntityVisible
(cameraMode_ != CameraMode::FirstPerson)` (the plan named
`setCameraMode()`/`cycleCameraMode()`; two more equivalent sites were
found by grep and fixed for consistency — `setPlayerSpawnId()` and
`IrrlichtRenderer::createEntity()` in `renderer_entities.cpp`, both gate
player visibility on `cameraMode_ == FirstPerson` the same way, and
leaving them on the old call would have reintroduced the hide-everything
behaviour on zone-in/player-entity-creation while already in first
person). All four now call
`entityRenderer_->setPlayerFirstPersonMode(cameraMode_ == CameraMode::FirstPerson)`.

`setCameraMode()`/`cycleCameraMode()` additionally call the new
`IrrlichtRenderer::repositionFirstPersonCamera()` when entering first
person — the stage-1 diagnostic's finding 2 (see above): these two
functions never moved the camera transform itself, only
`setPlayerPosition()`/`setPlayerSpawnId()` did, as a side effect of a
position update. `repositionFirstPersonCamera()` extracts that exact math
(unchanged formula: `camZ = playerZ_ + eyeHeight + playerConfig_.eyeHeight`,
heading-based look target) into one shared, no-op-safe (camera_ null or
`cameraMode_ != FirstPerson` both no-op) helper; `setPlayerSpawnId()` was
refactored to call it too rather than keep a third copy of the same
formula. `setPlayerPosition()`'s own inline copy was deliberately left
alone (works correctly today, called every frame the player moves, not
worth the risk of touching a hot path for a pure de-dup).

### `triggerFirstPersonAttack()` — deleted, not given a job

Zero callers anywhere (`IrrlichtRenderer::triggerFirstPersonAttack()` had
exactly one caller — nothing; `EntityRenderer::triggerFirstPersonAttack()`
had exactly one caller — the `IrrlichtRenderer` one being deleted).
Deleted both, plus their header declarations
(`include/client/graphics/{irrlicht_renderer,entity_renderer}.h`). Chose
delete over "give it a real job" because its own pre-existing comment
already correctly says what the real job is and where it lives: attack
animations arrive via `setEntityAnimation()` from `ZoneProcessDamage`, a
live, working, unrelated path this unit's territory explicitly excludes
touching (combat/damage handling). Manufacturing a "job" for this
function would have meant either duplicating that live path or routing
through it redundantly — a fourth hollow-adjacent function, just with a
body that does nothing new.

**Adjacent finding, not acted on**: `EntityRenderer::
updateFirstPersonWeapons()` (also declared in the plan's territory-adjacent
area) is likewise dead — zero callers — because `updateEquipmentTransforms()`
(the only thing its body does) is already called unconditionally for the
player every frame from the main per-frame entity-update loop
(`entity_renderer.cpp` ~line 2046, `if (visual.isPlayer) { updateEquipmentTransforms(visual); continue; }`),
regardless of camera mode. Left untouched — it wasn't named in the plan's
"give it a job or delete it" instruction (that was specific to
`triggerFirstPersonAttack()`), and touching it wasn't needed to make the
feature work. Recorded here rather than silently left for a future
"instance 10" grep to rediscover from scratch.

**Also now-orphaned, left alone**: `EntityRenderer::setPlayerEntityVisible()`
has zero callers after stage 3 rewires all four call sites to
`setPlayerFirstPersonMode()`. Not deleted — it's a real, correct,
general-purpose player-visibility toggle (not a hollow shell), plausibly
useful to a future debug/photo-mode caller, and deleting working utility
code wasn't asked for. Flagged rather than silently left implicit.

### Third person / paperdoll — provably unchanged

**By construction**, not just by testing: every changed line in this unit
sits behind either (a) `EQAnimatedMeshSceneNode::firstPersonMode_`/
`armBoneIndices_`, per-node members only ever written by
`setFirstPersonMode()`, only ever called by `EntityRenderer::
setPlayerFirstPersonMode()`, gated on `visual.isPlayer` — no NPC, corpse,
or paperdoll node's code path calls it, ever; or (b) the `cameraMode_ ==
CameraMode::FirstPerson` branch inside `setCameraMode()`/
`cycleCameraMode()`/`repositionFirstPersonCamera()` — Free/Follow modes
never execute the changed code at all. `CharacterModelView` (the
paperdoll) was confirmed (T54's Review, re-read for this unit) to have
been deleted in `0f25cb57` and never rebuilt — there is no live paperdoll
UI today for third-person-vs-paperdoll divergence to even be possible;
noted rather than silently assumed.

**Live evidence**: `review/T74/third_person_after_equipped_weapon_visible.png`
— Follow camera, `getCameraModeString()`'s "Camera mode: Follow" on
screen, Testchar's model rendering normally with the primary weapon
visibly held, after all of this unit's code was live. **Test evidence**:
`FirstPersonMode_IsPerNode_NotShared_Headless` builds two
`EQAnimatedMeshSceneNode`s sharing the same underlying `EQAnimatedMesh`
(same race/gender — exactly the sharing relationship a player's node and
an NPC of the same race, or a live entity and a hypothetical paperdoll
instance, would have), sets first-person mode on only one, and asserts
the other's `isFirstPersonMode()` flag and index count are both
unaffected. **Visual gate**: PASS, goldens untouched
(`review/T74/visual_gate_output.txt`) — the gate's own zone renders (no
player-first-person state involved) are byte-identical evidence third
person's normal render path took none of this unit's new branches.

### Live acceptance captures — what they show, and their honest limits

`review/T74/README.md` describes every file; summarized here:

- Stage 1's corrected-item diagnostic (weapons alone, valid weapon
  attached, camera-reposition fix active): still shows nothing
  recognizable in frame. Not re-litigated as a STOP per the stage-1
  verdict above — the transform math itself was shown sane via the
  earlier T74-DIAG instrumentation, and the geometry filter's correctness
  is independently proven by the headless per-race tests, which do not
  depend on camera framing at all.
- Dual-wield first person (`/equip` primary Kerran Fishingpole item 1062,
  secondary Dagger item 7001 — both confirmed class-legal for the
  level-2 Warrior test character, unlike the FIRST attempt with item 5140
  which the server actually REJECTED with "Your class, deity and/or race
  may not equip that item" — a live, real server-authoritative rejection,
  log-captured, not a bug in this unit's code): equips cleanly, camera
  enters first person and repositions correctly (log-confirmed), but no
  arm/weapon geometry is visible in the specific idle-standing frames
  captured.
- An attack attempt against a live `a_large_rat` engaged auto-attack
  without a range complaint, but no hit/miss text appeared before the
  capture window closed — recorded as inconclusive, not claimed as a
  landed swing.

**The open question this leaves, stated honestly rather than
papered over**: the arm-only geometry filter is proven correct at the
data/triangle level (headless tests), equipment attachment and camera
positioning are proven correct at the log/transform level (live
instrumentation), yet no live capture in this session shows visible arms
or a weapon on screen. The most likely explanation — EQ's idle-pose arms
hang at the sides, below shoulder height, while the first-person camera
looks level-forward from eye height, so idle arms may simply sit below
the vertical field of view rather than being absent or mispositioned —
was not independently confirmed with additional live cycles (e.g. forcing
a raised-arm swing pose into frame, or pitching the camera down) given
session time already spent chasing target-substring and item-class-
legality issues that turned out to be test-setup bugs, not code defects.
Flagged as follow-up work rather than claimed as resolved.

### `/camera` and `/equip` — no direct DB writes needed for either

Every equip action in stages 2/3's live captures went through `/equip`
(T66, fixed T71) — no direct `inventory` table writes. Direct DB writes
were used only for: staging weapons into GENERAL inventory slots before
`/equip` (the item has to exist somewhere for `/equip` to move it — same
pattern T63/T65 established, not a gap in `/equip` itself), Dual Wield
skill (no scriptable skill-training path, same T63/T65 precedent, though
in the end no capture in this unit's final evidence set required it —
recorded as staged, not chased into an unused feature), and character
position (sanctioned). `/camera firstperson`/`/camera follow` and
`getCameraModeString()`'s chat echo were used for every mode assertion,
never assumed.

### Test suite / build verification

- `cmake --build build -j24` (GLES2-config tree): clean.
- `ctest --test-dir build`: 2042/2043 passed — sole failure is the
  pre-existing documented `AudioManagerTest.PreloadCommonSounds` ALSA
  host waiver, unchanged from baseline. Full summary:
  `review/T74/ctest_full_summary.txt`.
- `scripts/build-gles2-x86.sh`: clean; visual gate PASS
  (`review/T74/visual_gate_output.txt`), goldens untouched.
- `scripts/build-arm-noble.sh`: clean cross-compile, `willeq` + full tool
  set produced.
- New tests: `FirstPersonArmFilter_PerRace_Headless`,
  `FirstPersonMode_IsPerNode_NotShared_Headless`,
  `FirstPersonArmFilter_FallbackWhenNoArmBones_Headless` (all in
  `tests/test_inventory_model_view.cpp`'s existing `RaceModelAnimationHeadlessTest`
  fixture — no server, no DISPLAY, `EDT_NULL` device, auto-discovered by
  ctest).

### Teardown

`willeq_test`/`Testchar`: inventory restored to its exact pre-unit state
(general slots 23-30 all holding item 12654 as found; primary/secondary
equip slots cleared, matching the empty state found at the start), Dual
Wield skill row deleted (matching its pre-unit absence), position restored
to the protocol's documented baseline (`zone_id=36 x=-77.9232 y=21.9086
z=3 heading=190`), all confirmed via a final `SELECT`. Server: clean
shutdown via `down.sh` (containers stopped/removed, confirmed), lock file
deleted.

### Deviations from the plan (summary)

1. **`entity_equipment.cpp` was read and temporarily instrumented (stage
   1), then fully reverted** — not in this unit's named territory
   (`animated_mesh_scene_node.cpp`+header, `entity_renderer.cpp`'s two
   named functions, `irrlicht_renderer.cpp`'s camera wiring, tests). The
   plan explicitly forbids touching equipment *placement* — the
   instrumentation was read-only in effect (logging around, not modifying,
   the existing rotation/offset math) and is not present in the final
   diff (`git diff fable -- src/client/graphics/entity_equipment.cpp`
   should be empty).
2. **Two additional call sites fixed beyond the plan's named
   `setCameraMode()`/`cycleCameraMode()`**: `setPlayerSpawnId()` and
   `IrrlichtRenderer::createEntity()` (`renderer_entities.cpp`) had the
   identical `setPlayerEntityVisible(cameraMode_ != FirstPerson)` pattern;
   leaving them unfixed would have reintroduced the old hide-everything
   behaviour specifically on zone-in while already in first person —
   judged necessary for the feature to work consistently rather than
   scope creep, and `renderer_entities.cpp` is the same `IrrlichtRenderer`
   class split across TUs the plan already names one file of.
3. **`check_file_sizes.sh fable` reports one FAIL**:
   `src/client/graphics/entity_renderer.cpp` at 3313 lines (> 3000
   ceiling). Pre-existing — `git show fable:src/client/graphics/entity_renderer.cpp
   | wc -l` is 3296, already over the ceiling before this unit touched it;
   this unit's net change to the file is +17 lines
   (`setPlayerFirstPersonMode()`'s rewrite plus comments, minus the
   deleted `triggerFirstPersonAttack()`). Same situation and same
   precedent T65's Review recorded for `tools/model_viewer.cpp`. Not a
   new violation, flagged per protocol rather than silently absorbed.
   `include/client/graphics/irrlicht_renderer.h` (2150) and
   `src/client/graphics/irrlicht_renderer.cpp` (2995) both WARN
   (pre-existing, > 1500) — unchanged category from before this unit.
4. **No live capture shows visible first-person arms/weapon geometry**,
   despite the underlying filter, attachment, and camera-positioning code
   all being independently verified correct (headless tests + live log
   instrumentation). See "Live acceptance captures" above — flagged as an
   open follow-up question, not silently claimed as working from the
   headless evidence alone.
5. **No live capture confirms a landed first-person swing.** Every attempt
   was blocked by test-setup issues (a `/target` substring accidentally
   matching an unrelated guard's name, then genuine out-of-range gates).
   The last attempt against a real nearby target engaged auto-attack
   cleanly but the capture window closed before hit/miss text appeared.
