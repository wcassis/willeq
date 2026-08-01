# T24 — SpellManager + BuffManager coverage (spell/combat batch, unit 2 of 3)

**Slot: t1. x86 only — no Pi, no eqemu, no live server.** No sibling
unit; t2 idle.

**Territory: `tests/` ONLY.** No `src/`, no `include/`. Will chose
**Option A** (`refactor/T23_spell_combat_seam_scoping.md`): cover this
layer through the surface that already exists, do NOT extract an
interface. If a source change looks unavoidable, STOP and report — that
report is the evidence that would justify revisiting the decision.

Read `refactor/EXECUTOR_PROTOCOL.md` first, then the scoping doc, then
`refactor/T23_combat_manager.md`'s Review (T23 is the sibling unit that
just landed; its fixture techniques and its two corrections apply here).

## Why this unit

`spell_manager.cpp` (1317 lines) and `buff_manager.cpp` (595) have no
direct test binary. Between them they own gem state, cast validation,
cooldowns, memorization, and every buff timer the UI reads.

## Two traps I verified while scoping — do not rediscover them

**1. `update(float delta_time)` does NOT honor delta_time.** The
parameter is threaded through `update()` → `updateGemCooldowns()` →
`updateMemorization()` and **never read** — all three call
`std::chrono::steady_clock::now()` internally
(`spell_manager.cpp:57/115/133/117`). The API advertises injectable time
and ignores it.

Consequences: cooldown, memorization, and cast-timer behavior is
wall-clock gated exactly like `CombatManager`'s. **Do NOT write
sleep-based tests.** Use the T17 precedent — `#define private public`
around the include and backdate the private timestamps
(`m_gem_cooldown_start[i]`, `m_memorize_start_time`, `m_cast_start_time`)
— see `tests/test_daybreak_state_machine.cpp` for the clean form.

The dead parameter is itself worth a finding: a caller passing a
scaled/paused delta (as a headless or fast-forward mode would) gets
silently ignored. Pin it with a clearly-named test if you can express it
cleanly; if not, write it up in the Review. Do NOT fix it.

**2. `GetReplaySuppressedSendCount()` does NOT observe these sends.**
T23's plan (mine) claimed it would, and that was wrong:
`EverQuest::QueuePacket(uint16_t, DynamicPacket*)` (`eq.cpp:3672`)
returns early on a null zone connection **without incrementing any
counter** — the T05 counter only covers the login/world paths.
`SpellManager::sendCastSpellPacket`/`sendInterruptPacket`/
`sendMemorizePacket` all go through that overload. Do not plan
assertions on it. Observable channels that DO work: the public getters,
the `setOn*` callbacks, and bridge events via `publishGemChanged`
(capture with `TestBridge` from `tests/zone_stream_test_utils.h`).

## Fixture recipe

- **Managers**: `EverQuest eq(...)` gives a live `SpellManager` via
  `eq.GetSpellManager()` (verified by probe, scoping doc).
- **Spell database**: `SpellManager::initialize()` loads from SQLite and
  returns false if absent. `EverQuest::InitializeSpellSystem()` wraps it
  and is what constructs `BuffManager` (`eq.cpp:205`). Follow
  `tests/test_spell_database.cpp`'s `hasDb()` + `GTEST_SKIP` precedent so
  the binary degrades cleanly where `data/willeq.db` is missing.
- **BuffManager standalone**: its ctor takes `SpellDatabase*` directly
  (`buff_manager.h:99`) — it can be constructed in a test with no
  `EverQuest` at all. Prefer that for pure buff tests; it is the cleanest
  isolation available in this layer.
- **Gem/spellbook state**: `setSpellGems()` / `setSpellbook()` are
  **public** (`spell_manager.h:122/125`) — inject state directly, no
  packet replay needed.
- **Mana / level for cast validation**: `checkMana` reads
  `m_eq->GetCurrentMana()` (`spell_manager.cpp:551`), which reads
  `GameState::player()`. `eq.GetGameState().player().setMana(cur, max)`
  is public — set it directly.
- **Entities** (for targeted casts): stage via T23's helpers, now in
  `tests/zone_stream_test_utils.h`.

## Priorities (stop when full — a T24b follow-on is fine, T12 precedent)

1. **Gem state machine.** `memorizeSpell` / `forgetSpell` / `swapGems`,
   `getMemorizedSpell`, `getGemState`, and the invalid-slot boundaries
   (slot ≥ `MAX_SPELL_GEMS`, and slot 0 vs 1 indexing — T16 found the
   0-vs-1-based confusion in this exact area, so pin which convention
   these use).
2. **Cast validation matrix.** `beginCastFromGem` / `beginCastById`
   against each reachable `CastResult`: already-casting, not-enough-mana,
   empty gem, unknown spell, gem on cooldown. This is the highest-value
   section — it is pure decision logic.
3. **Cooldowns + memorization timing** via backdating (trap 1).
   `getGemCooldownProgress` / `getGemCooldownRemaining` /
   `getMemorizeProgress` boundaries, and the Refresh→Ready transition
   including its `publishGemChanged` bridge event.
4. **Interrupt + server packet handlers.** `interruptCast`,
   `handleBeginCast`, `handleAction`, `handleInterrupt`,
   `handleManaChange` — these are the server-driven paths and are pure
   state updates, ideal to assert.
5. **Callbacks.** `setOnCastComplete` / `setOnSpellLand` /
   `setOnSpellFade` fire with the right arguments.
6. **BuffManager.** `setPlayerBuffs` / `setPlayerBuff` /
   `removePlayerBuffBySlot` / `getPlayerBuffCount`, pet and entity buff
   sets, and buff expiration (`checkBuffExpiration` is timer-driven —
   same backdating caveat).

**Out of scope for this unit**: `spell_effects.cpp` (929 lines) and
`spell_type_processor.cpp` (633). If priorities 1–6 fill the unit, say so
and stop; they become T24b.

## STOP conditions

- Any `src/`/`include/` change needed → STOP and report.
- A real bug found → pin with a `BUG_`-named test documenting current
  behavior, report prominently, do NOT fix.
- A behavior unreachable without a live server or a new accessor → skip,
  name it in the Review, do not invent fixtures under `data/`.

## File layout

`tests/test_spell_manager.cpp` and `tests/test_buff_manager.cpp` (each
≤800 lines). `tests/test_spell_database.cpp` already exists and covers
the DB layer only — do not disturb it (T22 strengthened it). Wire new
targets into `tests/CMakeLists.txt`; follow `test_combat_manager`'s block
from T23 (links `willeq_core`, Irrlicht include-dir guard).

## Verification bar (tests-only unit, x86)

1. `cmake --build <slot>/build -j24` clean.
2. `ctest --test-dir <slot>/build --output-on-failure` — full suite;
   known exceptions: `AudioManagerTest.PreloadCommonSounds` (no ALSA) and
   the `RDPServerFrameTest`/`RDPServerLifecycleTest` flake family under
   parallel load (T03b root-caused, perf territory — rerun isolated
   before reporting). Baseline currently 1664/1665.
3. New binaries' own runtimes: report them; each well under 5s. There is
   no excuse for a slow binary here — backdating is instant, so any
   multi-second runtime means a sleep crept in.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.

## Review

**Status: DONE.** Three new binaries (split from the planned two —
`test_spell_manager.cpp` exceeded the ~800-line guidance and was split by
topic, T23-precedent "split by topic if it grows"):
`tests/test_spell_manager.cpp` (797 lines, 52 tests — gem state machine +
cast validation matrix), `tests/test_spell_manager_timing.cpp` (520 lines,
29 tests — timing/handlers/callbacks/cast progress), and
`tests/test_buff_manager.cpp` (597 lines, 36 tests). 117 new tests total,
all passing. Base commit `bd0758e` (fable tip at start of this session).

### Verification bar

1. `cmake --build build -j24` — clean for every target this unit touches
   (`willeq_core`, `test_spell_manager`, `test_spell_manager_timing`,
   `test_buff_manager`) and for the tree as a whole. One **pre-existing**,
   unrelated failure was hit during the full build:
   `tools/hwcaps/gles2_zone_benchmark` fails to link
   (`undefined reference to 'irr::video::g_glCounters'`). Verified this is
   not caused by this unit: the file (`tools/hwcaps/gles2_zone_benchmark.cpp`)
   was last touched by P49, has nothing to do with spell/buff/combat code,
   and this unit's territory is `tests/` only — confirmed by building with
   `-k` (keep-going) that every other target, including both new test
   binaries, builds and links cleanly regardless. Not investigated further
   (out of territory); flagging for the record, not fixing.
2. `ctest --test-dir build --output-on-failure` — full suite, 1788/1789
   passed on the clean run (1787/1789 on an earlier run with the
   `RDPServerFrameTest` flake also firing). The only failure both times was
   the documented known host exception `AudioManagerTest.PreloadCommonSounds`
   (no ALSA); `RDPServerFrameTest.UpdateFrameMultipleTimes` fired once
   (documented intermittent flake family, T03b-root-caused, not this unit's
   territory) and was clean on the other run. Suite grew from the plan's
   quoted baseline (1664/1665) to 1789 total via other units landed since
   this plan was written — expected, not a regression.
3. New binaries' own runtimes (measured both via direct invocation and via
   `ctest -R`): `test_spell_manager` 52 tests, ~0.19s total (~0.10s test
   time); `test_spell_manager_timing` 29 tests, ~0.07s total; `test_buff_manager`
   36 tests, ~0.15s total. All three well under the 5s bar, and unlike T23
   there is no single slow test in any of them — SpellManager's timing
   entirely uses backdating (no real sleeps possible or needed, since
   `update()` doesn't honor `delta_time` — see below) and BuffManager's
   `update()` is a pure in-process accumulator, also no sleeps.
4. `bash scripts/refactor/check_file_sizes.sh fable` — exit 0, no new
   FAILs.

### Coverage per priority

1. **Gem state machine** — full: `memorizeSpell`/`forgetSpell`/`swapGems`
   invalid-slot boundaries (`MAX_SPELL_GEMS`==8, 0-based, slot 8 already
   invalid — T16-precedent convention pinned), `setSpellGems`/`setSpellbook`
   direct-injection round trips, `scribeSpell`'s DB-gated no-op without
   `initialize()`, `forgetSpell` state transitions including the
   casting-same-slot guard (and the casting-different-slot non-guard),
   `swapGems`'s casting/cooldown/memorizing guards, and the full real
   `memorizeSpell` workflow (scribed+level/class gate, forgets an existing
   gem occupant first, blocked while already casting or memorizing) —
   DB-gated via `hasDb()`/`GTEST_SKIP`. `getMemorizedSpell`/`getGemState`
   invalid-slot returns pinned.
2. **Cast validation matrix** — full, and via a different (better) route
   than planned: `validateCast`/`checkMana`/`checkRange`/
   `adjustTargetForSpellType`/`isValidCreatureTarget` are private methods
   taking `const SpellData&` by reference rather than a spell id, so most of
   this priority is tested by calling them directly with hand-built
   `SpellData` structs (via the T17-style private-access technique) instead
   of routing everything through the DB-backed public entry points. This
   sidesteps needing real DB rows for already-casting, not-enough-mana,
   zero/negative-mana-cost, in/out-of-range, missing-target, zero-range,
   self-spell-skips-range, and the full creature-type-restriction family
   (Animal/Undead/unrestricted, using a locally-added bodytype-capable spawn
   helper). Only the public `beginCastFromGem`/`beginCastById` end-to-end
   wiring itself (their internal `m_spell_db.getSpell(spell_id)` lookup) is
   DB-gated: empty-gem/casting/refresh/memorize-progress/invalid-slot/
   unknown-spell state-machine branches need no DB at all (they return
   before reaching the DB), success + `beginCastById`'s gem-slot delegation
   + a DB-loaded-but-nonexistent-id case are DB-gated.
3. **Cooldowns + memorization timing** — full, entirely via T17-style
   `#define private public` backdating of `m_gem_cooldown_start`/
   `m_memorize_start_time`/`m_cast_start_time` (no DB needed for any of it —
   see below for which specific timings needed backdating).
   `getGemCooldownProgress`/`getGemCooldownRemaining` ready/invalid/partial-
   midway, the 1500ms cooldown-duration floor, `getMemorizeProgress`
   not-memorizing and different-slot cases, and the Refresh→Ready /
   memorize-completion transitions including their `publishGemChanged`/
   `SpellMemorizeVisualComplete` bridge events — all backdated, none slept.
4. **Interrupt + server packet handlers** — full: `interruptCast`
   (casting and not-casting), `handleBeginCast` (own-cast confirm using the
   server's cast time, item-cast auto-start, other-caster target-cast
   tracking via a real `CombatManager::SetTarget`), `handleAction`
   (non-spell-type no-op, own-cast completion + `SpellLand` callback,
   target-cast-tracking clear), `handleInterrupt` (own-cast Fizzle vs
   Interrupted by `message_type`, other-caster tracking-only), and
   `handleManaChange` (smoke test — it's log-only per the implementation
   comment). None needed the DB; every internal spell lookup in these paths
   is null-checked.
5. **Callbacks** — full: `setOnCastComplete`/`setOnSpellLand` fire with
   correct arguments through real production call paths
   (`completeCast`/`handleAction`). `setOnSpellFade` has **no production
   call site** anywhere in `spell_manager.cpp` (grep-verified — only
   assigned via the setter, never invoked); pinned as a mechanically-working
   but currently-unused callback slot rather than skipped silently.
6. **BuffManager** — full, and notably did NOT need the T17 backdating
   technique at all (see the dead-delta_time section below for why):
   `setPlayerBuffs` bulk load (empty-slot skip, duration-tick conversion,
   permanent sentinel), `setPlayerBuff` single-slot replace/clear,
   `removePlayerBuffBySlot`, `getPlayerBuffCount`/`hasPlayerBuff`/
   `getPlayerBuff`/`getPlayerBuffBySlot`, pet buffs (`setPetBuffs` array
   parse incl. empty-slot skip and permanent sentinel, `clearPetBuffs`,
   owner-id tracking, replacement), entity buffs (`setEntityBuff`/
   `clearEntityBuffs`/`getEntityBuffs`/`hasEntityBuff`), `applyBuff`/
   `removeBuff`/`removeBuffBySlot` (player vs. entity routing, permanent
   sentinel, same-spell-replaces, the `MAX_PLAYER_BUFFS`-capacity silent-drop
   boundary), and buff expiration via real `update(delta_time)` calls (see
   below). `ActiveBuff`'s own helper methods (`isPermanent`/`isExpired`/
   `isAboutToExpire`/`getTimeString`) are unit-tested directly as pure struct
   logic. `isBeneficial`/`isDetrimental` DB-gated against real `is_beneficial`
   data.

### Fixture techniques used

- T17/T23-style `#define private public`/`protected public` around the
  *first* inclusion of `spell_manager.h`, used both for backdating private
  timestamp members (as T23 did for `combat.h`) **and**, going further,
  for calling private validation/state methods directly
  (`validateCast`/`checkMana`/`checkRange`/`adjustTargetForSpellType`/
  `isValidCreatureTarget`/`startGemCooldown`/`updateGemCooldowns`/
  `updateMemorization`/`completeCast`) with hand-built `SpellData` structs.
  Unlike T23's `combat.h` (transitively included by `eq.h`), `spell_manager.h`
  is **not** included by `eq.h` at all (only forward-declared) — verified
  before relying on the technique, so there is no ordering hazard to worry
  about (documented in both new files' headers).
- A locally-scoped bodytype-capable spawn-payload helper
  (`NewSpawnPayloadWithBodyType`, `test_spell_manager.cpp` only) extending
  `zonetest::NewSpawnPayloadAt` with an extra `PutUInt8` at
  `base+335` — not promoted into `zone_stream_test_utils.h` since only the
  creature-type-restriction tests need it.
- `hasDb()`/`GTEST_SKIP()` (test_spell_database.cpp precedent) for every
  test that needs the manager's internal `m_spell_db` populated from real
  SQLite data.

### The dead-delta_time finding (SpellManager)

Confirmed exactly as the plan described, and pinned with two BUG_-named
tests in `test_spell_manager_timing.cpp` covering two independent channels
(gem cooldown, cast timeout) rather than one, to demonstrate it isn't a
one-off:
`SpellManagerTiming.BUG_UpdateIgnoresDeltaTimeHugeDeltaDoesNotFastForwardGemCooldown`
and `...CastTimeout`. Each shows both halves of the finding in one test:
(a) calling `update(999999.0f)` (~11.5 real days) immediately after starting
a real cooldown/cast-timeout does **not** advance it at all (delta_time is
provably dead), and (b) backdating the real `steady_clock` timestamp and
calling `update(0.0f)` **does** complete the transition (confirming the gate
is genuinely wall-clock-based, not simply broken). All cooldown/memorization/
cast-progress tests in the file use this backdating technique instead of
sleeping.

### Divergent finding: BuffManager's delta_time is NOT dead

Worth flagging explicitly since it's the opposite of SpellManager's bug in
the same batch: `BuffManager::update(float delta_time)`
(`buff_manager.cpp:26-59`) genuinely accumulates `delta_time` into
`m_tick_accumulator` and decrements buff timers once per whole accumulated
second, with no `steady_clock` read anywhere in the class. So none of
`test_buff_manager.cpp`'s expiration tests needed backdating or sleeping —
`update(6.0f)` really does drain 6 seconds of buff duration. Pinned
positively (`BuffManagerExpiration.SubOneSecondDeltaAccumulatesAcrossCalls`
confirms sub-second deltas correctly accumulate rather than round down to
zero or up to one immediately) rather than treated as a bug, since this is
the *correct* behavior SpellManager should have had.

### New bug found and pinned (not in the plan — verified by reading, not guessed)

`SpellDatabase::queryAndCache()` (`spell_database.cpp:54-92`) maps every
`EQT::SpellInfo` field into `SpellData` **except** `class_levels` — because
`EQT::SpellInfo` (`willeq_database.h:72-98`) has no class-level-requirement
field at all to map from (same missing-source-data root cause as T22's
already-documented `effects`-never-populated gap in
`test_spell_database.cpp`). `SpellData::class_levels` therefore keeps its
default-constructed value (`std::array<uint8_t, NUM_CLASSES> class_levels =
{}` — all **zeros**, not the "unusable" sentinel 255) for every DB-backed
spell. `SpellData::canClassUse()` (`spell_data.h:200-207`) reads
`class_levels[class_idx-1]` and returns `required != 255 && level >=
required`; with every entry at 0 this is unconditionally true for any valid
class (1-16) at any level ≥ 0. Consequence: **`SpellManager::memorizeSpell()`'s
level/class gate (`spell_manager.cpp:628-633`) never actually rejects
anything against the real spell database** — a level-1 Warrior can
memorize any scribed spell, including ones the live game reserves for
level-60 casters of a completely different class. This is a real, live,
currently-reachable gap, not a hypothetical. Pinned with two tests in
`test_spell_manager.cpp`:
`SpellManagerGems.BUG_CanClassUseAlwaysTrueBecauseClassLevelsNeverPopulatedFromDb`
(shows the root cause directly — every `class_levels` entry is 0, and
`canClassUse` is true for arbitrary classes at level 1) and
`SpellManagerGems.BUG_MemorizeSpellSucceedsAtLevel1RegardlessOfSpellsRealClassRestriction`
(shows the consequence through the public `memorizeSpell` API). This
directly superseded the plan's originally-envisioned
`MemorizeSpellLevelTooLowFails` test, which turned out to be unwritable as
a *passing* test — every attempt to find a spell "usable at 60 but not at
1" via `FindSpellId` came back empty across the full scanned range,
which is itself what led to discovering the root cause. Not fixed, per this
unit's STOP-condition instructions (a real fix needs either a `src/` change
to populate `class_levels` from a real DB column if the schema has one, or a
policy decision on what "0" should mean when no level requirement was
recorded — out of this tests-only unit's territory).

### Real (non-bug) behavior notes pinned along the way

- `BuffManagerExpiration.ExpirationAppliesToPetAndEntityBuffsToo`: after
  `checkBuffExpiration()` empties an entity's buff vector, the
  `m_entity_buffs` map key is **not** erased (only `clearEntityBuffs()` does
  that) — `getEntityBuffs()` returns a non-null pointer to an empty vector,
  not `nullptr`, distinct from a truly-unknown entity id. Adjusted the test
  to assert this correctly rather than the (incorrect) nullptr assumption
  the test was first written with.
- `BuffManagerStatMod.PlayerStatModAlwaysZeroBecauseEffectsAreNeverPopulated`:
  reconfirms, in this unit, T22's already-documented finding that
  `SpellData::effects` is never populated by the DB-backed path — so
  `getBuffedStatMod`/`getPlayerStatMod` always return 0 regardless of a
  buff's real stat effect. Not a new bug, not fixed, included as a short
  pin since it's directly adjacent to this unit's territory.
- `beginCastFromGem`/`beginCastById`/`validateCast` do **not** check
  class/level at all — only `memorizeSpell` does (and, per the finding
  above, ineffectively). Pinned implicitly by
  `SpellManagerBeginCast.SuccessSetsStateAndGemCasting` needing no class/level
  setup to succeed.

### Behaviors skipped as unreachable, and why

- `adjustTargetForSpellType`'s "has a pet" branch (pet spell targets the
  pet, not just the "no pet → 0" no-op) — standing up a real owned-pet
  fixture needs a `PlayerProfile`-driven nonzero `GetEntityID()` plus a
  pet-owner-attributed spawn packet; out of proportion for one branch given
  the unit's scope. The "no pet" branch (the actually-reachable default
  state for every other fixture in this file) is fully covered.
- `checkLineOfSight`/`checkReagents` are both hard-coded `TODO` stubs
  (`return true` unconditionally) in the current implementation — not
  independently testable beyond confirming `validateCast` doesn't block on
  them (which the passing-matrix tests already demonstrate implicitly).
  Not pinned as bugs since they're explicit, visible `TODO`s in the source,
  not silent gaps.
- Packet-attempt assertions for `SpellManager`-initiated sends
  (`sendCastSpellPacket`/`sendInterruptPacket`/`sendMemorizePacket`) — same
  T23-documented gap: `GetReplaySuppressedSendCount()` doesn't observe the
  generic `QueuePacket(opcode, packet)` overload these all go through. Per
  the plan's trap 2, not attempted; only resulting state changes are
  asserted.
- `spell_effects.cpp`/`spell_type_processor.cpp` — explicitly out of scope
  per the plan (T24b territory if ever picked up).
- Priorities 1-6 filled the unit; no T24b follow-on needed.

### Deviations summary

- Split `test_spell_manager.cpp` into two files
  (`test_spell_manager.cpp` + `test_spell_manager_timing.cpp`) — the
  combined content exceeded the plan's ~800-line-per-file guidance;
  followed T23's explicit "split by topic if it grows" precedent. Both
  wired into `tests/CMakeLists.txt`.
- Discovered and pinned one real, previously-undocumented bug not named in
  the plan (`class_levels` never populated → `canClassUse` always true —
  see above); this replaced the plan-anticipated
  `MemorizeSpellLevelTooLowFails` test, which could not be written as a
  passing test once the root cause was found.
- One test (`BuffManagerExpiration.ExpirationAppliesToPetAndEntityBuffsToo`)
  needed its assertion corrected after a first-run failure exposed real
  map-key-persists-after-empty behavior (see "Real behavior notes" above) —
  documented inline in the test and here rather than silently adjusted.
- Pre-existing, unrelated `gles2_zone_benchmark` link failure encountered
  during the full-tree build (see Verification bar step 1) — not caused by
  this unit, not fixed (out of territory), flagged for the record.
- No `src/`/`include/` change was made or found necessary — the Option A
  seam ruling holds up under this unit's test-writing pressure too, same as
  T23. The one new finding above needs a `src/` change to *fix*, but this
  unit only pins it, per the STOP-condition instructions.
