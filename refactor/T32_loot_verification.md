# T32 — Finish T26's kill→loot verification (the leg that has never been proven)

**Slot: t1. x86 only. No Pi. OWNS EQEMU (lock protocol applies).**
No sibling unit — you have the tree and the server to yourself.

**Territory: verification only.** This unit is expected to land **no
production source changes**. If you find a real defect, report it — do
not fix it here unless it is a one-line blocker to running the check at
all, and say so loudly if you do.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T26_loot_state_machine.md` (the unit being verified, including
its Review), then `docs/decisions.md` 2026-08-02 "Automation is a
first-class requirement".

## Why this unit exists

T26 replaced four blocking `sleep_for` calls on the game thread (~1.1 s
per kill, 1.6 s with auto-hunt, during which the client drained no
packets) with an event-driven `LootSubState` machine. It is **merged and
shipping**, but its behavioural verification has never completed:

- T26 itself could not reach `/combat on` from automation at all.
- T29 closed that tooling gap — piped stdin now reaches the real
  registry, confirmed against a live server — but still could not
  complete a kill→loot cycle, blocked by pathfinding and an asset crash.

So the honest status today is: **the loot cycle's correctness rests on
unit tests over the production code plus a wiring citation, not on
evidence that a kill actually loots.** This unit closes that.

## What changed in your favour since the last attempt

All three are new since T29 tried this, and together they should make
this straightforward where it previously was not:

1. **T29** — slash commands pipe to stdin and reach the real registry,
   with per-command `CMD outcome=` lines on stdout.
2. **T31** — non-FATAL log output now actually reaches stdout. Previously
   `LOG_INFO`/`WARN`/`DEBUG` never flushed during normal operation, so
   all chat/system/combat narration was invisible. **This is the one that
   makes observation possible at all** — T26's state machine logs its
   transitions at DEBUG.
3. **T30** — the client exits cleanly on `/quit`/`/exit`/`/q` with a
   usable exit code, so a scripted run can end deterministically.

**And a fourth, found while scoping this unit:** the test config
`/home/user/projects/claude/casterella.json` had **three stale asset
paths** (`eq_client_path`, `maps_path`, `navmesh_path` all pointed at the
pre-`/mnt/projects` layout and did not exist). Fixed 2026-08-03; see the
deferred.md entry. That is very likely what T29 hit as "a missing zone
map file degrading pathfinding", and possibly the `race=415` asset crash
too. **Verify the paths resolve at the start of your run rather than
assuming** — the client previously failed to complain loudly about this.

## The verification

### Primary: a real kill→loot cycle, driven entirely by piped stdin

Bring up eqemu (**LOCK FIRST** per EXECUTOR_PROTOCOL.md: check
`/mnt/projects/fable/.eqemu-lock` does not exist, create it with your
unit ID and date, delete it after clean shutdown; `scripts/eqemu/up.sh`
only; restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190
when done).

Drive the client **entirely by piping slash commands** — no keyboard.
Testchar is a GM, so **spawn a mob adjacent to the player rather than
walking to one**: pathfinding is not what is under test here, and
removing it removes the thing that stopped the last attempt. Use GM
commands (`#spawn` and friends) to put a killable, **lootable** mob in
range. A mob with no loot does not exercise the item path — confirm the
corpse actually has items before drawing conclusions from a clean run.

Then: `/combat on`, target it, kill it, and observe the auto-loot cycle.
**What must be true, and what you must show evidence for:**

- Items actually loot (the point of the exercise).
- The client **never freezes** — packets keep flowing throughout. A
  cheap liveness probe: interleave `/who` or a chat line during the loot
  sequence and confirm it is answered while looting is in progress. That
  is the specific property T26 changed; before T26 the game thread was
  blocked ~1.1 s and nothing was drained.
- The state machine advances through its states. With T31's fix, T26's
  own `LOG_DEBUG(MOD_COMBAT, …)` transitions
  (`AwaitingCorpse` → `AwaitingItems` → `LootingItems` → `Closing`)
  should be visible — capture them; they are the clearest proof the new
  path ran rather than some fallback.

Capture the stdin script and the full stdout/stderr transcript.

### Secondary, if cheap: the anti-hang exits

T26's real risk was never the stall — it was that removing the sleeps
without deadlines would trade a 1.1 s stall for a permanent hang in
`LOOTING`. The unit tests cover the deadline and corpse-disappeared
exits synthetically; if you can provoke either one live without a lot of
work (e.g. let a corpse rot / despawn mid-sequence), do it and report.
**Do not spend the whole unit here** — the primary check is the
deliverable.

### Also re-test: does `race=415` still crash in graphics mode?

T29 could not run its acceptance demo in graphical mode at all because of
a `race=415` missing-model-data crash. Now that `eq_client_path` resolves,
re-test whether that still reproduces. If it is gone, say so — Will's
ruling specifically wants "the same commands in a real session", and
*real* means graphical. If it still crashes, capture the evidence and
report it as a live finding; do not chase the fix here.

## STOP conditions

- The loot cycle **misbehaves** — items do not loot, the client freezes,
  the machine hangs in a state, packets stop flowing → **STOP and report
  loudly and immediately.** That is a defect in shipped code and the most
  important possible outcome of this unit. Do not attempt a fix; capture
  the evidence (transcript, state transitions, and a gdb backtrace if it
  hangs) and hand it back.
- You cannot get a lootable mob spawned and killed after reasonable
  effort → STOP and report what blocked you, with specifics. Another
  environment gap is a real finding, not a failure.
- You find yourself changing production source to make the check pass →
  STOP. That inverts the point of a verification unit.

## Verification bar

This unit's deliverable IS evidence, so the bar is mostly about not
breaking anything while producing it:

1. `cmake --build <slot>/build -j24` clean. (Slots may carry a stale
   `third_party/irrlicht-gles2-x86/libIrrlicht.a` predating `3328570` →
   missing `eqtGPUPhaseBegin` symbols; rebuild with
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.)
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (known unfixed
   product use-after-free — rerun isolated and say that is what you saw).
   Only needed to show you left the tree as you found it.
3. The captured stdin script + stdout/stderr transcript, committed
   somewhere sensible under `benchmarks/audits/` so the evidence outlives
   this conversation.
4. Server left clean: lock removed, Testchar's position restored, eqemu
   shut down via `down.sh`.

## Review

### Verdict: T26's loot cycle is **NOT VERIFIED — it is DISPROVEN**

This unit completed everything the previous two attempts (T26, T29) were
blocked on — real `/combat on`, a real adjacent lootable spawn, a real
kill, no pathfinding needed, no keyboard — and the result is not "still
unverified," it is a **live-reproduced defect**: on a real kill against a
real server, T26's entire `LootSubState` machine is never entered. Not
"hangs," not "loots slowly" — never starts. This is the loudest possible
outcome of this unit and is reported accordingly, per the plan's STOP
condition ("items do not loot ... STOP and report loudly and
immediately").

### Root cause (found by reading the transcript, not by guessing)

There are **two independent, disjoint death-detection code paths**, and
only one of them was ever wired to the loot state machine:

1. **`CombatManager::ProcessCombatRound()`** (`src/client/combat.cpp:1096`,
   invoked only while `m_combat_state == COMBAT_STATE_ENGAGED`, from
   `CombatManager::Update()`'s switch at `combat.cpp:162`): polls
   `entity.hp_percent` on its own periodic HP-update cadence. Its
   `hp_percent <= 0` branch (`combat.cpp:1150-1190`) is the **only** call
   site that ever calls `SetCombatState(COMBAT_STATE_LOOTING)` +
   `EnterLootSubState(LootSubState::AwaitingCorpse)` for an auto-hunt
   kill. This is what T26 built, tested, and documented.
2. **`EverQuest::ZoneProcessDeath()`** (`src/client/eq.cpp:3131`): the
   handler for the server's actual, authoritative death packet
   (`Death_Struct`: victim/killer/corpse/spell/damage). This is what a
   **real kill on a real server actually delivers first** — packets are
   processed on the game thread the instant they arrive, not on
   `ProcessCombatRound()`'s next scheduled poll. It sets
   `entity.hp_percent = 0` and `is_corpse = true` (`eq.cpp:3175-3177`,
   correctly recognizing the death), fires chat messages, and — if the
   victim was our current target — calls
   `m_combat_manager->DisableAutoAttack()` (`eq.cpp:3234-3238`).

`DisableAutoAttack()` (`combat.cpp:431-437`) unconditionally does
`if (m_combat_state == COMBAT_STATE_ENGAGED) SetCombatState(COMBAT_STATE_IDLE);`
— synchronously, inside the packet handler, before `ProcessCombatRound()`
gets another tick. Once state flips to `IDLE`, `Update()`'s switch no
longer calls `ProcessCombatRound()` at all (case `ENGAGED` only,
`combat.cpp:162-164`), so its `hp_percent <= 0` branch — the only door
into `AwaitingCorpse` — is now **permanently unreachable for that kill**.
`ZoneProcessDeath()` never itself calls anything loot-related; it only
disables auto-attack. Net effect: the real death path race-wins, disarms
the only trigger the synthetic-HP-poll path needed, and the loot machine
never starts. `IsLooting()` (`combat.h:203`) stays false for the rest of
the corpse's life; nothing ever sends `HC_OP_LootRequest`.

I confirmed this is not one unlucky run: grepped the full primary
transcript for every loot-machine marker string
(`Awaiting`/`LootSubState`/`BeginLootRequest`/`EnterLootSubState`/
`COMBAT_STATE_LOOTING`/`CloseLootWindow`/`LootItem`/`AddLootItem`) —
**zero matches**, from spawn through kill through 60+ seconds of
post-kill observation to clean `/camp` shutdown. The corpse
(`a_scaled_wolf_pup's_corpse171`) sat there, holding its guaranteed item,
untouched, for the rest of the session.

This gap is invisible to T26's own unit tests because they drive
`hp_percent <= 0` directly through `ProcessCombatRound()` (or via
`UpdateLootState()` ticks with hand-backdated timestamps) without ever
also simulating a concurrent `ZoneProcessDeath()` packet arrival racing
in first — the synthetic tests can't lose a race that only exists against
a real packet-driven death. That's exactly why unit tests + a wiring
citation (T26's own acknowledged evidence gap) were not sufficient, and
why this unit — the first to get a real kill against a real server all
the way through — is what surfaced it.

### The verification method (what worked, unlike T26/T29)

Full protocol, LOCK FIRST: `/mnt/projects/fable/.eqemu-lock` confirmed
absent, created `T32-loot-verification 2026-08-03`, removed after clean
`down.sh` (`ls` confirmed absent afterward). Server via
`scripts/eqemu/up.sh arena` (`seed.sh` run first to (re)provision
`willeq_test`/`Testchar`). Client driven **entirely by piped stdin** — a
named FIFO (`benchmarks/audits/T32/cmds.fifo`), `willeq` launched
reading from it in the background, commands `echo`'d to a held-open write
fd from a driver script that polls the transcript for expected log lines
between sends (bounded loops, never blind sleeps) — genuinely no
keyboard, no `--script` file. Config:
`benchmarks/audits/T32/willeq_t32.json` (account `willeq_test`, character
`Testchar`, `eq_client_path=/mnt/projects/EverQuestP1999` — verified to
resolve before the run, per this unit's own scoping note about the
previously-stale `casterella.json`; Testchar's own zone_id=36 resolves to
`befallen`, not `arena` — `up.sh arena` starts a zone process that
Testchar's saved position never uses, a detail worth noting for whoever
next reaches for `up.sh`'s default zone thinking it's the zone they'll
land in).

**Pathfinding-avoidance, achieved as instructed:** `#npctypespawn` spawns
at the GM's own position (confirmed by reading
`akk-stack/code/zone/gm_commands/npctypespawn.cpp`:
`new NPC(npc_type, 0, c->GetPosition(), ...)`), so the target was always
`distance=0.0` (`ProcessCombatRound: target=..., distance=0.0, ...
in_range=true`, every tick) — no movement/pathfinding was ever exercised
or needed for combat itself. `/pathfinding on` was still required as a
**flag** (`CombatManager::SetAutoHunting()` refuses to enable without it:
`combat.cpp:863-866`), which is orthogonal to actual path-following.

**Mob selection — a real finding along the way, not the headline one:**
first attempt used npc 19001 "Chomper" (T26's convention of "any GM-spawn
works" undersold this) — it conned **yellow** to level-1 Testchar and
`CombatManager::IsTargetSuitableForHunt()` (`combat.cpp:1467-1519`)
silently excluded it from auto-hunt's target scan even at distance 0.0
(`"Chomper000 is NOT suitable for hunting"`). Re-picked npc **79037
`a_scaled_wolf_pup`** (level 1, matching Testchar; non-humanoid race 42,
satisfying the suitability rule for non-aggressive-faction mobs) with
loottable 103626 → lootdrop 156971 → item 12654 "Scaled Curskin" at
`probability=100`/`chance=100` (verified against the live DB before the
run — a guaranteed drop, not a maybe, so a clean corpse-with-no-items
result was never on the table as an ambiguity). Confirmed live at spawn:
`[Loot] [AddLootDrop] NPC [a_scaled_wolf_pup000] Item (12654) ... chance
[100]`.

Command order also mattered and is worth recording: `/hunt on` calls
`SetAutoHunting(true)`, which unconditionally forces
`SetCombatState(COMBAT_STATE_HUNTING)` — sent immediately followed by
`/target wolf_pup` + `/attack` (no delay) to win the race against
auto-hunt's own background target scan (`FindNextHuntTarget()`, throttled
to every 500ms) before it could claim a different pre-existing zone spawn
(`befallen` has ~140 existing NPCs; 23 were already in scan range).

### Primary transcript: the kill, in full

`benchmarks/audits/T32/transcript_primary.txt` (full stdout/stderr, `-d 5`
DEBUG; `.log` renamed `.txt` so it survives the repo's `*.log` gitignore
rule) + `transcript_primary.driver.txt` (driver's send/match log) +
`benchmarks/audits/T32/run_verification.sh` (the exact script). Key
excerpt (timestamps trimmed for the walk here, full timestamps in the
file):

```
CMD outcome=applied registry=action recognized=true message="Auto-hunting enabled" cmd="/hunt on"
CMD outcome=applied registry=game recognized=true cmd="/target wolf_pup"
CMD outcome=applied registry=game recognized=true cmd="/attack"
[DEBUG] [COMBAT] Target set to a_scaled_wolf_pup000 (ID: 171)
[DEBUG] [COMBAT] EnableAutoAttack: Auto attack ENABLED (sent both AutoAttack and AutoAttack2)
[DEBUG] [COMBAT] ProcessCombatRound: target=a_scaled_wolf_pup000 (171), distance=0.0, combat_range=8.5, in_range=true
  ... (real melee rounds, real damage both directions, ~17s fight) ...
[INFO ] [COMBAT] a_scaled_wolf_pup000 (171) was killed by Testchar (170) for 2 damage
[DEBUG] [COMBAT] You have slain a scaled wolf pup!
[DEBUG] [COMBAT] Our combat target 171 died, disabling auto-attack
[DEBUG] [COMBAT] Combat state changed to: 0
[INFO ] [ENTITY] Entity 171 became corpse: 'a_scaled_wolf_pup000' -> 'a_scaled_wolf_pup's_corpse171'
[INFO ] [MAIN] [CombatRecord] [Stop] [Summary] Mob [a scaled wolf pup] [Received] DPS [30] Heal/s [0] Duration [17 Seconds] (17s)
```

No `Awaiting corpse`, no `BeginLootRequest`, no `LootItem`, no
`CloseLootWindow` — anywhere, ever, for the rest of the ~65s the process
ran afterward. `Combat state changed to: 0` is `COMBAT_STATE_IDLE`, not
`COMBAT_STATE_LOOTING` (`4`) — confirming the state machine this unit was
verifying was never entered.

### Liveness — the one half of the primary check that DID hold

Despite the loot cycle never starting, T26's actual change (removing the
blocking sleeps from the game thread) is **not** contradicted by this
run, and the liveness evidence is real and worth keeping:

- `/who` (baseline, before combat) and a second `/who` + `/say
  liveness-probe-during-loot` sent immediately after the kill both got
  `CMD outcome=applied` acks and were answered essentially instantly
  (`[CHAT] Testchar (say): liveness-probe-during-loot` and a full 140+
  entry `/who` roster printed within ~1s) — no stall of any kind.
- `POS C->S`/`POS S->C` movement-sync packets kept flowing continuously
  throughout, before, during, and after the kill, with no gap.
- A clean `/camp` produced `Application shutdown complete`, exit code 0.

This just isn't evidence of the loot cycle working — there was no loot
cycle in progress to probe during. It's evidence that *if* the loot state
machine had run, nothing in T26's own tick-based design would have
blocked the game thread. That part of T26's design goal is intact; its
wiring to the real death event is not.

### Anti-hang exits: not exercised

Not reached — the state machine was never entered, so there was nothing
to provoke a deadline/corpse-gone exit from. This is downstream of the
headline finding, not a separate gap.

### `race=415` graphics-mode re-test: still crashes, unchanged from T29

`benchmarks/audits/T32/run_race415_graphics.sh` +
`transcript_race415_graphics.txt` (+`.driver.txt`). Ran `build-gles2/bin/willeq`
in full graphics mode (surfaceless EGL, no `--no-graphics`), zoned into
`befallen` (graphics init succeeded: `Graphics Init 685 ms`), spawned npc
**351043** (`a_rat`, race=415, gender=2 — the exact asset T29's finding
named), and it crashed identically:

```
[DEBUG] [GRAPHICS] RaceModelLoader::preloadModelData: no model found for race=415 gender=2
[FATAL] [GRAPHICS] Failed to load race model: race=415 gender=2 (not found in any S3D archive)
[FATAL] [GRAPHICS] loadZoneGraphicsOnThread: sequential loader failed — requesting quit
[FATAL] [GRAPHICS] checkLoadingComplete: loading thread failed — shutting down
```

**Verdict: `eq_client_path` resolving (this unit's other fix) did NOT
fix this — it is a genuinely separate, still-open gap**, isolated to
race=415's model data being absent from every searched S3D archive
(`global_chr.s3d` through `global7_chr.s3d`, all real cache
hits/misses/loads, none containing it), not a path-resolution problem.
Good news within the bad news: it is **not a hard crash** — the loader
detects the failure, requests a clean quit, and the process exits 0 with
a normal shutdown sequence, not a segfault or hang. Not chasing a fix
here per the plan ("do not chase a fix"); flagging as a live-confirmed,
still-open finding for a future unit. (A second, likely-unrelated,
already-known issue also visible in this transcript:
`[ERROR] [MAP] Unable to open map file: ./maps/befallen.map` — a
relative-path collision-map lookup, distinct from the `maps_path`/
`navmesh_path` config keys this unit's scoping fixed; not investigated
further, out of scope.)

### Server hygiene

Lock: created `T32-loot-verification 2026-08-03` after confirming absent,
removed after `down.sh` completed (`ls` confirmed absent both before
creation and after removal). Testchar: DB position before restore was
already `zone_id=36, x=-77.875, y=21.875, z=3, heading=192` (within float
rounding of canonical, matching T26's own note that `/camp` correctly
persists position) — explicitly re-set to the documented canonical exact
values (`x=-77.9232, y=21.9086, z=3, heading=190`) regardless. `down.sh`
ran clean (all four containers stopped/removed). No stray `willeq`
processes left running (checked via `pgrep` after each run and again at
the end).

### Verification bar results

1. `cmake --build build -j24` — clean, no changes needed (worktree's
   patched Irrlicht was already current, unlike some prior slots).
2. `ctest --test-dir build --output-on-failure` — 1803/1805
   (`AudioManagerTest.PreloadCommonSounds`, documented ALSA host
   exception; `RDPServerFrameTest.UpdateFrameAfterStart` SEGFAULT on the
   full run, confirmed the documented pre-existing RDP use-after-free
   flake by re-running the `RDPServerFrameTest` family in isolation:
   5/5 passed clean). No new failures.
3. `bash scripts/build-gles2-x86.sh` — clean, produced
   `build-gles2/bin/willeq` (used for the race=415 graphics retest) and
   `gles2_zone_benchmark`.
4. Evidence committed under `benchmarks/audits/T32/`: both driver scripts,
   both full transcripts (+`.driver` logs), and the client config used.
5. Server left clean (see above).

No production source was touched — `git status` shows only the new
`benchmarks/audits/T32/` directory. Per the plan's own rule ("You find
yourself changing production source to make the check pass → STOP"), the
fix for the race condition found above is explicitly **not** attempted
here.

### Deviations from the plan

- The primary kill used `a_scaled_wolf_pup` (npc 79037), not a mob named
  in the plan — the plan didn't name a specific mob, but the natural
  first choice (a rat/skeleton-style throwaway, matching T26's own prior
  convention) turned out to be unsuitable for auto-hunt's own con-color
  filter, which is itself worth recording as a minor, separate finding
  (`combat.cpp:1467-1519`'s `IsTargetSuitableForHunt()` silently drops
  yellow-con targets even at distance 0 — not investigated further, not
  a T26 defect, just a sharp edge for whoever next spawns a test mob by
  hand).
- Anti-hang exits (secondary, "if cheap") were not exercised, per the
  above — there was nothing to provoke once the headline finding made it
  clear the state machine never starts on a real kill.
- The unit's original framing ("closes the gap... loot cycle's
  correctness rests on unit tests + a wiring citation") is superseded:
  this unit did not close the gap by supplying the missing positive
  evidence. It closed the gap by proving the negative — the wiring
  citation in T26's Review (`Application::updateGameState()` calling
  `UpdateLootState()` unconditionally) is accurate and irrelevant to the
  actual defect, which is entirely inside `CombatManager`/`EverQuest`'s
  two-death-paths race, upstream of `UpdateLootState()` ever being
  reached with real work to do.
