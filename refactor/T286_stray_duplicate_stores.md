# T286 — sweep EverQuest's remaining duplicate/stray state stores (T280/T283 pattern)

Slot: slot3. x86. NO eqemu. Class A. Territory: `include/client/eq.h`
members and their `PlayerState`/`GameState` counterparts, the PlayerProfile
parse that feeds them, tests. Do NOT touch graphics (T284 slot2, T285
slot1), inventory.

## Pattern
T283 found `EverQuest::m_practice_points` duplicating
`PlayerState::m_practicePoints` (one stale reader, every user-visible read
going through the other). T280 found the same shape. The 2026-08-25
decision says state is written at the source into ONE instance — this unit
checks whether more of these exist.

## Method (mechanical, like T264's sweep)
1. For every scalar/string member of `EverQuest` that names player state
   (level, class, race, hp, mana, currency, flags, ids, zone, target...),
   find whether `PlayerState`/`CombatState`/`WorldState`/etc. holds the same
   value, and list: member | counterpart | writers of each | readers of each.
   T271 already found `m_class`/`m_level` being read where `GetClass()`/
   `GetLevel()` was meant (T276 fixed one instance) — start there.
2. For each duplicate: delete the `EverQuest` copy and route its readers to
   the state object, UNLESS the wire path genuinely needs a private staging
   value (say which and why).
3. A pinning test in the T264 style: parse the sources and assert the known
   duplicate set is empty (or exactly the justified exceptions), so this
   cannot silently regrow.

## Verification
Full ctest; Review with the table; evidence review/T286/. Commit:
`T286: player state has one store per value`.
