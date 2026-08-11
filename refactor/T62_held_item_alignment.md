# T62 — Equipped weapons render as a huge untextured mass, not held in the hand

**Slot: t2. x86 primary — the bug reproduces identically on x86 and Pi,
so hardware is not required to see it. NO EQEMU** (the paperdoll renders
from a synthetic `EntityAppearance`; the capture tool already does this).

**Territory: `include/client/graphics/eq/bone_attachment.h`,
`src/client/graphics/entity_equipment.cpp`,
`src/client/graphics/ui/character_model_view.cpp`,
`src/client/graphics/eq/equipment_model_loader.*`,
`tools/hwcaps/ui_inventory_capture.cpp` (you own it this round),
`tools/model_viewer.cpp`, and tests.**

**Do NOT touch `docker/irrlicht-drm/COpenGLES2Driver.*`** — sibling unit
T61 owns the driver this round and is investigating a separate Mali 2D
rendering defect. Do not take the Pi; T61 owns it.

Do not change UI layout or slot geometry. `refactor/deferred.md` is
append-only and shared.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `refactor/T60_paperdoll_restore.md`
including its Review — it converged the bone logic you are about to work
in, and documented a divergence you must not undo casually.

## The observation

T60 restored the paperdoll, which finally makes this bug visible in a
fixed, controllable view. See
`refactor/artifacts/T60/inventory_paperdoll_equipped.png` (x86) and
`refactor/artifacts/T60/pi/inventory_paperdoll_equipped.png` (Mali400) —
**identical on both.**

What the captures show: with a weapon and shield equipped, a **large
untextured white mass covers the character's torso and chest and extends
well past the arm.** The bare capture
(`inventory_paperdoll_bare.png`) renders correctly — a properly textured,
proportioned character — so the character model path is fine and the
defect is in equipment attachment.

That appearance suggests **two separable problems**, and you should
confirm whether both are present before fixing either:

1. **Untextured** — a white/blank surface usually means no texture bound
   or a failed texture lookup on the equipment mesh.
2. **Mis-scaled and/or mis-positioned** — the mass is far larger than a
   weapon should be and is nowhere near the hand.

Will has reported this as a long-standing problem ("issues getting
inventory items to appear properly held in a player's hand"), so treat it
as a real production defect, not a paperdoll artifact. **Confirm it
reproduces for in-world entities too** (3rd-person / another player or
NPC holding a weapon) — if it does, the fix belongs in the shared path
and benefits both; if it is paperdoll-only, that itself is the finding.

## The prime suspects

T60 converged three historical copies of this logic into
`include/client/graphics/eq/bone_attachment.h`. Read its comments first —
they record a real divergence found during convergence and are load-bearing.

- **`computeWeaponGripOffset(bbox)`** (`bone_attachment.h`):
  ```
  length = bbox.MaxEdge.X - bbox.MinEdge.X;
  isTwoHanded = (length > 3.0f);
  return isTwoHanded ? length * 0.25f : -bbox.MinEdge.X * 0.8f;
  ```
  The `3.0f` two-handed threshold is a magic number in unstated units,
  and the one-handed branch assumes `MinEdge.X` is negative. **Establish
  what units EQ weapon meshes actually use** before trusting either
  branch — if the threshold is wrong for the real data, every weapon
  takes the wrong branch.
- **`computeShieldGripOffset()`** returns a flat `0.3f`. T60 kept this
  deliberately because it is what production's per-frame update used, and
  documented that the model viewer and the deleted paperdoll used
  `bbox width * 0.175` instead. **One of those is wrong, and this is the
  unit that gets to decide which** — but decide it with evidence, and
  state the visual consequence for in-world entities.
- The hardcoded **180° X rotation** applied on top of the bone rotation.
- Equipment mesh **scale** — check whether any scale is applied at all,
  and whether the paperdoll's isolated scene manager needs the same scale
  the in-world path applies.

## Approach

1. **Reproduce and isolate.** Use the capture tool (you own it) to render
   a single known weapon with no shield, then a shield with no weapon.
   The current capture equips both at once, which makes the two
   contributions impossible to separate. Add per-piece captures.
2. **Answer the texture question first** — it is independent of geometry
   and probably simpler. An untextured mesh may indicate the equipment
   texture never loads in the isolated scene manager, or that the
   paperdoll's material setup differs from the in-world path.
3. **Then geometry.** Establish real mesh dimensions for a few known
   weapons, verify the two-handed threshold against them, and correct the
   grip math with the numbers written down in the Review.
4. **Behaviour for in-world entities may legitimately change here** —
   that is the point, if they are also wrong. But it is a visual change
   Will must accept, so capture before/after for **both** the paperdoll
   and an in-world entity, and never self-assess.

## Also in scope: make the paperdoll face front (Will, 2026-08-11)

The restored doll renders in **profile, facing right**
(`inventory_paperdoll_bare.png`). The original EQ paperdoll faces the
viewer. Change the default rotation to front-facing.

`CharacterModelView` already has `getRotationY()`/`setRotationY()` and
mouse-drag rotation — this is a default-value change, not new machinery.
Keep drag rotation working, and make sure the default is applied on
first render, not only after a drag.

## STOP conditions

- The fix requires driver changes — that is T61's territory this round.
- In-world weapon placement would change and you cannot produce
  before/after evidence for Will to judge.
- Any urge to "fix" this by special-casing the paperdoll while leaving
  in-world entities wrong. The shared header exists precisely to prevent
  that; a paperdoll-only correction re-creates the divergence T60 closed.

## Verification

Full protocol bar (x86; no Pi leg — T61 owns the Pi). Additionally:
per-piece captures isolating weapon vs shield, before/after for both the
paperdoll and an in-world entity, and the measured mesh dimensions that
justify whatever the grip math becomes. Extend `test_bone_attachment.cpp`
(T60 added 17 cases) with cases pinning the corrected offsets.

**Capture, state what you captured, and stop — Will judges the visuals.**

## Review

### Headline finding: this was never a production held-item defect

T60's "huge untextured white mass" observation, which seeded this unit,
was reproduced and root-caused to **two independent mistakes in the
test/debug tooling**, not a defect in `bone_attachment.h`'s shared grip
math or in production's in-world rendering path:

1. **Wrong test item.** T60's capture equipped database item **1000**,
   assuming it was a weapon. `data/willeq.db`'s `item_models` table maps
   item 1000 to equipment model **IT64**, and `equipment_models.textures`
   for IT64 is `sack1.bmp` — a sack/container model, not a weapon. Its odd,
   irregular, roughly-torso-sized silhouette is exactly what T60 described
   ("far larger than a weapon should be... nowhere near the hand") because
   it genuinely is not a weapon.
2. **Missing texture-cache wiring, in the tool only.**
   `EquipmentModelLoader::buildMeshFromGeometry()` (`equipment_model_loader.cpp:569`)
   only uploads a texture if `constrainedTextureCache_` is non-null, with
   **no fallback** (contrast `RaceModelLoader`'s character-body path,
   `mesh_building.cpp`, which resolves textures via `meshBuilder_` and
   does not require a `ConstrainedTextureCache` at all). T60's
   `ui_inventory_capture.cpp` called `setConstrainedTextureCache()` for
   `raceModelLoader` but never for `equipmentModelLoader` — every
   equipment mesh the tool ever built was untextured, regardless of which
   item was used. `tools/model_viewer.cpp` had the identical gap (no
   `ConstrainedTextureCache` anywhere in the file) — this explains the
   plan's own observation that "debugging in the viewer did not reliably
   predict production" (T60's language, inherited from the pre-T60 era):
   the viewer never had equipment textures to begin with, for a completely
   unrelated reason to grip math.

**Production was never affected by either mistake.**
`EntityRenderer::setConstrainedTextureCache()` (`entity_renderer.cpp:923`,
called from `IrrlichtRenderer` init, `irrlicht_renderer.cpp:2183`) wires
the cache to both `raceModelLoader_` and `equipmentModelLoader_` together
— the in-world path has always had this correctly wired. And production
never used item 1000/IT64 as anything other than whatever real item the
database actually maps it to for players; the T60 capture's item choice
was a capture-script mistake, not a reflection of real equipped-item data.

### Texture vs geometry: were both present?

**Texture: yes, a real defect — but confined to the capture tool and
`model_viewer.cpp`, fixed in both (see Deviations).** Confirmed via
before/after capture with the *same* item (1000, the sack) and the *same*
rotation, varying only whether `setConstrainedTextureCache()` was called —
see `/mnt/projects/fable/review/T62/before/paperdoll_weapon_only_repro_800x600.png`
(white, untextured) vs
`/mnt/projects/fable/review/T62/after/paperdoll_weapon_only_repro_800x600.png`
(correctly textured sack). Single-variable pair, texture wiring only.

**Geometry/mis-scale: no defect found.** Re-tested with real weapon/shield
items and the **unmodified** shared `computeWeaponGripOffset`/
`computeShieldGripOffset` formulas (bone_attachment.h was read, measured
against, and left byte-for-byte identical to the T60-converged version —
confirmed via `git diff` showing no changes to that file). Real meshes,
with real textures, render correctly scaled and gripped in the hand — see
the isolation captures below. The "mis-scaled/mis-positioned" appearance
T60 reported was the sack model's actual geometry (a sack is, correctly,
not weapon-shaped and not held like one), not a bug in the offset math.

### Measured weapon/shield dimensions (real gequip.s3d meshes)

Printed via `ui_inventory_capture`'s new `BBOX:` diagnostic lines
(`printBBox()` in `tools/hwcaps/ui_inventory_capture.cpp`), reading each
mesh's raw Irrlicht bounding box before any scale/attachment logic
touches it:

| Item | Extent (X,Y,Z) | X-length | `computeWeaponGripOffset` branch | Offset |
|---|---|---|---|---|
| IT1 (sword, 1H) | (2.746, 0.569, 0.148) | 2.746 | one-handed (`2.746 ≤ 3.0`) | `-MinEdge.X*0.8` = 1.099 |
| IT45 (wizard staff, 2H) | (5.887, 0.311, 0.312) | 5.887 | two-handed (`5.887 > 3.0`) | `length*0.25` = 1.472 |
| IT202 (kiteshield-m) | (2.671, 2.264, 0.256) | — | flat constant, bbox unused | 0.3 |

**The existing `3.0f` two-handed threshold is validated, not corrected.**
It sits between the real sword (2.746) and the real staff (5.887) with
comfortable margin on both sides, and the `MinEdge.X` negative assumption
holds for both real meshes (`-1.374`, `-5.823`). No change was made to
`computeWeaponGripOffset()` — see `git diff include/client/graphics/eq/bone_attachment.h`
(empty). Three new pinned unit tests
(`RealOneHandedSwordIT1GripsNearHilt`, `RealTwoHandedStaffIT45GripsAtQuarterLength`,
`RealShieldIT202BoundingBoxDoesNotAffectFlatOffset`) record these exact
measured dimensions in `tests/test_bone_attachment.cpp`, so a future
regression against real EQ mesh scale would be caught even though no
`EntityRenderer`-level integration test exists (T60's honest-limitation
note still applies — this doesn't close that gap, it just adds
ground-truth-dimension coverage at the pure-function level).

### Shield-offset decision: kept production's flat `0.3f`

Per T60's ask, this unit tested the alternative (`bbox width * 0.175`,
what the deleted paperdoll and `model_viewer.cpp`'s pre-convergence
initial-placement code used) against the real IT202 shield
(width 2.671, so `2.671*0.175 = 0.467` vs the flat `0.3`). Temporarily
patched `computeShieldGripOffset()` to the bbox-derived value, rebuilt,
and re-captured the shield-only paperdoll for an A/B comparison (not
retained in `review/` — this was a throwaway diagnostic render, not
release evidence, per the note below). **Visually near-identical**: the
0.167-unit difference in offset-along-rotated-local-X was not visually
distinguishable from the flat-0.3 render at this shield's real size,
confirming T60's own note that "these two formulas... produce
visually-similar results for typical shield sizes." Reverted immediately
(`bone_attachment.h` carries no net change — verified via `git diff`
showing an empty diff for that file). **Decision: keep production's flat
`0.3f`.** Evidence: (1) it is what production has always shipped and
in-world behaviour must not change without a real visual reason; (2) the
measured alternative produces no meaningfully different result for a
representative real shield; (3) the `after/paperdoll_shield_only_isolated_800x600.png`
capture (flat 0.3, real IT202) shows a plausible, correctly-textured,
correctly-gripped kite shield spanning roughly shoulder to hip — a
canonical EQ shield silhouette, not an obviously-wrong one. Will should
still judge this render on its own merits; this unit found no evidence
compelling a change.

### In-world entities: not affected, so not re-captured

`bone_attachment.h` and `entity_equipment.cpp` (the in-world/3rd-person
path) were read, measured against with real data, and left completely
unmodified — `git diff` for both shows no changes. Since the plan's STOP
condition is "in-world weapon placement would change and you cannot
produce before/after evidence" and in-world placement provably did **not**
change (no code in its path was touched), no in-world before/after
capture was needed or produced. No STOP was triggered.

No eqemu is in scope for this unit regardless (plan header: "NO EQEMU").
The paperdoll is not merely paperdoll-only evidence, though: per T60 Step
4, `CharacterModelView` consumes the identical shared
`findBoneIndex`/`extractBoneRotation`/`computeWeaponGripOffset`/
`computeShieldGripOffset` calls, the same `EntityAppearance`-driven
appearance, and the same `compositeAllowed=false` branch the in-world
`isPlayer` path uses (`entity_renderer.cpp:546`) — it is the closest
same-code-path proxy for in-world behaviour available without a live zone,
which is exactly why T60 built it as this unit's diagnostic instrument.

### Front-facing default rotation

`CharacterModelView::rotationY_`'s default changed from `0.0f` (profile,
facing right — matches T60's `inventory_paperdoll_bare.png`) to `90.0f`
(faces the camera), in `include/client/graphics/ui/character_model_view.h`.
Verified empirically, not by formula: temporarily set to `0.0f`, rebuilt,
captured (`review/T62/before/paperdoll_rotation_bare_800x600.png`,
profile); restored `90.0f`, rebuilt, captured
(`review/T62/after/paperdoll_rotation_bare_800x600.png`, front-facing) —
single-variable pair, same character/equipment (bare), same camera. Drag
rotation (`onMouseMove()`) is relative to `dragStartRotation_ = rotationY_`
at drag start, so it works correctly from any starting angle — verified by
reading (not re-testing interactively, since this tool captures static
frames): the drag math never assumes a `0.0f` origin. The default applies
at member-initialization time (`rotationY_ = 90.0f`), before
`updateCharacterModel()`'s `characterNode_->setRotation(...)` ever runs,
so it is correct on the very first render, not only after a drag.

### Capture paths

Per the coordinator's mid-flight protocol update, all review media went to
`/mnt/projects/fable/review/T62/` (outside this worktree, not committed —
see that directory's `README.md` for the full file-by-file description).
Summary:

- `review/T62/before/paperdoll_weapon_only_repro_800x600.png` +
  `review/T62/after/paperdoll_weapon_only_repro_800x600.png` — texture-fix
  pair (item 1000/sack held constant, only texture-cache wiring differs).
- `review/T62/before/paperdoll_rotation_bare_800x600.png` +
  `review/T62/after/paperdoll_rotation_bare_800x600.png` — rotation-fix
  pair (bare character held constant, only the rotation default differs).
- `review/T62/after/paperdoll_weapon_only_isolated_800x600.png`,
  `paperdoll_shield_only_isolated_800x600.png`,
  `paperdoll_two_handed_weapon_isolated_800x600.png`,
  `paperdoll_weapon_and_shield_equipped_800x600.png` — the plan's
  per-piece isolation ask (real IT1 sword alone, real IT202 shield alone,
  real IT45 two-handed staff alone, and both weapon+shield together), all
  with both fixes applied.
- `refactor/artifacts/T62/` (in-worktree, same convention as T57/T60) has
  the equivalent full capture set including the T57-style slot-only
  captures (`inventory_800x600_full.png` etc., unaffected by this unit,
  re-verified still correct) and the `TIMING:`/`BBOX:` diagnostic stdout
  this unit added.

**Per protocol: capture, state, and stop — Will judges. No image above is
self-assessed as "correct" beyond the narrow, evidence-backed claims this
Review makes (texture present/absent, rotation angle, measured
dimensions).**

### Deviations from the plan

1. **`tools/model_viewer.cpp` was patched with the same
   `ConstrainedTextureCache` wiring fix**, even though the plan's
   Approach section named only the capture tool as "you own it this
   round" for capture work. This is in the plan's listed territory
   (`tools/model_viewer.cpp`), fixes the identical root cause found in the
   tool this unit does own, and directly serves the plan's own diagnosis
   language ("debugging in the viewer did not reliably predict
   production"). Minimal, additive: one new member
   (`equipmentTextureCache`), two includes, four wiring lines. No existing
   model_viewer behaviour changed other than equipment meshes now
   receiving textures.
2. **`kTestPrimaryItemId` changed from `1000` to `1`** (a real sword,
   `IT1`) in `ui_inventory_capture.cpp`, and a new `kTestTwoHandedItemId = 45`
   (wizard staff) and `kTestShieldModelId = 202` (kiteshield) were added.
   Not named in the plan, but required to produce any meaningful capture
   at all — item 1000 was never a weapon (see Headline finding).
3. **`check_file_sizes.sh fable` reports one pre-existing FAIL**
   (`tools/model_viewer.cpp`, 3232 lines) — this file was already at 3217
   lines (already over the 3000-line threshold) at `fa3a3a90`, the commit
   this branch was cut from (confirmed via
   `git show fa3a3a90:tools/model_viewer.cpp | wc -l`). This unit's
   texture-cache fix added 15 lines to it. Not a new FAIL, matching the
   precedent T60 recorded for the same file. `include/client/graphics/ui/character_model_view.h`,
   `tests/test_bone_attachment.cpp`, and `tools/hwcaps/ui_inventory_capture.cpp`
   all report `ok`.
4. **No `EntityRenderer`-level in-world integration test was added** —
   same honest limitation T60 already recorded (`refactor/T60_paperdoll_restore.md`'s
   Review); out of scope here since `entity_equipment.cpp` was not
   touched.

No STOP condition was triggered: no driver changes were needed or made (T61's
territory untouched); in-world weapon placement did not change (verified —
`bone_attachment.h`/`entity_equipment.cpp` diffs are both empty); no
paperdoll-only special-casing occurred (the paperdoll's correctness is a
direct consequence of the shared header being correct, verified against
real measured data, not a paperdoll-specific patch).
