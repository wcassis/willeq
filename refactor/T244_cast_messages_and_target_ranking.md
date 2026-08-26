# T244 — cast refusals name the reason; /target substring picks the nearest match

Slot: slot3. x86. OWNS eqemu briefly (noclobber) if a live check is cheap;
unit tests are the acceptance. Class A (T240 findings). Territory:
src/client/spell/spell_manager.cpp `beginCastFromGem` result switch +
eq_strings lookups, src/client/state/entity_manager.cpp
`findEntityByName` (or wherever the /target ranking T173 built lives —
grep command_processor cmdTarget), tests. Do NOT touch eq_chat / cmdEmote
(T243 slot2), graphics/benchmark (T242 slot1).

## 1. Cast refusal messages
`beginCastFromGem()`'s result switch collapses OutOfRange / NoLineOfSight /
InvalidTarget (and any other distinct enumerators) into "Cannot cast
spell". The official client prints distinct strings — find them in
eqstr_us.txt (e.g. "Your target is out of range, get closer!", "You cannot
see your target.", "You must first select a target for this spell!") and
map each result to the right one. Test each branch. Red/green.

## 2. /target substring ranking
`/target <substring>` returns the LOWEST spawn id among matches, not the
nearest. Rank matches by distance from the player (keep T173's
living-over-corpse ordering as the primary key, distance secondary). Test:
two matches, farther one has the lower id → nearest wins; corpse vs living
still prefers living. Red/green.

## Verification
Full ctest; Review; evidence review/T244/. Commit:
`T244: cast refusals say why; /target prefers the nearest match`.
