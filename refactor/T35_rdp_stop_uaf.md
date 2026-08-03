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

(Executor fills: the repro recipe and observed pre-fix failure rate, the
loop-exit analysis confirming no deadlock, the double-stop/destructor
check, the post-fix run count, any sanitizer evidence, and the waiver
removal.)
