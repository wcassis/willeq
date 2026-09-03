# K15 — The S2 leftovers and the dead members the batch surfaced

Batch: `refactor/K_component_extraction.md`. **Runs after K13 merges**;
alongside K14 (disjoint territory).

Every item below is on `refactor/BOARD.md`, confirmed at the tip named
there. This unit closes them and strikes them. It is the first NON-move-only
K unit: deletions and two signature changes. The proof is the compiler plus
the suite, not a multiset.

## Items

| board | what | action |
|---|---|---|
| B005 | `src/client/ucs.cpp`, `include/client/ucs.h` `UCSConnection`, zero callers; the commented connect path in `WorldProcessSetChatServer` (now in `login_world_component.cpp`); the three `m_ucs_*` members K10 moved | delete TU, header, CMake line, the commented block, the members; tombstone comment at the old call site naming T297's measurement |
| B006 | S2 leftovers: `InitGraphics(int, int, IrrlichtRenderer*, GameStateBridge*)` — read it; if the renderer pointer only reaches `BridgeComponent::InitGraphics`, narrow the parameter to the interface the bridge component actually calls (`initLoadingScreen` / `initDeferredGraphics` — see `src/client/eq_bridge.cpp` history) or keep and RECORD why it cannot narrow; `door_manager.cpp`'s `EverQuest::GetDebugLevel()` → the logging module's level query (find what `LOG_*` macros use); any remaining `irrlicht_renderer.h` include outside `src/client/graphics` that no symbol needs | narrow / replace / delete; measure with the compiler |
| B019 (the first one — `m_guild_name`) | zero references | delete |
| B030 | `m_swim_up`/`m_swim_down` + `SetSwimUp`/`SetSwimDown` | delete (or wire if a swim-path caller exists — measure; expect delete) |
| B031 | `m_last_movement_update` | delete |
| B033 | 14 `IrrlichtRenderer` declarations defined nowhere | delete the declarations |
| B034 | 8 `IrrlichtRenderer` members with zero references | delete |
| B035 | `ZoneComponent::m_spawn_appearance_sent` / `m_client_spawned`, write-only | delete the members and their resets |
| B037 | `m_audio_config_use_3d_audio` settable only by editing the header | wire from `application_config.cpp` like its siblings (read how `m_audio_config_enabled` is wired) |
| numbering | `BOARD.md` carries two **B019** entries | renumber the second (the placeables gate) to the next free ID and fix its one cross-reference |

## STOP conditions

- A "zero references" claim is false at your tip → leave the item, say so.
- Narrowing `InitGraphics` needs a renderer interface that does not exist →
  record the shape, do not invent one.

## Territory

The files each item names, root `CMakeLists.txt` (UCS line),
`include/client/eq.h`, `include/client/graphics/irrlicht_renderer.h`
(declarations/members only — no Batch L work), `include/client/zone/zone_component.h`
+ its `.cpp`, `include/client/bridge/bridge_component.h`,
`src/client/application_config.cpp`, `src/client/graphics/door_manager.cpp`,
`refactor/BOARD.md`, both allowlist numbers (ratchet `eq.h` AND
`irrlicht_renderer.h`). If `door_manager.cpp` or `irrlicht_renderer.h`
changes, the visual gate and ARM steps are NOT skippable.

## Verification / Review

Full bar. `ReachabilitySweep`, `DuplicateStateStores` unchanged. Strike every
closed item on the board with the commit hash.
