#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "client/graphics/background_work_queue.h"
#include "client/graphics/background_thread_pool.h"

using namespace EQT::Graphics;

// ---------------------------------------------------------------------------
// Test types
// ---------------------------------------------------------------------------

struct TestRequest {
    int id;
    std::string data;
};

struct TestResult {
    int id;
    std::string processed;
    bool success;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static TestResult simpleProcessor(TestRequest&& req) {
    return {req.id, "processed:" + req.data, true};
}

static TestResult slowProcessor(TestRequest&& req) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return {req.id, "slow:" + req.data, true};
}

template <typename Queue>
static bool waitForIdle(Queue& q, int timeoutMs) {
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!q.isIdle()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

template <typename Queue>
static bool waitForResults(Queue& q, size_t count, int timeoutMs) {
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (q.getCompletedCount() < count) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(BackgroundWorkQueue, ConstructionDoesNotStartThread) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    EXPECT_TRUE(q.isIdle());
    EXPECT_EQ(q.getPendingCount(), 0u);
    EXPECT_EQ(q.getCompletedCount(), 0u);
}

TEST(BackgroundWorkQueue, StartStop) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();
    q.stop();
    EXPECT_TRUE(q.isIdle());
}

TEST(BackgroundWorkQueue, DoubleStartIsNoop) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();
    q.start(); // should not crash or spawn a second thread
    q.stop();
}

TEST(BackgroundWorkQueue, DoubleStopIsNoop) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();
    q.stop();
    q.stop(); // should not crash
}

TEST(BackgroundWorkQueue, DestructorCallsStop) {
    auto q = std::make_unique<BackgroundWorkQueue<TestRequest, TestResult>>(
        slowProcessor);
    q->start();
    for (int i = 0; i < 5; ++i)
        q->submit({i, "item"});
    q.reset(); // destructor should join without hanging
}

TEST(BackgroundWorkQueue, SubmitAndPollOne) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();
    q.submit({42, "hello"});

    ASSERT_TRUE(waitForIdle(q, 2000));

    TestResult result;
    ASSERT_TRUE(q.pollOne(result));
    EXPECT_EQ(result.id, 42);
    EXPECT_EQ(result.processed, "processed:hello");
    EXPECT_TRUE(result.success);

    q.stop();
}

TEST(BackgroundWorkQueue, MultipleItemsDrainInOrder) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();

    q.submit({1, "a"});
    q.submit({2, "b"});
    q.submit({3, "c"});

    ASSERT_TRUE(waitForResults(q, 3, 2000));

    auto results = q.pollAll();
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].id, 1);
    EXPECT_EQ(results[1].id, 2);
    EXPECT_EQ(results[2].id, 3);
    EXPECT_EQ(results[0].processed, "processed:a");
    EXPECT_EQ(results[1].processed, "processed:b");
    EXPECT_EQ(results[2].processed, "processed:c");

    q.stop();
}

TEST(BackgroundWorkQueue, PollAllReturnsEmptyWhenNoResults) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();
    auto results = q.pollAll();
    EXPECT_TRUE(results.empty());
    q.stop();
}

TEST(BackgroundWorkQueue, PollOneAndPollAllInterleaved) {
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.start();

    q.submit({1, "a"});
    q.submit({2, "b"});
    q.submit({3, "c"});

    ASSERT_TRUE(waitForResults(q, 3, 2000));

    TestResult first;
    ASSERT_TRUE(q.pollOne(first));
    EXPECT_EQ(first.id, 1);

    auto rest = q.pollAll();
    ASSERT_EQ(rest.size(), 2u);
    EXPECT_EQ(rest[0].id, 2);
    EXPECT_EQ(rest[1].id, 3);

    q.stop();
}

TEST(BackgroundWorkQueue, IdleTransitions) {
    BackgroundWorkQueue<TestRequest, TestResult> q(slowProcessor);
    q.start();

    EXPECT_TRUE(q.isIdle());

    q.submit({1, "work"});
    // Right after submit, idle should be false
    EXPECT_FALSE(q.isIdle());

    ASSERT_TRUE(waitForIdle(q, 2000));
    EXPECT_TRUE(q.isIdle());

    q.stop();
}

TEST(BackgroundWorkQueue, StopWhileItemsPending) {
    std::atomic<int> processedCount{0};
    BackgroundWorkQueue<TestRequest, TestResult> q(
        [&processedCount](TestRequest&& req) -> TestResult {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            processedCount.fetch_add(1, std::memory_order_relaxed);
            return {req.id, "done", true};
        });
    q.start();

    for (int i = 0; i < 20; ++i)
        q.submit({i, "item"});

    // Stop immediately — should not hang, and not all items need to be processed
    q.stop();

    int count = processedCount.load(std::memory_order_relaxed);
    EXPECT_LT(count, 20);
}

TEST(BackgroundWorkQueue, BatchHooksCalledAroundWork) {
    std::atomic<int> beginCount{0};
    std::atomic<int> endCount{0};

    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.setBatchHooks(
        [&beginCount] { beginCount.fetch_add(1, std::memory_order_relaxed); },
        [&endCount] { endCount.fetch_add(1, std::memory_order_relaxed); });
    q.start();

    q.submit({1, "a"});
    q.submit({2, "b"});
    q.submit({3, "c"});

    ASSERT_TRUE(waitForIdle(q, 2000));

    q.stop();

    EXPECT_GE(beginCount.load(), 1);
    EXPECT_GE(endCount.load(), 1);
    EXPECT_EQ(beginCount.load(), endCount.load());
}

TEST(BackgroundWorkQueue, BatchHooksNotCalledWhenNoWork) {
    std::atomic<int> beginCount{0};
    std::atomic<int> endCount{0};

    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor);
    q.setBatchHooks(
        [&beginCount] { beginCount.fetch_add(1, std::memory_order_relaxed); },
        [&endCount] { endCount.fetch_add(1, std::memory_order_relaxed); });
    q.start();
    // No submits
    q.stop();

    EXPECT_EQ(beginCount.load(), 0);
    EXPECT_EQ(endCount.load(), 0);
}

TEST(BackgroundWorkQueue, MoveOnlyTypes) {
    struct MoveRequest {
        std::unique_ptr<int> value;
    };
    struct MoveResult {
        std::unique_ptr<int> value;
    };

    BackgroundWorkQueue<MoveRequest, MoveResult> q(
        [](MoveRequest&& req) -> MoveResult {
            return {std::make_unique<int>(*req.value * 2)};
        });
    q.start();

    MoveRequest req;
    req.value = std::make_unique<int>(21);
    q.submit(std::move(req));

    ASSERT_TRUE(waitForIdle(q, 2000));

    MoveResult result;
    ASSERT_TRUE(q.pollOne(result));
    ASSERT_NE(result.value, nullptr);
    EXPECT_EQ(*result.value, 42);

    q.stop();
}

TEST(BackgroundWorkQueue, GetPendingAndCompletedCounts) {
    // Use a slow processor so we can observe pending count > 0
    std::atomic<bool> gate{false};
    BackgroundWorkQueue<TestRequest, TestResult> q(
        [&gate](TestRequest&& req) -> TestResult {
            while (!gate.load(std::memory_order_relaxed))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return {req.id, "done", true};
        });
    q.start();

    // Submit several items while processor is blocked
    for (int i = 0; i < 5; ++i)
        q.submit({i, "item"});

    // Give the worker a moment to pick up the first item
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // At least some should still be pending (worker is blocked on the first)
    size_t pending = q.getPendingCount();
    EXPECT_GE(pending, 3u); // worker took at most 1, so >= 4 pending, but allow slack

    EXPECT_EQ(q.getCompletedCount(), 0u); // nothing completed yet

    // Release the gate
    gate.store(true, std::memory_order_relaxed);

    ASSERT_TRUE(waitForIdle(q, 2000));

    EXPECT_EQ(q.getPendingCount(), 0u);
    EXPECT_EQ(q.getCompletedCount(), 5u);

    q.stop();
}

// ---------------------------------------------------------------------------
// Pool-backed mode tests
// ---------------------------------------------------------------------------

TEST(BackgroundWorkQueue, PoolBacked_SubmitAndPoll) {
    BackgroundThreadPool pool(1);
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor, &pool);

    q.submit({42, "hello"});
    ASSERT_TRUE(waitForIdle(q, 2000));

    TestResult result;
    ASSERT_TRUE(q.pollOne(result));
    EXPECT_EQ(result.id, 42);
    EXPECT_EQ(result.processed, "processed:hello");
}

TEST(BackgroundWorkQueue, PoolBacked_StartIsNoop) {
    BackgroundThreadPool pool(1);
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor, &pool);

    q.start(); // should be no-op
    EXPECT_TRUE(q.isPoolBacked());

    q.submit({1, "a"});
    ASSERT_TRUE(waitForIdle(q, 2000));

    TestResult result;
    ASSERT_TRUE(q.pollOne(result));
    EXPECT_EQ(result.id, 1);
}

TEST(BackgroundWorkQueue, PoolBacked_IsIdleTracking) {
    BackgroundThreadPool pool(1);
    std::atomic<bool> gate{false};

    BackgroundWorkQueue<TestRequest, TestResult> q(
        [&gate](TestRequest&& req) -> TestResult {
            while (!gate.load(std::memory_order_relaxed))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return {req.id, "done", true};
        }, &pool);

    EXPECT_TRUE(q.isIdle());

    q.submit({1, "work"});
    // Give pool thread time to pick it up
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(q.isIdle());

    gate.store(true, std::memory_order_relaxed);
    ASSERT_TRUE(waitForIdle(q, 2000));
    EXPECT_TRUE(q.isIdle());
}

TEST(BackgroundWorkQueue, PoolBacked_MultipleItems) {
    BackgroundThreadPool pool(2);
    BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor, &pool);

    q.submit({1, "a"});
    q.submit({2, "b"});
    q.submit({3, "c"});

    ASSERT_TRUE(waitForResults(q, 3, 2000));

    auto results = q.pollAll();
    ASSERT_EQ(results.size(), 3u);

    // Results may arrive in any order with multiple pool threads
    std::set<int> ids;
    for (const auto& r : results) ids.insert(r.id);
    EXPECT_EQ(ids.count(1), 1u);
    EXPECT_EQ(ids.count(2), 1u);
    EXPECT_EQ(ids.count(3), 1u);
}

TEST(BackgroundWorkQueue, PoolBacked_SubmitWithPriority) {
    BackgroundThreadPool pool(1);
    std::atomic<bool> gate{false};
    std::mutex orderMutex;
    std::vector<int> order;

    BackgroundWorkQueue<TestRequest, TestResult> q(
        [&](TestRequest&& req) -> TestResult {
            std::lock_guard<std::mutex> l(orderMutex);
            order.push_back(req.id);
            return {req.id, "done", true};
        }, &pool);

    // Block the pool thread
    pool.submit(0, [&gate] {
        while (!gate.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Submit with explicit priorities (lower = higher priority)
    q.submit({3, "low"}, 100);
    q.submit({1, "high"}, 10);
    q.submit({2, "mid"}, 50);

    gate.store(true, std::memory_order_relaxed);
    ASSERT_TRUE(waitForIdle(q, 2000));

    // With 1 pool thread, tasks should execute in priority order
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);  // priority 10
    EXPECT_EQ(order[1], 2);  // priority 50
    EXPECT_EQ(order[2], 3);  // priority 100
}

TEST(BackgroundWorkQueue, PoolBacked_DestroyWhileIdle) {
    BackgroundThreadPool pool(1);
    {
        BackgroundWorkQueue<TestRequest, TestResult> q(simpleProcessor, &pool);
        q.submit({1, "a"});
        ASSERT_TRUE(waitForIdle(q, 2000));
    } // destructor calls stop() which should return immediately
}

TEST(BackgroundWorkQueue, PoolBacked_StopWaitsForInFlight) {
    BackgroundThreadPool pool(1);
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};

    BackgroundWorkQueue<TestRequest, TestResult> q(
        [&](TestRequest&& req) -> TestResult {
            started.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            finished.store(true);
            return {req.id, "done", true};
        }, &pool);

    q.submit({1, "slow"});

    // Wait for task to start
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!started.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    q.stop(); // should block until the task completes
    EXPECT_TRUE(finished.load());
}
