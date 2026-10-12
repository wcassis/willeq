/**
 * T30 exit-matrix pin: headless + piped stdin + NO server -> clean exit.
 *
 * P36 ("Clean-shutdown pass") asserted an "all clean" exit matrix (/camp
 * exit-0 x5, SIGTERM, --script end, headless) but never covered this case:
 * a headless client, driven by piped stdin, that never connects to any
 * server at all. That combination aborted ("terminate called without an
 * active exception" -- a std::thread destroyed while still joinable,
 * ConsoleInputHandler::~ConsoleInputHandler() per the gdb backtrace in
 * refactor/T30_headless_shutdown_fix.md's Review) and never exited on its
 * own (piped /quit was gated behind m_fullyConnected, which a client with
 * no server to connect to never reaches). See that plan for the full
 * root-cause writeup and fix.
 *
 * This is process-level (needs a real spawn of the willeq binary with
 * piped stdin and no eqemu listening), so it can't be a normal in-process
 * ctest the way most of this suite is -- it forks and execs the client.
 *
 * Deliberately points at a TEST-NET-1 address (RFC 5737, 192.0.2.0/24 --
 * reserved for documentation/testing, guaranteed unroutable) rather than
 * relying on "no eqemu happens to be running right now": that would make
 * this test's pass/fail depend on ambient server state instead of pinning
 * the actual "no server reachable at all" case the defect was about.
 *
 * T465 (board B570): WAIT ON THE CLIENT'S OWN SIGNAL, NEVER ON A WALL CLOCK.
 * Until T465 the companion case ran the client under `timeout 5` and then
 * grepped whatever it had printed. It failed in every full `ctest -j6` run
 * of three merged tips on 2026-09-13 and passed standalone every time, in two
 * shapes: nothing printed at all (T461's run, test 115 of 4762, straight after
 * a build -- a cold 577 MB binary still paging in over NFS), and the console
 * banner printed but no `CMD outcome=` line before the kill (the T456 and T463
 * chain runs). Neither is a client defect: the held command is queued, only
 * ever peeked, and printed on the first game-thread input tick, and SIGTERM
 * exits 0 through P36's clean path. The client was simply not given enough
 * wall time; a 5 s window measured the host, not the code. T465 made both
 * shapes on demand by stalling the real client (review in
 * refactor/T465_headless_shutdown_load_flake.md).
 *
 * So each case now reads the child's output until the line it is waiting for
 * arrives, and only a generous deadline bounds the wait (kChildDeadline). A
 * passing run costs its real latency (~0.1 s), not a fixed window. The earlier
 * `sqlite rc=14` "shape" (T360) was never a failure cause: before T399 the
 * client opened the bare relative `data/willeq.db`, which cannot exist in
 * ctest's working directory, so that line was in every run, passing or not.
 *
 * The config goes in a per-process mkdtemp() directory. ctest runs the two
 * cases below as separate, concurrent processes, and every worktree slot runs
 * the same test, so a fixed /tmp path meant one run's SetUp() truncating the
 * file another run's client was reading.
 */

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// T465: the ONLY deadline in this file, and it is a hang bound, not a pass
// window -- every wait below ends as soon as the client produces what it is
// waiting for. Sized against the worst stall this host can impose on a child
// that is still starting: the build tree and the shipped data live on an NFS
// mount with `hard,timeo=600`, so a single lost RPC during the client's cold
// page-in blocks it 60 s before the first retransmit. 120 s covers that once
// with margin. These cases carry no ctest TIMEOUT property, so the worst case
// (five spawns in the first case) stays well inside a whole-suite run.
constexpr std::chrono::seconds kChildDeadline{120};

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// One headless client: argv is `<bin> -c <config> --no-graphics`, stdin is
// `stdinPayload` followed by EOF (what `printf ... |` gave it before), and
// stdout+stderr share one pipe (what `2>&1` gave it).
// B1039 (#359): pipe2() is Linux-only; macOS gets pipe() + FD_CLOEXEC (not
// atomic, but nothing else forks while this single-threaded test does).
int pipeCloexec(int fds[2]) {
#if defined(__linux__)
    return ::pipe2(fds, O_CLOEXEC);
#else
    if (::pipe(fds) != 0) return -1;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}

class HeadlessChild {
public:
    HeadlessChild() = default;
    HeadlessChild(const HeadlessChild&) = delete;
    HeadlessChild& operator=(const HeadlessChild&) = delete;

    ~HeadlessChild() {
        if (pid_ > 0 && !reaped_) {
            ::kill(pid_, SIGKILL);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
        if (outFd_ >= 0) ::close(outFd_);
    }

    bool start(const std::string& bin, const std::string& config,
               const std::string& stdinPayload, std::string& error) {
        // A client that dies before reading its stdin must fail the
        // assertions below, not kill the test process with SIGPIPE.
        std::signal(SIGPIPE, SIG_IGN);

        int inPipe[2];
        int outPipe[2];
        if (pipeCloexec(inPipe) != 0 || pipeCloexec(outPipe) != 0) {
            error = std::string("pipe2: ") + std::strerror(errno);
            return false;
        }
        started_ = Clock::now();
        pid_ = ::fork();
        if (pid_ < 0) {
            error = std::string("fork: ") + std::strerror(errno);
            return false;
        }
        if (pid_ == 0) {
            ::dup2(inPipe[0], STDIN_FILENO);
            ::dup2(outPipe[1], STDOUT_FILENO);
            ::dup2(outPipe[1], STDERR_FILENO);
            std::vector<char*> argv = {
                const_cast<char*>(bin.c_str()),   const_cast<char*>("-c"),
                const_cast<char*>(config.c_str()), const_cast<char*>("--no-graphics"),
                nullptr};
            ::execv(bin.c_str(), argv.data());
            _exit(127);
        }
        ::close(inPipe[0]);
        ::close(outPipe[1]);
        outFd_ = outPipe[0];

        size_t written = 0;
        while (written < stdinPayload.size()) {
            ssize_t n = ::write(inPipe[1], stdinPayload.data() + written,
                                stdinPayload.size() - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;  // the client already went away; asserted later
            written += static_cast<size_t>(n);
        }
        ::close(inPipe[1]);  // EOF, exactly as the end of a printf pipe
        return true;
    }

    // Reads until `prefix` has appeared followed by the end of its line.
    bool readUntilLine(const std::string& prefix, Clock::time_point deadline) {
        for (;;) {
            size_t at = output_.find(prefix);
            if (at != std::string::npos && output_.find('\n', at) != std::string::npos) {
                return true;
            }
            if (eof_ || !pump(deadline)) return false;
        }
    }

    // Reaps the client, draining its output meanwhile so a full pipe can
    // never block it. False if it is still running at the deadline.
    bool waitExit(Clock::time_point deadline) {
        while (!reaped_) {
            int status = 0;
            pid_t r = ::waitpid(pid_, &status, WNOHANG);
            if (r == pid_) {
                reaped_ = true;
                status_ = status;
                break;
            }
            if (r < 0 && errno != EINTR) return false;
            if (Clock::now() >= deadline) return false;
            if (!eof_) {
                pump(std::min(deadline, Clock::now() + std::chrono::milliseconds(50)));
            } else {
                ::usleep(20 * 1000);
            }
        }
        while (!eof_ && pump(deadline)) {}  // whatever it flushed on the way out
        return true;
    }

    void signal(int sig) { ::kill(pid_, sig); }
    const std::string& output() const { return output_; }
    double elapsed() const { return secondsSince(started_); }
    bool exitedNormally() const { return reaped_ && WIFEXITED(status_); }
    int exitCode() const { return exitedNormally() ? WEXITSTATUS(status_) : -1; }
    bool killedBySignal() const { return reaped_ && WIFSIGNALED(status_); }
    int termSignal() const { return killedBySignal() ? WTERMSIG(status_) : 0; }

private:
    // One poll+read step. False when the deadline passed with nothing to read.
    bool pump(Clock::time_point deadline) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now()).count();
        if (remaining <= 0) return false;
        pollfd pfd{outFd_, POLLIN, 0};
        int ready = ::poll(&pfd, 1, static_cast<int>(std::min<long long>(remaining, 1000)));
        if (ready < 0) return errno == EINTR;
        if (ready == 0) return Clock::now() < deadline;
        char buf[4096];
        ssize_t n = ::read(outFd_, buf, sizeof(buf));
        if (n > 0) {
            output_.append(buf, static_cast<size_t>(n));
        } else if (n == 0 || errno != EINTR) {
            eof_ = true;
        }
        return true;
    }

    pid_t pid_ = -1;
    int outFd_ = -1;
    bool eof_ = false;
    bool reaped_ = false;
    int status_ = 0;
    std::string output_;
    Clock::time_point started_{};
};

}  // namespace

class HeadlessShutdownTest : public ::testing::Test {
protected:
    std::string binPath_;
    std::string configPath_;
    fs::path dir_;

    void SetUp() override {
#ifndef WILLEQ_BIN_PATH
        GTEST_SKIP() << "WILLEQ_BIN_PATH not defined at configure time";
        return;
#else
        binPath_ = WILLEQ_BIN_PATH;
        std::error_code ec;
        if (!fs::exists(binPath_, ec)) {
            GTEST_SKIP() << "willeq not built at " << binPath_;
            return;
        }

        // T465: a directory of this process's own (see the file comment).
        std::string tmpl = (fs::temp_directory_path() / "t30_headless_shutdown_XXXXXX").string();
        std::vector<char> name(tmpl.begin(), tmpl.end());
        name.push_back('\0');
        ASSERT_NE(::mkdtemp(name.data()), nullptr) << "mkdtemp(" << tmpl << "): " << std::strerror(errno);
        dir_ = name.data();

        // Minimal, self-contained config -- no eq_client_path (not needed
        // headless), host is TEST-NET-1 (see file comment) so the client
        // never connects, matching the exact defect scenario.
        configPath_ = (dir_ / "config.json").string();
        std::ofstream out(configPath_);
        out << R"({
  "clients": [
    {
      "character": "T30Test",
      "host": "192.0.2.1",
      "port": 5998,
      "server": "T30NoServer",
      "user": "t30test",
      "pass": "test"
    }
  ]
})";
        out.close();
        ASSERT_TRUE(out) << "could not write " << configPath_;
#endif
    }

    void TearDown() override {
        if (!dir_.empty()) {
            std::error_code ec;
            fs::remove_all(dir_, ec);
        }
    }
};

// The acceptance bar from the plan: 5 CONSECUTIVE clean runs (the failure
// was intermittent at short timeouts -- one clean run proves nothing).
TEST_F(HeadlessShutdownTest, PipedQuitWithNoServerExitsCleanFiveTimesInARow) {
    for (int i = 0; i < 5; ++i) {
        SCOPED_TRACE("run " + std::to_string(i + 1));
        HeadlessChild child;
        std::string error;
        // T29's contract: /help then /quit piped to stdin -- the exact repro
        // from refactor/deferred.md's "Headless client aborts on shutdown..."
        // entry and refactor/T30_headless_shutdown_fix.md.
        ASSERT_TRUE(child.start(binPath_, configPath_, "/help\n/quit\n", error)) << error;

        ASSERT_TRUE(child.waitExit(Clock::now() + kChildDeadline))
            << "waiting for the client to EXIT after piped /help + /quit: still running "
            << child.elapsed() << " s after spawn (deadline " << kChildDeadline.count()
            << " s) -- output so far:\n" << child.output();

        EXPECT_FALSE(child.killedBySignal())
            << "client died on signal " << child.termSignal() << " -- output:\n" << child.output();
        ASSERT_TRUE(child.exitedNormally()) << "output:\n" << child.output();
        EXPECT_EQ(child.exitCode(), 0)
            << "expected clean exit 0, got " << child.exitCode() << " -- output:\n" << child.output();
        EXPECT_EQ(child.output().find("terminate called"), std::string::npos)
            << "abort signature present -- output:\n" << child.output();

        // /quit must actually have been dispatched pre-connection (T30's
        // narrow lifecycle exemption), not merely "the process happened to
        // die some other way" -- pin the positive signal too.
        EXPECT_NE(child.output().find("CMD outcome=applied registry=lifecycle"), std::string::npos)
            << "expected pre-connection /quit lifecycle dispatch ack -- output:\n" << child.output();
    }
}

// Companion case: a command that is NOT in the pre-connection lifecycle
// allowlist (movement/combat/etc.) must be held, not dropped or silently
// dispatched -- the gate at Application::processInput()'s m_fullyConnected
// check (application_commands.cpp) exists
// for a real reason (loading-drift protection) and T30 must not have
// widened it. Observability side of the same fix: a script piping into a
// client that never connects must see *something* on stdout instead of
// silence.
TEST_F(HeadlessShutdownTest, NonLifecycleCommandIsHeldNotDroppedOrApplied) {
    HeadlessChild child;
    std::string error;
    ASSERT_TRUE(child.start(binPath_, configPath_, "/attack\n", error)) << error;

    // T465: the readiness signal is the ack line itself. Before T465 this was
    // a fixed 5 s `timeout`, and a client still starting at 5 s failed here.
    ASSERT_TRUE(child.readUntilLine("CMD outcome=", Clock::now() + kChildDeadline))
        << "waiting for the client's 'CMD outcome=' ack of piped /attack (printed on its first "
           "pre-connection input tick): none " << child.elapsed() << " s after spawn (deadline "
        << kChildDeadline.count() << " s) -- output so far:\n" << child.output();

    EXPECT_NE(child.output().find("CMD outcome=held"), std::string::npos)
        << "expected a held/waiting outcome for a non-lifecycle command pre-connection -- output:\n"
        << child.output();

    // Nothing ever unblocks a held command without a server, so the client
    // runs until told to stop. SIGTERM is P36's clean path; it must exit 0.
    child.signal(SIGTERM);
    ASSERT_TRUE(child.waitExit(Clock::now() + kChildDeadline))
        << "waiting for the client to EXIT after SIGTERM: still running " << child.elapsed()
        << " s after spawn -- output so far:\n" << child.output();
    EXPECT_TRUE(child.exitedNormally() && child.exitCode() == 0)
        << "SIGTERM must take the clean-shutdown path (exit 0); signal=" << child.termSignal()
        << " exit=" << child.exitCode() << " -- output:\n" << child.output();

    // Checked over the WHOLE run, shutdown included.
    EXPECT_EQ(child.output().find("CMD outcome=applied"), std::string::npos)
        << "a gameplay command must NOT be dispatched pre-connection (gate widened too far) -- output:\n"
        << child.output();
}
