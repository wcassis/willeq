# K04 — `GroupComponent` (family: group, includes raid and pet)

Batch: `refactor/K_component_extraction.md` — §Mechanism (REVISED). Runs
alongside K02/K03 (Wave 1).

## Measured at `7ffc4522` (2026-09-03)

`src/client/eq_group.cpp`, 980 lines, 25 `EverQuest::` bodies (group + raid
+ pet; they share `m_game_state.pet()`/`group()` and stay together — the
component is `GroupComponent`, say "group, raid and pet" in its header
comment), one root `CMakeLists.txt` site.

| method | callers outside the family | tests |
|---|---|---|
| `AcceptGroupInvite`, `GetGroupMember`, `SendGroupDecline`, `SendGroupInvite` | `eq_action_handler.cpp` | 1/1/0/0 |
| `SendLeaveGroup` | `eq_action_handler.cpp`, `action/command_processor_social.cpp` | 0 |
| `SendGroupDisband` | `action/command_processor_social.cpp` | 0 |
| `SendPetCommand` | `eq_action_handler_spells_trade.cpp` | 1 |
| `SendRaidCommand` | `eq_action_handler_extended.cpp`, `action/action_dispatcher.cpp`, `action/command_processor_social.cpp`, `action/action_handler.h` | 0 |
| `ClearGroup` | `eq_zone.cpp` | 0 |
| `GetPetLevel`, `GetPetName` | `eq.cpp` | 0 |
| `ZoneProcessGroupCancelInvite`, `ZoneProcessGroupDisband`, `ZoneProcessGroupFollow`, `ZoneProcessGroupInvite`, `ZoneProcessGroupUpdate` | `eq_zone_dispatch.cpp` | 0 |
| `ZoneProcessRaidUpdate` | `eq_entities_updates.cpp`, `eq_zone_dispatch.cpp`, `raid_update.h` | 1 |
| `DeclineGroupInvite`, `SendGroupFollow` | none in src/include | 1 each — keep, retarget tests |
| `DismissPet`, `FillGroupMemberFromEntities`, `FindGroupMemberByName`, `GetPetEntity`, `UpdateGroupMemberFromEntity`, `ZoneProcessGroupJoin` | none in src/include, 0 tests | **internal to the family if called within `eq_group.cpp`; if a method has zero callers INCLUDING inside the family, delete it and board nothing (§5)** |

Re-measure at your tip. `eq.cpp`'s `GetPetLevel`/`GetPetName` callers: read
them — if they are in the ctor/wiring, retarget; if they are `Test_` seams,
move the seam with the method.

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_in_raid`, `m_pending_inviter_name`, `m_pending_raid_inviter`, `m_pet_button_states`, `m_raid_leader`, `m_raid_locked`, `m_raid_roster` | 1–6 | none | **move** (7 members) |
| `m_test_bypass_send_guard` | 2 | `eq_movement_state.cpp`, `eq_movement_update.cpp`, `Test_SetBypassSendGuardForSeam()` | shared non-core: stays, `owner_.` |
| `m_zone_connection_manager` (5), `m_zone_connection`, `m_bridge`, `m_character`, `m_entities`, `m_game_state` (67) | core | | `ctx_.` |

Predicted `eq.h` delta: −25 declarations (fewer if some are already
private-only and grouped) −7 members −(the "Pet queries" T40 comment block
travels with the pet getters) +4 scaffolding ≈ **−28 or better**.

## Steps

Per §Mechanism. New files `include/client/group/group_component.h`,
`src/client/group/group_component.cpp` (`git mv`), `tests/test_group_component.cpp`
(register next to the existing group/raid tests — grep `SendGroupFollow` in
`tests/cmake/`). Port two cases (accept invite; raid command) to the component.

## Territory

`include/client/group/`, `src/client/group/` (new), `src/client/eq_group.cpp`
(moves), `include/client/eq.h`, `src/client/eq.cpp` (ctor + the two pet-getter
call sites), root `CMakeLists.txt`, `tests/test_group_component.cpp` + cmake,
allowlist (one number), `docs/module_map.md` (group row), `include/client/raid_update.h`
(one call site if it is a real call), and CALL-SITE-ONLY edits in
`eq_action_handler.cpp`, `eq_action_handler_extended.cpp`,
`eq_action_handler_spells_trade.cpp`, `eq_zone_dispatch.cpp`, `eq_zone.cpp`,
`eq_entities_updates.cpp`, `action/command_processor_social.cpp`,
`action/action_dispatcher.cpp`, `action/action_handler.h`, and the tests that
call retargeted methods.

⚠ Same collision note as K02.

## Verification / Review

As K02.
