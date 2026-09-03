# K06 — `EntitiesComponent` (family: entities — spawns, appearance, doors, world objects)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED). Wave 2;
runs alongside K05/K07.

## Measured at `feb6b10b` (2026-09-03)

Four TUs, 2,641 lines, 28 bodies: `eq_entities.cpp` (830),
`eq_entities_appearance.cpp` (653), `eq_entities_updates.cpp` (767),
`eq_entities_world.cpp` (391). Four root `CMakeLists.txt` sites. Move into
`src/client/entities/`; header `include/client/entities/entities_component.h`.

**`m_entities` stays in `EverQuest` as a core member** (`ctx_.entities`): 11
families use it and the batch's core rule applies. This component OWNS the
handlers that mutate it, not the map itself — say so in the header comment.

Caller discovery by compiler (K05 rule). Lead clusters: `eq_zone_dispatch.cpp`
(17 `ZoneProcess*`), `eq_zone.cpp` / `eq_zone_entry.cpp` (`ZoneProcessClientUpdate`,
`ZoneProcessNewSpawn`, `ZoneProcessZoneSpawns`, `ClearWorldObjects`),
`eq_bridge_intents.cpp` (`SendClickDoor`, `SendClickObject`, `SendCloseContainer`,
`SendSpawnAppearance`, `UpdatePlayerAppearanceFromInventory`),
`eq_action_handler*.cpp`, `eq_commands*.cpp`, `action/command_processor_social.cpp`
(`FindEntityByName`), `eq_movement.cpp` (`FindEntityByName`, `ListEntities`),
`eq_player_social.cpp` / `eq_gm.cpp` / `eq_movement_state.cpp` / `eq.cpp`
(`SendSpawnAppearance`, `SendAnimation`), `skill/skill_manager.cpp`
(`SendAnimation`), `include/client/application.h` (`ZoneProcessMobHealth` — read
it), `include/client/world_object.h` (`ZoneProcessGroundSpawn` — a comment?).
`ApplyAppearanceToGameState`: no external caller — internal or dead (§5).

Members:

| member | refs | elsewhere | decision |
|---|---|---|---|
| `m_pending_door_clicks` | 2 | none | **move** |
| `m_loot_complete_corpse_id` | 3 | `eq_bridge_ui_audio.cpp` | shared: `owner_.` (loot cluster → K11) |
| `m_exp`, `m_exp_aa` | 3 / 2 | `eq_zone_entry.cpp`, `eq.cpp` | shared with zone: `owner_.`; K09 moves them |
| `m_player_graphics_entity_pending`, `m_req_new_zone_sent`, `m_send_exp_zonein_received`, `m_server_filter_sent`, `m_weather_received`, `m_zone_entry_sent`, `m_zone_lines`, `m_update_running` | 1–3 | zone / handshake / movement | shared: `owner_.`; zone-family members, K09 |
| `m_world_objects` | 7 | `eq_player_misc.cpp`, `eq_action_handler_spells_trade.cpp`, `eq_bridge_intents.cpp`, `action/action_dispatcher.cpp` | shared non-core: `owner_.`; **candidate to move here when K08/K11/K12 retarget their readers — record the four reader sites** |
| `m_guild_id` | 2 | `eq_zone_entry.cpp`, `guild_component.cpp` | `owner_.` |
| core: `m_bridge` 57, `m_entities` 57, `m_game_state` 55, `m_combat_manager` 14, `m_trade_manager` 12, `m_inventory_manager` 11, `m_character` 7, `m_zone_connection` 5, `m_active_tradeskill_object_id` 4, `m_audio_manager` 2, `m_buff_manager` 2 | | | `ctx_.` |

Predicted `eq.h` delta: −28 declarations −1 member −banners +4 ≈ **−25 or
better**.

## Territory

`include/client/entities/` + `src/client/entities/` (new), the four
`eq_entities*.cpp` (move), `eq.h`, `eq.cpp` (ctor), root `CMakeLists.txt`
(four lines), `tests/test_entities_component.cpp` + cmake, allowlist,
`docs/module_map.md`, CALL-SITE-ONLY edits in the caller files the compiler
names. `test_reachability_sweep.cpp` may scan `eq_entities.cpp` by name —
grep `tests/` for `eq_entities` as a STRING before you move anything; if it
does, the repoint is territory (K13-shape correction 27).

## Verification / Review

As K05.
