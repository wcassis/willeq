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

(Executor fills: where the entry was wired and the ordering rationale,
the corpse-identity evidence from T32's transcript or a live run, whether
the old `hp_percent` block was removed and the proof it was unreachable,
the gate left intact, the acceptance transcript with items actually
looted, the multi-corpse check, deviations.)
