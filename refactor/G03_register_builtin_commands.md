# G03 — `registerBuiltinCommands`: one list becomes per-family registrations, ORDER PRESERVED

Batch: `refactor/G_oversize_methods.md` (G01 corrections 1–7). Two-wide
with G02. Measured at `32e2adab`: `src/client/action/command_processor_registration.cpp`
(792 lines); `CommandProcessor::registerBuiltinCommands` is a sequence of
~142 registration calls. **Registration order is load-bearing** (J04: later
registrations shadow earlier names; T097/T098 audited shadowing). Re-measure
mechanically.

## Shape

1. Cut the sequence at its existing banners into `registerXCommands()`
   private methods (movement, social, world, bank, loot, train, spells/GM,
   …— use the banners that are there; do not invent families), each a
   verbatim slice; `registerBuiltinCommands` becomes the ordered list of
   those calls. **No reordering of any call across a cut.**
2. Proof: (a) the line multiset (every registration line moves once);
   (b) **the order test**: `tests/test_command_registration_order.cpp`
   snapshots the registry's listing (names in registration order — read
   how `merged_help.cpp`/`test_merged_help.cpp` enumerate the registry)
   against a golden `tests/goldens/command_registration_order_g03.txt`
   extracted at `fable` BEFORE the change (from a run of the old code in
   the test's own fixture, or from `git show fable:…` parsed for the
   registration names in order); assert the golden is non-empty and the
   right size in its own case (G01 5); plant the fault in the code by
   swapping two adjacent registrations across a cut and confirm the test
   fails (G01 6).
3. `test_merged_help` and `test_command_registry_*` (grep) must pass with
   assertions untouched; `test_reachability_sweep` may scan this TU for
   registration calls — read it, widen if needed.

Predicted: `registerBuiltinCommands` 768 → the number of families (≈ 8–12
lines); `command_processor.h` gains ≈ 10 declarations.

STOP: a registration depends on a local defined earlier in the sequence
that a cut would separate (then the cut moves, not the call); the order
golden differs after the change for any reason.

Territory: `src/client/action/command_processor_registration.cpp`,
`include/client/action/command_processor.h`, the new test + golden +
cmake, `tests/test_reachability_sweep.cpp` (widening only),
`docs/module_map.md`. No graphics files.
