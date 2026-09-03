# Batch L — Component extraction from `class IrrlichtRenderer`

Same programme as `refactor/K_component_extraction.md`, same mechanism
(§Mechanism REVISED, every correction K01–K10 applies verbatim), second god
class. **Dispatch after K13 closes Batch K**, unless slots are idle — L01 has
no file overlap with any K unit and may run early.

## Measured at `cfadb78e` (2026-09-03)

| | `class IrrlichtRenderer` |
|---|---|
| header | `include/client/graphics/irrlicht_renderer.h`, 1,914 lines, one class |
| member functions declared | 356 (72 inline in the header) |
| data members | 288 (`foo_` trailing-underscore style) |
| definitions | 269 bodies across 27 flat `src/client/graphics/renderer_*.cpp` + `irrlicht_renderer.cpp` (~21,750 lines) |
| friends | 11, all gtest fixtures |
| nested types | 20 |
| declared, defined NOWHERE | **14** (BOARD B033) |
| members with zero references | 9, 8 dead tree-wide (BOARD B034) |

Already extracted and to be left alone: `EntityRenderer` (13 TUs, reached
only via `entityRenderer_`) and `RendererEventReceiver` (own TU).

### Coupling

| shape | members |
|---|---|
| exactly one family | 79 |
| two families | 63 |
| three or four | 82 |
| five or more (the core) | **55** |

The core is four times Batch K's: `entityRenderer_` (13 families / 270
refs), `driver_` (13/222), `config_` (13/165), `constrainedTextureCache_`
(12/151), `smgr_` (12/108), `zoneShader_` (11/233), `doorManager_`,
`zoneBspTree_`, `regionMeshNodes_`, `currentPvsRegion_` (10 each), then
~45 more at 5–9. **`RendererContext` therefore carries ~55 references and is
the pilot's real deliverable.**

Zone state is one blob jointly owned by six families (`currentZone_`,
`regionMeshNodes_`, `objectNodes_`, `objectInSceneGraph_`, `portalSystem_`,
`currentPvsRegion_`) — it stays on the context, no zone family takes it.

### Families (measured; defs = `IrrlichtRenderer::` bodies)

| family | TUs | defs | single-family members | notes |
|---|---|---|---|---|
| rdp | `renderer_rdp` | 9 | 2 | **L01 pilot** — whole surface under `#ifdef WITH_RDP`; creates `RendererContext` |
| effects | `renderer_effects` | 13 | 9 (`icoLoc*` uniforms) | `tests/cmake/commands.cmake` names the TU (`RENDERER_EFFECTS_SOURCE`) — repoint |
| overlays | `renderer_overlays`, `renderer_hud_env` | 35 | 21 | |
| diagnostics | `renderer_diagnostics`, `renderer_debug` | 28 | 1 | reads 40+ core members; read-mostly, so cheap despite breadth |
| player | `renderer_player_physics`, `renderer_player_collision` + `renderer_camera` | 34 | 3 | ⚠ `test_first_person_camera_single_formula.cpp` names both TUs as strings; `updatePlayerMovement` (806 lines) moves whole |
| slash | `renderer_slash_commands` | 1 (593 lines) | 0 | `commands.cmake` `RENDERER_COMMAND_SOURCE` |
| sim_bridge | `renderer_sim_bridge` | 9 | 2 | |
| zone_stream | `renderer_zone_stream`, `renderer_zone_merge` | 15 | 2 | |
| zone_load | `renderer_zone_load`, `_entities`, `_env`, `_objects` | 21 | 3 | zone blob stays on context |
| zone_lifecycle | `renderer_zone_lifecycle` | 13 | 4 | |
| zone_draw | `renderer_zone_draw` | 10 | 0 | |
| gpu_upload | `renderer_gpu_upload` | 5 | 0 | |
| frame | `renderer_frame`, `_input`, `_render` | 9 | 24 (UI panel state) | **LAST** — the 24 members are what the 11 friend fixtures and the event receiver touch across the thread boundary; `test_ui_hide_command` / `test_static_ui_scroll` read `_input`/`_render` as text |
| init / core | `renderer_init`, `irrlicht_renderer.cpp` | 35 | 8 | ctor/dtor/shutdown/run — stays |

### External surface

229 receiver-filtered call sites in 19 files; `bridge/irrlicht_bridge.cpp`
62, `application_zone_loading.cpp` 42, `application.cpp` 15, the rest tests.
Lifecycle entry points `initLoadingScreen`, `initDeferredGraphics`,
`processFrame`, `shutdown`, `run` stay on `IrrlichtRenderer` (the K11 rule:
`Application` and `IrrlichtBridge` must not learn a component exists).

## Unit table

| unit | family | serialise against | why |
|---|---|---|---|
| L01 | rdp + `RendererContext` | — (pilot, ALONE) | smallest; the context is the work |
| L02 | effects | L01 | 9 private uniforms, one cmake text reader |
| L03 | overlays | L01 | largest single-member family |
| L04 | diagnostics | L01 | breadth test of the context |
| L05 | player + camera | L02–L04 | 806-line method moves whole; two text readers |
| L06 | slash | L05 | one body; `eventReceiver_` traffic |
| L07 | sim_bridge | L05 | |
| L08 | zone_stream | L07 | |
| L09 | zone_lifecycle + zone_draw + gpu_upload | L08 | |
| L10 | zone_load | L09 | 4 TUs; all zone state via context |
| L11 | frame | L10 | thread boundary; friend fixtures retarget |
| L12 | final | L11 | remove transition handle/friends, directory move `src/client/graphics/renderer/`, `irrlicht_renderer.h` ratchet closed |

Ratchet: `include/client/graphics/irrlicht_renderer.h` allowlist entry is
lowered every unit exactly as `eq.h` was. Predict the delta from
declarations + inline accessors + members before dispatch.

## L01 corrections (2026-09-03) — the Batch L template

1. Namespace is `EQT::Graphics` (the renderer's own); no lowercase
   `eqt::graphics` exists and inventing one costs a substitution class.
2. The core is **54 members** (the plan's 52 rows held two multi-member
   lines); `renderer_context.h` needs NO include of `irrlicht_renderer.h` —
   `renderer_types.h` plus 17 forward declarations cover every reference
   binding. `gpuUploadThread_` carries `#ifdef EQT_HAS_GLES2` in both the
   struct and the initialiser list.
3. **Cost scaffolding at +3..+6 lines per construct**, not +1 — the transition
   notes are real lines; L01 predicted −48 and landed −31 with exact
   arithmetic on the deleted blocks.
4. Dropping `irrlicht_renderer.h` from a moved TU drops ~60 transitive
   includes; expect "incomplete type" errors and add the direct includes
   (renderer version of K03 e).
5. `IOutputRenderer` virtual overrides (`setCurrentTarget`,
   `clearCurrentTarget`, `toggleHUD`, `toggleNameTags`, `toggleWireframe`,
   `dumpScene`, `getMemoryReport`…) MUST stay on `IrrlichtRenderer` as
   forwarders — the interface is the sanctioned surface, like K11's
   `Application` entry points. Count them per unit.
6. Tests bind the context by MUTATION through public accessors, not by
   address equality on private members.
7. A component that names no `IrrlichtRenderer` private static/nested
   type/constant needs no `friend` (K13 ii) — `RdpComponent` has none.

## Per-unit plan files

`refactor/L01_rdp_component.md` … written after K13, from a re-measurement
at that tip. L01's plan must specify the `RendererContext` member list (the
55) with each member's exact type.
