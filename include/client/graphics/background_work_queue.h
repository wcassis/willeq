#ifndef EQT_GRAPHICS_BACKGROUND_WORK_QUEUE_H
#define EQT_GRAPHICS_BACKGROUND_WORK_QUEUE_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include "common/logging.h"
#include <vector>

namespace EQT {
namespace Graphics {

class BackgroundThreadPool;

/// Thread-safe background work queue with single worker thread or pool-backed mode.
/// Requests are processed FIFO by the worker; results are polled from the main thread.
///
/// Two modes:
/// 1. Own-thread mode (legacy): BackgroundWorkQueue owns a dedicated std::thread.
///    Use the single-arg constructor + start()/stop().
/// 2. Pool-backed mode: Work is submitted to a shared BackgroundThreadPool.
///    Use the three-arg constructor. start() is a no-op; stop() waits for in-flight.
template <typename Request, typename Result>
class BackgroundWorkQueue {
public:
    using ProcessorFn = std::function<Result(Request&&)>;
    using BatchHookFn = std::function<void()>;

    /// Own-thread mode constructor.
    explicit BackgroundWorkQueue(ProcessorFn processor)
        : processor_(std::move(processor)) {}

    /// Pool-backed mode constructor.
    BackgroundWorkQueue(ProcessorFn processor, BackgroundThreadPool* pool,
                        uint32_t defaultPriority = 0xFFFFFFFF)
        : processor_(std::move(processor))
        , pool_(pool)
        , defaultPriority_(defaultPriority) {}

    ~BackgroundWorkQueue() { stop(); }

    BackgroundWorkQueue(const BackgroundWorkQueue&) = delete;
    BackgroundWorkQueue& operator=(const BackgroundWorkQueue&) = delete;
    BackgroundWorkQueue(BackgroundWorkQueue&&) = delete;
    BackgroundWorkQueue& operator=(BackgroundWorkQueue&&) = delete;

    /// Set optional hooks called around each batch of work on the worker thread.
    /// Must be called before start(). Only effective in own-thread mode.
    void setBatchHooks(BatchHookFn onBegin, BatchHookFn onEnd) {
        onBatchBegin_ = std::move(onBegin);
        onBatchEnd_ = std::move(onEnd);
    }

    /// Launch the worker thread. No-op if pool-backed or already running.
    void start() {
        if (isPoolBacked()) return;
        if (running_.load(std::memory_order_relaxed))
            return;
        running_.store(true, std::memory_order_relaxed);
        worker_ = std::thread(&BackgroundWorkQueue::workerLoop, this);
    }

    /// Signal shutdown and join the worker thread. In pool-backed mode,
    /// spins until all in-flight tasks complete.
    void stop() {
        if (isPoolBacked()) {
            // Wait for all in-flight pool tasks referencing our state to complete
            while (inFlightCount_.load(std::memory_order_acquire) > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return;
        }
        if (!running_.load(std::memory_order_relaxed))
            return;
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            running_.store(false, std::memory_order_relaxed);
        }
        cv_.notify_one();
        if (worker_.joinable())
            worker_.join();
    }

    /// Add a request to the queue with default priority (thread-safe).
    void submit(Request request) {
        if (isPoolBacked()) {
            submitToPool(std::move(request), defaultPriority_);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            idle_.store(false, std::memory_order_relaxed);
            requestQueue_.push_back(std::move(request));
        }
        cv_.notify_one();
    }

    /// Add a request with explicit priority (pool-backed mode only).
    /// In own-thread mode, falls back to default FIFO submit.
    void submit(Request request, uint32_t priority) {
        if (isPoolBacked()) {
            submitToPool(std::move(request), priority);
            return;
        }
        submit(std::move(request));
    }

    /// Pop one result. Returns false if no results available.
    bool pollOne(Result& out) {
        std::lock_guard<std::mutex> lock(resultMutex_);
        if (resultQueue_.empty())
            return false;
        out = std::move(resultQueue_.front());
        resultQueue_.pop_front();
        return true;
    }

    /// Return all pending results.
    std::vector<Result> pollAll() {
        std::lock_guard<std::mutex> lock(resultMutex_);
        std::vector<Result> results;
        results.reserve(resultQueue_.size());
        for (auto& r : resultQueue_)
            results.push_back(std::move(r));
        resultQueue_.clear();
        return results;
    }

    /// True when no work is in-flight for this queue.
    bool isIdle() const {
        if (isPoolBacked())
            return inFlightCount_.load(std::memory_order_acquire) == 0;
        return idle_.load(std::memory_order_acquire);
    }

    /// Approximate number of pending requests.
    size_t getPendingCount() const {
        if (isPoolBacked())
            return inFlightCount_.load(std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(requestMutex_);
        return requestQueue_.size();
    }

    /// Number of completed results waiting to be polled.
    size_t getCompletedCount() const {
        std::lock_guard<std::mutex> lock(resultMutex_);
        return resultQueue_.size();
    }

    /// Check if this queue is backed by a shared pool.
    bool isPoolBacked() const { return pool_ != nullptr; }

private:
    // Pool-backed submit: wrap processor + result delivery into a lambda
    void submitToPool(Request request, uint32_t priority);

    void workerLoop() {
        while (true) {
            Request req;
            {
                std::unique_lock<std::mutex> lock(requestMutex_);
                cv_.wait(lock, [this] {
                    return !requestQueue_.empty() ||
                           !running_.load(std::memory_order_relaxed);
                });
                if (!running_.load(std::memory_order_relaxed))
                    break;
                req = std::move(requestQueue_.front());
                requestQueue_.pop_front();
            }

            if (onBatchBegin_)
                onBatchBegin_();

            // Process the first request
            {
                Result result = processor_(std::move(req));
                std::lock_guard<std::mutex> lock(resultMutex_);
                resultQueue_.push_back(std::move(result));
            }

            // Drain remaining requests in this batch
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(requestMutex_);
                    if (requestQueue_.empty()) {
                        idle_.store(true, std::memory_order_release);
                        break;
                    }
                    req = std::move(requestQueue_.front());
                    requestQueue_.pop_front();
                }
                Result result = processor_(std::move(req));
                std::lock_guard<std::mutex> lock(resultMutex_);
                resultQueue_.push_back(std::move(result));
            }

            if (onBatchEnd_)
                onBatchEnd_();
            FlushThreadLog();
        }
    }

    ProcessorFn processor_;
    BatchHookFn onBatchBegin_;
    BatchHookFn onBatchEnd_;

    std::thread worker_;
    std::atomic<bool> running_{false};

    mutable std::mutex requestMutex_;
    std::condition_variable cv_;
    std::deque<Request> requestQueue_;

    mutable std::mutex resultMutex_;
    std::deque<Result> resultQueue_;

    std::atomic<bool> idle_{true};

    // Pool-backed mode members
    BackgroundThreadPool* pool_ = nullptr;
    uint32_t defaultPriority_ = 0xFFFFFFFF;
    std::atomic<size_t> inFlightCount_{0};
};

} // namespace Graphics
} // namespace EQT

// Include pool header and define submitToPool after BackgroundThreadPool is complete
#include "client/graphics/background_thread_pool.h"

namespace EQT {
namespace Graphics {

template <typename Request, typename Result>
void BackgroundWorkQueue<Request, Result>::submitToPool(Request request, uint32_t priority) {
    inFlightCount_.fetch_add(1, std::memory_order_relaxed);
    // Wrap request in shared_ptr to make the lambda copyable (std::function requirement).
    // This is safe because the lambda is invoked exactly once and then destroyed.
    auto reqPtr = std::make_shared<Request>(std::move(request));
    auto processor = processor_;
    pool_->submit(priority, [this, processor, reqPtr]() mutable {
        Result result = processor(std::move(*reqPtr));
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            resultQueue_.push_back(std::move(result));
        }
        inFlightCount_.fetch_sub(1, std::memory_order_release);
    });
}

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_BACKGROUND_WORK_QUEUE_H
