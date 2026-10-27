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

## Attempt 2 (ruling 2026-10-26 23:05Z): the offline mechanism, then the re-run

The orchestrator ruling (task comment 23:05Z) gated this: neither silent leg is
the answer. The common factor in both attempt-1 legs was that the character's
class / level / skills / disciplines were **changed AFTER the client had zoned
in** (SQL class switch, then in-world `#level` / `#traindisc` / `#set skill`).
Step 1 of the ruling was OFFLINE: from the two pcaps, count and dump the S->C
`OP_DisciplineUpdate` and the skill entries of the zone-in player profile, and
show whether the learned disciplines / skill 71 were present in what the client
had **WHEN** the `/disc` and Intimidation steps ran.

### What the client had at zone-in (the zone-in `OP_PlayerProfile`, S->C)

Both legs received ONE `OP_PlayerProfile` (0x75df) per caster at zone-in:

| | combat13 (Warrior) | disc325 (Shadow Knight) |
|---|---|---|
| zone-in t | 1793053080.977 | 1793051842.106 |
| class / level / race | 1 / **1** / 6 | 5 / **1** / 6 |
| skills (100 x u32 @ 4460) | all 0 — **skill 71 = 0** | all 0 — **skill 71 = 0** |
| spellbook (400 x u32 @ 2312) | 87 spells — **no 113 (Shock of Spikes), no 71** | same 87 — **no 113, no 71** |
| memorised gems (9 @ 4360) | 93,38,54,41,253,458,656 | same |

The zone-in level was **1** in both legs (the in-world `#level 60` ran AFTER
zone-in), so the profile the client built its list from was a level-1, zero-skill
character — even though the class had already been switched pre-login.

### What the client received mid-session, and WHEN the step ran

**Combat leg** — `#level 60` (step 11, t=1793053176.026) then `#set skill 71 5000`
(step 12, t=1793053179.699). The server's response to the in-world `#set skill`
was an `OP_SkillUpdate` (S->C, t≈1793053180.8) carrying **skill=71 value=0** — the
GM `#set skill` was a **no-op on the wire**: the client was told Intimidation = 0,
never 5000. There is **no `OP_DisciplineUpdate` anywhere** in the combat pcap.
`press G` (step 25, t=1793053256.477) → **0 C->S `OP_InstillDoubt`, 0 C->S
`OP_CastSpell`, 0 S->C `OP_Action` 229/209.**

**Mechanism (combat):** the client's copy of skill 71 was 0 for the whole leg
(zone-in profile 0; the in-world `#set skill` answered with `SkillUpdate 71=0`).
When G was pressed the client had **no Intimidation skill**; with the decompile's
G-key gate (valid non-self target + standing + [in-combat OR known spell 113], and
the zone-in spellbook carried no 113) the press was swallowed client-side — no
C->S packet at all.

**Disc leg** — `#level 60` (step 11, t=1793051936.700) then `#traindisc 60`
(step 12, t=1793051940.049) → `OP_DisciplineUpdate` (S->C, t=1793051940.842)
delivered exactly **6 disciplines: 4504, 4520, 4585, 4587, 4590, 7005**. **6236
(Reaper Strike) and 6268 (Lich Sting) were NOT delivered.** The three `/disc`
steps then ran:

| step | t | /disc | id in client's buffer? | result |
|---|---|---|---|---|
| 25 | 1793052013.634 | Leechcurse Discipline | **4504 yes** (delivered 73 s earlier) | 0 C->S `OP_CastSpell` |
| 34 | 1793052066.213 | Reaper Strike | **6236 no** | 0 C->S `OP_CastSpell` |
| 43 | 1793052120.089 | Lich Sting | **6268 no** | 0 C->S `OP_CastSpell` |

**Mechanism (disc):** the official client's `OP_DisciplineUpdate` handler
(decompile `eqgame.exe.c` 81589) **memcpys the payload into its discipline buffer
regardless of timing** — so the client DID hold 4504 at the time of `/disc
Leechcurse`. Two of the three `/disc`s (Reaper Strike 6236, Lich Sting 6268)
name disciplines the client **never received** → their silence is exactly the
"not in the client's list" mechanism the ruling predicted. `/disc Leechcurse`
(4504 in the live buffer) being **still silent** points to one of: (a) the
client's `/disc` name-resolution uses a **zone-in-frozen** discipline list rather
than the live `DisciplineUpdate` buffer, or (b) it resolves "Leechcurse
Discipline" to the **class/level-appropriate variant (8604)** — the SHD-lvl-60
`spells_us.txt` row (class-cap column 60) — rather than the delivered generic
4504. Either way the fix is the same: put the Leechcurse discipline in the
character **before zone-in** so the client's zone-in data carries it.

### The re-run (ruling step 2)

The ruling asked for ONE combined leg with state fixed before zone-in. The lane
driver (`scripts/eqemu/oracle/oracle_remote.sh`) has **no mid-leg SQL verb** and
runs the plan as a single remote stream, so a single-leg mid-leg class switch is
not possible with the current runner. **Two legs are run instead, each fixing the
character state before its own zone-in** — the ruling's operative requirement
("the character state fixed BEFORE the client zones in") — with the same worktree,
same lane 5, same observer rig, same HARD RULES.

- **Leg A — `/disc` (class 5 SHD):** `plans/b1162_disc325_v2.tsv` +
  `sql/b1162_disc325_v2.sql`. Pre-SQL (in the lock, before login): `level=60` and
  `INSERT character_disciplines` for the Leechcurse variants **8604** (the
  ruling's /disc-triggerable pick) **and 4504** (the generic row #traindisc
  delivered — a hedge on the client's name-resolution target). Plan: login, zone,
  `/disc Leechcurse Discipline`. HARD RULE: 0 C->S `OP_CastSpell` → block with the
  pcap lines.
- **Leg B — Intimidation (class 1 Warrior):** `plans/b1162_combat13_v2.tsv` +
  `sql/b1162_combat13_v2.sql`. Pre-SQL: `level=60`, `INSERT character_skills
  (skill_id 71 = 5000)` (the in-world `#set skill` was a no-op, so the skill goes
  in the DB before login), and `INSERT character_spells (113 Shock of Spikes)`
  (the decompile's G-key gate-4 known-spell half). Plan: login, zone, `#spawn
  TrollF`, `/target`, `#stun`, then **both** the G key (the decompile's
  `OP_InstillDoubt` sender) and the `alt+c` Abilities window with a still + a
  click on the Intimidation slot. HARD RULE: 0 C->S `OP_InstillDoubt` → block
  with the opcode census.

## Leg A result — `/disc` on the owning class: LANDED

Leg A ran clean (rc=0, 16 steps, observer rig, chars 67+103 restored). The pre-SQL
disciplines were delivered to the client at zone-in (`OP_DisciplineUpdate` ids=
**[8604, 4504]**, S->C, t=1793062354.009/.015), and the `/disc` step then fired:

- **C->S `OP_CastSpell`(12363) t=1793062472.595**
  payload `0a000000 9c210000 ffffffff 00000000 d224603f` = slot 10, spell **8604**
  (Leechcurse Discipline, SHD-lvl-60 variant), self-target — B1134 P4's working form.
- S->C landing (caster side): `OP_InterruptCast`x2 + `OP_SpecialMesg`x2 +
  `OP_ManaChange`x11 / `OP_HPUpdate`x9 / `OP_Stamina`x2 (the Leechcurse drain).
  **No `OP_BeginCast`** (record 325 has no Target stage).
- Observer (char 103) in the window: **no landing** (only keepalive/`OP_MoveDoor`).
  Matches B1134: the observer draws NO landing on /disc (caster-side record).

**Mechanism proven:** the official client resolves `/disc <name>` against its OWN
(zone-in) discipline list, not the live `OP_DisciplineUpdate` buffer. Attempt 1
delivered the disciplines mid-session (only 4504 + 5 others, never 8604), so
"Leechcurse Discipline" (which the client resolves to 8604 for a SHD) was not in the
resolution list -> silent. With 8604 in `character_disciplines` BEFORE zone-in it is
in the zone-in list -> /disc fires. This is the fix the ruling predicted.

## Leg B result — Intimidation -> Fear 229: ZERO C->S (HARD RULE)

Leg B ran clean (rc=0). Zone-in delivered **skill 71 (Intimidation)=5000 and spell
113** (88-book, has 113). With TrollF000 targeted (`C->S OP_TargetCommand` 262,
server-confirmed) and `#stun 120000` active, the G-press (step 22) sent **only
keepalives** (`OP_ClientUpdate`x3 + `OP_FloatListThing`x3): **zero `OP_InstillDoubt`,
zero `OP_CastSpell`**; no S->C `OP_Action` 229/209; `/doability 1..2` (negative
control) also zero.

The decompile (the ONE `0x389e` sender, eqgame.exe.c line 28460) gates the G key on
ONLY: (1) valid non-self target, and (3) caster position state `+0x251=='d'`
(standing). **NOT** skill 71, **NOT** spell 113, **NOT** in-combat, **NOT** a
Combat-Abilities slot — correcting the attempt-1 "Gate-4 known-113 / in-combat"
reading. Both apparent gates were satisfied in the leg, yet nothing was sent: the
remaining cause is at the client's key-delivery / handler-entry level (the G key not
reaching this branch, or the caster byte not 'd' at the instant), which the pcap
cannot show and which no skill/spell/combat pre-SQL can change. Per the HARD RULE
this is a block (no GM `#castspell` substitution, no FOLLOW-UP).

## Next steps (for the orchestrator) — Leg A is done; Leg B is a block

1. **`/disc` (record 325/326): DONE.** It lands on the real client path when the
   discipline is in `character_disciplines` before zone-in (Leg A pcap lines above).
   The oracle draws the caster-side drain (mana/HP/stamina + SpecialMesg), NO landing
   on the observer (record 325 has no Target stage), consistent with B1134.
2. **Intimidation (record 13 / 229): BLOCKED.** The trigger is the G key (VK 0x47),
   whose only gates are (valid non-self target) + (caster `+0x251=='d'`). A level-60
   Warrior with Intimidation 5000 + spell 113 + a confirmed in-range stunned target
   still sent zero `OP_InstillDoubt`. The next leg must get the G key to actually
   reach the 0x47 handler in a 'd' state (input focus / state at the instant) — a
   client-input question, not a character-state one.
3. Harness rows + two ledger clips (ask #3/#4): the /disc half can proceed (Leg A
   landed); the Intimidation half is moot until the G-press lands.

## Lane state (before == after)

- `lane_before.txt` (pre-leg): char 67 = class 12 (Wizard) level 1 race 6;
  char 103 = class 1 (Warrior) level 1 race 6; stack up.
- Both legs' post-SQL (world down) restored char 67 to class 12, level 1, race 6,
  gems `0/93 1/38 2/54 3/41 4/253 5/458 6/656`, and the original
  `character_skills`/`character_disciplines`/`timers`. Char 103 (observer) was not
  modified by either leg. Stack left down by the legs' `down.sh` step (expected).

## Evidence

- Attempt 1: `plans/b1162_disc325.tsv`, `plans/b1162_combat13.tsv` (both pass
  `oracle_plan_check.py`).
- Attempt 2 (this block): `plans/b1162_disc325_v2.tsv`, `plans/b1162_combat13_v2.tsv`
  + `sql/b1162_{disc325,combat13}_v2.sql` + `.post.sql` (pre-SQL fixes state before
  zone-in; post-SQL restores level). Both plans pass `oracle_plan_check.py --lane 5`.
- `~/kb-evidence/kb-b1162-combat-fx/`:
  - Attempt 1: `disc325_census.txt`, `combat13_census.txt`, `oracle/b1162_disc325/`,
    `oracle/b1162_combat13/`, `disc325_finding.md`, `lane_before.txt`.
  - Attempt 2: **`attempt2_finding.md`** (both legs' result + mechanism + decompile
    gate), `oracle/b1162_disc325_v2/` (Leg A: stills + per-step opcodes + pcap),
    `oracle/b1162_combat13_v2/` (Leg B: same), probes `probe_b1162_{legA,legA_landing,
    legA_observer,legB,legB_gwindow,legB_target}.py`, `probe_b1162_{book,book2,
    discupdate,spellfields}.py`.
- Class-ownership probes: `probe_b1162_fields.py`, `probe_b1162_classcols.py`,
  `probe_b1162_encode.py` (in `~/kb-evidence/kb-b1162-combat-fx/`).
