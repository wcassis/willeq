#include "client/graphics/background_thread_pool.h"

namespace EQT {
namespace Graphics {

BackgroundThreadPool::BackgroundThreadPool(int threadCount) {
    if (threadCount < 1) threadCount = 1;
    threads_.reserve(threadCount);
    for (int i = 0; i < threadCount; ++i) {
        threads_.emplace_back(&BackgroundThreadPool::workerLoop, this);
    }
}

BackgroundThreadPool::~BackgroundThreadPool() {
    shutdown();
}

void BackgroundThreadPool::submit(uint32_t priority, std::function<void()> task) {
    uint64_t seq = nextSequence_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_.load(std::memory_order_relaxed)) return;
        queue_.push(WorkItem{priority, seq, std::move(task)});
    }
    cv_.notify_one();
}

void BackgroundThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_.load(std::memory_order_relaxed)) return;
        shutdown_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

size_t BackgroundThreadPool::getPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void BackgroundThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || shutdown_.load(std::memory_order_relaxed);
            });
            if (shutdown_.load(std::memory_order_relaxed) && queue_.empty())
                break;
            if (queue_.empty()) continue;
            task = std::move(const_cast<WorkItem&>(queue_.top()).task);
            queue_.pop();
        }
        task();
    }
}

} // namespace Graphics
} // namespace EQT
