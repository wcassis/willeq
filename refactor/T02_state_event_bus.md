# T02 — EventBus + state module unit tests

**Slot: t1. x86 only — no Pi, no eqemu, no graphics.** First unit of
the testing campaign (`refactor/TESTING_CHARTER.md`). No sibling unit
is running; t2 is idle.

**Territory: `tests/` ONLY.** You create `tests/test_event_bus.cpp`,
`tests/test_state_modules.cpp`, and add their target blocks to
`tests/CMakeLists.txt`. You modify **no** source under `src/` or
`include/` — not even a one-line seam. If you believe a test is
impossible without a source change, STOP and report it; do not make
the change. (`refactor/testing_progress.md` has a "Needs seam" section
for exactly this.)

Read `refactor/EXECUTOR_PROTOCOL.md` first and follow it exactly.
Read `refactor/testing_progress.md` for the survey this unit came from.

## Why this unit

`src/client/state/` is 1987 lines across 11 modules at **0.0% line
coverage** — nothing in `tests/` references it today. It is the spine
of the threading model: every game→renderer event flows through
`EventBus`. It has no graphics dependency and no I/O, so it is cheap to
test and there is no excuse for it being untested.

Dependency check already done for you: the state modules include only
stdlib, `glm/glm.hpp`, and `client/door_state_manager.h` (from
`game_state.h`; `src/client/door_state_manager.cpp` exists and links
cleanly). **Link no Irrlicht.** If a target you add pulls in Irrlicht,
you have wired it wrong.

## Scope

In scope — `src/client/state/`:

| file | lines | notes |
|------|------:|-------|
| `event_bus.cpp` | 56 | + the type surface in `event_bus.h` (1108 lines, 99 event types) |
| `entity_manager.cpp` | 329 | largest module |
| `player_state.cpp` | 342 | |
| `spell_state.cpp` | 278 | |
| `inventory_state.cpp` | 235 | |
| `group_state.cpp` | 181 | |
| `pet_state.cpp` | 145 | |
| `world_state.cpp` | 142 | |
| `game_state.cpp` | 103 | aggregate/owner |
| `tradeskill_state.cpp` | 96 | |
| `combat_state.cpp` | 80 | |

Out of scope: everything else. Do not touch `irrlicht_bridge.cpp`
(perf territory), `eq*.cpp` (does not compile without graphics — see
`refactor/TESTING_HANDBACK.md`), or any existing test.

## Steps

### Step 1 — `tests/test_event_bus.cpp`

Cover the `EventBus` contract in `src/client/state/event_bus.cpp`.
Read the implementation before writing — it is 56 lines and its
behavior is more subtle than the API suggests.

Required cases, at minimum:

1. Unfiltered `subscribe(listener)` receives every published event type.
2. Filtered `subscribe(type, listener)` receives only matching events
   and is not invoked for others.
3. `unsubscribe(handle)` stops delivery; unsubscribing an already-
   removed or never-issued handle is a no-op, not a crash.
4. `clear()` removes all listeners.
5. Multiple listeners on the same type all fire, and handles are
   distinct.
6. The templated `publish(type, data)` convenience overload delivers
   the same event as the `publish(GameEvent&)` form.

**Re-entrancy — pin the actual behavior, do not assume it.**
`publish()` copies the listener vector under the mutex and then invokes
the copies with the lock released. Two consequences follow, and both
are behavior a future refactor could silently change:

- A listener that calls `unsubscribe()` on *another* listener during
  dispatch does **not** prevent that listener from being called for the
  in-flight event — it was already copied.
- A listener that calls `subscribe()` during dispatch is **not** invoked
  for the in-flight event.

Write a test for each. Assert what the code actually does. If you find
the real behavior differs from what I have described here, **trust the
code, write the test to match it, and say so in your Review** — my
reading of `publish()` may be wrong and I would rather know.

7. Thread safety: concurrent `publish()` from N threads with a listener
   incrementing an atomic counter delivers exactly the expected total.
   Keep it bounded and deterministic — fixed thread count, fixed
   iteration count, join all threads, assert an exact number. **No
   sleeps, no timing assertions, no "eventually" polling.** A test that
   can flake under load is worse than no test; if you cannot make a
   concurrency case deterministic, drop it and note why.

Do **not** attempt to test listener lifetime (a listener capturing
`this` of an object destroyed mid-dispatch). That is a genuine
dangling-reference hazard in the current design, but exercising it is
UB and cannot be asserted safely. Note it in your Review as an
observation for the tracker instead.

### Step 2 — `tests/test_state_modules.cpp`

Cover the 10 state modules. Aim for the **behavior** of each module:
its state transitions, its accessors, and its edge cases. Prioritize
by size and risk — `entity_manager`, `player_state`, `spell_state`,
`inventory_state` first; `combat_state`, `tradeskill_state` last.

For each module, the baseline expectations are:

- Default-constructed state has sane, documented values.
- Each mutator produces the observable change its name implies.
- Lookups for absent keys return the documented "not found" result
  rather than crashing or returning garbage (`entity_manager` and
  `inventory_state` are the ones that matter here).
- Bounds/edge cases: empty containers, index 0, max slot indices,
  double-remove, remove-then-add.

Assert **behavior, not implementation** (charter rule): test through
the public accessors, not by reaching into internals. The exception in
the charter is binary packet layout, which this unit does not touch —
so there is no reason for any `sizeof`/`offsetof` assertion here.

Split into a second TU if the file gets unwieldy; keeping it under the
project's file-size check is your responsibility (step 6 of the
protocol's verification bar).

### Step 3 — CMake wiring

Add target blocks to `tests/CMakeLists.txt` following the existing
style in that file (`ADD_EXECUTABLE` / `TARGET_LINK_LIBRARIES` /
`GTEST_DISCOVER_TESTS`). List the individual `src/client/state/*.cpp`
files the target needs, exactly as neighboring test targets do — do
not introduce a new library target, and do not restructure the file.
Stay inside your own target blocks.

Both new targets must link `GTest::gtest_main` and **no Irrlicht**.

## STOP conditions

- A test requires a source change under `src/` or `include/` → STOP,
  report which test and what change, do not make it.
- A state module turns out to need graphics to compile → STOP and
  report; that contradicts the survey and I want to know immediately.
- You find what looks like a real bug in a state module → **do not fix
  it.** Write a test that documents the current behavior (or an
  `EXPECT` that is disabled with a clear comment if current behavior is
  indefensible), and report it prominently. Fixing base-layer bugs is
  not this unit's job.

## Acceptance criteria

1. `tests/test_event_bus.cpp` and `tests/test_state_modules.cpp` exist
   and are wired into `tests/CMakeLists.txt`.
2. `cmake --build <slot>/build -j24` clean.
3. `ctest --test-dir <slot>/build --output-on-failure` green, minus the
   documented host exceptions (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.*` — rerun those isolated before believing a
   failure).
4. **3 consecutive full-suite green runs** (charter's no-new-flake
   rule). Report all three.
5. GLES2 tree builds and its ctest is green:
   `bash <slot>/scripts/build-gles2-x86.sh`, then configure with
   `-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86
   -DEQT_BUILD_TESTS=ON`. Known genuine failure that is **not yours**:
   `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh fable` — no new
   FAILs.
7. Each new test binary runs in **under 5 seconds**. Report the times.

Verification-bar steps that do **not** apply (tests-only diff, no
shared source touched): the visual gate and the ARM build. Skip both
and say so in your Review.

## Review

Fill in on completion per the protocol: evidence per acceptance
criterion, the three green-run results, per-binary runtimes, any
deviations (flagged, never silently absorbed), and — importantly — a
plain statement of **which state modules you did not meaningfully
cover and why**. A partial-but-honest result is worth more to me than
a claim of full coverage I have to disprove by reading the diff.

Report the coverage delta if you can get it cheaply; do not stand up a
coverage build just for this.
