#ifndef EQT_GRAPHICS_BACKGROUND_WORK_QUEUE_H
#define EQT_GRAPHICS_BACKGROUND_WORK_QUEUE_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace EQT {
namespace Graphics {

/// Thread-safe background work queue with single worker thread.
/// Requests are processed FIFO by the worker; results are polled from the main thread.
template <typename Request, typename Result>
class BackgroundWorkQueue {
public:
    using ProcessorFn = std::function<Result(Request&&)>;
    using BatchHookFn = std::function<void()>;

    explicit BackgroundWorkQueue(ProcessorFn processor)
        : processor_(std::move(processor)) {}

    ~BackgroundWorkQueue() { stop(); }

    BackgroundWorkQueue(const BackgroundWorkQueue&) = delete;
    BackgroundWorkQueue& operator=(const BackgroundWorkQueue&) = delete;
    BackgroundWorkQueue(BackgroundWorkQueue&&) = delete;
    BackgroundWorkQueue& operator=(BackgroundWorkQueue&&) = delete;

    /// Set optional hooks called around each batch of work on the worker thread.
    /// Must be called before start().
    void setBatchHooks(BatchHookFn onBegin, BatchHookFn onEnd) {
        onBatchBegin_ = std::move(onBegin);
        onBatchEnd_ = std::move(onEnd);
    }

    /// Launch the worker thread. No-op if already running.
    void start() {
        if (running_.load(std::memory_order_relaxed))
            return;
        running_.store(true, std::memory_order_relaxed);
        worker_ = std::thread(&BackgroundWorkQueue::workerLoop, this);
    }

    /// Signal shutdown and join the worker thread. No-op if not running.
    void stop() {
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

    /// Add a request to the queue (thread-safe).
    void submit(Request request) {
        {
            std::lock_guard<std::mutex> lock(requestMutex_);
            idle_.store(false, std::memory_order_relaxed);
            requestQueue_.push_back(std::move(request));
        }
        cv_.notify_one();
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

    /// True when the request queue is empty and no work is in-flight.
    bool isIdle() const {
        return idle_.load(std::memory_order_acquire);
    }

    /// Approximate number of pending requests.
    size_t getPendingCount() const {
        std::lock_guard<std::mutex> lock(requestMutex_);
        return requestQueue_.size();
    }

    /// Number of completed results waiting to be polled.
    size_t getCompletedCount() const {
        std::lock_guard<std::mutex> lock(resultMutex_);
        return resultQueue_.size();
    }

private:
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
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_BACKGROUND_WORK_QUEUE_H
