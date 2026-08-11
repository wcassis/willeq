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

### Blast radius: both paperdoll AND in-world

`character_model_view.cpp` (paperdoll) and `entity_equipment.cpp`
(in-world 3rd-person) both applied the textually identical broken
rotation split before this fix — confirmed by direct code reading, not
inference:

```cpp
// character_model_view.cpp:549-551 (paperdoll) — before
irr::core::vector3df rot = secondaryIsShield_
    ? irr::core::vector3df(180, 0, 180) : irr::core::vector3df(180, 0, 0);
// entity_equipment.cpp:235 (in-world) — before, same shape
irr::core::vector3df itemRot = boneRot + irr::core::vector3df(180.0f, 0, isShield ? 180.0f : 0);
```

`tools/model_viewer.cpp` had the same split at its initial-placement site
only (its per-frame update site had already, inconsistently, hardcoded
the shield rotation for all secondary items — an internal inconsistency
now resolved the same way everywhere).

**Headless reproduction of T63's exact repro**: `tools/hwcaps/
ui_inventory_capture.cpp` extended with a Dark Elf female (race=6,
gender=1 — T63's exact race/gender) + IT5 dagger (T63's exact item)
secondary-only capture. Reproduced "nothing visible anywhere" byte-for-
byte outside a live session — see `review/T65/headless/before/`.

### A/B result: CONFIRMED, not refuted

`character_model_view.cpp`'s rotation ternary was temporarily forced to
always emit the shield's `(180, 0, 180)`, single-variable change (same
item, same race, same bone/offset), rebuilt, recaptured: the invisible
dagger became visible, gripped correctly in the hand — see
`review/T65/headless/ab_test/`. The extra Z rotation the shield branch
applied and the plain-weapon branch omitted is what was pointing the
`computeWeaponGripOffset()` offset axis into/behind the torso, hidden
behind body geometry — T63's "rendering inside the character" theory,
confirmed rather than the alternative causes the plan listed (zero scale,
culling, failed material, unexpected parenting were not investigated
further because the A/B isolated the rotation as sufficient and
necessary).

### Root cause and fix location

Root cause: the `isShield ? extraZRotation : 0` split was never a
legitimate divergence — shields and plain weapons in the secondary slot
need the same base rotation. Fix added `computeSecondaryItemBaseRotation()`
to `bone_attachment.h` (the shared layer T60 converged, per the plan's
instruction to fix here so all three consumers stay behaviour-identical):
returns `(180, 0, 180)` unconditionally for the secondary slot. Updated
all consumers to call it, removing the `isShield`-conditional Z rotation:
`character_model_view.cpp` (paperdoll, 2 call sites),
`entity_equipment.cpp` (in-world, 1 call site), `tools/model_viewer.cpp`
(debug tool, 2 call sites). Primary weapons (`R_POINT`, `(180, 0, 0)`)
are a separate bone/slot and are untouched by this fix.

### Regression evidence for primary and shield

Headless, byte-level (`numpy.array_equal`), not eyeballed — before vs
after the fix, holding everything else constant:

| Configuration | before vs after |
|---|---|
| Primary weapon only | identical=True |
| Secondary shield only | identical=True |
| DAF secondary shield | identical=True |
| Bare character | identical=True |
| Two-handed primary | identical=True |
| DAF secondary dagger (the defect) | identical=False (fixed, as intended) |
| Human secondary dagger | identical=False (fixed, as intended) |

Live session regression check: `review/T65/live/03_live_paperdoll_primary_mace_secondary_shield.png`
— live server, log-confirmed `primary=7 secondary=202`, shield renders
correctly on the arm, unchanged from T63's finding.

Full protocol test suite: 1974/1975 passing (sole failure is the
pre-existing documented `AudioManagerTest.PreloadCommonSounds` ALSA host
waiver — no new failures). `test_bone_attachment.cpp` extended from 20 to
22 cases (2 new, pinning `computeSecondaryItemBaseRotation()`'s value and
its independence from an `isShield` flag), all passing.

### Live in-world confirmation (lit scene)

eqemu brought up in `qeynos2` (daylight outdoor zone, visible sky/clouds
in the captures — unlike T63's `arena`, documented as rendering "nearly
black"). Character `Testmelee` (T63's exact Dark Elf female). Live,
server-driven paperdoll (drawing on the same live `EntityAppearance` the
in-world 3rd-person entity path renders from, per T60's Review) confirms
the fix: `review/T65/live/04_live_paperdoll_primary_mace_secondary_dagger.png`,
log-confirmed `primary=7 secondary=5` (T63's exact item ids), dagger
now visible — `review/T65/live/crop_04_dagger_hand.png` zooms in to
confirm it is a real mesh, not an artifact.

**Honest limitation**: an actual 3rd-person (non-paperdoll) capture in
the lit zone was attempted (`review/T65/live/05_3rd_person_lit_distant_uncertain_equip_state.png`)
but the camera distance in the default Follow mode (character ~150px
tall in an 800x600 frame) made the secondary item unresolvable even
brightened/gamma-corrected — flagged as inconclusive, not claimed as
evidence, in `review/T65/README.md`. `entity_equipment.cpp` itself was
therefore not independently re-verified live via a clear 3rd-person
screenshot; the live-in-world claim rests on (1) the identical-code-before,
identical-fix-after argument, code-verified, and (2) the paperdoll's
proven equivalence to the in-world entity's appearance data, not on a
direct 3rd-person screenshot of the dagger. Stated rather than glossed
over.

### Deviation: `/equip`'s live leg found broken, reported, not fixed

T66 merged `/equip` into `fable` after this branch was cut; per the
plan, `fable` was merged into this task branch (`git merge fable`,
fast-forward) to pick it up, since the plan preferred it over T63's
direct-SQL approach if available in time. Tried first. **Finding: `/equip`'s
live wire behaviour did not match its client-side confirmation text** —
`/equip 30 14` printed `"Moving item: slot 30 -> Secondary (14)"` and
`outcome=applied` (emitted on successful validation/intent-push per T66's
own Review, not a server ack) every time, but a subsequent DB read (with
a 5s idle-stability check ruling out query timing) showed the secondary
slot had NOT received a clean swap in every case tried (one run left it
holding the same item id as primary — duplicated, not swapped). This
matches exactly what T66's Review flagged as unverified and asked the
next eqemu-holding unit to close: it did not close cleanly here either.
**Not fixed** — `eq_commands.cpp`/`SendMoveItem`/`ZoneProcessMoveItem`
are outside T65's territory fence. Recorded in `review/T65/README.md` for
the board; fell back to T63's sanctioned direct-`inventory.item_id`-write
approach for the decisive live captures, restoring Testmelee's baseline
inventory and position afterward (confirmed via a final `SELECT`).

### eqemu discipline

Lock checked absent, taken (`T65 (slot2)`), deleted after `down.sh`
confirmed clean container teardown. Server via sanctioned
`scripts/eqemu/up.sh qeynos2` only. `Testmelee`'s baseline
(`zone_id=25 x=-259 y=-1201 z=-5.75 heading=160.25`, primary item
`9998`, secondary item `9979`) was recorded before any change and fully
restored before shutdown.

### Deviations from the plan (summary)

1. `git merge fable` (fast-forward) into the task branch, to pick up
   T66's `/equip` (merged after this branch was cut) and T61's landed
   driver work — needed for the live leg per the plan's own instruction
   to prefer `/equip` if merged in time. No conflicts; only my 6 files
   remained locally modified afterward (`git status` clean otherwise).
2. `check_file_sizes.sh` was run against `738da2a5` (this branch's actual
   fork point) rather than the literal `fable` argument, since `fable`
   had moved forward (to T66/T61's merges) by the time this ran and
   diffing against the literal ref would have reported those siblings'
   files as "changed" against my branch — not a real diff for this unit.
   Confirmed via `git merge-base`. Result: 5 ok, 0 warn, 1 pre-existing
   FAIL (`tools/model_viewer.cpp`, 3232 lines before this unit's 4-line
   comment addition, 3236 after — already over the 3000 ceiling before
   T65 touched it, not a new violation).
3. `/equip`'s live-leg finding (above) — discovered while following the
   plan's own instruction to prefer it, reported rather than silently
   worked around or fixed out-of-territory.
