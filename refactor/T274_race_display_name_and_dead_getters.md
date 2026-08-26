# T274 — "Froglok (Playable)" is printed to players; two zero-caller race getters (T271)

Slot: slot1. x86. NO eqemu. Class A, tiny. Territory:
config/race_models.json (name column only), tools/willeq_db_builder (no
change expected — just rebuild data/willeq.db with the T246/T265 proof),
src/client/graphics/eq/race_codes.cpp (`getRaceName`, `getGenderName`
deletion + their decls), tests (`test_race_names.cpp`,
`WilleqDatabaseShippedContentTest`). Do NOT touch spellbook (T272 slot2),
read-book (T273 slot3).

## Items
1. Race 330's `name` is `"Froglok (Playable)"`; T271 made names db-only so
   `/who` now prints that to players. Titanium shows "Froglok" for both.
   Fix the row's display name (keep any disambiguation in a comment or a
   separate column, NOT in `name`); check every other row for the same
   pattern (parenthetical annotations, "(Playable)", "(Monster)", codes in
   the name) — T265's table is in review/T265/audit_table_after.md. Rebuild
   the db with the row-level diff proof.
2. `EQT::Graphics::getRaceName()` and `getGenderName()` have zero callers
   (T271). Delete both with the grep proof; keep `EverQuest::GetRaceName()`
   (the reachable one) and its test.
3. Update `test_race_names.cpp` so the sweep still pins name(id) for all
   rows via the surviving function.

## Verification
Full ctest; Review; evidence review/T274/. Commit:
`T274: race display names are clean; dead race getters deleted`.
