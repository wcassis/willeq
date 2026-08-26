# T245 — CombatManager::Update() writes combat() off the game thread (T227 STOP item)

Slot: slot1. x86. OWNS eqemu briefly (noclobber) for a `/combat on` TSAN
tour. Class A. Territory: src/client/combat.cpp / include/client/combat.h
(Update, CheckForAggro, ProcessCombatRound, FindNextHuntTarget,
SetTarget/ClearTarget call sites), src/client/eq.cpp `m_update_thread`
loop (~3766-3800), tests. Do NOT touch spell_manager.cpp /
entity_manager.cpp / cmdTarget (T244 slot3), eq_chat / cmdEmote (T243).

## Defect
`EverQuest::m_update_thread` (500 ms tick, eq.cpp ~3798) calls
`CombatManager::Update()`, which — only when `/combat on` — reaches
`CheckForAggro` / `ProcessCombatRound` / `FindNextHuntTarget` →
`SetTarget` / `ClearTarget` → T221's `m_game_state.combat()` writes, plus
the bridge publishes. Every other GameState write is game-thread; T227's
convergence assumed single-threaded access (docs/decisions.md 2026-08-25,
second entry). The same hazard is why `UpdateMovement()` was moved OFF that
thread (eq.cpp ~3766-3789 comment).

## Scope
1. Prove it: TSAN build (review/T220/README.md tooling), a tour with
   `/combat on` and a hostile NPC in range (`#npctypespawn` something
   aggressive, e.g. the fire beetle T240 used) → expect a `state::` race
   report between `m_update_thread` and the game thread. Record BEFORE.
2. Fix by the codebase's discipline: move `CombatManager::Update()` onto
   the game thread (call it from `UpdateMovement()`/`GameTick` at the same
   500 ms cadence via a timestamp check) and delete `m_update_thread` if
   nothing else runs on it — read the eq.cpp comment: it may already say
   the thread should go. No mutex on GameState.
3. AFTER: same TSAN tour → zero `state::` reports. Auto-combat still works
   live (the beetle dies) — compare timing of the first swing before/after
   to show the cadence held.
4. Unit test: Update() advances the combat round when invoked from the
   tick path (existing combat tests likely cover the logic; add the
   cadence test if none).

## Verification
Full ctest; Review; evidence review/T245/. Commit:
`T245: auto-combat runs on the game thread -- the update thread is gone`
(or "-- update thread retained for X" with the reason).

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T245-combat-thread`,
base fable tip `aae1ae65` (verified at checkout; `fable` advanced to
`e72a761f` during the unit -- T246's plan only). eqemu owned for the four
live legs only (noclobber take after a 230 s bounded wait for T243, deleted
after `down.sh`; Testchar restored). Evidence: `/mnt/projects/fable/review/T245/`
(README indexes every file).

### Headline

`EverQuest::m_update_thread` is DELETED. `CombatManager::Update()` is called
from `EverQuest::UpdateMovement()` (eq_movement.cpp, game thread, every
tick, gated `m_combat_manager && IsFullyZonedIn()` -- the old loop's exact
condition) and paces itself with a new `m_last_update` /
`kUpdateIntervalMs = 500` early-return, so the wire-visible cadence is the
one the thread's `sleep_for(500ms)` imposed. `StartUpdateLoop()` /
`StopUpdateLoop()` survive as flag flips: `m_update_running` is part of
`IsFullyZonedIn()` and is the once-only gate on `OnGameStateComplete()`
(eq_entities.cpp:1691), so it stays. Nothing else ran on the thread (the
P36 comment already said UpdateMovement had been moved off it), so it was
deleted rather than retained. No mutex on GameState. `EverQuest` now spawns
no thread of its own.

### Step 1 -- proven (TSAN BEFORE)

Plan tour adjusted, with a reason: `CombatTarget::is_aggro` is hard-coded
`false` (`GetNearbyHostiles`, TODO), so `CheckForAggro` can never reach
`SetTarget`; and `/attack` runs `EnableAutoAttack` on the game thread. The
only reachable off-thread `combat()` writer was the hunt path, so the tour
is `/combat on` + `/hunt on` with three `#npctypespawn 2024` beetles, plus
100 `/hail` at 0.2 s for dense game-thread `hasTarget()` reads (T220's
technique). Result: 58 reports, 3 on `state::CombatState` --
`m_targetId` written by the `StartUpdateLoop` lambda via `Update ->
UpdateAutoHunting -> FindNextHuntTarget -> SetTarget -> PublishTargetChanged
-> CombatState::setTarget`, read by `gameThreadLoop` via `cmdHail ->
hasTarget()`. 54 of the 58 carry a `CombatManager::` frame on that thread
(`m_entities` walks racing position/health writes, `Face()`,
`SendPositionUpdate()` reached from the update thread, vector growth).
`review/T245/tsan_before_GAMESTATE_RACE.txt`.

### Step 2 -- fix

| file | change |
|---|---|
| `include/client/eq.h` | `std::thread m_update_thread` removed; `m_update_running` kept with a comment on why |
| `src/client/eq.cpp` | `StartUpdateLoop`/`StopUpdateLoop` are flag set/clear (69 -> 31 lines; the P36 history is summarised in 10 lines and points here) |
| `src/client/eq_movement.cpp` | the tick, after the two deferred early-returns, before `UpdateCampTimer()` |
| `include/client/combat.h` / `src/client/combat.cpp` | `kUpdateIntervalMs`, `m_last_update` (epoch-initialised so the first call runs, as the thread's first iteration did), the gate at the top of `Update()`; three stale "m_update_thread" comments rewritten |
| `src/client/application.cpp` | comment only (the T26 loot note that warned about the 2 Hz thread) |
| `tests/test_combat_manager.cpp` | comment + `CombatManagerTick` x2 (below) |
| `docs/decisions.md`, `refactor/deferred.md` | T227 STOP item closed in place |

### Step 3 -- AFTER

TSAN, same tour, fixed binary: 15 reports, **0** `state::`, **0** frames in
combat/eq code, **0** `StartUpdateLoop` frames. All 15 are one pre-existing
audio class (`MidiPlayer::stop()` from `EnableAutoAttack ->
startAutoAttackMusic` vs the miniaudio worker's render on TinySoundFont
voices; 4 of the same in BEFORE) -- out of territory, listed for the board,
not fixed. Live (normal build, DEBUG): `/target beetle` + `/combat on` +
`/attack` -> first `Attack timer ready` +42 ms BEFORE / +484 ms AFTER (phase
of the 500 ms interval), swings every 2.0 s on both, `You have slain a fire
beetle!` on both (56 s / 67 s -- dice), identical combat-state transitions,
13/13 commands, exit 0. Table in the README.

### Step 4 -- tests

`CombatManagerTick.UpdateRunsOnFirstCallThenPacesToInterval` (first call
scans; a second call inside the interval does not even with the scan gate
open; past the interval it scans again -- all by backdating the two clocks,
no sleeps) and `CombatManagerTick.UpdateIsANoOpWhenDisabled`. The existing
Consider fixture (`Update()` once after backdating `m_last_target_scan`)
still passes unchanged because the pacing clock starts at the epoch.

### Verification bar

| step | result |
|---|---|
| build (GLES2-config) | exit 0 |
| ctest | 2878/2880 -> net 2879/2880: ALSA `PreloadCommonSounds` (documented) + `check_build_tree_orphans` flagging my parked BEFORE binary and T240's stale `test_eq_action_handler_transforms`; both removed, rerun passes |
| build-gles2-x86.sh | exit 0 |
| visual gate | not run -- no renderer-adjacent change, goldens untouched |
| build-arm-noble.sh | exit 0 |
| check_file_sizes | 1 FAIL = `eq.cpp` 3911 lines, pre-existing (3950 on fable; -39 here); 0 new WARN |

### Deviations / findings

- Tour uses `/hunt on`, not `CheckForAggro` (dead: `is_aggro` never set).
  **Board lead:** `CombatTarget::is_aggro` TODO means auto-defence on being
  attacked does not exist; only hunting and manual `/attack` engage.
- Both TSAN legs' hunts engaged `a_scaled_wolf_pup` at `dist=0.0`, not a
  beetle: befallen holds dozens of leftover GM-spawned level-1 pups at
  Testchar's spot; hunt picks nearest suitable con. Heuristic, not threading.
  In the live legs `/hunt on` after the kill never searched at all because
  `m_current_target_id` still held the corpse id (pre-existing, both sides).
- **Board lead (audio):** the 15 residual TSAN reports -- `MidiPlayer`
  play/stop on the game thread vs the miniaudio render callback on tsf voice
  state, no lock. Not new; the thread move only changes which game thread
  stack reaches it.
- DEBUG live logs contain bytes grep treats as binary: use `grep -a`.
- The old thread's log lines flushed in batches (`FlushThreadLog`), so
  BEFORE's COMBAT lines appear out of timestamp order; AFTER's are in order.
