# B1143 — rig residue: the 93-miss attribution + the new combat-ability / discipline rows

Lane 5. Char 67 (Testcaster, caster) + char 103 (Oraclewatch, observer), the B1130 rig
(caster `:98` Xvfb/mesa, observer `:99` NVIDIA — the leg's fixed geometry). Evidence:
`/home/user/kb-evidence/kb-b1143-rig-residue/`.

## Item 1 — the `hasTargetStage` gate (VERIFIED, present and pinned)

The fix is at `src/client/graphics/spell_visual_fx.cpp:849`:

    // B1143: a spell that names no Target stage (e56, e262, e13) draws its
    // Source only; the gate here stops it from also drawing a Target at the
    // player even when the effect carries a target id.
    if (!visuals::HasTargetStage(rec, name) &&
        !ctx_.spellEffects()->hasTargetStageFor(name) &&
        (e.type == GameEventType::Effect || e.type == GameEventType::EffectFlag)
        && e.data.targetID != e.data.spawnerID &&
        e.data.targetID != EQT::GameState::PLAYER_ENTITY_ID)
        continue;

`tests/test_has_target_stage.cpp` pins the rule with the three measured rows
(e56 `Reap Soul` source-only, e262 `Ritual of Rebirth` no-target, e13 `Fear` /
`Spook the Dead` source+target). A regression that re-enables the Target for a
no-target record is a red test. **Item 1 is complete.**

## Item 2 — the 93 Burst of Flame rig miss (leg captured; the casts did not land)

The leg `oracle/p3_rig93.fix/` ran the card's design:
- caster (Testcaster) class 6 Druid at the B1130 pose (`#goto -71.6,431.6,3.75,225`,
  heading 225) — the same spot as the B1130 reference leg;
- observer (Oraclewatch) at the TrollF spot (`#goto -67.6,434.4,3.75`), three
  fixed-camera sweeps + 36 s settle + record;
- target `#spawn TrollF` (named `TrollF000` by the server) — the **NPC** target,
  plus `#target Oraclewatch` for the **player** target, so the leg captures both
  the NPC-vs-player comparison and the renderer variable (this leg's caster is
  `:98`/mesa, the B1134 leg's was `:99`/NVIDIA);
- 4 casts: 2 on the TrollF (NPC), 2 on Oraclewatch (player), `/cast 1` (gem 0 = 93).

Captured: `video.mp4` (94 MB), `video-caster.mp4`, `capture.pcap` (1.9 MB, 41 648
packets), 100 stills, `timeline.tsv`, `video.calibration.tsv` (epoch 1792935218
basis, 30 fps), `measure/p3_rig93.fix_casts.json`.

**FINDING — the 4 casts did not land.** The pcap shows **2 OP.BeginCast (caster
port) and 0 OP.Action anywhere** (no effect on either client). `obs_measure.py`
returns no cast windows. The caster video confirms it: the Druid enters a cast
pose but **no flame renders and the chat is empty**. So the 93-miss cannot be
attributed (NPC-vs-player vs renderer) from this leg — the casts must be made to
land first.

Root cause of the cast failure, two parts:
1. **Target (fixed).** Run 1 used `#target TrollF`; the server names the spawned
   NPC **`TrollF000`** (confirmed in the pcap `OP_NewSpawn`), so the chat read
   `Target 'TrollF' not recognized` → `You must first select a target for this
   spell!`. The fix (use `TrollF000`) is in `refactor/B1143/plans/p3_rig93.tsv`.
2. **The cast still did not complete.** After the target fix the 4 `/cast 1`
   still produced 2 BeginCast / 0 effect — the B1134 leg's identical `/cast 1`
   did land, so the difference is in this leg's setup (the drive-cast timing vs
   the 2.5 s cast time, the class-6 gem state, or a stale world/zone). This is
   the open item.

## Item 3 — the record-325 disciplines + record-1 combat abilities (spells identified)

From `spells_us.txt` (field 145 = the record id, `^`-delimited):
- **Record 325** — Leechcurse Discipline (1256), Reaper Strike (1257), Lich
  Sting (1258) + five recourse spells (1259–1263).
- **Record 1** — ~190 spells (the combat-ability kits). Char 67's class (ninja)
  subset: e.g. 4505 Crippling Strike, 4506 Thunderkick, 4504 Defensive
  Discipline.

Status: the `/disc` activation leg and the measurement are **not done** (the
record-325 owning class is pending — a DB query on `spells_new.class` / the
spell→class mapping), and the rows are not yet measured.

## Intimidation → Fear (record 13) — the skill-triggered addition (NOT done)

Will's 2026-10-25 13:05Z addition. The server casts Fear 229 (record 13, the
same record as 209 *Spook the Dead*, the B1130 e13 'skull cloud') from a
successful Intimidation via `SpellOnTarget(229, who, 0, true, -2000)` — the
clients see an **OP_Action with NO OP_BeginCast**. The leg (char 67 as a class
with Intimidation, `#setskill`, `/intimidate` on a `#spawn` NPC at the TrollF
spot, measure the NPC's record-13 Target-stage landing + confirm NO Source on
the intimidator) is **not run** — see the FOLLOW-UP.

## FOLLOW-UP (what's needed to finish)

1. **Item 2 — make the casts land.** Diagnose why `/cast 1` here yields BeginCast
   but no effect (compare the drive-cast, gem-slot and world/zone state against
   the working B1134 `p2_rig93` leg). Re-run `p3_rig93.tsv` (target already
   fixed). Then measure the NPC (TrollF000) vs player (Oraclewatch) end/coverage
   bands in the observer view and compare to the B1130 reference — that answers
   whether the 93-miss is the target class or the caster renderer.
2. **Item 3 — the /disc leg.** Determine the record-325 owning class (DB), make
   char 67 that class, `#traindisc` + activate each record-325/record-1
   discipline through the Combat Abilities window with the observer recording,
   measure Source + landing, restore char 67 to class 12 / gems.
3. **Intimidation → Fear.** Char 67 as a class with Intimidation (`#class`/
   `#level`, `#setskill`), `/intimidate` on a `#spawn` NPC at the TrollF spot,
   repeat until one success (the pcap OP_Action 229 / the 'You intimidate'
   chat). Measure the NPC's record-13 Target-stage (skulls) landing against the
   B1130 row-13 bands and confirm NO Source on the intimidator (B1144).

Rows land in `refactor/B1143/rows/` and `animRecords` entries in
`config/spell_effects.json` as the measurements complete.
