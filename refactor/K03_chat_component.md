# K03 — `ChatComponent` (family: chat)

Batch: `refactor/K_component_extraction.md` — §Mechanism (REVISED). Runs
alongside K02/K04 (Wave 1).

## Measured at `7ffc4522` (2026-09-03)

`src/client/eq_chat.cpp`, 645 lines, 13 `EverQuest::` bodies, one root
`CMakeLists.txt` site. `eq_strings.cpp` (16 bodies, string-table lookups)
is NOT in this unit — it is a candidate for free functions and gets its own
decision later; do not move it.

**Decision taken for this unit:** `AddChatSystemMessage`,
`AddChatCombatMessage`, `AddChatMissMessage` are OUTPUT PRIMITIVES with 30+
callers across every family (the same shape as `QueuePacket`). They STAY in
`EverQuest` for now; retargeting 30 call sites would collide with K02/K04's
territory. Record them in the Review as "core-candidates"; K-final decides
whether they become `ClientContext` services. Their bodies stay in
`eq_chat.cpp` → so `eq_chat.cpp` is NOT `git mv`'d whole: move the other
bodies out into `src/client/chat/chat_component.cpp` and leave the three
primitives (and nothing else) in `eq_chat.cpp`. Say in the Review how many
lines stayed.

| method | callers outside the family | tests |
|---|---|---|
| `DispatchSlashCommand` | `application_commands.cpp`, `application.cpp`, `eq_commands.cpp`, `action/command_processor_registration.cpp`, `help/merged_help.cpp`, `application.h` | 3 files |
| `ProcessChatInput` | `application_commands.cpp`, `eq_bridge_intents.cpp`, `eq_bridge_ui_audio.cpp`, `application.h` | 7 files |
| `SendChatMessage` | `ucs.cpp` (dead — BOARD B005), `eq_action_handler.cpp`, `action/action_dispatcher_social.cpp`, `action/command_processor_social.cpp`, `ucs.h`, `action/command_processor.h` | 1 |
| `ZoneProcessChannelMessage` | `eq_zone_dispatch.cpp`, `eq_enums.h` (a comment — check) | 2 |
| `ZoneProcessColoredText`, `ZoneProcessFormattedMessage`, `ZoneProcessSimpleMessage` | `eq_zone_dispatch.cpp` | 0 |
| `ZoneProcessEmote` | `eq_zone_dispatch.cpp`, `eq_zone_entry.cpp` | 0 |
| `ZoneSendChannelMessage`, `ZoneSendEmote` | none in src/include | 2 / 0 |

`ZoneSendEmote` has zero callers anywhere → delete (§5). `ZoneSendChannelMessage`
has test callers only → keep on the component, retarget the tests.
Re-measure everything at your tip.

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_unrecognized_command_fallback` | 2 | none | **move** |
| `m_last_tell_sender` | 2 | `action/action_dispatcher_social.cpp`, `action/command_processor_social.cpp` (readers) | shared non-core: stays as `owner_.m_last_tell_sender`; note the two readers for K12 |
| `m_string_db` | 1 | `eq_strings.cpp`, `eq_player_social.cpp` | stays, `owner_.` |
| `m_structs` | 0 in family | 0 tree-wide? measure | if zero: board as dead, do not touch |
| `m_bridge`, `m_character`, `m_combat_manager`, `m_command_registry`, `m_entities`, `m_game_state`, `m_zone_connection` | core | | `ctx_.` |

Predicted `eq.h` delta: −10 declarations (13 − 3 primitives) −1 member
+4 scaffolding ≈ **−7 or better**.

## Steps

Per §Mechanism. New files `include/client/chat/chat_component.h`,
`src/client/chat/chat_component.cpp` (moved bodies), `tests/test_chat_component.cpp`
(register next to `test_chat_channel_names`). Note `src/client/chat/` already
exists with one file — read it first; `include/client/chat/` likewise.

## Territory

`include/client/chat/chat_component.h` (new), `src/client/chat/chat_component.cpp`
(new), `src/client/eq_chat.cpp` (shrinks), `include/client/eq.h`,
`src/client/eq.cpp` (ctor), root `CMakeLists.txt`, `tests/test_chat_component.cpp`
+ its cmake, allowlist (one number), `docs/module_map.md` (chat row), and
CALL-SITE-ONLY edits in the caller files listed above plus the tests that
call retargeted methods. NOT `eq_strings.cpp`, NOT `ucs.cpp` (B005 deletes it
later; leave its dead call as-is — it does not compile against anything
that moves? verify: if `ucs.cpp` calls `SendChatMessage` on an `EverQuest*`,
retarget that one line too).

⚠ Same collision note as K02: K02/K04 edit the shared files at other lines;
the allowlist number will conflict at merge.

## Verification / Review

As K02.
