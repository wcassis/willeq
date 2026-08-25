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

## Review (2026-08-25, slot1, option B)

**Facts verified.** `GameState::tradeskill()` (game_state.h:109-110, member
:179) existed with `TradeskillState` (tradeskill_state.h/.cpp, 3 open/close
methods firing `TradeskillContainerOpened/Closed`). Reader census
(`review/T232/reader_census_before.txt`, `grep -rn 'tradeskill()'` over
src/ include/ tests/): the ONLY hits are the two accessor definitions in
game_state.h. Zero callers in the action layer, commands, UI, bridge, or
tests. `TradeskillState` itself was named only by game_state.{h,cpp}, the
two CMake lists, and six unit tests in test_state_modules_secondary.cpp
that exercised the class in isolation. No writer, no bridge case, no
reader -- option B by the plan's rule.

**What the production code uses instead** (unchanged by this unit):
- The open world container is `EverQuest::m_active_tradeskill_object_id`
  (eq.h:1772), written at the source by `ZoneProcessClickObjectAction`
  (eq_entities.cpp ~2652/2675), cleared by `ClearWorldObjects()` and the
  eq_bridge.cpp:1405 close path, read by `EqActionHandler` through
  `GetActiveTradeskillObjectId()` (eq_action_handler.cpp:1107) and pinned
  by tests/test_eq_action_handler_tradeskill.cpp.
- The same handler already publishes `TradeskillContainerOpenedEvent` /
  `TradeskillContainerClosedEvent` directly to the bridge; irrlicht_bridge.cpp
  :404/:407 consumes them. Those event types and structs are KEPT.
- Recipes (`SendRecipesFavorite/Search/Details/AutoCombine`, eq_merchant.cpp
  1622-1697) are send-only; `ZoneProcessRecipeReply` / `ZoneProcessRecipeAutoCombine`
  only log. There is no recipe STATE anywhere to wire -- the deleted type
  never modelled recipes at all, only container open/closed.

**Deleted:** include/client/state/tradeskill_state.h,
src/client/state/tradeskill_state.cpp, the accessor + member + include in
game_state.h, 4 wiring lines in game_state.cpp, 3 CMake source entries,
6 `TradeskillStateTest` cases, the CLAUDE.md table row. deferred.md T221
note struck as CLOSED. No source-writes test applies (nothing to write).

**Verification.**
| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 (`review/T232/build.log`; reconfigure picked up the CMake change) |
| `ctest` full | **2804/2805**, sole failure AudioManagerTest.PreloadCommonSounds (ALSA, documented). 6 fewer tests than the pre-change tip (implied 2811 registered at `adeb930e`; the protocol's 2746 baseline predates T220-T229). `review/T232/ctest.log` |
| `build-gles2-x86.sh` | exit 0 |
| `check_file_sizes.sh fable` | checked 4 files, 4 ok |
| ARM build / visual gate | not run -- header/CMake-only deletion, no render-path change; plan narrows verification to full ctest |

**Deviation / observation (not acted on, out of territory):** the
2026-08-25 source-write rule says wire-learned state goes into
`m_game_state`; the open-container id lives in an `EverQuest` member
instead. That is a pre-existing shape (T12/T99 era), has a reader and a
test, and moving it would touch eq.h / eq_action_handler.cpp. Flagging
only; it is not the dead-data defect T221 filed.
