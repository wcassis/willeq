# T65 — A plain secondary weapon attaches successfully and renders nothing

**Slot: t2. OWNS EQEMU, but only for the final in-world leg — diagnose
headless first. x86 only, no Pi.**

**Territory: `include/client/graphics/eq/bone_attachment.h`,
`src/client/graphics/ui/character_model_view.cpp`,
`src/client/graphics/entity_equipment.cpp`,
`tools/hwcaps/ui_inventory_capture.cpp`, `tools/model_viewer.cpp`, and
tests.**

Do not touch the GLES2 driver (sibling T61 owns it) or the command
registries (sibling T66). `refactor/deferred.md` is append-only and shared.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`/mnt/projects/fable/review/T63/README.md`
in full** — it is the evidence that produced this unit and it names a
specific hypothesis — then `refactor/T60_paperdoll_restore.md`'s Review
(which converged the three copies of the bone logic you will be working in)
and `refactor/T62_held_item_alignment.md`'s Review (which established how
to isolate a single equipped piece in a capture).

## The defect

T63 verified equipment rendering in a live session. Three of four
configurations were correct; one was not:

| Slot | Item | Bone | Result |
|---|---|---|---|
| Primary | material 7 (IT7 mace) | index 6 | renders correctly |
| Secondary — **shield** | material 202 (IT202) | index 11 (`SHIELD_POINT`) | renders correctly, on the arm |
| Secondary — **plain weapon** | material 5 (IT5 dagger) | index 10 (`L_POINT`) | **nothing visible** |

See `review/T63/02_secondary_only/paperdoll.png` — the weapon row shows
`Dag` equipped and the left hand is empty. Reproduced independently in
config 3 (dual wield), same bone index, same offset `0.51`, same result.

**This is not a lookup failure.** The log confirms the whole chain
succeeded: mesh built, texture loaded, bone resolved
(`Attached secondary weapon 5 (bone=10 offset=0.51)`). A node is created
and positioned. Nothing appears. So the geometry is being put *somewhere* —
most likely somewhere you cannot see it.

## T63's hypothesis — test it, do not trust it

T63 flagged a candidate and was explicit that it is unconfirmed:

`character_model_view.cpp:549-552` (mirrored in
`entity_equipment.cpp:233-236`) applies rotation `(180, 0, 0)` to a
non-shield secondary but `(180, 0, 180)` to a shield. On this skeleton's
`L_POINT` orientation, that extra shield-only Z rotation may be what
pushes the shield clear of the body mesh, while the plain-weapon rotation
points the `0.51` offset **into or behind the torso** — so the weapon is
rendering *inside the character*, hidden rather than missing.

That is a plausible story and it fits every observation, which is exactly
why it needs disproving rather than assuming. **Run a controlled A/B**:
temporarily give the plain weapon the shield's rotation and re-capture. If
it appears, the hypothesis holds and you have the root cause. If it does
not, report that and widen — check whether the node is scaled to zero,
culled, has a null/failed material, or is parented somewhere unexpected.

## Step 1 — blast radius, before any fix

**Establish whether this is paperdoll-only or also in-world.** T63 could
not tell: its 3rd-person captures were in a dark zone and it says so
plainly (`review/T63/01_primary_only/3rd.png` and friends are nearly
black). That is an evidence gap, not a finding.

The paperdoll and the in-world entity path share `bone_attachment.h` since
T60 but apply it in different code
(`character_model_view.cpp` vs `entity_equipment.cpp`), so they can
genuinely differ. Determine which, because it decides where the fix goes
and how much regression risk it carries.

Diagnose headless first — `tools/hwcaps/ui_inventory_capture.cpp` (T62
gave it per-piece isolation) renders the paperdoll with no server at all
and is the fastest loop. Take the eqemu lock only for the in-world leg.

**When you do go in-world, use a LIT scene.** T63's darkness is why this
question is still open; repeating it wastes the server session. A daylight
outdoor zone, or `#settime` if it is stable, or simply a zone that is not
pitch black.

## Step 2 — fix at the shared layer, and do not break the two things that work

If the root cause is in the rotation/offset math, it likely belongs in
`bone_attachment.h`, which the paperdoll, `entity_equipment.cpp` and
`model_viewer.cpp` all consume since T60. **A fix that corrects the
paperdoll while leaving in-world entities wrong — or vice versa — is a
failed unit**; that divergence is exactly what T60 spent its effort
closing.

**Mandatory regression evidence.** The primary weapon and the shield both
render correctly today. Capture before/after for all three configurations —
primary, shield, plain secondary — and show the two working cases
unchanged. Changing shield or primary placement is a visual regression Will
would have to re-accept, and it is not this unit's purpose. If your fix
moves them, STOP and report rather than shipping it.

Extend `tests/test_bone_attachment.cpp` (T60 added 17 cases, T62 took it to
20) with cases pinning whatever the corrected rotation/offset turns out to
be.

## STOP conditions

- The A/B refutes the hypothesis and the real cause is outside this unit's
  territory (e.g. in the driver, or in mesh loading).
- The fix would change primary or shield placement.
- Any temptation to special-case the paperdoll.
- eqemu lock already held — check `/mnt/projects/fable/.eqemu-lock` first.

## Verification

Full protocol bar. Additionally: the blast-radius answer with evidence;
the A/B result stated plainly whether it confirmed or refuted; before/after
captures for all three configurations in a **lit** scene; and, if the
in-world path changed, confirmation from a live session.

Media to `/mnt/projects/fable/review/T65/` with a README, per protocol.
**T64 landed video capture** (`--dump-frames-dir`/`--dump-every`/
`--dump-max-frames` + `scripts/ci/encode_capture.sh`) — use it if any part
of this turns out to be temporal or animation-dependent; stills are fine if
it is static. Capture, describe, stop — Will judges.

If you take the server: lock first, clean shutdown, delete the lock,
restore any character you modify. T63 had to write `inventory.item_id`
directly to equip anything because no scriptable equip exists — **sibling
unit T66 is adding `/equip` for exactly this reason.** If T66 has merged by
the time you need it, use the command instead of touching the database; if
not, follow T63's backup-and-restore approach and say so.

## Review

(executor fills: blast-radius answer; the A/B result; the root cause; where
the fix landed and why that layer; regression evidence for primary and
shield; capture paths; deviations)
