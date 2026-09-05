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
