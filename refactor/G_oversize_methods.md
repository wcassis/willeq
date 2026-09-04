# Batch G — The oversize methods: decompose the eight 546–880-line bodies

Successor to Batches K and L. Every one of these now sits inside a
component with a bounded header, so a unit can read the whole class it
edits. Same rules: one method per unit, executors in slots, the full
verification bar, and a proof — here the proof is a **call-graph
multiset**: the concatenation of the extracted step bodies, sorted, equals
the original body sorted, minus exactly the step-call lines and the step
signatures (K-batch multiset with a different normalisation).

## The eight (re-measure at dispatch; locations at `69c6164c`)

| method | where now | lines | shape | decomposition |
|---|---|---|---|---|
| `ZoneComponent::DispatchZonePacket` | `src/client/zone/zone_dispatch.cpp` | ~880 | one `switch` over opcodes, ~100 arms | **a table**: `static const std::unordered_map<uint16_t, void (ZoneComponent::*)(const Packet&)>`; arms with logic beyond a call become named private handlers first. Batch G's pilot — it is the most mechanical |
| `PlayerCameraComponent::updatePlayerMovement` | `src/client/graphics/player/renderer_player_physics.cpp` | ~806 | sequential phases (input → intent → collision → water → ground → send) | one private method per phase, a `MovementFrame` struct carrying the locals that cross phases; ⚠ `test_first_person_camera_single_formula` and `test_first_person_body_draw_rule` pin behaviour and read the TU as text — correction 21 |
| `CommandProcessor::registerBuiltinCommands` | `src/client/action/command_processor_registration.cpp` | ~768 | a list of `registry.add(...)` calls | **registration ORDER is load-bearing** (J04): split into per-family `registerXCommands()` called in the SAME order; a test pins the order by diffing the registry's listing before/after |
| `Application::initialize` | `src/client/application.cpp` | ~569 | sequential init steps with early returns | one private step per subsystem returning `bool`; the top-level becomes a list |
| `BridgeComponent::ProcessBridgeIntents` | `src/client/bridge/bridge_component_intents.cpp` | ~580 | a `std::visit` visitor with one arm per intent | one private `onXIntent(const XIntent&)` per arm; the visitor becomes a dispatch; `test_reachability_sweep` scans intent arms by text — widen (21) |
| `IrrlichtRenderer::processFrameRender` (→ `FrameComponent` after L11) | `src/client/graphics/frame/renderer_frame_render.cpp` | ~546 | sequential render passes | one private pass per method; `test_ui_hide_command` / `test_static_ui_scroll` read the TU by text — widen (21) |
| `loadModelFromGlobalChrWithVariantsForAnimation` | `src/client/graphics/eq/variant_loading_animation.cpp` | ~815 | asset loading pipeline | phases: resolve → load → variants → animation attach; ⚠ EQE1 all-or-nothing rule (Will's standing decision) must survive unchanged |
| `createDefaultConfig` | `src/client/graphics/detail/detail_zone_configs.cpp` | ~548 | a data table | **leave** (J tracker's ruling); optionally move the table to a data file in a later, non-G unit |

## Order

G01 `DispatchZonePacket` (pilot, alone) → G02 `ProcessBridgeIntents` +
G03 `registerBuiltinCommands` (two-wide, disjoint) → G04 `Application::initialize`
+ G05 `processFrameRender` (two-wide) → G06 `updatePlayerMovement` (alone;
the camera guards) → G07 the variant loader (alone).

## Unit plan template

Measured at the tip: the method's line range, its locals that cross phase
boundaries (the struct), every early `return`/`break` (they constrain the
cut points), every text reader of the TU, the tests that pin behaviour.
Predicted: the number of steps and their names. Proof: the call-graph
multiset. STOP: a step needs a local the struct cannot carry without a
lifetime change; a pinned test's assertion would change.

## G01 corrections (2026-09-04) — the Batch G template

1. Re-measure the arm/step classes MECHANICALLY at the tip (a parser, not a
   grep); G01's plan said 101/68, the parse said 104 pure / 3 no-op / 38 logic.
2. A "deliberate no-op" is a comment and a `break`, nothing else; an arm that
   logs is a handler.
3. A captureless lambda in the initialiser of a STATIC MEMBER FUNCTION gives
   class scope: one function-pointer type covers in-family and sibling arms,
   zero forwarders, and the table lives behind the accessor so the dispatcher
   stays under 40 lines.
4. Tell an arm-terminating `break;` from a loop `break;` (indentation) before
   any `break`→`return` pass.
5. The golden list is asserted non-empty and the right size in its own case
   (the empty-file trap, K11 ee); store `<value> <label>`.
6. Plant the fault in the CODE, both directions (delete a real entry; add an
   invented one), not in the golden.
7. The text-reader check (TU filename AND method name across `tests/`,
   `scripts/`, `tools/`, docs) is per-unit; record it even when negative.

## G03 corrections (2026-09-04)

8. **A killed `ar` can leave the archive PRESENT and non-empty with ZERO
   members**; the ~30 undefined-reference link errors that follow read as
   a source defect. After any exit 143: `ar t build/lib/*.a | wc -l` per
   archive, not a filename check. (Protocol amendment.)
9. Check what the CONTAINER preserves before designing an order test —
   `std::map` registries cannot witness registration order; parse the TU.
10. The families are the file's own banners (21), not a planned count.
11. One-registration banners fold into the preceding family rather than
   becoming one-line functions.
12. Plant the CALL-ORDER fault too (swap two family calls), not only a
   registration swap.
