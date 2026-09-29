// B918 (issue #237): see include/client/fatal_exit.h.

#include "client/fatal_exit.h"

#include <atomic>
#include <mutex>

namespace eqt {

namespace {
std::atomic<bool> g_fatal{false};
std::mutex g_reasonMutex;
std::string g_reason;
}  // namespace

void RecordFatalExit(const std::string& reason) {
    std::lock_guard<std::mutex> lock(g_reasonMutex);
    if (!g_fatal.load(std::memory_order_acquire)) {
        g_reason = reason;
    }
    g_fatal.store(true, std::memory_order_release);
}

bool FatalExitRecorded() {
    return g_fatal.load(std::memory_order_acquire);
}

std::string FatalExitReason() {
    std::lock_guard<std::mutex> lock(g_reasonMutex);
    return g_reason;
}

void ResetFatalExitForTest() {
    std::lock_guard<std::mutex> lock(g_reasonMutex);
    g_reason.clear();
    g_fatal.store(false, std::memory_order_release);
}

}  // namespace eqt
