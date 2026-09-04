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
