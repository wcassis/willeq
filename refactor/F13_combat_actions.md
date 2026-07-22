# F13 — Split combat-resolution cluster into eq_combat_actions.cpp

Move the combat resolution/loot-corpse packet cluster out of
`src/client/eq.cpp` (10,819 lines). Pure move — no behavior change, no
interface change, no reformatting. (Folding this domain INTO CombatManager is
a future non-mechanical refactor, explicitly not this unit.)

## Scope (orig 17217–17710 @ a0c02cd, ~5 large methods. Enumerate by NAME.)

Move to new `src/client/eq_combat_actions.cpp` the contiguous cluster
containing: the Consider handler, the Action handler, the Damage handler
(~237 lines by itself), the MoneyOnCorpse handler, and the loot-item
handler(s). F07's review notes it deliberately left the loot-corpse family
(`ZoneProcessMoneyOnCorpse`, `RequestLootCorpse`, `LootItemFromCorpse`,
`LootAllFromCorpse`, `DestroyAllCorpseLoot`) for a later unit — if they are
in or contiguous with this cluster, they belong to this unit; enumerate and
document.

## Steps

1. Locate the cluster; list every `EverQuest::` definition found; note
   interleaved out-of-scope methods (leave + document).
2. Create `src/client/eq_combat_actions.cpp`: header comment + pruned
   includes (CombatManager full type likely needed — see F06's combat.h
   precedent).
3. Move verbatim; preserve #ifdef guards exactly.
4. File-local static helpers: usual rule.
5. Add to `CLIENT_CORE_SOURCES` in CMakeLists.txt.

## Verification

- `cmake --build build -j24` clean.
- `ctest --test-dir build` — green modulo documented exceptions in
  refactor/test_baseline.txt.
- `python3 scripts/refactor/verify_move_only.py BASE..HEAD` — PASS or
  documented necessary-include/de-static deviations only.
- `bash scripts/refactor/check_file_sizes.sh --files <abs>/eq_combat_actions.cpp` — ok.
- eq.cpp shrinks by ~450+ lines.

## Review

(executor fills in)
