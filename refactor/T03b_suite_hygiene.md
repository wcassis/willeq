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

Per the protocol. For Part B specifically I want the evidence, not a
conclusion: paste the backtrace, name the trigger, and state plainly
how confident you are. "I could not determine the cause, here is what I
ruled out" is a perfectly acceptable result and far more useful than a
confident guess.
