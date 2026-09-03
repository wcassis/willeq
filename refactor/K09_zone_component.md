# K09 — `ZoneComponent` (family: zone — connection lifecycle, handshake, entry, `DispatchZonePacket`)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED). **Runs
ALONE after K08 and K10 merge** — it is the family every other component's
handlers are dispatched from, and it owns the zone flags the extracted
components currently reach as `owner_.m_*`.

## Measured at `feb6b10b` (2026-09-03) — re-measure at your tip

Four TUs, 35 bodies: `eq_zone.cpp`, `eq_zone_dispatch.cpp` (`DispatchZonePacket`,
880 lines, moves WHOLE — Batch G decides its decomposition, not this unit),
`eq_zone_entry.cpp`, `eq_zone_handshake.cpp`. 103 members referenced, 32
single-family.

**What this unit is really doing:** after K01–K08 and K10, `DispatchZonePacket`
is a switch whose arms call `m_<family>->ZoneProcess*(p)`. Moving it into
`ZoneComponent` turns those into `owner_.<Family>().ZoneProcess*(p)` — a
mechanical rewrite of ~100 arms, and the last big block of `eq.h`
declarations goes with it.

Members:
- **move** (single-family, 32): every `m_*_sent` / `m_*_received` handshake
  flag, `m_zone_sequence`, `m_zone_archive`, `m_zone_wld`, `m_maps_path`,
  `m_safe_x/y/z`, `m_gm_mode` (+ `GetGMMode()` accessor — 4 external readers,
  retarget), `m_test_force_zone_out_handshake`,
  `m_test_last_zone_change_request` (+ `Test_` accessors), `m_zoning_enabled`.
  Some names in the single-family list are OTHER classes' members appearing
  in lambdas (`m_id`, `m_instance`, `m_pos`, `m_Position`, `m_speed`,
  `m_spells`, `m_bank`, `m_mode`, `m_packet_type`, `m_offset_annotated`,
  `m_zone`, `m_profile_appearance`, `m_spawn_id`, `m_structs`) — verify each
  against `eq.h`; only `EverQuest` members move.
- **move, and retarget the components that read them as `owner_.m_*`**: the
  zone flags K06/K07 left behind (`m_req_new_zone_sent`, `m_server_filter_sent`,
  `m_zone_entry_sent`, `m_weather_received`, `m_player_graphics_entity_pending`,
  `m_send_exp_zonein_received`, `m_exp_zonein_sent`, `m_zone_out_*`,
  `m_zone_to_zone_in_progress`, `m_zone_lines`, `m_movement_history`,
  `m_last_movement_history_send`, `m_replay_suppressed_sends`, `m_exp`,
  `m_exp_aa`, `m_aa_table_count`, `m_tribute_count`, `m_guild_tribute_count`,
  the scribe trio `m_pending_scribe_*`, `m_navmesh_path`). Each becomes a
  small public getter/setter on `ZoneComponent` or a public member — prefer
  the accessor; record the choice per member. Readers: `entities/`,
  `movement/`, `merchant/`, `player/`, `login_world/` components, `eq_bridge*.cpp`,
  `eq.cpp`.
- **stay** (bridge/renderer-facing zone environment: `m_zone_map`,
  `m_zone_bsp_tree`, `m_zone_bsp_tree_zone_name`, `m_zone_fog_*`,
  `m_zone_sky_type`, `m_zone_type`, `m_eq_client_path`, `m_zone_audio_manager`,
  `m_hotbar_model`, `m_chat`, `m_guild`, `m_gm` (component pointers),
  `m_loot_items`, `m_pending_loot_slots`, `m_guild_id`, `m_guild_rank`,
  `m_world_*` (K10 moved or kept them — read K10's Review). These go to
  K11 (bridge) or K13.

Callers by compiler. `DispatchZonePacket` has external callers in `eq.cpp`
(the receive callback) and `eq_player_social.cpp`; `CleanupZone` /
`LoadZoneSources` from `application*.cpp` and `eq_bridge.cpp`; `FixZ` /
`CheckZoneLine` / `ProcessDeferredZoneChange` / `BeginGracefulZoneOut` /
`DisconnectFromZone` from the movement and player components (`owner_.` →
`owner_.Zone().`); `GetGMMode` from the action layer.

Text readers: `test_reachability_sweep.cpp` and `test_duplicate_state_stores.cpp`
scan `eq.h`; `test_zone_connection_flow.cpp` (1,367 lines) drives the
handshake through `EverQuest` `Test_*` seams — those seams move with their
members and the test's call sites are retargeted (assertions untouched).
Grep `tests/` for `eq_zone` as a string.

Predicted `eq.h` delta: −35 declarations −(32 + ~20) members −accessors
−banners +4 ≈ **−90 or better**. After K09 `eq.h` should be near 1,400.

## Territory

`include/client/zone/` + `src/client/zone/` (new), the four `eq_zone*.cpp`
(move), `eq.h`, `eq.cpp`, root `CMakeLists.txt`, `tests/test_zone_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in every
caller the compiler names, including inside `src/client/*/` components and
`application*.cpp`. Runs alone: no collision matrix.

## STOP conditions

- `DispatchZonePacket` needs any edit beyond `m_x->` → `owner_.X().` and the
  standard substitutions → STOP with the arm named.
- `test_zone_connection_flow` needs an assertion change → STOP.

## Verification / Review

As K05; plus the per-member accessor table for the flags the components
now reach through `Zone()`.
