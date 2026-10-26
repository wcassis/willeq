# B1162 (#481): combat-ability + discipline FX on the real client path

**Outcome: BLOCKED — neither leg landed on the real client path.** Both legs ran
cleanly (rc=0, 56 steps each) on lane 5, observer rig, and both were silent on the
wire. The two HARD RULES tripped: zero C->S `OP_CastSpell` (the /disc leg) and
zero C->S `OP_InstillDoubt` (the Intimidation leg). This documents the legs, the
evidence, and the class-ownership investigation that corrects B1143's lead.

## The two legs

Both legs: caster char **67 Testcaster**, observer char **103 Oraclewatch**, lane
5, `run_leg_b1162.sh <plan> <class>` (class is switched in `character_data` by
`oracle_class_leg.sh`, restored post-SQL with the world down). Plans in
`plans/`, censer `scripts/pcap_census.py` (generalised from B1159).

### Leg 1 — `/disc` on the OWNING class (record 325/326), class 5 Shadow Knight

`plans/b1162_disc325.tsv` (52 steps): login, `#level 60`, `#traindisc 60`,
`/disc Leechcurse Discipline`, `#resetdisc_timer all`, `/disc Reaper Strike`,
`/disc Lich Sting`.

**Result: 0 C->S `OP_CastSpell`.** The client swallowed every `/disc` silently —
the step opcodes show only keepalive (`OP_ClientUpdate`/`OP_FloatListThing`) at
each `/disc`, exactly B1143's "swallowed" signature. No S->C `OP_Action`/
`OP_SpecialMesg` landing. **HARD RULE tripped.**

### Leg 2 — Intimidation -> Fear 229 (record 13 / spells.eff record 101), class 1 Warrior

`plans/b1162_combat13.tsv` (56 steps): login, `#level 60`, `#set skill 71 5000`
(Intimidation, in-world — skill rows are created at login so the pre-SQL would be
a no-op), `#spawn TrollF`, `/target`, `#stun 120000`, then `press G` (the
decompile's VK 0x47 OP_InstillDoubt sender), a 14 s wait, then `/doability 1..5`
+ `alt+c` fallbacks.

**Result: 0 C->S `OP_InstillDoubt`, 0 C->S `OP_CastSpell`, 0 S->C `OP_Action`
spell 229/209.** **HARD RULE tripped.**

**The setup was verified correct from the stills + opcodes:**
- Character was a **Warrior (class 1)** (the `character_data` switch; the
  `char-state-before.tsv` queries the separate unswitched `characters` table and
  shows 12 — misleading, the leg's `.run.out` pre-SQL `UPDATE character_data
  SET class=1 WHERE id=67 AND class=12` confirms the switch).
- `#set skill 71 5000` took effect — chat shows **"You have become better at
  intimidation!"** (the press-G still, step 25).
- Target set: step 21 `OP_TargetCommand target=262` (TrollF000) C->S and
  S->C-confirmed.
- Stun landed: step 23 `OP_SpecialMesg` x2 ("You stunned TrollF (262)").
- Character standing (no sit indicator), NPC in range.
- **The caster's key presses DO reach the client**: the step-50 `alt+c` press
  toggled the Combat Abilities window closed (the stills differ), so xdotool key
  delivery is not the issue.

So the G-press reached a standing Warrior with a valid in-range target and the
Intimidation skill set, and still sent nothing.

## The class-ownership investigation (corrects B1143's lead)

B1143 ran the /disc leg on "class 4 (Shadow Knight)". The Ti `spells_us.txt`
class-ownership columns **f104..f119 are per-class level caps** (class N at
f(103+N)); f121 is a `/disc`-triggerable flag, NOT a class id (B1143's "field 121
= class 13" was a misread). The record-325 disciplines
(8604/8617/8642 Leechcurse Discipline variants) carry **f108=60 = class 5
(Shadow Knight) level 60** — the owning class is **5, not 4**. So B1143's leg ran
on a class that does NOT own the disciplines, which is a genuine cause of a silent
`/disc`.

**B1162 re-ran the leg on the correct owning class (5) and it was STILL silent**
(0 C->S `OP_CastSpell`), with the character at level 60 and `#traindisc 60`
trained. So **the class-ownership fix was necessary but not sufficient** — there is
a second gate. The press-G... (disc leg) step-25 still shows **"You have learned 0
new disciplines!"** in chat: the `#traindisc 60` feedback reports zero disciplines
learned, even though `character_disciplines` had 6 rows for char 67. This suggests
the Leechcurse discipline was not actually available/trained to the level the
client's `/disc` resolution requires — the discipline-training state (or a
client-side `/disc` name-resolution / availability gate) is the next thing to
check, not the class.

## What the oracle draws on /disc (B1130 rows 325/326)

B1134 P4 (the /disc that DID work on its own class) and the B1130 row 325/326
record-325 spells have **no Target stage** — the /disc landing is a self/caster
effect. So when `/disc` does fire, the observer would see a caster-side effect,
not a target landing. (Moot here: the leg never sent the packet.)

## Lane state (before == after)

- `lane_before.txt` (pre-leg): char 67 = class 12 (Wizard) level 1 race 6;
  char 103 = class 1 (Warrior) level 1 race 6; stack up.
- Both legs' post-SQL (world down) restored char 67 to class 12, level 1, race 6,
  gems `0/93 1/38 2/54 3/41 4/253 5/458 6/656`, and the original
  `character_skills`/`character_disciplines`/`timers`. Char 103 (observer) was not
  modified by either leg. Stack left down by the legs' `down.sh` step (expected).

## Next steps (for the orchestrator) — this is a block, not a FOLLOW-UP

1. **`/disc` (record 325/326)**: the owning class is 5 (verified), but the leg was
   still silent at level 60 + `#traindisc 60` with "0 new disciplines" learned.
   Check the discipline-training state the client's `/disc` resolution requires
   (a trained-level column, or a client-side name/availability gate) — the class
   was not the only gate.
2. **Intimidation (record 13 / 229)**: G-press reached a standing Warrior with a
   valid in-range target and Intimidation set, yet sent nothing. The decompile's
   VK 0x47 gate (`*(caster+0x251)=='d'`) is more than "standing" — likely requires
   the caster **in combat** (the card's option (c)) and/or Intimidation **equipped
   in a Combat Abilities slot** (option (b); the CA window showed empty slots).
   The next leg needs the character to attack (enter combat) and/or equip
   Intimidation before the G press.
3. Harness rows + two ledger clips (ask #3/#4) are moot until a leg lands.

## Evidence

- `plans/b1162_disc325.tsv`, `plans/b1162_combat13.tsv` (both pass
  `oracle_plan_check.py`).
- `~/kb-evidence/kb-b1162-combat-fx/`: `disc325_census.txt`, `combat13_census.txt`,
  `oracle/b1162_disc325/`, `oracle/b1162_combat13/` (stills + per-step opcodes +
  pcap), `disc325_finding.md`, `lane_before.txt`, `lane_after.txt` (stack down at
  probe; restoration confirmed by the legs' post-SQL output in `*.run.out`).
- Class-ownership probes: `probe_b1162_fields.py`, `probe_b1162_classcols.py`,
  `probe_b1162_encode.py` (in `~/kb-evidence/kb-b1162-combat-fx/`).
