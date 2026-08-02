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

(Executor fills: why standalone passed while the app failed, the fix
shape and its cost argument, the before/after transcript, the pinning
test and proof it fails against unfixed code, deviations.)
