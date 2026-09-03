# K14 — Directory move: the last flat `eq_*.cpp` files, and `eq_strings` → free functions

Batch: `refactor/K_component_extraction.md`. **Runs after K13 merges.** Can
run alongside K15 (disjoint territory).

## Measured at `a89314e0` (2026-09-03) — re-measure at your tip

Flat files left in `src/client/` from the original 38: `eq.cpp`,
`eq_action_handler.cpp`, `eq_action_handler_extended.cpp`,
`eq_action_handler_spells_trade.cpp`, `eq_chat.cpp` (may be empty after
K13 — delete if so), `eq_player_info.cpp` (same), `eq_strings.cpp` (14
bodies, all pure lookups: `GetClassName`, `GetRaceName`, `GetDeityName`,
`GetOpcodeName`, `GetBodyTypeName`, `GetChatTypeName`, `GetEquipSlotName`,
`GetNpcTypeName`, `FormatPrice`, `GetStringMessage`,
`GetFormattedStringMessage`, `LoadStringFiles`, `DumpPacket`,
`DumpEntityAppearance`).

## Steps

1. `git mv src/client/eq.cpp src/client/core/everquest.cpp`;
   `git mv src/client/eq_action_handler*.cpp src/client/action/` (the action
   layer already lives there — check for name clashes with
   `action_handler.cpp` and prefix `eq_` stays if there is one). CMake lines
   follow; `docs/module_map.md` rows follow; `tests/` and `scripts/` text
   readers of any moved filename are repointed (grep first — K03 c).
2. `eq_strings.cpp`: the lookups that read no `EverQuest` state become free
   functions in `include/client/eq_strings.h` / `src/client/core/eq_strings.cpp`
   (namespace `eqt::strings`); the ones that read `m_string_db`
   (`GetStringMessage`, `GetFormattedStringMessage`, `LoadStringFiles`) move
   into a small `StringDb` class owned by `EverQuest` and exposed on
   `ClientContext` as `ctx_.strings`. Callers by compiler. `DumpPacket` /
   `DumpEntityAppearance` are debug helpers with `s_debug_level` reads —
   free functions taking the level as a parameter.
3. `include/client/eq.h`: delete the moved declarations; ratchet.
4. Multiset proof per moved body set; zero-code-residual.

## Territory

The files above, `src/client/core/` + `src/client/action/` (moves in), root
`CMakeLists.txt`, `include/client/eq.h`, `include/client/client_context.h`
(one member), `include/client/eq_strings.h` (new), `docs/module_map.md`,
tests/scripts text readers, CALL-SITE-ONLY edits in callers. NOT the
component directories except call sites.

## Verification / Review

Full bar; residue and ratchet as every K unit.
