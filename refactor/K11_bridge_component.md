# K11 — `BridgeComponent` (family: bridge — event/intent pumps, loot window, config I/O, audio/graphics lifecycle, zone-load snapshot)

Batch: `refactor/K_component_extraction.md` §Mechanism (REVISED through
K10). **Wave 4; runs alongside K12 AFTER K09 merges.**

The batch table originally said this family "becomes bridge/intent handlers,
not a component". That is a design step, not a mechanical one; the mechanical
step is the same extraction as K01–K10, and it is what makes the later design
step delegable. Do the extraction. Folding `BridgeComponent` into
`GameStateBridge`/`IrrlichtBridge` is K13-or-later.

## Measured at `dfd36b06` (2026-09-03) — re-measure at your tip

Three TUs, 45 bodies: `eq_bridge.cpp`, `eq_bridge_intents.cpp`
(`ProcessBridgeIntents`, 580 lines, moves WHOLE — Batch G), `eq_bridge_ui_audio.cpp`.
Callers are the APPLICATION layer: `application*.cpp` (`InitGraphics`,
`ShutdownGraphics`, `GameTick`, `PostRenderTick`, `ProcessBridgeEvents`,
`CreateZoneLoadSnapshot`, `SetZoneBspTree`, `LoadHotbarConfig`,
`LoadInspectMessage`, `wireBridgeToSubsystems`, `setBridge`), the renderer
(`OnGraphicsMovement` from `renderer_player_physics.cpp`; `InitGraphics`
from `renderer_zone_load_env.cpp` — read it, may be a comment), the
extracted components (`OnPet*`, `OnSpawn*Graphics`, `UpdateDayNightState`,
`UpdateInventoryStats`, `PlaySound`, `CloseLootWindow`, `LootItemFromCorpse`,
`RequestLootCorpse` — rewrite as `owner_.Bridge().X()`), `combat*.cpp`,
`eq_action_handler*.cpp`, `input/input_action_bridge.cpp`,
`static_ui_input.cpp`, `command_processor_loot.cpp`.

⚠ `setBridge` / `InitGraphics` / `GameTick` are `EverQuest`'s LIFECYCLE
entry points, called by `Application`. Move their bodies, but keep the
`EverQuest` entry points as the ONLY sanctioned forwarders in the batch —
`Application` must not need to know a component exists to start the game.
Say so in the header comment and count them in the Review (expect ≤ 5).

Members (single-family → move): `m_audio_config_*` (5), `m_config_path`,
`m_is_daytime`, `m_region_maps_path`. `m_instance`/`m_slot`/`m_Struct` are
other classes' names — ignore. Shared → `owner_.`: the zone-environment set
K09 left for this unit (`m_zone_map`, `m_zone_bsp_tree*`, `m_zone_fog_*`,
`m_zone_sky_type`, `m_zone_type`, `m_eq_client_path`, `m_zone_audio_manager`,
`m_hotbar_model`, `m_loot_items`, `m_pending_loot_slots`,
`m_loot_complete_corpse_id`) — **this unit moves the loot cluster** (the
loot window is bridge-facing; K05/K06 recorded it as K11's) and the
zone-environment set if their only readers are here + `eq.cpp`; record each.

Predicted `eq.h` delta: −40 declarations −8 members −(loot + env cluster,
≈15) +4 ≈ **−55 or better**.

## Territory

`include/client/bridge/bridge_component.h` (new — `include/client/bridge/`
already holds `GameStateBridge`; do not touch those files),
`src/client/bridge/eq_bridge*.cpp` → `src/client/bridge/bridge_component*.cpp`
(move), `eq.h`, `eq.cpp`, root `CMakeLists.txt`, `tests/test_bridge_component.cpp`
+ cmake, allowlist, `docs/module_map.md`, CALL-SITE-ONLY edits in the callers
the compiler names, including `application*.cpp`, `src/client/*/` components
and `src/client/graphics/renderer_player_physics.cpp` (one call site — if it
changes, the visual gate and ARM steps are NOT skippable). K12 runs alongside
and edits `eq_action_handler*.cpp` at its own lines.

## Verification / Review

As K05; plus the forwarder count and the loot/env cluster decision table.
