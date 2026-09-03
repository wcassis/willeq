# K08 — `PlayerComponent` (family: player — social, buffs, misc, state, camp, duel, corpse, inspect, AA/tribute handlers)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K06/K07). Wave 3; runs alongside K10. **K09 (zone) waits for both.**

## Measured at `feb6b10b` (2026-09-03) — re-measure at your tip

Six TUs, 142 `EverQuest::` bodies: `eq_player_info.cpp` (46),
`eq_player_misc.cpp` (31), `eq_player_social.cpp` (29), `eq_player_state.cpp`
(21), `eq_player_buffs.cpp` (15), `eq_profile_appearance.cpp` (0 —
free functions; move the file with the family, no class change).

**The cut this unit must make.** `eq_player_info.cpp` is dominated by thin
STATE ACCESSORS that read `m_game_state` (`GetSTR`…`GetWIS`, `GetLevel`,
`GetClass`, `GetRace`, `GetGender`, `GetDeity`, `GetCurrentHP/Mana/Endurance`,
`GetMax*`, `GetPlatinum/Gold/Silver/Copper`, `GetBank*`, `GetWeight`,
`GetMaxWeight`, `GetPosition`, `SetPosition`, `SetHeading`, `GetHeading`,
`IsMoving`, `SetMoving`, `IsAFK`, `IsAnonymous`, `IsRoleplay`, `IsCamping`,
`IsInGroup`, `IsGroupLeader`, `GetGroupLeaderName`, `GetGroupMemberCount`,
`HasPendingGroupInvite`, `GetPendingInviterName`, `GetPracticePoints`,
`GetClassName`, `GetRaceName`, `Test_GetGameStatePlayerPosition`). They are
the **player-state facade** the whole tree reads (`GetPosition` alone has
callers in combat, skills, spells, the action handlers). **They STAY in
`EverQuest`**, in `eq_player_info.cpp`, exactly as the three `AddChat*`
primitives stayed in K03. K13 decides whether the facade becomes a
`ClientContext` service. Record how many bodies stayed.

Everything else moves: every `Send*`, every `ZoneProcess*`, `PerformEmote`,
`CancelCamp`/`StartCampTimer`/`UpdateCampTimer`, `PublishPlayerBuffs`,
`PublishPlayerBuffUpdated`, `RequestCancelBuff`, `RemoveEntityFromGameState`,
`SyncEntityToGameState`, `SetCombatStopDistance`, `SetMoveSpeed`, `SetAFK`,
`SetAnonymous`, `SetRoleplay`. If a mover is a one-line accessor with a
tree-wide reader set, argue it in the Review and leave it with the facade.

Callers by compiler (K05 rule). Known large clusters: `eq_action_handler*.cpp`
(~40 sites), `eq_zone_dispatch.cpp` (~45 handlers), `eq_bridge_intents.cpp`,
`eq_bridge_ui_audio.cpp`, `combat*.cpp`, `spell_manager.cpp`,
`skill_manager.cpp`, `action/command_processor_*.cpp`,
`action/action_dispatcher_guild_misc.cpp`; and the already-extracted
components (`group_component.cpp` reads `GetGroupLeaderName` etc. via
`owner_.` — those stay with the facade, so no change there).

Members (single-family → move): `m_dragged_corpse_name`, `m_duel_initiator_id`,
`m_duel_target_id`, `m_has_pending_duel`, `m_is_dueling`, `m_is_lfg`,
`m_is_on_boat`, `m_pending_rezz`, `m_test_last_face_change` (+ its `Test_`
accessor), `m_test_last_inspect_answer` (+ accessor), `m_transport`,
`m_inspect_message` (1 reader in `eq_bridge_ui_audio.cpp` → `Player()`
getter). Shared → `owner_.`: `m_has_pending_rezz` and `m_is_dragging_corpse`
(`action_dispatcher_guild_misc.cpp` reads them — retarget those two reads
to a `Player()` getter and MOVE the members; record it), `m_pending_logout_disconnect`
(movement), `m_replay_suppressed_sends` (login/zone), the zone flags, and
the tribute/AA counters (zone-entry writes; K09). Confirm each exists first
(K04 h). Grep `eq.h` for inline accessors on every moving member (K04 f).

Predicted `eq.h` delta: −(≈95 declarations) −12 members −2 `Test_` accessors
−banners +4 ≈ **−100 or better**.

## Territory

`include/client/player/` + `src/client/player/` (new; the six TUs move
except `eq_player_info.cpp`, which stays and shrinks), `eq.h`, `eq.cpp`
(ctor), root `CMakeLists.txt`, `tests/test_player_component.cpp` + cmake,
allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the caller files
the compiler names, including inside `src/client/*/` components (write
`owner_.Player().Foo()` there). K10 runs alongside and edits the login/world
lines of the same shared files; K09 does not run until both merge.

## Verification / Review

As K05; plus the list of facade methods that stayed.
