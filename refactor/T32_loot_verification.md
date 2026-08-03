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

(Executor fills: the exact command script, the transcript, whether items
looted, the liveness evidence during looting, the state transitions
observed, the anti-hang exits if provoked, the `race=415` re-test verdict,
and — plainly — whether T26's loot cycle is now VERIFIED or NOT.)
