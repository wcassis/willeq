# G02 — `ProcessBridgeIntents`: the `std::visit` visitor becomes one `onXIntent` per arm

Batch: `refactor/G_oversize_methods.md` (G01 corrections 1–7 are the
template). Two-wide with G03 (disjoint: `src/client/bridge/` vs
`src/client/action/`). Measured at `32e2adab`:
`src/client/bridge/bridge_component_intents.cpp` (712 lines),
`BridgeComponent::ProcessBridgeIntents` from line ~109; **36 visitor arms**
over the `RendererIntent` variant (`include/client/events/renderer_intents.h`).
Re-measure mechanically (correction 1).

## Shape

1. Every arm with a body beyond a single call becomes a named private
   `void BridgeComponent::onXIntent(const XIntent&)`, body verbatim.
   Single-call arms may stay inline in the visitor or become handlers —
   pick ONE rule and apply it to all (prefer: every arm is a handler, so
   the visitor is a pure dispatch and reads as a table).
2. The visitor: `std::visit(overloaded{ [&](const XIntent& i){ onXIntent(i); }, … }, intent)`
   — or a table keyed by `variant::index()` if the tree already has a
   pattern for that (read `irrlicht_bridge.cpp`'s event switch and
   `test_reachability_sweep.cpp`'s intent-consumer scan first: **the sweep
   counts consumer arms by text** — `EveryIntentHasAConsumerArm` — so the
   new shape must keep exactly one recognisable arm per intent, and the
   sweep's matcher is widened + re-proved with a planted missing arm
   (correction 21 / G01 6).
3. Proof: the call-graph multiset over the TU (G01's form: every added
   line accounted for as signature/brace/dispatch-line; every removed line
   as absorbed); and a coverage test `tests/test_bridge_intent_table.cpp`:
   every alternative of the `RendererIntent` variant has a handler (use
   `std::variant_size` + a per-index probe, or the sweep's own list) — no
   golden file needed because the variant IS the golden.
4. Text readers: eight tests name `ProcessBridgeIntents`
   (`test_bridge_component`, `test_buff_click_off`, `test_combat_manager`,
   `test_eq_action_handler_cast`, `test_eq_bank_coins`, `test_eq_commands_equip`,
   `test_loot_window_actions`, `test_merged_help`) — they drive intents
   through it; assertions untouched.

Predicted: `ProcessBridgeIntents` 580 → < 60 (the dispatch); ~36 handlers
declared in `include/client/bridge/bridge_component.h` (+~40 lines;
report the count; it is under the ceiling).

STOP: an arm's body needs a local that spans arms (there should be none
in a visitor); a driving test's assertion would change; the sweep cannot
be widened without weakening its floor.

Territory: `src/client/bridge/bridge_component_intents.cpp`,
`include/client/bridge/bridge_component.h`, `tests/test_bridge_intent_table.cpp`
(new) + cmake, `tests/test_reachability_sweep.cpp` (widening only),
`docs/module_map.md`. No graphics files: visual gate + ARM skipped with
evidence.
