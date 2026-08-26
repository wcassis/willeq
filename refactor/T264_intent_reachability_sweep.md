# T264 — intent/handler reachability sweep (patterns #12, #13, #15 were found one at a time)

Slot: slot3. x86. NO eqemu. Class A. Territory: read the whole tree; EDIT
only (a) clearly dead producer-less intents / caller-less handlers
(delete, with the grep proof), (b) `InventoryManager::getItemById()`
returning `false` instead of `nullptr` on miss (T251 note, tiny), tests.
Do NOT touch willeq_database / builder / EQE1 resolver (T262 slot1). If a
dead intent looks like it SHOULD have a producer (a user-facing feature
that cannot be reached), do NOT wire it here — list it as a class-A
finding with the feature it blocks.

## Method (mechanical, exhaustive — this is the point)
1. `RendererIntent` variant (include/client/events/renderer_intents.h,
   ~51 structs): for each, grep producers (`pushIntent(`, `emplace`, the
   struct name constructed) outside tests, and consumers (the `case` /
   `std::visit` arm in eq_bridge.cpp ProcessBridgeIntents). Table:
   intent | producers (file:line) | consumer | verdict.
2. `GameEventType` (102 enumerators): publishers (`pushEvent(` with the
   type) and subscribers/cases (IrrlichtBridge switch, ConsoleRenderer,
   AutomatedMode, GameStateSyncBridge residue). Same table.
3. `IActionHandler` pure virtuals: which `ActionDispatcher` method calls
   each, and which command/hotkey/intent reaches that dispatcher method
   (CommandProcessor registration, InputActionBridge, hotkey table).
   Same table — this is where #12/#15 lived.
4. `EverQuest` public `Send*` methods: callers outside tests (T155/T168
   did this once; re-run it — T168 deleted the last five, more may have
   appeared).
Deliverables: the four tables in the Review; deletions for the dead ones
that block nothing; a class-A list for the dead ones that block a
feature; a test that pins reachability for anything you wire or keep
(e.g. a compile-time or gtest table check — see T223's drift-guard test
for the pattern) so the sweep never has to be repeated by hand.

## Verification
Full ctest; Review; evidence review/T264/ (the raw greps). Commit:
`T264: intent/handler reachability sweep -- <n> dead deleted, <m> blocked features listed`.
