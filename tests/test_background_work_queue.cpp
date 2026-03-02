#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "client/graphics/background_work_queue.h"

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
