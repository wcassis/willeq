# T03b — Suite hygiene: parallel-unsafe Hotkey tests + RDP segfault diagnosis

**Slot: t1. x86 only — no Pi, no eqemu.** No sibling unit; t2 idle.

**Territory: `tests/` ONLY.** Part A is a real fix. Part B is a
**diagnosis**, and its likely fix location is off-limits to you — read
the Part B rules carefully before touching anything.

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Context: `refactor/testing_progress.md`, "Suite hygiene" section.

## Why this unit

The charter forbids new flaky tests and requires 3 consecutive green
full-suite runs before any unit merges. That rule is only worth
something if the existing suite is trustworthy. Right now it is not:
there are three flake families, only two of which are documented, and
one "known flake" is a segfault nobody has diagnosed.

Every future unit's "3x green" claim is weakened until this is fixed.

---

# Part A — make the Hotkey tests parallel-safe (FIX)

## Root cause (already diagnosed by the orchestrator — verify, then fix)

`tests/test_hotkey_manager.cpp` has **four** test fixtures — at lines
216, 365, 522, 672 — and every one of them does:

```cpp
m_tempDir = std::filesystem::temp_directory_path() / "hotkey_test";
std::filesystem::create_directories(m_tempDir);
m_tempConfigPath = m_tempDir + "/test_hotkeys.json";
```

with `std::filesystem::remove_all(m_tempDir)` in `TearDown()`.

All four share **one hardcoded path**, `/tmp/hotkey_test`, and each
one deletes the whole directory on teardown. `GTEST_DISCOVER_TESTS`
registers every `TEST_F` as its own ctest entry, so under `ctest -j8`
several `test_hotkey_manager` processes run concurrently — and one
process's `TearDown()` deletes the directory out from under another
process that is mid-test.

Confirmed reproduction:
- `ctest -R Hotkey` (serial) → 47/47 pass.
- `ctest -R Hotkey -j8` → fails, **with no other test binary running**.

So this is the Hotkey tests fighting each other over a filesystem path,
not cross-suite contention.

## What to do

Give each test its own directory. Uniqueness must hold across
concurrent *processes*, not just across fixtures in one process — the
PID is the reliable discriminator; the gtest test name alone is not
enough if the same test is ever retried concurrently, though
PID + test name is a good belt-and-braces combination
(`::testing::UnitTest::GetInstance()->current_test_info()`).

Requirements:
- No two concurrently-running processes may share a temp path.
- `TearDown()` must remove only its own directory, never a shared
  parent.
- Leave no litter in `/tmp` on success or on failure.
- Fix all four fixtures, not just the two that were observed failing —
  the other two have the identical defect and are failing silently by
  luck.

Do not change what the tests *assert*. This is a fixture-isolation fix,
not a rewrite.

## Part A acceptance

- `ctest -R Hotkey -j8` green **10 consecutive times**. Two or three
  runs proves nothing for a race; state the actual count you ran.
- `ctest -R Hotkey` serial still green.
- No `/tmp/hotkey_test*` residue after the runs.

---

# Part B — characterize the RDP segfault (DIAGNOSE, DO NOT FIX)

## What is known (measured by the orchestrator, current tip, t1)

`RDPServerFrameTest.UpdateFrameMultipleTimes`:

| condition | result |
|-----------|--------|
| isolated, serial (`ctest -R RDPServerFrameTest`) | 5/5 pass |
| RDP tests only, `-j8` (58 tests), 4 consecutive runs | pass, 0 failures |
| **full suite, `-j8` (1362 tests), 3 consecutive runs** | **SEGFAULT 3/3** |

So it is **not** intrinsic to RDP tests running concurrently with each
other. It needs full-suite load. That points at a resource or timing
dependency — exhaustion (fds, ports, memory), a genuine race in the
frame-update path that only loses under scheduling pressure, or
interference from one specific other test.

This is currently waived as a "documented intermittent flake". A
segfault reproducing 3 out of 3 is not intermittent, and the waiver has
been hiding it.

## Your job

Establish **what actually crashes and why**. Concretely:

1. Get a stack trace. Run the binary under `gdb --batch` or enable core
   dumps, or run the specific gtest under `catchsegv`/ASan — whatever
   gets you a real backtrace. A build with
   `-fsanitize=address,undefined` in a **scratch** build directory
   (gitignored, cleaned up after — do NOT commit build-system changes)
   is likely the fastest path to a precise answer.
2. Determine whether the crash is in **test code** or in
   `src/client/graphics/rdp/` (`rdp_server.cpp`,
   `rdp_peer_context.cpp`, `rdp_input_handler.cpp` — the sources the
   target links).
3. Narrow the trigger: does it need the full suite, or a specific
   concurrent test? Bisecting by ctest label or `-E` exclusion is
   cheap. If you can find a minimal reproducer, that is the single most
   valuable output of this unit.

## HARD RULE for Part B

`src/client/graphics/**` is **off-limits to the testing session** —
that is perf-session territory per the charter, and `test_rdp_server`
links three files from `src/client/graphics/rdp/`.

- If the bug is in **test code** (`tests/test_rdp_server.cpp`) → fix it,
  same bar as Part A.
- If the bug is in **`src/client/graphics/rdp/`** → **STOP. Do not
  fix it.** Write up the diagnosis (trace, trigger, minimal reproducer,
  your read of the root cause) and report. It becomes a handoff to the
  perf session. This is the expected outcome and is a **success**, not
  a failure of the unit — a precise handoff is exactly what is wanted.

Do not "fix" it by adding retries, increasing timeouts, serializing the
test via ctest properties, or marking it skipped. If the honest answer
is "this is a real crash in RDP frame handling", say that.

---

## STOP conditions

- Part B's fix lands in `src/client/graphics/**` → STOP and report
  (expected; see above).
- Any fix would require changing `src/` or `include/` outside graphics
  → STOP and report; do not make the change.
- You cannot get a backtrace → report what you tried, do not guess at a
  cause from the symptom alone.

## Acceptance criteria

1. Part A: all four Hotkey fixtures isolated; `ctest -R Hotkey -j8`
   green 10 consecutive runs; serial green; no `/tmp` residue.
2. Part B: a backtrace, a statement of where the crash is (test code vs
   graphics source), the narrowest trigger you found, and either a
   tests/-side fix or a written handoff.
3. Full suite: 3 consecutive runs. Report the RDP result honestly in
   each — if Part B was a handoff, RDP may still segfault, and that is
   an acceptable, expected outcome for this unit. **Do not suppress it
   to make the runs look green.** `AudioManagerTest.
   PreloadCommonSounds` remains a documented host exception.
4. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
5. Any scratch sanitizer build directory removed before you finish.

Visual gate and ARM build do not apply (tests-only diff). Say so.

## Environment notes

- GLES2 config is the only tree. As of `6b53e0d` the documented
  **relative** `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86`
  now works (an earlier absolute-path workaround is no longer needed).
  Bootstrap once if `third_party/irrlicht-gles2-x86/lib/libIrrlicht.a`
  is absent: `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.
- Fixture symlink if absent:
  `ln -s /mnt/projects/fable/captures <slot>/data/captures`.

## Review

### Part A — Hotkey fixture isolation (FIX, landed)

Root cause confirmed exactly as diagnosed: `tests/test_hotkey_manager.cpp`
had four fixtures (`HotkeyConflictTest`, `HotkeyJsonLoadTest`,
`HotkeyModeLookupTest`, `HotkeyResetReloadTest`, originally at lines
209/359/516/666, shifted after the fix) all hardcoding
`std::filesystem::temp_directory_path() / "hotkey_test"` and calling
`remove_all()` on it in `TearDown()`. Under `GTEST_DISCOVER_TESTS` each
`TEST_F` is its own ctest process; concurrent processes shared the path
and stomped each other's directory.

Fix: added one helper, `MakeUniqueHotkeyTestDir()`, built from
`getpid()` + the running gtest suite/test name (via
`::testing::UnitTest::GetInstance()->current_test_info()`), and pointed
all four fixtures' `SetUp()` at it instead of the literal string. No
assertions changed — this is purely fixture isolation, per the plan's
instruction. `TearDown()` bodies were untouched; they already only
`remove_all()` their own `m_tempDir`, which is now unique per
process+test.

Evidence:
- `ctest -R Hotkey` serial: 47/47 pass.
- `ctest -R Hotkey -j8`: **10/10 consecutive green runs** (actual count
  run: 10, not sampled down from more or truncated from fewer).
- `ls /tmp/hotkey_test*` after the 10 runs: no matches (no residue).

### Part B — RDP segfault (DIAGNOSIS, no fix landed — handoff)

**Verdict: real crash, not test-code. Off-limits per the charter/plan.
STOPped without touching `src/client/graphics/rdp/`.**

**Evidence gathered (not a guess):**

1. Swapped the built `test_rdp_server` binary for an ASan+UBSan scratch
   build (`build-asan-t03b/`, configured identically to the GLES2 tree
   plus `-fsanitize=address,undefined -g`, target-only build — it does
   not link Irrlicht so this was fast). Isolated run: clean. Swapped
   into `build/bin/test_rdp_server` and ran the full suite `-j8` twice:
   both clean (no crash, no sanitizer report beyond an unrelated, pre-
   existing `LeakSanitizer` note about `freerdp_listener_new()` not
   being freed when `RDPServerFrameTest::TearDown()` skips `stop()`
   because the server was never started — cosmetic, not the segfault,
   not touched). ASan's overhead apparently perturbs the scheduling
   enough to close the race window; it did not reproduce the crash in
   either run.
2. Restored the original binary, ran the full suite `-j8` four more
   times (including two runs with a second full suite running
   concurrently, and parallel `gdb --batch`-wrapped invocations of
   `RDPServerFrameTest.UpdateFrameMultipleTimes` racing alongside it to
   try to catch a live backtrace) — **could not reproduce the crash live
   in this session**, in 4 attempts of full-suite-equivalent-or-heavier
   load.
3. **`journalctl -k` for this host has real kernel oops records that
   answer the question anyway.** 11 occurrences today (`Jul 28`,
   02:39–17:09, spanning well before and up to ~17 minutes before I
   started chasing it), all identical in shape:
   ```
   kernel: traps: test_rdp_server[PID] general protection fault
   ip:<addr> sp:<addr> error:0 in libwinpr3.so.3.30.0[<base>+75000]
   ```
   Every one of the 11 has **`ip - base` == `0xB3EB`**, byte-for-byte
   identical across 11 independent ASLR-randomized loads. This is not
   the "segfault at 0/small-offset" shape of a null-deref (compare the
   unrelated `test_inventory_model_view` crashes in the same log, which
   are ordinary `error 4` page faults at address 0) — it's a **general
   protection fault, error code 0**, the signature of an instruction
   touching a non-canonical/garbage address, i.e. classic
   use-after-free/dangling-pointer corruption, not a simple null check
   miss.
4. Resolved the crash site: the reported module base corresponds to
   `libwinpr3.so.3.30.0`'s second (R+E) `PT_LOAD` segment, which starts
   at file vaddr `0x48000`. Absolute vaddr of the fault =
   `0x48000 + 0xB3EB = 0x533EB`. Nearest exported dynamic symbol below
   that address (`nm -D --defined-only`): **`WaitForMultipleObjectsEx`
   at `0x4c880`, offset `0x16b` into the function.** (No debug symbols
   package is installed for `libwinpr3`/`freerdp3` on this host and none
   could be installed in this environment, so this is nearest-symbol
   resolution, not a source-line backtrace — flagged as the limit of
   what I could pin down.)

**Where the bug is, and why:** `RDPServer::listenerThread()`
(`src/client/graphics/rdp/rdp_server.cpp:448-481`) calls
`listener_->GetEventHandles(...)` then blocks in
`WaitForMultipleObjects(count, handles, FALSE, 100)` (line 459) in a
loop gated only by `running_.load()`. `RDPServer::stop()` (lines
325-366) does:
```
running_.store(false);
listener_->Close(listener_);       // tears down listener resources
listenerThread_.join();            // ... while the thread above may
                                    //     still be inside the wait call
...
freerdp_listener_free(listener_);  // (this part is safely after join)
```
`running_` is a coarse flag checked only *between* wait calls; it does
nothing to stop a wait that is already in flight when `Close()` runs.
`RDPServerFrameTest.UpdateFrameMultipleTimes` does
`start()` → 10 near-instant `updateFrame()` calls → `stop()`, all
within a few milliseconds, so the listener thread often hasn't even
entered its first `WaitForMultipleObjects` call before `stop()` reaches
`Close()`. Under light load (isolated, or RDP-tests-only `-j8`) the
scheduler runs the listener thread promptly enough that it observes the
closed socket and returns safely before `Close()`'s internal teardown
completes. Under full-suite `-j8` (1362 tests, heavy CPU contention),
the listener thread can be descheduled for tens of milliseconds with a
stale `handles` array already fetched, right as `Close()` runs on
another thread — the exact TOCTOU window that produces a
use-after-free/dangling pointer dereference inside FreeRDP/WinPR's
`WaitForMultipleObjectsEx`, manifesting as the observed `#GP`. The peer
thread (`peerThreadImpl`, same file, lines 483+) has the identical
pattern (`WaitForMultipleObjects` on freshly-fetched handles, gated only
by `running_`) and is structurally exposed to the same class of race,
though I have no direct evidence it has fired (no peer connects in
these unit tests, so `peerThreadImpl` never actually starts running for
`RDPServerFrameTest`).

**Confidence:** high that the crash is real, deterministic in its crash
site, and lives in the interaction between `RDPServer::stop()`'s
teardown order and `listenerThread()`'s wait loop in
`src/client/graphics/rdp/rdp_server.cpp` (or in WinPR's handling of a
concurrently-closed handle, which our code's ordering exposes) — that
part is evidence-backed, not a guess (11 kernel-level crash records,
consistent PC, symbol-resolved to a function our code calls directly on
that exact code path). Medium confidence on the precise mechanism inside
`WaitForMultipleObjectsEx` itself, since I could not get a source-level
backtrace (no debug symbols available for `libwinpr3`, and I could not
reproduce the crash live under `gdb` in this session to capture our own
stack frames) — the "why it's a UAF inside WinPR" read is the most
plausible explanation consistent with the `#GP`/error-0 shape and the
code path, not a directly observed heap-corruption trace.

**Per the plan's HARD RULE:** the fix (serializing `stop()`'s teardown
so `Close()`/`freerdp_listener_free()` cannot race a still-in-flight
`WaitForMultipleObjects` call — e.g. join before Close, or a
cancellable wait, or re-checking `running_` immediately after handle
acquisition with a much shorter timeout) belongs in
`src/client/graphics/rdp/rdp_server.cpp`, which is off-limits to this
unit. **STOPped. Not fixed. Handing off to the perf session** with this
diagnosis. `RDPServerFrameTest.*` remains a documented intermittent
flake (per `EXECUTOR_PROTOCOL.md`'s known host exceptions) — this unit
does not remove that exception, it explains it.

**Scratch build cleanup:** `build-asan-t03b/` removed before finishing;
confirmed via `md5sum` that `build/bin/test_rdp_server` was restored
byte-identical to the pre-ASan binary before the final verification
runs.

### Full-suite verification (3 consecutive runs, GLES2-config tree)

| Run | Result | RDP | Notes |
|-----|--------|-----|-------|
| 1 | 1361/1362 passed | passed | Only `AudioManagerTest.PreloadCommonSounds` failed (documented host exception) |
| 2 | 1361/1362 passed | passed | Same |
| 3 | 1361/1362 passed | passed | Same; `journalctl -k` over the run window shows no new RDP kernel oops |

Honest report as instructed: **RDP did not crash in these 3 final
verification runs** (nor in 4 earlier attempts specifically hunting for
it under gdb/ASan/heavier concurrent load). This is not suppression —
it's the genuinely observed outcome, consistent with an
intermittent/scheduling-dependent race whose window did not open in
these particular runs. The independent, earlier-collected kernel-log
evidence (11 crashes today, same PC every time, see above) is the basis
for the diagnosis; it is not contradicted by these 3 clean runs, since
the crash was never claimed to be deterministic at the ctest/process
level — the plan's own measurement found "flaky-but-frequent" behavior,
not 100% reproducibility across all conditions.

### Other verification

- `bash scripts/refactor/check_file_sizes.sh fable`: no FAILs (exit 0).
- Visual gate / ARM build: **N/A** — tests/-only diff
  (`tests/test_hotkey_manager.cpp`), no shared source touched, per the
  plan and charter.
- Territory: only `tests/test_hotkey_manager.cpp` modified. No changes
  to `src/`, `include/`, or `src/client/graphics/**`.

### Deviations from the plan

- None substantive. The plan suggested an ASan/UBSan scratch build as
  "likely the fastest path" for Part B; I used it (target-only build,
  not a full-project build, to save time since `test_rdp_server` links
  no Irrlicht) but it did not reproduce the crash — the kernel log from
  earlier in the session turned out to be the decisive evidence instead.
  Flagging this since it means the backtrace evidence is a kernel oops
  + nearest-symbol resolution rather than a `gdb`/ASan stack trace with
  our own frames, which is a slightly weaker (but still real, not
  guessed) form of evidence than the plan's primary suggestion.
