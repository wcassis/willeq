# T30 — Headless client must shut down cleanly and exit on `/quit`

**Slot: t1. x86 only. No Pi. eqemu only for the "with a server" leg
(lock protocol applies).**

**Territory: the thread-lifecycle/shutdown path (whichever component
owns the unjoined thread — identify it, don't assume),
`src/client/application.cpp`'s quit/`m_fullyConnected` gating, and the
exit-matrix tests.** Do NOT touch `include/common/logging.h` — that is a
separate known defect with its own unit coming (see deferred.md,
"Non-FATAL log output never reaches stdout"); if invisible logging makes
diagnosis hard, note it, don't fix it here.

Read `refactor/EXECUTOR_PROTOCOL.md` first, then **`docs/decisions.md`
2026-08-02 "Automation is a first-class requirement"** (this unit exists
under that policy), then the deferred.md entry **"Headless client aborts
on shutdown and never exits when driven headlessly without a server"** —
that entry is orchestrator-verified ground truth with an exact repro.

## The defect

Repro, with **no eqemu server running**:
```
printf '/help\n/quit\n' | timeout 100 <build>/bin/willeq \
    -c /home/user/projects/claude/casterella.json --no-graphics
```
Two problems, both reproduced 2/2 by the orchestrator:

1. **`terminate called without an active exception`** on stderr — the
   classic signature of a `std::thread` destructor running while the
   thread is still joinable (neither joined nor detached).
2. **The process never exits**; `timeout` must kill it (exit 124).
   `/quit` piped to stdin does not terminate the client, because the
   input bridge that drains piped commands is gated on
   `m_fullyConnected` (`application.cpp:840`) — with no server the
   command is never dispatched at all.

**Attribution already done — do not redo it:** identical behaviour on
both the T29 branch and the pre-T29 `fable` tip, so this is pre-existing
and T29 did not cause it. What is NOT known: whether it regressed since
P36 (which explicitly claimed "exit matrix all clean: 5×/camp exit-0 +
SIGTERM + script-end + headless") or is a case that matrix never covered
(headless + piped stdin + **no server**). Establishing which is part of
this unit — check P36's own matrix and say plainly which it is, because
if it is a regression, something else changed and that matters.

## Why it blocks automation

Under the standing policy this is a defect, not a quirk. Every automated
headless run that ends this way exits non-zero via `timeout` and dumps
core, so **process exit codes are useless as a CI signal** and every run
leaves a core file behind. Automation cannot distinguish "the client
failed" from "the client finished."

## Scope

### 1. Fix the abort

Find the thread destroyed while joinable. This codebase has a playbook
for exactly this class — P36 (self-join `EDEADLK` from
`UpdateMovement()` on two threads) and T03b (`RDPServer::stop()` closing
handles before `join()`): the correct ordering is
`running_.store(false)` → `join()` → destroy/close/free.

**Identify the specific thread with evidence** (gdb backtrace on the
abort, or `LOG`/`std::cerr` instrumentation of thread start/stop —
remembering that `LOG_*` output is currently invisible, so prefer
`std::cerr` or gdb). Do not "fix" it by wrapping something in a
try/catch or calling `detach()` — detach hides the bug and converts a
loud abort into a silent use-after-free later.

### 2. Make `/quit` work pre-connection, and make waiting observable

Two sub-problems; solve both:

- **`/quit` must terminate the client even when not connected.** A quit
  command is not a gameplay action and should not sit behind
  `m_fullyConnected`. Be careful *how* you exempt it: the gate exists
  for real reasons (the comment at `application.cpp:834-840` cites
  movement drifting the player off-map during loading, with the server
  interpolating the unbounded position). Do not open the gate for
  everything — exempt the specific class of commands that are
  client-local lifecycle (at minimum `/quit`; `/help` is a reasonable
  candidate, use judgement and justify it).
- **Held commands must be observable.** Today a script piping into a
  client that never connects gets *no stdout at all* — no way to tell
  "waiting to connect" from "hung". T29's stdout contract has no outcome
  for this. Add one (e.g. `outcome=held`/`waiting`) so a script can
  distinguish the two, and document it alongside T29's existing
  contract in that plan's Review or a successor doc.

### 3. Pin it so it stays fixed

P36 asserted a clean exit matrix and this case still escaped, which
tells you the matrix has a hole. Add the missing case as an automated
test: **headless + piped stdin + no server → clean exit, zero abort
output, sane exit code.** If that cannot be a ctest (process-level, needs
a spawn), say so and add it wherever the project's process-level checks
live, rather than leaving it un-pinned.

## STOP conditions

- The unjoined thread turns out to be inside third-party or Irrlicht
  code rather than ours → STOP and report with the backtrace; that is a
  different kind of fix.
- Fixing the abort requires changing the shutdown *order* of subsystems
  in a way that affects the graphical path → STOP and report; graphical
  shutdown is not this unit's territory and P36 territory is delicate.
- Exempting `/quit` from the connection gate would require exempting
  gameplay commands too → STOP; that gate protects against a real
  documented failure.
- Any behaviour change beyond shutdown/exit and command gating → STOP.

## Verification bar

1. `cmake --build <slot>/build -j24` clean. (Slots may carry a stale
   `third_party/irrlicht-gles2-x86/libIrrlicht.a` predating `3328570`
   → missing `eqtGPUPhaseBegin` symbols; rebuild with
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`. Stale
   artifact, not your bug.)
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (known unfixed
   product use-after-free — rerun isolated and say that is what you saw).
3. **Build and test the headless configure too** (`EQT_GRAPHICS=OFF`) —
   this unit is specifically about that path.
4. Golden replay: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq`,
   golden **unchanged**. (Each worktree needs its own `data/captures`
   symlink — `ln -s /mnt/projects/fable/captures <slot>/data/captures` —
   or this test **silently SKIPS** instead of running. Confirm it RAN.)
5. **The acceptance check**: the exact repro above must exit cleanly,
   with no `terminate called` on stderr and a sane exit code, run at
   least 5 consecutive times (this was intermittent at short timeouts —
   one clean run proves nothing).
6. **Also verify the with-a-server case still works**: bring up eqemu
   (LOCK FIRST; `up.sh` only; restore Testchar to zone_id=36,
   x=-77.9232, y=21.9086, z=3, h=190), pipe a short command script
   including `/quit`, and confirm clean exit there too — the fix must not
   only work in the no-server case.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs. (`eq.cpp`/`eq.h`/`application.cpp` already FAIL/WARN at the
   fable baseline; those are pre-existing, confirm rather than assume.)

## Review

(Executor fills: which thread was unjoined and how it was identified,
the P36-regression-or-never-covered verdict with evidence, how `/quit`
was exempted and why that class and no wider, the held/waiting stdout
outcome, the pinning test and where it lives, the 5×-clean acceptance
runs, deviations.)
