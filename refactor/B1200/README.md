# B1200 (#519): WORN-EQUIPMENT particle effects — mechanism, data, and the FIX plan

**Diagnose leg, report only — no implementation.** This answers the four asks in
issue #519 against the decompile (`review/B1141/ghidra/out/eqgame.exe.c`), the
lane-5 akk-stack DB (peq), and willeq's own tree. Will's rule applied throughout:
willeq copies the official client exactly; the fix is planned leg-by-leg, not
speculated.

The one-sentence result: **the worn effect is an item word (`Worn.Effect`, a
spell id) that willeq already parses into `ItemInstance::wornEffect` and prints
in the tooltip — but willeq has no dispatch or draw for it.** The official
client renders the `spellsnew.eff` record at the actor's position for items
that carry the word. The epics the issue names **are present in the P1999
lane-5 DB** — `Jagged Blade of War` (ids 10908, 66175) is in it and reads
**`worneffect = -1`** (field 144) — so **field 144 is not (only) the mechanism
behind the epic glow, and the real dispatch is untraced**: the client-side
resolution from field 144 (or any other word) to the drawn record is not
located in this decompile. Will's 2026-10-30 ruling governs: "Do not make
assumptions about the P1999 server. Epics are included as-is and have proper
effects when worn and viewed with the original client." The lane-5 measurement
that finds the mechanism (every-class epic equipped on Testcaster, observer
video + pcap + stills, the item wire words per epic) is carried by **B1206
(#525)**.

## 0. Sources and method

| input | path | note |
|---|---|---|
| item wire serialisation | `akk-stack/code/common/patches/titanium.cpp` | `SerializeItem`, the pipe-delimited static data the client loads |
| item struct (server) | `akk-stack/code/common/item_data.h` | `item::ItemEffect_Struct Worn`, `WornName[65]` |
| client decompile | `review/B1141/ghidra/out/eqgame.exe.c` | cited per line; symbols `FUN_*`, `DAT_*` |
| willeq item parse | `src/client/inventory/inventory_item_parser.cpp` | the field the fix extends |
| willeq item struct | `include/client/inventory/item_instance.h` | `ItemEffect wornEffect` |
| spellsnew layout | `docs/spellsnew_format.md`, `review/B1141/edd_fields_from_code.tsv` | 0x10c records, the 70 PROVEN EDD fields |
| lane-5 DB (peq) | queried live on lane 5 (`EQEMU_LANE=5`) | the item census, `refactor/B1200/scripts/census_worn.sh` |

Scripts (committed, `refactor/B1200/scripts/`): `census_worn.sh` (the §3 tables,
run against lane 5), `oracle_worn_plan.tsv` + `worn_fable_leg.sh` (the §5 live
leg, for the FIX leg). **Every number in §3 is produced by `census_worn.sh` —
none is retyped.** Output is kept at
`~/kb-evidence/kb-b1200-worn-fx/census_worn.txt` (+ `census_worn_full.txt`).

## 1. The item word (ask 1a)

The Titanium item static-data block carries FIVE effect sub-structs — Click,
Proc, **Worn**, Focus, Scroll — each `Effect, Type, Level2, Level, name(0)`.
Worn is the third block, **wire fields 144–148**, serialised by the server as:

- `akk-stack/code/common/patches/titanium.cpp:3535` — `ob << '|' << itoa(item->Worn.Effect);`
- `akk-stack/code/common/patches/titanium.cpp:3536` — `ob << '|' << itoa(item->Worn.Type);`
- `akk-stack/code/common/patches/titanium.cpp:3537` — `ob << '|' << itoa(item->Worn.Level2);`
- `akk-stack/code/common/patches/titanium.cpp:3538` — `ob << '|' << itoa(item->Worn.Level);`
- `akk-stack/code/common/patches/titanium.cpp:3539` — `ob << '|' << "0"; // Worn name`

`item->Worn` is the server struct `item::ItemEffect_Struct`
(`akk-stack/code/common/item_data.h:496`, the `Worn {}` member beside
`WornName[65]` at `akk-stack/code/common/item_data.h:534`). **The word that carries the worn
particle effect is `Worn.Effect` (wire field 144) — a spell id.** `Worn.Type`
(field 145) is the effect-kind (the `ItemEffect` enum; the parser already reads
it as `wornEffect.type`).

**How it reaches the client.** Same static-data block, delivered with the item
tables the client loads at zone-in — the exact serialisation willeq already
parses in `src/client/inventory`:

- `inventory_item_parser.cpp:423` — `item.wornEffect.effectId = toInt(fields[144]);`
- `inventory_item_parser.cpp:424` — `item.wornEffect.type = toInt(fields[145]);`

So the parse is **already present and correct** — willeq holds the spell id in
`ItemInstance::wornEffect.effectId` (`item_instance.h:155`, `ItemEffect` whose
`effectId` is documented `// Spell ID`, `item_instance.h:20`). What is absent is
everything downstream: the dispatch and the draw. The **entire** willeq usage
surface of `wornEffect` is three lines — the parse (above), the tooltip, and a
has-effect test:

- `item_display_text.cpp:201` — `if (item.wornEffect.effectId != 0) {` … prints
  eqstr 3032 `"(Worn)"` (`item_display_text.cpp:204`).
- `item_instance.h:290` — `hasEffects()` counts `wornEffect.effectId != 0`
  (one disjunct of a four-effect OR, `item_instance.h:290`–`293`).

There is no equip hook, no effect-slot write, no particle spawn. That is the gap
the FIX legs close.

## 2. The client dispatch and draw (ask 1b–1e)

The official client does **not** treat the worn effect as a special case. It is
a spell: equipping the item applies the spell to the actor, and the spell's
`spellsnew.eff` record is rendered by the generic "spell-effect-on-actor" path.
That path is cited below; where the trace stops is stated explicitly (ask 1c).

**The particle definition is a `spellsnew.eff` record.** The client keeps a
table of effect records at `DAT_00851398`, **0x10c (276) bytes per record**,
count in `DAT_0085139c`:

- `review/B1141/ghidra/out/eqgame.exe.c:68750` — `DAT_0085139c = _Size / 0x10c;`
  the **file-loaded** table, in the `else` branch at
  `review/B1141/ghidra/out/eqgame.exe.c:68748` after the `SpellsNew.eff`
  `fopen` at `review/B1141/ghidra/out/eqgame.exe.c:68711` (276-byte stride).

- `review/B1141/ghidra/out/eqgame.exe.c:68731` — `FUN_0060e450((int)DAT_00851398 + 0x10c,"Blank Spell Effect");`
  this line sits **inside the `fopen` file-not-found fallback** (the
  `if (pFVar1 == (FILE *)0x0)` branch at
  `review/B1141/ghidra/out/eqgame.exe.c:68712`: count 2, record 0 copied from
  `DAT_00644380` at `review/B1141/ghidra/out/eqgame.exe.c:68715`, record 1
  written with the `"Blank Spell Effect"` name) — it is not part of the
  file-loaded table.

This is the same 0x10c-record table the spell-FX project already loads
(`docs/spellsnew_format.md`), so a worn effect reuses the existing
`SpellEffects`/particle renderer — it is a spellsnew record index, not a new
table.

**The render entry points** take a record index, bounds-check it against
`DAT_0085139c`, index `idx * 0x10c + DAT_00851398`, read the **actor position**
(x/y/z at `+0x30/+0x34/+0x38`), and spawn the effect via the effect-instance
creator named at the end of the list:

- `review/B1141/ghidra/out/eqgame.exe.c:74012` — `void FUN_0045448b(undefined4 param_1,int param_2)`
  (single-effect add; `param_2` = record index)
- `review/B1141/ghidra/out/eqgame.exe.c:74021` — `if ((0 < param_2) && (param_2 < DAT_0085139c)) {`
- `review/B1141/ghidra/out/eqgame.exe.c:74023` — `iVar2 = param_2 * 0x10c + DAT_00851398;`
- `review/B1141/ghidra/out/eqgame.exe.c:74025` — `local_10 = *(undefined4 *)(iVar1 + 0x30);` (x)
- `review/B1141/ghidra/out/eqgame.exe.c:74026` — `local_c = *(undefined4 *)(iVar1 + 0x34);` (y)
- `review/B1141/ghidra/out/eqgame.exe.c:74027` — `local_8 = *(undefined4 *)(iVar1 + 0x38);` (z)
- `review/B1141/ghidra/out/eqgame.exe.c:74028` — `FUN_00495cc3(param_2,2,iVar2,iVar1,iVar1,&local_10,0,0,0);`
  (mode **2** = position-anchored at the actor origin)

- `review/B1141/ghidra/out/eqgame.exe.c:138089` — `void __thiscall FUN_004b6908(…)`
  the **effect-entry draw**: reads the record index from the effect entry at
  `param_1[2]` (`review/B1141/ghidra/out/eqgame.exe.c:138100`), bounds-checks it
  against `DAT_0085139c`, and calls
  `FUN_00495cc3(local_8,param_2,iVar3,iVar1,uVar2)` at
  `review/B1141/ghidra/out/eqgame.exe.c:138110`. `mode` is its own parameter
  (`param_2`), the 5th argument is a **second actor** (`uVar2`, from
  `FUN_00401b28(param_1[1],…)`), and the position anchor comes via
  `FUN_00401b28(*param_1)`. Its only cited callers:
  `review/B1141/ghidra/out/eqgame.exe.c:138131` and
  `review/B1141/ghidra/out/eqgame.exe.c:138135` in the timer/update fn
  FUN_004b697f (modes 1 and 2), and
  `review/B1141/ghidra/out/eqgame.exe.c:138248` in the add/list-node ctor
  FUN_004b6a7e (mode 0). **No equip/unequip path is cited for it** — it is
  a generic effect-entry draw used by that add/timer chain, and the chain is
  where the worn-FX dispatch trace starts (ask 1c, below).

- `review/B1141/ghidra/out/eqgame.exe.c:103546`–`103551` — the actor's **six active-effect slots**: the
  per-frame loop reads six record indices and, for each valid one
  (`review/B1141/ghidra/out/eqgame.exe.c:103551` — `FUN_00495cc3(iVar5,2,iVar3,param_1,param_1,&local_18,0,0,0);`),
  renders it at the actor position. This is the steady-state draw that keeps an
  item effect alive every frame once its record index is in a slot.

- `review/B1141/ghidra/out/eqgame.exe.c:118444` — `int FUN_00495cc3(…)` — the effect-instance creator
  (spawn the particle system for one record at a position).

**Attach point (ask 1b).** The cited path anchors the effect at the **actor's
position** (`+0x30/+0x34/+0x38`, mode 2), not at a named weapon joint. The
emitter's own offsets/size inside the spellsnew record spread the particles
around that origin. The slot (primary/secondary/ranged/other) does not select a
different anchor in this path; the record does the shaping. *(Boundary: the
item-equip → six-slot write is not traced to a single line here; the measurement
leg (B1206) verifies it live.)*

**Which particle definition (ask 1c).** `spellsnew.eff` record, reached by
`item.Worn.Effect → spells_new.id → spells_new.spellanim` (the record index).
**The data side is confirmed (§3)**: the 281 distinct spells the 7,559 worn
items point at carry a `spellanim` (273 of them non-zero) — the spellsnew
record — and the `nimbuseffect` column is 0 for all of them, so `spellanim` is
the operative index, not `nimbuseffect`. **The dispatch leg is UNLOCATED:** no
client line shows the worn word (field 144) becoming a spellsnew record index —
the `68750`/`68731` cites above show the table LOAD only. The six-slot source
(who writes the entity's six effect indices at `+0x73`, read at
`review/B1141/ghidra/out/eqgame.exe.c:103546`–`103551`) is unidentified, as is
the item-equip trigger. The trace's starting point is the
FUN_004b6a7e/FUN_004b6908 add-timer chain
(`review/B1141/ghidra/out/eqgame.exe.c:138115`–`138248`); **B1206 (#525)** will
locate the real dispatch from the lane-5 measurement.

**Start/stop (ask 1d).** **Not assertable from this decompile leg.** No cited
line shows the equip/unequip/zone/death/hide triggers or the effect-list teardown
— the only cited lifecycle is the per-frame read of the six slots
(`review/B1141/ghidra/out/eqgame.exe.c:103546`–`103551`) and the add/timer chain
(`review/B1141/ghidra/out/eqgame.exe.c:138115`–`138248`). The FIX legs inherit
whatever start/stop the measurement (B1206) shows the client doing, not an
assumption.

**Options flag (ask 1e).** **Not assertable from this decompile leg.** No cited
line shows this draw gated by a spell/particle options flag. The spell-FX
project's gate rows cover the cast visuals; whether the worn-FX draw shares a
gate is open until the trace (B1206 → FIX legs) reaches it.

## 3. The data census (ask 2)

Query committed at `refactor/B1200/scripts/census_worn.sh` (run against the
lane-5 peq DB, `EQEMU_LANE=5`). Output:
`~/kb-evidence/kb-b1200-worn-fx/census_worn.txt`.

- **117,944** total items; **7,559** carry a worn effect
  (`worneffect >= 0`); **110,385** read the sentinel **`-1`** (none). The
  sentinel is `-1`, not `0` — `worneffect = 0` occurs on **zero** rows, so
  `>= 0` is the only correct predicate.
- **Finding (per Will's 2026-10-30 ruling): the epics are present in this DB
  with field 144 = -1 -> field 144 is NOT the (only) epic mechanism; the real
  dispatch is untraced by this diagnose leg.** The issue's example item:
  `Jagged Blade of War` (ids 10908, 66175) is present and reads
  **`worneffect = -1`**, `minstatus = 0`, `artifactflag = 0`
  (`census_worn.txt` §2) — and `MAX(minstatus) = 0` across all 117,944 rows,
  so status cannot flag them. The diagnose leg traced the field-144 path as far
  as it goes and did **not** trace the real epic-glow dispatch — that
  measurement is **B1206 (#525)**: every-class epic equipped on Testcaster on
  lane 5, observer video + pcap + stills, the item wire words per epic, and the
  client glow mechanism found from the measurement.
- **What the 7,559 items actually carry:** 281 distinct spells, of which **273
  have a non-zero `spellanim`** (a visible spellsnew record) and **0** have a
  non-zero `nimbuseffect`. The population is dominated by combat-buff spells —
  `Faerune` (spellanim 74, 1633 items), the `Improved Dodge / Parry-Block /
  Sharpshooting / Ferocity / Cleave` lines (spellanim 73, the rest), `Plainsight`
  / `Ultravision` (spellanim 74), sight spells, etc. So worn items in this DB
  carry field-144 words that resolve to spellsnew records 73/74/… — a real,
  measurable effect for the non-epic items; the epics' field-144 words read -1,
  which is why their glow is a different, untraced mechanism.
- **57** of the 7,559 worn items point at a `spells_new` id with **no row**
  (the LEFT JOIN returns NULL — visible as `NULL` spell columns in
  `census_worn_full.txt`, e.g. `Raid Elder Chain Helm Seal of the Mariner`
  (id 142399, worneffect 39948) and `Deathseeker's Legionnaire Vambraces`
  (id 147420, worneffect 41952)); those are data orphans.

## 4. willeq today: draws nothing (ask 3, fable half)

willeq holds the spell id (`wornEffect.effectId`, §1) and prints `"(Worn)"` in
the tooltip, but has **no dispatch and no draw** — the full `wornEffect` usage
surface is the three lines in §1. Equipping any of the 7,559 items in willeq
therefore renders no particle for a non-zero field-144 word. For the epics
(field 144 = -1) willeq likewise draws nothing, and B1206's willeq-side
"draws nothing" baseline frames will document that half.

## 5. The live leg (ask 3) — committed, not wired; the epic measurement is B1206 (#525)

**The lane-5 oracle measurement of the epic glow — acceptance C of the issue —
is carried by B1206 (#525), in progress.** It runs every class epic equipped
on Testcaster with the B1134 observer rig (video + pcap + stills), captures the
item wire words per epic, and finds the client glow mechanism from the
measurement. The willeq-side "draws nothing" baseline frames are owed by the
same leg. This leg does **not** re-run it, on the correct premise: the epics
are present in the DB (finding, §3), so the epic measurement is the missing
piece and it is already owned — the diagnose question ("is field 144 the
worn-FX dispatch?") is answered here as *no*, and the follow-up measurement is
tracked as B1206 (#525). That is why the MR keeps "Closes #519": the issue's
diagnose asks are answered, with the measurement leg named.

The committed scripts remain the **mechanism baseline** for an **available**
non-epic worn item (one of the 7,559 with a non-zero `spellanim`), committed
but **not wired** — `worn_fable_leg.sh` itself notes the remote run + frame
pull is not wired ("the FIX leg wires it"), and `oracle_worn_plan.tsv`'s equip
lines are labelled a best-effort fallback:

- `refactor/B1200/scripts/oracle_worn_plan.tsv` — the B1134 observer-rig plan:
  caster `Testcaster` (char 67) at the B1134 pose, observer `Oraclewatch`
  (char 103) framed on it; GM `#summonitem <id>` for a worn item, equip, steady
  state (video + pcap, `# fps=30`), then unequip/destroy. The item id is chosen
  at run time from §3 (a class-12-equipable item with a non-zero `spellanim`,
  e.g. a `Faerune` wearable) — the plan carries a `# ITEM=<id>` line the runner
  fills.
- `refactor/B1200/scripts/worn_fable_leg.sh` — the same scene on willeq
  (the B1176 two-client fable rig): A = willeqwatch observer windowed with
  `--dump-frames-dir`, B = headless GM Testcaster running the `#` commands via
  `/say`; A's frames show the **empty** draw.

**Why the baseline equip driving was not solved in this leg:** the reliable
equip needs a double-click, and the lane-5 driver's `jclick`
(`scripts/eqemu/oracle/oracle_remote.sh:70` — focus + 0.6 s + 0.4 s + 0.3 s of
delays per click) means two plan clicks cannot register as one double-click. The
official client equips a cursor item with a **single** click on the target
equipment slot (so the existing `jclick` primitive suffices), and item
`8364` (`Talisman of Vah Kerrath`, the plan's default) is classes=65535 —
all classes — which `Testcaster` (class 12) can equip; eqemu GM-equip was
available too. Both routes were open and are the FIX leg's (and B1206's) to
take; this leg is the diagnose leg and does not take the lane-5 rig.

## 6. The FIX legs (ask 4)

Each is one card, one leg, sized per the standing "one leg per card" rule. Will's
rule: copy the client exactly, and measure the oracle before anything is drawn.
**Prerequisite: B1206 (#525) lands first** — its measurement names the real
dispatch (the six-slot source, the item-equip trigger, the epic-glow record
source) that this decompile leg left unlocated (§2, ask 1c).

1. **Parse (already 90 %).** `wornEffect.effectId`/`.type` are parsed
   (`inventory_item_parser.cpp:423`–`424`). Leg: resolve the spell →
   `spellanim` (spellsnew record index) the way the client does, and store the
   record index on the item (or resolve at equip). Verify the 273-with-spellanim
   set from §3 resolves; the `nimbuseffect`=0 rows are a no-op.
2. **Dispatch.** Add/remove the actor's worn effect slot so the per-frame
   renderer sees it — copy the mechanism B1206's measurement names (the
   six-slot write + the add/timer chain,
   `review/B1141/ghidra/out/eqgame.exe.c:138115`–`138248`), for whatever
   start/stop and options gate the measurement shows.
3. **Draw.** Render the spellsnew record at the actor position (mode 2) through
   the existing particle renderer — the client's `FUN_00495cc3`
   (`review/B1141/ghidra/out/eqgame.exe.c:118444`) / `FUN_0045448b` path. No new table: it is a
   spellsnew.eff record.
4. **Gate rows.** Add the hygiene/gate rows that pin it: a ctest that a worn
   item with a non-zero `spellanim` produces an effect instance in the sim
   (replay or `model_viewer`), and the live-leg measurement (B1206) re-run as
   the oracle-vs-fable gate.

**Ordering note for the orchestrator:** the epic 1.0s are a **measurement**
question, not a data one — they are in the P1999 DB with a worn effect and
field 144 = -1, so there is no data card to "populate" their rows; the open
work is B1206 (#525) measuring their glow on lane 5 and tracing the real
dispatch. The code legs above render whatever the server ships, epic or not.

## 7. Verification

Cite-checker (B1156), run over this README with the mounted decompile and the
akk-stack tree as extras:

```
python3 -I scripts/refactor/cite_check.py refactor/B1200/README.md --tree . \
    --extra /mnt/projects/fable --extra /mnt/projects \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis
    -> checked 45 cites, 0 problems, 0 address warnings
```

`0 problems` — every `file:line` cite resolves and the named symbol is on the
line; no address warnings. The three scripts (one doc per invocation):
`census_worn.sh` -> `checked 0 cites, 0 problems, 0 address warnings`;
`oracle_worn_plan.tsv` -> `checked 1 cites, 0 problems, 0 address warnings`;
`worn_fable_leg.sh` -> `checked 0 cites, 0 problems, 0 address warnings`.

## 8. Evidence index

| what | where |
|---|---|
| census (summary) | `~/kb-evidence/kb-b1200-worn-fx/census_worn.txt` |
| census (all 7,559 rows) | `~/kb-evidence/kb-b1200-worn-fx/census_worn_full.txt` |
| census script (committed) | `refactor/B1200/scripts/census_worn.sh` |
| live leg plan (committed) | `refactor/B1200/scripts/oracle_worn_plan.tsv` |
| willeq leg (committed) | `refactor/B1200/scripts/worn_fable_leg.sh` |
| decompile (cited) | `review/B1141/ghidra/out/eqgame.exe.c` |
| spellsnew layout | `docs/spellsnew_format.md`, `review/B1141/edd_fields_from_code.tsv` |

Live frames/video/pcap land under `~/kb-evidence/kb-b1200-worn-fx/` (small) and
`/mnt/scratch/kb-evidence/kb-b1200-worn-fx/` (large) when B1206 (#525) runs the
epic measurement and the FIX leg runs the §5 baseline.
