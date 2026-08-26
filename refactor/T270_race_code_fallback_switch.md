# T270 — the pre-JSON race-code fallback switch contradicts the audited rows (T267 finding)

Slot: slot1. x86. NO eqemu. Class A. Territory:
src/client/graphics/eq/race_codes.cpp (`getRaceCode` DB-null fallback,
`getFallbackRaceCode`, `getZoneSpecificRaceCode`) and the 38 tests that pin
the old numbering, tests. Do NOT touch loot (T268 slot3), bank (T269 slot2).

## Finding
When the db lookup returns null, `getRaceCode()` falls back to a hardcoded
switch that predates race_models.json; it disagrees with T265's audited
rows (17 ORC vs GOL, 35 RAT vs EEL, 72 ELE vs SHIP, 13 WOL vs AVI, 46 GOB vs
the row, 127/240 INV vs IVM ...). Inert at runtime while the db loads, but
a silent wrong answer the moment it does not — and 38 tests assert the
wrong numbering.

## Scope — decide by the codebase rule (db is the source of truth)
1. Delete the fallback numbering: with no db row the honest answer is
   "unknown → placeholder", the same path T213 documents. If the loader
   REQUIRES a code for the placeholder path, return a single sentinel.
2. Regenerate the 38 pinned expectations from the shipped db (a table
   test reading willeq.db, like `WilleqDatabaseShippedContentTest`) so
   the tests assert the audited truth, not the old switch. Red/green:
   the old tests fail against the new code for the contradicting ids.
3. Grep for any OTHER hardcoded race→code/archive map (`case 330`,
   `"FRG"` etc. outside data) and list it.

## Verification
Full ctest; visual gate (race code path); Review; evidence review/T270/.
Commit: `T270: race codes come from the db only; pre-JSON fallback deleted`.
