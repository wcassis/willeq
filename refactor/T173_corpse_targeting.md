# T173 — /target ranks a living entity above its own corpse

**Slot: slot1. x86. No Pi, no server** — the defect is client-side name
resolution and the acceptance is unit tests measured against `fable`.

**Territory:** `refactor/T173_corpse_targeting.md`,
`src/client/state/entity_manager.cpp`, the extracted
`corpse_predicate.{h,cpp}`, `src/client/combat.cpp` (annotation only),
`tests/test_state_modules.cpp`, and the CMake lists that build the new TU.

## The bug

`EntityManager::findEntityByName` returned the **first** entity whose lowercased
name *contained* the search string, in `std::map` (spawn-id) order. Every corpse
of X is named `X's corpse<id>`, so every corpse of X contains X — and whenever
the corpse sat at a lower spawn id than the living X standing next to it,
`/target X` resolved to the corpse.

This is not theoretical. It fired **three times in the single T172 session** —
each of probe 3's reverse inspect leg, and it was harmless only in probe 4
because the target's identity did not matter there — and T161 lost a `#kill` to
the same shape the day before (`/target Testmelee` + `#kill` matched
`Testmelee's corpse26`). Two separate live units bled on it.

## The fix

`findEntityByName` now **ranks** candidates and returns the best, lowest tier
winning:

| tier | match | example |
|---|---|---|
| 0 | exact name, not a corpse | `/target Testmelee`, Testmelee alive |
| 1 | exact name, a corpse | `/target "Testmelee's corpse237"` |
| 2 | substring, not a corpse | `/target Testme` |
| 3 | substring, a corpse | the only tier the old code effectively had |

A corpse always loses to a non-corpse **at the same quality of match**, and an
exact match always beats a substring. **Tier 1 above tier 2 is deliberate:**
naming a corpse exactly must still reach it, or `/consider` and `/loot` lose
their subject. `m_entities` is a spawn-id-keyed `std::map`, so within a tier the
first-seen candidate is the lowest spawn id — the result is deterministic and
does not move between runs. An exact living match (tier 0) short-circuits.

**"Corpse" is the ONE shared predicate, not a fourth copy.** T160 established
corpse recognition (the flag, the `Spawn_Struct` NPC field, the three name
forms) and centralised it precisely so copies could not drift. The state layer's
`eqt::state::Entity` and the client's `Entity` are different types, so to reuse
that predicate without a second copy the field-based core moved to
`corpse_predicate.{h,cpp}`:

- `IsCorpseByFields(is_corpse_flag, npc_type, name)` — the predicate on the three
  fields any entity representation carries.
- `ParseCorpseName(...)` — moved verbatim, T160's three separators and its
  mandatory-trailing-digits false-positive guard unchanged.
- `combat.cpp`'s `IsCorpseEntity(const Entity&)` is now a one-line delegate.

The header carries a **do-not-re-derive** warning aimed at exactly the tempting
mistake: in the state layer `npcType == 2` alone *looks* sufficient and silently
misses every corpse whose `npc_type` we never saw (one already lying there at
zone-in, or one known only through `OP_MobRename`). The tests cover both of
those routes.

## The second resolver — left alone, annotated

There is a **second** name resolver, `CombatManager::SetTarget(const
std::string&)` — exact-then-substring, no corpse ranking, and case-*sensitive*
where `findEntityByName` is case-insensitive (T97 flagged that divergence; still
open). It was **not** fixed, because it is **unreached**: its only caller is
`EqActionHandler::targetEntityByName`, which itself has zero production callers
(the reachability pattern again). Fixing dead code proves nothing and would have
meant a second copy of the ranking on a third Entity type; deleting the whole
`targetEntityByName` path crosses three layers and is a disposition call, not a
bug fix. It now carries a comment saying so and warning against wiring it
without the ranking. **Disposition owed: wire it through a shared ranking, or
delete it.**

## Acceptance

- **New tests measured against old code.** Seven `EntityManagerTest.FindEntity*`
  cases; reverting only `entity_manager.cpp` to `fable` fails **4 of them**
  (`PrefersLivingOverOwnCorpse`, `CorpseRecognisedByNpcTypeAlone`,
  `CorpseRecognisedByFlagAlone`, `ExactBeatsSubstring`) and passes the three that
  assert preserved behaviour (corpse still found when alone, exact corpse name
  still reaches the corpse, ties resolve to lowest spawn id). Restored → all 7
  green. The failing-against-baseline set is the proof the tests bite.
- **Suite** `ctest` **2649/2651**, the two standing failures both pre-existing
  and unrelated: the documented ALSA `AudioManagerTest.PreloadCommonSounds`, and
  `visual_gate` on a **befallen p95 CPU 40.242 ms vs a 40.144 ms ceiling** — a
  0.2% timing flutter on a shared box that **passes cleanly when run alone**
  (18 s, 0 failures) and touches no code this unit changed.
- **ARM cross-build** clean (`scripts/build-arm-noble.sh`) — the new TU is
  dependency-free and compiles for armhf.
- **Size gate:** `combat.cpp` stays a pre-existing WARN (2170 lines, was already
  over 1500 on `fable`); this unit made it **shorter**, not longer, by moving the
  predicate out. The other three changed files are ok.

## Still owed / follow-ups

- The dead `targetEntityByName` / `SetTarget(name)` path: wire or delete.
- The case-sensitivity divergence between the two resolvers (T97), now moot for
  the live path but still real for the dead one.

## Deviations

None. Scope held to the resolver, the predicate extraction it required, and the
one annotation.
