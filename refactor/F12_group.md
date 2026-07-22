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

**Cluster located:** a single contiguous run in `eq.cpp` at (pre-move) lines
8336–8937, immediately after `ZoneProcessTradeSkillCombine` and immediately
before `SendJump`. Header comments `// Group Methods` and `// Pet Methods`
bracket the two sub-clusters. No non-group/pet methods were interleaved
inside this range, so nothing had to be skipped mid-block.

**Moved methods (24), in original order:**
- `GetGroupMember`
- `SendGroupInvite`
- `SendGroupFollow`
- `SendGroupDecline`
- `SendGroupDisband`
- `SendLeaveGroup`
- `SendRaidInvite`
- `AcceptGroupInvite`
- `DeclineGroupInvite`
- `GetPetEntity`
- `GetPetHpPercent`
- `GetPetName`
- `GetPetLevel`
- `GetPetButtonState`
- `SendPetCommand`
- `DismissPet`
- `ClearGroup`
- `FindGroupMemberByName`
- `UpdateGroupMemberFromEntity`
- `ZoneProcessGroupInvite`
- `ZoneProcessGroupFollow`
- `ZoneProcessGroupUpdate`
- `ZoneProcessGroupDisband`
- `ZoneProcessGroupCancelInvite`

The header comment separates them into "Group Methods" (9: `GetGroupMember`
plus 8 invite/accept/decline/disband/raid senders) and "Pet Methods" (7:
`GetPetEntity`, `GetPetHpPercent`, `GetPetName`, `GetPetLevel`,
`GetPetButtonState`, `SendPetCommand`, `DismissPet`), plus 3 group-state
helpers (`ClearGroup`, `FindGroupMemberByName`,
`UpdateGroupMemberFromEntity`) and 5 packet processors — matching the
plan's "~25 methods" estimate within one.

**Interleaved/nearby methods explicitly left behind (with reasons):**
- `ZoneProcessRaidUpdate` (eq.cpp, still at original location near
  `ZoneProcessExpUpdate`, well outside the 8336–8937 range) — this is a
  `HC_OP_RaidUpdate` packet handler grouped with the other `ZoneProcess*Update`
  dispatch handlers near the top of the network-packet-handling section of
  eq.cpp, not physically contiguous with the group/pet cluster. Moving it
  would require cherry-picking a single method out of an unrelated,
  differently-organized region — out of scope for a "contiguous cluster"
  move and would risk scope creep into that neighboring region's own future
  split unit. Left in place and documented here per the "interleaved methods
  left behind" instruction, even though it is topically raid-related.
- Group ACCESSORS (`IsInGroup`, `IsGroupLeader`, `GetGroupMemberCount`,
  `GetGroupLeaderName`, `HasPendingGroupInvite`, `GetPendingInviterName`,
  `SyncGroupMemberToGameState`) — already moved to `eq_player_info.cpp` in
  F07, per the unit plan's explicit exclusion. Confirmed present there
  (lines 328–363) and not duplicated.
- `ZoneProcessPetBuffWindow` and `SendAssistGroup` — already resident in
  `eq_player_info.cpp` (moved in an earlier unit), not part of this
  cluster's contiguous range, left untouched.

**Helper analysis:** No new file-local static helpers were needed — the
moved code only calls other `EverQuest::` members (`IsFullyZonedIn`,
`AddChatSystemMessage`, `HasPet`, `SyncGroupMemberToGameState`,
`ClearGroup`, `FindGroupMemberByName`) and members of `m_game_state`,
`m_bridge`, `m_entities`, `m_group_members`, all of which remain declared
in `eq.h` and require no forwarding.

**Includes:** `eq_group.cpp` includes `client/eq.h`, `client/bridge/
game_state_bridge.h` (for `eqt::bridge::GameStateBridge` / `eqt::state::*`
event data types via its transitive `client/state/event_bus.h`),
`common/logging.h` (for `LOG_*` macros and transitively `<fmt/format.h>`
for `fmt::format`), `common/packet_structs.h` (for `EQT::*_Struct`,
`HC_OP_*` opcodes, `EQT::PetButton`/`PetCommand` enums), and `<cstring>`
(for `strncpy`). This is a pruned subset of eq.cpp's includes — no new
includes were invented, all were already used transitively by eq.cpp for
this code.

**Line counts:**
- `eq.cpp`: 11,932 → 11,329 lines (−603).
- `eq_group.cpp`: 623 lines (21-line header/include block + 602 lines of
  moved body, verbatim).

**CMakeLists.txt:** `src/client/eq_group.cpp` added to `CLIENT_CORE_SOURCES`
immediately after `src/client/eq_player_info.cpp`.

**Verifier summary:**
- `cmake --build build -j24`: clean (pre-existing unrelated `-Wpointer-arith`
  warnings in eq.cpp only, none introduced by this change).
- `ctest --test-dir build`: 1191/1192 passed; the sole failure,
  `AudioManagerTest.PreloadCommonSounds`, is the documented ALSA
  expected-fail in `refactor/test_baseline.txt`.
- `python3 scripts/refactor/verify_move_only.py <base>..HEAD --repo
  <repo>`: PASS — move-only confirmed after commit (multiset of
  non-boilerplate lines preserved across `eq.cpp` and the new
  `eq_group.cpp`; `CMakeLists.txt` change reported but not counted).
- `check_file_sizes.sh --files eq_group.cpp`: `ok` (623 lines, well under
  the 1,500-line target).

**Deviations:** none beyond the standard pruned-include set described
above (no de-static or necessary-include deviations were required).
