# T363 — `/newskills` becomes `/skills` (B058, Will's ruling 2026-09-05)

Class B, any slot. Read the B058 board text.

1. Rename the slash command `/newskills` (`renderer_slash_commands.cpp:422`)
   to `/skills`; REMOVE `/newskills` entirely (no alias — Will's ruling).
   The body that survives is the fuller one: toggle the skills popup, close
   the spellbook and inventory popups, echo to chat.
2. The bare `skills` keyword on piped stdin (`console_input_handler.cpp:432`
   → `InputAction::ToggleSkills` → `renderer_frame_input.cpp:222`) must end
   up in the SAME body — route the intent to the one implementation so the
   two paths cannot drift again (measure that the popup-closing behaviour
   is now reached from stdin too; if `ToggleSkills` has other callers, the
   compiler finds them).
3. `/skills` is one of the names `/help` and tab completion (T357) list —
   check both show it and neither shows `/newskills`. The command
   registration order is pinned by the G03 golden
   (`tests/goldens/command_registration_order_g03.txt`): it learns the
   rename IN PLACE (correction 32), both faults re-planted.
4. Tests, pre-fix failing: `/skills` toggles the popup and closes the other
   two; `/newskills` is unknown; the stdin keyword closes the other two.
5. Every text mention of `/newskills` tree-wide (docs, CLAUDE.md files,
   help strings, tests) — grep and fix; `DocCitations` and the help golden
   if any.
6. Strike B058.

## Proof

Build, `ctest -j6`, the new tests with pre-fix/post-fix runs, G03 golden,
file sizes; renderer files change → visual gate standalone + ARM (rebuild
all three trees first; willeq.db v6).

STOP: none expected.
