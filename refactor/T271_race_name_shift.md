# T271 — EverQuest::GetRaceName() is shifted and user-visible (T270 finding)

Slot: slot1. x86. NO eqemu. Class A. Territory:
src/client/eq_strings.cpp `GetRaceName` (+ its header decl), the same-class
cosmetic maps `getRaceScale()` / `getRaceName()` in
src/client/graphics/eq/race_codes.cpp and `getColorForRace()` in
entity_renderer.cpp, tests. Do NOT touch spellbook (T272 slot2), read-item
(T273 slot3).

## Defect (T270 Review)
`EverQuest::GetRaceName()` maps 14 → "Iksar" (row says Werewolf; Iksar is
128), 128 → "Vah Shir" (that is 130), 130 → "Froglok" (that is 330), 330 →
"Drakkin". It is printed to the user (`/who`, `/consider`, inspect, target
info), so players see wrong race names today.

## Scope
1. Source the names from the db (`race_models` has the name column T265
   audited) exactly as T270 made codes db-only; no row → the numeric id or
   "Unknown" (match what the official client shows for an unknown race —
   check eqstr for a race-name table first: if eqstr_us.txt carries race
   names, prefer those ids).
2. `getRaceScale()` / `getRaceName()` / `getColorForRace()`: same
   treatment if a db column exists; if `race_models` has no scale column,
   leave scale alone and say so (do not invent data).
3. Tests: a table test over all 190 rows asserting name(id) == the row;
   the four shifted ids pinned by name; unknown id → the fallback. Red
   against the current code for the shifted ids.

## Verification
Full ctest; visual gate; Review; evidence review/T271/. Commit:
`T271: race names come from the db -- the shifted table is gone`.

---

## Review (executor, 2026-08-26, slot1, branch task/T271-race-names)

### Name source: `race_models.name`, because eqstr has no race table
Scope 1 told me to check `eqstr_us.txt` for a race-name table first. **It has
none.** The block that looks like one -- 3200..3223, "Barbarian / Erudian /
Elvish / Dark Elvish / Dwarvish / Troll / Ogre / Gnomish / Halfling / Froglok
/ Goblin / Gnoll / Combine Tongue / ... / Vah Shir" -- is the **LANGUAGE**
list, not races (it sits between the spell-set strings and the
Awful..Master consider ladder, and it contains Combine Tongue, Thieves Cant
and Dark Speech). A grep for the remaining race names as whole lines
(`Iksar`, `Wood Elf`, `High Elf`, `Half Elf`, `Erudite`, `Drakkin`) returns
nothing at all. So `race_models.name` is the only race-name source in the
tree, and that is what both functions now read.

The column is fully populated: `SELECT count(*), sum(name IS NULL OR
name=''), sum(scale IS NULL) FROM race_models` -> `190|0|0`.

### What changed
| where | before | after |
|---|---|---|
| `eq_strings.cpp` `EverQuest::GetRaceName()` | 16-case switch, **shifted** (14 Iksar / 128 Vah Shir / 130 Froglok / 330 Drakkin), default `"Race{id}"` | row's `name`; no row -> `"Unknown (id)"` |
| `race_codes.cpp` `getRaceName()` | db, then a 70-case pre-JSON fallback switch | db only; same `"Unknown (id)"` |
| `race_codes.cpp` `getRaceScale()` | db, then a 60-case pre-JSON fallback switch | db only; no row -> `1.0f` (the switch's own default) |
| `entity_renderer.cpp` `getScaleForRace()` | loader, else a **third** copy of the numbering (42 -> 0.3 "Rat", 43 -> 0.4 "Bat") | one line: `EQT::Graphics::getRaceScale()` |
| `entity_renderer.cpp` `getColorForRace()` | unchanged | unchanged; annotated (see below) |
| `eq.h` | `GetRaceName` private | public, same rationale the sibling `GetClassName` already carries (pure lookup, no state, tests must reach it) |

The four shifted ids now print the row: 14 **Werewolf**, 128 **Iksar**, 130
**Vah Shir**, 330 **Froglok (Playable)**.

### `getColorForRace()` left alone, deliberately
`race_models` has **no colour column**. Re-keying that table to the audited
rows would mean inventing a colour per race, which the plan forbids, so the
values are untouched. What I did do is annotate the function: its case
comments are contradicted by the rows in ten places (21 is BEH Evil Eye not
Skeleton, 26 FRO Froglok not Snake, 33 GHU Ghoul not Ghost, 35 EEL Eel not
Rat, 42 WOL Wolf not Rat, 43 BEA Bear not Bat, 46 IMP Imp not Goblin, 48/49
KOB Kobold / DRA Lava Dragon not Spiders, 85 SPE Spectre not Dragon) and a
reader could easily mistake them for a name source. The comment now says
they are not one and points at `getRaceName()`. Colour is placeholder-cube
tint only, reached when no model could be built (T213).

### Tests
New `tests/test_race_names.cpp` (10 cases, 2 fixtures):
- `RaceNameNoDatabaseTest` (no db): all 16 ids the old switch answered for
  return `"Unknown (id)"`; the graphics twin does too; `getRaceScale()` is
  1.0 for all 13 ids its switch had an entry for.
- `RaceNameFromDbTest` (opens the shipped `data/willeq.db`, skips without
  it): `EveryRowServesItsOwnName` is the table test -- it walks race ids
  0..600, asserts `GetRaceName(id) == row.name` for every row found and that
  exactly **190** rows were seen; `GraphicsNameAndScaleFollowEveryRow` does
  the same for the graphics twin's name *and* scale and additionally asserts
  the two name functions agree on every row (they cannot drift again);
  `ShiftedIdsAnswerTheRow` pins the four, each carrying what it used to
  print; `GraphicsScalePinsTheContradictedIds` / `GraphicsNamePinsTheContradictedIds`
  pin 12 + 8 ids where the deleted switches disagreed with the rows;
  `IdWithNoRowIsUnknownEvenWithTheDbOpen` covers race 9999.

**Red/green (measured):** `test_red_before_fix.txt` -- **8 of 10 fail**
against the pre-fix code (the two that pass are the graphics pin cases,
which the db path already served). `test_green_after_fix.txt` -- **10/10**.

### One consumer was relying on the deleted switch (found by ctest, fixed)
`tests/test_who_response.cpp`'s `WhoResponseHandlerTest` builds a headless
`EverQuest` and **never opened willeq.db**, so every race name in its pinned
`/who` lines came from the hardcoded switch. Three cases went red
(`"* GM * [1 Rogue] Testchar (Unknown (3))"`). The fixture now opens the
shipped db in `SetUp()` the way `EverQuest::OpenDatabase()` does -- the same
correction T270 made to `test_inventory_model_view`. This is the T270 pattern
repeating: a test fixture that looked green only because the wrong-answer
fallback was there to answer it.

Added `WhoResponseHandlerTest.ShiftedRaceIdRendersTheRowName` to that file --
the **user-visible** half of the defect, asserting the rendered line
`"[12 Warrior] Lupine (Werewolf)"` for race 14, which used to render
`(Iksar)`.

### Verification
- `cmake --build build -j24`: ok (`build_after_fix.log`).
- `ctest -j24`: **3023/3024** (`ctest_full.txt`); the single failure is the
  documented ALSA `AudioManagerTest.PreloadCommonSounds`. (3024 vs the
  baseline's 2746 -- this branch is cut from a much later fable, and +11 are
  this unit's own.)
- `scripts/build-gles2-x86.sh`: ok (`build_gles2_x86.log`).
- Visual gate: **PASS**, goldens untouched (`visual_gate.txt`; `git status`
  shows only the 6 source/test files + the new test TU).
- `scripts/build-arm-noble.sh`: ok (`build_arm_noble.log`, "Build complete").
- `check_file_sizes.sh fable` (`check_file_sizes.txt`): 1 FAIL + 1 WARN,
  **both pre-existing and neither made worse** -- `entity_renderer.cpp`
  3341 -> **3340** (shrank), `eq.h` 2509 -> 2513 (+4, the public-access
  comment). `race_codes.cpp` 432 -> **267**; `eq_strings.cpp` 751 -> 748.

### Deviations
- **`entity_renderer.cpp::getScaleForRace()` was in scope by defect class but
  not named in the plan.** The plan's territory listed `getColorForRace()`
  from that file; `getScaleForRace()` sits 40 lines below it and was a third
  copy of the same pre-JSON numbering. Fixed (collapsed to the one db-backed
  call) rather than left, but flagged here rather than absorbed silently.
- **`EverQuest::GetRaceName` moved from private to public.** Required to test
  it at all; it is a stateless lookup and its sibling `GetClassName` is
  already public for the same reason (T180). Not a behaviour change.
- **`tests/test_who_response.cpp` and its CMake block were touched**, forced
  by the deletion (see above). No overlap with T272 (spellbook) or T273
  (read-item).

### Findings for the orchestrator (not changed here)
1. **`EQT::Graphics::getRaceName()` and `getGenderName()` have ZERO callers**
   in the whole tree (checked across `src/`, `include/`, `tests/`, `tools/`).
   The reachability pattern again. I kept `getRaceName()` and made it db-only
   per the plan's Scope 2, and it is now pinned equal to the reachable
   `EverQuest::GetRaceName()` on every row, so the duplication is 5 lines and
   cannot drift -- but deleting both is a legitimate follow-up.
2. **Race 330's row name is `"Froglok (Playable)"`**, which is what `/who`
   and `/consider` will now print for a playable Froglok. That reads like a
   data annotation, not a display name. It is a `config/race_models.json`
   row-level fix plus a `willeq_db_builder` rebuild, i.e. a data change
   outside this unit's territory -- flagged, not touched.
3. `entity_renderer.cpp:~398` still spells "invisible" as
   `raceId == 127 || raceId == 240` when `race_models.invisible` is the
   column that means exactly that. T270 already listed it; still open.
