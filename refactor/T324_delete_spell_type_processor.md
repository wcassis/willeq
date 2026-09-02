# T324 — delete the unreached spell-type processor

**Slot: slot1. Branch `task/T324` cut from `fable` at `bcbf2ddc`. x86 only.
No Pi, no eqemu.**

Closes **board item 33** of `refactor/deferred.md`. Will ruled on it directly
(2026-09-02): the class can be deleted.

## Why

T314 (`refactor/T314_spell_targeting_correctness.md`, Review §5) found the
whole class unreached from production. Re-verified at this tip:

- `m_spell_type_processor` exists at exactly three sites — the `make_unique`
  in `src/client/eq.cpp:264`, the getter in `include/client/eq.h:1069`, and
  the member at `eq.h:2080`.
- `GetSpellTypeProcessor()` has **zero callers** anywhere in `src/`,
  `include/`, `tests/` or `benchmarks/`.
- `update()` is never ticked; `processSpell()` is never invoked.
- The live cast path is `EqActionHandler`/`eq_bridge` →
  `SpellManager::beginCastFromGem()` → `validateCast()` →
  `isValidCreatureTarget()`, which does not go through this class.
- The server is authoritative: eqemu resolves targets and sends `OP_Action`,
  `OP_Damage`, buff packets and appearance packets per target. The client
  displays what it is told. Same conclusion T318 reached for shrink/grow.

## Scope of the dead subtree (established before deleting anything)

| symbol | callers outside its own TU | verdict |
|---|---|---|
| `EQ::SpellTypeProcessor` (whole class) | none | DELETE |
| `EQ::RainSpellInstance` | none (only the class) | DELETE (same header) |
| `EverQuest::GetSpellTypeProcessor()` | none | DELETE |
| `EQ::SpellEffects` (whole class) | **only** the class above | see line below |
| `EQ::BodyTypeSatisfiesTargetType()` etc. | `SpellManager` — **LIVE** | KEEP |
| `include/client/eq_body_types.h` | `SpellManager`, `eq_strings.cpp`, combat | KEEP |

**Every public method of `SpellEffects` was checked individually**
(`processSpell`, `processEffect`, `calculateEffectValue`, and all 40-odd
`handle*` entry points): none has a caller outside `spell_effects.cpp` other
than the class being deleted. `spell_effects.cpp` has **no** entry point from
the server-packet path — it publishes no bridge event, pushes no intent, and
calls only `EverQuest::AddChatSystemMessage()` and `GetEntityID()`.

## Where the line is drawn, and why

**Delete the type processor. Do NOT delete `SpellEffects` in this unit.**

Will's ruling names one class. `SpellEffects` becoming callerless is a *new*
finding of the same class-C shape (wire it in or delete it), not something
covered by the existing ruling, and it is a much larger surface — ~1,120 lines
of per-SPA handlers plus a chat-message path — that a future client-side
prediction or effect-display layer is the obvious consumer for. Under
`refactor/EXECUTOR_PROTOCOL.md` an executor escalates product/design calls
rather than making them. It is boarded as a new item with a recommendation and
named in the tombstone, so the next reader cannot miss it. `m_spell_effects`
stays constructed; nothing else changes about it.

## Changes

- delete `include/client/spell/spell_type_processor.h`,
  `src/client/spell/spell_type_processor.cpp`
- `CMakeLists.txt`: drop the TU
- `include/client/eq.h`: drop the forward declaration, the getter, the member
- `src/client/eq.cpp`: drop the construction, leave a tombstone
- drop the now-dangling `#include` from the 12 `eq*.cpp` TUs that carried it
- `tests/test_spell_group_targeting.cpp`: deleted with the code it pins
- `tests/test_spell_body_type_targeting.cpp`: the three `SpellTypeProcessorBodyType`
  cases removed; the other 8 (the predicate table, the `SpellManager` cases and
  the source guard) are untouched and still cover the live consumer
- `tests/cmake/spells.cmake`: drop the deleted target
- `CLAUDE.md`, `include/client/eq_body_types.h`,
  `include/client/spell/spell_body_type_target.h`: struck / put in past tense
- `refactor/deferred.md`: item 33 CLOSED; new item 37 for `SpellEffects`

## On the deleted tests

`tests/test_spell_group_targeting.cpp` was written by T314 *yesterday* and is
being deleted the day after. It is **not** vestigial in the sense of testing
nothing — it was a good test of a real contract, and T314 proved its negatives
fail on the old code. It is deleted only because the code it pins is gone;
there is no way to keep a test of a class that does not exist. The contract it
encoded (eqemu `Group::CastGroupSpell()` — caster unconditional, every other
member range-checked against `Mob::GetAOERange()`, membership by spawn id with
a name fallback) is transcribed in full in T314's Review and is the reference
if the behaviour is ever rebuilt.

The three `SpellTypeProcessorBodyType` cases in
`tests/test_spell_body_type_targeting.cpp` are a different matter: they
asserted that the *deleted* consumer had stopped accepting everything.
`SpellBodyTypeTarget.*` and `SpellManagerBodyType.*` in the same file assert
the same predicate on the consumer that is live, so nothing is lost.

## Acceptance

- `cmake --build build -j24` PASS
- `bash scripts/build-gles2-x86.sh` **before** ctest
- `ctest -j6` — zero new failures vs `refactor/test_baseline.txt`
- visual gate PASS, goldens untouched
- `bash scripts/build-arm-noble.sh` PASS
- `check_file_sizes.sh --files` no new FAILs
- `tests/test_reachability_sweep.cpp` unaffected — checked before the delete:
  neither deleted TU pushes an event or an intent, implements an
  `IActionHandler` virtual, or calls any `EverQuest::Send*`, so none of the
  sweep's four inputs change.

---

## Review (slot1, 2026-09-02)

Done as planned. Branch `task/T324` from `fable` at `bcbf2ddc`.

### Was it reachable after all? No.

Nothing found during execution contradicts the deadness finding. The four
independent checks:

1. `GetSpellTypeProcessor()` — zero occurrences outside its own declaration
   across `src/`, `include/`, `tests/` and `benchmarks/`.
2. The class compiled and linked into `willeq_core`, so any caller anywhere
   would have been a build error on deletion. The x86 build, the GLES2 build
   and the ARM cross-build all went green on the first attempt with no
   call-site fix in any of them. **The only compile failures were the two test
   files that named the class directly.**
3. `tests/test_reachability_sweep.cpp` — all four of its sweeps still pass
   unchanged (`ReachabilitySweep.EveryEventIsPublishedAndConsumed`,
   `EverySenderHasACaller`, and the intent/action-handler pair). Predicted
   before deleting: the removed TU pushed no event, pushed no intent,
   implemented no `IActionHandler` virtual, and called no `EverQuest::Send*`
   (it reached `EverQuest` only for `GetEntities`, `GetEntityID`,
   `GetGameState`, `GetPosition`). Confirmed after.
4. Full suite: no test outside the two named files referenced it.

### One thing the guards caught

The first full `ctest` run failed `check_build_tree_orphans` — **correctly**:
`build/bin/test_spell_group_targeting` was still sitting in the build tree
after its target was removed, since cmake never prunes `bin/`. A binary pinned
to deleted sources that still runs and still passes is exactly what that guard
exists for. The stale binary and its discovery shims were deleted and the
suite re-run clean. Worth recording for the next unit that deletes a test
target: **deleting the source and the `ADD_EXECUTABLE` is not enough; remove
`build/bin/<target>`, `build/tests/<target>[1]_{tests,include}.cmake` and
`build/tests/CMakeFiles/<target>.dir` too.**

### Verification bar

| step | result |
|---|---|
| 1. `cmake --build build -j24` (GLES2-config tree) | **PASS**, EXIT=0 (436 s; no exit 143 anywhere in this unit, so no truncation scan was owed — the ARM `willeq` was `readelf`-checked anyway: valid ELF32 ARM) |
| 2. `bash scripts/build-gles2-x86.sh` — **run BEFORE ctest** | **PASS**, EXIT=0 |
| 3. `ctest --test-dir build -j6 --output-on-failure` | **PASS** — 3493/3494. The sole failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds`. Zero new failures vs `refactor/test_baseline.txt`. |
| 4. Visual gate (`visual_gate`, inside the ctest run) | **PASS**, 30.5 s. Goldens untouched — `git status` shows no golden file modified. |
| 5. `bash scripts/build-arm-noble.sh` | **PASS**, EXIT=0 |
| 6. `check_file_sizes.sh --files` (6 touched files) | **4 ok, 2 warn, 0 fail.** Both WARNs (`include/client/eq.h` 2643, `CMakeLists.txt` 2098) are pre-existing and this unit only made them shorter. |

### Suite count

3507 → **3494**, i.e. **-13**, and all thirteen are accounted for: the 10 cases
of `tests/test_spell_group_targeting.cpp` and the 3 `SpellTypeProcessorBodyType`
cases. No other test changed state.

### Size

**1,221 lines deleted, 226 added, net -995** across 24 files. Production code
alone: 668 (`spell_type_processor.cpp`) + 179 (`spell_type_processor.h`) = **847
lines of unreachable production code removed**, plus 277 test lines and 52 lines
of test edits, against ~35 lines of tombstone and doc prose.

### Kept, and for whom

- `include/client/spell/spell_body_type_target.{h,cpp}` and
  `include/client/eq_body_types.h` — **`EQ::SpellManager` uses them, and it is
  reachable** (gem click / `/cast` → `beginCastFromGem()` → `validateCast()` →
  `isValidCreatureTarget()`). Untouched except for two provenance comments put
  into the past tense.
- The other 8 cases of `tests/test_spell_body_type_targeting.cpp` — the
  37-row predicate table, the `SpellManagerBodyType` cases against the live
  consumer, and `SpellBodyTypeGuard.NoBTConstantOrBareBodyTypeIntegerSurvivesInSpellCode`, the source-scanning guard that keeps the numbering in one home. All
  still pass.
- `EQ::SpellEffects` — kept deliberately; see the line drawn above and item 37.

### Deviations

None from the plan. One judgement call, flagged rather than absorbed: the
`SpellEffects` half of the dead subtree was **not** deleted, because Will's
ruling named one class and this is a second class-C decision. Boarded as
item 37 with an executor recommendation to delete.
