# T465 — `HeadlessShutdownTest` fails under `ctest -j6` load in every full run

Class P (process: a red that every verifier has to re-run by hand), **no lock,
no Pi**, slot2.

## The evidence

On 2026-09-13 the full `ctest -j6` run of three separate merged tips
(`cb9c3f3c`, T461's rebased tip, `641a9402`) each had exactly one red:
`HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied`, green
standalone every time. T461 saw "its client printed nothing within 5 s under
load". EXECUTOR_PROTOCOL records an earlier shape of the same test failing with
`failed to open data/willeq.db (sqlite rc=14)` (T360). A test every unit waves
through as a "load artefact" is a test that cannot catch the defect it was
written for.

## Tasks

1. Read the test and what it launches (`tests/`, grep the name). Establish
   the actual failure mechanism(s) under load, with evidence: reproduce by
   running it alongside synthetic load (e.g. `stress-ng`/a parallel ctest of
   heavy binaries, or `taskset` to one core) — measure how often it fails and
   which assertion/timeout fires; capture the child's output. Cover BOTH
   shapes (5 s no-output timeout; sqlite rc=14 = SQLITE_CANTOPEN — relative
   path/cwd? a shared db file opened concurrently by another test? a temp dir
   collision?).
2. Fix the root cause, not the timeout number alone: e.g. wait on a readiness
   signal instead of a fixed 5 s sleep; absolute/`ResolveDataPath` db path;
   unique temp dirs. If a deadline must remain, justify its size against the
   measured worst case and make the failure message say what was being waited
   on.
3. Grep for other tests with the same pattern (fixed wall-clock waits on a
   spawned client, shared relative `data/willeq.db` opens) and fix or board
   them (B570–B579).
4. Prove: the synthetic-load repro fails ≥N times pre-fix and 0 times in the
   same number of runs post-fix; then two full `ctest -j6` runs with zero reds.
   Update EXECUTOR_PROTOCOL.md's load-artefact paragraph (remove the waiver for
   this test if fixed).

## Proof

Standard verification bar (the visual gate and ARM build are required only if
non-test sources change). Board B570–B579.

File territory: the test and its fixture, test-only helpers, whatever
production seam the readiness signal needs (keep it minimal and say so),
`refactor/EXECUTOR_PROTOCOL.md`. **Not** `src/client/graphics/ui/`/frame timing
(T462), **not** camera/player/hotkeys/`application_zone_loading.cpp` (T464).

STOP: the root cause is a real production shutdown defect (report it with
evidence before fixing — it becomes its own class A item).

## Review

Executed 2026-09-13, slot2, base `448dc38e`. **No production code changed**: the
test file, `refactor/BOARD.md` (B570 closed, B571 new) and
`refactor/EXECUTOR_PROTOCOL.md`. The STOP condition did not fire: neither shape
is a client defect (see 2).

### 1. The failures themselves

`--output-on-failure` logs survive for all three 2026-09-13 reds, and they show
**two shapes, neither of them sqlite**:

| run | position | wall | captured child output |
|---|---|---|---|
| T461 (slot1 `ctest.log`, 18:33) | test 115 of 4762, straight after a build | 5.41 s | **empty** |
| T456 chain (18:14) | 158 of 4708 | 6.92 s | banner + help hint, **no `CMD outcome=` line** |
| T463 chain (20:06) | 4082 of 4770 | 6.26 s | banner + help hint, **no `CMD outcome=` line** |

In the T463 run the sibling case (`PipedQuit...`) ran about 500 tests later, so the
shared-config race (6) played no part there.

**The `sqlite rc=14` shape (T360) was never a cause.** T360 Leg A (`3bca791c`)
predates T399 (`3128a1fa`; `merge-base --is-ancestor` confirms), and at that
tip `initEverQuestClient()` opened the bare literal `"data/willeq.db"`. ctest runs
this test from `build/tests`, which has no `data/`, so every run printed
SQLITE_CANTOPEN, pass or fail, and a failing run's dump just happened to include it.

### 2. Mechanism: a fixed 5 s window racing the client's startup

`printf '/attack\n' | timeout 5 willeq` passed only if the client reached its
first game-thread input tick within 5 s of spawn. That is
`processInput()` → `processPreConnectionLifecycleCommands()`, which prints `held`.
Between spawn and that tick sit exec and page-in of a 577 MB binary from the NFS
build tree, `initialize()` (the banner is printed inside `initGameMode`, with
`std::endl`), `ConnectToLogin()` and the game thread's start.

Why this is not a production defect:
- **No drop path.** `ConsoleInputHandler::pipedInputThreadFunc()` queues the line
  under `m_mutex`, and the pre-connection intercept only `peekRawCommand()`s a
  non-lifecycle command; it never consumes it. EOF leaves the fd readable, so the
  reader's `select()` cannot hang.
- **SIGTERM is clean.** With `timeout --preserve-status 3` the child's own status
  is **0**, and the log runs `SIGTERM received` → `Game thread joined` →
  `Application shutdown complete`.
- **A stalled client exits when actually signalled.** Three clients were left
  running by a harness defect (5). Each sat in its normal held loop (`State: S`,
  main thread in `hrtimer_nanosleep`, TERM caught, nothing pending) and exited
  **0.16 s, 1.12 s and 1.71 s** after a hand-sent SIGTERM.

Cold start, measured on a quiet host: evicting `willeq` from page cache
(`posix_fadvise DONTNEED`) moved time-to-first-line from **0.05 s to 2.24 s**. The
mount is `nfs vers=3,hard,timeo=600`, so a lost RPC stalls a reader 60 s before
the first retransmit.

### 3. What did NOT reproduce it (recorded so nobody repeats them)

| attempt | runs | result |
|---|---|---|
| evict `willeq` + 5 cold `--gtest_list_tests` loads beside each run | 20 | 20 pass |
| child `nice 19` pinned to a core owned by a busy loop | 2 | 2 pass |
| child `SCHED_IDLE` pinned to a core owned by a busy loop | 10 | 10 pass, walls 5.47-5.91 s |
| 5 rounds × 32 concurrent test processes, cache evicted per round | 160 | 160 pass |
| full pre-fix `ctest -j6` on slot2 (load 4-7) | 1 | 4770/4770; the case passed in 5.04 s |

**CPU starvation is not the mechanism on this host.** The client needs very little
CPU to reach its first tick. The real reds came from waves where other slots were
linking multi-GB archives over the same NFS, a condition this unit could not
recreate without degrading the other slots.

### 4. The repro: the two observed stalls injected into the real client

`build/bin/willeq` was temporarily renamed and replaced by a wrapper
(scratchpad `T465/willeq_stall_wrapper.sh`, `T465/inject.sh`). The test only
checks that the path exists. The harness restores the binary on exit and
verified it after every leg: same inode `23539229`, size 577,342,368, magic
`7f454c46`. The two stalls:

- **`prestart:6`**: sleep 6 s, then exec the client. This gives the EMPTY shape.
- **`postbanner:6`**: SIGSTOP the whole client for 6 s right after it prints the
  banner. This gives the BANNER_ONLY shape, and the failure text is identical to
  the T456/T463 logs.

The pre-fix binary is the fable test compiled into scratch with the target's own
flags. `git diff 448dc38e fable -- tests/test_headless_shutdown.cpp` is empty, so
it is the base's test.

| mode | pre-fix | post-fix |
|---|---|---|
| `none` (wrapper transparent) | 5/5 pass | 5/5 pass |
| `prestart:6` | **5/5 FAIL** (EMPTY) | **5/5 pass** |
| `postbanner:6` | **10/10 FAIL** (BANNER_ONLY; plus 5/5 fail with the v1 wrapper) | **10/10 pass** |
| **total with a stall** | **15/15 fail** | **15/15 pass** |

Load during these runs was 3-41 (`/proc/loadavg`, recorded per run in each
`summary.txt`). Only the NonLifecycle case ever failed pre-fix. `PipedQuit`
survived every 6 s stall inside its 20 s window, although that window has the
same exposure at a larger stall.

### 5. Deviation: a harness race, found and fixed rather than absorbed

The first post-fix `postbanner:6` legs failed **1/5 and then 2/10**, each by
hitting the new 120 s deadline **after** the ack. The v1 wrapper forwarded
SIGTERM to the client through a bash `trap`. An instrumented rerun's wrapper log
ends at `fwd [CMD outcome=held ...]` with **no** `TRAP TERM` entry, while a passing
run shows the trap 2.7 ms after the same line. Bash lost a trapped signal that
landed just before it blocked in `read` on the FIFO. A standalone experiment
showed the trap does run when the signal arrives while bash is already blocked,
so the loss is that narrow window. The stranded clients (3706090, 3712127 from
run 4, 3714473 from run 8) are the three in 2.

The v2 wrapper `exec`s into the client, so the test signals the client directly
and a background reader does the STOP/CONT. Under v2 the result was 10/10 pass,
and the pre-fix legs were rerun under v2 as well. Nothing in the client or the
fix changed between v1 and v2.

### 6. The fix (`tests/test_headless_shutdown.cpp`, 166 → 353 lines)

- **No wall-clock pass window.** A small `HeadlessChild` (fork/exec, stdin =
  payload then EOF, stdout+stderr on one pipe, `poll()`-driven reads) replaces
  `popen()` + `timeout`.
  - NonLifecycle reads until a **complete** `CMD outcome=` line arrives, asserts
    `held`, sends SIGTERM, asserts **exit 0**, and checks for no `applied` over
    the whole output, shutdown included.
  - PipedQuit waits for the process to exit.
- **One deadline, a hang bound only: `kChildDeadline` = 120 s,** sized against
  the NFS `timeo=600` (one 60 s retransmit plus margin). A passing run costs its
  real latency. In the full suite NonLifecycle went from 5.04 s to 0.10-0.21 s,
  and PipedQuit is unchanged at 0.43-0.46 s. Every failure message names what
  was being waited on and the elapsed seconds.
- **Coverage added:** SIGTERM must now take P36's clean path (exit 0), and a
  crash is reported by `WTERMSIG`. The old `"Segmentation fault"` text was
  printed by the shell `popen` ran, which no longer exists.
- **Config in a per-process `mkdtemp()` directory**, removed in `TearDown()`.
  The fixed `/tmp/t30_headless_shutdown_test/config.json` was truncated by each
  `SetUp()`; ctest starts the two cases as concurrent processes (adjacent
  `Start 2439`/`Start 2438` in the pre-fix run), and every slot shares `/tmp`.
  **Not observed as a failure** (0 CONFIG-shaped failures in 160 wave runs and 3
  logs); fixed as a latent hazard because it was one line.
- The production seam the plan allowed was **not needed**: the ack line already
  was the readiness signal.

### 7. Sibling census (task 3)

- **A fixed wall-clock kill on a spawned client:** only this test (`grep -rn
  "timeout [0-9]" tests/`). The other spawners, `test_entity_viewer_regression`
  and `test_eqe1_composite_open` (`popen`) and `test_tsf_oob_read` and
  `test_willeq_database` (`fork`), wait for natural exit with no deadline.
- **Shared relative `data/willeq.db` opens in tests:** none at runtime. Twelve
  test files still spell `#define WILLEQ_DB_PATH "data/willeq.db"`
  (`test_eff_loader`, `test_spell_manager`, `test_buff_manager`,
  `test_buff_click_off`, `test_spellbook_memorize`, `test_vision_buff_fade_wire`,
  `test_buff_landing_producer`, `test_eq_action_handler_cast`,
  `test_eq_action_handler_spell_pet`, `test_game_state_source_writes`,
  `test_scribe_scroll`, `test_spell_database`). All twelve sit under `#ifndef
  WILLEQ_DB_PATH`, and the target compiling each one gets the absolute
  `${CMAKE_SOURCE_DIR}/data/willeq.db` from `TARGET_COMPILE_DEFINITIONS` (checked
  per target in `tests/CMakeLists.txt` + `tests/cmake/*.cmake`), so the relative
  fallback is never compiled. The client itself resolves through
  `ResolveDataPath` since T399.
- **Fixed cross-slot `/tmp` names:** seventeen, in nine other test files, none
  failing today → **boarded B571**, not fixed (outside territory).

### 8. Verification

| step | result |
|---|---|
| 1. `cmake --build build -j24` | **EXIT 0**; `test_headless_shutdown` recompiled with 0 errors, 0 warnings; binary ELF, newer than source |
| 2. full `ctest -j6`, run 1 (21:18-21:21, load 3.3-3.6) | **4770/4770 passed**, ctest rc 0 |
| 2. full `ctest -j6`, run 2 (21:21-21:25, load 8.7-4.8) | **4770/4770 passed**, ctest rc 0 |
| 3-5. `build-gles2-x86.sh`, visual gate, `build-arm-noble.sh` | **not run**: no non-test source changed (the plan's own narrowing) |
| 6. `check_file_sizes.sh fable` | **26 ok, 0 warn, 0 fail**; the test is 353 lines |
| injected-stall repro, pre vs post | **15/15 fail → 15/15 pass** (4) |

⚠ Both full runs happened on a quiet host (other slots idle), so they are the
regression bar, not the load proof. The load proof is the injected-stall table,
which covers both recorded shapes deterministically.

### 9. Board

- **B570**: entered and closed by this unit.
- **B571** (P, latent): the seventeen shared `/tmp` names.
- EXECUTOR_PROTOCOL's load-artefact paragraph: the HeadlessShutdown waiver is
  retired, with the rc=14 misdiagnosis written down.
