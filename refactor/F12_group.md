# F12 — Split group/raid/pet cluster into eq_group.cpp

Move the group/raid/pet domain out of `src/client/eq.cpp` (11,932 lines).
This is the first HIGH-entanglement eq.cpp unit: the cluster reads and writes
`m_entities` and the group-state arrays heavily. It is still a pure TU move —
all state stays in EverQuest members; only method definitions relocate.
No behavior change, no interface change, no reformatting.

## Scope (orig 14339–14937 @ a0c02cd, ~25 methods. Enumerate by NAME.)

Move to new `src/client/eq_group.cpp` the contiguous group/raid/pet cluster:
group invite/accept/decline/disband senders and processors, group member
update handlers, `UpdateGroupMemberFromEntity`, raid stubs, pet command
senders and processors.

Note: F07 already moved the group ACCESSORS (`IsInGroup`, `IsGroupLeader`,
`GetGroupMemberCount`, `GetGroupLeaderName`, `HasPendingGroupInvite`,
`GetPendingInviterName`, `SyncGroupMemberToGameState`) to eq_player_info.cpp
— they are NOT part of this unit. This unit is the action/packet-handler
cluster only.

## Steps

1. Locate the cluster; list every `EverQuest::` definition found; confirm
   contiguity and note any interleaved non-group methods (leave + document).
2. Create `src/client/eq_group.cpp`: header comment + pruned includes.
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local static helpers: usual rule.
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt (ALSA expected-fail; RDP flake → re-run isolated).
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_group.cpp` — ok.
- eq.cpp shrinks by ~550+ lines.

## Review

(executor fills in)
