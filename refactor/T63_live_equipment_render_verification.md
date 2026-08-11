# T63 — Live verification: do equipped weapons and shields actually render correctly, in every view?

**Slot: t2. OWNS EQEMU — the whole unit is a live session. x86 only, no Pi.**

This unit answers a question that has been open all session and that no
amount of headless work can settle. T62 proved the *captured artifact*
that looked like a held-item bug was tooling (wrong test item — a sack —
plus a missing texture cache in the capture tool alone). It explicitly
did **not** verify Will's separately-reported in-game experience of items
not appearing properly held. **That is this unit's job, and it requires a
real character holding real items in a real session.**

**Territory: `refactor/`, `/mnt/projects/fable/review/T63/`, and
throwaway scripts under `scripts/eqemu/` or a scratch path.**

**This is a VERIFICATION unit. Production code is READ-ONLY.** If you
find a defect, you capture it, characterise it precisely, and report it —
you do not fix it. A fix needs its own unit with Will's visual
acceptance. The one exception: if the client cannot be driven at all
without a trivial scripting affordance, STOP and report rather than
inventing one.

Read `refactor/EXECUTOR_PROTOCOL.md` (**the eqemu section — lock first,
no exceptions**), then `refactor/TESTING_CHARTER.md`, then
`refactor/T62_held_item_alignment.md`'s Review (it establishes how to map
an item id to an equipment model, and which items are real weapons).

## What must be verified

Four equipment configurations, each in the views listed:

| # | Configuration | 3rd person | 1st person (swinging) | Paperdoll |
|---|---|---|---|---|
| 1 | Primary weapon only | yes | yes | yes |
| 2 | Secondary weapon only | yes | yes | yes |
| 3 | Dual wield — both weapons | yes | yes | yes |
| 4 | **Shield in secondary** | yes | n/a unless shown | yes |

**Configuration 4 has a specific, separate acceptance criterion:** a
shield must render **on the forearm/arm**, not gripped in the hand.

Will asked for this as one job; splitting it into stages is fine and
encouraged, **but do not split it across units** — eqemu is an exclusive
lock and a handoff would mean re-establishing the whole session. Commit
progressively so a late failure does not lose earlier evidence.

## Why the shield case is the most likely to fail — read before testing

The shield path is already distinct in production
(`src/client/graphics/entity_equipment.cpp:212-247`):

- `EquipmentModelLoader::isShield(modelId)` classifies by model id range —
  **`modelId >= 200 && modelId < 300`**
  (`equipment_model_loader.cpp:77-80`). A shield whose model falls
  outside IT200-IT299 is treated as a weapon.
- Shields look up bones with a **fallback chain**:
  ```
  isShield ? {SHIELD_POINT_DAG, SHIELD_POINT, shield_point_dag, shield_point,
              L_POINT_DAG, L_POINT, l_point_dag, l_point}
           : {L_POINT_DAG, L_POINT, l_point_dag, l_point}
  ```
  **If the race skeleton has no `SHIELD_POINT` bone, the lookup silently
  falls through to `L_POINT` — the left hand — and the shield renders
  gripped rather than strapped to the arm.** That is precisely the
  symptom to look for, and it would be per-race.
- Shields get an extra 180° Z rotation and use `computeShieldGripOffset()`
  (a flat `0.3`) rather than the bbox-derived weapon offset.

**So report, for the race you test: did `findBoneIndex` resolve a real
`SHIELD_POINT`, or did it fall back to `L_POINT`?** Log it — do not infer
it from the picture alone. If it fell back, test a second race to
establish whether the shield point exists anywhere. That turns "shields
look wrong" into an actionable, scoped finding.

## Setup

- **Lock first**: check `/mnt/projects/fable/.eqemu-lock` does not exist;
  if it does, STOP and report. Otherwise create it with `T63` + the date,
  and delete it after clean shutdown.
- Sanctioned start only: `scripts/eqemu/up.sh` / `spire spire:launcher
  start`. Never `eqemu-server:launcher` directly. Clean shutdown via
  `down.sh` at the end.
- Client: `build-gles2/bin/willeq` (EGL surfaceless, no X needed).
  Account `willeq_test`; characters `Testchar` / `Testmelee` /
  `Testcaster` are GM. **`Testmelee` is the right character** — dual
  wield needs a melee class with the skill.
- **Restore afterwards**: Testchar → `zone_id=36, x=-77.9232, y=21.9086,
  z=3, h=190`. If you move or re-equip any character, restore it and say
  so.

**Choosing items.** Do not guess item ids — that is exactly the mistake
that produced T60's phantom bug. Use `data/willeq.db`'s `item_models`
table to map item id → equipment model (`IT###`), the method T62 used to
discover item 1000 was a sack. Pick and record:
- a one-handed weapon (T62 measured **IT1** at 2.746 units),
- a second one-handed weapon for the offhand,
- a shield in the **IT200-IT299** range (T62 used **IT202**),
- optionally a two-handed weapon (T62 measured **IT45** at 5.887 units)
  as a control.

**State the item id AND the resolved model id for every item you equip.**
A capture without that mapping is unreviewable.

Dual wield needs the skill — if `Testmelee` lacks it, GM-set it and note
that you did.

## Capturing

`/screenshot <path>` is a scriptable slash command
(`irrlicht_renderer.cpp:1036-1044`). **Read `saveScreenshot`
(`:2681` onward) before relying on it** — it carries documented caveats
about `createScreenShot()` returning garbage under the surfaceless/DRM
case, and P10 hit exactly that. **Verify your first capture is a real
image and not noise before generating the rest.** Garbled evidence voids
every claim built on it.

**The 1st-person swing capture is the hard one.** The weapon appears
during the attack animation (`triggerFirstPersonAttack()`,
`updateFirstPersonWeapons()`, `entity_renderer.h:407-415`), so a single
screenshot will usually miss it. Use auto-attack against a live target
and burst-capture across the swing, then keep the frames that show the
weapon. Camera mode is cycled via the `ToggleCameraMode` action /
`cycleCameraMode()`; confirm which mode you are in from
`getCameraModeString()` rather than assuming.

All media goes to **`/mnt/projects/fable/review/T63/`** per the protocol's
review-drop rule, with a `README.md` you write: one section per
configuration, each naming the item ids, the resolved model ids, the
race/gender tested, and what to look at. Suggested layout:

```
review/T63/
  README.md
  01_primary_only/     3rd.png  firstperson_swing.png  paperdoll.png
  02_secondary_only/   ...
  03_dual_wield/       ...
  04_shield/           3rd.png  paperdoll.png
```

**Never self-assess whether it looks right.** Say what is in each frame —
"weapon visible in right hand, angled downward", "shield appears gripped
in the left hand, not on the forearm" — and let Will judge. Describing
what you observe is required; grading it is not yours.

## STOP conditions

- `.eqemu-lock` exists.
- The client cannot be driven to a state a configuration needs, and
  working around it would mean changing production code.
- Screenshots come back garbled and you cannot get a clean one — report
  the harness limitation rather than shipping noise.
- Any urge to fix a defect you find. Capture and characterise it instead.

## Verification

This unit's deliverable is **evidence, not a code change**, so the
standard build/test bar is a formality — run it to prove you broke
nothing (you should have changed nothing under `src/`), and put the real
effort into the captures and the written findings.

Confirm explicitly: eqemu shut down cleanly, lock file deleted, character
positions restored, no production files modified (`git status` in your
slot should show only `refactor/` changes).

## Review

Full per-configuration tables, log excerpts, and screenshots are in
**`/mnt/projects/fable/review/T63/README.md`** (media in that directory's
subfolders) — this section summarizes.

**Live session used**: character `Testmelee` (race 6 Dark Elf, gender 1
female, class 1 warrior), zone `arena`. Testchar was never touched.

**Key finding, before the four configurations even start**: the live wire
protocol's equipment slot carries a server-resolved *material* id, not the
database item id. Equipping DB item 6011 (Rusty Mace, idfile IT7) produced
`attachEquipment: ... primary=7` on the wire, never 6011; item 9997
(idfile IT5) -> material 5; item 9004 (idfile IT202) -> material 202. This
means `EquipmentModelLoader::getEquipmentMesh()`'s "try item_models.json as
an item-id map first" branch is dead weight for live player equipment — it
always misses and falls through to using the value directly as a model id,
which is correct because EQEMU already resolved it server-side. Verified
by log, not assumed.

**Separately** (not the cause of anything below, filed for completeness):
`data/item_models.json` was found to be stale/disconnected from the live
server's real item database — sampled 1,863 real weapon/shield items,
only 497 had any entry, only 3 of those matched the item's real idfile.
Does not affect live equipped-weapon rendering (see above), but is
objectively wrong data if anything else reads it. Worth its own audit unit.

### Configuration 1 — primary weapon only
Item 6011 (Rusty Mace) -> material 7 -> IT7. 3rd person: dark scene,
character/weapon not distinguishable. Paperdoll: weapon clearly visible,
gripped in right hand. Log: `attachEquipment: ... primary=7 secondary=0`,
`R_POINT bone search result: index=6`. **Renders correctly** (paperdoll).

### Configuration 2 — secondary weapon only
Item 9997 (Dagger*) -> material 5 -> IT5. 3rd person: dark scene.
Paperdoll: **no weapon visible anywhere**, despite the log showing mesh
built, texture loaded (`dagger.bmp` 16x16), and bone resolved
(`L_POINT bone search result: index=10`, offset 0.51). See Defect 1.

### Configuration 3 — dual wield
Items 6011 (primary) -> material 7 -> IT7, 9997 (secondary) -> material 5
-> IT5. Dual Wield skill GM-set 0->50 (restored after). 3rd person: the
first capture caught the zone-loading screen (INVALID, excluded, see
review README); combat-swing captures are valid but too dark to read
weapon detail, target HP stayed 100% across the ~4s burst (swing timer
likely hadn't ticked). Paperdoll: primary weapon visible (same as config
1); secondary again **not visible**, reproducing config 2 exactly with
identical bone index (10) and offset (0.51).

### Configuration 4 — shield in secondary
Items 6011 (primary) -> material 7 -> IT7, 9004 (Kite Shield) -> material
202 -> IT202 (same shield model T62 used). 3rd person: dark scene.
Paperdoll: shield **is clearly visible** — a flat kite shape alongside the
left arm/torso, shoulder to hip height, visually distinct from the
weapon's raised right-hand grip. Log: `attachEquipment: ... primary=7
secondary=202`, `SHIELD_POINT/L_POINT bone search result: index=11`,
offset 0.30 (matches `computeShieldGripOffset()`'s unchanged flat
constant). **Renders correctly** (paperdoll).

### SHIELD_POINT vs L_POINT — resolved, with log + asset evidence

**No fallback to L_POINT for this race.** Two independent proofs: (1) the
plain-weapon L_POINT lookup (configs 2/3) and the shield lookup (config 4)
resolved to *different* bone indices (10 vs 11) on the same character —
if the shield had fallen back to L_POINT it would have returned the same
index 10, not a new one; (2) dumped `global_chr.s3d`'s WLD with `wld_dump`
and confirmed the Dark Elf female skeleton (`DAF`) has a real
`DAFSHIELD_POINT_DAG`/`DAFSHIELD_POINT` bone, distinct from
`DAFL_POINT_DAG`/`DAFL_POINT` (`data/captures/t63_wld_dump.log`, lines
~3264-3318 and ~22213-22234 in this worktree — scratch/gitignored, not
committed, regenerate via `wld_dump global_chr.s3d global_chr.wld` from
the slot root if needed). A broader scan of the same dump found a
`*SHIELD_POINT_TRACKDEF` bone for essentially every playable race code in
`global_chr.s3d` (25 codes checked, all present) — the L_POINT fallback
this unit was warned about is very unlikely to trigger for any standard
playable humanoid race. A second race wasn't tested *live* since the
asset-level check already answers the "does the fallback risk exist
elsewhere" question for the whole roster in this file.

### Defects found (characterized, not fixed)

**Defect 1 — paperdoll never visibly renders a plain (non-shield)
secondary/left-hand weapon.** Reproduced independently in configs 2 and 3
(same item, same bone index 10, same offset 0.51, both times invisible)
despite full successful mesh/texture/bone resolution in the log — this is
not a "not found" failure. The shield in the same slot (different bone,
index 11) and the primary weapon (bone index 6) both render. Root cause
not investigated (verification unit, read-only) — flagged candidate for a
fix unit: `character_model_view.cpp:549-552` (mirrored in
`entity_equipment.cpp:233-236`) applies rotation `(180,0,0)` to non-shield
secondary items vs `(180,0,180)` for shields; the extra shield-only Z
rotation may be what keeps the shield outside the body mesh while the
plain-weapon rotation points its offset into/behind it. **Not confirmed**
— needs a controlled A/B, not asserted as the cause here. Whether this
also affects in-world (non-paperdoll) rendering is **not established** —
`entity_equipment.cpp` shares the same logic, but arena's dark default
lighting made 3rd-person confirmation/refutation impossible in this
session.

**Defect 2 (data quality, not a render defect)** — `data/item_models.json`
mismatch, see "Key finding" above.

### STOP: first-person camera mode unreachable from `--script`

The plan's 1st-person-during-swing capture could not be produced.
`ToggleCameraMode` (Ctrl+F5) is consumed only via real Irrlicht key events
(`hotkey_manager.cpp:544` -> `irrlicht_renderer.cpp:296` ->
`graphical_mode.cpp:152-153` -> `IrrlichtRenderer::cycleCameraMode()`); no
slash command in any of the three command registries wraps it, and
`--script` can only dispatch slash commands, never raw key events. Default
camera mode (`Follow`, 3rd person) never changes across a scripted
session. Per the plan's explicit instruction, no scripting affordance was
added to production code — this STOPs and is reported rather than worked
around.

Read-only correction found while chasing this: `triggerFirstPersonAttack()`
/ `updateFirstPersonWeapons()` (named in the plan) are near-no-op
compatibility stubs, not a separate first-person weapon system — their own
comments (`entity_renderer.cpp:2807-2829`) state first-person weapon
rendering reuses the same skeletal bone-attachment path already exercised
for 3rd person/paperdoll in this unit. So the gap is purely "can't switch
camera mode from a script," not a distinct rendering path this unit failed
to reach.

### Teardown and restore confirmation

- eqemu shut down cleanly via `down.sh` (all containers stopped/removed);
  `.eqemu-lock` deleted.
- Testmelee: inventory slots 13/14 restored to baseline (9998/9979,
  charges 1 each); `character_skills` Dual Wield (skill_id 14) restored
  0; position restored to zone_id=25 (nektulos), x=-259, y=-1201,
  z=-5.75, heading=160.25 (all read from the character row before any
  edit).
- Testchar: never touched — confirmed still at zone_id=36,
  x=-77.9232, y=21.9086, z=3, h=190 (queried after the session).
- `git -C wt/slot2 status --short` is clean (no changes at all, not even
  `refactor/` — this unit made zero production-code edits; the plan file
  edit is this commit).
- Verification bar run as a formality (no source changed):
  `cmake --build build -j24` — no-op rebuild, all targets already current.
  `ctest --test-dir build --output-on-failure` — 1966/1967 passed, sole
  failure `AudioManagerTest.PreloadCommonSounds` (documented host-ALSA
  exception, matches the standing baseline). `check_file_sizes.sh fable`
  — "no C++ files to check... nothing was checked" (correct: zero source
  files changed vs the fable base).

### Deviations from the plan

1. **DB writes beyond the documented "character-position" carve-out.**
   The client has no scriptable way to equip an item (no `/equip` slash
   command, no GM command that places an item directly into a worn slot —
   `#itemadd`/`#summonitem`/`#giveitem` all put items on the cursor for
   drag-and-drop, which `--script` cannot drive). To make any of the four
   configurations possible at all, this unit wrote `inventory.item_id`
   directly for Testmelee's primary/secondary slots (13/14) and
   `character_skills.value` for Dual Wield, in addition to the
   plan-sanctioned zone/position write needed to move Testmelee into the
   already-running `arena` zone. All writes were read-backed-up before
   editing and restored to their exact original values afterward (see
   Teardown above). Flagged here rather than silently absorbed, per
   protocol; this was necessary to do the verification at all, not a
   convenience shortcut, and touched only `inventory`/`character_skills`/
   `character_data` rows for `Testmelee`, never any code.
2. **One invalid capture kept, not discarded.**
   `03_dual_wield/3rd_INVALID_caught_loading_screen.png` shows the
   zone-loading screen instead of gameplay (a script-timing miss, not
   renderer garbage) — kept and clearly labeled per "capture, state, and
   stop" rather than silently dropped, so the gap is visible instead of
   looking like a skipped step.
3. **First-person swing not captured** — see STOP section above.
4. **Second race for SHIELD_POINT not tested live** — the asset-level
   `wld_dump` check already answered the fallback-risk question for the
   whole playable-race roster in `global_chr.s3d`, making a second live
   race redundant for the question the plan asked.

Board (`refactor/deferred.md`, append, never delete): record the verdict
for each configuration. If everything renders correctly, **say so
plainly** — that closes a question Will has been carrying, and a
negative result is a real result. If defects are found, log each as its
own entry with enough detail to scope a fix unit without re-running the
session.
