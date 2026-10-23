# T33 — Wire the loot state machine to the authoritative death event

**Slot: t1. x86 only. No Pi. OWNS EQEMU (lock protocol applies).**
No sibling unit.

**Territory: `src/client/combat.cpp` / `include/client/combat.h` (the
loot state machine + its entry), `src/client/eq.cpp`'s
`ZoneProcessDeath()`, and `tests/test_combat_manager.cpp`.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred.md entry
**"Auto-loot on kill has NEVER worked against a real server"** (the
verified finding this fixes), then `refactor/T26_loot_state_machine.md`
and `refactor/T32_loot_verification.md` Reviews. T32's transcript at
`benchmarks/audits/T32/transcript_primary.txt` is a **real kill against
a real server** — use it as evidence, it will answer questions faster
than reasoning from the code.

Will's ruling 2026-08-03: **wire the loot machine to the death event.**

## The defect

`CombatManager::ProcessCombatRound()`'s `entity.hp_percent <= 0` block
(`combat.cpp:1150`) is the only site that enters
`LootSubState::AwaitingCorpse`. But `ProcessCombatRound()` early-returns
at `combat.cpp:1100` when `!m_auto_attack_enabled`, and
`EverQuest::ZoneProcessDeath()` (`eq.cpp:3233-3237`) calls
`DisableAutoAttack()` first when the authoritative death packet says our
target died. The poll is therefore foreclosed before it can ever observe
the dead HP, and T32 confirmed live that the machine is never entered.

**This is pre-existing, not a T26 regression** — verified against
`677807c`, immediately pre-T26. You are making a dormant feature work for
the first time, not repairing something that recently broke. Treat it
accordingly: this is a behaviour change, not a restoration.

## Scope

### 1. Enter the machine from the death event

Make `ZoneProcessDeath()` (or a `CombatManager` method it calls) start
the loot flow, instead of a poll the same event disables. Shape is your
call; keep the ordering deliberate — `DisableAutoAttack()` and
`ClearTarget()` must still happen, and the loot entry must not depend on
state they clear.

Carry over what the dead block did: set `m_pending_corpse_name`, set
`COMBAT_STATE_LOOTING`, enter `AwaitingCorpse`, record
`m_combat_end_time`. If the dead `hp_percent <= 0` block becomes
genuinely unreachable afterwards, **say so and remove it** rather than
leaving two entry points that can diverge — but only if you can show it
is unreachable, not merely redundant.

### 2. PRESERVE the auto-hunt gate — this is the part most likely to
### cause harm if you get it wrong

The dead block is gated `if (m_auto_loot_enabled && m_auto_hunting_enabled)`.
`m_auto_loot_enabled` defaults **true**; `m_auto_hunting_enabled`
defaults **false**. So today, even wired, this only fires for a player
who explicitly turned auto-hunt on.

**Keep that gate.** Dropping it would make every player auto-loot every
corpse they kill — a large, unrequested gameplay change, and exactly the
kind of thing that is hard to notice in a test and obvious in play. Will
asked for the machine to be wired, not for auto-loot to become universal.
If you believe the gate is wrong, say so in the Review and leave it
alone.

### 3. Fix the corpse-matching rule BEFORE it goes live

`TickAwaitingCorpse()` currently accepts the **first** entity in the map
matching `corpse_entity.is_corpse || corpse_entity.name == m_pending_corpse_name`.
The `is_corpse` disjunct means it will happily grab **any** corpse in the
zone — and EQ corpses persist for minutes, so "some other corpse is
lying around" is the normal case, not an edge case.

This was harmless while the path was dead. The moment you wire it, it
becomes a real bug: looting the wrong corpse, or repeatedly failing
against a corpse that isn't yours.

Match the **specific** victim. `ZoneProcessDeath()` has `victim_id`, and
it already sets `is_corpse = true` on that same entity — so an id-based
match should be available. **Confirm empirically whether the server
spawns a separate corpse entity with a different id**, using T32's
transcript rather than assuming: if it does, an id captured at death
won't match and you need the rename/name path as well. Get this right
with evidence; a plausible guess here produces a bug that only shows up
in a populated zone.

### 4. Tests

T26's unit tests drive `hp_percent` synthetically, which is precisely why
this stayed invisible — they simulated a path the real server never
takes. Add coverage that exercises the **death-event** entry, and make
sure at least one test would fail against the current (unwired) code.
Keep the existing anti-hang coverage green: every non-Idle state must
still have a deadline and a corpse-gone exit.

## STOP conditions

- Wiring from `ZoneProcessDeath()` requires reordering combat shutdown
  (`DisableAutoAttack`/`ClearTarget`) in a way that changes behaviour
  beyond loot → STOP and report.
- You cannot establish, from evidence, whether the corpse is the same
  entity id or a new one → STOP rather than guessing.
- Making this live turns out to require dropping or widening the
  auto-hunt gate → STOP; that is Will's call, not a mid-unit judgement.
- Any behaviour change beyond the loot-on-kill path → STOP.

## Verification bar

1. `cmake --build <slot>/build -j24` clean. (Slots may carry a stale
   `third_party/irrlicht-gles2-x86/libIrrlicht.a` predating `3328570` →
   missing `eqtGPUPhaseBegin` symbols; rebuild with
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.)
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (known unfixed
   use-after-free — rerun isolated and say that is what you saw).
3. Golden replay: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq`,
   golden **unchanged**. (Worktree needs its own `data/captures`
   symlink or this test **silently SKIPS** — confirm it RAN.)
4. **THE ACCEPTANCE CHECK — a real kill that actually loots.** Reuse
   T32's method, which works and is committed at
   `benchmarks/audits/T32/run_verification.sh`: eqemu (LOCK FIRST;
   `up.sh` only; restore Testchar to zone_id=36, x=-77.9232, y=21.9086,
   z=3, h=190), GM-spawn a **lootable** mob adjacent, drive entirely by
   piped stdin, `/combat on` + auto-hunt on, kill it. Show:
   - the state machine entering `AwaitingCorpse` and advancing
     (`→ AwaitingItems → LootingItems → Closing`) in the DEBUG log;
   - **items actually moving into inventory** — not just the states
     firing. Confirm the corpse had items first;
   - liveness preserved: `/who` or chat answered *during* the loot
     sequence (T32 proved this held; don't regress it);
   - a clean exit afterwards.
5. **The wrong-corpse check**: with more than one corpse present (kill
   two mobs, or spawn and kill one near a pre-existing corpse), show it
   loots the right one. This is the specific hazard item 3 exists for.
6. Server left clean; evidence committed under `benchmarks/audits/T33/`.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs (`eq.cpp`/`eq.h`/`application.cpp` already FAIL/WARN at
   baseline; confirm rather than assume).

## Review

### Where the entry was wired, and why that ordering

`EverQuest::ZoneProcessDeath()` (`src/client/eq.cpp`) already had the block
that matters: `if (m_combat_manager && victim_id ==
m_combat_manager->GetTargetId())` — i.e. "the thing that just died was our
current combat target" — which previously only called `DisableAutoAttack()`.
Added one line right after it:

```cpp
m_combat_manager->DisableAutoAttack();
m_combat_manager->NotifyTargetDied(victim_id);
```

`CombatManager::NotifyTargetDied(uint16_t victim_id)` (new, `combat.cpp`) is
the real entry point. It reapplies exactly the gate and side effects the
old `ProcessCombatRound()` dead block applied once a kill was confirmed
(`m_auto_loot_enabled && m_auto_hunting_enabled` gate, `m_combat_end_time`
update, `SetCombatState(COMBAT_STATE_LOOTING)`,
`EnterLootSubState(LootSubState::AwaitingCorpse)`), keyed off `victim_id`
instead of a name.

Ordering: `DisableAutoAttack()` first, `NotifyTargetDied()` second — same
order the old dead block used for its two side effects (`ClearTarget()` +
`DisableAutoAttack()` before the loot check). I did **not** add a
`ClearTarget()` call to `ZoneProcessDeath()`: it doesn't have one today
(only the synthetic `ProcessCombatRound()` path ever called it), and adding
one would be a real-path behaviour change beyond loot, which the plan's
STOP condition names explicitly. Concretely this means the real death path
leaves `m_current_target_id` pointed at the (now corpse) entity until
`ZoneProcessDeleteSpawn()`'s existing "despawn our target" logic clears it
later — unchanged from pre-T33 behaviour, just now also observed to be
harmless to the loot flow (which matches by id, not by "is this my
target").  Flagging this as a **pre-existing, not touched** discrepancy
between the two entry paths rather than fixing it, since fixing it wasn't
in scope and STOP said not to widen combat-shutdown ordering.

### Corpse-identity evidence (landmine 2)

Confirmed **empirically, twice** that eqemu renames the *same* entity in
place on death rather than spawning a second one:

1. **T32's transcript** (`benchmarks/audits/T32/transcript_primary.txt`):
   `Entity 171 became corpse: 'a_scaled_wolf_pup000' -> 'a_scaled_wolf_pup's_corpse171'`
   — same spawn_id (171) before and after death, no new `New spawn:` line
   for the corpse.
2. **Source**: `EverQuest::ZoneProcessDeath()` (`eq.cpp`) sets
   `is_corpse = true` on the victim's *existing* `m_entities` entry
   directly (no new entry created). The `HC_OP_MobRename` handler
   (`eq_entities.cpp`, the "became corpse" log site) parses the numeric id
   back out of the incoming `"...'s_corpseNNN"` name and looks up that
   same id in `m_entities` — it does not create a new entity either.
   `ZoneProcessDeleteSpawn()` additionally special-cases corpses (`if
   (it->second.is_corpse)`) to refuse deletion until looting completes, so
   the id survives the server's `DeleteSpawn(0)` (then interpreted as "delete
   current target") that arrives immediately after death — confirmed live
   in T32's transcript at the same timestamp the corpse rename happened.
   [B1125: id 0 is now ignored outright — it is eqemu's despawn of the NPC
   object whose id went to the corpse, and the target mapping erased a
   self-targeted player; `refactor/B1125/README.md`.]

This means the id captured at `ZoneProcessDeath()` time (`victim_id`) is
valid for the corpse's entire lifetime — no name/rename fallback needed.
`TickAwaitingCorpse()` now does `entities.find(m_pending_corpse_id)`
(O(1), and requires `it->second.is_corpse` as a defense-in-depth check)
instead of looping the whole entity map for `is_corpse || name ==
m_pending_corpse_name` — the old rule that would grab **any** corpse in
the zone. Re-verified against **three separate live kills in this unit's
own runs** (see Acceptance below) in a zone that had 3-5 unrelated stale
corpses sitting around from prior runs at the time — every kill resolved
to its own corpse, never a neighbour's.

### The old `hp_percent<=0` block: kept, not removed — and why

Did **not** remove `ProcessCombatRound()`'s dead block. I can show it is
*effectively* unreachable for a server-signalled kill in the common case
(once `ZoneProcessDeath()` calls `DisableAutoAttack()`, `m_combat_state`
leaves `COMBAT_STATE_ENGAGED` synchronously, and `Update()`'s switch only
calls `ProcessCombatRound()` in the `ENGAGED` case — so the poll can never
observe this kill's death again), but I could **not** prove it is
*logically* unreachable, which is the bar the plan set for removal:

- `CombatManager::Update()` (and therefore `ProcessCombatRound()`) runs on
  a **separate thread** (`m_update_thread`, 500ms cadence — see
  `EverQuest::StartUpdateLoop()`), independent from the game/network
  thread that processes `ZoneProcessDeath()`.
- `entity.hp_percent` can also be set to 0 by a **different** packet,
  `ZoneProcessMobHealth()` (`OP_MobHealth`), which some server content
  could plausibly send ahead of `OP_Death`.
- If that happened, there is a narrow window between the `MobHealth`
  packet setting `hp_percent = 0` and the `Death` packet actually arriving
  where the 500ms-cadence poll could observe the death first.

T32's real transcript shows this specific fight's HP sequence going
straight from 18% to the kill line with no `0%` `MobHealth` packet, and my
own three live kills in this unit's runs never triggered the old block
either — so it's unreachable *in practice against this server*, but "a
plausible guess" is exactly what the plan told me not to rely on for
removal. Kept the block, but converged it onto the same
`NotifyTargetDied()`/`m_pending_corpse_id` machinery (was previously its
own hand-rolled copy using a name-based key) so if it is ever hit, it goes
through the identical, now-safe corpse-matching path rather than a second,
divergent one. Left a comment at the call site explaining exactly this so
a future reader doesn't "clean it up" on a guess either.

### Auto-hunt gate: left intact

`NotifyTargetDied()`'s only gate is `if (!m_auto_loot_enabled ||
!m_auto_hunting_enabled) { ...; return; }` — byte-for-byte the same
condition the old block used. Did not touch `m_auto_loot_enabled`'s
default (`true`) or `m_auto_hunting_enabled`'s default (`false`).
`tests/test_combat_manager.cpp`'s new
`ZoneProcessDeathDoesNotLootWithoutAutoHunting` test pins this: a plain
`/combat on` kill with `/hunt` never toggled stays out of
`COMBAT_STATE_LOOTING` and `LootSubState::AwaitingCorpse` even though
`auto_loot` is on by default.

### Tests

`tests/test_combat_manager.cpp`, `CombatManagerLoot` suite (8 tests, was
5):

- Fixed the two pre-existing tests that drove `AwaitingCorpse` via the
  now-removed `m_pending_corpse_name` string field to use
  `m_pending_corpse_id` instead (`AwaitingCorpseDeadlineExpiresWithoutHanging`,
  `AwaitingCorpseFindsCorpseAndBeginsLootRequest` — the latter now also
  replays a real `HC_OP_Death` packet to set `is_corpse=true`, since
  `TickAwaitingCorpse` now requires that flag).
- New `AwaitingCorpseIgnoresOtherCorpseAndMatchesRightId`: stages two
  `is_corpse` entities, confirms the machine resolves to the one named by
  `m_pending_corpse_id`, not whichever the map iterates to first — direct
  unit coverage of landmine 2, independent of the live evidence above.
- New `ZoneProcessDeathEntersAwaitingCorpseWhenAutoHunting`: drives a real
  `HC_OP_Death` packet (not a synthetic `hp_percent` write) through
  `ReplayZone` with auto-hunt on, and asserts `GetCombatState() ==
  COMBAT_STATE_LOOTING` / `GetLootSubState() == AwaitingCorpse`. **Verified
  this fails against the pre-T33 code**: temporarily reverted the
  `ZoneProcessDeath()` one-line wiring and reran — the test fails with
  `GetCombatState()` stuck at `0` (IDLE) and `GetLootSubState()` stuck at
  `Idle`, exactly the T32-diagnosed defect. Restored the fix and confirmed
  green again afterward.
- New `ZoneProcessDeathDoesNotLootWithoutAutoHunting`: the gate-preservation
  pin described above.
- The existing anti-hang tests (`AwaitingCorpseDeadlineExpiresWithoutHanging`,
  `CorpseDisappearingMidLootAbortsWithoutHanging`,
  `AwaitingItemsHardDeadlineClosesWhenAutoLootDisabled`) all still pass
  unmodified in behaviour (only the corpse-key field name changed) — every
  non-Idle state still has both a deadline and a corpse-gone exit.

All 44 tests in the binary pass (`build/bin/test_combat_manager`).

### Verification bar

1. `cmake --build build -j24` — clean (only pre-existing, unrelated
   `-Wpointer-arith` warnings in `eq_entities.cpp`/`eq.cpp`, same as
   baseline).
2. `ctest --test-dir build --output-on-failure` — **1806/1808 pass**; the
   two failures are exactly the documented exceptions
   (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes` [SEGFAULT, known
   use-after-free]). No other regressions.
3. Golden replay: confirmed it **ran** (not skipped) —
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq ctest
   -R SessionReplayGolden` → `Passed` (not the "Golden created" baseline
   message), golden unchanged. (Running via plain `ctest -R
   SessionReplayGolden` alone silently skips because ctest's working
   directory is `build/`, not the source tree, so the test's default
   relative path `data/captures/session.sleq` doesn't resolve there even
   though the worktree's own `data/captures` symlink exists — the plan's
   `WILLEQ_SESSION_LOG` override is required, not just the symlink.)
4. **Acceptance — real kills against a real server, driven entirely by
   piped stdin.** LOCK FIRST protocol followed
   (`T33-loot-death-event-wiring 2026-08-03`, confirmed absent before,
   removed after clean `down.sh`). Testchar restored to zone_id=36,
   x=-77.9232, y=21.9086, z=3, h=190 both before starting and after
   finishing. `benchmarks/audits/T33/run_verification_primary.sh` +
   `transcript_primary.txt` (+`.driver.txt`): npc 79037
   `a_scaled_wolf_pup` (same guaranteed-drop mob T32 used, item 12654
   "Scaled Curskin" at 100% chance, reconfirmed against the live DB before
   the run), killed via `/combat on` + `/hunt on` + `/target` + `/attack`.
   Evidence in the transcript:
   - `Our combat target 176 died, disabling auto-attack` →
     `Combat state changed to: 3` (LOOTING) → `Awaiting corpse (id 176)`
     — **the death event**, not the poll, entering the machine.
   - `Entity 176 became corpse` → `Found corpse: ...(ID: 176)` →
     `BeginLootRequest: ... corpse ID 176` — exact id match.
   - `Loot window item: slot 22` (corpse confirmed to actually hold an
     item before any loot action) → `LootItem ACK: lootee=176 looter=175
     slot=22` → chat confirmation `"You have looted a kin."` (a pre-existing,
     out-of-scope `FormattedMessage` string-substitution bug truncates
     "Scaled Curskin" to "kin" — cosmetic, unrelated to T33's loot-state
     wiring, not touched) → `CloseLootWindow: Sent HC_OP_EndLootRequest`
     — **item actually moved**, corpse->inventory, not just states firing.
   - Liveness: `/who` (full 140+-entity roster) and `/say
     liveness-probe-during-loot` (echoed back in chat) both landed and were
     answered **inside** the ~1.1s `AwaitingCorpse`→`Closing` window
     (38.261s→39.357s in the transcript) — same liveness property T32
     proved, still holding.
   - Clean exit: `You have camped.` → `Application shutdown complete`,
     exit code 0.

   First two attempts at this script hit a genuine but **out-of-scope**
   finding, not a T33 defect: `/target <partial-name>` matches the first
   substring hit in `m_entities` (sorted by ascending spawn_id), and
   befallen accumulates persistent stale corpses/spawns across runs whose
   names also contain the search substring — so `/target wolf_pup` can
   target a leftover corpse or an unrelated live spawn instead of the
   freshly-spawned mob. Not a loot-matching bug (the loot machine's own
   id-based matching handled the resulting "wrong" kills correctly both
   times, including one where the old dead `hp_percent<=0` block fired
   for real against an already-dead stale corpse and correctly hit its
   deadline instead of hanging). Worked around in the driver script by
   reading the client's own `New spawn: a_scaled_wolf_pup<N> (ID: <id>)`
   confirmation out of the transcript and targeting that literal string,
   not by touching product code (out of this unit's territory). Recording
   here rather than silently fixing so it's visible for whoever next
   reaches for `/target` in automation.

5. **Multi-corpse check.** `benchmarks/audits/T33/run_verification_multicorpse.sh`
   + `transcript_multicorpse.txt` (+`.driver.txt`): two deliberate kills in
   one session (ids 181, then 182), plus a third, unscripted kill (auto-hunt's
   background scan picked up a leftover live spawn from an earlier run,
   id 179) — all three resolved while **3-5 unrelated stale corpses**
   from this unit's own earlier runs were already sitting in the zone
   (`is_corpse=true`, per the persistence behaviour confirmed above):
   - kill id=181 → `Awaiting corpse (id 181)` → `Found corpse:
     ...corpse181 (ID: 181)` → `BeginLootRequest: ... corpse ID 181` →
     `LootItem ACK: lootee=181` → `CloseLootWindow: ... corpseId=181`.
   - kill id=182 → same chain, id 182 throughout, **not** 181 (which was
     still present and already looted).
   - kill id=179 → same chain, id 179 throughout, **not** 181 or 182.

   Every kill looted its own corpse specifically; none re-looted or
   mismatched a neighbour's, across three separate ids and 3-5 zone
   corpses. This is the landmine-2 hazard directly exercised live, not
   just unit-tested.

6. Server left clean: `down.sh` completed, lock file removed and
   confirmed absent, Testchar restored to the canonical position. Evidence
   committed under `benchmarks/audits/T33/` (both driver scripts, both
   full transcripts, the client config).
7. `check_file_sizes.sh` — must be invoked with `--files <absolute paths>`
   from outside the worktree (plain `check_file_sizes.sh fable` resolves
   `git rev-parse --show-toplevel` against the caller's cwd, not the
   worktree, and silently checks zero files if run from `main` — a
   sharp edge worth flagging for future units). Confirmed against baseline
   (`fable` tip): `combat.h` 361→382 lines (still well under the 1500
   warn line), `combat.cpp` 1995→2028 lines (**already WARN at baseline
   pre-T33**, not a new crossing — the plan's baseline note names
   `eq.cpp`/`eq.h`/`application.cpp` but combat.cpp had already crossed
   1500 independently of this unit), `eq.cpp` 3734→3743 lines (already
   FAIL at baseline, still FAIL, not newly so). No new FAILs.

### Deviations from the plan

- Kept `ProcessCombatRound()`'s dead `hp_percent<=0` block instead of
  removing it, per the "only if you can show it is unreachable, not
  merely redundant" clause — see the dedicated section above for the
  cross-thread race that keeps it from being provably dead.
- Discovered and worked around (script-side only) a `/target` partial-name
  ambiguity against persistent stale corpses/spawns, not in this unit's
  territory to fix.
- `check_file_sizes.sh fable` (no `--files`) silently checks nothing when
  invoked from outside the target worktree; used `--files` with absolute
  paths instead and confirmed the result against manually-diffed baseline
  line counts.
