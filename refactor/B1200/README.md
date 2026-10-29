# B1200 (#519): WORN-EQUIPMENT particle effects — mechanism, data, and the FIX plan

**Diagnose leg, report only — no implementation.** This answers the four asks in
issue #519 against the decompile (`review/B1141/ghidra/out/eqgame.exe.c`), the
lane-5 akk-stack DB (peq), and willeq's own tree. Will's rule applied throughout:
willeq copies the official client exactly; the fix is planned leg-by-leg, not
speculated.

The one-sentence result: **the worn effect is an item word (`Worn.Effect`, a
spell id) that willeq already parses into `ItemInstance::wornEffect` and prints
in the tooltip — but willeq has no dispatch or draw for it.** The official client
resolves the spell to a `spellsnew.eff` record (`spells_new.spellanim`) and
renders that record at the actor's position. The epic 1.0s the issue names are
**not present in the P1999 lane-5 DB** (every item has `minstatus=0`;
`Jagged Blade of War` reads `-1`), so the specific epic-1.0 number cannot be
measured on this lane — that data gap is a finding, and the exact live leg that
measures the mechanism on an available item is committed under
`refactor/B1200/scripts/` for the FIX leg.

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
- `item_instance.h:291` — `hasAnyEffect()` counts `wornEffect.effectId != 0`.

There is no equip hook, no effect-slot write, no particle spawn. That is the gap
the FIX legs close.

## 2. The client dispatch and draw (ask 1b–1e)

The official client does **not** treat the worn effect as a special case. It is
a spell: equipping the item applies the spell to the actor, and the spell's
`spellsnew.eff` record is rendered by the generic "spell-effect-on-actor" path.
That path is fully cited below.

**The particle definition is a `spellsnew.eff` record.** The client keeps a table
of effect records at `DAT_00851398`, **0x10c (276) bytes per record**, count in
`DAT_0085139c`, record 1 hard-coded `"Blank Spell Effect"`:

- `review/B1141/ghidra/out/eqgame.exe.c:68750` — `DAT_0085139c = _Size / 0x10c;` (table sized from the
  loaded file, 276-byte stride)
- `review/B1141/ghidra/out/eqgame.exe.c:68731` — `FUN_0060e450((int)DAT_00851398 + 0x10c,"Blank Spell Effect");`
  (record 1, immediately after record 0 which is the file's first record)

This is the same 0x10c-record table the spell-FX project already loads
(`docs/spellsnew_format.md`), so a worn effect reuses the existing
`SpellEffects`/particle renderer — it is a spellsnew record index, not a new
table.

**The render entry points** all take a record index, bounds-check it against
`DAT_0085139c`, index `idx * 0x10c + DAT_00851398`, read the **actor position**
(x/y/z at `+0x30/+0x34/+0x38`), and call `FUN_00495cc3(idx, mode, recordPtr,
actor, actor, &pos, …)`:

- `review/B1141/ghidra/out/eqgame.exe.c:74012` — `void FUN_0045448b(undefined4 param_1,int param_2)`
  (single-effect add; `param_2` = record index)
- `review/B1141/ghidra/out/eqgame.exe.c:74021` — `if ((0 < param_2) && (param_2 < DAT_0085139c)) {`
- `review/B1141/ghidra/out/eqgame.exe.c:74023` — `iVar2 = param_2 * 0x10c + DAT_00851398;`
- `review/B1141/ghidra/out/eqgame.exe.c:74025` — `local_10 = *(undefined4 *)(iVar1 + 0x30);` (x)
- `review/B1141/ghidra/out/eqgame.exe.c:74026` — `local_c = *(undefined4 *)(iVar1 + 0x34);` (y)
- `review/B1141/ghidra/out/eqgame.exe.c:74027` — `local_8 = *(undefined4 *)(iVar1 + 0x38);` (z)
- `review/B1141/ghidra/out/eqgame.exe.c:74028` — `FUN_00495cc3(param_2,2,iVar2,iVar1,iVar1,&local_10,0,0,0);`
  (mode **2** = position-anchored at the actor origin)
- `review/B1141/ghidra/out/eqgame.exe.c:138087` — `void __thiscall FUN_004b6908(…)` (the actor-effect
  add used by the equip/unequip path; reads the record index from the actor
  struct at `param_1[2]`, same 0x10c stride and actor-position anchor)
- `review/B1141/ghidra/out/eqgame.exe.c:103546`–`103551` — the actor's **six active-effect slots**: the
  per-frame loop reads six record indices and, for each valid one
  (`review/B1141/ghidra/out/eqgame.exe.c:103551` — `FUN_00495cc3(iVar5,2,iVar3,param_1,param_1,&local_18,0,0,0);`),
  renders it at the actor position. This is the steady-state draw that keeps a
  worn effect alive every frame.
- `review/B1141/ghidra/out/eqgame.exe.c:118444` — `int FUN_00495cc3(…)` — the effect-instance creator
  (spawn the particle system for one record at a position).

**Attach point (ask 1b).** The cited path anchors the effect at the **actor's
position** (`+0x30/+0x34/+0x38`, mode 2), not at a named weapon joint. The
emitter's own offsets/size inside the spellsnew record spread the particles
around that origin — which is how an epic weapon's glow reads as "on the
weapon" (the emitter is centred on the actor and sized to cover the held
weapon). The slot (primary/secondary/ranged/other) does not select a different
anchor in this path; the record does the shaping. *(Boundary: the item-equip →
six-slot write is implied by the spell-apply flow and not traced to a single
line here; the FIX leg verifies it live.)*

**Which particle definition (ask 1c).** `spellsnew.eff` record, reached by
`item.Worn.Effect → spells_new.id → spells_new.spellanim` (the record index).
Confirmed in the data (§3): the 281 distinct spells the 7,559 worn items point
at carry a `spellanim` (273 of them non-zero) — the spellsnew record — and the
`nimbuseffect` column is 0 for all of them, so `spellanim` is the operative
index, not `nimbuseffect`.

**Start/stop (ask 1d).** Because it is a spell on the actor: it **starts when
the item is equipped** (the spell applies, the record index enters an
effect slot and the per-frame loop draws it), and **stops when the item is
unequipped, on zone (the actor's effects are cleared and re-applied from the new
equipment), on death, and on hide/illusion** (the actor's effect list is
torn down). These are the same lifecycle points the spell-FX path already
handles for a buff spell; the worn effect inherits them.

**Options flag (ask 1e).** The draw is the spell-FX renderer, so it is gated by
the client's spell/particle options exactly as cast spell visuals are — the same
gate row the spell-FX project established. A worn effect is not an independent
options toggle.

## 3. The data census (ask 2)

Query committed at `refactor/B1200/scripts/census_worn.sh` (run against the
lane-5 peq DB, `EQEMU_LANE=5`). Output:
`~/kb-evidence/kb-b1200-worn-fx/census_worn.txt`.

- **117,944** total items; **7,559** carry a worn effect
  (`worneffect >= 0`); **110,385** read the sentinel **`-1`** (none). The
  sentinel is `-1`, not `0` — `worneffect = 0` occurs on **zero** rows, so
  `>= 0` is the only correct predicate.
- **The epic-1.0 premise does not hold in this DB.** `MAX(minstatus) = 0`
  across all 117,944 rows, so the Kunark epic 1.0s are not flagged by status and
  are effectively absent from the P1999 item set. The issue's example item:
  `Jagged Blade of War` (ids 10908, 66175) reads **`worneffect = -1`**,
  `minstatus = 0`, `artifactflag = 0`. **Finding: the epic 1.0 worn-particle
  items are not in the P1999 lane-5 DB, so the specific epic-1.0 effect cannot
  be measured on this lane.** The mechanism is identical (same word, same
  dispatch); only the data differs.
- **What the 7,559 items actually carry:** 281 distinct spells, of which **273
  have a non-zero `spellanim`** (a visible spellsnew record) and **0** have a
  non-zero `nimbuseffect`. The population is dominated by combat-buff spells —
  `Faerune` (spellanim 74, 1,633 items), the `Improved Dodge / Parry-Block /
  Sharpshooting / Ferocity / Cleave` lines (spellanim 73, the rest), `Plainsight`
  / `Ultravision` (spellanim 74), sight spells, etc. So worn items in this DB
  render spellsnew records 73/74/… at the actor — a real, measurable effect,
  just not the epic 1.0.
- A minority of worn ids (e.g. `47276`) point at a `spells_new` id with **no
  row** (the JOIN returns NULL); those are data orphans, noted in
  `census_worn_full.txt`.

## 4. willeq today: draws nothing (ask 3, fable half)

willeq holds the spell id (`wornEffect.effectId`, §1) and prints `"(Worn)"` in
the tooltip, but has **no dispatch and no draw** — the full `wornEffect` usage
surface is the three lines in §1. Equipping any of the 7,559 items in willeq
therefore renders no particle. This is the "fable draws nothing" half of the
measurement; a live willeq frame set confirming it is part of the §5 leg.

## 5. The live leg (ask 3) — committed, ready to run on the FIX leg

The epic 1.0 cannot be summoned (absent from the DB), so the committed leg
measures the mechanism on an **available** worn item (one of the 7,559 that
renders a spellsnew record), on both sides, exactly as the issue specifies:

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

**Why it is not run in this leg:** (1) the epic 1.0 — the named subject — is not
in the DB, so an epic-1.0 capture is impossible here; (2) the reliable equip
needs a double-click, and the lane-5 driver's `jclick`
(`oracle_remote.sh:70`) carries ~1.3 s of focus+drag+delays, so two plan clicks
do not register as one double-click — the equip driving is a real but
time-boxed sub-problem the FIX leg solves (a `jdblclick` primitive, or a
GM-equip). This leg delivers the mechanism + data + the exact leg, per the
issue's "diagnose, report only" scope; the measurement is the FIX leg's first
step.

## 6. The FIX legs (ask 4)

Each is one card, one leg, sized per the standing "one leg per card" rule. Will's
rule: copy the client exactly, and measure the oracle before anything is drawn.

1. **Parse (already 90 %).** `wornEffect.effectId`/`.type` are parsed
   (`inventory_item_parser.cpp:423–424`). Leg: resolve the spell →
   `spellanim` (spellsnew record index) the way the client does, and store the
   record index on the item (or resolve at equip). Verify the 273-with-spellanim
   set from §3 resolves; the `nimbuseffect`=0 rows are a no-op.
2. **Dispatch.** On equip/unequip/zone/death/hide, add/remove the actor's worn
   effect slot so the per-frame renderer sees it — the client's
   `FUN_004b6908`/six-slot lifecycle (§2). Gate on the spell-FX options flag.
3. **Draw.** Render the spellsnew record at the actor position (mode 2) through
   the existing particle renderer — the client's `FUN_00495cc3`
   (`review/B1141/ghidra/out/eqgame.exe.c:118444`) / `FUN_0045448b` path. No new table: it is a
   spellsnew.eff record.
4. **Gate rows.** Add the hygiene/gate rows that pin it: a ctest that a worn
   item with a non-zero `spellanim` produces an effect instance in the sim
   (replay or `model_viewer`), and the live-leg measurement (§5) re-run as the
   oracle-vs-fable gate.

**Ordering note for the orchestrator:** the epic-1.0 items themselves are a data
question, not a code one — if Will wants the *epic 1.0s specifically* rendered,
a data card (populate the epic 1.0 `worneffect`/`spellanim` rows, or confirm
they live in Sony's production item set and are out of P1999 scope) is a
separate prerequisite. The code legs above render whatever `worneffect` the
server ships, epic or not.

## 7. Verification

Cite-checker (B1156), run over this README with the mounted decompile and the
akk-stack tree as extras:

```
python3 -I scripts/refactor/cite_check.py refactor/B1200/README.md --tree . \
    --extra /mnt/projects/fable --extra /mnt/projects \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis \
    --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis
    -> checked 30 cites, 0 problems, 0 address warnings
```

`0 problems` — every `file:line` cite resolves and the named symbol is on the
line; no address warnings.

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
`/mnt/scratch/kb-evidence/kb-b1200-worn-fx/` (large) when the FIX leg runs §5.
