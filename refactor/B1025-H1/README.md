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

The official client stores opaque source-specific values such as
`Page3Button1=E12` in `UI_<Char>_<server>.ini` (H-O `persistence.md:31`).
Fable's model instead holds typed spell/skill/item IDs. Its JSON twin is
`<cache>/ui/UI_<Char>_<server>.hotbar.json`, sharing the INI's sanitized
identity stem (`include/client/ui_identity_cache_name.h`) but not the INI
writer. Writing an invented conversion into `[HotButtons]` would assert an
unmeasured code taxonomy and conflict with the existing UI-preferences writer.

## Implementation

- `HotbarModel` addresses `{page, slot}`; both are zero-based and checked.
  `currentPage` is model state. The prior `{"buttons":[...]}` shape loads into
  page zero; the new shape writes `{"currentPage":n,"pages":[...]}`.
- `BridgeComponent::LoadHotbarConfig` resets before character load. The old
  `clients[0].hotbar` is imported only when no per-character file exists, then
  removed after a successful new-file write. `SaveHotbarConfig` writes the
  character twin on slot/page changes. No second client-config loader exists.
- Keyboard and mouse Spell activation both call `FindHotbarSpellGem` over
  `GameState::spells()`, so a Spell slot stores a spell ID and resolves the
  currently memorized visible gem. Without a model or matching gem, keyboard
  activation does not guess by button position. The player-profile
  `SpellManager::setSpellGems` load now publishes its initial gems into that
  authoritative state, rather than leaving it empty until a later gem change.

## Verification

The page round-trip, legacy migration, invalid page, event payload, two-character
disk separation, unsafe filename character and keyboard spell-id cases are
asserted by `test_skill_reachability`, `test_bridge_teardown` and
`test_input_action_bridge`. The remaining hotbar handler tests are retained.

## Not verified / left open

- Hotbutton assignment gesture and live relog scenario belong to H-3/H-5,
  not this model leg. No official-client source-code mapping from opaque
  `[HotButtons]` values to Fable's typed IDs was measured.
- The H-O QA/review at MR !165 reported two documentation findings; this card
  does not change that upstream verdict or claim it passed.
