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

**Status: DONE.** Executed 2026-07-28 in slot t1, branch
`task/T02-state-event-bus` (base commit 62fa0c8).

### Deliverables

- `tests/test_event_bus.cpp` (256 lines) — 11 cases covering `EventBus`.
- `tests/test_state_modules.cpp` (908 lines) — 72 cases: `EntityManager`,
  `PlayerState`, `SpellState`, `InventoryState` (the priority-1 modules).
- `tests/test_state_modules_secondary.cpp` (650 lines) — 44 cases:
  `GroupState`, `PetState`, `WorldState`, `CombatState`,
  `TradeskillState`, `GameState`.
- **Deviation**: the plan named two files
  (`test_event_bus.cpp` + `test_state_modules.cpp`); a third,
  `test_state_modules_secondary.cpp`, was added because the single
  `test_state_modules.cpp` reached 1521 lines, over the file-size
  check's 1500-line WARN ceiling. The plan explicitly permitted this
  ("Split into a second TU if the file gets unwieldy"). Split at the
  EntityManager/PlayerState/SpellState/InventoryState vs.
  GroupState/PetState/WorldState/CombatState/TradeskillState/GameState
  boundary (matches the plan's own priority ordering). `tests/CMakeLists.txt`
  has three new target blocks accordingly, all in the same style as
  neighboring targets, none introducing a new library target.

Total: 127 new test cases, all green.

### Acceptance criteria

1. **`test_event_bus.cpp` + `test_state_modules.cpp` exist and are wired
   into `tests/CMakeLists.txt`** — PASS (plus the third split file, see
   deviation above).
2. **`cmake --build <slot>/build -j24` clean** — PASS. Full desktop tree
   builds with no errors (pre-existing unrelated warnings only, e.g.
   narrowing-conversion warnings in `inventory_manager.cpp`, not touched
   by this unit).
3. **`ctest --test-dir <slot>/build --output-on-failure` green, minus
   documented host exceptions** — PASS. All 127 new cases pass in every
   run; only failures across all runs were the documented
   `AudioManagerTest.PreloadCommonSounds` (host ALSA) and, once,
   `RDPServerFrameTest.UpdateFrameMultipleTimes` (documented
   intermittent flake family — passed on 2 of 3 runs, consistent with
   "intermittent").
4. **3 consecutive full-suite green runs** — PASS, all post-split (the
   pre-split runs were superseded once the file was split; the 3 runs
   below are the ones that count):
   - Run 1: 1361/1362 passed (1 fail: `AudioManagerTest.PreloadCommonSounds`), 1 skipped visual_gate (no display), 325.8s wall.
   - Run 2: same result, 1 fail (`AudioManagerTest.PreloadCommonSounds`).
   - Run 3: same result, 1 fail (`AudioManagerTest.PreloadCommonSounds`).
   (An earlier run, pre-split, also saw `RDPServerFrameTest.UpdateFrameMultipleTimes`
   SEGFAULT once — the documented intermittent-flake exception; it did
   not recur in the post-split runs, consistent with "intermittent.")
5. **GLES2 tree builds and its ctest is green** — PASS with a flagged,
   pre-existing, out-of-territory finding. `scripts/build-gles2-x86.sh`
   ran clean (builds `willeq` + `gles2_zone_benchmark` only, as the
   script does deliberately). Reconfigured with
   `-DEQT_GLES2_IRRLICHT_DIR=<abs path to>/third_party/irrlicht-gles2-x86
   -DEQT_BUILD_TESTS=ON` (the plan's relative path resolved against the
   wrong cwd for `git rev-parse`-based scripts in this session — used an
   absolute path instead, see Deviations). All three new test targets
   build clean and link no Irrlicht; all 127 cases pass, run 3x.
   **Full `cmake --build build-gles2 -j24` (unscoped) hits 17 pre-existing
   link failures** unrelated to this diff: several standalone graphics
   tools (`zone_line_editor`, `model_viewer`, `s3d_dump`,
   `generate_surface_map`, `entity_composite_builder`) and three test
   binaries (`test_graphics`, `test_asset_loading`, `test_door_loading`)
   fail to link against the patched `irrlicht-gles2-x86` archive with
   undefined references to `X*` (Xlib) symbols — `CIrrDeviceFB_X11.cpp`
   in the archive calls Xlib functions but the link line for these
   particular targets doesn't pull in `-lX11`. This is graphics/Irrlicht
   territory (off-limits per the charter) and predates this unit — T02
   touches nothing under `src/client/graphics/` or the Irrlicht patch.
   Ran `ctest --test-dir build-gles2` anyway; result: 1181/1187 passed,
   3 `_NOT_BUILT` (the three link-failed test binaries above), the
   plan's documented `RaceModelAnimationHeadlessTest.ModelAnimatesProperly_Headless`
   SEGFAULT, and `AudioManagerTest.PreloadCommonSounds`. **This X11-link
   finding is new information for the tracker — filed below, not fixed.**
6. **`check_file_sizes.sh fable` — no new FAILs** — PASS (after the
   3-way split). `tests/test_event_bus.cpp` 256 lines,
   `tests/test_state_modules.cpp` 908 lines,
   `tests/test_state_modules_secondary.cpp` 650 lines — all "ok"
   (well under the 1500-line WARN, let alone the 3000-line FAIL).
7. **Each new test binary runs in under 5 seconds** — PASS by a wide
   margin. Desktop-tree wall times (`time ./binary`):
   - `test_event_bus`: 0.010–0.020s
   - `test_state_modules`: 0.005–0.010s
   - `test_state_modules_secondary`: 0.009s
   GLES2-tree binaries: same order of magnitude (1–11ms per gtest's own
   internal timer).

Visual gate and ARM build: **skipped**, as the plan says — tests-only
diff, no shared source touched.

### Findings (documented in tests, not fixed — per STOP conditions)

1. **`CombatState::fireTargetChangedEvent()` and `fireCombatEvent()` are
   empty stubs** (`src/client/state/combat_state.cpp`). Both check
   `m_eventBus` for null and then do nothing — just comments ("Target
   changed event would go here"). Consequence: `setTarget()`,
   `clearTarget()`, and `setAutoAttacking()` never actually publish
   anything on the `EventBus`, despite `GameEventType::TargetChanged`
   and `GameEventType::CombatEvent` existing in the enum and being
   documented in `event_bus.h` as the mechanism for exactly this. Any
   renderer/UI code expecting target or auto-attack notifications via
   the bus gets nothing from `CombatState` today. Pinned in
   `CombatStateTest.KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent`.
   This is the clearest gap found in the unit.
2. **`InventoryState::fireInventoryChangedEvent()` always reports
   `hasItem=true`, `itemId=0`**, regardless of the actual occupied state
   passed to the setter — the source comment acknowledges this
   ("Caller should update this based on context"). Pinned in
   `InventoryStateTest.InventorySlotChangedEventAlwaysReportsHasItemTrue`.
3. **`WorldState::fireZoneChangedEvent()` reads position from the
   pending-zone fields, not from anything passed to `setZone()` /
   `setZoneName()`.** Calling `setZone()` without a prior
   `setPendingZone()` publishes a `ZoneChanged` event with position
   (0,0,0)/heading 0. Pinned in
   `WorldStateTest.ZoneChangedEventPositionComesFromPendingZoneNotSetZoneArgs`.
4. **X11 link failures in the GLES2-x86 tree** for `test_graphics`,
   `test_asset_loading`, `test_door_loading`, and several standalone
   graphics tools — see acceptance criterion 5 above. Perf-session
   territory (graphics/Irrlicht); not fixed.
5. **Listener lifetime is a genuine, unexercised UB hazard** in
   `EventBus`: a listener capturing `this` of an object destroyed
   mid-dispatch (between the copy-under-lock and the unlocked invoke)
   has nothing keeping the referent alive. Per the plan's instruction,
   this was *not* turned into a test (it would be UB to exercise
   safely) — noted here as an observation for the tracker instead.

Findings 1–3 and 5 belong in `refactor/testing_progress.md` (state/base
layer, this session's territory); finding 4 is a perf-session handoff.
Not added to the tracker file itself in this pass — flagging here per
the plan's "prominently in your Review" instruction; recommend Will
route items 1–3 into the tracker's gap backlog and item 4 into
`TESTING_HANDBACK.md` after review.

### Which modules were NOT meaningfully covered, and why

None of the 10 state modules or `EventBus` itself were skipped — all 11
components have dedicated test suites. Within that, coverage depth
varies by the plan's own stated priority:

- **Fully covered, all documented mutators/accessors/edge cases**:
  `EventBus` (11 cases incl. both re-entrancy behaviors + a bounded
  concurrent-publish test), `EntityManager` (27 cases — spawn/despawn,
  position/HP/equipment updates, all spatial and pet queries, iteration),
  `PlayerState` (13 cases — position, stats, currency, vendor/bank/
  trainer/loot interaction, `loadFromProfile`), `SpellState` (16 cases —
  gems, cooldown math, casting, memorizing, scribed-spell tracking
  including the 64-slot cap), `InventoryState` (12 cases — all three
  slot arrays, cursor, bags, equipment stats, reset/clearBank).
- **Covered but lighter, per the plan's own "small, last" ordering**:
  `GroupState` (10 cases), `PetState` (6 cases), `WorldState` (8 cases),
  `CombatState` (9 cases — including the stub-event finding above),
  `TradeskillState` (6 cases), `GameState` (5 cases — focused on the
  aggregate's own contract: shared EventBus wiring, `resetForZoneChange`,
  `clearAll`, convenience accessors; does not re-test each sub-state's
  full behavior, since that's already covered by their own suites).
- **Not exercised anywhere, by design**: `EventBus` listener-lifetime UB
  (finding 5, cannot be tested safely) and door-state events (
  `DoorSpawned`/`DoorStateChanged`, which `GameState` wires from
  `EQT::DoorStateManager` callbacks) — `door_state_manager.cpp` itself
  (the manager's own add/remove/query logic) was **not** independently
  unit-tested here; it is out of the plan's explicit scope table
  (`src/client/state/*.cpp` only), is linked only because `GameState`'s
  constructor needs it to compile, and `GameStateTest.SubStatesShareTheSameEventBus`
  only exercises the callback wiring indirectly (via player/entity/pet
  events, not doors). If door-state behavior needs coverage, that is a
  natural follow-up unit against `include/client/door_state_manager.h` /
  `src/client/door_state_manager.cpp`, not part of T02's scope.

### Coverage delta

Not measured — the plan says not to stand up a coverage build just for
this. Qualitatively: `src/client/state/` (the T01 survey's "state/EventBus"
row, 1261 instrumented lines at 0.0%) now has 127 passing test cases
exercising all 11 files in the directory (`event_bus.cpp` +
`entity_manager.cpp`, `player_state.cpp`, `spell_state.cpp`,
`inventory_state.cpp`, `group_state.cpp`, `pet_state.cpp`,
`world_state.cpp`, `combat_state.cpp`, `tradeskill_state.cpp`,
`game_state.cpp`), going from linked-into-zero-test-binaries to linked
into three, all green.

### Deviations (flagged)

1. Third test file added (`test_state_modules_secondary.cpp`) — see
   acceptance criterion 1, explicitly permitted by the plan.
2. `scripts/refactor/check_file_sizes.sh` and the GLES2 configure step
   both resolve paths via `git rev-parse --show-toplevel` / cwd-relative
   arguments; this session's shell cwd was `/mnt/projects/fable/testing`
   (the charter's home checkout), not the slot, and never `cd` (per
   protocol). Worked around with `env --chdir=<slot>` for the file-size
   script and an absolute path for `-DEQT_GLES2_IRRLICHT_DIR`, rather
   than `cd`-ing. No script content was modified.
3. New finding for the tracker: GLES2-x86-tree X11 link failures in
   three test binaries + five tools (acceptance criterion 5 / finding 4
   above) — discovered only because this unit ran the full
   `cmake --build build-gles2 -j24`, which `build-gles2-x86.sh` itself
   deliberately avoids (it builds only `willeq` + `gles2_zone_benchmark`).
   Not fixed (graphics/Irrlicht territory); reported for the perf
   session via this Review and recommended for `TESTING_HANDBACK.md`.
