# T384 — `target_anim` and `travel_type` reach willeq.db (B163)

Class B, slot3, no server. Read the B163 board text, T380's §Review
(`scripts/refactor/t380_spell_visual_census.py`), and T355's §Review (the
last schema bump: how the builder appends a column, how the loader reads
it as the LAST column so no index moves, the `kWilleqDbExpectedVersion`
bump, the byte-identical-except-metadata proof, and the fact that a bump
invalidates EVERY build tree — rebuild `build`, `build-gles2`,
`build-arm-noble` before any gate).

1. Bind `SpellField::TargetAnim` (121) and `TravelType` (122) in
   `tools/willeq_db_builder.cpp` `loadSpells()`; append `target_anim`,
   `travel_type` to the `spells` schema; `SpellData` gains both; the
   loader reads them as the last two columns; version 6 → 7.
2. Rebuild `data/willeq.db`; prove: 13 tables both sides, `spells` 99 → 101
   columns, 8,066 rows, every shared column byte-identical except
   `metadata`; `target_anim` non-zero for 3,608 rows, `travel_type == 3`
   for exactly 85 (T380's census numbers — re-run the census script).
3. Carry `travelType` on `SpellVisualVocabulary` (T381 left the slot;
   read its Review) — 8 bytes stays 8 if it fits; say the size.
4. Tests, pre-fix failing: round-trip of three measured spells (Frost
   Bolt 54 travel_type 3; pick two others from the census with distinct
   target_anim values); the vocabulary carries travelType.
5. Strike B163. Do NOT wire projectiles (B165's decision comes after
   B162's oracle) — this unit only makes the data available.

## Proof

Build (all three trees), `ctest -j6`, the tests with pre-fix/post-fix
runs, visual gate standalone (the db version bump — expect the
`characterModelsBytes mismatch` signature if a tree is stale), ARM, file
sizes, `willeq.db` mode 664.

STOP: field 121/122 do not match eqemu's `spells_new` column names
`TargetAnim` / `TravelType` at those indices (verify against
`base_spells_new_repository.h` as T355 did — quote the index).

## Review

Executor session 2026-09-05, slot3, branch `task/T384-spell-columns-db7`,
base `3bea8022`. No server: the field-index check was done against eqemu's
repository header ON DISK (read-only, no lock, no container), which is
stronger evidence than a live `spells_new` query anyway — see §1.

### 1. The STOP did not fire — indices MEASURED against eqemu's own importer

T355 established the rule: `spells_new` is a **positional** import of
`spells_us.txt` (`ImportSpells()` writes `split[i]` into column `i` with no
name mapping), so the ordinal of a column in eqemu's `Columns()` list **is**
the field index in the text file. Parsing that list out of
`/mnt/projects/akk-stack/code/common/repositories/base/base_spells_new_repository.h`
(237 entries) gives:

| index | eqemu column | fable `SpellField` |
|---|---|---|
| 19  | `mana`        | `ManaCost` (the control T355 used) |
| **120** | **`CastingAnim`** | `CastAnim` — **already bound by the builder** |
| **121** | **`TargetAnim`**  | `TargetAnim` |
| **122** | **`TravelType`**  | `TravelType` |
| 123 | `SpellAffectIndex` | `SpellAffectIndex` |
| 166 | `EndurCost`   | `EnduranceCost` (T355) |

The decisive control is index 120: the builder was **already** reading field
120 as `cast_anim`, and eqemu names it `CastingAnim` at that same ordinal. So
121 and 122 land on `TargetAnim` and `TravelType` by the same arithmetic that
the shipped `cast_anim` column already validates. STOP not fired.

### 2. Pipeline (one column each, appended everywhere)

| file | change |
|---|---|
| `tools/willeq_db_builder.cpp` | `target_anim INTEGER, travel_type INTEGER` appended to the `spells` schema; two names + two placeholders on the INSERT; two `sqlite3_bind_int(..., safeInt(EQ::SpellField::TargetAnim / ::TravelType))` after the T355 endurance bind. Stored verbatim. |
| `include/client/willeq_database.h` | `SpellInfo::targetAnim` (uint16_t), `SpellInfo::travelType` (int32_t); `kWilleqDbExpectedVersion` **6 → 7**. |
| `src/client/willeq_database.cpp` | both spell SELECTs end `..., endurance_cost, target_anim, travel_type`; read in the shared positional-tail helper `readSpellEffectColumns()` so `getSpell` and `getSpellByName` cannot drift. **No existing column index moved** — new columns go on the end, the rule T355 set. |
| `include/client/spell/spell_data.h` | new `uint8_t travel_type`; `target_anim` (declared since forever, never assigned) documented with its field index; `visualVocabulary()` copies `travelType`. |
| `src/client/spell/spell_database.cpp` | `spell.target_anim = info.targetAnim; spell.travel_type = info.travelType;` beside the `cast_anim` line. |
| `include/client/spell/spell_visual_vocabulary.h` | `uint8_t travelType`, placed after `school` so it takes the existing padding byte; the "NOT carried here: travel_type" banner replaced by the closing note; `static_assert(sizeof(SpellVisualVocabulary) == 8)` added so a future field cannot silently grow the bridge payload. |

**Vocabulary size: 8 bytes, unchanged.** Layout is
`bool known` (1) + `ResistType` (1, uint8_t) + `SpellSchool` (1, uint8_t) +
**`travelType` (1, was padding)** + `int32_t spellAffectIndex` (4). T381
measured that `EventData` (280 B) and `GameEvent` (288 B) are sized by a
larger alternative, so even a growth would not have moved the queue element;
here there is no growth at all, and the `static_assert` pins it.

### 3. `data/willeq.db` regenerated — proof

```
build/bin/willeq_db_builder --eq-path /mnt/projects/EverQuestP1999 \
    --data-path <slot>/data --config-path <slot>/config -o <slot>/data/willeq.db
```
EXIT 0. Size **3,866,624 → 3,887,104** bytes (+20,480). Mode forced back to
**664** (the builder writes 755 — the T306 stray-mode trap); git index mode is
`100644`, the same as the base commit.

Old (v6) vs new (v7), compared with a full sqlite3 read of every table:

| assertion | result |
|---|---|
| tables | **13 both sides**, identical set |
| `spells` columns | **99 → 101** |
| added / removed | added `['target_anim', 'travel_type']`, removed **`[]`** |
| the 99-column prefix | **unchanged, in order** (nothing was reordered) |
| `spells` rows | **8,066 → 8,066** |
| shared columns, every table | **byte-identical across ALL rows** except `metadata` |
| `metadata` deltas | `version` 6 → 7, `build_date` — nothing else |

Census re-run (`scripts/refactor/t380_spell_visual_census.py --db data/willeq.db`)
reproduces T380's numbers **exactly**:

```
field 121 target_anim  distinct=27  nonzero=3608  top=[(0,4458),(13,3083),(27,138),(43,105),(49,70)]
field 122 travel_type  distinct=2   nonzero=85    top=[(0,7981),(3,85)]

== willeq.db cross-check ==
  column cast_anim    present
  column target_anim  present     <- was ABSENT
  column travel_type  present     <- was ABSENT
```

and the db agrees with the text file row-for-row:
`select target_anim!=0` → **3,608**, `select travel_type=3` → **85**,
`group by travel_type` → `[(0, 7981), (3, 85)]`.

Spot rows out of the new db:

```
 15 Greater Healing  target_anim  0  travel_type 0  cast_anim 43
 50 Summon Food      target_anim 43  travel_type 0  cast_anim 43
 54 Frost Bolt       target_anim 13  travel_type 3  cast_anim 44
202 Courage          target_anim 27  travel_type 0  cast_anim 42
```

### 4. Tests — pre-fix RED, post-fix GREEN

RED was taken **without `git stash`** (protocol): the working tree was
exported to a patch, then the READ side alone was reverted — version bump
7→6, both SELECT column lists, the two lines in `readSpellEffectColumns()`,
and the `SpellDatabase::queryAndCache` assignment — with the **pre-T384
`data/willeq.db` (v6, 99 columns) still in place**. That is the real pre-fix
state; the struct fields were left declared only so the new tests compile
(pre-T384 their absence is a compile error, not a failing assertion). The
three files were then restored from the exported patch and the resulting tree
verified byte-identical to the saved diff before committing.

| test | pre-fix | post-fix |
|---|---|---|
| `SpellDatabaseTest.SpellVisualColumnsRoundTripForMeasuredSpells` | **FAIL** — `target_anim` 0 vs 13 (54), 0 vs 27 (202), 0 vs 43 (50); `travel_type` 0 vs 3 (54) | PASS |
| `SpellDatabaseTest.SpellVisualColumnsAreZeroForANonProjectileSpell` | PASS (the control — it pins that the columns are not non-zero for everything, so it is green either way by design) | PASS |
| `SpellVisualVocabularyTest.VocabularyCarriesTravelType` | **FAIL** — `started.spellVisual.travelType` was `'\0'`, expected 3 | PASS |

The three round-trip spells carry **three distinct `target_anim` values**
(13 / 27 / 43), so a stuck-at-one-value read fails; Frost Bolt is the only one
of the three with `travel_type == 3`, so a stuck-at-zero (pre-fix) and a
stuck-at-three read both fail. Spells re-measured from the shipped
`spells_us.txt`, not quoted.

All 7 `test_spell_visual_vocabulary` tests pass post-fix (T381's 6 plus this
one).

### 5. Verification

| step | result |
|---|---|
| `cmake --build <slot>/build -j24` | **EXIT 0** (no exit-143 anywhere in this unit — no truncation sweep needed) |
| `bash <slot>/scripts/build-gles2-x86.sh` | **EXIT 0** |
| `bash <slot>/scripts/build-arm-noble.sh` | **EXIT 0** |
| `ctest --test-dir <slot>/build -j6 --output-on-failure` | **4,010 tests, 1 failed** — `AudioManagerTest.PreloadCommonSounds`, the sanctioned ALSA host exception. 173 s. |
| `visual_gate` | **Passed, 22.70 s**, inside that ctest run |
| `FileSizeCeiling` / `DocCitations` | Passed |
| goldens | **untouched** (`git status --porcelain tests/goldens/` empty) |
| `bash scripts/refactor/check_file_sizes.sh fable` | **26 files: 26 ok, 0 warn, 0 fail** |
| `data/willeq.db` mode | **664** on disk, `100644` in the index (unchanged from base) |

**All three trees were rebuilt before the gate**, per T304/T355 — the version
bump invalidates every one of them. The gate passed on the first read, i.e.
the `characterModelsBytes mismatch` signature did **not** appear, which is the
positive confirmation that `build-gles2` was genuinely rebuilt against v7.

### 6. Deviations / notes

- **One ctest discovery-stage artefact.** The first `ctest -j6` died at
  `GoogleTestAddTests.cmake:112`, "Process terminated due to timeout" while
  listing `test_player_camera_component` — not a test failure. The binary was
  checked with `--gtest_list_tests` (exit 0, tests enumerated), so it was the
  cold-cache contention case the protocol names, not truncation. The re-run
  completed with only the ALSA failure. No test was waived.
- `tools/willeq_db_builder.cpp` is now **1,462 lines** — still under the 1,500
  ceiling and `check_file_sizes.sh` reports it `ok`, but it is the closest file
  to the ceiling this unit touched, and every future db column adds ~4 lines to
  it (schema + INSERT name + placeholder + bind). Worth knowing while **B167 /
  T385** is deciding what to do about `tools/`; not boarded separately, since
  the file is not yet a FAIL and T385 owns that territory.
- Nothing consumes `travel_type` yet, exactly as the plan scopes it. The 85
  spells are now visible to the renderer through
  `SpellVisualVocabulary::travelType`, and the decision about what to draw for
  them stays **B165**, behind B162's oracle.
- No new board items: nothing was found that was not already an open entry.
