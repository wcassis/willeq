# T240 — castSpell reads the real gems: repoint at SpellManager, delete CombatManager's dead gem map

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for the keypress cast
proof (T235's driver + Testcaster in review/T235/). Class A (T235 finding).
Territory: src/client/eq_action_handler.cpp `castSpell`/`castSpellOnTarget`
(+ header), src/client/combat.cpp / include/client/combat.h
(`m_spell_gems`, `CastSpell`, `MemorizeSpell` — the dead map and its
hardcoded cast-time stub at ~681-732), tests. Do NOT touch
input_action_bridge.cpp / action_dispatcher hotbar arms /
`processCommandText` (T239 slot3 — coordinate: keep your
eq_action_handler.cpp hunks to the two cast methods), application.cpp
script path (T238 slot1).

## Defect
`EqActionHandler::castSpell(gem)` → `CombatManager::CastSpell(slot)` looks
up `CombatManager::m_spell_gems`, filled only by `CombatManager::
MemorizeSpell()`; the profile load fills `EQ::SpellManager`'s gems instead.
So every action-layer cast (keyboard gem keys, `/cast N` if routed here)
fails "no spell memorized" while `/gems` shows the spells. T101 already
repointed `interruptCast` at SpellManager (`eq_action_handler.cpp:517-531`)
— same shape.

## Scope
1. Repoint `castSpell` / `castSpellOnTarget` at `SpellManager` (the method
   `/cast` uses — find it: `beginCastFromGem` or similar), preserving
   target semantics. Delete `CombatManager::m_spell_gems`, `CastSpell`,
   `MemorizeSpell` and the hardcoded cast-time/slot-0 stub IF no other
   caller remains (grep; tests that only test the dead map go too). If
   `MemorizeSpell` has a live caller, keep it but make it write SpellManager.
2. Tests: real EverQuest harness with the DB (test_eq_action_handler_spell_pet.cpp
   pattern): gem memorised via the real path → `castSpell(1)` → SpellManager
   isCasting + OP_CastSpell bytes recorded; empty gem → refusal. Red/green.
3. Live: Testcaster, alt+1 → server `BeginCast 'Frost Bolt'` (T235's
   AFTER run had 0 — that is the before). Also `/cast 1` for the record.

## Verification
Full ctest; visual gate not needed; Review; evidence review/T240/. Commit:
`T240: action-layer casts use SpellManager's gems; dead CombatManager gem map deleted`.

## Review (executor, 2026-08-26, slot2, off fable e53f43bf)

Evidence: /mnt/projects/fable/review/T240/ (README.md indexes every file).

### Fix (Scope 1)
- `src/client/eq_action_handler.cpp`: `castSpell(gem)` is now
  `beginCastFromGem(gem, 0)` (SpellManager resolves the current
  CombatManager target, or self/pet/group per target type);
  `castSpellOnTarget(gem, id)` does `CombatManager::SetTarget(id)` then
  `beginCastFromGem(gem, id)` -- the pre-T240 order, so the explicit target
  is also the current target afterwards. `memorizeSpell(gem, id)` had a
  live caller (`ActionDispatcher::memorizeSpell`), so per the plan it was
  kept and repointed at `SpellManager::memorizeSpell()` with the same chat
  feedback `memorizeSpellByName()` gives. All three hunks are inside the
  named methods; `beginCastFromGem`/`interruptCast` got comment updates
  only.
- `include/client/combat.h` / `src/client/combat.cpp` -- DELETED:
  `CastSpell(uint32_t, uint16_t)` (the stub: hardcoded 3000 ms cast time,
  `cs->slot = 0`, unverified spell id), `CastSpell(SpellSlot, uint16_t)`,
  `MemorizeSpell()`, `InterruptCast()`, `IsCasting()`, the private
  `CastSpell_Struct` copy at the top of combat.cpp, `m_spell_gems`, and the
  casting-state members only they touched (`m_is_casting`,
  `m_current_spell_id`, `m_spell_target_id`, `m_cast_start_time`,
  `m_cast_time_ms`) plus their ctor inits. Grep before deleting: no
  production caller of any of them remained after the repoint (the only
  callers were the two cast methods, `memorizeSpell`, and T93's test).
  Left in place (not casting, still have definitions/callers or a struct
  field): the `SpellSlot` enum (`SpellInfo::gem_slot`),
  `m_memorized_spells`/`GetMemorizedSpells()`, `CanCastSpell()`.

### Tests (Scope 2) -- measured red/green
`tests/test_eq_action_handler_cast.cpp` (new; T234's WireHarness -- real
DaybreakConnection over FakeUdpTransport as `m_zone_connection` -- plus
T101's DB-opening `main()`); gems loaded through
`SpellManager::setSpellGems()`, the exact call eq.cpp's PlayerProfile
handler ends in; asserts on the outbound OP_CastSpell / OP_MemorizeSpell
bytes. 6 tests:
- `CastSpell_EmptyGem_SendsNothingAndSaysSo`
- `CastSpell_ProfileLoadedGem_CastsThroughSpellManager` (the T235 defect)
- `CastSpellOnTarget_ExplicitTargetIsCastTargetAndCurrentTarget`
- `CastSpell_GemIndexIsOneBased` (T93's off-by-one pin, both ends, on the
  real store)
- `CastSpell_GemBeyondEight_RefusedNothingSent`
- `MemorizeSpell_ById_WritesSpellManagerAndSendsMemorize`
RED (new test built against the pre-fix src via `git stash` of the three
production files): **5/6 FAIL** -- `red_cast.txt` (isCasting false, 0
OP_CastSpell on the wire, gem stays Empty, no OP_MemorizeSpell). The one
pass is gem>8, documented as equal behaviour (both refuse silently).
GREEN: **6/6** -- `green_cast.txt`.
Deleted: `tests/test_eq_action_handler_transforms.cpp` (T93; both tests
observed `CombatManager::IsCasting()` on the deleted map) and its
CMake target; the new target takes its place in tests/CMakeLists.txt.

### Live (Scope 3) -- Testcaster, befallen, fixed binary
`t240_drive.sh` = T235's driver with the cadence widened for Frost Bolt's
13 s recast (Alt+1, Alt+1 at +15 s, Alt+2 at +30 s) and `/cast 1` from the
--script at ~+48 s. Xvfb :79, `--egl-x11 --eqe1-path`, xdotool.
- **Run 1** (`run1_outofrange_*`): `/target excavator` resolved to
  `a_skeletal_excavator000`, a roamer 395 units away (Frost Bolt range 300).
  All three presses AND `/cast 1` reached `SpellManager::validateCast` --
  the T235 dead end is gone -- and were refused `OutOfRange` (result 4,
  "Cannot cast spell"). Kept as evidence that hotkey and `/cast` now take
  the same path and fail the same way.
- **Run 2** (`after_live_client_testcaster_debug.log`, the deliverable):
  `#npctypespawn 2024` (a_fire_beetle, level 2, no other beetle in
  befallen) at Testcaster's feet, `/target beetle` -> `a_fire_beetle000`
  (id 403). Each of the three presses: `Key pressed ... action=SpellGemN`
  -> `Sent CastSpell packet: spell=54, gem=0|0|1, target=403` ->
  `Beginning cast of 'Frost Bolt'` -> `Casting from gem N` ->
  **`BeginCast: caster=402, spell='Frost Bolt' (54), time=1000ms` /
  `Server confirmed cast of 'Frost Bolt'`** (OP_BeginCast from the
  server) -> `SpellAction: caster=402, target=403 ... effect_flag=0` and
  `=4` (OP_Action from the server) -> `Cast of 'Frost Bolt' completed
  successfully`. Then `/cast 1` at 00:12:17: same sequence, `gem=0`.
  **4 casts, 4 server BeginCast lines** (log lines 14449-14585,
  16123-16367, 17784-18064, 19249-20149). Before (T235 AFTER run): 3
  presses, 0 BeginCast.
- `after_alt2_press_1024x768.png`: real render (verified by eye: HUD,
  target box `a_fire_beetle000`, chat "Casting from gem 1 / 1 / 2", "You
  hit a fire beetle for 3 points of damage", "You have slain a fire
  beetle!", gem panel A1/A2 "Fros"). Nothing to judge visually beyond
  that; the cast itself is temporal and proven by the log, not the still.
- eqemu's zone log at its default level does not log casts
  (`server_zone_log_cast_lines.txt` is empty by design); the server-side
  proof is OP_BeginCast + OP_Action received by the client, which only the
  server sends.

### Verification bar
1. `cmake --build slot2/build -j24` -- OK.
2. ctest: **2858/2860** on the full run -- `ctest_full.txt`; failures =
   AudioManagerTest.PreloadCommonSounds (ALSA, documented) +
   `check_build_tree_orphans`, which correctly flagged the STALE
   `bin/test_eq_action_handler_transforms` binary left by the deleted
   target (cmake never prunes bin/). Deleted it; re-run passes --
   `ctest_orphans_rerun.txt`. Net: 2859/2860, sole failure the ALSA quirk.
   (+36 vs T235's 2824: -2 T93 tests, +6 new, the rest from T238/T239/T241
   merged since.)
3. `build-gles2-x86.sh` -- OK (exit 0) -- `build_gles2_x86.txt`.
4. Visual gate: not required by the plan (no renderer change); goldens
   untouched (`git status`: only this unit's five files + the new test).
5. `build-arm-noble.sh` -- OK (exit 0) -- `build_arm_noble.txt`.
6. `check_file_sizes.sh fable` -- 0 fail (`check_file_sizes.txt`; note it
   diffed against the MOVED fable tip b9b503ec, so T241's files appear
   too); `--files` on this unit's five files: 0 fail, 3 pre-existing WARNs
   (>1500 lines) -- `check_file_sizes_own_files.txt`.

### eqemu
Lock taken atomically (noclobber) as T240; up on befallen; Testcaster
staged to zone 36 at Testchar's spot (`db_staged.txt`), restored to zone 25
/ -259 / -1201 / -5.75 / 0 (`db_restored.txt`); Testchar row restored to
zone 36 / -77.9232 / 21.9086 / 3 / 190 (it read -77.875/21.875 before this
unit -- float-rounded by an earlier session's save, `db_before.txt`);
memmed spells 0/1 = 54 unchanged. `down.sh` clean; lock deleted.

### Deviations
- Two live runs, not one: run 1's `/target excavator` picked an
  out-of-range roamer (substring match returns the lowest spawn id). Run 2
  spawns a uniquely-named NPC at the caster so the target is
  deterministic. Both logs kept.
- Killing the beetle levelled Testcaster 1 -> 2 (server `SetLevel` line;
  `db_after_run.txt`). Restored `level=1, exp=0` -- exp was not captured
  in `db_before.txt` (T235's snapshot did not carry it either); a fresh
  level-1 character's exp is 0, so this is the best available restore and
  is flagged here rather than assumed.
- `InterruptCast()`/`IsCasting()` and the casting-state members went with
  the map (not named in Scope 1 individually, but they were the stub's
  only state and had no other callers).

### Also found (not fixed, out of territory)
1. `EqActionHandler::beginCastFromGem()`'s result switch has no
   `OutOfRange`/`NoLineOfSight`/`InvalidTarget` cases -- they all fall to
   the default "Cannot cast spell" (run 1 shows it). A per-case message is
   a small follow-up in the same method; not touched here because the plan
   confines this unit's eq_action_handler.cpp hunks to the two cast
   methods.
2. Stale comments now describing a CombatManager cast path that no longer
   exists: `include/client/action/action_dispatcher.h:172-177` ("NOT
   CombatManager (the subsystem castSpell()/castSpellOnTarget() above
   use)"), `src/client/action/action_dispatcher.cpp:506` and
   `tests/test_action_dispatcher.cpp:624-629`. Left alone: action_dispatcher
   is T239's live territory.
3. `/target <name>` substring match returns the lowest-spawn-id match, not
   the nearest -- fine for the command's contract, but a trap for scripted
   live legs (run 1).
