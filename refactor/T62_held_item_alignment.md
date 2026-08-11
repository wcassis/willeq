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

(executor fills: whether texture and geometry are separate defects and
which was which; whether in-world entities reproduce it; the measured
weapon dimensions and the corrected grip math; the shield-offset decision
and its evidence; before/after capture paths; the front-facing change;
deviations)
