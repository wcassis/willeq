# T381 — the spell visual vocabulary becomes reachable (B160, T380 Leg B1)

Class A, slot3, no server. Read the B160 board text and
`refactor/T380_spell_visuals_survey.md` §4 Leg B1 (the shape, the
`InventorySlotChanged` precedent, the STOP).

1. Enrich `SpellCastVisualStartedData` / `SpellCastVisualCompleteData`
   (`include/client/state/event_bus.h`) with `resistType`, `school`,
   `spellAffectIndex`, `travelType`, filled on the GAME thread by
   `SpellManager::handleBeginCast` / `handleAction` from its own
   `SpellDatabase` (never hand the db pointer across the bridge — its LRU
   has no mutex). `travelType` needs B163's dropped column: if it is not in
   willeq.db yet, carry `spellAffectIndex` + `resistType` + `school` only
   and leave `travelType` to B163 (say so).
2. In the bridge consumer (`src/client/bridge/irrlicht_bridge.cpp`) and
   `SpellVisualFX`, key colour / spray style / `resolveVisual()` on the
   event payload instead of `m_spell_db`; delete `setSpellDatabase()` and
   `m_spell_db` (zero callers) and every null branch that existed only
   because of them (K12 ff check — the FX layer publishes nothing).
3. Tests, pre-fix failing: a Fire spell and a Cold spell yield DIFFERENT
   particle colours through the event path; a spell whose affect index
   resolves reaches `Resolved` (T70's suppression gates become reachable —
   one test that a gate fires). The intent-table golden
   (`test_bridge_intent_table`) learns any reviewed statement IN PLACE.
4. Strike B160. B161 (dead FX API) stays for a later unit unless a
   function becomes reached here (say which).

## Proof

Build, `ctest -j6`, the tests with pre-fix/post-fix runs, goldens for the
TUs touched, file sizes; `src/client/graphics` changes → visual gate
standalone + ARM (the gate's zones cast nothing, so no golden should move;
if one moves, STOP with the contact sheet — never bless).

STOP: the enriched payload outgrows `GameStateBridge`'s coalescing (report
the size and the coalescing rule); a pixel golden moves.

## Review

Executor session 2026-09-05, slot3, branch `task/T381-spell-visual-vocabulary`,
base `790964a0`. No server (the eqemu lock was held by T377).

### What changed

The renderer had no route to a spell's resist type, school or
`spell_affect_index`, because the only route ever built —
`SpellVisualFX::setSpellDatabase()` — was never called by the client. T381
routes the three facts across the bridge **by value**, on the events that
already exist, the way `CLAUDE.md` describes for `InventorySlotChanged`. The
`SpellDatabase` pointer itself still never crosses: the only instance is
`SpellManager::m_spell_db`, game-thread-owned, and its LRU has no mutex.

| file | change |
|---|---|
| `include/client/spell/spell_visual_vocabulary.h` | NEW. `struct EQ::SpellVisualVocabulary { bool known; ResistType resistType; SpellSchool school; int32_t spellAffectIndex; }` — 8 bytes, trivially copyable, includes only `spell_constants.h`. |
| `include/client/spell/spell_data.h` | `SpellData::visualVocabulary()` — the producer-side conversion, one place. |
| `include/client/state/event_bus.h` | `spellVisual` field on `SpellCastVisualStartedData` and `SpellCastVisualCompleteData`. |
| `src/client/spell/spell_manager.cpp` | fills it at all **four** publish sites: `handleBeginCast` (player branch and NPC branch), `handleAction`, and the cast-timeout path in `update()` (which publishes the same completion event the server's success packet does and would otherwise have shipped a colourless impact). |
| `src/client/bridge/irrlicht_bridge.cpp` | `noteSpellVocabulary(d.spellId, d.spellVisual)` before `createCastGlow()` on Started and before `removeCastGlow()` on Complete (a cast can land without this client having seen its BeginCast). |
| `include/client/graphics/spell_visual_fx.h` | `setSpellDatabase()`, `m_spell_db`, and the `class SpellDatabase` / `struct SpellData` forward declarations DELETED. New `noteSpellVocabulary()` / `spellVocabulary()`; `resolveVisual()` made public (pure query, and the only headless observation point for the T72 gates). |
| `src/client/graphics/spell_visual_fx.cpp` | `resolveVisual()`, `getTextureForSpell()`, `isSprayCastStyle()` keyed on the noted vocabulary; `#include "client/spell/spell_database.h"` removed. |
| `src/client/graphics/spell_visual_fx_nodes.cpp` | `getSpellColor(uint32_t)` keyed on the vocabulary; the `getSpellColor(const SpellData*)` overload replaced by `static colorForVocabulary()`; `spell_database.h` removed. |
| `tools/model_viewer.cpp` | plays the game thread's part: `noteSpellVocabulary()` in `triggerSpellTestCast()` and in the HUD's cast-style readout. |
| `tests/test_spell_visual_vocabulary.cpp` + `tests/cmake/renderer_guards.cmake` | NEW, 6 tests. |

Deleted: `setSpellDatabase()`, the `m_spell_db` member, the
`getSpellColor(const SpellData*)` overload, and the four `if (m_spell_db)`
null branches that existed only for it. `src/client/graphics/` no longer
includes `client/spell/spell_database.h` anywhere.

`travelType` is NOT carried, exactly as the plan's escape hatch allows:
`PRAGMA table_info(spells)` on `data/willeq.db` has `cast_anim` and no
`travel_type` (nor `target_anim`) column, so there is no value to copy. That
is board **B163**, and it bumps `kWilleqDbExpectedVersion`. Adding an
always-zero field would have been a fresh instance of the very defect this
unit removes.

### Payload size against the coalescing rule — no STOP

`GameStateBridge::coalesceEvents()` (`include/client/bridge/game_state_bridge.h`)
inspects **only** `GameEventType::EntityMoved`, reading `EntityMovedData::spawnId`
to keep the last event per entity; every other event type is copied through
untouched. It has no size assumption of any kind. The queue element is
`state::GameEvent`, whose payload is one `std::variant` sized by its largest
alternative.

Measured (a probe TU compiled against `include/` at the base tip and at this
tip, both `g++ -std=c++17`):

| type | base `790964a0` | this tip | delta |
|---|---|---|---|
| `SpellCastVisualStartedData` | 120 B | 128 B | +8 |
| `SpellCastVisualCompleteData` | 80 B | 88 B | +8 |
| `EventData` (the variant) | 280 B | **280 B** | **0** |
| `GameEvent` | 288 B | **288 B** | **0** |
| `EntityMovedData` (the only coalesced type) | 36 B | 36 B | 0 |

Neither spell payload is anywhere near the variant's largest alternative, so
the queue element did not grow at all. Nothing to report as a STOP.

### Tests — pre-fix red, post-fix green

`tests/test_spell_visual_vocabulary.cpp`, run standalone
(`build/bin/test_spell_visual_vocabulary`). Both halves of the production wire
are driven: `SpellData::visualVocabulary()` onto a real
`eqt::state::GameEvent`, then `std::get<>` back out and
`noteSpellVocabulary()` — so cutting either half fails the test.

The pre-fix run was produced WITHOUT `git stash` and without reverting the new
API (which would not compile the test): a patch restored the three consumers'
pre-T381 BEHAVIOUR — `getSpellColor()` returning the default,
`isSprayCastStyle()` returning `false`, `resolveVisual()` pinning the index at
`kSpellsEffNoEffectIndex` — which is exactly what a permanently-null
`m_spell_db` produced in production. Patch kept at
`scratchpad/t381_prefix.patch`; reversed by re-editing, and the tree was
rebuilt and re-run green afterwards.

| test | pre-fix | post-fix |
|---|---|---|
| `EventPayloadCarriesResistSchoolAndAffectIndex` | PASS (producer half; green either way by design) | PASS |
| `FireAndColdSpellsDrawDifferentColours` | **FAIL** — both `(255,150,180,255)` | PASS — Fire `(255,120,50)`, Cold `(100,180,255)` |
| `AnUnnotedSpellKeepsThePreT381Fallback` | PASS (pins that the fallback is preserved) | PASS |
| `SchoolSelectsTheSprayCastStyle` | **FAIL** — spray never selected | PASS |
| `ASpellWithAnAffectIndexReachesResolved` | **FAIL** — `NoAffectIndex`, no stages | PASS — `Resolved`, index 23, Source + Target |
| `TravelStageGateFiresForARecordWithNoMiddleStage` | **FAIL** — `Resolved` unreachable | PASS |

4 of 6 red before, 6 of 6 green after.

Spells used, verified against the shipped data: **93** Burst of Flame (Fire,
index 0), **54** Frost Bolt (Cold, Evocation, index 23), **15** Greater Healing
(Alteration → spray), **3** Summon Corpse (index 4: Source + Target, no
Middle), **6** Ignite Blood (index 20: all three stages), **6594** GM Cleansing
(index −1). Both file-dependent tests `GTEST_SKIP` rather than fail when
`willeq.db` or `spells.eff` is absent.

### The suppression gates: reachable, but only one has a population — B165

The plan asked for "one test that a gate fires". Getting there produced a
finding worth more than the test. Cross-referencing all 256 `spells.eff`
records against the 7,105 spells whose index resolves:

| stages present in the referenced record | spells |
|---|---|
| Source + Middle + Target | 855 |
| Source + Target, **no Middle** | 6,250 |
| any record missing Source, or missing Target | **0** |

So `!isFallback() && !hasSourceStage()` and `!isFallback() &&
!hasTargetStage()` are **still** unreachable after this fix — no spell in the
game references a populated record that lacks a Source or a Target stage. The
only gate with a population is the TRAVEL gate, whose 6,250 spells guard
`createProjectile()` — one of B161's methods, which no client code calls. And
all four gates sit inside `if (m_particle_manager)`, which T124 recorded as
null in production. Boarded as **B165**; the test drives the travel gate
directly through `createProjectile()` with a `ParticleManager` attached,
observing the branch by the entity-position callback the gate returns before
consulting.

### B160's "zero callers tree-wide" was wrong — B166

`SpellVisualFX::setSpellDatabase()` had exactly one caller:
`tools/model_viewer.cpp:2750`. T380's census grepped `src/`, `include/` and
`tests/`. That is the whole reason the defect survived T70/T72: the one place
a developer exercises the spell FX layer by hand is the one place the database
was wired, so the vocabulary looked alive in the tool and was dead in the
game. `model_viewer` now feeds the FX layer the same way the client does.
Boarded as **B166** (P): a zero-caller claim must name the directories it
grepped, and `tools/` must be one of them. B161's own "zero callers tree-wide"
was corrected on the board for the same reason — `model_viewer` calls six of
its seven methods.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` (GLES2 config, tests on) | **EXIT=0** |
| `ctest --test-dir build -j6` | **3,986 of 3,987**; sole failure `AudioManagerTest.PreloadCommonSounds` (the waived ALSA host quirk). ctest exit 8 as documented. |
| golden / order tests standalone (`BridgeIntentTable`, `ReachabilitySweep`, `ZoneDispatch`, `CommandRegistration`, `ApplicationInit`, `FrameRenderOrder`, `MovementOrder`, `SpellVisual*`, `SpellManager*`) | 146 of 146 PASS |
| `scripts/build-gles2-x86.sh` | **EXIT=0** |
| `scripts/ci/visual_gate.sh` | **=== VISUAL GATE: PASS ===**, all three zones + three memory-coverage runs. No golden moved (`git status` shows no file under a golden path modified) — expected, the gate's zones cast nothing. |
| `scripts/build-arm-noble.sh` | **EXIT=0**, `build-arm-noble/bin/willeq` = ELF 32-bit ARM EABI5 |
| `scripts/refactor/check_file_sizes.sh fable` | 23 ok, **1 fail: `tools/model_viewer.cpp` 3,246 > 3,000 — PRE-EXISTING** (3,242 at `fable`, i.e. already over). Net +4 lines; `tools/` is outside the ctest `FileSizeCeiling` (src/include/tests), which passed. No NEW fail. |

Two builds hit the 600 s ceiling (exit 143) and got the protocol's post-143
sweep, which caught both known shapes: the `build/` tree had **22 zero-byte
binaries** behind an otherwise clean incremental state (including `willeq`),
and the `build-gles2/` tree had **`lib/libwilleq_core.a` missing outright**
plus a `lib/stzyfHyJ` `ar` temp. Deleted and rebuilt; post-sweep scan
`checked=280 bad=0`, `ar t libwilleq_core.a | wc -l` = 322 (build) / 318
(build-gles2).

### Owed / deviations

- **Live two-client pair OWED.** The eqemu lock was held by T377 for the whole
  session, so nobody has watched a fire cast and a frost cast side by side.
  The unit test proves the colours differ through the event path; it does not
  prove the pixels do. Noted on B160's close-out.
- `resolveVisual()` moved from private to public. It is a pure query and the
  only way to observe the T72 gates without a GL context; flagged rather than
  absorbed.
- B161 is NOT closed here and no method of it became reached — but its board
  text was corrected (six of the seven have a `model_viewer` caller), which
  changes how Leg B4's "wire or delete" decision has to be framed.
