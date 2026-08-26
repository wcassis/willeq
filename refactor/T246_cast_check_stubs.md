# T246 — SpellManager's cast pre-checks are `return true` stubs (T244 finding)

Slot: slot3. x86. OWNS eqemu briefly (noclobber) for a live check of the
reagent refusal. Class A (player state, reagents) / B (line of sight).
Territory: src/client/spell/spell_manager.cpp `checkPlayerState`,
`checkReagents`, `checkLineOfSight` (+ header), the state they need
(read-only: PlayerState flags, InventoryManager item lookup, buff list),
tests. Do NOT touch combat.cpp / eq.cpp update thread (T245 slot1),
eq_chat / cmdEmote (T243 slot2).

## Defect
`checkPlayerState()`, `checkReagents()`, `checkLineOfSight()` all
`return true` (pre-existing TODOs, `spell_manager.cpp:612-625` per the
board), so T244's Stunned / Silenced / Feared / ComponentMissing /
NoLineOfSight refusals are unreachable and the server rejects instead
(with a round trip and, for reagents, no local message).

## Scope
1. `checkPlayerState`: stunned / silenced / feared / mezzed from whatever
   the client already tracks (grep for `m_is_stunned`, AT_STUN /
   SpawnAppearance handlers, buff effect ids for silence/fear/mez in
   `SpellData::effects`). Implement what exists; for anything the client
   does NOT track, leave that sub-check permissive and list it — do not
   invent state.
2. `checkReagents`: spell components from `SpellData` (component ids +
   counts — verify the fields are populated by willeq.db), look them up via
   InventoryManager; refuse with ComponentMissing when absent. Note the
   official client's "You are missing <item>" string if an id exists.
3. `checkLineOfSight`: ATTRIBUTE ONLY unless there is an existing LoS query
   (grep renderer/collision for a ray test the game thread can call
   without touching the renderer — `hasLineOfSight`, `raycast`). If none is
   callable from the game thread, keep permissive and document.
4. Tests for 1 and 2 (real EverQuest harness with willeq.db, T240's
   test_eq_action_handler_cast.cpp pattern): stunned → refusal string 175;
   missing reagent → 272 and NO OP_CastSpell sent; reagent present → cast
   proceeds. Red/green.
5. Live (optional if the lock is free): a spell with a reagent the
   character lacks → local refusal, nothing sent.

## Verification
Full ctest; Review; evidence review/T246/. Commit:
`T246: cast pre-checks are real for player state and reagents; line of sight <attributed|implemented>`.
