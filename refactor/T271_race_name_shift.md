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
