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

// B1099 (#419): the could-not-connect record, same shape.
namespace {
std::atomic<bool> g_noConnect{false};
std::mutex g_noConnectMutex;
std::string g_noConnectReason;
}  // namespace

void RecordCouldNotConnect(const std::string& reason) {
    std::lock_guard<std::mutex> lock(g_noConnectMutex);
    if (!g_noConnect.load(std::memory_order_acquire)) {
        g_noConnectReason = reason;
    }
    g_noConnect.store(true, std::memory_order_release);
}

bool CouldNotConnectRecorded() {
    return g_noConnect.load(std::memory_order_acquire);
}

std::string CouldNotConnectReason() {
    std::lock_guard<std::mutex> lock(g_noConnectMutex);
    return g_noConnectReason;
}

void ResetCouldNotConnectForTest() {
    std::lock_guard<std::mutex> lock(g_noConnectMutex);
    g_noConnectReason.clear();
    g_noConnect.store(false, std::memory_order_release);
}

}  // namespace eqt
