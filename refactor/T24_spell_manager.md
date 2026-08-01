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

(Executor fills: coverage per priority, which timings needed backdating,
the dead-delta_time finding, bugs pinned, behaviors skipped as
unreachable and why, per-binary runtimes, deviations.)
