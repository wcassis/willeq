# T35 — Fix the `RDPServer::stop()` use-after-free, and retire its waiver

**Slot: t1. x86 only. No Pi. No eqemu.**
No sibling unit.

**Territory: `src/client/graphics/rdp/rdp_server.cpp` (+ its header if
needed), `tests/` for the RDP suites, and the waiver wording in
`refactor/EXECUTOR_PROTOCOL.md` / `refactor/TESTING_CHARTER.md`.**

Read `refactor/EXECUTOR_PROTOCOL.md` first, then the deferred.md entry
**"RDP frame-update race — ROOT-CAUSED (T03b), fix still open"** and
`refactor/TESTING_HANDBACK.md` Round 3.

## The defect

This is the **oldest confirmed crash on the board** — a real
use-after-free that has been waved through as a "known intermittent
flake" for weeks. T03b root-caused it; nobody landed the fix because it
sits in `src/client/graphics/**`, which was off-limits to the testing
session.

`RDPServer::stop()` (`rdp_server.cpp:325`) currently does:

```cpp
running_.store(false);
if (listener_) listener_->Close(listener_);       // (2) closes the listener
if (listenerThread_.joinable()) listenerThread_.join();   // (3) THEN joins
... join peer threads, clear peers ...
if (listener_) { freerdp_listener_free(listener_); listener_ = nullptr; }
```

Meanwhile `listenerThread()` (`rdp_server.cpp:448`) is sitting in:

```cpp
count = listener_->GetEventHandles(listener_, handles, 32);
DWORD status = WaitForMultipleObjects(count, handles, FALSE, 100);
```

So `Close()` at step (2) can invalidate handles the listener thread has
already fetched and is waiting on. T03b's evidence: **12 kernel GP
faults at an identical libwinpr3 offset** (`0xB3EB` →
`WaitForMultipleObjectsEx+0x16b`), with the symbol resolution
double-checked against `readelf -lW` (the kernel reports the containing
segment base; libwinpr3's executable LOAD starts at vaddr `0x48000`).

**Correct ordering (T03b): `running_.store(false)` → `join()` →
`Close()` → `free()`.**

## Why the reorder is safe — verify this, don't take my word

The obvious objection to joining before closing is deadlock: if the
listener thread only wakes because `Close()` kicks it, joining first
would hang forever.

It does not. `listenerThread()`'s wait has a **100 ms timeout** and the
loop checks `running_.load()` both at the top and immediately after the
wait, so it exits within ~100 ms of `running_` going false with no help
from `Close()`. **Confirm that reading yourself before relying on it**,
and confirm nothing else in the loop blocks unbounded.

Peer threads look fine as-is (`while (running_.load())` with a 16 ms
wait timeout, and each peer cleans up its own client after its loop) —
`stop()` does not touch peer clients before joining them. If you find
otherwise, that is a finding: report it, and say whether it needs fixing
here or separately.

## Scope

1. **Reorder `stop()`** to `running_.store(false)` → join listener →
   `Close()` → join peers / clear → `free()`. Keep it minimal; this is a
   lifecycle-ordering fix, not a redesign of the RDP server.
2. **Check the double-stop path.** `stop()` early-returns on
   `!running_.load()` and `~RDPServer()` calls `stop()`. Make sure the
   new ordering is still correct if `stop()` is called twice, or
   concurrently with the destructor — `RDPServerLifecycleTest.
   MultipleStartStop` exercises some of this.
3. **Retire the waiver.** `refactor/EXECUTOR_PROTOCOL.md` (~line 71)
   lists `RDPServerFrameTest.UpdateFrameMultipleTimes (intermittent)` as
   a known host exception, and the charter has similar wording. That
   label is precisely what let a real crash ride for weeks — T03b's own
   standing caution says **"a segfault is never a flake; it is a crash
   with a timing precondition."** Once the fix is proven, **remove the
   waiver** so a future RDP segfault fails loudly instead of being
   waved through. Do not remove it if you cannot demonstrate the fix.

## Verification — this is the hard part, not the fix

The crash is intermittent (T03b: ~1 in 5 standalone; at one point 3/3
under `-j8`; T30's run hit it once, then passed 9/9 isolated). **A
single green run proves nothing**, and "I couldn't reproduce it" is not
evidence of a fix.

1. **Reproduce it on unfixed code first.** Establish your repro before
   changing anything — a parallel-load loop over the RDP suites, run
   enough times to see at least one failure. Record the recipe and the
   observed rate. If you genuinely cannot reproduce it in a reasonable
   number of attempts, **STOP and report that** rather than landing an
   unverifiable fix; that is a legitimate outcome and worth knowing.
2. **Then re-run the identical recipe against the fix**, many times —
   at least 20 consecutive clean runs of the RDP suites under parallel
   load, and say exactly what you ran. More is better here; this is
   cheap.
3. Full `ctest` on both the GLES2 tree and, since you are touching
   shutdown, at least one run under `-j8` parallel load to match the
   conditions the crash favours.
4. If tooling helps, use it: TSAN or ASAN on the RDP suites would turn a
   probabilistic crash into a deterministic report. `tools/particle_race_stress.cpp`
   is precedent for a purpose-built stress harness (P23) if a loop in the
   test binary is not enough.
5. Golden replay unchanged; `data/captures` symlink present or the test
   **silently SKIPS** — confirm it RAN.

## STOP conditions

- You cannot reproduce the crash on unfixed code → STOP and report the
  attempt count and recipe. Do not land a fix you cannot demonstrate.
- The reorder deadlocks or the listener thread does not exit on
  `running_ = false` → STOP; that means the loop analysis above is wrong
  and the fix needs a wakeup mechanism, which is a bigger change.
- The fix requires changing FreeRDP call patterns beyond ordering → STOP
  and report.
- Any behaviour change beyond RDP start/stop lifecycle → STOP.

## Verification bar

Steps 1-5 above, plus:

6. `cmake --build <slot>/build -j24` clean — **RDP tests need
   `WITH_RDP=ON`; confirm the suites actually built and ran rather than
   being absent.**
7. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — use the `--files` form; the bare `... fable` form prints nothing and
   exits 0 when the diff is empty, indistinguishable from a clean pass.

## Review

**Setup note:** this worktree's `build/` cache had `EQT_GLES2_IRRLICHT_DIR`
resolved to `/mnt/projects/fable/main/third_party/...` instead of its own
tree — the exact "invocation-cwd absolutize" trap the CMakeLists comment
(~line 98) warns about (relative `-D` value absolutized against the shell's
cwd at the time `cmake -S` was invoked, not the source tree). Reconfigured
with an absolute `-DEQT_GLES2_IRRLICHT_DIR=/mnt/projects/fable/wt/t1/third_party/irrlicht-gles2-x86`
to fix it; worktree's own patched Irrlicht already had `eqtGPUPhaseBegin`,
no rebuild needed.

### Loop-exit analysis (confirmed by reading the source, not assumed)

`listenerThread()` (`rdp_server.cpp:448-481`): `while (running_.load())`,
`WaitForMultipleObjects(count, handles, FALSE, 100)` (100ms timeout),
`running_.load()` rechecked immediately after the wait (line 461, before
touching `status`). The only other calls in the loop —
`listener_->GetEventHandles` and `listener_->CheckFileDescriptor` — are
non-blocking listener queries, not waits. So the thread exits within
~100ms of `running_` going false with no help from `Close()`; no deadlock
from joining first. Peer loop (`peerThreadImpl`, line 567 on) is separately
confirmed fine as-is: it waits on `client`-specific handles (never
`listener_`), 16ms timeout, same running_ recheck pattern, and `stop()`
does not touch peer clients before joining them (peer join happens after
listener close in `stop()`, unchanged by this fix).

### Pre-fix repro (unmodified `rdp_server.cpp`, fable tip)

Recipe: `ctest --test-dir build -R RDP -j8` (58 RDP tests), repeated in a
foreground bounded loop. Reproduced on the *first* invocation:
`RDPServerFrameTest.UpdateFrameMultipleTimes` — `***Exception: SegFault`.

Ran **66 total pre-fix iterations** (1 initial + 65 looped). Kernel GP
faults (`journalctl -k`) confirm **6 real UAF crashes**, all at the
identical offset `ip - segment_base = 0xB3EB` — exactly T03b's
`WaitForMultipleObjectsEx+0x16b` signature (computed precisely for each of
the 6, not eyeballed). Observed rate: 6/66 ≈ 9%. Lower than T03b's ~1-in-5
because that figure was under full-suite `-j8` (more scheduling pressure);
this loop was RDP-suite-only `-j8`.

**Distinct finding, not the UAF:** ~13 more pre-fix runs failed
non-crash — `RDPServerFrameTest`/`RDPServerLifecycleTest` tests reporting
`Failed to open listener on port 23392/23389`. Root cause: `GTEST_DISCOVER_TESTS`
registers each `TEST_F` as its own ctest test → its own OS process
(`--gtest_filter=...`), and several fixtures shared one hardcoded port
across multiple `TEST_F`s in the same fixture (e.g. `RDPServerFrameTest`'s
`UpdateFrameAfterStart`/`UpdateFrameMultipleTimes` both bound 23392,
`RDPServerLifecycleTest`'s `StartAndStop`/`MultipleStartStop`/
`ClientCountInitiallyZero` all bound 23389) — under `ctest -j`, two of
those processes can race for the same port. Pre-existing, unrelated to the
UAF (confirmed present in the unmodified tree), not a crash (clean
assertion failure with a clear error message, not a segfault) — but it
shares the same test names as the segfault waiver, so it had to be
resolved before that waiver could be honestly retired. Fixed in scope
(`tests/` is named territory): moved `initialize(port)` out of each
fixture's shared `SetUp()` into each individual `TEST_F` body with a
unique literal port (`tests/test_rdp_server.cpp`).

### The fix

`src/client/graphics/rdp/rdp_server.cpp`, `RDPServer::stop()`: reordered to
join the listener thread *before* calling `listener_->Close()` (was the
reverse). Comment added explaining why this can't deadlock. Nothing else
in `stop()`'s ordering changed (peer-thread join, `activePeers_.clear()`,
`freerdp_listener_free()` all still happen after, same as before).

### Double-stop / destructor check

`running_.store(false)` is still the first statement inside `stop()`,
unchanged in position — so the early-return guard
(`if (!running_.load()) return;`) behaves identically to before the
reorder for a second `stop()` call, whether that's an explicit call after
one that already completed, or `~RDPServer()` (which just calls `stop()`)
running after an explicit `stop()`. Only the ordering *within* one live
`stop()` call changed. `RDPServerLifecycleTest.MultipleStartStop`
(sequential start/stop/re-initialize/start/stop in one thread) passed in
every run of every batch below, and was additionally run 10x isolated with
no failures. Concurrent (cross-thread) `stop()`/destructor races are not
exercised by any test and are a pre-existing, unchanged property of the
class (not introduced or worsened by this fix) — noted, not in scope.

### Post-fix verification

- `ctest -R RDP -j8`: **30 consecutive clean runs** (0 failures), then a
  further **20 consecutive clean runs at `-j24`** (heavier scheduling
  pressure) — 50 total, 0 crashes, 0 non-crash failures.
- Kernel GP-fault count (`journalctl -k`) stayed at exactly **6** across
  every post-fix run — the same 6 from the pre-fix loop, confirming zero
  new faults, not just zero ctest-visible ones.
- Full `ctest --test-dir build --output-on-failure` (whole suite, serial):
  **1818/1819**, sole failure `AudioManagerTest.PreloadCommonSounds`
  (documented ALSA host exception). RDP suite: all green.
- Full suite again under **`-j8`** (matches the conditions that favour the
  crash): **1818/1819**, same sole failure, all 58 RDP tests green.
- `bash scripts/build-gles2-x86.sh`: clean build.
- Visual gate (`scripts/ci/visual_gate.sh`): **PASS**, goldens untouched.
- `bash scripts/build-arm-noble.sh`: clean cross-compile, `willeq` produced.
- `scripts/refactor/check_file_sizes.sh --files <both changed files>`: both
  `ok` (1013 and 392 lines).
- TSAN: built a separate `-fsanitize=thread` tree per the P23 precedent
  (`-DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"`, run under
  `setarch $(uname -m) -R`). Ran `--gtest_repeat=20` in-process, both
  pre-fix and post-fix: **no TSAN warnings either way**, and pre-fix did
  not crash under TSAN either. Read as inconclusive rather than
  exculpatory: `--gtest_repeat` reruns tests sequentially inside one
  process, which doesn't reproduce the cross-process/thread scheduling
  pressure that the `ctest -j8` loop needed to expose the race, and TSAN's
  instrumentation overhead perturbs timing further. The process-level
  `ctest -j` evidence (6 crashes pre-fix at a kernel-verified consistent
  offset, 0 crashes in 50 consecutive post-fix runs plus 2 full-suite
  passes) is the operative evidence for this fix, not the TSAN attempt.

### Golden replay — confirmed it ran, not silently skipped

Initial full-suite run showed `SessionReplayGolden.
MatchesRecordedSessionOrCreatesBaseline` as `Skipped` *despite* the
worktree-root `data/captures` symlink already existing
(`/mnt/projects/fable/wt/t1/data/captures` -> `/mnt/projects/fable/captures`,
present before this unit started). Root cause: `GTEST_DISCOVER_TESTS` sets
this test's ctest `WORKING_DIRECTORY` to `<build>/tests` (the target's
binary dir), and the test's default session-log path
(`DefaultSessionLogPath()` in `test_session_replay.cpp`) is the relative
string `"data/captures/session.sleq"` — so it resolves against
`build/tests/`, not the repo root. The repo-root symlink is necessary but
not sufficient; `build/tests/data/captures` also needs to exist. Created
`ln -s /mnt/projects/fable/captures /mnt/projects/fable/wt/t1/build/tests/data/captures`
(inside `build/`, gitignored, not part of this commit) and reran the full
suite: golden test now runs and PASSES (confirmed, not just "not skipped").
Flagged here rather than fixed at the CMake/test level since it's outside
this unit's territory; worth a `refactor/deferred.md` note for whichever
unit next rebuilds a worktree's `build/` from scratch.

### Waiver retirement

Both the UAF crash and the (distinct) port-contention flake it was
commingled with are fixed and verified deterministic. Retired the waiver:
- `refactor/EXECUTOR_PROTOCOL.md` (~line 68-79): removed
  `RDPServerFrameTest.UpdateFrameMultipleTimes (intermittent)` from the
  known-host-exceptions list; added a dated note pointing at this plan and
  `refactor/deferred.md` for the history, and an explicit caution against
  re-waiving a future RDP segfault without root-causing it.
- `refactor/TESTING_CHARTER.md` (~line 65-69): removed
  `RDPServerFrameTest.* [parallel-run flake family]` from the documented
  flaky-test exceptions, same dated note.

### Deviations from the plan

- Widened scope slightly beyond the pure lifecycle-ordering fix: fixed the
  port-contention test bug in `tests/test_rdp_server.cpp` (explicitly
  named territory) because it shared test names with the crash waiver and
  would have made a "clean" waiver retirement dishonest (the family would
  still show intermittent, if non-crash, failures under `-j8`). Flagged
  here per the "deviations flagged, never silently absorbed" rule rather
  than silently including it in "the fix."
- Found and worked around (did not fix in-tree) a golden-replay-test
  `WORKING_DIRECTORY` trap that silently skips the test for any freshly
  built `build/` dir even with the documented repo-root `data/captures`
  symlink in place; see above. Recommend a `refactor/deferred.md` entry
  and/or a CMake-level fix (e.g. set the test's `WORKING_DIRECTORY` to the
  source tree, or create the symlink as part of the build) so this stops
  costing each new worktree/build the same rediscovery.
