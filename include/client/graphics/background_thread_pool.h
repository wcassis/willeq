#ifndef EQT_GRAPHICS_BACKGROUND_THREAD_POOL_H
#define EQT_GRAPHICS_BACKGROUND_THREAD_POOL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace EQT {
namespace Graphics {

/// Shared priority-ordered thread pool for background work.
/// Lower priority value = higher priority (runs first).
/// Within the same priority level, tasks run in FIFO order.
class BackgroundThreadPool {
public:
    explicit BackgroundThreadPool(int threadCount);
    ~BackgroundThreadPool();

    BackgroundThreadPool(const BackgroundThreadPool&) = delete;
    BackgroundThreadPool& operator=(const BackgroundThreadPool&) = delete;

    /// Submit a task with the given priority (lower = higher priority).
    void submit(uint32_t priority, std::function<void()> task);

    /// Signal all threads to finish and join them. Safe to call multiple times.
    void shutdown();

    bool isShutdown() const { return shutdown_.load(std::memory_order_acquire); }
    int getThreadCount() const { return static_cast<int>(threads_.size()); }
    size_t getPendingCount() const;

private:
    struct WorkItem {
        uint32_t priority;
        uint64_t sequence;  // FIFO tiebreaker within same priority
        std::function<void()> task;

        bool operator>(const WorkItem& o) const {
            if (priority != o.priority) return priority > o.priority;
            return sequence > o.sequence;
        }
    };

    void workerLoop();

    std::vector<std::thread> threads_;
    std::priority_queue<WorkItem, std::vector<WorkItem>, std::greater<WorkItem>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};
    std::atomic<uint64_t> nextSequence_{0};
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_BACKGROUND_THREAD_POOL_H
