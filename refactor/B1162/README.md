# B1162 (#481): combat-ability + discipline FX on the real client path

**Outcome (continuation, 2026-10-27): Intimidation LANDED on the real client
path, and a real-client /disc LANDED on lane 5 (4499 Defensive Discipline,
record 326); record 325 (Leechcurse) cannot land on this lane, because no
spell id gives the Shadow Knight a level on both the client and the lane
server.** Read "Continuation step 1" (why attempts 1-2 could not send
OP_InstillDoubt), "Continuation step 2" (the landing), "Correction: Leg A did
not land", "Rows, clips, lane", "The /disc landing leg" and "Fix leg (review
note 7739)" at the end. The sections between here and there are the two
earlier attempts. Their leg facts stand; two of their readings are corrected
in place: Leg A did not land (the server refused 8604), and the G-key reading
of attempt 2 is superseded by step 1.

**How `/disc` picks its spell (decompile, replaces the earlier "zone-in
list" reading).** `/disc <name>` walks the client's LIVE discipline list,
the same `+0x3aec` buffer that the `OP_DisciplineUpdate` handler fills
whenever the packet arrives, at zone-in or mid-session. It stops at the FIRST
id whose spell name prefix-matches the typed text and sends only if the
client's own spell row gives the caster's class a level at or below the
caster's level. Attempt 1's list began with 4504 (client row all 255:
silent). Attempt 2's list began with 8604 (client SHD 60: sent). The timing
of the delivery played no part. Details: "Leg A result" and "The /disc
landing leg".

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
f(103+N)); f121 is not a class id (B1143's "field 121 = class 13" was a misread).
(Correction, "The /disc landing leg": f121 is not a `/disc` flag either. It
is the `TargetAnim` column of the lane's `spells_new`, and 13 is an animation
id shared by ordinary spells such as 93 Burst of Flame.) The record-325 disciplines
(8604/8617/8642 Leechcurse Discipline variants) carry **f108=60 = class 5
(Shadow Knight) level 60** — the owning class is **5, not 4**. So B1143's leg ran
on a class that does NOT own the disciplines, which is a genuine cause of a silent
`/disc`.

**B1162 re-ran the leg on the correct owning class (5) and it was STILL silent**
(0 C->S `OP_CastSpell`), with the character at level 60 and `#traindisc 60`
trained. So **the class-ownership fix was necessary but not sufficient** — there is
a second gate. The press-G... (disc leg) step-25 still shows **"You have learned 0
new disciplines!"** in chat: the `#traindisc 60` feedback reports zero disciplines
learned, even though `character_disciplines` had 6 rows for char 67.
(Resolved by the fix leg: the second gate is the client's own spell row. The
attempt-1 list began with 4504, whose client row gives no class a level, so
`/disc Leechcurse Discipline` matched 4504 first and the client sent nothing.
See "Leg A result".)

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

**Mechanism (disc), as the fix leg read it from the decompile:** the official
client's `OP_DisciplineUpdate` handler (decompile `eqgame.exe.c` 81589-81595)
**memcpys the payload into its discipline buffer regardless of timing**, so the
client DID hold 4504 at the time of `/disc Leechcurse`. Two of the three `/disc`s
(Reaper Strike 6236, Lich Sting 6268) name disciplines the client **never
received**, so no list entry prefix-matches and nothing is sent. `/disc
Leechcurse` was silent for a different reason. The pcap's list is [4504, 4520,
4585, 4587, 4590, 7005] with 4504 at index 0, so 4504 is the first prefix match.
The client's own `spells_us.txt` row for 4504 gives no class a level (all 255),
so the send gate refuses it (see "Leg A result"). The ruling's attempt-2 fix
(state before zone-in) worked only because its list put 8604 first, not
because of the timing.

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
  delivered — a hedge on the client's name-resolution target). The SQL puts
  8604 in slot 0 and 4504 in slot 1; that order is what decided the match
  (see "Leg A result"). Plan: login, zone,
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

## Leg A result — `/disc` on the owning class: the client SENT, the server refused

Leg A ran clean (rc=0, 16 steps, observer rig, chars 67+103 restored). The pre-SQL
disciplines were delivered to the client at zone-in (`OP_DisciplineUpdate` ids=
**[8604, 4504]**, slot 0 = 8604, slot 1 = 4504, S->C, t=1793062354.009/.015),
and the `/disc` step then sent:

- **C->S `OP_CastSpell`(12363) t=1793062472.595**
  payload `0a000000 9c210000 ffffffff 00000000 d224603f` = slot 10, spell **8604**
  (Leechcurse Discipline, SHD-lvl-60 variant), self-target — B1134 P4's working form.
- S->C: `OP_SpecialMesg`x2 "Your class cannot learn from this tome." +
  `OP_InterruptCast`x2, i.e. the server REFUSED 8604 (see "Correction: Leg A did
  not land"). The `OP_ManaChange` / `OP_HPUpdate` / `OP_Stamina` rows in the
  window are regular tick traffic, not a drain. **No `OP_BeginCast`.**
- Observer (char 103) in the window: **no landing** (only keepalive/`OP_MoveDoor`).

**The mechanism (decompile `review/B1141/ghidra/out/eqgame.exe.c`; it replaces
the "zone-in-frozen list" reading this section first gave).** `/disc` is
`FUN_0046fc1b` (line 91578). It walks `FUN_005f0a00(i)` for i = 0..99, which
returns `*(base + 0x3aec + i*4)` (line 386913). The `OP_DisciplineUpdate`
handler (opcode 0x7180, lines 81589-81595) memcpys the packet into
`FUN_005f0a30()` = `base + 0x3aec` (line 386925). It is one live buffer, so
the client resolves against whatever the latest `OP_DisciplineUpdate` held,
zone-in or mid-session. The walk stops at the FIRST entry whose name
prefix-matches the typed text (line 91652 `__strnicmp`) and calls
`FUN_004bb39d` on it (line 91654), then returns whatever that call does.
`FUN_004bb39d` sends C->S `OP_CastSpell` slot 10 only if
`FUN_004bb2dd(id) == 1`, and `FUN_004bb2dd` returns -1 when the client's own
spell row has 0xff for the caster's class (line 142850). The full gate is
quoted in "The /disc landing leg".

Applied to the two pcaps (both 400-byte payloads, decoded slot by slot):

| leg | `OP_DisciplineUpdate` slots (S->C) | first prefix match for "Leechcurse Discipline" | client `spells_us.txt` row, SHD (field 108) | result |
|---|---|---|---|---|
| attempt 1 `b1162_disc325` | 0:4504 1:4520 2:4585 3:4587 4:4590 5:7005 (t 1793051940.842, mid-session) | 4504 (slot 0) | 255 (every class 255) | `FUN_004bb2dd` = -1, nothing sent |
| attempt 2 `b1162_disc325_v2` | 0:8604 1:4504 (t 1793062354.009, zone-in) | 8604 (slot 0) | 60 | C->S `OP_CastSpell` slot 10 spell 8604 |

The delivery time (zone-in or mid-session) played no part. What changed between
the legs was which Leechcurse id came first in the list.

## Leg B result — Intimidation -> Fear 229: ZERO C->S (HARD RULE)

(Superseded by "Continuation step 1": 0x47 is skill id 71, not the G key, and
a Warrior has no Intimidation cap. The G-key reading below is attempt 2's.)

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

## Next steps (for the orchestrator, attempt 2; superseded by the continuation sections below)

1. **`/disc` (record 325/326):** superseded. Leg A's client SENT 8604 because
   8604 was the first prefix match in its list, but the server refused it
   ("Correction: Leg A did not land"). The real-client `/disc` landing is
   4499 in "The /disc landing leg".
2. **Intimidation (record 13 / 229): BLOCKED** (superseded: it landed in
   "Continuation step 2"). The trigger is the G key (VK 0x47),
   whose only gates are (valid non-self target) + (caster `+0x251=='d'`). A level-60
   Warrior with Intimidation 5000 + spell 113 + a confirmed in-range stunned target
   still sent zero `OP_InstillDoubt`. The next leg must get the G key to actually
   reach the 0x47 handler in a 'd' state (input focus / state at the instant) — a
   client-input question, not a character-state one.
3. Harness rows + two ledger clips (ask #3/#4): the /disc half can proceed (Leg A
   landed); the Intimidation half is moot until the G-press lands.

## Lane state (before == after)

- `lane_before.txt` (pre-leg): char 67 = class 12 (Wizard) level 1 race 6;
  char 103 = class 1 (Warrior) level 1 race 1; stack up.
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

## Continuation step 1 (ruling 01:50Z, offline): what `uVar5 == 0x47` is

All lines below are `review/B1141/ghidra/out/eqgame.exe.c` (Ghidra decompile of
the eqgame.exe with MD5 a9de1b8cc5c451b32084656fcacf1103 -- the SAME MD5 as the
lane-5 oracle client `~/eq-oracle/client/eqgame.exe`, read-only check saved in
`review/B1162/worker/oracle_readonly_ini_20261027.txt`) unless a file is named.

**0x47 is neither a key code nor a keymap index: it is the SKILL ID 71
(Intimidation).** The OP_InstillDoubt sender is inside `FUN_0041f4a0` (decl. line
27566), the client's "use ability" function. Its byte argument is the skill id:
line 27638 `uVar5 = (uint)param_2 & 0xff;`, and every branch of the function is a
skill (0x43 = 67 Begging sends 0x13e7 at lines 28414-28431; 0x47 = 71
Intimidation; 0x49 = 73 Taunt from line 28473). The 0x47 branch (lines 28448-28461)
sends opcode 0x389e with a NULL payload of size 0 through line 27887
`FUN_004912b1(&DAT_00905ca0,uVar14,puVar16,uVar18,1)` -- matching the server's
`Handle_OP_InstillDoubt` "packet is empty" (akk-stack `zone/client_packet.cpp`
9095-9106).

**Where the skill id comes from.** `FUN_0041f4a0` has exactly four call sites in
the disassembly (`grep "call 0x41f4a0" eqgame.dis`): 0x46a813 (`/doability`),
0x4d3b9d (Actions-window Abilities-page button, decompile line 164019), 0x4d3f73
(Actions-window Combat-page button, line 164200) and 0x514241 (a hotbutton of
type 2/3 that points at one of those slots, lines 214402-214410). Every one passes
a value read from the ability-slot tables `DAT_0095d424` (6 Ability slots) /
`DAT_0095d414` (4 CombatSkill slots), which the client loads from the CHARACTER
INI `[Abilities] Ability1..6` / `[CombatSkills] CombatSkill1..4` (lines
133398-133402 and the CombatSkill loop after it; the INI is `.\<name>_<server>.ini`,
built at line 133465 `_sprintf(_Dest_00,".\\%s_",local_208)`). The `/doability`
handler (disassembly 0x46a7d1-0x46a86f; command table entry `/doability` ->
handler 0x46a7d1 in .data at 0x78b9bc) maps N=1..6 to `DAT_0095d424[N-1]`, N=7..10
to `DAT_0095d414[N-7]`, and on a slot holding -1 returns silently (0x46a7f8 `cmp
dword ptr [eax], -1` / 0x46a7fb `je 0x46a86f`). The Abilities-page button on a -1
slot opens the skill picker instead (line 164011-164016) and sends nothing.
**No key binding reaches it**: the keymap commands in the binary's strings have
no Intimidation/ability entry, the lane-5 oracle `eqclient.ini` `[KeyMaps]` holds
only five DirectInput scan codes (FORWARD_2=17, BACK_2=31, RIGHT_2=32, LEFT_2=30,
DUCK_1=208), and a key reaches `FUN_0041f4a0` only through a hotbutton holding an
ability slot. The caster's INI (attempt 2 `before-character.ini`, and the same
read-only today) has `Ability1..6=-1` and `CombatSkill1..4=-1`.
=> **attempt 2's G press could never send OP_InstillDoubt, and its `/doability
1..2` was silent because both slots were -1.**

**The class gate (a second, independent reason).** Line 27627 returns before any
packet when `FUN_0041c8f4(skill) == 0`, i.e. `FUN_0041b0de` (line 24787-24826):
it starts from the racial value `FUN_004184f4` (for race 6 Dark Elf only skill
0x1d) and only consults the character's skill value (`FUN_0040e722`, line 14792
`*(iVar2 + 0xf70 + skill*4)`) when the class cap `FUN_0044a335(class)` is
non-zero (line 24790). That cap comes from the client's own static
[class][skill][level] table: the skill-object constructor `FUN_00448d94`
(line 64437; built for skills 0..99 at line 126421-126431) copies it at lines
65366-65385 from .data 0x698c30 + skill*0x118 + class*28000.
`refactor/B1162/scripts/client_skill_table.py` decodes it from the binary
(`review/B1162/worker/client_skill_table_71_73.tsv`): **skill 71 Intimidation has
a non-zero cap only for class 7 MNK (from level 18), 8 BRD (26), 9 ROG (22), 16
BER (1); class 1 WAR has cap 0 at every level.** (Sanity row in the same file:
skill 73 Taunt = WAR/PAL/SHD/RNG from level 1, the known in-era owners.) So for
the Warrior of attempt 2 the function returned at line 27627 even with skill
71 = 5000 in the profile -- the ruling's "class 1 Warrior" cannot fire it.
The load-time slot check (line 133521-133527) also clears an Ability slot whose
skill fails `FUN_0041b0de`.

**`+0x251 == 'd'` is the caster's stand state, 'd' = 100 = standing.** The byte is
written by the stand-state setter (lines 104774-104799: 'd' -> 0x6f duck, 'o' ->
100 / 0x6e) and set to 0x66 (102) by the stun-message handler (line 80377); the
server's `Animation` constants are Standing 100, Freeze 102, Looting 105, Sitting
110, Crouching 111, Lying 115 (akk-stack `common/eq_constants.h` 68-75). The
caster also must not be frozen ('f', 0x3050 message at the top of the function)
and needs a non-self target (lines 28449-28453).

**What the server needs** (akk-stack `zone/special_attacks.cpp` 2390-2445,
`Mob::InstillDoubt`): the target is an NPC, no DivineAura, `CombatRange(who)`,
the 9 s reuse timer (`common/features.h` 126 `InstillDoubtReuseTime = 9`), then
`zone->random.Roll(GetSkill(71) + CHA/4 - (target level*4 + target WIS/4))`;
success = `SpellOnTarget(229, who, 0, true, -2000)`, failure = the NOT_SCARING
message. No skill-ownership check server-side.

**The trigger for the ONE live leg:** class 9 Rogue (owns 71), level 60 and skill
71 = 5000 before zone-in (`sql/b1162_combat13_v3.sql`), `Abilities.Ability1=71`
in the caster's INI for this leg only (new driver directive `# char-ini-set=`,
`scripts/eqemu/oracle/oracle_remote.sh`, same backup + byte-identical restore as
`# ini-unset=`, documented in `docs/oracle_driver.md`), TrollF000 targeted +
`#stun`, caster standing, `/doability 1` (the same `FUN_0041f4a0(DAT_0095d424[0])`
call the Abilities-page button makes, 0x46a813 vs 0x4d3b9d). Plan
`plans/b1162_combat13_v3.tsv`.

## Continuation step 2 (ONE lane-5 leg): Intimidation -> Fear 229 LANDED

Leg `b1162_combat13_v3` (2026-10-27 11:35-11:49Z, rc=0, class 9 Rogue, observer
rig, `refactor/B1162/scripts/run_leg_b1162.sh b1162_combat13_v3 9 --pre-sql-file
refactor/B1162/sql/b1162_combat13_v3.sql --post-sql-file
refactor/B1162/sql/b1162_combat13_v2.post.sql`; evidence
`review/B1162/worker/oracle/b1162_combat13_v3/`). Pcap lines
(`refactor/B1162/scripts/` decode of `capture.pcap`, caster port 35593,
observer port 38639; `review/B1162/worker/probe_b1162_msgs.py`):

```
1793101494.790 S->C OP_SpecialMesg   "You stunned TrollF (262) for 2 Minutes."   (step 20 #stun)
1793101502.249 C->S OP_InstillDoubt  size 0                                       (step 22 /doability 1)
1793101502.276 S->C OP_Action        060104013c000a00... (target 262, source 260, spell 0x00e5 = 229)
1793101502.276 S->C OP_Action        same bytes, observer port 38639
1793101502.452 C->S OP_Animation     04010c2b
1793101523.554 S->C OP_FormattedMessage  ... "Fear" "TrollF"  (the fade, Act+21.3 s)
1793101539.432 C->S OP_InstillDoubt  size 0                                       (step 27 /doability 1)
1793101539.444 S->C OP_Action        060104013c000a00... (229 on 262), also to 38639
```

Census over the whole pcap (`all-opcodes.tsv`): 2 C->S `OP_InstillDoubt`,
10 S->C `OP_Action`, 4 S->C `OP_BeginCast` -- all four are
`57000b00d0070000` = source 87 spell 11 cast 2000 ms at 1793101451.925-.941 (an
ambient qeynos2 NPC, 50 s before the first press). **Zero `OP_BeginCast` from
the intimidator (260): no Source**, as the client plays a record's Source only
from its begin-cast handler (B1144). The ability slot came from the
`# char-ini-set=` edit: `character-after.ini` still holds `Ability1=71` at stop
(the load-time slot check kept it for a Rogue), and the driver restored the
original INI byte-identically (`character-restored` marker after the `cmp`).

**The NPC landing in the observer view** (`refactor/B1162/scripts/obs_measure_b1162.py`,
the B1143 instrument with only the eqpcap path changed; geometry
`refactor/B1162/scripts/geom_b1162_combat13_v3.json` = B1143 p3_rig93 at the
same poses; files `review/B1162/worker/measure/combat13_v3/`), vs
`review/B1130/record_table.tsv` row 13 (line 24 of the file: e13, 209 Spook the
Dead, GM cast WITH a BeginCast, fixed_npc camera):

| | onset (Act+) | end (Act+) | peak | run cov | cen_h | h p5..p95 | ground band |
|---|---|---|---|---|---|---|---|
| oracle 229 rep 1 (observer, NPC 228 px) | +0.392 (f12985) | +6.259 (f13161) | f13045 Act+2.392 cov 1.024 | 0.676 | 0.72 | -0.34..1.91 | 0.20 |
| oracle 229 rep 2 | +0.391 (f14100) | +6.258 (f14276) | f14198 Act+3.658 cov 0.969 | 0.649 | 0.76 | -0.29..1.94 | 0.18 |
| B1130 row 13 (209, fixed_npc) | +0.39 | +6.39 | landing_cov 3.264 (whole-frame RISE, includes the held Source) | -- | 0.77 | 0.23..1.63 | -- |
| fable harness 229 action-only, b1130 preset, at the RENDERED target (feet 953,782 / 316 px, crop y 60..800; fix leg) | +0.333 (f55) | +6.267 (f233) | f85 Act+1.333 anchor cov 0.422 (RISE peak f89 Act+1.467, RISE cov 1.183) | 0.263 | 1.01 | 0.13..2.00 | 0.08 |
| (retracted) the same run at the preset anchor (feet 940,652 / 325 px, crop y 60..600) | +0.333 (f55) | +6.267 (f233) | f81 Act+1.200, RISE cov 0.927, anchor cov 0.339 | 0.177 | 0.87 | 0.25..1.60 | 0.00 |

The harness row (`review/B1162/worker/harness/fear229_b1130/`, plan
`1.5 229 other 0 0 60 action`, `--camera b1130 --seed 1`; measured with
`refactor/B1162/scripts/harness_measure_b1130_action.py`, the B1155 instrument
with BC = Act - 1 frame for an action-only cast) draws record 101 (field 145)
Target slots 0-3 only (`frames/slots.tsv`: spelkb x2, spelcb x2, no Source
slot) and SOURCE none (max 57 px = 0.0013 model^2). Onset and end match the
oracle to 2 frames / 0.01 s. The anatomy columns (run cov, cen_h, h, ground)
are re-measured in "Fix leg (review note 7739)": the first measurement used
the oracle's target box (940,652 / 325 px) while the harness drew its target
at rows 467..782, so the feet and the ground band were outside the crop. The
ground-band miss and its `createLanding` owner that this paragraph used to
name are RETRACTED. At the rendered anchor the harness run has ground 0.08 and
h_p5 0.13. No FX owner is named for the anatomy: the b1130 preset's target fit
is the defect (B1166, #485), and the like-for-like comparison waits for it.
The coverage columns are NOT comparable across the two rigs (observer camera
with the NPC inside the caster vs the b1130 preset's separate target), so no
coverage verdict is drawn.

## Correction: Leg A (/disc, attempt 2) did NOT land

Re-decoding `review/B1162/worker/oracle/b1162_disc325_v2/capture.pcap` around
the `/disc` (same decoder):

```
1793062472.595 C->S OP_CastSpell     0a0000009c210000ffffffff... (slot 10, spell 0x219c = 8604)
1793062472.625 S->C OP_SpecialMesg   "Your class cannot learn from this tome."
1793062472.625 S->C OP_InterruptCast 04010000b7010000
1793062472.625 S->C OP_InterruptCast 04010000be3000005465737463617374657200 ("Testcaster", observer port 48968)
1793062472.641 S->C OP_SpecialMesg   "Your class cannot learn from this tome."   (repeat)
```

There is **no S->C `OP_Action`** for 8604; the ManaChange/HPUpdate/Stamina rows
the attempt-2 section called "the Leechcurse drain" are the regular tick
traffic. The server's `UseDiscipline` (akk-stack `zone/effects.cpp` 885-890)
prints that line when `spells[spell_id].classes[GetClass() - 1] == 255`, i.e.
the lane's server-side spell row for 8604 does not give class 5 a level, while
the client's own `spells_us.txt` does (field 108 = 60). What stands from Leg A:
**the real client `/disc` sends C->S OP_CastSpell slot 10 when the first
prefix-matching id in its live discipline list has a client class level at or
below the caster's level** (8604 here; "Leg A result"). The server then refused
it, so the oracle had nothing to draw. Record 325's on-screen behaviour on a real `/disc` landing is
therefore still unobserved; the fable side is pinned regardless:
`tests/test_fx_harness.cpp` `TheLeechcurseDisciplineWithoutBeginCastDrawsNothing`
(8604 action-only: peak 4 px vs pre-cast noise 3 px; the 4499 record-326 test
alongside: 0 vs 0 -- `review/B1162/worker/fx_gate_disc.txt`), and the harness
row `review/B1162/worker/harness/disc8604_b1130self/` (empty `slots.tsv`,
SOURCE none max 48 px, LANDING none max 112 px). The ruling of 2026-10-27
allowed one more lane leg; see "The /disc landing leg" below.

## Rows, clips, lane

- Ledger clips (`refactor/B1158/scripts/make_clip.py`, oracle LEFT / willeq
  RIGHT, BC = Act for both action-only casts):
  `review/B1162/worker/ledger/clips/b1162_fear229_intimidation.mp4` (+ `_strip.png`;
  oracle f12973 @ 30 fps, t0 1793101069.835; willeq f45) and
  `review/B1162/worker/ledger/clips/b1162_disc8604_leechcurse.mp4` (+ `_strip.png`;
  oracle f14732, t0 1793061981.526, cut at the C->S OP_CastSpell epoch; willeq f45).
  Specs in `review/B1162/worker/ledger/specs/`, alignment lines in `clip.log`.
- Lane before == after (the leg's own census): char 67 `char-state-before.tsv`
  = `char-state-after.tsv` = class 12, level 1, race 6; char 103
  before = after = class 1, level 1, race 1; driver.log SAVED/LEG/BACK:
  character_skills 0/1/0, character_disciplines 0/0/0, timers 2/2/2; gems back
  to 93,38,54,41,253,458,656 ("char 67 restored" in the run output); the
  caster INI restored byte-identically. Lease FREE at the end
  (`cleanup exit=0 lane=5 lock=state=FREE`). An "Oraclewatch's corpse" already
  lay in the observer's view at the settle still (`16_wait.frame.png`); it was
  not created or touched by this leg.

## The /disc landing leg (orchestrator ruling 2026-10-27: observe one real /disc landing)

The ruling did not accept acceptance C on the harness pin alone. It asked for
one lane-5 `/disc` with a discipline that the lane server grants the caster's
class. The steps are below. Every line number refers to
`review/B1141/ghidra/out/eqgame.exe.c` (the lane-5 client binary, same MD5)
unless another file is named.

### Which discipline can land (offline + the read-only lane query)

**The server gate.** `Client::UseDiscipline` (akk-stack `zone/effects.cpp`
852, the same file on the lane: md5 bfbd2057f758579ece7350b5fe2c7882 for both
`/opt/akk-stack/code/zone/effects.cpp` and the NFS copy) needs three things.
The id must be in `m_pp.disciplines`. Then line 887
`uint8 level_to_use = spell.classes[GetClass() - 1];` must not be 255, or
line 889 prints `"Your class cannot learn from this tome."`. Finally the level
must be at most the caster's level.

**The client gate.** The `/discipline` command (command table .data 0x78b5b0:
`/discipline` -> handler 0x46fc1b, the entry before `/random`; `/disc` is its
abbreviation) is `FUN_0046fc1b` (line 91578):
- line 91604 is its class gate. Classes 1, 7, 9, 3, 4, 5, 8, 15 and 16 pass;
  any other class gets string 0x16d0 (line 91664).
- It walks the 100-entry discipline list `FUN_005f0a00(i)` (line 386913,
  `+0x3aec + i*4`, the buffer `OP_DisciplineUpdate` fills). It takes the
  FIRST entry whose spell name prefix-matches the typed text (line 91652
  `__strnicmp((char *)puVar3[0x82],param_2,_MaxCount)`) and calls
  `FUN_004bb39d` (line 91654).
- `FUN_004bb39d` (line 142875) calls `FUN_004baca6` (line 142348: the id is
  in the list) at line 142913. Line 142914 then tests `(in list &&
  FUN_004bb2dd(id) == 1) || bVar1`. `bVar1` is a one-shot bypass set at lines
  142906-142911: when `*(*(this + 0xc288) + 0x1a8) + 0x107c` is 1, the
  function clears it to 0 and sets `bVar1`, which skips both checks once. Past
  that test (and the spell-record lookup), the reuse-timer message and the
  `+0x180 == 6` check, it sends `FUN_00411f3d(10,param_2,...)` (line 142953)
  = C->S `OP_CastSpell` slot 10. Attempt 1 sent nothing for 4504, so the
  bypass did not fire there.
- `FUN_004bb2dd` (line 142831) returns -1 when the CLIENT's own spell row
  holds 0xff for the caster's class (line 142850; `FUN_0044e755` line 69481
  reads `spell + 0x150 + class`, with `ecx` = the spell record per
  `eqgame.dis` 0x4bb320 `mov ecx, edi`). It returns -2 below that level
  (line 142859) and 1 otherwise (line 142864).

So, outside the one-shot `bVar1` bypass, a `/disc` lands only when the
client's `spells_us.txt` row AND the server's `spells_new` row both give the
caster's class a level, and on the client side only the FIRST prefix-matching
id in the live list is tried.

**The lane query** (lease `kb-b1162-combat-fx` taken 12:27Z; the stack was
fully down, so only the `mariadb` service was started with the lane scripts'
`compose up -d mariadb`; the leg's own up/down followed). Output:
`review/B1162/worker/lane5_spells_new_discs_20261027.txt`:

| id | client `spells_us.txt` (name, class level) | lane `spells_new` (name, class level) | passes both |
|---|---|---|---|
| 4504 | Leechcurse Discipline, **none** (all 255) | Leechcurse Discipline, SHD 60 | no (client refuses) |
| 8604 | Leechcurse Discipline, SHD 60 | **Bear Claw**, none | no (server refuses: attempt 2) |
| 8617 | Leechcurse Discipline, SHD 60 | **Grappling Roots**, none | no |
| 8642 | Leechcurse Discipline, SHD 60 | **Tangleweb**, none | no |
| 4520 / 8616 / 8618 | Unholy Aura (SHD 55 on 8616/8618 only) | 4520 SHD 55; 8616 Wind Raze, 8618 Deafening Gibberish | no |
| **4499** | Defensive Discipline, WAR 55 | Defensive Discipline, WAR 55 | **yes** |
| 4502, 4503, 4509, 4510, 4515, 4671, 4673 | MNK 54, WAR 52, MNK 53, MNK 51, ROG 55, BST 55, ROG 53 | the same | yes |

The client's ids 8600+ and the lane's `spells_new` ids name different spells.
**No record-325 id (4504, 8604, 8617, 8642, and Reaper Strike/Lich Sting
6236/6268, which have no client class level) passes both gates on this lane.**
So record 325's real `/disc` landing cannot be observed here, and the fable
side stays pinned only by the record-325 harness test. Every passing id uses
record 326. I chose **4499 (WAR 55)**: it is the spell of B1130's record-51 row
(`review/B1130/record_table.tsv` line 5, the row that holds spellsnew.eff
record 326; B1130 has no row for record 325, "not cast here") and of B1134's
P4 `/disc` measurement.

Field 121 (the attempt-2 "/disc-eligible flag") is the lane's `TargetAnim`
column (`review/B1162/worker/lane5_spells_new_columns_20261027.txt`: 120
CastingAnim, 121 TargetAnim, 145 spellanim). It is not a `/disc` gate, and 13
is shared by ordinary spells such as 93 Burst of Flame.

### The leg

The leg is `b1162_disc4499_v3`, run 2026-10-27 13:10-13:21Z, rc=0. It waited
on B1164's lease from 12:31Z and never broke it. Command:
`refactor/B1162/scripts/run_leg_b1162.sh b1162_disc4499_v3 1 --pre-sql-file
refactor/B1162/sql/b1162_disc4499_v3.sql --post-sql-file
refactor/B1162/sql/b1162_disc325_v2.post.sql`. The plan is
`refactor/B1162/plans/b1162_disc4499_v3.tsv`, with the same poses and observer
F9x3 camera as combat13_v3 and no NPC. Before zone-in, the caster was class 1,
level 60, with `character_disciplines` (67, 0, 4499). Evidence is in
`review/B1162/worker/oracle/b1162_disc4499_v3/`.

Pcap lines (caster port 39676, observer port 37940;
`review/B1162/worker/probe_b1162_msgs_local.py`):

```
1793106957.096 S->C 39676 OP_DisciplineUpdate 400  93110000 00000000 ...  (delivered at zone-in: slot 0 = 0x1193 = 4499)
1793107093.629 C->S 39676 OP_CastSpell 20  0a000000 93110000 ffffffff 00000000 457ed4e0  (slot 10, spell 4499, self)
1793107093.641 S->C 37940 OP_Action 31  030103013c000a00...e700000000 9311 3c00  (target 259, source 259, spell 4499) -> observer
1793107093.641 S->C 39676 OP_Action 31  same (+ the ...3c04 second row) -> caster
1793107093.641 S->C 39676 OP_DisciplineTimer 12  01000000 76020000 00000000  (timer 1, 0x276 = 630 s = recast_time 630000)
```

The leg's whole-pcap census (`all-opcodes.tsv`) holds 1 C->S `OP_CastSpell`
and 5 S->C `OP_Action`, all spell 4499. There is no `OP_InterruptCast` and no
refusal line. There are 4 S->C `OP_BeginCast`, all `57000b00d0070000` at
1793107001.886/.903 (source 87, spell 11, 2000 ms: the ambient qeynos2 NPC
also seen in combat13_v3, 92 s before the `/disc`). **There is no
`OP_BeginCast` from the caster, so no Source**, as B1134 found. The caster's
own view (`review/B1162/worker/strips/disc4499_v3_caster.png`, from
`video-caster.mp4`, uncalibrated t0) shows "Defensive Discipline" in the
Combat Abilities window and the chat line "You assume a defensive fighting
style." from about Act+0.9.

**The landing row in the observer view.** The instrument is
`refactor/B1162/scripts/obs_measure_b1162.py ... 4499 --npc-id 259`, which
anchors on the caster, because target = source = 259. The geometry is
`refactor/B1162/scripts/geom_b1162_disc4499_v3.json`, B1134's `rig.json`
unchanged and checked on `15_wait.frame.png`. The output is in
`review/B1162/worker/measure/disc4499_v3/`; the strip is
`review/B1162/worker/strips/disc4499_v3_observer.png` (f14078 / f14092 /
f14098 / f14106 / f14130).

| | onset (Act+) | end (Act+) | peak | run cov | cen_h | h p5..p95 | ground band |
|---|---|---|---|---|---|---|---|
| oracle 4499 /disc, this leg (observer, caster 212 px) | +0.454 (f14092) | +1.120 (f14112) | f14098 Act+0.654 cov 0.067 | 0.054 | 0.70 | 0.28..1.09 | 0.02 |
| B1134 p4 4499 /disc (`review/B1134/measure/p4_discs_4499_1.txt`) | +0.460 (f13245) | +1.126 (f13265) | f13256 Act+0.826 cov 0.067 | 0.054 | 0.69 | 0.28..1.09 | 0.02 |
| B1130 record-51 row (spellsnew.eff 326; its numbers are the GM `#castspell 4499 0` path WITH a BeginCast) | Source +0.70 | lit to Act+8.0-8.3 | Source cov 0.231 | -- | 0.21 | -0.05..0.74 | feet ring |
| fable harness 4499 action-only, b1130-self (`review/B1162/worker/harness/disc4499_b1130self/`; same at the rendered caster box with the crop to y 800, fix leg) | none | none | SOURCE max 25 px, LANDING max 72 px (thr 288) | -- | -- | -- | -- |

The oracle's lit run is the caster's arm/turn animation, not a sprite. The
strip shows the arms raised and the body turning at f14092-f14106 and no
glow. Onset, end, run cov, cen_h and extent repeat B1134's `/disc` row to
1 frame / 0.01 (onset +0.454 vs +0.460, end +1.120 vs +1.126, run cov 0.054,
h 0.28..1.09 both). The peak does not: f14098 Act+0.654 against B1134's
f13256 Act+0.826, 5 frames (0.17 s) apart, at the same peak cov 0.067.
The GM path's feet-ring Source (the B1130 row) does NOT appear on the real
`/disc`, because there is no `OP_BeginCast`. The harness draws no spellsnew.eff
slot (`frames/slots.tsv` empty; run.log `createLanding: caster=1 target=1
spell=4499: no Target slot drawn -- no landing`), which **matches the oracle's
sprite content** (none). The harness does not animate the caster, so the
0.054 animation coverage has no counterpart. That is a model-animation
question, not an FX one, and no FX owner is named. The pin is
`FxHarness.ADisciplineWithoutBeginCastDrawsNothing` (tests/test_fx_harness.cpp,
4499; its comment now cites this leg). The record-325 test's comment was
corrected: the server refused 8604, so it pins the fable side only.

**Ledger clip:** `review/B1162/worker/ledger/clips/b1162_disc4499_defensive.mp4`
(+ `_strip.png`; spec `ledger/specs/b1162_disc4499_defensive.json`; oracle
f14078 at 30 fps, t0 1793106624.361 = the leg's `video.calibration.tsv`
calibrated_t0, BC = Act; willeq f45; line in `ledger/clip.log`).

**Lane before == after (this leg):** char 67 `char-state-before.tsv` =
`char-state-after.tsv` = class 12, level 1, race 6. Char 103 before = after =
class 1, level 1, race 1, with an unchanged book. driver.log SAVED/LEG/BACK:
character_skills 0/0/0, character_disciplines 0/1/0, timers 2/3/2 (the 4499
reuse timer was added and removed). Gems are back to 93,38,54,41,253,458,656
("char 67 restored"). Spellbook and buffs before = after, "snapshot restored
BYTE-IDENTICALLY" x2. The caster INI `before-character.ini` is byte-identical
to the file at stop (`cmp`). The lease was FREE at the end (`cleanup exit=0
lane=5 lock=state=FREE`).

**Gates for this step (ThinkPad, tbuild):** `ctest -R Discipline` = both
FxHarness discipline pins passed (`review/B1162/worker/ctest_disc_20261027.log`);
hygiene = 100% of 48 passed (`review/B1162/worker/hygiene_20261027.log`);
`scripts/refactor/cite_check.py` over this README = `checked 14 cites, 0 problems`.

## Fix leg (review note 7739)

The cold review of 0483f5e5 (MR !316 note 7739) found the Fear-229 harness
anatomy measured at the wrong anchor and the `/disc` mechanism text stale.
Each finding was checked against the files before the fix.

**1. The harness rows at the rendered anchor.**
`refactor/B1162/scripts/harness_measure_b1162_anchor.py` derives the box the
harness drew from the pre-cast frame `frame_00010.png` (every pixel that
differs from the clear colour, right of column 800 for the NPC target). It
then runs the unchanged B1132 instruments (`fx_measure.py`,
`fx_timing.py`, `landing_onset_abs.py`, `anchor.py`) at both anchors.
- Fear 229 (`harness/fear229_b1130/`): the target's lit rows are 467..782 and
  its columns 896..1023 (centroid 952.7), so the rendered anchor is feet
  953,782 / 316 px. The preset anchor is feet 940,652 / 325 px, the ORACLE's
  target box from `review/B1130/geom/r51.json`. The harness's own run.log says
  the fit missed it: `rendered target feet row 652 at column 940, 50 px head
  top to feet`.
- 4499 (`harness/disc4499_b1130self/`, self-cast): the caster's lit rows are
  390..597 (centroid 641.2), so the rendered anchor is 641,597 / 208 px,
  within 1 px of the preset's 640,598 / 209. Only the crop changes (y 60..600
  to 60..800).

Output: `review/B1162/worker/harness/{fear229_b1130,disc4499_b1130self}/measure_anchor/anchor_table.tsv`
(+ `measure_anchor.log`).

| run | anchor | crop (x,y,w,h) | onset (Act+) | end (Act+) | RISE peak / cov | run cov | cen_h | h p5..p95 | ground |
|---|---|---|---|---|---|---|---|---|---|
| 229 | preset 940,652 / 325 (old) | 150,60,990,540 | +0.333 (f55) | +6.267 (f233) | f81 Act+1.200 / 0.927 | 0.177 | 0.87 | 0.25..1.60 | 0.00 |
| 229 | rendered 953,782 / 316 (new) | 150,60,990,740 | +0.333 (f55) | +6.267 (f233) | f89 Act+1.467 / 1.183 | 0.263 | 1.01 | 0.13..2.00 | 0.08 |
| 229 | reviewer's 960,782 / 315 (cross-check, `measure_anchor_rv/`) | 150,60,990,740 | +0.333 (f55) | +6.267 (f233) | f89 Act+1.467 / 1.183 | 0.263 | 1.01 | 0.13..2.01 | 0.08 |
| 4499 | preset 640,598 / 209 (old) | 150,60,900,540 | none | none | none (SOURCE max 25 px, LANDING max 72 px) | -- | -- | -- | -- |
| 4499 | rendered 641,597 / 208 (new) | 150,60,900,740 | none | none | none (SOURCE max 25 px, LANDING max 72 px) | -- | -- | -- | -- |

Onset and end do not move. The 4499 row does not move. The 229 anatomy does
move: run cov 0.177 to 0.263, cen_h 0.87 to 1.01, h 0.25..1.60 to 0.13..2.00
and ground 0.00 to 0.08. These match the reviewer's re-measure.
**Verdict changes.** The ground-band miss and its `createLanding` owner are
dropped. At the rendered anchor the harness has ground 0.08 and h_p5 0.13,
against the oracle observer row's 0.18-0.20 and -0.29..-0.34. That compares
two different cameras (observer rig vs the b1130 preset), so it is no
like-for-like miss. Against B1130 row 13, the same preset camera, the harness
is now lower at the feet (h_p5 0.13 vs 0.23) and taller at the top (h_p95
2.00 vs 1.63, cen_h 1.01 vs 0.77). That comparison is not like-for-like
either: the harness target stands at rows 467..782, not at the oracle's
652 / 325 px, so the camera sees it from another place. **The defect is the
b1130 preset's target fit, owned by B1166 (#485), which solves the fit on the
rest pose (50 px).** This card does not fix the fit and names no FX owner for
the 229 anatomy. B1166 ask 4 re-measures these two rows with the fixed fit.
The ledger clip `b1162_fear229_intimidation` was regenerated with the verdict
`anatomy: not compared until B1166 fixes the b1130 target fit = n/a` in place
of `ground band 0.20 vs 0.00 = short` (`ledger/specs/`, `clip.log` and
`alignments.jsonl` updated; same alignment f12973 / f45).

**2-6.** The mechanism of `/disc` is rewritten in the banner, in "Mechanism
(disc)" and in "Leg A result". It now uses the live-buffer, first-match,
client-row reading instead of the zone-in list. The pcap slots were
re-decoded: attempt 1 0:4504 1:4520 2:4585 3:4587 4:4590 5:7005; attempt 2
0:8604 1:4504; 4499 leg 0:4499. The client rows were re-read from
`review/B1162/worker/spells_us.txt` (md5 c598b20a...):
- 4504, 4520, 4585 and 4587 are all 255.
- 8604 is SHD 60.
- 4590 is WAR 50.
- 7005 is SHD 56.
- 4499 is WAR 55.

Other corrections:
- The `FUN_004bb39d` gate now cites line 142914 and the `|| bVar1` bypass at
  142906-142911.
- `plans/b1162_disc325_v2.tsv` keeps its historical text, with the field-121
  claim annotated as refuted.
- The 4499 sentence names the columns that agree and the 5-frame peak
  difference.
- Char 103's race is 1.

`tests/test_fx_harness.cpp` was checked: its two discipline comments make no
zone-in or field-121 claim and stay as they are.

**Gates for the fix leg (ThinkPad, tbuild):** `test -R 'Discipline|FxHarness'`
= 7/7 passed (`review/B1162/worker/ctest_fix_20261027.log`); hygiene = 100% of
48 passed (`review/B1162/worker/hygiene_fix_20261027.log`);
`scripts/refactor/cite_check.py` over this README = `checked 14 cites,
0 problems, 0 address warnings`, and every `eqgame.exe.c` line cited in the
rewritten text was read by hand.

## The /disc record leg (card t_a74670e3, 2026-10-30): the records-325/326 harness rows at the current tip + the /disc ledger clip

The /disc visual half landed on the real client path in the previous leg;
the combat-ability half awaits the ruling on #481. This leg is the
remaining /disc RECORD only (report-only; "Relates to #481", no Closes; no
combat-ability change): the /disc harness rows for records 325/326 measured
at the CURRENT fable tip, and the /disc ledger clip rebuilt with this tip's
willeq side.

**Why a fresh render.** Four legs touched the render/FX path after the
previous legs' measurements (B1176 #495 player scaling, B1181 #500, B1185
#504, B1188 #507, all between B1174's measurement and this tip), so the
pre-B1174 numbers are untrusted. Concretely: the 8604 harness RISE coverage
is 0.104 at this tip against B1174's 0.065 (the player scale moved the lit
box), and the harness now ANIMATES the caster -- the line-638 statement
"The harness does not animate the caster" was true at that leg's tip and is
superseded: the FX harness plays the record's CastingAnim (record 325 -> 44
= t06, record 326 -> 27 = s01, eqgame's table, `refactor/B1148`), as B1174
measured for 8604 and this leg measures for both.

**The committed rows.** `refactor/B1162/rows/disc325_326_harness.tsv`, the
B1143 item-3 row shape (`refactor/B1143/rows/p3_disc325_325.tsv`), from
`~/kb-evidence/kb-b1162-disc-record/measure/<tag>/` (the committed
B1132-instrument fork `refactor/B1162/scripts/harness_measure_b1162_anchor.py`
at the preset and the rendered anchors; action-only self-cast, level 60,
b1130-self camera, the previous legs' plans
`refactor/B1162/plans/harness_{8604,4499}.plan`: `1.5 <spell> self 0 0 60
action`):

| record | spell | stage | anchor | onset (Act+) | end (Act+) | peak | rise cov | run cov | cen_h | h p5..p95 | ground |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 325 | 8604 | SOURCE | both | none | -- | -- | -- | -- | -- | -- | -- |
| 325 | 8604 | LANDING | rendered | +0.100 (f48) | +3.967 (f164) | f111 Act+2.200 | 0.104 | 0.064 | 0.61 | 0.15..0.91 | 0.06 |
| 325 | 8604 | LANDING | preset   | +0.100 (f48) | +3.967 (f164) | f111 Act+2.200 | 0.104 | 0.065 | 0.62 | 0.15..0.92 | 0.06 |
| 326 | 4499 | SOURCE | both | none | -- | -- | -- | -- | -- | -- | -- |
| 326 | 4499 | LANDING | rendered | +0.200 (f51) | +2.867 (f131) | f96 Act+1.700 | 0.049 | 0.039 | 0.72 | 0.13..1.03 | 0.07 |
| 326 | 4499 | LANDING | preset   | +0.200 (f51) | +2.867 (f131) | f96 Act+1.700 | 0.049 | 0.039 | 0.72 | 0.13..1.04 | 0.07 |

SOURCE is "none above thr (max 101px = 0.0023 model^2)" in both runs, like
the oracle: no `OP_BeginCast` on the real `/disc`, so no Source (this
README's /disc landing leg). The swing track (the runs' `fx_activity.tsv`):
8604 plays t06 over f45..f167 (123 frames, 4.1 s -- the t06 track length,
`refactor/B1148`) and 4499 plays s01 over f45..f134 (90 frames, 3.0 s), each
starting at the Action packet (f45, t_event 1.5). Both runs draw no
spellsnew.eff slot (`slots.tsv` empty), max 0 active effects and 0 missiles.
The lit run IS the caster's CastingAnim: no sprite, matching the oracle's
sprite content on both records.

**The oracle side (no re-capture).** The card re-captures on lane 5 only if
the existing capture is unfit; it is not -- the clip's verdicts are packet
facts plus the lit-run anatomy, both already measured for it, and the
capture carries the S->C `OP_Action` epoch the clip windows on. Record 325:
the B1174 lane-5 leg (`refactor/B1174/README.md` sec. 4: lit +0.422..+4.355, peak
cov 0.066, run cov 0.052, no sprite). Record 326: this README's /disc
landing leg (line 623: lit +0.454..+1.120, peak cov 0.067, run cov 0.054 =
the caster's arm animation, no sprite), capture
`review/B1162/worker/oracle/b1162_disc4499_v3` (byte-identical to
`~/kb-evidence/kb-b1162-combat-fx/oracle/b1162_disc4499_v3`, md5-checked).

**The ledger clip.** `review/B1162/worker/ledger/clips/b1162_disc4499_defensive.mp4`
(+ `_strip.png`), rebuilt by `refactor/B1162/scripts/make_disc_clip_4499.py`
on the B1158 clip builder: oracle f14078 (BC=Act) @ 30 fps (t0
1793106624.361, the capture's `video.calibration.tsv`), willeq f45 (BC=Act)
@ 30 fps, window BC-1.0..Act+8.0 (270 frames). Verdicts: `Source none
(both)=PASS; Landing sprite none (both)=PASS; caster s01 swing at the
packet (both)=PASS; swing anatomy: different cameras=n/a` (the observer rig
vs the b1130-self preset -- the B1174 convention). The other two alignment
lines (`b1162_fear229_intimidation`, `b1162_disc8604_leechcurse`) are
untouched; the index is regenerated, and `ledger/index.tsv` + `index.html`
-- never generated before this leg -- now exist (3 rows).

**Commands** (repo root; the render runs on the ThinkPad, the measure here,
per the 2026-10-29 rule):

```bash
bash refactor/B1162/scripts/disc_harness_rows.sh <worktree> kb-b1162-disc-record \
    ~/kb-evidence/kb-b1162-disc-record
python3 refactor/B1162/scripts/disc_rows_tsv.py ~/kb-evidence/kb-b1162-disc-record \
    refactor/B1162/rows/disc325_326_harness.tsv
python3 refactor/B1162/scripts/make_disc_clip_4499.py \
    /mnt/projects/fable/review/B1162/worker/ledger \
    /mnt/projects/fable/review/B1162/worker/oracle/b1162_disc4499_v3 \
    ~/kb-evidence/kb-b1162-disc-record/frames/disc326_4499
```

**Gates.** The diff touches no C++ -- the targeted ctest is the hygiene
label plus the FxHarness/Discipline pins. On this branch (cut at
cdf6fbb67, 27 commits behind origin/fable 62fff22ec at the final
re-check): hygiene = 48/49 (`rbuild hygiene` at the tip,
`review/B1162/worker/hygiene_record_20261030b.log`), the one FAIL is
`DocCitations` on three anchored cites in `refactor/B1189/README.md`
(the createCastGlow / createLanding / createLandingFromSlots lines of
`src/client/graphics/spell_visual_fx.cpp`, 526 / 844 / 933 there) --
stale BASE cites from the red tip the branch was cut at (fable pipeline
2583), fixed on fable by 5c9cde90e (fable pipeline 2627 green). They are
not in this diff and disappear in the MR's merged tree: MR pipeline 2617
(merge ref, 01:17 UTC) failed on exactly those three cites and nothing
else -- `2347 citation(s): 2187 ok, 3 fail, 157 skip`, every cite in this
leg's README among the ok -- so the MR re-run created by the final push
against 62fff22ec is the gate
(`~/kb-evidence/kb-b1162-disc-record/pipeline_state_20261030b.txt`, the
2617 hygiene job trace on GitLab). `rbuild test -R 'Discipline|FxHarness'`
= 12/12 passed (`review/B1162/worker/ctest_record_20261030.log`);
`scripts/refactor/cite_check.py` over this README: 0 problems (re-run at
the final commit of this leg).
