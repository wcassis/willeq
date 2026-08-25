# T234 — pin the five untested EqActionHandler transforms against the real EverQuest

Slot: slot1. x86. NO eqemu. Class A (T93 leftover, deferred.md ~L5462).
Territory: tests/ only (a new or existing test_eq_action_handler_transforms*
binary + tests/CMakeLists.txt). Do NOT edit production code; if a test
exposes a defect, write the failing test, mark it DISABLED_ with a comment
naming the defect, and report it — do not fix in this unit. Do NOT touch
tests that T230 (slot3, unmerged) edits: test_action_dispatcher.cpp,
test_input_action_bridge.cpp, test_eq_action_handler_hotbar_item.cpp, or
combat/hotbar tests.

## Gap (T93)
`EqActionHandler` methods whose transform logic is exercised only through
the fake handler, never against the real `EverQuest`: `targetSelf` (self /
pet toggle), `targetGroupMember(index)` (validation + bounds), `hailTarget`
(fallback when no target), `setMovementMode`, `setPositionState`. Build
the real `EverQuest` the way tests/test_eq_action_handler_trade.cpp and
tests/test_game_state_source_writes.cpp do (RecordingBridge + Test_ seams),
drive each method through real state (a pet spawned via the packet path,
group members via the group handlers, no target), and assert on
`GetGameState()` / the recorded bridge events / the sent packet bytes.
Each test must be shown to FAIL against a deliberately wrong expectation
or a stubbed-out method (measure red the T219 way) so it is known to bite.

## Verification
Full ctest; Review in this file (list any defect found); evidence
review/T234/. Commit: `T234: EqActionHandler transforms pinned against the real client`.
