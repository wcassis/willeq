#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "client/graphics/background_thread_pool.h"

using namespace EQT::Graphics;

// ---------------------------------------------------------------------------
// Construction and lifecycle
// ---------------------------------------------------------------------------

TEST(BackgroundThreadPool, ConstructWithOneThread) {
    BackgroundThreadPool pool(1);
    EXPECT_EQ(pool.getThreadCount(), 1);
    EXPECT_FALSE(pool.isShutdown());
    EXPECT_EQ(pool.getPendingCount(), 0u);
}

TEST(BackgroundThreadPool, ConstructWithMultipleThreads) {
    BackgroundThreadPool pool(4);
    EXPECT_EQ(pool.getThreadCount(), 4);
}

TEST(BackgroundThreadPool, ConstructWithZeroClampedToOne) {
    BackgroundThreadPool pool(0);
    EXPECT_EQ(pool.getThreadCount(), 1);
}

TEST(BackgroundThreadPool, DestructorCallsShutdown) {
    auto pool = std::make_unique<BackgroundThreadPool>(2);
    std::atomic<int> count{0};
    for (int i = 0; i < 5; ++i)
        pool->submit(0, [&count] { count.fetch_add(1); });
    pool.reset(); // destructor should join cleanly
}

TEST(BackgroundThreadPool, ShutdownWithEmptyQueue) {
    BackgroundThreadPool pool(2);
    pool.shutdown();
    EXPECT_TRUE(pool.isShutdown());
}

TEST(BackgroundThreadPool, DoubleShutdownIsNoop) {
    BackgroundThreadPool pool(1);
    pool.shutdown();
    pool.shutdown(); // should not crash
    EXPECT_TRUE(pool.isShutdown());
}

// ---------------------------------------------------------------------------
// Submit and completion
// ---------------------------------------------------------------------------

TEST(BackgroundThreadPool, SubmitAndComplete) {
    BackgroundThreadPool pool(1);
    std::atomic<bool> done{false};
    pool.submit(0, [&done] { done.store(true); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    EXPECT_TRUE(done.load());
}

TEST(BackgroundThreadPool, MultipleSubmits) {
    BackgroundThreadPool pool(2);
    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i)
        pool.submit(0, [&count] { count.fetch_add(1); });

    pool.shutdown();
    EXPECT_EQ(count.load(), 100);
}

// ---------------------------------------------------------------------------
// Priority ordering
// ---------------------------------------------------------------------------

TEST(BackgroundThreadPool, PriorityOrdering) {
    // Use 1 thread with a gate to ensure tasks queue up before any run
    BackgroundThreadPool pool(1);
    std::atomic<bool> gate{false};
    std::mutex orderMutex;
    std::vector<int> order;

    // Submit a blocking task to hold the thread
    pool.submit(0, [&gate] {
        while (!gate.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });

    // Give it a moment to pick up the blocking task
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Submit tasks with varying priorities (lower value = higher priority)
    pool.submit(100, [&] { std::lock_guard<std::mutex> l(orderMutex); order.push_back(100); });
    pool.submit(10,  [&] { std::lock_guard<std::mutex> l(orderMutex); order.push_back(10); });
    pool.submit(50,  [&] { std::lock_guard<std::mutex> l(orderMutex); order.push_back(50); });
    pool.submit(1,   [&] { std::lock_guard<std::mutex> l(orderMutex); order.push_back(1); });

    // Release the gate
    gate.store(true);
    pool.shutdown();

    ASSERT_EQ(order.size(), 4u);
    // Should be in priority order (ascending value)
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 10);
    EXPECT_EQ(order[2], 50);
    EXPECT_EQ(order[3], 100);
}

TEST(BackgroundThreadPool, FIFOWithinSamePriority) {
    BackgroundThreadPool pool(1);
    std::atomic<bool> gate{false};
    std::mutex orderMutex;
    std::vector<int> order;

    // Block the thread
    pool.submit(0, [&gate] {
        while (!gate.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Submit tasks with same priority
    for (int i = 0; i < 5; ++i)
        pool.submit(10, [&, i] { std::lock_guard<std::mutex> l(orderMutex); order.push_back(i); });

    gate.store(true);
    pool.shutdown();

    ASSERT_EQ(order.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(order[i], i);
}

// ---------------------------------------------------------------------------
// Shutdown while items pending
// ---------------------------------------------------------------------------

TEST(BackgroundThreadPool, ShutdownDrainsPendingItems) {
    // Pool drains remaining items after shutdown signal (workers finish current
    // queue contents but accept no new submits)
    BackgroundThreadPool pool(1);
    std::atomic<int> count{0};

    for (int i = 0; i < 50; ++i)
        pool.submit(0, [&count] {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            count.fetch_add(1);
        });

    pool.shutdown();
    // After shutdown, all tasks that were in the queue should have completed
    EXPECT_EQ(count.load(), 50);
}

// ---------------------------------------------------------------------------
// Concurrent submits from multiple threads
// ---------------------------------------------------------------------------

TEST(BackgroundThreadPool, ConcurrentSubmits) {
    BackgroundThreadPool pool(2);
    std::atomic<int> count{0};

    std::vector<std::thread> submitters;
    for (int t = 0; t < 4; ++t) {
        submitters.emplace_back([&pool, &count] {
            for (int i = 0; i < 25; ++i)
                pool.submit(0, [&count] { count.fetch_add(1); });
        });
    }

    for (auto& t : submitters) t.join();
    pool.shutdown();

    EXPECT_EQ(count.load(), 100);
}

// ---------------------------------------------------------------------------
// Submit after shutdown is ignored
// ---------------------------------------------------------------------------

TEST(BackgroundThreadPool, SubmitAfterShutdownIgnored) {
    BackgroundThreadPool pool(1);
    pool.shutdown();

    std::atomic<bool> ran{false};
    pool.submit(0, [&ran] { ran.store(true); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(ran.load());
}
