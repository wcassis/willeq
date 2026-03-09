# A01: Create BackgroundWorkQueue Abstraction

## Plan

### Context

The codebase has 5 independent background threads (EntityPrepWorker, ItemIconLoader,
zone load, deferred work, BSP preload), each with their own thread lifecycle, queues,
and result communication. The architecture design calls for unifying these into a shared
work queue. This step creates the abstraction. No existing code is migrated (A02-A04).

### Step 1: Create `include/client/graphics/background_work_queue.h`

Header-only template `BackgroundWorkQueue<Request, Result>` in `EQT::Graphics`.

Public API:
- `BackgroundWorkQueue(std::function<Result(Request&&)> processor)`
- `~BackgroundWorkQueue()` — calls stop()
- `void setBatchHooks(BatchHookFn onBegin, BatchHookFn onEnd)`
- `void start()` / `void stop()`
- `void submit(Request request)` — thread-safe
- `bool pollOne(Result& out)` / `std::vector<Result> pollAll()`
- `bool isIdle() const` — atomic
- `size_t getPendingCount() const` / `size_t getCompletedCount() const`

Worker loop: wait on cv → pop request → onBatchBegin → process → drain remaining →
onBatchEnd → if empty set idle=true under lock.

idle_ protocol: submit() sets idle_=false before cv notify. Worker sets idle_=true
under requestMutex_ after confirming empty. Prevents stale-idle race.

### Step 2: Create `tests/test_background_work_queue.cpp`

15 test cases covering lifecycle, submit/poll, FIFO ordering, idle transitions,
stop-while-pending, batch hooks, move-only types, count methods.

### Step 3: Add test to `tests/CMakeLists.txt`

```cmake
ADD_EXECUTABLE(test_background_work_queue test_background_work_queue.cpp)
TARGET_LINK_LIBRARIES(test_background_work_queue GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_background_work_queue)
```

No EQT_GRAPHICS guard — pure threading, no graphics dependencies.

## Acceptance Criteria

- [ ] `cmake --build build` succeeds with no errors or warnings from new files
- [ ] `ctest -R test_background_work_queue --output-on-failure` — all tests pass
- [ ] Header includes only standard library headers
- [ ] No existing files modified except `tests/CMakeLists.txt`
- [ ] No existing tests broken

## Review

(To be filled after implementation)
