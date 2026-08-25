# T240 — castSpell reads the real gems: repoint at SpellManager, delete CombatManager's dead gem map

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for the keypress cast
proof (T235's driver + Testcaster in review/T235/). Class A (T235 finding).
Territory: src/client/eq_action_handler.cpp `castSpell`/`castSpellOnTarget`
(+ header), src/client/combat.cpp / include/client/combat.h
(`m_spell_gems`, `CastSpell`, `MemorizeSpell` — the dead map and its
hardcoded cast-time stub at ~681-732), tests. Do NOT touch
input_action_bridge.cpp / action_dispatcher hotbar arms /
`processCommandText` (T239 slot3 — coordinate: keep your
eq_action_handler.cpp hunks to the two cast methods), application.cpp
script path (T238 slot1).

## Defect
`EqActionHandler::castSpell(gem)` → `CombatManager::CastSpell(slot)` looks
up `CombatManager::m_spell_gems`, filled only by `CombatManager::
MemorizeSpell()`; the profile load fills `EQ::SpellManager`'s gems instead.
So every action-layer cast (keyboard gem keys, `/cast N` if routed here)
fails "no spell memorized" while `/gems` shows the spells. T101 already
repointed `interruptCast` at SpellManager (`eq_action_handler.cpp:517-531`)
— same shape.

## Scope
1. Repoint `castSpell` / `castSpellOnTarget` at `SpellManager` (the method
   `/cast` uses — find it: `beginCastFromGem` or similar), preserving
   target semantics. Delete `CombatManager::m_spell_gems`, `CastSpell`,
   `MemorizeSpell` and the hardcoded cast-time/slot-0 stub IF no other
   caller remains (grep; tests that only test the dead map go too). If
   `MemorizeSpell` has a live caller, keep it but make it write SpellManager.
2. Tests: real EverQuest harness with the DB (test_eq_action_handler_spell_pet.cpp
   pattern): gem memorised via the real path → `castSpell(1)` → SpellManager
   isCasting + OP_CastSpell bytes recorded; empty gem → refusal. Red/green.
3. Live: Testcaster, alt+1 → server `BeginCast 'Frost Bolt'` (T235's
   AFTER run had 0 — that is the before). Also `/cast 1` for the record.

## Verification
Full ctest; visual gate not needed; Review; evidence review/T240/. Commit:
`T240: action-layer casts use SpellManager's gems; dead CombatManager gem map deleted`.
