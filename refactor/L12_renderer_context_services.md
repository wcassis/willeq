# L12 — Batch L close-out: census the transition handle, retire what is cheap, board the residue

The renderer's K13. **Runs ALONE after L11.** Method: K13's, verbatim
(`refactor/K13_context_services.md`), applied to `IrrlichtRenderer`:

1. Measure `owner_.` across `src/client/graphics/*/` by target group:
   sibling-component accessors (→ `ctx_.<Family>()` pointers set after
   construction), the 39-member "below-threshold but shared" set L04/L07
   reached via `owner_` (→ promote into `RendererContext` those used by
   ≥ 3 components NOW that the components exist — re-measure breadth per
   component, not per TU family), stayed-behind methods (list each; the
   frequent ones become context services or move to the component that
   owns their state), private statics/nested types (K13 ii axis 2).
2. Remove `friend class` lines only where zero qualified-private uses
   remain; remove the `IrrlichtRenderer&` ctor parameter only where zero
   `owner_.` uses remain (`RdpComponent` qualifies today).
3. Directory move: the remaining flat `renderer_*.cpp` (`renderer_init`,
   `irrlicht_renderer.cpp`, `renderer_entities`, `renderer_entity_load_gate`,
   `renderer_event_receiver`) into `src/client/graphics/renderer/`; text
   readers repointed (correction 27's full list; `tools/hwcaps/*` comment
   citations swept — L07 deviation 1).
4. The `irrlicht_renderer.h` allowlist entry set to the measured count;
   the residue table boarded (one entry, ranked clusters, one reason each).
5. `docs/module_map.md` regenerated for `src/client/graphics/`; `CLAUDE.md`
   §"Core Components" and `src/client/graphics/CLAUDE.md` updated to name
   the components.

STOP: the residue after step 1 exceeds 300; any thread/sync change.

Territory: everything under `src/client/graphics/` and
`include/client/graphics/` (call sites and the moves above), the renderer
tests' call sites, `tools/hwcaps/` comment citations, allowlist,
`docs/module_map.md`, `CLAUDE.md`, `src/client/graphics/CLAUDE.md`,
`refactor/BOARD.md`. Visual gate + ARM NOT skippable.

After L12: **Batch G** — the eight 546–880-line methods
(`DispatchZonePacket`, `updatePlayerMovement`, `registerBuiltinCommands`,
`Application::initialize`, `ProcessBridgeIntents`, `processFrameRender`,
`loadModelFromGlobalChrWithVariantsForAnimation`, `createDefaultConfig`
(a data table — leave)), each now inside a component with a bounded
header, decomposed one per unit with the same move-only proof.
