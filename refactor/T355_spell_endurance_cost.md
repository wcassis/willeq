# T355 — `SpellData::endurance_cost` is never assigned, and the server DOES enforce it (B062)

Class B, slot2. T353 found the skill costs were fiction; the spell one is
real: eqemu charges spell endurance (`/mnt/projects/akk-stack/code/zone/effects.cpp:900`)
and refuses a cast without it. fable's field is always 0.

1. Measure the source of truth: Titanium's `spells_us.txt` has a
   per-spell endurance-cost column (find it by name — the eqemu
   `spells_new` importer `/mnt/projects/akk-stack/code/` names the column
   mapping; cite it). Confirm with three spells whose cost is non-zero in
   eqemu's `spells_new` table (`SELECT id, name, EndurCost FROM spells_new
   WHERE EndurCost > 0 LIMIT 3` — read-only DB access is sanctioned) that
   the text file carries the same number at the same column.
2. Load it: the spell-file reader (`src/client/spell/` — the T070/T073
   `spells_eff` reader and the `spells_us.txt` parser) fills
   `endurance_cost` from that column; `static_assert`/test the column
   index against the three measured spells.
3. Consumer: does fable refuse a cast locally on endurance
   (`SpellManager::validateCast` — the `mana_cost` check is the model)?
   Match the oracle: the official client refuses locally with the
   "insufficient endurance" text before sending (it does for mana — read
   `spell_cast_checks.cpp` for the mana precedent and mirror it), so add
   the endurance check beside the mana one; the server's refusal stays
   as the backstop. Test: a discipline with cost > current endurance is
   refused locally with the same message shape; pre-fix failure pasted.

Territory: the spell-file parser, `include/client/spell/spell_data.h`,
`src/client/spell/spell_cast_checks.cpp`, tests, `refactor/BOARD.md`. No
graphics files.

STOP: the column cannot be identified against eqemu's mapping (report,
do not guess).

---

## Review

### 1. The column: MEASURED, not inferred (plan step 1)

`SpellData::endurance_cost` had **zero** assigning sites; the source column is
spells_us.txt **field 166**.

The mapping is not read off a name — it is read off eqemu's own importer.
`/mnt/projects/akk-stack/code/client_files/import/main.cpp`
(`ImportSpells()`, lines 130-180) opens `spells_us.txt`, splits the caret line,
and does `INSERT INTO spells_new VALUES(...)` writing **split[i] into column i**
with no name mapping anywhere. So the ordinal of a column in `spells_new` **is**
the field index in the text file. In
`/mnt/projects/akk-stack/code/common/repositories/base/base_spells_new_repository.h`
the `Columns()` list (237 entries) has `EndurCost` at index **166** — and the
same file reads it back as `row[166]` (lines 1245, 2309, 2570). The corroborating
control: `mana` is at index **19** in the same list, which is exactly the index
fable's builder already used for `ManaCost` (`safeInt(19)`).

Three-spell cross-check against a live peq `spells_new` (mariadb container
brought up read-only under the `.eqemu-lock`, taken atomically with
`set -o noclobber`, released and the container `down`ed after):

```
SELECT id, name, EndurCost, mana FROM spells_new WHERE EndurCost > 0 ORDER BY id LIMIT 5;
id    name                     EndurCost  mana
4520  Unholy Aura Discipline   900        0
4608  Provoke                   85        0
4614  Phantom Zephyr           125        0
4659  Sneak Attack             325        0
4681  Bellow                   175        0
```

and field 166 of `/mnt/projects/EverQuestP1999/spells_us.txt` for the same ids:

```
id=4520 name=Unholy Aura Discipline field166=900 field19(mana)=0
id=4608 name=Provoke               field166=85  field19(mana)=0
id=4614 name=Phantom Zephyr        field166=125 field19(mana)=0
id=4659 name=Sneak Attack          field166=325 field19(mana)=0
id=4681 name=Bellow                field166=175 field19(mana)=0
```

Exact match on all five. The STOP did not fire.

### 2. The pipeline (plan step 2)

The column was NOT in willeq.db's schema, so the whole existing pipeline needed
one column each — builder -> DB -> `SpellInfo` -> `SpellData`:

- `include/client/spell/spell_constants.h`: `SpellField::EnduranceCost = 166`,
  with the eqemu provenance in the comment.
- `tools/willeq_db_builder.cpp`: `spells.endurance_cost INTEGER` **appended**
  to the schema, the INSERT column list and its placeholder run, and one
  `sqlite3_bind_int(..., safeInt(EQ::SpellField::EnduranceCost))`. Stored
  verbatim, like every other spell column.
- `include/client/willeq_database.h`: `SpellInfo::enduranceCost`, and
  `kWilleqDbExpectedVersion` **5 -> 6** (T056 mechanism).
- `src/client/willeq_database.cpp`: `endurance_cost` appended as the LAST
  column of BOTH spell SELECTs (`getSpell`, `getSpellByName`) so no existing
  column index moved, and read in the shared positional-tail helper — one
  read site for both queries, so they cannot drift.
- `src/client/spell/spell_database.cpp`: `spell.endurance_cost =
  info.enduranceCost;` beside the `mana_cost` line that was already there.

**263 of 8066 spells** carry a non-zero endurance cost in the rebuilt db.

### 3. The local refusal (plan step 3), mirroring mana exactly

`SpellManager::validateCast()` had a mana check and nothing else resource-shaped.
Mirrored one-for-one:

- `CastResult::NotEnoughEndurance = 17` (`spell_constants.h`), beside
  `NotEnoughMana = 3`.
- `SpellManager::checkEndurance()` (`spell_manager.cpp`) is `checkMana()`
  line-for-line, including the `<= 0` early-out (0 for every pre-Velious spell;
  a negative would be endurance-returning, never a refusal), reading
  `EverQuest::GetCurrentEndurance()` where mana reads `GetCurrentMana()`.
- `validateCast()` calls it immediately after the mana check, so mana still wins
  when a spell is short on both (pinned by a test).
- `cast_refusal.h`: `STR_INSUFFICIENT_ENDURANCE = 5805` ->
  `castRefusalStringId(NotEnoughEndurance)`. eqstr 5805 is
  **"Insufficient Endurance to activate this ability!"** — verified in
  `/mnt/projects/EverQuestP1999/eqstr_us.txt:2613` and present in the rebuilt
  willeq.db `eqstrings`. That is the official client's counterpart of 199
  "Insufficient Mana to cast this spell!". Adding one case to the existing
  table means all THREE refusal sites (action layer, CastSpellIntent, hotbar
  Spell arm) emit it with no further wiring — the T244/T253 design paying off.
- `src/client/core/eq_strings.cpp`: `case 5805` added to the no-string-db
  fallback table, so the reason never degrades to `[Unknown message #5805]`.

The server stays the backstop: eqemu refuses at
`/mnt/projects/akk-stack/code/zone/effects.cpp:900`
(`if(GetEndurance() < spell.endurance_cost)`).

### 4. Pre-fix failures (RED, measured)

RED was taken by reverting exactly four things in the working tree (version bump,
the two DB read sites, the `SpellData` assignment, and the `validateCast` branch)
with the new tests and the PRE-T355 `data/willeq.db` in place — i.e. the real
pre-fix state — then rebuilding. No `git stash` was used (patch-free scripted
swap, per the protocol).

```
[ RUN      ] SpellDatabaseTest.EnduranceCostRoundTripsForMeasuredDisciplines
tests/test_spell_database.cpp:686: Failure
Expected equality of these values:
  spell->endurance_cost
    Which is: 0
  r.endurance
    Which is: 900
spell 4520
tests/test_spell_database.cpp:686: Failure
  spell->endurance_cost  Which is: 0
  r.endurance            Which is: 85
spell 4608
tests/test_spell_database.cpp:686: Failure
  spell->endurance_cost  Which is: 0
  r.endurance            Which is: 125
spell 4614
[  FAILED  ] SpellDatabaseTest.EnduranceCostRoundTripsForMeasuredDisciplines

[ RUN      ] SpellManagerValidate.NotEnoughEnduranceBlocksCast
tests/test_spell_manager.cpp:559: Failure
Expected equality of these values:
  mgr->validateCast(spell, 0)
    Which is: 1-byte object <00>          <-- CastResult::Success
  CastResult::NotEnoughEndurance
    Which is: 1-byte object <11>
[  FAILED  ] SpellManagerValidate.NotEnoughEnduranceBlocksCast
```

The two refusal-string tests have no runnable RED: pre-T355 the symbol
`CastResult::NotEnoughEndurance` does not exist, so their pre-fix state is a
compile error, not a failing assertion.

New tests (7): `EnduranceCostRoundTripsForMeasuredDisciplines` (the three
measured spells, ids/names/values/mana=0), `EnduranceCostIsZeroForAClassicSpell`
(Cloak/5 — the control, so the new column cannot turn every spell into a
discipline), `NotEnoughEnduranceBlocksCast`,
`ZeroOrNegativeEnduranceCostAlwaysPassesEnduranceCheck`, `EnoughEndurancePasses`,
`ManaIsCheckedBeforeEndurance`, plus the 5805 rows added to
`CastRefusalStringIds_MatchEqstr` and `CastRefusalStrings_ResolveWithoutStringDb`.

### 5. `data/willeq.db` regenerated

```
build/bin/willeq_db_builder --eq-path /mnt/projects/EverQuestP1999 \
    --data-path <slot>/data --config-path <slot>/config -o <slot>/data/willeq.db
```

- Size **3,854,336 -> 3,866,624 bytes** (+12,288). Mode forced back to 664
  (the builder writes 755 — the T306 stray-mode trap).
- `metadata.version` **5 -> 6**; `total_entries` unchanged at 32889.
- **Regeneration proof** (sqlite3 dump + compare, old vs new): 13 tables both
  sides, identical set; `spells` **98 -> 99 columns**, added `['endurance_cost']`,
  **removed `[]`**; **8066 -> 8066 rows**; every table's shared columns
  byte-identical across ALL rows except `metadata`, whose only differences are
  `version` (5->6) and `build_date`. Nothing else moved.
- Spot rows out of the new db: 4520 `('Unholy Aura Discipline', 900, mana 0)`,
  4608 `('Provoke', 85, 0)`, 4614 `('Phantom Zephyr', 125, 0)`,
  5 `('Cloak', 0, mana 50)`. 263 rows with `endurance_cost > 0`.

### 6. Verification

| Step | Result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** (first invocation hit the 600 s ceiling, exit 143; `test_eq_action_handler_hotbar_command` had been DELETED by the kill — absence, not corruption. ELF-magic scan over `build/bin`: 0 bad of 249, then 0 bad of 268; `ar t libwilleq_core.a` 321 members, `libEtcLib.a` 15. Re-invocation exit 0, all 268 valid ELF.) |
| `ctest --test-dir build -j6 --output-on-failure` | **3859 tests, 1 failed**: `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA host exception). All 7 new tests pass; `WilleqDatabaseVersionTest` Matching/Older/Newer/MissingMetadata all pass at the new version 6. |
| `bash scripts/refactor/check_file_sizes.sh --all` | **880 files: 880 ok, 0 warn, 0 FAIL** |
| goldens | **untouched** (`git status --porcelain tests/goldens/` empty) |
| `DocCitations` | Caught the first draft of the B062 close-out: a citation of
`common/repositories/base/base_spells_new_repository.h` written RELATIVE, which the
checker resolves in-repo and reported as `file does not exist`. Rewritten absolute
(`/mnt/projects/akk-stack/code/...`) so it lands as a cross-project SKIP like the
other eqemu citations on the board. **Green on the final run.** |

Final full run: `3859 tests, 1 failed` — `AudioManagerTest.PreloadCommonSounds`
only.

Also updated: the comment on `SpellData::endurance_cost` itself
(`include/client/spell/spell_data.h`) now names field 166, the loader and the
local refusal, so the next reader does not have to re-derive any of it.

### 7. DEVIATION: the gate could NOT be skipped — T304's trap fired

The dispatch narrowed verification to build/ctest/file-sizes with gles2, the
visual gate and ARM skipped as "no graphics files". **`git diff --name-only`
confirms no graphics file changed** (13 files: 4 headers, 4 client sources,
3 tests, the builder, and `data/willeq.db`). But the first full `ctest` run
FAILED the visual gate:

```
=== VISUAL GATE: FAIL ===
  - memory-coverage qeynos2: characterModelsBytes mismatch (expected 25737076, got 24701259)
  - memory-coverage gfaydark: characterModelsBytes mismatch (expected 25368606, got 24332789)
```

This is **exactly** the trap `refactor/T304_wills_three_rulings.md:217-225`
recorded, down to the delta: **-1,035,817 bytes, identically in every zone**.
The gate runs `build-gles2/bin/gles2_zone_benchmark`, a DIFFERENT build tree;
after `kWilleqDbExpectedVersion` moves and `willeq.db` is rebuilt, that stale
binary silently refuses the newer db, loads **no character models**, and the
gate reads as a memory regression that is not one.

So step 3 was NOT skippable, and neither was the gate. Both were run:
- `bash scripts/build-gles2-x86.sh` — **EXIT 0** (also hit the 600 s ceiling
  once, exit 143, which DELETED `build-gles2/bin/willeq`; ELF scan on the
  surviving binaries 0 bad, re-invocation exit 0, `willeq` and
  `gles2_zone_benchmark` both relinked and valid).
- `ctest -R '^visual_gate$'` — **PASS, 20.84 s, goldens untouched.**

For the same staleness reason `bash scripts/build-arm-noble.sh` was also run
(**EXIT 0** after one 143 + re-invocation; 18 ARM binaries, 0 bad ELF,
`libwilleq_core.a` 316 members) so no future Pi run inherits a version-5 binary
against a version-6 db.

**Generalisation worth boarding: any db schema-version bump invalidates EVERY
build tree, not just `build`.** T304 recorded `build-gles2`; T355 confirms
`build-arm-noble` is in the same class. A unit that bumps
`kWilleqDbExpectedVersion` must rebuild all three regardless of what its diff
touches.

### 8. eqemu server

`.eqemu-lock` taken atomically (`set -o noclobber`, EXIT 0), only the `mariadb`
service started, one read-only `SELECT` against `peq.spells_new`,
`docker compose down` (EXIT 0), lock deleted. No character was moved; no server
code or config touched.
