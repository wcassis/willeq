# T232 — GameState::tradeskill() has no writer: wire it or delete it

Slot: slot1. x86. NO eqemu unless the wire path is chosen (then a short
noclobber-locked live leg after T231/T230 release it). Class A (T221
finding). Territory: include/client/state/tradeskill_state.*,
src/client/state/tradeskill_state.cpp, game_state.h (that member only),
src/client/eq_merchant.cpp recipe/combine handlers (writes only),
tests. Do NOT touch combat.cpp / action/* (T230 unmerged), eq_zone.cpp /
eq_movement.cpp (T231 unmerged).

## Facts to verify first
`state::GameState::tradeskill()` exists (game_state.h ~:109) with a
TradeskillState type, but grep shows no writer on the one remaining
instance and no bridge case ever existed. Find every READER
(`tradeskill()` across src/ and tests/). Then decide by the codebase rule
(docs/decisions.md 2026-08-25, second entry): state a handler learns from
the wire is written at the source.

## Scope — pick ONE
A. If there are real readers (action layer, /recipe* commands, UI) that
   currently see empty state: wire the writes from the recipe/combine
   packet handlers in eq_merchant.cpp (RecipeReply / RecipesFavorite /
   RecipesSearch / TradeSkillCombine result — whatever the handlers are
   named), add cases to tests/test_game_state_source_writes.cpp (red/green
   measured), and prove one reader now sees data.
B. If there are NO readers outside tests: delete the domain (type, member,
   accessor, its tests), and record in the Review what the recipe commands
   use instead. Confirm the build is clean without it.

## Verification
Full ctest; Review in this file; evidence review/T232/. Commit:
`T232: tradeskill state wired at the source` or `T232: dead tradeskill state domain deleted`.
