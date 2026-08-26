# T253 — the hotbar Spell arm has its own cast-refusal literals (T251 finding)

Slot: slot1. x86. NO eqemu. Class A, tiny. Territory: src/client/eq_bridge.cpp
`HotbarActivateIntent` → `HotbarSlotType::Spell` arm (~1309) only, tests.
Do NOT touch inventory_manager.* (T252 slot3), combat.* / eq_movement
(T254 slot2).

## Gap
T244 unified two refusal sites through `castRefusalStringId`; T251 added
`castRefusalMessage()` for the named-component form. A THIRD site — the
mouse hotbar Spell arm — still switches on its own literals and says
nothing for ComponentMissing / OutOfRange / etc.

## Scope
1. Route the arm through `EQ::castRefusalMessage(*this, result)` exactly
   like the other two; delete the literals.
2. Test in tests/test_eq_action_handler_cast.cpp (or the hotbar harness):
   hotbar Spell click with a missing reagent → "You are missing X."; out of
   range → 100. Red/green.
3. Grep for any FOURTH site (`"Cannot cast spell"` / `CastResult::` switch)
   and list it if found.

## Verification
Full ctest; Review; evidence review/T253/. Commit:
`T253: hotbar spell clicks use the shared cast-refusal message`.
