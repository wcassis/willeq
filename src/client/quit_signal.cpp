// B1099 (GitLab #419): see include/client/quit_signal.h.

#include "client/quit_signal.h"

#include <atomic>
#include <csignal>

#ifndef _WIN32
#include <signal.h>
#endif

namespace eqt {

namespace {
std::atomic<int> g_pending{0};
std::atomic<int> g_seen{0};
std::atomic<bool> g_installed{false};
#ifndef _WIN32
struct sigaction g_prevInt;
struct sigaction g_prevTerm;

void QuitSignalHandler(int sig) {
    if (g_seen.fetch_add(1, std::memory_order_relaxed) == 0) {
        g_pending.store(sig, std::memory_order_relaxed);
        return;
    }
    // Second signal: the previous handler first (the DRM device's keyboard
    // restore re-raises from inside and never returns), then the default.
    const struct sigaction& prev = (sig == SIGINT) ? g_prevInt : g_prevTerm;
    if (!(prev.sa_flags & SA_SIGINFO) && prev.sa_handler != SIG_DFL &&
        prev.sa_handler != SIG_IGN && prev.sa_handler != QuitSignalHandler) {
        prev.sa_handler(sig);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif
}  // namespace

void InstallQuitSignalHandlers() {
#ifndef _WIN32
    if (g_installed.exchange(true)) return;
    struct sigaction sa{};
    sa.sa_handler = QuitSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &g_prevInt);
    sigaction(SIGTERM, &sa, &g_prevTerm);
#endif
}

int TakeQuitSignal() {
    return g_pending.exchange(0, std::memory_order_relaxed);
}

const char* QuitSignalName(int sig) {
#ifndef _WIN32
    if (sig == SIGINT) return "SIGINT";
    if (sig == SIGTERM) return "SIGTERM";
#endif
    return "signal";
}

void ResetQuitSignalsForTest() {
#ifndef _WIN32
    if (g_installed.exchange(false)) {
        sigaction(SIGINT, &g_prevInt, nullptr);
        sigaction(SIGTERM, &g_prevTerm, nullptr);
    }
#endif
    g_pending.store(0);
    g_seen.store(0);
}

}  // namespace eqt
