# T27 — Populate `SpellData::class_levels` (makes the dead class/level gate real)

**Slot: t2. x86 only. No eqemu server, no Pi.** Sibling unit: T26 runs
concurrently in t1 (`combat.cpp`/`combat.h`/`test_combat_manager.cpp`) —
stay out of that territory.

**Territory: `tools/willeq_db_builder.cpp`, the `spells` table schema,
`src/client/spell/spell_database.cpp`, `include/client/spell/spell_data.h`
(only if a mapping change is needed), `data/willeq.db` (TRACKED BINARY —
see below), and `tests/test_spell_manager.cpp`'s two pinned tests.**

Read `refactor/EXECUTOR_PROTOCOL.md` first, then the "T24 finding — the
spell class/level gate is dead" section in `refactor/testing_progress.md`.

Will's ruling 2026-08-02: **populate it** (option (a) of three). This plan
is the contract.

## The defect being fixed

`SpellData::class_levels` (`spell_data.h:122`) is
`std::array<uint8_t, NUM_CLASSES> class_levels = {}` and
`SpellDatabase::queryAndCache()` never assigns it — verified by reading
the full field-mapping block (`spell_database.cpp:61-84`); it is simply
absent. Every cached spell therefore carries all-zero class levels.

`canClassUse()` (`spell_data.h:200`) computes `required = class_levels[idx]`
→ `0`, then returns `required != 255 && level >= required` → **always
true**. `SpellManager::memorizeSpell()` gates on exactly that call
(`spell_manager.cpp:630`, logging "level too low" on failure), so the
check never rejects anything: any class can memorize any spell at any
level as far as the client is concerned. The server would refuse, so this
is a client-side validation gap, not an exploit — but the UI will show a
memorization that cannot succeed.

## Ground truth already established (do not re-derive)

The orchestrator confirmed all of this on 2026-08-02; verify cheaply,
don't re-investigate:

- **The data is NOT in the database.** `data/willeq.db`'s `spells` table
  has 24 columns and none of them are class levels (`PRAGMA
  table_info(spells)`). This was never extracted, not extracted-then-
  dropped.
- **The source data IS present.** `/mnt/projects/EverQuestP1999/spells_us.txt`
  (5.1 MB, `^`-delimited, the classic Titanium layout) carries them.
- **The field positions are already declared in-tree.**
  `include/client/spell/spell_constants.h:536` —
  `// Class minimum levels (16 classes): fields 104-119`, with
  `constexpr int SpellField::ClassLevel1 = 104` and
  `constexpr uint8_t NUM_CLASSES = 16` (line 44). Fields 104-119 are
  consecutive, one per class, in `PlayerClass` order (see the "Player
  Classes" block at line 376 — **confirm the ordering matches
  `canClassUse()`'s index derivation before trusting it**; a silent
  off-by-one here would be worse than the current no-op, because it would
  reject legitimate memorizations).
- **The builder already parses this file** with a documented
  "authoritative field positions" block and `safeInt()` helper
  (`willeq_db_builder.cpp:~122-140`), invoked at line 1258 as
  `loadSpells(db, eqPath + "/spells_us.txt")`.

## Scope

Four pieces, in this order:

1. **Schema + builder** (`willeq_db_builder.cpp`): carry fields 104-119
   into the `spells` table. Storage shape is your call, but justify it in
   the Review — recommendation: a single 16-byte `BLOB` (or fixed-width
   TEXT) column `class_levels` rather than 16 separate INTEGER columns,
   which keeps the table readable and the bind block short. Whatever you
   pick, it must round-trip exactly: `255` (= class cannot use the spell)
   must survive as `255`, and it is the sentinel `canClassUse()` tests
   for, so do not let it become `-1`/`0` anywhere in the path.
2. **Schema version**: `metadata.version` is currently `1`. Bump it, and
   check whether anything reads it — if a runtime check exists that would
   reject a bumped version, handle that explicitly rather than leaving a
   version nobody validates.
3. **Runtime mapping** (`spell_database.cpp`): populate `class_levels` in
   `queryAndCache()`. **Check how the existing SELECT is written first** —
   if it is `SELECT *` with positional column indices, adding a column
   shifts nothing only if appended last; if it names columns, add yours.
   Either way, verify the other 24 fields still map to the same values
   after your change (a spot-check of a few known spells against the
   pre-change db is the cheap oracle).
4. **Tests**: re-point T24's two pinned tests in
   `tests/test_spell_manager.cpp` from "the gate is dead" to asserting
   real behavior — at minimum one spell a class CAN use at the right
   level, one it cannot use at all (the `255` sentinel), and one it can
   use only above a level threshold (rejected below, accepted at/above).
   Use real spell IDs from the shipped db so the test is a genuine
   pipeline oracle, and name them so a future reader knows they are
   pinning data-pipeline correctness, not just arithmetic.

## `data/willeq.db` is a TRACKED binary — the part most likely to go wrong

`git ls-files` confirms `data/willeq.db` (2.9 MB) is committed, not
generated at build time. So this unit MUST regenerate it and commit the
new binary, or the schema change ships broken for everyone.

- Rebuild with the documented invocation (`willeq_db_builder --eq-path
  /mnt/projects/EverQuestP1999 --data-path <slot>/data --config-path
  <slot>/config --output <slot>/data/willeq.db`), from THIS slot, against
  the NFS EQ data path.
- **Prove the rebuild is otherwise a no-op.** Before committing, diff the
  old and new db *content* (not bytes — `sqlite3 .dump` both and diff, or
  compare per-table row counts + a checksum over each table's rows). The
  ONLY expected differences are: the new column, `metadata.version`, and
  `metadata.build_date`. If any other table or row moves, STOP and report
  — that means the builder's other loaders are not reproducible from the
  current source data, which is a separate finding and a bigger deal than
  this unit.
- `metadata.total_entries` is currently 35097; account for it if it moves.

## STOP conditions

- Class ordering in fields 104-119 cannot be confirmed to match
  `canClassUse()`'s index → STOP. Do not guess; a wrong mapping silently
  blocks legitimate spells, which is worse than today's no-op.
- The db rebuild changes anything beyond the three expected deltas above
  → STOP and report.
- Making the gate real turns out to reject spells that Testchar can
  legitimately memorize today → STOP and report with the specific spell;
  that means either the mapping or the sentinel handling is wrong.
- Any behavior change beyond spell memorization gating → STOP.

## Verification bar (FULL — source-touching and behavioral)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` flake family (T03b, perf
   territory — rerun isolated to confirm it is that and not you).
3. **Golden replay**: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq` — must
   pass with the golden file **unchanged**. If the golden moves, the
   memorization gate is now rejecting something the recorded session did;
   that is a real finding — report it, do not re-bless.
4. The db-content diff evidence from the section above, in the Review.
5. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new FAILs.

Note for the record (no action): the `willeq-benchmark` clone and the
other worktrees carry their own `data/willeq.db` copies. They are not
this unit's territory; the orchestrator will flag the schema bump to the
benchmark clone separately.

## Review

(Executor fills: storage shape chosen and why, the class-ordering
confirmation and how it was verified, the db-content diff evidence, the
tests re-pointed and what real spell data they pin, deviations.)
