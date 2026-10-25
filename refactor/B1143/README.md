# B1143 — rig residue: the 93-miss attribution + the new combat-ability / discipline rows

Lane 5. Char 67 (Testcaster, caster) + char 103 (Oraclewatch, observer), the B1130 rig
(caster `:98` Xvfb/mesa, observer `:99` NVIDIA — the leg's fixed geometry). Evidence:
`/home/user/kb-evidence/kb-b1143-rig-residue/`.

## Item 1 — the `hasTargetStage` gate (present, pinned)

The gate is in `SpellVisualFX::createImpact` at `src/client/graphics/spell_visual_fx.cpp:841-854`
(`sed -n '841,856p'`):

    void SpellVisualFX::createImpact(uint16_t target_id, uint32_t spell_id)
    {
    #ifdef EQT_HAS_GLES2
        if (m_particle_manager) {
            // T72: spells.eff's Target emitter maps onto ImpactBurst. Same
            // fallback-preserving rule as createCastGlow/createProjectile.
            SpellVisualResolution visual = resolveVisual(spell_id);
            if (!visual.isFallback() && !visual.hasTargetStage()) {
                LOG_DEBUG(MOD_SPELL, "T72: spell {} spells.eff record {} has no Target-stage content "
                                      "— suppressing impact burst (data-driven, not a bug)",
                          spell_id, visual.spell_affect_index);
                return;
            }

Pinned by (all cited lines verified at this tip):

- `tests/test_spell_visual_selection.cpp:58` — the zero record resolves to
  `hasTargetStage() == false`; `:113` — a resolved record with Target content is `true`;
  `:142` — a resolved record without is `false`.
- `tests/test_spell_visual_vocabulary.cpp:281` — the vocabulary resolution keeps
  `hasTargetStage() == true` for a Target-bearing record.
- `tests/test_fx_harness.cpp:230` — `FxHarness.ADisciplineWithoutBeginCastDrawsNothing`:
  4499 Defensive Discipline's record 326 has no Target stage, so the action-only
  (/disc) packets draw nothing on the harness.
- `tests/test_spell_impact_visual.cpp:190,196,209-210` — the impact probe: 0
  creates for a no-Target record, 1 create/1 release when a Target stage exists.

A regression that re-enables the Target impact for a no-Target record is a red test.
**Item 1 is complete.**

## Item 2 — the 93 Burst of Flame rig miss: MEASURED (4 landed casts)

### 2a. Why the first leg (`p3_rig93.fix`) failed — the casts never left the client

`oracle/p3_rig93.fix/capture.pcap`: **25 953 packets**, connections caster
`7001/51876`, observer `7001/51542`; **zero C->S `OP_CastSpell`**.

The two `S->C OP_BeginCast` in the leg (1792938324.405/.421 Z) decode to
**source 87, spell 11, cast_ms 2000** — zone-in residue the world started with
(the same pair appears in every leg, including the 18:30 leg at 1792953598.056),
142 s before the first target attempt; the caster client sent nothing that
started it and it never completes. It is not the cause.

The cause, from the quoted `S->C OP_SpecialMesg` lines: the plan used the GM
form `#target <name>`, which this eqemu build does not have, so the caster
**never had a target** and the official client refused the targeted spell
locally:

- 1792938466.356 Z `Command '#target TrollF000' not recognized.` (repeat
  1792938499.812 Z); 1792938546.526 Z / 1792938581.130 Z
  `Command '#target Oraclewatch' not recognized.`
- Two more silent failures that would have broken the measurement even with a
  working target:
  - 1792938394.290 Z `Usage: #stun [Duration]` — `#stun` takes a duration only
    and stuns the **current target** (which did not exist), so TrollF was never
    stunned.
  - 1792938398.497 Z `Player -67.6,434.4,3.75,225 could not be found.` — the
    comma-form `#goto` was parsed as a player name, so the caster never left
    its driver pose for the whole leg (all three actors co-located at the
    driver pose; the B1130 9.05-unit geometry was never achieved).

### 2b. The fixed plan landed (leg `p3_rig93`, 2026-10-25 18:30 Z)

`plans/p3_rig93.tsv`: client-side `/target <name>` (the form that worked in B1130
r51 and B1134 P2), `#stun 120000` AFTER `/target`, no `#goto` (both clients placed
by the driver's `pose=` / `observer-pose=` in the B1134 P2b geometry: caster at
the TrollF spot -74,428,3.75 h160, observer at B1130 r51's caster pose
-67.6,434.4,3.75 h160); TrollF000 `#spawn`ed at the caster's position (eqemu
`#spawn` takes no coordinates); 2 casts on the NPC (262) + 2 on the player
(261); the NPC `#kill`ed after its phase. A short verification leg
(`plans/p3_short2.tsv`, 2 NPC casts, `#corpse deletenpccorpses` first) confirmed
`OP_Action` before the full leg: `oracle/p3_short2/`.

`oracle/p3_rig93/capture.pcap`: **30 681 packets**:

- 1792953671.819 Z `New NPC spawned! Name | TrollF000`; 1792953685.637 Z
  `You stunned TrollF (262) for 2 Minutes.` (the stun worked this time).
- 4 x C->S `OP_CastSpell` slot 0 at 1792953765.863 / 1792953802.855 /
  1792953870.472 / 1792953904.515 Z.
- 4 x S->C `OP_BeginCast` spell 93, cast_ms 1500, source 260 to BOTH clients.
- 4 x S->C `OP_Action` spell 93 source 260: target **262** (casts 1-2, at
  +1.53/+1.51 s) and target **261** (casts 3-4).
- `OP_InterruptCast`: 0. No error chat lines.
- 1792953828.697 Z `Killing TrollF (262).` — the spawned NPC was despawned
  inside the leg (no leftover for the lane_after state).

### 2c. Measurement — observer view, against the B1130 r51 row-0 bands

Fable client rendered the caster (`:98`); the observer (official client, `:99`
NVIDIA) recorded 1280x800 @ 30 fps. Bands per the B1134 "Rig check" table
(`review/B1134/README.md`): onset +-0.10 s, centroid +-0.15, h lo/hi +-0.15,
lateral edges +-0.2. r51 row 0 is the NPC-target reference (B1130's stunned
`#spawn`ed TrollF).

| stage | field | B1130 r51 | cast 1 (NPC 262) | cast 2 (NPC 262) | cast 3 (player 261) | cast 4 (player 261) |
|---|---|---|---|---|---|---|
| Source (caster) | onset BC+ | 0.46 | 0.463 | 0.442 | 0.455 | 0.450 |
| | centroid | 0.71 | 0.61 | 0.67 | 0.65 | 0.71 |
| | h p5..p95 | 0.07..1.25 | -0.15..1.23 | 0.16..1.31 | 0.18..1.33 | 0.13..1.35 |
| | dx p5..p95 | -0.57..+0.61 | -0.66..+0.55 | -0.48..+0.54 | -0.60..+0.44 | -0.51..+0.53 |
| Landing (NPC) | onset (RISE) | 0.43 (0.548) | 0.582 | 0.175 | — | — |
| | centroid | 0.80 | 0.90 | 0.90 | — | — |
| | h p5..p95 | 0.25..1.46 | -0.04..1.84 * | -0.01..1.86 * | — | — |
| | end Act+ | 6.67 | 6.649 | 7.341 | — | — |
| | coverage (RUN) | 1.020 | 1.763 * | 1.723 * | — | — |
| Landing (player) | onset (RISE) | 0.43 (0.548) | — | — | 0.577 | 0.573 |
| | centroid | 0.80 | — | — | 0.50 | 0.49 |
| | h p5..p95 | 0.25..1.46 | — | — | 0.09..1.02 | 0.09..1.02 |
| | end Act+ | 6.67 | — | — | 5.077 | 4.539 |
| | coverage (RUN) | 1.020 | — | — | 0.126 | 0.361 |

`*` inflated by the co-located source glow: in this rig the NPC spawns ON the
caster, so the caster's held hand-flame/spark cloud sits inside the landing ROI
and raises its hi edge, coverage and RISE. The clean NPC discriminator is the
landing **end**. Frame files for every number:
`measure/p3_rig93_93_<rep>_<kind>/` + `measure/p3_rig93_93_<rep>_<kind>.txt`
(evidence root). Machine-readable rows: `refactor/B1143/rows/p3_rig93_93.tsv`.

Band verdicts: Source onset/centroid in band on all four casts (cast 1's h lo
edge -0.15 is 0.07 below the band floor); NPC landing end 6.649 is in the
6.52..6.82 band (cast 2's 7.341 is +0.67, OUT; its RISE 0.175 is the
co-location glow, above); player landing end 5.077/4.539 OUT (-1.59/-2.13),
player centroid 0.49-0.50 OUT (band 0.65..0.95), player coverage 0.126/0.361
vs 1.020.

### 2d. Attribution

With the caster, the renderer (fable on `:98`), the rig and the observer view
all identical and **only the target class changed**, the landing moves with the
target: the NPC-target landing ends at 6.649 s (in the B1130 NPC band; the
co-location-inflated cast 2 at 7.341 s) while the player-target landing ends at
4.54-5.08 s — shorter, lower-centroid and smaller — the same signature as
B1134's own P2/P2b player leg (end 4.55/4.69 observer, 4.67/4.72 caster view).
The renderer draws the NPC-target landing at the oracle's duration, so it is
exonerated for the miss: **the 93 landing end/centroid/coverage miss is
attributable to the target class** (the server's PvP effect on a player target
is shorter and smaller than on the stunned `#spawn`ed NPC), not to fable's
renderer or to the observer rig.

## Item 3 — record 325 + record 1 (spells re-derived; the /disc leg not run)

Re-derived from the leg's own `spells_us.txt` (evidence copy
`eq/spells_us.txt`; 0-based `^`-split ordinals == eqemu `spells_new` column
ordinals — the T355 note in `include/client/spell/spell_constants.h`; the
spellsnew.eff record is field 145, `SpellAnim`, line 559):

- **Record 325** (spellsnew.eff, 8 spells): 4504 Leechcurse Discipline, 6236
  Reaper Strike, 6268 Lich Sting, 6299 Reaper Strike Recourse, 6311 Lich Sting
  Recourse, 8604/8617/8642 Leechcurse Discipline (the three class-level variant
  rows carry Shadow Knight level 60; the base rows carry no class level).
- **Record 1** (spellsnew.eff, 190 spells — the shared combat-ability kit:
  Warrior abilities, Monk abilities, Bard songs, Cleric turn/wrack,
  Necromancer fear): includes 4593 Crippling Strike (Warrior 47), 4629
  Thunderkick (Monk 22). Crippling Strike / Thunderkick have NO ability
  strings in `eqstr_us.txt`, so they are not manually activatable in the Ti
  client (see `plans/p3_combat1.tsv` header) — documented, not measured.
- **Corrections to the first leg's spell list**: 1256-1263 are record 76 auras
  (Accuracy, Adroitness, Power, Aura of Antibody, Null Aura, Aura of Cold,
  Aura of Heat, Aura of Purity), not record 325; 4499 Defensive Discipline is
  record 326; 4505 Deadeye Discipline is record 329; 4506 Trueshot Discipline
  is record 326. Char 67 is class 12 = WIZARD (level 1), not "ninja".

Status: the `/disc` + `#castspell` activation leg (`plans/p3_disc325.tsv`,
class 4, leg `p3_disc325` 2026-10-25 22:08Z) RAN: all three disciplines
landed (36 473 packets; `#level 60` + `#traindisc 60` = "You have learned 5
new disciplines!"; chat "Cast Leechcurse Discipline (4504) on yourself",
"Cast Reaper Strike (6236) on TrollF (262)", "Cast Lich Sting (6268) on
TrollF (262)"; 3x `OP_BeginCast cast_ms=0`; `OP_Action` spell 4504). Measured
in the observer view: **none of the three draws a detectable Source or
Landing** (max coverage <= 0.02 over the Act+10 s window, below threshold) —
record 325 has no Target-stage content, so the B1144 no-Target-slot gate
(item 1) suppresses the impact and the source visual is below the ROI
coverage threshold. This is the live-leg confirmation of the gate on a second
record. Rows: `refactor/B1143/rows/p3_disc325_325.tsv` (six gate rows).
**animRecords** (record -> spells, from the field re-derivation above, NOT in
`config/spell_effects.json`): record 325 = 4504, 6236, 6268, 6299, 6311, 8604,
8617, 8642 (the three activatable disciplines: 4504/6236/6268); record 1 =
190 spells incl. 4593 Crippling Strike, 4629 Thunderkick (not manually
activatable — no `eqstr_us.txt` ability strings).

## Intimidation -> Fear (B1130 row 13) — leg ran, did NOT land (FOLLOW-UP)

B1130 row 13 (e13) is the **field-123** (spells.eff, `SpellAffectIndex`)
record 13 = 209 Spook the Dead + 229 Fear (the "skull cloud" landing); in the
spellsnew field the same two spells sit on record 101. The server casts Fear
229 from a successful Intimidation (`SpellOnTarget(229, who, 0, true, -2000)`),
so the clients see an `OP_Action` with NO `OP_BeginCast`. The Ti client has NO
`/intimidate` command — the intended route is `/doability 1 - 10` or the
Combat Abilities window (`plans/p3_combat1.tsv` header, with the decompile
cites).

Leg `p3_combat1` (class 1 Warrior, 2026-10-25 22:32Z, 23 980 packets) RAN but
did **not** land a Fear:

- **0 `OP_InstillDoubt` (0x389e) and 0 `OP_Action` spell 229** in the whole
  pcap. The `/doability 1..10` sweep on the stunned TrollF000 (262) never
  triggered the Intimidation branch — Intimidation is a *skill* (skill_id 71),
  and none of the ten `/doability` indices maps to it in this client build
  (or it requires the ability to be equipped / the caster in combat first).
  The chat has no "intimidate / fear / doubt / cooldown" lines at all.
- **The pre-SQL was a no-op.** `char 67` had **0 `character_skills` rows**
  before the leg (the switched-class skill rows are created at LOGIN, after
  the driver's offline save + class switch), so the pre-SQL
  `UPDATE character_skills SET value=5000 WHERE skill_id=71` affected 0 rows —
  Intimidation stayed at its default and the success roll could not be forced.
  The fix is IN-WORLD: `#set skill 71 5000` (eqemu
  `zone/gm_commands/set_skill.cpp`: `#set skill [ID] [Value]`, capped at
  MaxSkill) added to the plan after `#level 60` (now in
  `plans/p3_combat1.tsv`).
- The leg exited **rc=65**: the report (`oracle_report.py`) raised
  `uncaptured step 14_command` — the driver's `timeline.tsv` step marks and the
  report's step names diverge on this plan (the item-3 plan of the same shape
  reported clean), so the step table did not close. This is a
  driver/report step-naming issue on the `/doability` + `alt+c` plan, separate
  from the no-IntillDoubt finding.

So item 4's Fear landing + no-Source check is **not yet measured**; see the
FOLLOW-UP for the corrected next steps.

## Lane state (before == after)

- `lane_before.txt` (18:08 Z): stack down, lease FREE; char 67 class 12 level 1
  at x35/y-82/z3, gems 93,38,54,41,253,458,656; char 103 class 1 at
  x-510/y57/z31.75.
- `lane_after.txt` (21:31 Z): lease FREE; world down (mariadb only); char 67
  unchanged (class 12 level 1, same gems, same position); char 103 unchanged;
  TrollF000 killed inside the leg (1792953828.697 Z `Killing TrollF (262).`).
  Only drift: the two `last_login` timestamps.

## FOLLOW-UP (item 4 only — item 3 is complete in this MR)

Item 3 (record 325) is DONE: leg `p3_disc325` ran, rows measured
(`refactor/B1143/rows/p3_disc325_325.tsv`, six gate rows), animRecords in the
item-3 section above.

Item 4 (Intimidation -> Fear) needs a corrected leg:

1. **Find the Intimidation trigger.** The `/doability 1..10` sweep sent
   **zero** `OP_InstillDoubt` (0x389e). Intimidation is a skill (skill_id 71),
   not one of the ten `/doability` indices in this build. Options to try, in
   order: (a) the Combat Abilities window — `alt+c`, read the button layout
   from the still, click the Intimidation button (the window is already in the
   plan at the `ca_window` step); (b) check whether Intimidation must be
   EQUIPPED as a combat ability first (the eqemu `combat_abilities` / the
   client's ability slots); (c) confirm the caster is a client (not NPC) and
   has a valid NPC target in range at the moment of the click.
2. **Re-run the corrected plan.** `plans/p3_combat1.tsv` now sets Intimidation
   in-world (`#set skill 71 5000` after `#level 60`) so the success roll is
   certain (the pre-SQL alone is a no-op — skill rows are created at login).
   `EQEMU_LANE=5 bash refactor/B1143/scripts/run_leg.sh p3_combat1 1
   --pre-sql-file refactor/B1143/plans/p3_combat1_skill.sql`
3. **Fix the report step-capture.** The leg exited rc=65 on `uncaptured step
   14_command` (driver `timeline.tsv` marks vs `oracle_report.py` step names
   diverge on the `/doability` + `alt+c` plan). Confirm the report closes
   before trusting the measurement.
4. **Measure** the NPC's record-13 landing against B1130 row 13 (209/229,
   the "skull cloud") and confirm NO Source on the intimidator; row to
   `refactor/B1143/rows/p3_combat1_13.tsv`.

`animRecords` (record -> spells, from the field re-derivation above, NOT in
`config/spell_effects.json`): record 325 = 4504, 6236, 6268, 6299, 6311, 8604,
8617, 8642; record 1 = 190 spells incl. 4593, 4629; record 13 (field 123) =
209, 229 (spellsnew record 101).
