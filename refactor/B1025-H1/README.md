# B1025-H1 — hotbar model and character storage

Part of #345. This is the model/persistence leg, not the assignment gesture.

## Scope and oracle

The H-O official-client probe found a single visible stock hotbar. Its page
keys and ten pages are recorded in `pages.tsv:2-7` under
`/home/user/kb-evidence/kb-h0-hotbar-oracle/`; the attempted resized
HotButtonWnd2–4 still did not render (`pages.tsv:11`). The 2026-10-13
orchestrator amendment to this card therefore narrows the model to one window,
ten pages and ten slots per page. This is [measured] for the stock UI; no
independent assignment semantics for windows 2–4 are claimed.

The official client stores source-specific values such as `Page3Button1=E12`
in the separate `<Char>_<server>.ini`, not `UI_<Char>_<server>.ini`
(`refactor/B1025-H0/README.md:15`). Fable writes `[HotButtons]` into that
character INI and preserves other sections. The loader retains each value
verbatim and maps the observed E/J/H/F codes to typed slots. This is a mapping
for the six observed values, not a claim to know the complete taxonomy.

## Implementation

- `HotbarModel` addresses `{page, slot}`; both are zero-based and checked.
  `currentPage` is model state. The prior `{"buttons":[...]}` shape loads into
  page zero for legacy import; the importer also accepts the former
  `{"currentPage":n,"pages":[...]}` shape. Character storage is INI.
- `BridgeComponent::LoadHotbarConfig` resets before character load. The old
  `clients[0].hotbar` is imported only when no per-character INI exists, then
  removed after a successful new-file write. A malformed existing INI is
  preserved and reported as a load failure. `SaveHotbarConfig` writes the
  character INI on slot/page changes. No second client-config loader exists.
- Keyboard and mouse Spell activation both use `HotbarModel::findSpellGem` over
  `SpellManager::getMemorizedSpell`, so a Spell slot stores a spell ID and
  resolves the currently memorized visible gem. Without a model or matching
  gem, keyboard activation does not guess by button position. EverQuest wires
  the model's gem reader to its SpellManager, and both input routes call the
  model's `findSpellGem`; no second spell table or later GameState projection
  is involved.

## Verification

The page switching, legacy migration, invalid page, event payload, two-character
disk separation, unsafe filename character and keyboard spell-id cases are
asserted by `test_skill_reachability`, `test_bridge_teardown` and
`test_input_action_bridge`. The persistence test also exercises the actual
`BridgeComponent` legacy import, removal of the old global key, and callback
save after assignment. The remaining hotbar handler tests are retained.

Fix regression baseline: `ctest --test-dir build -R
'PageSwitchDoesNotCarryVisibleCooldown|ExistingInvalidCharacterFileIsNeverReplacedByLegacyImport'
--output-on-failure` failed both new assertions before the production fixes;
the captured log is `/home/user/kb-evidence/kb-h1-hotbar-model/red-fix12.log`.

| Acceptance criterion | Result | Evidence |
|---|---|---|
| One window, ten pages, ten slots; addressed model and event | PASS | `include/client/hotbar/hotbar_model.h:33`; `tests/test_skill_reachability.cpp:391` |
| Per-character INI storage, observed-code round-trip and one-time legacy import | PASS | `src/client/hotbar/hotbar_persistence.cpp`; `tests/test_bridge_teardown.cpp` |
| One spell-ID-to-visible-gem rule for keyboard and mouse | PASS | `src/client/hotbar/hotbar_model.cpp:26`; `src/client/bridge/bridge_component_intents.cpp:744`; `tests/test_input_action_bridge.cpp:559` |
| Relevant hotbar and neighbouring tests at original H1 head | PASS | Captured local CTest selection: 168 passed, two ASAN-only repros skipped (170 discovered). New fix tests: see the RED/GREEN evidence paths below. |
| Hygiene | PASS | Final local hygiene CTest: 31 passed, 0 failed. |
| Golden/budget/third-party files untouched | PASS | `git diff --name-only origin/fable...HEAD` and clean worktree at handoff. |

## Not verified / left open

- Hotbutton assignment gesture and live relog scenario belong to H-3/H-5,
  not this model leg. The observed-code mapping does not establish the full
  official-client code taxonomy.
- The H-O QA/review at MR !165 reported two documentation findings; this card
  does not change that upstream verdict or claim it passed.
