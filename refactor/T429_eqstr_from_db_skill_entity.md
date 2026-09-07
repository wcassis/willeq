# T429 — the display composers read eqstr from willeq.db (B390); the skill and entity halves of B298

Class B ×2, slot3, no server. Read B390 (both composers hardcode format
strings the client ships in `willeq.db`'s `eqstrings` table, reachable via
`EQT::StringDatabase::getString()`), B298's remaining halves (`SkillData`
and `EntityManager` fields parsed and never shown), T421's and T425's
§Reviews (the composers, `spell_display_text.cpp`, `item_display_*`),
`/mnt/projects/EverQuestP1999/eqstr_us.txt` for the skill and entity
(consider/target) vocabulary, and the skills window (T363) / target
panel.

## Leg A — B390

Both composers fetch each format string by id through `getString()`
(with the id in the code and a one-line comment naming the string, so
the intent stays readable) and substitute `%1..%n` through one shared
formatter (find the existing eqstr substitution in `chat_component.cpp`
/ `eq_strings.cpp` — reuse, never a second). A test pins that every id
the composers use exists in the shipped db (pre-fix: the strings were
literals — the test fails to find the ids referenced). Strike B390.

## Leg B — B298 skill + entity halves

Table: each `SkillData` / entity field the census names → the eqstr line
that shows it (skill: the `/skills` window rows — T363 draws name +
value; what else does the official window show? `EQUI_SkillsWindow.xml`
quote) and for entity fields the consider/target text (`/con` lines are
eqstr; the target panel's HP/level/class). Draw what the oracle shows,
in its place, through Leg A's fetch; fields with no line stay hidden
(say which). Tests pre-fix failing. Strike the halves; B298 fully closed
if nothing remains, else annotate.

## Proof

Build (background), `ctest -j6` (no expected failures), pre-fix/post-fix,
G05 in place if a pass changes, visual gate standalone + ARM, file sizes;
`ui_panel_capture` for the touched panels into `review/T429/`.

STOP: none expected.

---

## Review

Branch `task/T429-eqstr-db-skill-entity`, base `466eda2f` (verified at
checkout). **Leg A `3d59d342`**, Leg B is this commit.

### Leg A — B390, struck

**How the composers fetch and substitute now.** Both take a
`const EQT::StringDatabase&` as their first argument and call
`eqt::EqStrLine(strings, id, args)` for every line
(`include/client/display_eqstr.h`, `src/client/core/display_eqstr.cpp`).
`EqStrLine()` is a fetch-and-call wrapper over
`EQT::StringDatabase::formatString()` — **the client's one `%n` substitution
engine**, `src/client/string_database.cpp`, the same one `chat_component.cpp`,
`merchant.cpp` and `player_component.cpp` reach through `ctx_.strings`. No
second formatter was written. Its only added behaviour is that a table miss
(`formatString()` returns `""`) means **draw no line**, never a hardcoded copy
of the format — a fallback copy is exactly the defect B390 names.

The production call sites take `ctx_.strings.Db()`
(`bridge_component_intents.cpp`, both display intents). `ui_panel_capture`
opens `data/willeq.db` itself, through `ResolveDataPath()`, and says so on
stderr if it cannot.

**The surprise, and the one thing the shared engine cannot do.** eqstr mixes
two placeholder conventions. Nearly every row is positional `%1..%n`, but the
two item cast-time qualifiers are **C printf formats**:

```
3030  (Casting Time: %2.1f)
3033  (Must Equip.  Casting Time: %2.1f)
```

`%2.1f` is one float conversion (width 2, precision 1), not argument two
followed by the literal `".1f"`. Fed to the positional engine, 3030 renders
`(Casting Time: .1f)` — which the test pins, so a future table change is
visible. `EqStrPrintfFloat()` handles that one shape in one place and is
documented as such; it leaves a positional row untouched.

**Two smaller findings, both recorded in the code rather than boarded.**
(a) eqstr 3048 `LORE: `, 3049 `Race: ` and 3064 `Class: ` carry **no
placeholder at all** — they are labels the official client concatenates its own
list onto, so those three are a fetch-then-append, and a missing label now
suppresses the whole line rather than printing a bare list. (b) The word tables
`ItemInstance::getFlagsString()`, `buildSlotsString()`, `buildClassString()`
and `buildRaceString()` are *not* format strings and are deliberately outside
B390's fetch; the composer says so.

**Test, pre/post.** `tests/test_display_text_from_db.cpp`, 9 cases:

| case | pre-fix | post-fix |
|---|---|---|
| `ItemLinesFollowTheStringTableNotTheSource` | FAIL | PASS |
| `SpellLinesFollowTheStringTableNotTheSource` | FAIL | PASS |
| `AnEmptyTableDrawsNoItemLines` | FAIL | PASS |
| `AnEmptyTableDrawsNoSpellLines` | FAIL | PASS |
| `EveryItemDisplayIdExistsInTheShippedDb` (52 ids) | PASS | PASS |
| `EverySpellDisplayIdExistsInTheShippedDb` (9 ids) | PASS | PASS |
| `TheShippedCastTimeRowsAreCPrintfFormats` | PASS | PASS |
| `TheClickEffectQualifierRendersItsCastTime` | PASS | PASS |
| `PrintfFloatLeavesAPositionalRowAlone` | PASS | PASS |

Pre-fix run = **5 passed, 4 failed**; post-fix = **9 passed**.

**Deviation, flagged.** The pre-fix state could not be reproduced by reverting
the composers wholesale, because the fix changes their SIGNATURE and the test
cannot compile against the old one. The pre-fix run was staged by putting the
literals back for the four lines the reword/empty-table cases target
(`AC: {}` / `DMG: {}` in the item composer, `Mana Cost: {}` / `Cast Time: {}`
in the spell composer) while keeping the new signature — i.e. exactly the
pre-fix code for those lines. The four failures above are that run; the
restore was by file copy, never `git checkout --` and never a stash.

### Leg B — B298's skill and entity halves, struck; B298 CLOSED

**Neither half was a missing display.** The five fields the census names were
measured against the oracle's own XML and against `eqstr_us.txt`, and the
answer in every case is "no line". They are three different defects wearing
B298's clothes, each now boarded separately.

| field | eqstr line | element | drawn? |
|---|---|---|---|
| `SkillData::animation_code` | none (a skeletal code, `"t02"`) | none in `EQUI_SkillsWindow.xml` | **no** — and it is a DUPLICATE STORE: the one consumer, `SkillManager` at `src/client/skill/skill_manager.cpp:719`, calls `getSkillAnimationCode(skill_id)` directly. **B415** |
| `SkillData::usable_while_sitting` | none | none | **no** — a GATE predicate for a while-sitting check that does not exist anywhere under `src/client/skill/`. **B416** |
| `Entity::anon_status` | 5023/5024 (`[ANONYMOUS]` is inside the format) | none — those formats belong to /who, which parses its OWN wire entries (`who_response.h`) and never reads `Entity` | **no**. **B418** |
| `Entity::guildId` | the /who guild column | none reachable — fable has NO guild-id→name source: `WorldProcessGuildsList` is one `LOG_DEBUG` that parses nothing, and `data/willeq.db` has no guild table | **no**, same shape as B391. **B418** |
| `Entity::deltaHeading` | none | none | **no** — a movement field, the sibling of `deltaX/Y/Z`. **B418** |

**The skills window question, answered from the XML.**
`EQUI_SkillsWindow.xml`'s `SKLW_SkillList` declares **three** columns —

```xml
<Columns><Width>130</Width><Heading>Skill Name</Heading></Columns>
<Columns><Width>80</Width><Heading>Rank</Heading></Columns>
<Columns><Width>45</Width><Heading>Value</Heading></Columns>
```

— and T363 draws two of them (name, and a combined `value/max`). **The missing
one is `Rank`, and its contents are not derivable from anything shipped.** No
eqstr row anywhere is a skill-rank word (`grep -i rank eqstr_us.txt` returns
four rows, all guild/AA text), `EQUI_TrainWindow.xml` declares the same
`Skill Name | Rank | Value` triple with `Rank` 95px of text beside a 50px
numeric `Value`, so `Rank` is a WORD the official executable computes, and no
`Rank` mapping exists in eqemu either. Drawing a column heading over an empty
column would be worse than the present two, so the window was left alone and
the gap is stated here rather than guessed at. (Not boarded separately: it is
the same "the oracle's executable holds the table" shape as B417.)

**The target panel, quoted.** `EQUI_TargetWindow.xml` holds exactly three
pieces plus the box art: `Target_HP` (a `Gauge`, `EQType 6`), `Target_HPLabel`
(a `Label`, `EQType 29` — the name) and `Target_HPPercLabel` (the literal
`%`). **There is no level, no class, no guild and no anon element**, which is
what rules out three of the five fields above. fable's `TargetInfoData` already
carries a `level` it draws — that is fable's own addition, and it is left
alone.

**What Leg B DID draw: the `/con` line, through Leg A's fetch.**
`eqt::commands::BuildConsiderText()`
(`include/client/commands/consider_text.h`, `src/client/commands/consider_text.cpp`)
composes it from **eqstr 12239 `%1 %2 -- %3`** with the faction phrase from
**12212..12220**. `ZoneProcessConsider` now calls it.

**That found a live defect of exactly the kind B390 predicted.** The old code
carried nine faction phrases as literals; eight were faithful, and

> faction 8 (`FACTION_THREATENINGLY`,
> `/mnt/projects/akk-stack/code/common/faction.h:34`) printed **"threatens
> you"** — a phrase that appears in **no row** of `eqstr_us.txt`. The oracle's
> line is **12218 "glares at you threateningly"**.

A second trap the fetch exposed: **the string table's order is not the
server's faction order.** Faction 6/7/8/9 map to 12220/12219/12218/12217, not
to 12217..12220. The map is pinned field by field.

**Flagged as NOT the oracle's (T419 ORACLE_ROW_H rule):** the difficulty tail
that fills `%3`. The table has sixteen candidate difficulty lines
(12223..12238, several near-duplicates of each other) and carries **no
con-colour → line mapping**; that table is in the official client's
executable. Choosing six of sixteen would be inference, so fable keeps its own
`cons <colour>` tail and says so in the header. **B417**, and it is measurable
(we control the server).

**Test, pre/post.** `tests/test_consider_text.cpp`, 6 cases:

| case | pre-fix | post-fix |
|---|---|---|
| `TheLineIsTheOraclesOwnFormat` | PASS | PASS |
| `FactionEightIsTheOraclesThreateningPhrase` | FAIL | PASS |
| `EveryFactionValueMapsToItsOraclePhrase` | FAIL | PASS |
| `AnUnknownFactionValueInventsNothing` | FAIL | PASS |
| `AMissingLineFormatDrawsNothing` | FAIL | PASS |
| `EveryConsiderIdExistsInTheShippedDb` | PASS | PASS |

Pre-fix (the literal phrase table put back verbatim inside
`BuildConsiderText`, faction 8 included) = **2 passed, 4 failed**; post-fix =
**6 passed**.

### Verification bar

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config) | **exit 0** |
| `ctest --test-dir build -j6` | **4426 / 4426, 0 failed, exit 0** (no expected failures; `FileSizeCeiling` and `PackReproducible` both green) |
| `scripts/build-gles2-x86.sh` | exit 0 |
| `scripts/ci/visual_gate.sh` (standalone) | **PASS**, goldens untouched (`git status` shows no golden modified) |
| `scripts/build-arm-noble.sh` | exit 0 |
| `scripts/refactor/check_file_sizes.sh fable` | 9 files checked, **9 ok, 0 warn, 0 fail** (largest touched: `bridge_component_intents.cpp` 963) |
| `ui_panel_capture` | 19 PNGs into `/mnt/projects/fable/review/T429/panels/` |

Media: `/mnt/projects/fable/review/T429/` with a README; the two that matter
are `panels/item_display_800x600.png` and `panels/spell_display_800x600.png`.

### G05

No golden pass changed: neither leg adds or removes a statement in a
golden-pinned TU's ordered list (`bridge_component_intents` gains no new
call — the two composer calls are the same two, with an extra argument). The
`bridge_component_intents` golden is green in the run above.

### Deviations and things that surprised this unit

1. **eqstr is not one format language.** Rows 3030/3033 are C printf. This is
   the reason `EqStrPrintfFloat()` exists and the reason the plan's "one
   formatter" rule needed a written exception rather than a silent one.
2. **`fable` moved during the unit** — it is at `ced5ff7e` ("Will's
   directions 2026-09-07"), not the `466eda2f` this branch is based on. The
   branch is correct per the dispatch; flagged so the merge is not surprised.
   `refactor/BOARD.md` differs between the two, so this branch's board edits
   are against `466eda2f`'s copy and will need a normal merge.
3. **The `/con` defect was found by the fix, not by the census.** B390's own
   text predicted "a transcription typo is invisible"; there was one, in a
   third hardcoder the entry did not name.
4. **`StringDatabase` is neither copyable nor movable** (an `atomic` query
   counter), so the test helper fills one in place rather than returning it.
