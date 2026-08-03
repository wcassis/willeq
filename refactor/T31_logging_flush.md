# T31 — Non-FATAL log output must actually reach stdout

**Slot: t2. x86 only. No Pi. NO eqemu — T30 is running concurrently in
t1 and owns the server lock; do not touch it.**

**Territory: `include/common/logging.h` (and its .cpp if one exists),
plus any new tests you add for it.** T30 owns the thread-lifecycle/
shutdown path, `application.cpp`'s quit gating, and the exit-matrix
tests — stay out of those. Reading them is fine.

Read `refactor/EXECUTOR_PROTOCOL.md` first, then **`docs/decisions.md`
2026-08-02 "Automation is a first-class requirement; slash commands are
THE command channel"** — this unit exists under that policy — then the
deferred.md entry **"Non-FATAL log output never reaches stdout during
normal operation"**, which is the verified finding this unit fixes.

## The defect

`common/logging.h`'s per-thread double buffer (`ThreadLogBuffer`) flips
its active/drain sides only on `LOG_FATAL` or thread exit:
`FlushThreadLog()` is called by the `LOG_FATAL` macro and **nowhere
else**. The periodic drain thread (`LogManager::drainLoop()`, 50 ms)
only ever reads the *inactive* side — which, absent an explicit flip,
nothing ever writes to for non-FATAL levels.

**Net effect: `LOG_INFO` / `LOG_WARN` / `LOG_DEBUG` / non-fatal
`LOG_ERROR` never reach stdout during normal operation, regardless of
`--log-level` or `-d`.** `AddChatSystemMessage`,
`ConsoleBridge::applyEvent`, and essentially all chat/system/combat
narration go through those macros, so the client's own account of what
it is doing is invisible.

**Corroborating detail** (orchestrator, independent of the T29 executor
who found this): running the headless client with a bad config emits a
`[ERROR] [CONFIG]` line followed immediately by `[FATAL]` — the ERROR
becomes visible precisely because the FATAL that follows flushes the
buffer. Consistent with the diagnosis. Note also that
`=== WillEQ Console Mode ===` reaches the terminal because it is a
direct `std::cout` write, not a `LOG_*` call — do not let that mislead
you into thinking logging works.

**Known wrinkle, and the first thing to resolve:** an isolated
standalone repro of `logging.h` alone reportedly works CORRECTLY; only
the full app's steady state (with no FATALs firing) exposes the failure.
Explain that difference before you fix anything — it is the difference
between fixing the real bug and fixing a plausible-looking one. If the
standalone case passes, something about the app (thread lifetimes, when
buffers are registered, level filtering, a second `LogManager`, static
init order) is load-bearing. Find out what.

## Why this matters

Under the standing policy this is a defect, not an inconvenience. T29
gave automation a way to *send* commands and a `CMD outcome=` line
confirming dispatch — but a script still cannot **observe the game's
response**. Half the automation loop is missing. This very likely
contributed to T29's own live loot-cycle verification failing to
complete, and it will block the next attempt the same way.

## The constraint that makes this non-trivial

The double buffer exists to keep logging off the hot path. **This is a
frame-time-sensitive codebase**: the Orange Pi tier runs to a 33.3 ms
budget and the 30 fps KPI currently sits ~1 ms under the line at p95.
The naive fix (always flip, or flush on every call) risks drain-thread
lock contention and a `std::cout` write every 50 ms per thread, which is
exactly what the current design was avoiding.

So: fix correctness **without** putting work on the logging call path.
Whatever you choose, argue the cost explicitly in the Review. Reasonable
shapes include flipping when the drain side is empty and the active side
is not (the diagnosis patch that proved the cause), a size/time
threshold, or a proper lock-free handoff — your call, but justify it.

**The default log level is 0/NONE (quiet), so production volume is low
and the common case must stay ~free.** The level that matters for this
unit is the raised level a developer or a script asks for.

## Scope

1. **Explain the standalone-passes / app-fails difference** (above) with
   evidence, before fixing.
2. **Fix the flush** so non-FATAL levels reach stdout during normal
   operation at whatever level was requested, with bounded latency (the
   50 ms drain cadence is a reasonable target; say what you achieve).
3. **Do not regress the hot path.** No new work per `LOG_*` call beyond
   what is already there, and no new lock on the logging call path.
4. **Pin it.** A test that would have caught this: emit non-FATAL lines
   at a raised level from a running system, assert they appear, with no
   FATAL anywhere in the test. A unit test over `logging.h` alone is not
   sufficient on its own given the wrinkle above — if the standalone
   case already passes today, a standalone test proves nothing. Pin it
   at a level that reproduces the real failure, and say why your test
   would have failed before the fix (verify that: run it against the
   unfixed code and confirm it fails).

## STOP conditions

- The real cause turns out to be outside `logging.h` (e.g. a second
  `LogManager` instance, static init order across TUs, level filtering
  elsewhere) → STOP and report before changing anything; the fix may
  belong in another unit's territory.
- A correct fix would require changing the logging threading model
  (dropping the double buffer, changing the drain thread's ownership) →
  STOP and report; that is a design change, not a bug fix.
- You measure any frame-time regression attributable to logging at the
  default level → STOP.
- Any behaviour change beyond log delivery → STOP.

## Verification bar

1. `cmake --build <slot>/build -j24` clean. (Slots may carry a stale
   `third_party/irrlicht-gles2-x86/libIrrlicht.a` predating `3328570` →
   missing `eqtGPUPhaseBegin` symbols; rebuild with
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`. Stale
   artifact, not your bug.)
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (known unfixed
   product use-after-free — rerun isolated and say that is what you saw).
   **`logging.h` is included nearly everywhere, so treat the whole suite
   as your blast radius, not a formality.**
3. Build the headless configure (`EQT_GRAPHICS=OFF`) too.
4. Golden replay: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq`,
   golden **unchanged**. (Each worktree needs its own `data/captures`
   symlink — `ln -s /mnt/projects/fable/captures <slot>/data/captures` —
   or this test **silently SKIPS**. Confirm it RAN.)
5. **The acceptance check**: run the real client headless at a raised
   log level with no server, and show `LOG_INFO`/`LOG_WARN` lines
   arriving on stdout during steady-state operation with no FATAL
   involved. Include a before/after transcript — before is the whole
   point, since "logs appear" is unconvincing without showing they
   did not.
6. **Cost evidence**: show the hot path is not slower. A microbenchmark
   of the logging call path at default level, or a reasoned argument
   from the code plus a measurement of the drain cadence, is acceptable
   — but say which and show numbers. "Should be fine" is not evidence.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs (`eq.cpp`/`eq.h`/`application.cpp` already FAIL/WARN at
   baseline; confirm rather than assume).

## Review

### 1. Why the standalone repro passed while the app failed

`ThreadLogBuffer::~ThreadLogBuffer()` calls `LogManager::unregisterBuffer()`,
which does an **unconditional** drain of both sides regardless of the
active/inactive flip state (`include/common/logging.h:219-248`). A short-lived
standalone program that logs a few lines and returns triggers this
destructor almost immediately — so it "passes" by catching the exit-time
safety net, not by proving the periodic (50 ms) drain path works. The
app's game/render threads are long-lived and, absent a `LOG_FATAL` or a
thread exit, never hit that destructor during a session — so the bug
was invisible in the repro and total in the app.

Proved with a standalone program (`ThreadLogBuffer` from unfixed
`logging.h`, a worker thread logging 10 `LOG_INFO` lines 40 ms apart,
then staying alive 2 s before returning):

```
[t+ 0.012s] [repro] t=0ms: starting worker thread, will log 10 lines 40ms apart
[t+ 0.414s] [repro] worker loop done (t~=400ms), sleeping 2000ms before returning (thread NOT exited yet)
[t+ 2.415s] [repro] worker function returning now (t~=2400ms) -- thread_local ThreadLogBuffer dtor fires here
[t+ 2.425s] [2026-08-02 23:57:39.058] [INFO ] [MAIN] worker line 0
   ... (all 10 lines, embedded timestamps 39.058-39.420, i.e. logged
        during the first 420ms) appear only from t+2.425s onward ...
[t+ 2.512s] [2026-08-02 23:57:39.420] [INFO ] [MAIN] worker line 9
```

All 10 lines were logged in the first 420 ms; none reached stdout until
the instant the worker thread's destructor ran at t+2.415s — over 2
seconds and 40+ drain cycles later. This is the exact shape of the app's
failure: steady state (thread alive, no FATAL) delivers nothing; only
exit does. Not a second `LogManager`, not static-init order, not level
filtering — confirmed by reading `ThreadLogBuffer`/`LogManager` (single
Meyer's singleton, one drain thread) and by this reproduction. **No STOP
condition applies**; the cause is entirely inside `logging.h`.

(Separately, verifying this in the real headless client, I hit a
main.cpp ordering quirk unrelated to this defect: `main()` calls
`EverQuest::SetDebugLevel(config.debugLevel)` — which internally calls
`LogManager::SetGlobalLevel()` — *after* `InitLogging()` has already
applied `--log-level=`. Since `config.debugLevel` defaults to 0 without
`-d`, this silently clobbers `--log-level=INFO` back to NONE. Worked
around in the acceptance demo below by passing both `-d 4` and
`--log-level=INFO`; flagged here, not fixed, since `main.cpp` is outside
this unit's territory (`include/common/logging.h` only) and doesn't
change the diagnosis.)

### 2. Fix shape and cost argument

`ThreadLogBuffer::append()`, `appendError()`, and `appendEntity()` now
call the buffer's own `flush()` (self-flip) after writing, instead of
only `LOG_FATAL` calling it. This is the "flip when the drain side is
empty and the active side is not" shape the plan named as reasonable —
already implemented and correct in the existing `flush()`, just never
invoked outside `LOG_FATAL`/thread-exit before now.

Why it's safe with no new lock: a `ThreadLogBuffer` is `thread_local`,
so only its owning thread ever calls `append*()`/`flush()` on it. The
drain thread only ever reads/clears the *inactive* side and never calls
`flush()`. So the flip is always performed by the same thread that is
the sole writer to `buffers[]` — no cross-thread mutation race is
introduced. (I deliberately rejected the alternative of having the
*drain* thread perform the flip, which is what the earlier throwaway
diagnosis patch mentioned in deferred.md did: that moves the flip to a
thread that has no synchronization with an in-flight `append()` on the
writer thread, which is a genuine — if narrow — data race on the
`std::string` buffer. Keeping the flip on the writer thread avoids that
entirely and stays inside the existing single-writer/single-reader
invariant the double buffer already relies on.)

Cost is unchanged at the default level and small at a raised level.
Microbenchmark (`bench_logging.cpp`, 2,000,000 iterations, x86 dev VM,
`-O2`), before vs after, 2 runs each:

| Scenario | Unfixed | Fixed |
|---|---|---|
| `LOG_INFO` at default level (`LOG_NONE`) — production case | 2.01 / 2.09 ns/call | 1.60 / 2.01 ns/call |
| `LOG_INFO` at raised level (`LOG_DEBUG`) — append()+flush() execute | 793 / 816 ns/call | 947 / 876 ns/call |
| `flush()` alone, steady state (drain side already empty → early return) | 1.49 / 1.50 ns/call | 1.63 / 1.64 ns/call |

At the default level the two builds are indistinguishable (noise-level
difference, ~2 ns either way): `ShouldLog()` gates `LOG_WARN`/`INFO`/
`DEBUG`/`TRACE` before `append()` is ever reached, so this fix adds
**zero** work to the level that matters for the 33.3 ms Pi budget. At a
raised level, the added cost is the `flush()` call itself — measured at
~1.5-1.6 ns in isolation, i.e. noise against the ~800 ns `fmt::format()`
call that already dominates each `LOG_*` invocation. The naive
alternatives the plan warned about (always flip, or a `std::cout` write
per call) are not what this does: no I/O and no mutex are added to the
call path — the drain thread still owns 100% of the `std::cout`/
`std::cerr` writes on its unchanged 50 ms cadence.

### 3. Before/after transcript (acceptance check)

Real headless client (`build-nogfx/bin/willeq`, `EQT_GRAPHICS=OFF`), no
eqemu server, dummy unreachable login host, `-d 4 --log-level=INFO`,
process left running (not exited, no FATAL) for the full window:

**BEFORE** (unmodified `logging.h`, `/mnt/projects/fable/main/build/bin/willeq
--no-graphics`, same config/flags, file polled every 300ms for 3.6s):

```
--- t=0.3s .. t=3.6s, file size=66 (constant) ---
=== FINAL CONTENT ===

=== WillEQ Console Mode ===
Type 'help' for available commands.
```

Only the direct `std::cout` banner (not a `LOG_*` call) ever appears;
zero log lines reach stdout the entire window, at any level, despite
`-d 4 --log-level=INFO`.

**AFTER** (this unit's `build-nogfx/bin/willeq`, same config/flags/window):

```
--- t=0.3s, file size=678 ---  (stays 678 for the rest of the window: nothing further was logged)
=== FINAL CONTENT ===
[2026-08-03 00:24:18.411] [INFO ] [MAIN] Starting WillEQ with debug level 4, config file: ..., pathfinding: enabled

=== WillEQ Console Mode ===
Type 'help' for available commands.
[2026-08-03 00:24:18.412] [INFO ] [MAIN] Log level: INFO  (use --log-level=LEVEL to change)
[2026-08-03 00:24:18.412] [INFO ] [MAIN] Loading config file: ...
[2026-08-03 00:24:18.412] [INFO ] [MAIN] Initializing application...
[2026-08-03 00:24:18.455] [INFO ] [MAIN] Game thread started
```

All 6 `LOG_INFO` lines from `main.cpp`/`Application` land within the
first drain cycle (well under 50 ms after being logged, per their own
embedded timestamps vs process start), with the process still running
and no FATAL anywhere in the run.

### 4. Pinning test and proof it fails against unfixed code

Added `LoggingTest.SteadyState_NonFatalLogReachesStdoutWithoutFatalOrThreadExit`
to `tests/test_logging.cpp`. It redirects fd 1 to a temp file, spawns a
worker thread that logs one `LOG_INFO` line then stays alive (sleeping,
no exit, no FATAL) for 400 ms (8 drain cycles), restores fd 1, joins the
thread, and asserts the marker line is present. This exercises exactly
the append→drain→stdout path the existing `LoggingTest` suite never
touched (that suite only tests level/gating logic, never that a logged
line reaches stdout).

Proof it fails against unfixed code: built the identical test logic as
a standalone executable (`pin_test_standalone.cpp`) against the unfixed
`logging.h` (via `git show fable:include/common/logging.h`) and the
fixed one:

```
=== against UNFIXED logging.h (should FAIL) ===
captured stdout during steady state (thread alive, no FATAL): []
marker found: NO (FAIL)
exit code: 1
=== against FIXED logging.h (should PASS) ===
captured stdout during steady state (thread alive, no FATAL): [[2026-08-03 00:01:03.384] [INFO ] [MAIN] T31_PIN_MARKER steady state line
]
marker found: YES (PASS)
exit code: 0
```

(In the unfixed run, the marker line instead printed to the *real*
terminal after the test's fd-1 redirect was already restored — i.e. it
only ever emerged via the thread-exit destructor, exactly the mechanism
in section 1.) The real gtest version (`ctest -R LoggingTest`) passes
23/23 in both the `build` and `build-nogfx` trees against the fixed
header.

### 5. Verification bar results

1. `cmake --build build -j24` — clean, no errors. No stale-Irrlicht
   `eqtGPUPhaseBegin` link failure was encountered in this slot.
2. `ctest --test-dir build --output-on-failure` — **1802/1803 passed**,
   1 failed: `AudioManagerTest.PreloadCommonSounds` (documented ALSA
   host exception). `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`
   initially showed **Skipped** — traced to `GTEST_DISCOVER_TESTS`
   running tests with working directory `build/tests`, so the
   `data/captures` symlink at the worktree root (already present) isn't
   on the relative lookup path used by the test binary. Added
   `build/tests/data -> /mnt/projects/fable/wt/t2/data` (a build-tree
   symlink, not a source file) and reran — test then actually **RAN**
   and passed (confirmed via `-V`, not just the summary line). No golden
   file was touched.
3. Headless configure: `cmake -S . -B build-nogfx -DEQT_GRAPHICS=OFF
   -DEQT_BUILD_TESTS=ON` + build — clean. `ctest --test-dir build-nogfx
   -R LoggingTest` — 23/23 passed (same symlink applied there too).
4. Golden replay: confirmed RAN (not skipped) per above, golden
   unchanged (`data/captures/session.golden` untouched — test only
   reads it).
5. Acceptance check: see section 3.
6. Cost evidence: see section 2 (microbenchmark table).
7. `check_file_sizes.sh fable` scoped to the two files this unit
   touched (`git -C <slot> diff --name-only fable` confirms only
   `include/common/logging.h` and `tests/test_logging.cpp` changed):
   both `ok` (923 and 416 lines). No new FAILs; didn't touch
   `eq.cpp`/`eq.h`/`application.cpp` so their baseline FAIL/WARN status
   is unaffected (confirmed, not assumed, by the diff scope above).

### Deviations

- Added a `build/tests/data` and `build-nogfx/tests/data` symlink
  (build-tree artifacts, gitignored/not committed) to make the golden
  replay test actually execute under `ctest`, beyond the single
  worktree-root `data/captures` symlink the plan/protocol called out.
  Without it the test silently skips under `ctest` specifically
  (running the binary directly from the worktree root, as I did first,
  masked this because relative paths resolved differently). Flagging in
  case other units hit the same thing.
- Discovered and worked around (did not fix, out of territory) a
  `main.cpp` ordering bug where `SetDebugLevel(0)` (the default when
  `-d` isn't passed) clobbers an earlier `--log-level=` setting. See
  section 1.
- Did not modify the `LOG_FATAL` macro's existing explicit
  `FlushThreadLog()` call — it's now redundant (the `appendError()` call
  inside `EQT_LOG_IMPL` already self-flips) but harmless (idempotent),
  and leaving it keeps the FATAL path's guarantee obviously explicit
  rather than implicit.
