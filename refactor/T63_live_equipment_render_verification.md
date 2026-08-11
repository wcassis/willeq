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

(executor fills: one section per configuration with item id → model id →
race → view → observation; the `SHIELD_POINT` vs `L_POINT` resolution
finding with the log evidence; every defect found, characterised but not
fixed; review-media paths; eqemu teardown and restore confirmation;
deviations)

Board (`refactor/deferred.md`, append, never delete): record the verdict
for each configuration. If everything renders correctly, **say so
plainly** — that closes a question Will has been carrying, and a
negative result is a real result. If defects are found, log each as its
own entry with enough detail to scope a fix unit without re-running the
session.
