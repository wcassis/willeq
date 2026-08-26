# T253 — the hotbar Spell arm has its own cast-refusal literals (T251 finding)

Slot: slot1. x86. NO eqemu. Class A, tiny. Territory: src/client/eq_bridge.cpp
`HotbarActivateIntent` → `HotbarSlotType::Spell` arm (~1309) only, tests.
Do NOT touch inventory_manager.* (T252 slot3), combat.* / eq_movement
(T254 slot2).

## Gap
T244 unified two refusal sites through `castRefusalStringId`; T251 added
`castRefusalMessage()` for the named-component form. A THIRD site — the
mouse hotbar Spell arm — still switches on its own literals and says
nothing for ComponentMissing / OutOfRange / etc.

## Scope
1. Route the arm through `EQ::castRefusalMessage(*this, result)` exactly
   like the other two; delete the literals.
2. Test in tests/test_eq_action_handler_cast.cpp (or the hotbar harness):
   hotbar Spell click with a missing reagent → "You are missing X."; out of
   range → 100. Red/green.
3. Grep for any FOURTH site (`"Cannot cast spell"` / `CastResult::` switch)
   and list it if found.

## Verification
Full ctest; Review; evidence review/T253/. Commit:
`T253: hotbar spell clicks use the shared cast-refusal message`.

## Review (2026-08-26, slot1, x86, no eqemu)

### What changed
- `src/client/eq_bridge.cpp` `HotbarActivateIntent` -> `HotbarSlotType::Spell`
  arm: the three literals (`"Insufficient mana"`, `"Spell not ready"` for
  GemCooldown, `"Already casting"`) are gone; the arm now does
  `EQ::castRefusalMessage(*this, result)` exactly like the CastSpellIntent
  arm (~1020) and `EqActionHandler::beginCastFromGem()`. `SpellNotReady`
  (no official string, `castRefusalStringId` returns 0) keeps the same
  local literal the other two sites use.
- Wording change worth knowing: a hotbar click on a gem in cooldown now says
  236 "Spell recast time not yet met." (the table's string) instead of the
  literal "Spell not ready" -- that is the point of the unit, the three
  sites no longer drift.
- `include/client/spell/cast_refusal.h`: header comment records the third
  site.
- `tests/test_eq_action_handler_cast.cpp`: two new tests push a real
  `HotbarActivateIntent{type=Spell,id}` through `ProcessBridgeIntents()`:
  - `HotbarSpellClick_ReagentCountShort_SaysMissingNamedItem` -- Bone Walk
    (351) in gem 3 with one Bone Chip of the two required -> "You are
    missing Bone Chips.", nothing sent, not casting.
  - `HotbarSpellClick_TargetOutOfRange_SaysOutOfRange` -- ranged
    single-target spell, target 500 units away -> "Your target is out of
    range, get closer!" (eqstr 100), nothing sent.

### Red / green (measured)
- Red: `review/T253/red_hotbar.txt` -- both tests FAIL on the pre-T253 arm,
  `lastSystemMessage()` is `""` in both (the arm said nothing for
  ComponentMissing / OutOfRange).
- Green: `review/T253/green_cast_full.txt` -- test_eq_action_handler_cast
  32/32 (the 30 prior + 2 new).

### Fourth site
None. `review/T253/fourth_site_grep.txt` (grep for `castRefusalMessage(`,
`"Cannot cast spell"`, `"Insufficient mana"`, `"Already casting"`,
`"Spell not ready"` across src/ + include/): the only producers of a
refusal chat line are the three now-unified sites. The remaining
`"Spell not ready"` / `"Cannot cast spell"` literals are the documented
no-official-string fallbacks (SpellNotReady / InvalidSpell / default) in
`eq_action_handler.cpp` and the two bridge arms, all sitting BEHIND the
shared call. `spell_cast_checks.cpp` returns `CastResult`s, emits no text.

### Verification
| step | result |
|---|---|
| `cmake --build build -j24` | pass (`review/T253/build_green.log`) |
| full ctest | 2936/2937 -- sole failure AudioManagerTest.PreloadCommonSounds (documented ALSA quirk) (`review/T253/ctest_full.txt`) |
| check_file_sizes.sh fable | 0 FAIL (eq_bridge.cpp / inventory_manager.cpp pre-existing WARNs, neither grown by this unit beyond +8 lines) |
| build-gles2-x86.sh / visual gate / ARM | not run -- Class A tiny, no rendering or platform surface touched; plan's bar is "full ctest" |

### Deviations
None. Territory respected: inventory_manager.*, combat.*, eq_movement
untouched.
