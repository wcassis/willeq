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

## Review

Executed 2026-09-04 in `wt/slot3`, branch `task/G02-process-bridge-intents`
off `fable` at `e06237c7`. Structural, move-only. No STOP condition hit.

### Arm census (measured mechanically at the tip, correction 1)

A parser walked `ProcessBridgeIntents` from its signature to the visitor's
close, matching `^\t{3}(else )?if constexpr \(std::is_same_v<T, eqt::events::(\w+)>\) \{$`
and taking each arm's body up to its own `\t\t\t}`.

| class | count | disposition |
|---|---|---|
| `is_same_v` arms | **36** | all became handlers |
| arms with a body beyond a single statement | **18** | named private handler, body verbatim (largest: `TargetIntent` 64 statements, `BankCurrencyConvertIntent` 48, `HotbarActivateIntent` 47) |
| arms that are a single statement (plus, often, a leading comment) | **18** | **also** a named private handler — ONE RULE |
| terminal `else` (`LOG_TRACE "unhandled intent type"`) | 1 | **deleted**, see below |
| `RendererIntent` alternatives | **36** | one arm each, no gaps, no duplicates |

The plan's arm count of 36 was right (unlike G01's) — but it was re-derived,
not inherited, and `grep -c 'is_same_v<T, eqt::events::'` returns **38** on
the pre-G02 file, because the banner comment quoted the pattern twice. The
plan's shape clause ("single-call arms MAY stay inline") turns out to bear on
**half the arms**, not a handful: the split is 18/18, which is why the "pick
ONE rule" instruction mattered here more than the count did.

**The `else` fallback was deleted, deliberately.** All 36 alternatives had
arms, so `std::visit` could never reach it — it was dead. The new overload
set has no generic lambda, which converts that dead runtime trace into a
**compile error**: adding an alternative to `RendererIntent` without a
handler now fails to build (proved below). Net behaviour change: none
reachable.

**No local spans arms** (the STOP condition): the old arms shared only `i`
and `T`, both per-alternative. Confirmed by the verbatim check passing with
a pure dedent and zero substitutions.

### Shape delivered

- **36 handlers**, all `void eqt::bridge::BridgeComponent::on<Alternative>(const eqt::events::<Alternative>& i)`,
  private, declared in `include/client/bridge/bridge_component.h`, reached
  only from `ProcessBridgeIntents`. Each arm's leading comment moved with it
  and is now the handler's doc comment.
- **ONE RULE, no exceptions.** The plan's preferred option — every arm is a
  handler — was taken, so the visitor reads as a table. The rule costs
  exactly one trivial forwarder: `onPlayerPositionChanged` is
  `OnGraphicsMovement(i);`, because S2's movement handler already existed
  under a different name. A hole in the table would have been worse than a
  one-line body, and the coverage test's arm/declaration/definition triple
  only works if there are no exceptions.
- **The visitor**: `std::visit(overloaded{...}, intent)` over a file-local
  `overloaded` in an anonymous namespace (C++17, so with the explicit
  deduction guide). Each arm is one **canonical line**:
  `[this](const eqt::events::X& i) { onX(i); },` — 36 of them, in the
  variant's own order. That canonicality is what both text nets match on.
- **`ProcessBridgeIntents`: 590 lines → 47.** (Old span 109–698 inclusive;
  new 745–791.) Under the plan's < 60 target; 36 of the 47 are the arms.
- `src/client/bridge/bridge_component_intents.cpp`: 712 → **803** lines.
  `include/client/bridge/bridge_component.h`: 305 → **355** (+50: 36
  declarations, a 12-line block comment, the `renderer_intents.h` include
  with its 3-line reason, minus the `PlayerPositionChanged` forward
  declaration those replace). Plan predicted ~+40 for ~36 declarations; the
  extra 10 are comment. Both files far under the ceiling.
- The header now `#include`s `client/events/renderer_intents.h` instead of
  forward-declaring one intent struct. That header is dependency-free
  (`<cstdint>`, `<string>`, `<variant>`, `hotbar_slot_type.h`) and the
  component's TUs all included it transitively already.

### Substitutions inside moved bodies

**Zero.** Every one of the 36 bodies is byte-identical modulo three tabs of
dedent (preprocessor lines, which sit at column 0, were left at column 0).
Correction 4's `break`→`return` hazard does not arise here: a visitor arm has
no arm-terminating `break`, and the `return;`s that were early exits inside
lambdas (e.g. `onTargetIntent`'s "don't target, we're initiating trade")
mean exactly the same thing inside a `void` handler.

### Proof

**(a) Line multiset over the TU** (`git show fable:…` vs the new file, each
line `strip()`ed, `Counter` difference both ways). Full residual, every line
attributed:

```
ADDED: 143 lines
     38  blank line (36 handler separators + 2 around the banners)
     36  handler signature
     36  dispatch line (the canonical [this](const eqt::events::X& i) { onX(i); },)
     28  comment (rewritten file banner + the handler-block banner + the
         dispatcher's own note)
      4  the file-local overloaded helper (namespace {, struct, guide, })
      1  std::visit(overloaded{

REMOVED: 52 lines
     36  arm head (if/else if constexpr (std::is_same_v<T, ...>) {)
     11  comment (the file-banner lines the rewrite replaced)
      1  std::visit([this](const auto& i) {
      1  using T = std::decay_t<decltype(i)>;
      1  else {                                  } the dead terminal fallback
      1  LOG_TRACE(MOD_MAIN, "Bridge: unhandled intent type");   }
      1  }                                       } (its closing brace)
```

Residual outside those buckets: **0 lines, both directions.** The 36 arm
`{`/`}` pairs and the 36 handler `{`/`}` pairs cancel exactly; the only
unmatched brace is the deleted fallback's.

**(b) Handler-body verbatim check.** Each of the 36 handler bodies was
reconstructed from the new file and compared line-for-line against its arm as
it stands on `fable`, after removing three leading tabs from each body line:
**36 checked, 0 mismatches.**

**(c) The coverage test** — `tests/test_bridge_intent_table.cpp` (223 lines,
6 cases, registered in `tests/cmake/commands.cmake`). **No golden file: the
variant IS the golden.** The alternative list is parsed out of
`renderer_intents.h` as text, and case 1 cross-checks that parse against
`std::variant_size_v<eqt::events::RendererIntent>` — so a mis-parsed or
unreadable header cannot let the other five pass vacuously (correction 5's
trap, met without a golden file to keep in step).

1. `VariantParseAgreesWithTheVariantItself` — parse size == `variant_size_v`
   (36), > 20, no duplicate names;
2. `EveryIntentHasExactlyOneCanonicalDispatchLine` — **exactly one** line
   equal to `[this](const eqt::events::X& i) { onX(i); },`. Exactly-one
   catches a duplicated arm; the handler name being *inside the matched line*
   catches an arm wired to a neighbour's handler;
3. `EveryIntentHasExactlyOneHandlerDeclaration` — in the component header;
4. `EveryIntentHasExactlyOneHandlerDefinition` — in the intents TU;
5. `NoIntentHandlerWithoutAnAlternative` — the other direction: the set of
   `void eqt::bridge::BridgeComponent::on*(` definitions equals the
   alternative set exactly (this is the T264 stale-handler class);
6. `ProcessBridgeIntentsIsPureDispatch` — the pump body is < 60 lines,
   contains no `is_same_v` anywhere, and its arm count equals
   `variant_size_v`.

What the compiler already covers is deliberately *not* duplicated here: an
alternative with no arm does not compile (fault A below). What the compiler cannot see
— an arm calling the wrong handler, a handler nothing dispatches, a body
creeping back into the visitor — is what cases 2, 5 and 6 are for.

**(d) The reachability sweep, widened and re-proved (correction 21 / G01 6).**
`EveryIntentHasAConsumerArm` and `ProducerlessIntentsAreExactlyTheBlockedList`
both matched `"is_same_v<T, eqt::events::" + n + ">"`, which G02 deleted. Both
now call one new helper:

```
std::string intentArm(const std::string& n) {
    return "[this](const eqt::events::" + n + "& i) { on" + n + "(i); }";
}
```

**This is not a weakening of the floor.** The old substring occurred only
inside the visitor; so does this one. Matching the bare type name, or the
handler signature `void eqt::bridge::BridgeComponent::onX(const eqt::events::X& i)`,
would each have let a handler that nothing dispatches pass — the new matcher
contains `[this](`, which no signature does, so a dispatched-nowhere handler
still fails. The sweep's own header comment (its item 1) was repointed from
the pre-K11 filename `eq_bridge.cpp` to the real TU while there.

**Planted faults, in the CODE, both directions (correction 6):**

- **A — delete a real arm.** Removed the `BuffCancelIntent` dispatch line.
  Three independent nets fired: `ReachabilitySweep.EveryIntentHasAConsumerArm`
  FAILED naming the exact expected line;
  `BridgeIntentTable.EveryIntentHasExactlyOneCanonicalDispatchLine` and
  `.ProcessBridgeIntentsIsPureDispatch` FAILED (36 expected, 35 found); and
  the **build itself failed** — `no type named 'type' in
  std::invoke_result<overloaded<…>, const eqt::events::BuffCancelIntent&>`,
  i.e. `std::visit` could not find an alternative's overload. Restored, green.
- **B — an invented handler.** Added
  `void BridgeComponent::onZombieIntent(const eqt::events::TargetIntent&)`,
  declared and defined, dispatched by nothing. **It compiles clean** (exit 0)
  — which is exactly the point: this is the regression a compiler cannot
  catch, and it is what a deleted intent leaves behind.
  `BridgeIntentTable.NoIntentHandlerWithoutAnAlternative` FAILED with
  "found 37, expected 36"; the reachability sweep passed (blind to it, as
  designed — its job is the other direction). Restored, rebuilt, 6/6 green.

**(e) Text-reader check (correction 7) — POSITIVE, and larger than the plan
said.** `grep -rl ProcessBridgeIntents tests/ scripts/ tools/ docs/` finds
**15 pre-existing test files**, not the eight the plan named. The seven the
plan missed are `test_train_skill`, `test_spellbook_memorize`,
`test_read_book`, `test_position_frame_roundtrip`, `test_tradeskill_combine`,
`test_scribe_scroll`, `test_ui_sound_events`. All fifteen only *call* the
public pump, so none needed an edit; all fifteen were run and pass (counts in
the table below). `grep -rl bridge_component_intents` over the same roots
finds only `test_reachability_sweep.cpp` (widened) and the cmake fragment.
`is_same_v<T, eqt::events` now occurs **exactly once tree-wide**, in the
sweep's own explanatory comment.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | **EXIT 0** (clean, then again after the configure that registers the new test). No new warnings — the only warnings in the log are the pre-existing FreeRDP `sspi.h` deprecations. |
| `ctest -j6 --output-on-failure` | **3743/3744 passed**; sole failure `AudioManagerTest.PreloadCommonSounds` (the known ALSA host quirk). 3744 tests, was 3738 — the 6 new coverage cases. |
| `tests/test_bridge_intent_table` | **6/6 passed** |
| `tests/test_reachability_sweep` | **5/5 passed** (56 s) |
| the eight intent-driving suites the plan named, assertions untouched | `test_bridge_component` 9/9, `test_buff_click_off` 21/21, `test_combat_manager` 66/66, `test_eq_action_handler_cast` 32/32, `test_eq_bank_coins` 13/13, `test_eq_commands_equip` 57/57, `test_loot_window_actions` 18/18, `test_merged_help` 24/24 |
| the seven the plan missed | `test_train_skill` 24/24, `test_spellbook_memorize` 20/20, `test_read_book` 12/12, `test_position_frame_roundtrip` 13/13, `test_tradeskill_combine` 23/23, `test_scribe_scroll` 21/21, `test_ui_sound_events` 13/13 |
| `check_file_sizes.sh --all` | **checked 861 files: 861 ok, 0 warn, 0 fail** |
| hygiene re-run after the `docs/module_map.md` edit | `FileSizeCeiling` + `DocCitations` **2/2 passed** |
| call-graph line multiset | residual fully attributed, **0 unexplained lines** both directions |
| handler-body verbatim | **36/36, 0 mismatches, 0 substitutions** |
| planted faults | A (deleted arm): 3 test failures **+ a build failure**. B (invented handler): compiles clean, caught by the coverage test. Both restored. |
| visual gate / ARM | **skipped with evidence**: `git diff --name-only` (plus the one untracked file) lists only `include/client/bridge/bridge_component.h`, `src/client/bridge/bridge_component_intents.cpp`, `tests/cmake/commands.cmake`, `tests/test_reachability_sweep.cpp`, `tests/test_bridge_intent_table.cpp`, `docs/module_map.md`, this plan. **Zero files under `src/client/graphics` or `include/client/graphics`.** |

### Residual (not fixed here — outside territory)

Two docs carry a stale intent count, both predating G02 and both wrong at
`fable` already: `CLAUDE.md` ("35 intent structs in the `RendererIntent`
variant") and `include/client/bridge/CLAUDE.md` ("## Intent Types (35)",
whose own bullet list already enumerates 36 including
`PlayerModelGeometryChanged`). The measured count is **36**. Left alone: the
plan's territory is `docs/module_map.md`, and `DocCitations` does not flag
them. Worth a one-line fix in a later unit.

### Corrections to the Batch G template

- **(8) The plan's text-reader list is a LEAD, not a census — run correction
  7's grep even when the plan already lists the readers.** G02's plan named
  eight tests that drive `ProcessBridgeIntents`; the grep found fifteen. Eight
  would have been a 47%-complete regression check. Correction 7 said to run
  the grep "even when negative"; G02 says run it even when the plan looks
  positive and complete.
- **(9) Prefer a shape whose exhaustiveness the COMPILER enforces, then test
  only what the compiler cannot see.** Replacing the `if constexpr` chain's
  terminal `else` with a fallback-free `std::visit` overload set turned "a new
  intent silently logs a trace" into "a new intent does not build" — proved by
  planted fault A, which failed the build before any test ran. The coverage
  test then spends its six cases on the *residual* risks (wrong handler
  called, orphan handler, body creeping back), not on re-proving the compiler.
  A `switch`-and-table shape (G01's) has no such property; a variant visitor
  does, and it is free.
- **(10) When "every arm is a handler" collides with an existing handler under
  another name, add the one-line forwarder — do not carve an exception.** G02's
  `onPlayerPositionChanged` exists only to call the pre-existing
  `OnGraphicsMovement`. The exception would have saved one line and cost the
  coverage test its arm/declaration/definition triple, which is what makes a
  copy-paste arm detectable at all.
- **(11) "Widen the matcher" must be argued as a non-weakening, in the test's
  own comment, naming the matchers that WOULD have weakened it.** G02 matches
  the whole canonical dispatch line; matching the bare type name or the
  handler signature would each have admitted a handler nothing dispatches.
  The comment says so at the helper, so the next widening starts from the
  floor rather than from the substring.
- **(12) A coverage net does not need a golden file when the type system holds
  the list.** G01 needed `zone_opcodes_g01.txt` because `case` labels are not
  a type. Here `std::variant_size_v` is the count and the header is the names,
  so case 1 pins the text parse to the real type and correction 5's empty-file
  trap is met with no file to keep in step. Plant the faults in the code
  either way (correction 6 is unaffected).
