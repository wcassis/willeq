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
5. ~~`IOutputRenderer` virtual overrides … MUST stay as forwarders~~ **STRUCK
   (L03/L04, B044): `class IrrlichtRenderer` implements NO interface.** A
   forwarder is justified only by its ACTUAL callers — the bridge,
   `Application`, `graphical_mode` (the K11 rule). Measure callers; never
   cite an interface.
6. Tests bind the context by MUTATION through public accessors, not by
   address equality on private members.
7. A component that names no `IrrlichtRenderer` private static/nested
   type/constant needs no `friend` (K13 ii) — `RdpComponent` has none.

## L02 corrections (2026-09-03)

8. **Component includes go OUTSIDE `#ifdef WITH_RDP`** (next to
   `renderer_context.h`). Slot builds have `WITH_RDP=ON` and pass; the
   gles2 tree (no FreeRDP) is the build that catches it.
9. **`EQT_HAS_DRM`-only code is compiled ONLY by the ARM build.** The
   icosphere bodies and `icoLoc*` members exist only under `-DEQT_DRM=ON`;
   `build-arm-noble.sh` is the sole compiler of that third of L02 — prove
   with `strings` on the ARM binary. Never skip ARM for a renderer unit.
10. **A nested type in a moving SIGNATURE is exempt from §3**, and any
   member typed by it follows it — otherwise the component header must
   name `IrrlichtRenderer::Type` and re-include the 60-transitive header.
   `renderer_init.cpp` is a terminal owner: "the last family moves it"
   never fires for a member shared only with init/core.
11. Banners travel with declarations — plan deltas that count declarations
   only are 3–4× pessimistic; count the banner lines between them.
12. `friend class` is needed whenever the component reaches private
   renderer members through `owner_` (axis 1); `RdpComponent` was the
   exception, not the rule.

## L03 corrections (2026-09-03)

13. **`class IrrlichtRenderer` has NO base class.** L01 correction 5's
   "`IOutputRenderer` overrides" premise was false — `setCurrentTarget`,
   `toggleHUD`, `setCameraMode` etc. are plain public methods that
   `IOutputRenderer`'s other implementations happen to share by name.
   Forwarders are still kept for bridge/application/`graphical_mode`
   callers (the K11 rule), but the reason is the caller, not an interface.
   Measure the base class before writing "override".
14. Inline-accessor state (K04 f) again: three single-family members were
   invisible to body counts (`loadingTitle_`, `navmesh_`, `headingDebugText_`).
15. Member BLOCKS split across families are costed per member, not per
   block.
16. The 600 s ceiling kills builds mid-link; the truncation scan must
   cover `build/lib` (`ar` temporaries `st*`) as well as `bin/`.
17. A zero-caller SETTER whose member is read live (`setNavmesh`) is kept
   and boarded — deleting it turns a one-line wiring fix into two files.

## L04 corrections (2026-09-03)

18. The context carried the breadth test: 30 of 54 members via `ctx_`
   (329 uses) and 39 more via `owner_` (254 uses) — do not widen the
   context per unit; the L12 census decides.
19. Frame-phase state (`measureSection`/`sectionStart_`) stays with the
   frame family even when its body sits in another family's TU — relocate
   the body, not the ownership.
20. A "forwarder premise" that is false hides defects: the four debug
   toggles had NO callers on either route (B043). Every "stays as a
   forwarder" claim must name a real caller.

## L05 corrections (2026-09-04)

21. **Text-matching guards must learn the component spelling, and be
   re-proved with a deliberate break.** `test_first_person_camera_single_formula`
   locates the camera body by `IrrlichtRenderer::` and asserts on
   `camera_->` — both break under extraction, and its duplicate-formula
   detector goes BLIND to component-style code (`ctx_.camera->`) across the
   whole batch. Ruling: widen the inputs to both spellings, assert exactly
   one matches, and prove the detector still fires by planting a
   component-style duplicate before committing. Same shape as
   `ReachabilitySweep` (K05 k) and `DuplicateStateStores` (K09 aa).
22. A shared enum named in a moving signature (`CameraMode`) is hoisted to
   `renderer_types.h` with a `using` alias left in the class — every
   existing spelling compiles untouched.
23. Third consecutive unit where a "forwarder" premise was a same-named
   `IOutputRenderer` default body (`cycleCameraMode`): the renderer
   implements no interface; name the real caller or it is not a forwarder.

24. (L05) Findings' comment cost goes in the COMPONENT header, not the
   god header — budget it there; L05 predicted −40 and landed −65.
25. (L05) Drop the "forwarders that stay" column from L plans; list
   callers, and let the executor derive forwarders from real callers only.

## L06 corrections (2026-09-04)

26. **A family that owns NO state costs the god header +3..+7 lines** (the
   include, friend, member, accessor) and there is nothing to delete to
   fund it. The "may not grow" ratchet is a property of the BATCH — the
   header must be lower after every wave than before it — not of every
   unit. Ratified for L06 (+7). `zone_draw` and `gpu_upload` (0
   single-family members) will hit the same; fold them into a
   state-owning neighbour's unit (L09 does) rather than dispatching them
   alone.
27. `DocCitations` is a text reader of SOURCE PATHS: the K03 (c) grep for a
   moved filename must cover `docs/`, `refactor/BOARD.md` and every
   `CLAUDE.md`, not only `tests/`.
28. `test_merged_help` located `processSlashCommand` by owner spelling —
   correction 21's shape again; widened and re-proved with two planted
   breaks.

## L07 corrections (2026-09-04)

29. A friend fixture is only at risk if it names a member the unit MOVES —
   intersect the fixture's member set with the move set in the plan.
30. K04 (f) reads both ways: an inline accessor can make a SHARED member
   look single-family (`loading_`).
31. `owner_` is the answer to a thread-ownership STOP, not a workaround:
   the worker handle stays on the renderer, the component reaches it.
32. A killed `ar` DELETES the archive (protocol's absence case) — assert
   expected artifacts exist before validating.

## L08 corrections (2026-09-04)

33. A family's HEADER need not travel with its TU when it declares nothing
   of the class (`renderer_zone_merge.h`: namespace-scope guards and free
   functions) — leave it and its includers alone.
34. In a multi-class TU, "code lines only" means the MOVING class's lines.
35. A component header that forward-declares its member types needs the
   ctor out-of-line as well as the dtor — the ctor's unwind path destroys
   already-initialised members.
36. A member can look single-family to a body count and be shared by a
   TEARDOWN path (`pvsNeighborhood_`, `pvsNeighborhoodAnchor_`) — check
   `unloadZone`/`shutdown` readers before moving.

## L09 corrections (2026-09-04)

37. Since K16 `IrrlichtRenderer` DOES have a base class (`IZoneLoadSurface`,
   two methods) — measure the base class AND what it declares; correction
   13's conclusion (no interface forwarders) still holds.
38. A nested type named inside a LAMBDA BODY needs the third substitution,
   and that alone makes the friend line's axis 2 live — L12's census must
   count nested-type references, not only `owner_.`.
39. A teardown-only reader is not ownership (`pvsNeighborhood_`,
   `regionNeighbors_`): they stay.
40. **Write the component header first.** The same extraction measured +7
   with the rationale in the god header and −23 with it in the component
   header.

## Per-unit plan files

`refactor/L01_rdp_component.md` … written after K13, from a re-measurement
at that tip. L01's plan must specify the `RendererContext` member list (the
55) with each member's exact type.
