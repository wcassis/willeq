# T381 — the spell visual vocabulary becomes reachable (B160, T380 Leg B1)

Class A, slot3, no server. Read the B160 board text and
`refactor/T380_spell_visuals_survey.md` §4 Leg B1 (the shape, the
`InventorySlotChanged` precedent, the STOP).

1. Enrich `SpellCastVisualStartedData` / `SpellCastVisualCompleteData`
   (`include/client/state/event_bus.h`) with `resistType`, `school`,
   `spellAffectIndex`, `travelType`, filled on the GAME thread by
   `SpellManager::handleBeginCast` / `handleAction` from its own
   `SpellDatabase` (never hand the db pointer across the bridge — its LRU
   has no mutex). `travelType` needs B163's dropped column: if it is not in
   willeq.db yet, carry `spellAffectIndex` + `resistType` + `school` only
   and leave `travelType` to B163 (say so).
2. In the bridge consumer (`src/client/bridge/irrlicht_bridge.cpp`) and
   `SpellVisualFX`, key colour / spray style / `resolveVisual()` on the
   event payload instead of `m_spell_db`; delete `setSpellDatabase()` and
   `m_spell_db` (zero callers) and every null branch that existed only
   because of them (K12 ff check — the FX layer publishes nothing).
3. Tests, pre-fix failing: a Fire spell and a Cold spell yield DIFFERENT
   particle colours through the event path; a spell whose affect index
   resolves reaches `Resolved` (T70's suppression gates become reachable —
   one test that a gate fires). The intent-table golden
   (`test_bridge_intent_table`) learns any reviewed statement IN PLACE.
4. Strike B160. B161 (dead FX API) stays for a later unit unless a
   function becomes reached here (say which).

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, goldens for the
TUs touched, file sizes; `src/client/graphics` changes → visual gate
standalone + ARM (the gate's zones cast nothing, so no golden should move;
if one moves, STOP with the contact sheet — never bless).

STOP: the enriched payload outgrows `GameStateBridge`'s coalescing (report
the size and the coalescing rule); a pixel golden moves.
