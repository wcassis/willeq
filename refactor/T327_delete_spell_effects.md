# T327 — delete the unreached spell-effects processor

**Slot: slot1. Branch `task/T327` cut from `fable` at `09c99b0b`. x86 only.
No Pi, no eqemu.**

Closes **board item 44** of `refactor/deferred.md`. Will ruled on it directly
(2026-09-02), after being shown what is in the file: **"delete it all."**

This is T324's unit run one layer down. `refactor/T324_delete_spell_type_processor.md`
deleted this class's only consumer yesterday, deliberately stopped at this
boundary because Will's ruling then named one class, and boarded the rest as
item 44. Method here is T324's: establish the full extent of the newly-dead
subtree first, delete only what is genuinely unreachable, say where the line
is drawn, and leave a tombstone.

## Why

Re-verified at this tip (`09c99b0b`), not taken on trust:

- `EQ::SpellEffects` appears at exactly four production sites outside its own
  two files: the forward declaration (`include/client/eq.h:48`), the getter
  `EverQuest::GetSpellEffects()` (`eq.h:1067`), the member declaration
  (`eq.h:2078`) and the `make_unique` (`src/client/eq.cpp:258`).
- `GetSpellEffects()` has **zero callers** anywhere in `src/`, `include/`,
  `tests/`, `benchmarks/` or `tools/`.
- `EffectResult` is named only inside `spell_effects.{h,cpp}`.
- `processSpell()`, `processEffect()`, `calculateEffectValue()` and all 49
  `handle*` entry points have no caller outside `spell_effects.cpp`. Their one
  external consumer was `SpellTypeProcessor`, deleted by T324.
- `spell_effects.cpp` has **no entry point from the server-packet path**: it
  publishes no bridge event, pushes no intent, implements no action-handler
  virtual, and calls no `EverQuest::Send*`. It reaches `EverQuest` at exactly
  two lines — `GetEntityID()` (line 575) and `AddChatSystemMessage()` (581).
- **All 49 `handle*` methods are logging stubs.** Not one applies an effect:
  `handleHeal` does not heal, `handleTeleport` does not teleport. T318 found
  the same at shrink/grow/illusion and established why — the server is
  authoritative and broadcasts the change itself.
- **The `BuffManager*` constructor argument is never dereferenced.**
  `m_buff_mgr` is stored and used at zero sites in the file, so even the one
  collaborator the class was given was never actually used.
- No test builds or includes it: it appears in no `tests/cmake/*.cmake` source
  list and no test source includes `spell_effects.h`.

## Scope of the dead subtree (established before deleting anything)

| symbol / file | callers outside `spell_effects.cpp` | verdict |
|---|---|---|
| `EQ::SpellEffects` (whole class, 49 handlers) | none (its one consumer was deleted by T324) | **DELETE** |
| `EQ::EffectResult` | none | **DELETE** (same header) |
| `EverQuest::GetSpellEffects()`, `m_spell_effects`, the fwd decl | none | **DELETE** |
| `include/client/spell/spell_effects.h` + `src/client/spell/spell_effects.cpp` | — | **DELETE** |
| `getStatName()`, `formatModifier()` (file-static in the .cpp) | none — TU-local | **DELETE** with the file |
| `EQ::BuffManager` | `eq.cpp`, 20+ `eq_*.cpp`, `spell_cast_checks.cpp`, `buff_cancel_request.cpp`, `action_dispatcher.h`, 4 test targets | **KEEP — LIVE** |
| `EQ::SpellDatabase` | `SpellManager` (`spell_manager.h`), `BuffManager`, `willeq_database.cpp`, `spell_visual_fx*.cpp`, 8 test targets | **KEEP — LIVE** |
| `EQ::SpellEffectSlot` (`spell_data.h`) | `spell_database.cpp`, `tests/test_spell_database.cpp` | **KEEP — LIVE** |
| `EQ::ResistType` (`spell_constants.h`) | `spell_database.cpp`, `spell_visual_fx.cpp`, `spell_visual_fx_nodes.cpp` | **KEEP — LIVE** |
| `EQ::SpellEffect` enum (`spell_constants.h`) | `eq.cpp` vision-buff recalc, `SpellManager`, `BuffManager` | **KEEP — LIVE** |
| `EQT::Graphics::Environment::SpellEffectsConfig` | the whole particle stack | **KEEP — unrelated**, only the name collides |

**Where the line is drawn, and why:** at the boundary between the class and
its two collaborators. Everything `SpellEffects` was *given* (`BuffManager`,
`SpellDatabase`) and everything it *read* (`SpellEffectSlot`, `ResistType`,
`SpellEffect`) has live consumers reached from `/cast`, a gem click, the buff
window, or the particle system — `SpellManager` and `BuffManager` keep all of
it alive. Nothing is deleted that any of them can reach. Only symbols whose
last caller was inside the deleted file itself go.

⚠ `SpellEffectsConfig` (graphics, `config/spell_effects.json`, particle
presets) is a different class in a different namespace that merely shares a
prefix. It is untouched.

## Changes

- delete `include/client/spell/spell_effects.h`,
  `src/client/spell/spell_effects.cpp`
- `CMakeLists.txt`: drop the TU from `willeq_core`
- `include/client/eq.h`: drop the forward declaration, the getter, the member
- `src/client/eq.cpp`: drop the include and the construction; extend the T324
  tombstone to cover this deletion (and fix its stale "item 37" reference,
  which should read item 44)
- drop the now-dangling `#include "client/spell/spell_effects.h"` from the ten
  `eq_*.cpp` TUs that carried it
- `CLAUDE.md`: the `SpellEffects` bullet struck / put in past tense
- `tests/test_entity_size_change.cpp`: its header comment names
  `spell_effects.cpp` in the present tense — prose only, put in past tense
- `refactor/deferred.md`: item 44 CLOSED with Will's ruling and the commit; new
  item 45 boarding the `calculateEffectValue` formula finding

**No test target changes and therefore no `build/bin/` pruning.** Checked
before deleting: no `tests/cmake/*.cmake` lists `spell_effects.cpp` and no
test source includes the header, so nothing under `build/bin` becomes stale.
(T324's trap — cmake never prunes `bin/` — does not apply here, but was
checked rather than assumed.)

## The finding that must survive the deletion

⚠ **`SpellEffects::calculateEffectValue` was WRONG against the server, and it
was never validated because nothing called it.** Checked line by line against
eqemu's `Mob::CalcSpellEffectValue_formula`
(`/mnt/projects/akk-stack/code/zone/spell_effects.cpp:3513`):

- **Every linear formula is shifted by one slot.** fable's 101 (`base + level`)
  is eqemu's 102; fable's 102 (`base + 2L`) is eqemu's 103; 103→104, 104→105.
  eqemu's own 101 is `base + level/2`, which fable has nowhere.
- **107 and 108 are not level formulas at all.** fable has
  `base + (level - 12)` / `base + (level - 16)`; eqemu's 107/108 are
  tick-based decay over the buff's remaining duration
  (`ubase - ticdif`, `ubase - 2*ticdif`, where `ticdif` comes from
  `CalcBuffDuration_formula`), and they set `degenerating_effects`.
- **109-113, 119, 121-125 all disagree**: eqemu's 109 is `base + level/4`,
  110 is `base + level/6`, 111 is `base + 6*(level-16)`, 112 is
  `base + 8*(level-24)`, 113 is `base + 10*(level-34)`, 119 is `base + level/8`,
  121 is `base + level/3`, 122 is the "splurt" tick decay (`ubase - 12*ticdif`),
  123 is `random(ubase, |max|)`, and 124/125 are `>level 50` step-ups.
  fable has a plausible-looking made-up rule for each.
- **The `updownsign` mechanism is absent entirely**, along with the
  `ubase = abs(base)` normalisation it depends on. eqemu sets
  `updownsign = -1` when `max < base && max != 0` — the case that makes snares
  and damage spells grow *downward*. Without it every such spell scales the
  wrong direction.
- **Formulas 0, 1-99 (`base + level*formulaID`), 60, 70, 114-118, 120, 126-144,
  201/203 and the 1000-1999 splurt family are all missing**, falling through to
  a bare `result = base`.

**Same defect class as the body-type tables T308/T314 fixed:** written from
something other than the server, and never validated because nothing called
it. **If client-side effect calculation is ever wanted, the formulas must be
transcribed from `Mob::CalcSpellEffectValue_formula` — NOT recovered from this
file or from its git history.** Boarded as item 45 so it does not disappear
with the code, and named in the tombstone.

## Acceptance

- `cmake --build build -j24` PASS
- `bash scripts/build-gles2-x86.sh` **before** ctest
- `ctest -j6` (not -j24) — zero new failures vs `refactor/test_baseline.txt`
- visual gate PASS, goldens untouched
- `bash scripts/build-arm-noble.sh` PASS
- `check_file_sizes.sh --files` no new FAILs
- `tests/test_reachability_sweep.cpp` unaffected — checked before the delete:
  the deleted TU pushes no event, pushes no intent, implements no
  `IActionHandler` virtual and calls no `EverQuest::Send*`, so none of the
  sweep's four inputs change. The `SpellBodyTypeGuard` source scan over
  `src/client/spell/` + `include/client/spell/` only loses files.
- ⚠ Tombstone wording must not quote any token a test or plan greps for.

---

## Review (slot1, 2026-09-02)

Done as planned, no deviations. Branch `task/T327` from `fable` at `09c99b0b`.

### Was it reachable after all? No.

Four independent checks, all negative:

1. `GetSpellEffects` — zero occurrences outside its own declaration across
   `src/`, `include/`, `tests/`, `benchmarks/` and `tools/`.
2. The class compiled and linked into `willeq_core`, so any caller anywhere
   would have been a build error on deletion. **The x86 build, the GLES2 build
   and the ARM cross-build all went green on the first attempt, with zero
   call-site fixes and zero test-file fixes** — a stronger result than T324,
   which at least had two test files naming its class.
3. `tests/test_reachability_sweep.cpp` — all four sweeps still pass unchanged.
   Predicted before deleting (the removed TU pushed no event, pushed no intent,
   implemented no action-handler virtual, and called no sender on this class);
   confirmed after.
4. Full suite: no test outside a single prose comment referenced it, and no
   `tests/cmake/*.cmake` compiled the TU.

### Two things found during execution that the board did not know

- **All 49 `handle*` methods were logging stubs.** Not one applied an effect.
  T318 had found this independently for three of them (shrink, grow, illusion)
  and established why — the server broadcasts the change itself. It was true of
  the entire bank.
- **The `BuffManager*` the class was constructed with was never dereferenced.**
  `m_buff_mgr` appears at exactly two lines: the constructor parameter and the
  initialiser. The one collaborator the class was given was never used, which
  is its own small piece of evidence that the file was never finished, let
  alone wired.

### Where the line was drawn, and who keeps what alive

Deleted: only symbols whose last caller lived inside the deleted file —
`SpellEffects`, `EffectResult`, the accessor, the member, the forward
declaration, and the two TU-static helpers (`getStatName`, `formatModifier`).

Kept, with the live consumer named:

| kept | kept alive by |
|---|---|
| `EQ::BuffManager` | `eq.cpp`, 20+ `eq_*.cpp`, `spell_cast_checks.cpp`, `buff_cancel_request.cpp`, `action_dispatcher.h`, 4 test targets — the buff window and T277's click-off path |
| `EQ::SpellDatabase` | `SpellManager`, `BuffManager`, `willeq_database.cpp`, `spell_visual_fx*.cpp`, 8 test targets |
| `EQ::SpellEffectSlot` (`spell_data.h`) | `spell_database.cpp`, `tests/test_spell_database.cpp` |
| `EQ::ResistType`, `EQ::SpellEffect` (`spell_constants.h`) | the particle stack, `SpellManager`, `BuffManager`, and `eq.cpp`'s vision-buff recalculation |
| `EQT::Graphics::Environment::SpellEffectsConfig` | the whole particle system — a different class in a different namespace that only shares a prefix |

`SpellManager` and `BuffManager` are both LIVE and neither lost anything.

### Verification bar

| step | result |
|---|---|
| 1. `cmake --build build -j24` (GLES2-config tree) | **PASS**, EXIT=0 on the first attempt. No exit 143 anywhere in this unit, so no truncation scan was owed; the ARM `willeq` was `readelf`-checked anyway (valid ELF32 ARM, little endian). |
| 2. `bash scripts/build-gles2-x86.sh` — run **BEFORE** ctest | **PASS**, EXIT=0 |
| 3. `ctest --test-dir build -j6 --output-on-failure` | **PASS** — 3497/3498. Sole failure is the documented ALSA host quirk `AudioManagerTest.PreloadCommonSounds`. **Zero new failures vs `refactor/test_baseline.txt`.** |
| 4. Visual gate (`visual_gate`, test 3495, inside the ctest run) | **PASS**, 22.6 s. Goldens untouched — `git status` shows no golden modified. |
| 5. `bash scripts/build-arm-noble.sh` | **PASS**, EXIT=0 |
| 6. `check_file_sizes.sh --files` (7 touched files) | 4 ok, 2 warn, 1 fail — **all three pre-existing**. `CMakeLists.txt` (2098) and `include/client/eq.h` (2635) were already WARN and this unit only made them shorter. `refactor/deferred.md` was already FAIL at 10,344 lines on `fable`; it is a research record the board's own rules forbid shrinking. **No new FAILs.** |

### Suite count

3498 before, **3498 after — unchanged**. No test was deleted, edited for
compilation, or lost. The only test touched is a three-line prose correction in
`tests/test_entity_size_change.cpp`'s header comment, which named
`spell_effects.cpp` in the present tense. **No test in the tree was pinning any
behaviour of the deleted class**, vestigial or otherwise — a cleaner result
than T324, which had to delete `test_spell_group_targeting` and three body-type
cases. Nothing of genuine test value was lost in this unit.

Consequently **no `build/bin/` pruning was owed**: no test target was removed,
so T324's stale-binary trap does not apply here. This was checked before
deleting (no `tests/cmake/*.cmake` lists the TU, no test includes the header),
not assumed after.

### Size

**1,176 lines deleted, 281 added, net -895** across 19 files. Production code
alone: 947 (`spell_effects.cpp`) + 173 (`spell_effects.h`) = **1,120 lines of
unreachable production code removed**, plus 13 lines of declarations and stale
includes, against 57 lines of tombstone. Everything else is plan and board
prose.
