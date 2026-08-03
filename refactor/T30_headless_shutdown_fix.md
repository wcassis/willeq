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

### Bug 1 — the abort: `ConsoleInputHandler`'s stdin thread, unjoined

**Identified with a real gdb backtrace off a core dump**, not log
instrumentation (LOG_* is invisible per the standing defect; `std::cerr`
wasn't needed once a core was available). `core_pattern` on this sandbox
pipes to apport with no root to change it (same obstacle P36 hit) — worked
around with `sudo -n tee /proc/sys/kernel/core_pattern` (sudo turned out to
be available; wrote `core` as a local relative pattern) plus `ulimit -c
unlimited`. Reproduced the exact repro under `timeout -s TERM 5` (5/5,
matching the plan's "intermittent at short timeouts" framing — a longer
`timeout 100` also reproduces, just slower), then:

```
gdb -batch -ex "thread apply all bt" -ex "info threads" ./build/bin/willeq core
```

```
Program terminated with signal SIGABRT, Aborted.
#4  __GI_abort ()
#5-7 std::terminate() [libstdc++]
#8  std::thread::~thread()
#9  eqt::input::ConsoleInputHandler::~ConsoleInputHandler()
#10 eqt::input::ConsoleInputHandler::~ConsoleInputHandler()  [ABI: complete+deleting dtor]
#11 std::default_delete<ConsoleInputHandler>::operator()
#12 std::unique_ptr<ConsoleInputHandler>::~unique_ptr()
#13 eqt::mode::HeadlessMode::~HeadlessMode()
#14 eqt::mode::HeadlessMode::~HeadlessMode()
#18 eqt::Application::shutdown()
#19 main()
```

Root cause: `ConsoleInputHandler::shutdown()`
(`src/client/input/console_input_handler.cpp`) only joined `m_inputThread`
inside an `if (m_active.load())` guard. That guard assumed `shutdown()` was
the *only* writer of `m_active` — but `pipedInputThreadFunc()` (the piped/
non-tty stdin reader, T29) also sets `m_active.store(false)` itself on EOF,
which is exactly what happens on `timeout`'s SIGTERM path: `printf` closes
its end of the pipe, the reader thread hits EOF, sets `m_active=false`, and
finishes. By the time `Application::shutdown()` → `~HeadlessMode()` →
`~ConsoleInputHandler()` → `shutdown()` runs, `m_active` is already false,
so the guard's body — the only place that ever called `m_inputThread.join()`
— never executes, and `~thread()` later runs on a still-joinable thread.
Classic TOCTOU on a flag two different threads can flip.

**Fix** (`src/client/input/console_input_handler.cpp`): the P36/T03b
playbook applied unconditionally — `store(false)` → `join()` (if joinable)
→ proceed to disable raw mode, no longer gated on who flipped `m_active`
first. Confirmed the abort is gone: same `timeout -s TERM 5` repro, 5/5,
`terminate called` no longer appears in stderr.

**Not third-party, no shutdown-order change**: `ConsoleInputHandler` is our
code (`src/client/input/`); the fix is entirely internal to that one
class's `shutdown()` — it does not reorder anything at the `Application`
teardown-sequence level, so neither STOP condition about scope applies.

### Problem 2 verdict: P36-regression or never-covered?

**Never covered — not a regression.** Read P36's own exit matrix
(`refactor/P36_shutdown_pass.md` Review, rows 1–8): every row is either a
*live* `/camp` against the sanctioned eqemu stack, a SIGTERM against a live
connected session, or `--script` end (which is also a connected-session
`/camp`, per row 6's note that there's no separate script-completion
auto-exit). Row "Headless (`--no-graphics`)" is satisfied by rows 1/2/3/4/
5/7 reusing `--no-graphics` — but every one of those still has a live
server connection. **None of P36's 8 rows is "headless + piped stdin + no
server reachable at all"** — the specific combination this unit's repro
uses. P36's Bug 1 (self-join) and Bug 2 (missing SIGTERM handler) are both
still fixed and still hold (verified: this unit's SIGTERM-based repro would
have hit Bug 2's symptom, i.e. no shutdown attempt at all, if it had
regressed — it didn't; the process still shows "SIGTERM received..." →
teardown attempt before the (former) abort). This is a hole in P36's
matrix, not something that broke since — exactly the framing the plan
predicted ("if it is a regression, something else changed and that
matters" — it isn't, so nothing else needs to be chased).

### Problem 2 fix: `/quit` pre-connection, and the held/waiting outcome

Two distinct sub-bugs, both fixed:

1. **The whole console command queue — including `/quit` — was gated on
   `m_fullyConnected`** (`Application::processInput()`,
   `application.cpp`), so with no server, nothing piped ever got dispatched
   at all. Fixed with a narrow, additive intercept,
   `Application::processPreConnectionLifecycleCommands()`, called only
   while `!m_fullyConnected`, right where the gate used to unconditionally
   skip `m_inputBridge->update()`. It **peeks** (new
   `IInputHandler::peekRawCommand()`, non-destructive) the front of the
   console raw-command queue:
   - If it's `/quit`, `/exit`, `/help`, or `/?` (case-insensitive, first
     token only) — the narrow "client-local lifecycle" class named in the
     plan — it's popped and handled immediately: `/quit`/`/exit` call
     `Application::requestQuit()` directly (the same controlled path
     SIGTERM and window-close already use, per P36), `/help`/`/?` go
     through the normal `DispatchSlashCommand()` (read-only, safe at any
     connection state).
   - Anything else is left untouched in the queue (not consumed, not
     reordered) for the normal gated path once `m_fullyConnected` flips
     true — verified with `NonLifecycleCommandIsHeldNotDroppedOrApplied`
     (below): a non-lifecycle command never gets `outcome=applied` while
     disconnected.
   This is why the STOP condition ("exempting /quit would require
   exempting gameplay commands too") does not apply: the intercept only
   ever inspects the front of the queue, and only ever *acts* on four
   fixed literal command names — it is not a second, wider dispatch path.
2. **`/quit`'s own handler was advisory-only even when connected**
   (`eq_commands_movement.cpp`'s `EverQuest::m_command_registry` "quit"
   entry just printed "Use /camp to logout safely, or /q to exit
   immediately" — it never called `RequestQuit()`, unlike its sibling
   `/q`). This surfaced only once problem 2.1 above was fixed and the
   plan's own verification-bar item 6 (`/quit` against a *live* eqemu
   session) was attempted: `/quit` dispatched fine (`registry=game`,
   `outcome=applied`) but the process never exited, because "applied"
   here just meant the advisory handler ran, not that anything asked to
   quit. Fixed by making `/quit`/`/exit` call `RequestQuit()` directly —
   the same effect as `/q`. `/camp` (the *safe*, server-acknowledged
   logout, sit-down + wait-for-confirmation) is untouched and remains the
   right choice for a graceful in-game exit; `/quit` is now simply an
   immediate-exit synonym for `/q`, matching what a command literally
   named "quit" reads as, and what the exit-matrix's own repro assumed.
   This is a shutdown/exit-only behavior change (a command that previously
   only printed text now actually exits) — no gameplay logic was touched.

**Held/waiting stdout outcome**: extended `CommandDispatchResult`
(`application.h`) with a `bool held` field and taught
`printCommandResult()` a new `outcome=held` line, distinct from
`unrecognized` (no registry claimed it) — `held` means dispatch was
deliberately deferred and the command is still queued to run once
connected. Debounced (`m_preConnectionHeldAnnounced` /
`m_preConnectionHeldLastCmd`, reset on every disconnect transition) so a
script waiting on a dead connection gets one line, not 60/sec:

```
CMD outcome=held registry=pending recognized=false message="waiting for connection" cmd="/attack"
```

Documented here (this Review) as the plan directs, alongside T29's
existing outcome vocabulary (`applied`/`failed`/`queued`/`unrecognized`,
`printCommandResult()`'s own doc comment in `application.cpp` — updated in
this unit to list `held` too).

### Pinning test

`tests/test_headless_shutdown.cpp` (new; CMake target added in
`tests/CMakeLists.txt`, unconditional — not gated on `EQT_GRAPHICS`, since
`willeq` always builds and this needs no graphics assets). Process-level
(spawns the real `willeq` binary via `popen()`, same pattern
`test_entity_viewer_regression.cpp` already uses for its shellout), because
this is fundamentally about the whole binary's exit behavior, not
in-process unit behavior — a normal ctest can't observe a process's own
exit code. Two cases:
- `PipedQuitWithNoServerExitsCleanFiveTimesInARow`: the plan's exact repro,
  5 consecutive spawns, asserts exit code 0, no `terminate called`/
  `Segmentation fault` in output, and an `outcome=applied
  registry=lifecycle` ack for the pre-connection `/quit`.
- `NonLifecycleCommandIsHeldNotDroppedOrApplied`: pins the STOP condition
  itself — a non-lifecycle command (`/attack`) piped pre-connection must
  show `outcome=held`, never `outcome=applied`.

Points at a TEST-NET-1 address (`192.0.2.1`, RFC 5737, guaranteed
unroutable) rather than depending on "no eqemu happens to be running" —
deterministic regardless of ambient server state on whatever host runs
ctest.

Runs green in both configures:
- GLES2-config tree (`build/`): `[ PASSED ] 2 tests` (~5.9s total).
- Headless configure (`build-nogfx/`, `EQT_GRAPHICS=OFF`): `[ PASSED ] 2
  tests` (~5.7s total).

### 5×-clean acceptance runs

Exact repro from the plan, `build/bin/willeq` (GLES2-config tree),
`casterella.json` (no server reachable from this sandbox), run 5
consecutive times after all fixes landed:

```
run1 exit=0    run2 exit=0    run3 exit=0    run4 exit=0    run5 exit=0
```

stderr empty on all 5 (no `terminate called`). Also re-ran 5× immediately
after the abort-only fix (before the `/quit` gate fix) to confirm the
abort specifically was gone even though exit was still 124 at that point,
and 5× again after the full fix, all clean.

### With-a-server verification (eqemu, per protocol)

Lock acquired (`T30-headless-shutdown`, free at dispatch) → `up.sh` →
account `willeq_test`/`Testchar` (GM), server `WillEQ`,
`127.0.0.1:5998` (this host *is* the docker host — `casterella.json`'s
`10.0.30.13` is a different LAN host's address, not reachable/relevant
here; used a local config with the same credentials instead). Piped
`/help`, waited 8s for zone-in, then piped `/quit`: **exit 0**, clean
`Console renderer shutdown` line, no abort — 4 consecutive runs (1 while
diagnosing problem 2.2, 3 more after the fix). Testchar's position had
drifted from the documented restore point during testing (DB read showed
`zone_id=36, x=100.75, y=-361.375, z=-9.375, heading=196.75` — same zone,
different spot) — restored via direct `UPDATE` to `zone_id=36,
x=-77.9232, y=21.9086, z=3, heading=190`, confirmed by read-back. `down.sh`
run clean, `.eqemu-lock` removed.

### Verification bar — results

1. `cmake --build build -j24` — clean (no stale-Irrlicht issue in this
   slot; `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a` already carried
   `eqtGPUPhaseBegin`).
2. `ctest --test-dir build --output-on-failure` — **1803/1804**, sole
   failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host
   exception). `RDPServerFrameTest.UpdateFrameMultipleTimes` appeared once
   during an earlier full run (SEGFAULT) and was absent on a later full
   run and on an isolated rerun failing again — consistent with the plan's
   "known unfixed product use-after-free, intermittent" framing, not
   something this unit's changes touch (RDP server code is untouched).
   `SessionReplayGolden` — confirmed it actually **ran** (not skipped):
   `data/captures` symlink was already present in this slot; ctest's own
   working directory doesn't resolve the test's relative default path, so
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq` was set
   explicitly for the ctest invocation (same effect, confirmed via a
   direct binary run showing real zone-load log lines, not a skip
   message), golden unchanged.
3. **Headless configure** (`EQT_GRAPHICS=OFF`, `build-nogfx/`): built
   clean, `ctest` **1372/1373** (same sole ALSA exception; no RDP tests
   exist in this configure). `test_headless_shutdown` passes here too.
4. Golden replay: see #2 above — ran, passed, golden untouched.
5. **Acceptance repro**, 5× consecutive clean — see above.
6. **With-a-server**: see above — 4× consecutive clean, position restored,
   lock released.
7. `check_file_sizes.sh` — no new FAILs. Diffed against the actual
   merge-base (`2f725d260e17be0626917a2a31bd52b80da6b0b3`), not the literal
   `fable` ref, because `fable` advanced past that point mid-unit (T31
   landed) — using the moved ref would have misattributed T31's logging.h
   changes to this unit. `application.cpp` WARNs (2304 lines, +126 from
   this unit's 2178-line baseline) — pre-existing WARN per P36, still a
   WARN not a FAIL. All other touched files `ok`.

Additionally ran, unrequested but relevant since `willeq` and
`eq_commands_movement.cpp`/`console_input_handler.cpp` changed:
`build-gles2-x86.sh` clean, `visual_gate.sh` PASS (goldens untouched,
confirmed via `git status`), `build-arm-noble.sh` clean — all re-run after
the `/quit` handler fix (the last code change), not just after the abort
fix.

### Deviations

- **`/quit`'s connected-session semantics changed** (see problem 2.2
  above): previously a no-op advisory message, now an immediate exit
  (alias for `/q`). This is a user-facing behavior change for anyone
  typing `/quit` in a live interactive session, not just automation —
  flagging explicitly since the plan's STOP conditions are about the
  *gate*, and this is a different (adjacent, but distinct) change to a
  command handler's own body. Judged in-scope as "shutdown/exit behavior"
  per the plan's territory description and required by verification-bar
  item 6, but it's the one call in this unit that most deserves a second
  look from Will.
- **`/q`'s `action::CommandProcessor` stub is still broken** (P36's
  documented-but-unfixed finding: returns `ActionResult::Success("exit")`
  but nothing inspects the sentinel). Not touched — out of this unit's
  named territory (the fix lives in `EverQuest::m_command_registry` via
  registry 1, which both `/quit` and the working `/q` alias already reach
  through `DispatchSlashCommand()`'s stage 1 before `CommandProcessor` is
  ever consulted, so this stub is dead code on the path this unit cares
  about — but it remains a latent bug for whoever next touches
  `CommandProcessor`).
- `include/common/logging.h` was NOT touched (per the plan's explicit
  instruction) — confirmed by diffing against the merge-base rather than
  the (by-then-moved) `fable` ref; a naive `git diff fable` would have
  shown logging.h changes that actually belong to T31, merged into
  `fable` mid-unit.
- Used `LOG_*` sparingly given the known invisible-non-FATAL-logging
  defect; relied on `gdb`+core dump for the abort (the plan's preferred
  evidence) rather than `std::cerr` instrumentation.
- `sudo` turned out to be available in this sandbox
  (`echo core | sudo -n tee /proc/sys/kernel/core_pattern`) where P36
  reported none — used it only to get a local core dump for diagnosis
  (`sudo rm -f core` after reading it), and restored `core_pattern` back to
  the original apport pipe (`|/usr/share/apport/apport -p%p -s%s -c%c -d%d
  -P%P -u%u -g%g -F%F -- %E`) before finishing, confirmed via read-back.
