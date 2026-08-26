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
