# B1209 (#528): B1189 P13/P14 (NONE) — aura / ground circle: diagnose

Leg: **diagnose** (report only; no C++ change). Worktree `task/kb-b1209-aura-ground`,
cut at origin/fable `74dabc3a` (the B1192 merge).

Sources:

- decompile: `/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c` — every
  decompile cite below is written `review/B1141/ghidra/out/eqgame.exe.c:<line>`
  (the same file, relative to `/mnt/projects/fable`);
- mounted data: `/mnt/projects/EverQuestP1999` (all census tables are computed
  from it by the committed scripts, §6);
- B1189 census rows P13/P14: `refactor/B1189/README.md` section 1;
- B1138 deletion: `refactor/B1138/README.md` step 4 and
  `include/client/graphics/spell_visual_fx.h:195-196`.

## 0. Bottom line

1. **P13's `0x491cff` is not the aura / ground-circle creation path.** It is a
   **stage-FX visibility gate**: a pure predicate,
   `bool FUN_00491cff(stage, source, target)` (review/B1141/ghidra/out/eqgame.exe.c:115163), called once,
   at review/B1141/ghidra/out/eqgame.exe.c:122018, by the stage dispatcher
   `FUN_0049b260` (defined review/B1141/ghidra/out/eqgame.exe.c:121989), to decide
   whether a spellsnew-record stage's effect is drawn for a given actor pair.
   It reads **no spellsnew asset word and no EDD word** — no shape, attach,
   motion, lifetime or texture: its inputs are one user-option byte
   (`[Defaults] SpellParticleCastFilter`, `DAT_0098581c`,
   review/B1141/ghidra/out/eqgame.exe.c:115177-115192) and the two actors' death-state char (`+0x244`)
   and identity (review/B1141/ghidra/out/eqgame.exe.c:115204-115216). The actual drawing reads
   the EDD words, in `FUN_0049ab78` (review/B1141/ghidra/out/eqgame.exe.c:121698) — §1.1.
2. **P14's `0x44d0d0` is not a buff/aura path.** It is a **spell-ID range
   validator**: `1` iff `1 <= id <= 9999` (review/B1141/ghidra/out/eqgame.exe.c:68316-68322). 90 call
   sites, all generic spell-ID gates (`tables/call_sites.tsv`). It is not on
   the aura path.
3. **What the client actually draws as an "aura / ground circle"** is two
   things: (a) the **ring/disc EDD emitters on the record path** the B1189
   census already traced (P1/P3/P4/P6) — 39 of the 42 measured spellsnew
   records carry at least one ring/disc EDD entry (148 slot lines,
   `tables/auracircle_shaped.tsv`) — and (b) **one legacy path**: the
   per-frame buff tick `FUN_004368b3` (review/B1141/ghidra/out/eqgame.exe.c:49746), which draws the
   `spells.eff` stage of every buff whose `+0x22` char is `'5'`
   (review/B1141/ghidra/out/eqgame.exe.c:49805) through `FUN_0049ab78` (review/B1141/ghidra/out/eqgame.exe.c:49813-49814).
   The mounted data makes (b) nearly a dead path: **213** of the 8,066
   spells carry the P8 class (effect `5`, `tables/effect_column.tsv`), but
   **211 of the 213 also carry a spellsnew record** — only **4728 (Wave of
   Rage) and 6096 (Rage of the Zun'Muram)** are legacy-only P8 auras
   (`tables/effect_column.tsv` rows `effect5_split_*`). The oracle's ground
   ring and the 6127 legacy aura were re-measured on lane 5 for this card
   (§4): the ring is the measured B1134 P4b band reproduced; the legacy
   aura's presence on the wire is recorded per packet.
4. **The B1138-deleted entry points (`createBuffAura` / `createGroundCircle` /
   `createRainEffect` / `createImpactAtPosition`,
   `include/client/graphics/spell_visual_fx.h:195-196`) were willeq's own
   heuristics, not ports of `0x491cff`/`0x44d0d0`** (B1138 README step 4).
   B1189 §3.2's proposed card B1189-F12 ("re-create the aura/ground-circle
   paths from the client code") rests on that misidentification: there is no
   client function called "createBuffAura" behind either address.
5. **What willeq actually lacks** (gap list, §3):
   - (a) the **visibility gate** — willeq's FX dispatch has no corpse check, no
     per-actor filter and no option for the `0x491cff` predicate;
   - (b) the **P8 legacy buff-tick aura** — `BuffManager` stores
     `effect_type` (`src/client/spell/buff_manager.cpp:121,184,304`) but nothing
     under `src/client/graphics/` reads it (zero `effect_type` references there).

## 1. The trace

### 1.1 `0x491cff` — the stage-FX visibility gate

`bool FUN_00491cff(undefined4 param_1, int param_2, int param_3)` —
review/B1141/ghidra/out/eqgame.exe.c:115163. `param_1` = stage (0=Source, 1=Middle, 2=Target; the
names are the strings the draw compares, review/B1141/ghidra/out/eqgame.exe.c:121757,121760),
`param_2` = source actor, `param_3` = target actor. The dispatcher treats a
true return as **skip** (`if (cVar1 == '\0') { ...draw... }`,
review/B1141/ghidra/out/eqgame.exe.c:122018-122019), so false = draw.

Inputs read (every read cited; there are no others in the function):

| word | line | use |
|---|---|---|
| `DAT_0098581c` (`[Defaults] SpellParticleCastFilter`) | 115177 (`& 0x10`), 115181 (`& 8`), 115183 (`& 4`), 115184 (`& 2`) | the option bit decode |
| `source + 0x244` (death-state char) | 115211 (`== '\x01'`), 115214 (`== '\0'`) | source corpse / alive |
| `target + 0x244` | 115206 (`!= '\0'` → dead target never draws) | target alive |
| `FUN_004303a6(target + 0x120)` | 115207 | "target in the player list" |
| `FUN_00485df1(target + 0x120)` | 115208 | "target name matches" |
| `FUN_004303a6(source + 0x120)` | 115215 | "source in the player list" |
| `FUN_00485df1(source + 0x120)` | 115216 | "source name matches" |
| `DAT_00905ce4` (local player) | 115209, 115217 | the "my own" exemption |

The helpers: `FUN_004303a6` (review/B1141/ghidra/out/eqgame.exe.c:44069) calls `FUN_00430366`
(review/B1141/ghidra/out/eqgame.exe.c:44045), which scans the 72-name slots at `+0x19c`, stride
`0x118`, and returns the slot index or `-1`; `FUN_00485df1`
(review/B1141/ghidra/out/eqgame.exe.c:106491) compares the character's name (thiscall, name in ECX)
against the slot name at `+0x15c` or five `0x40` slots.

Option-bit decode (115181-115192): with the option byte `O`,

| O & 0x08 | O & 0x04 | O & 0x02 | resulting flags |
|---|---|---|---|
| set | – | – | all filter flags **on** (`bVar3`=`bVar4`=`bVar1`=`bVar5`=`bVar2` true, 115186-115194) |
| clear | set | – | list+name checks on (115186-115192: `bVar3=bVar6` false, list/name true) |
| clear | clear | set | name check on only (115190-115192) |
| clear | clear | clear | all filter flags **off** (115185 fall-through) |

(`bVar3` = skip the source check; the list flag calls `FUN_004303a6`;
the name flag calls `FUN_00485df1`; `bVar8 = !(O & 0x10)` reads
`DAT_0098581c` at review/B1141/ghidra/out/eqgame.exe.c:115177.)

Behavior per value (from the common path, review/B1141/ghidra/out/eqgame.exe.c:115196-115219):

| value | draws |
|---|---|
| `0x00`, `0x10` | local-centric (source local, or target alive+local; `0x10` adds the corpse-source exemption, 115211-115212 — `O&8` clear ⇒ `bVar3` false ⇒ 115200 can never fire for them) |
| `0x18` (**the default**) | everything (115200-115201: `bVar3 && !bVar8` ⇒ immediate draw) |
| `0x08` | live sources draw unconditionally; only a corpse/null source takes the target check (115211-115212) |
| `0x04`, `0x06` | target alive and (local, or in the player list, or name-matched) (115206-115208) |
| `0x02` | target alive and (local or name-matched) (115206-115208) |
| `0x12`, `0x14` | as `0x04`/`0x02` but the corpse-source exemption applies (115211-115212) |

The option byte's other sites: read with default `0x18` at
review/B1141/ghidra/out/eqgame.exe.c:132638 (`FUN_004ae64c("Defaults","SpellParticleCastFilter",0x18)`),
written at 132533, reset to `0x18` at 125621 (alongside its siblings
`DAT_0098582c`, `DAT_0098583c`, 125622, 125624), and the UI dropdown names the
key at 245183.

The sole caller, in the stage dispatcher `FUN_0049b260`
(review/B1141/ghidra/out/eqgame.exe.c:121988):

```
122018:  cVar1 = FUN_00491cff(param_1,param_3,param_4);
122019:  if (cVar1 == '\0') {            // gate false = draw
```

### 1.2 `FUN_0049b260` — the stage dispatcher (what the gate guards)

`FUN_0049b260(stage, EDDrecord*, source, target, pos/offset words)`,
review/B1141/ghidra/out/eqgame.exe.c:121988. It is the function that, per stage, selects the EDD
words, passes the visibility gate and dispatches to the draw:

| line | word | meaning |
|---|---|---|
| 121998-122001 | guard | `target==0`, `EDDrecord==0`, or both record words empty (`+0x22c` and `+0x234`), or `stage` outside 0..2 → no FX |
| 122004 | `*(int*)(stage*0x3a0 + 0x224 + EDD[+0x22c])` | the legacy `spells.eff` EDD slot index (the `EDD[+0x234]==0` side) |
| 122007 | `*(int*)(stage*0x44 + 0x40 + EDD[+0x234])` | the `spellsnew.eff` record's stage word — the word the B1189 census and `review/B1141/edd_fields_from_code.tsv` read |
| 122008-122016 | `stage != 1` and slot != -1 | `FUN_00479ee8(slot)` (review/B1141/ghidra/out/eqgame.exe.c:122015) on the stage's own actor — the previous-stage handle bookkeeping |
| 122018 | — | the visibility gate, §1.1 |
| 122020-122024 | legacy `spells.eff` fallback arm (`EDD[+0x234]==0`) | draw iff `DAT_0092484c != 0` **or** `stage == 1` **or** `FUN_0044ced9() == 1` (review/B1141/ghidra/out/eqgame.exe.c:122021) → `FUN_0049ab78(stage, EDD[+0x22c], *EDD, source, target, 0, pos, offset, ...)` (122022). `DAT_0092484c` is the P12 global FX-flag family (card B1205); `FUN_0044ced9` (review/B1141/ghidra/out/eqgame.exe.c:68131) is the FX-toggling check |
| 122025-122028 | `spellsnew.eff` record path (`EDD[+0x234]!=0`) | draw iff `DAT_0063f4cc < _DAT_00985810` and `< _DAT_00985814` and `DAT_0092484c != 0` (review/B1141/ghidra/out/eqgame.exe.c:122025-122026) → `FUN_00495cc3(*EDD, stage, EDD[+0x234], source, target, pos, offset, 0)` (122028) |

`*EDD` (`+0x000` of the record) is the spell ID — the same ID `0x44d0d0`
validates at the entry points below.

### 1.3 The dispatch sites that reach the gate (the P13 "path" is 5 calls)

| call | enclosing (def) | trigger |
|---|---|---|
| review/B1141/ghidra/out/eqgame.exe.c:122270 | `FUN_0049b39d` (122037) | the OP_BeginCast trigger: the dispatcher's `uVar20 == 0x3990` arm, review/B1141/ghidra/out/eqgame.exe.c:80566-80568; spell ID read from `+2` and validated by `0x44d0d0` at 122117 |
| review/B1141/ghidra/out/eqgame.exe.c:95153 | `FUN_00476b12` (94981) | the `0x4033` missile-object path (B1189 P4): stage from local_10, actor `+0xfe4` |
| review/B1141/ghidra/out/eqgame.exe.c:95672 | `FUN_0047789d` (95588) | the spell-complete/target path (B1189 P3): `stage 2`, the `0x36` value compare at 95659 |
| review/B1141/ghidra/out/eqgame.exe.c:101960 | `FUN_0047f4af` (101890) | the magic-affect path (B1189 P15/B1211's `0x525d` neighbour): spell ID at `+0x1b`, validated by 0x44d0d0 at 101912; the `SPA` flag at `+0x180` (101920) |
| review/B1141/ghidra/out/eqgame.exe.c:146701 | the dispatcher call inside `FUN_004bfe0f` (def 146548) | the deferred-source path on NPC spawn (B1189 P6): `stage 0`, ID validated at 146694 |

`0x411f3d` (review/B1141/ghidra/out/eqgame.exe.c:18097, B1189 P15's `0x304b` cast-start) also calls
the dispatcher's sibling `FUN_0049b39d` — twice, at 18860 and 18886, both
gated on the P12 global flags `DAT_00923798`/`DAT_00923884` (review/B1141/ghidra/out/eqgame.exe.c:18857-18861) — so the BeginCast glow is the one stage whose very call is
flag-gated before the `0x491cff` gate even runs.

### 1.4 `0x44d0d0` — the spell-ID validator (the whole P14 "path")

```
68316: undefined4 FUN_0044d0d0(int param_1)
68319:   uVar1 = 1;
68320:   if ((param_1 < 1) || (9999 < param_1)) { uVar1 = 0; }
```

No actor, no FX, no asset read — a range predicate. 90 call sites in
`tables/call_sites.tsv` (regenerated by `scripts/call_sites.py`): spell-name
lookups (one at review/B1141/ghidra/out/eqgame.exe.c:13511 in the spell-name helper `FUN_0040cf9b`), chat/parse sites (`FUN_00541d4a`
247563, `FUN_0054210e` 247812), tooltips (`FUN_00556d93` 264318, …), the zone
dispatcher's magic-affect arm (`FUN_0041c905` 26076, over `+0x1b`), the
`OP_BeginCast` handler (122117), the magic-affect path (101912) and the
deferred-spawn path (146694). B1189's P14 label "buff/aura" is a
misidentification: the buff/aura path is the P8 row of the B1189 census
(§1.5), which `0x44d0d0` does not touch.

### 1.5 The legacy aura: the per-frame buff tick (what P14 actually is)

`FUN_004368b3` (review/B1141/ghidra/out/eqgame.exe.c:49746) walks the buff list **every frame** —
callers at review/B1141/ghidra/out/eqgame.exe.c:60390, 60586, 60701 (the two per-frame loops and the
thunk of B1189's P8 row) — and for each buff:

| line | word | meaning |
|---|---|---|
| 49804 | buff `+0x4f` == `'\0'` | the buff is active (not suppressed) |
| 49805 | buff `+0x22` == `'5'` | **the aura class** — the char the server sets from the spell's effect type; 213 of the 8,066 mounted spells carry effect `'5'` (`tables/effect_column.tsv`) |
| 49807 | buff `+0x23` == `'\x01'` and `+0x94` == 0 | first frame of the aura → create |
| 49808 | `sVar2 = *(short*)(buff + 0x66)` | the `spells.eff` index |
| 49809 | `0 < sVar2 < 0x100` and local player non-null | bounds; only the local player's own auras draw (the walk is local-only) |
| 49810-49812 | buff `+0x38`, `+0x3c`, `+0x40` | the position words the aura is anchored to |
| 49813-49814 | `FUN_0049ab78(2, &DAT_008513b8 + sVar2*0x2ba, sVar2, local, 0, 0, &pos, 0, 43200000)` | draw stage **2** of `spells.eff` record `sVar2` (stride `0x2ba`, the loader's stride at 49813); the handle is stored at buff `+0x94` |
| 49818-49823 | buff `+0x23` == `'\0'` | last frame → release the handle (vtable `+0x10`), `+0x94` = 0 |

`spells.eff` shape (`tables/legacy_file.tsv`): the mounted file is
`714,752` bytes = **1,024 records of stride `0x2ba`, the division exact**.
The loader's `idx*0x2ba` indexing (49813) is therefore bounded in practice
by the buff-word range check `0 < sVar2 < 0x100` (49809), which covers the
first 256 of the 1,024 records.

The legacy draw `FUN_0049ab78` (review/B1141/ghidra/out/eqgame.exe.c:121698) reads, for its aura
call: the level-band char from `actor + 0x24c` (121734), the stage's EDD slot
word (121738), the stage's sound/attach name at `stage*0x3a0 + 0x68` compared
against `"Source"`/`"Target"` (121756-121760), and the bone attach names
`"%sL_POINT_DAG"` / `"%sSPELL_POINT_DAG"` / `"%sR_POINT_DAG"` (121773, 121776,
121787, 121790). The EDD entry's shape/texture/lifetime words are the
`review/B1141/edd_fields_from_code.tsv` table (70 PROVEN rows) — this card
does not re-derive them.

## 2. What the mounted data says (the census)

All tables regenerate from the two committed scripts (commands in §6):

| table | says |
|---|---|
| `tables/call_sites.tsv` | every call site of `0x44d0d0` (90), `0x491cff` (1: review/B1141/ghidra/out/eqgame.exe.c:122018) and the dispatcher `0x49b260` (5: review/B1141/ghidra/out/eqgame.exe.c:95153, 95672, 101960, 122270, 146701), with enclosing function and line |
| `tables/gate_reach.tsv` | 8,066 spells in `spells_us.txt`; **7,909** carry a spellsnew.eff record (their stage FX all pass the gate at review/B1141/ghidra/out/eqgame.exe.c:122018, the dispatcher's `FUN_00491cff` call); **157** have no record (their stage FX go the `0x49ab78` legacy arm, still gated at review/B1141/ghidra/out/eqgame.exe.c:122021-122024); the 41 pinned `anim` values of `tests/spell_anim_rows.h` resolve to **42 distinct measured records**; the 7 tour spells of `refactor/B1184/tables/tour.tsv` resolve to records 80, 82, 104, 121, 137 |
| `tables/gate_reach_measured.tsv` | the 42 measured records, one row each: **39 of the 42** carry ring/disc EDD entries (**148** slot lines total, the `edd_ring_disc_entries` column), and 5 have a mounted spell naming the record (80 ← 38, 93; 82 ← 54, 656; 104 ← 253; 121 ← 458; 137 ← 41) |
| `tables/auracircle_shaped.tsv` | those 148 slot lines in full: record, stage, slot, EDD index/name, shape (1=even ring, 4=disc, 9=random ring), attach, anchor, texture, lifetime, frames, fps — the read of `spellsnew.eff` × `spellsnew.edd` the client's dispatcher does at review/B1141/ghidra/out/eqgame.exe.c:122007 |
| `tables/idcheck_reach.tsv` | all **8,066** mounted spell IDs are inside `0x44d0d0`'s range (1..9999); **0** outside; 90 call sites |
| `tables/effect_column.tsv` | the P8 buff-tick class: **213** of 8,066 mounted spells carry effect value `5` in the spells column the server's buff `+0x22` char is set from (column 11 on the 209-field rows, 12 on the 211-field rows); split: **211 with** a spellsnew record, **2 without — 4728, 6096** |
| `tables/aura_candidates.tsv` | 353 rows: **all 157 legacy spells** plus **196** spellsnew spells whose text mentions "aura"; of the 196, 168 carry a ring/disc EDD entry in their own record and 28 do not |
| `tables/legacy_file.tsv` | the mounted `spells.eff`: 714,752 bytes = 1,024 records of stride `0x2ba`, division exact |

**Reachability.** The question the census answers is not "do any mounted
spells reach `0x491cff`" — all 7,909 spellsnew spells do, on their cast, and
the 42 measured records are the lane-5-measured subset (B1184 tour casts,
`tests/spell_anim_rows.h` pins), so the gate at 122018 is live for mounted
data. What the census separates instead is *which* path draws each mounted
aura: the record path for 39 measured records / 168 aura-named spells, the
legacy buff tick for the 213 effect-`5` spells' buffs (211 of which draw
their CAST FX on the record path too), and legacy-only for 4728/6096.

## 3. What willeq lacks (the gap list)

The current `spell_visual_fx.h` surface (`include/client/graphics/spell_visual_fx.h`):

| function | line | dispatch site |
|---|---|---|
| `createCastGlow` | 220 | `src/client/bridge/spell_cast_fx.cpp:31` on `SpellCastVisualStarted` |
| `createImpact` | 244 | via `createCastGlow`, `spell_cast_fx.cpp:31` |
| `createSpellComplete` | 247 | via `createCastGlow`, `spell_cast_fx.cpp:31` |
| `createLanding` | 264 | `spell_cast_fx.cpp:58` |
| `spawnMissileObject` | 295 | `spell_cast_fx.cpp:17` |
| `removeCastGlow` | 356 | the bridge's cancel arm |
| `releaseCastGlow` | 363 | the bridge's complete arm |
| `createLandingFromSlots` | 502 | `createLanding`'s spellsnew.eff path |

The B1138 tombstone (`createImpactAtPosition` and the other three): `spell_visual_fx.h:195-196` —
`createBuffAura`, `createRainEffect` and `createGroundCircle` are deleted with
their presets and config styles; `:203` — `hasBuffAura` went with
`createBuffAura`.

The gaps, against §1:

1. **The visibility gate (`0x491cff`) has no willeq counterpart.** willeq's
   dispatch (`spell_cast_fx.cpp:17,31,58`) checks the spell's gate level and
   the On-For option (`spell_fx_options.h`, B1165) — it has **no corpse check
   on the source/target actors, no per-actor filter and no option byte** like
   `[Defaults] SpellParticleCastFilter` (review/B1141/ghidra/out/eqgame.exe.c:115163-122018). A dead
   caster's FX draw in willeq; other characters' FX are never filtered.
2. **The P8 legacy buff-tick aura does not exist.** `BuffManager` stores
   `effect_type` (`src/client/spell/buff_manager.cpp:121,184,304` — the wire
   word the client's `+0x22` char is the server form of) but **nothing under
   `src/client/graphics/` reads it** (zero `effect_type` references there):
   willeq has no per-frame buff walk (review/B1141/ghidra/out/eqgame.exe.c:49746-49823), no `spells.eff`
   stage-2 draw for buff-class-`'5'` auras, no buff-anchored position
   (`+0x38/+0x3c/+0x40`) and no create/release across the `+0x23`
   `'\x01'`→`'\0'` edge.
3. **The OP_NewSpawn FX side-effect is absent.** `zone_dispatch.cpp:827`
   routes `OP_NewSpawn` to `ZoneProcessNewSpawn`, which spawns the entity —
   the client's deferred-spawn function also fires the dispatcher's stage-0 arm
   at review/B1141/ghidra/out/eqgame.exe.c:146701 (`FUN_004bfe0f`, def 146548) — the deferred-source FX on NPC
   spawn; willeq draws nothing on the spawn packet.

**Flagged for the orchestrator (not edited, per the card):**
`src/client/graphics/CLAUDE.md:53-58` — "Only three of these are reached from
the game … The projectile, buff-aura, rain and ground-circle entry points
have **no production caller**" is stale in both halves: the sentence
pre-dates the bridge's record-driven re-routing (`spell_cast_fx.cpp:17,31,58`)
and describes deleted entry points whose replacement question this card
answers (§0.4, §5).

## 4. The measurement (lane 5, observer rig)

The plan `refactor/B1209/plan_b1209.tsv` ran on lane 5 with the B1134
observer rig (the official Titanium client as the observer, char
**Oraclewatch**; the caster **Testcaster** casting, in order):

- `#castspell 4499 0` — Defensive Discipline, spellsnew.eff record 326,
  Source **even-ring** `glowflare02.tga` (`tables/auracircle_shaped.tsv`):
  the ground ring B1134 P4b already measured
  (`/mnt/projects/fable/review/B1134/measure/p4b_discs_4499_1.txt`).
  CONTROL: the record-path ring on this rig.
- `#castspell 6127 0` — Pyrilen Aura: **no spellsnew record** (legacy,
  effect `100` in the mounted column, `tables/effect_column.tsv` row 100):
  the only aura mechanism that does not go through the record path is the P8
  buff tick (§1.5). Whether the oracle draws it depends on the `+0x22` word
  the server sends — the pcap is the evidence either way.

### 4.1 Measured (run 2 — the leg's numbers)

Capture: `/mnt/scratch/kb-evidence/kb-b1209-aura-ground/aura_ground/`
(`video.mp4` = the observer's window, 14,905 frames @ 30 fps,
`video.calibration.tsv`: frame 0 = epoch 1793362687.353; `capture.pcap` =
both clients). The observer's cast times
(`tables/measurements/cast_times.tsv`, from `cast_times.py` over the
observer's stream only):

| cast | observer's wire rows (epoch) | route into the gate |
|---|---|---|
| 4499 | `OP_BeginCast` `0x3990` at 1793363127.553 (instant: `t_bc == t_act`) | the `0x3990` arm (review/B1141/ghidra/out/eqgame.exe.c:80566) → `FUN_0049b39d` → dispatcher (122270) → **0x491cff** (122018) |
| 6127 | `OP_BeginCast` `0x3990`, `OP_Action` (`0x497c`), `OP_Damage` (`0x5c60`), `OP_SpellEffect` (`0x22a5`), all at 1793363160.131 (`tables/measurements/buff_dump_6127.tsv`) | **not the gate's path**: 6127 has no spellsnew record, so the BeginCast helper returns before its dispatcher call (review/B1141/ghidra/out/eqgame.exe.c:122270); the null-record check is review/B1141/ghidra/out/eqgame.exe.c:122121-122124 (`FUN_0040cbd6` null → return). The buff lands in the entity's buff state via the received packets and the aura is drawn per-frame from that state — the mechanism willeq's deleted `createBuffAura` was the heuristic for |

Per-frame lit-pixel tables (one row per video frame, committed):
`tables/measurements/frames_4499.tsv` (397 frames) and
`tables/measurements/frames_6127.tsv` (396 frames), each with frame index,
epoch, `t_act`, `n_px`, blobs, RGB, hue, bbox, centroid, produced by
`review/B1122/scripts/fx_measure.py` on the B1134 rig crop
(`330,150,910,530`, `/mnt/projects/fable/review/B1134/geom/rig.json`).
LIT = `n_px > 1,000` (the pre-cast baseline max is 1,036 for 4499 and 293
for 6127; the threshold separates the effect from the cast animation):

| cast | onset (frame, `t_act`, px) | fully lit (frame, `t_act`, px) | peak (frame, `t_act`, px, hue) | last LIT (frame, `t_act`, px) | shape |
|---|---|---|---|---|---|
| 4499 (record 326, "Discipline - Blue01/02" even rings, `glowflare02.tga`) | 13206, +0.000, 1,063 | 13220, +0.467, 9,883 | 13398, **+6.400, 26,131, hue 233 (blue)** | 13566, +12.000, 5,322 (still going) | bbox bottom at y=529 (the floor): the **ground ring** |
| 6127 (legacy, "is covered in an aura of flame", no record) | 14196, +0.422, 4,132 | 14198, +0.489, 6,163 | 14305, **+4.055, 7,735, hue 33 (orange)** | 14543, +11.989, 3,700 (still going) | bbox top at y=95..110 (the body): the **aura** |

Findings, per row:

- **4499 reproduces the B1134 P4b band** (band data:
  `/mnt/projects/fable/review/B1134/measure/p4b_discs_4499_1.txt`; the
  description is `review/B1134/README.md:96`: "a ring/curtain of blue-white
  flames on the ground round the feet", lit to Act+8.04-8.28): the measured
  peak is blue (hue 233), floor-level (bbox to y=529), and the effect is
  still LIT at the window's end (+12.0 s)
  — the 2.0-s `glowflare02.tga` ring entries of record 326
  (`tables/auracircle_shaped.tsv`) re-fire on the sustained discipline buff,
  which is why the ring does not decay inside the window.
- **6127 draws the legacy aura**: an orange (hue 33) body aura, onset
  +0.422 s, sustained for the whole 12 s window at 3.5-7.7k px. 6127 has **no
  spellsnew record** (`tables/aura_candidates.tsv`: route `legacy`,
  `has_ring_disc_edd` N), so the `0x3990` BeginCast arm's helper returns before
  its dispatcher call (review/B1141/ghidra/out/eqgame.exe.c:122117-122124) and
  **nothing on the gate's path draws it**.
  The buff lands in the entity's buff state through the received
  `OP_Action` (`0x497c`) / `OP_SpellEffect` (`0x22a5`)
  (`tables/measurements/buff_dump_6127.tsv`), and the oracle draws the
  sustained aura per-frame from that state. The local-player variant of that
  per-frame mechanism is the P8 buff tick (`FUN_004368b3`, review/B1141/ghidra/out/eqgame.exe.c:49746; reads
  buff `+0x22=='5'`, review/B1141/ghidra/out/eqgame.exe.c:49805; draws via `FUN_0049ab78`, review/B1141/ghidra/out/eqgame.exe.c:49813-49814) —
  the mechanism willeq's deleted `createBuffAura`
  (`spell_visual_fx.h:195`) was the heuristic for. willeq handles the
  `0x497c` packet for **state** (`zone_dispatch.cpp:864` →
  `CommandsComponent::ZoneProcessAction`, `src/client/commands/combat_actions.cpp:132`)
  but has no per-frame buff-aura draw, so a 6127-type aura is invisible in
  willeq even though the buff is applied.
- The census consequence (§0.3): the only mounted spells whose aura can
  **only** come from the legacy path are **4728 (Wave of Rage) and 6096
  (Rage of the Zun'Muram)** (`tables/effect_column.tsv` rows
  `effect5_split_*`) — 6127 (effect column `100`, `tables/effect_column.tsv`
  row 100) is outside the P8 class and draws as a legacy cast effect, which
  is exactly what the leg measured.

### 4.1 Run 1 (the rig failure, kept as evidence)

The first run (`/mnt/scratch/kb-evidence/kb-b1209-aura-ground/aura_ground_run1_darkshadow/`)
omitted `# obs-ini-set=Defaults.LastCharSel=1`; the observer client logged
its account's slot-0 character **Darkshadow [1 Enchanter]** instead of
Oraclewatch, zoned into **Nobles' Causeway** (still `14_obs_cam_settle.png`:
"You have entered Nobles' Causeway"), and the two casts — visible on the
caster stream at `capture.pcap` t=1793362013.24 (`OP_Action` spell 4499,
flag 0) and t=1793362045.41 (spell 6127, flag 0) — reached the observer's
port **0 times** (0 `OP_BeginCast`/`OP_Action` on port 42830 of 2,175 rows).
That run is the control for "the packets are not there" vs "the observer is
in another zone": the cast packets were generated and the observer was
simply not in the zone to receive them.

## 5. The proposed FIX legs (one line per mechanism)

- **B1209-G1 — the visibility gate.** Implement the `0x491cff` predicate
  (option byte + source/target death char + player-list/name checks,
  review/B1141/ghidra/out/eqgame.exe.c:115163-115330; the dispatcher's call is
  at 122018) at willeq's FX dispatch
  (`spell_cast_fx.cpp`), with the `[Defaults] SpellParticleCastFilter`-equivalent
  option (default `0x18` = draw everything; review/B1141/ghidra/out/eqgame.exe.c:132638).
- **B1209-G2 — the P8 legacy buff-tick aura.** A per-frame buff walk in the
  game thread drawing the `spells.eff` stage-2 record for buff-class-`'5'`
  auras (create on `+0x23=='\x01'`, release on `'\0'`,
  review/B1141/ghidra/out/eqgame.exe.c:49746-49823), anchored at the buff's `+0x38/+0x3c/+0x40` position words.
- **B1209-G3 — the spawn FX side-effect.** Fire the dispatcher's stage-0 arm
  (review/B1141/ghidra/out/eqgame.exe.c:146701, stage 0, source = target = the spawned entity) for entities
  whose spawn state carries a deferred spell (the `0x3b3/0x3b4/0x3b5` words,
  ID validated at 146694, record via `FUN_0040cbd6`) — B1189 P6: wire/NPC-spawn
  driven, not countable from `spells_us.txt`.
- **No card for the ring/disc visuals themselves**: the record-path emitters
  (39 records, 148 slot lines) are already implemented and measured —
  B1189 §2.1, B1134, and §4 of this card.
- B1189 §3.2's **B1189-F12 as written should be closed as misidentified**;
  its real content is G1+G2 above.

## 6. Verification

- `refactor/B1209/scripts/census.py` — regenerates `auracircle_shaped.tsv`,
  `gate_reach_measured.tsv`, `aura_candidates.tsv`, `effect_column.tsv`,
  `legacy_file.tsv` from `/mnt/projects/EverQuestP1999`:
  `python3 refactor/B1209/scripts/census.py`
- `refactor/B1209/scripts/call_sites.py` — regenerates `call_sites.tsv` from
  the decompile: `python3 refactor/B1209/scripts/call_sites.py`
- `refactor/B1209/scripts/cast_times.py` — the observer-stream cast times and
  buff-word dump for a capture:
  `python3 refactor/B1209/scripts/cast_times.py <capture_dir> 4499 6127 --observer Oraclewatch`
- `refactor/B1209/scripts/fx_summary.py` — onset/peak/end frames from
  `review/B1122/scripts/fx_measure.py`'s `frames.tsv`:
  `python3 refactor/B1209/scripts/fx_summary.py <frames.tsv>`
- the lane-5 session: `ORACLE_OWNER=kb-b1209-aura-ground bash scripts/eqemu/oracle/oracle_session.sh 5 refactor/B1209/plan_b1209.tsv <out_dir>`
  (the driver's plan, its pre/post snapshots and the restore live in
  `<out_dir>/`).
- Cite check (worker-side, `.c`-aware): `python3 -I scripts/refactor/cite_check.py
  refactor/B1209/README.md --tree . --extra /mnt/projects/fable
  --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis
  --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked 96 cites, 0 problems, 0 address warnings`. The same command over
  the plan → `checked 3 cites, 0 problems, 0 address warnings`:
  `python3 -I scripts/refactor/cite_check.py refactor/B1209/plan_b1209.tsv --tree . --extra /mnt/projects/fable
  --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis
  --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`.
- no C++ change: the MR touches `refactor/B1209/**` only (README, scripts,
  tables, plan); the hygiene label is green on a docs+scripts-only diff.
