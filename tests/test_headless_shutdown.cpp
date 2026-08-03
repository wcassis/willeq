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
 * ctest the way most of this suite is -- it shells out via popen(), same
 * pattern as test_entity_viewer_regression.cpp's --entity-viewer shellout.
 *
 * Deliberately points at a TEST-NET-1 address (RFC 5737, 192.0.2.0/24 --
 * reserved for documentation/testing, guaranteed unroutable) rather than
 * relying on "no eqemu happens to be running right now": that would make
 * this test's pass/fail depend on ambient server state instead of pinning
 * the actual "no server reachable at all" case the defect was about.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>

namespace fs = std::filesystem;

namespace {

// Result of one spawn: combined stdout+stderr, and the process's exit code
// (as returned by WEXITSTATUS -- only meaningful if exited normally).
struct RunResult {
    std::string output;
    bool exitedNormally = false;
    int exitCode = -1;
};

RunResult runOnce(const std::string& binPath, const std::string& configPath) {
    RunResult result;
    // T29's contract: /help then /quit piped to stdin -- the exact repro
    // from refactor/deferred.md's "Headless client aborts on shutdown..."
    // entry and refactor/T30_headless_shutdown_fix.md. `timeout` is a
    // safety net only (test-visible hang instead of a stuck ctest run) --
    // the fix under test means it should never actually fire.
    std::string cmd = "printf '/help\\n/quit\\n' | timeout 20 \"" + binPath +
        "\" -c \"" + configPath + "\" --no-graphics 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return result;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        result.output += buf;
    }
    int status = pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        result.exitedNormally = true;
        result.exitCode = WEXITSTATUS(status);
    }
    return result;
}

}  // namespace

class HeadlessShutdownTest : public ::testing::Test {
protected:
    std::string binPath_;
    std::string configPath_;

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

        // Minimal, self-contained config -- no eq_client_path (not needed
        // headless), host is TEST-NET-1 (see file comment) so the client
        // never connects, matching the exact defect scenario.
        fs::path dir = fs::temp_directory_path() / "t30_headless_shutdown_test";
        fs::create_directories(dir, ec);
        configPath_ = (dir / "config.json").string();
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
#endif
    }
};

// The acceptance bar from the plan: 5 CONSECUTIVE clean runs (the failure
// was intermittent at short timeouts -- one clean run proves nothing).
TEST_F(HeadlessShutdownTest, PipedQuitWithNoServerExitsCleanFiveTimesInARow) {
    for (int i = 0; i < 5; ++i) {
        SCOPED_TRACE("run " + std::to_string(i + 1));
        RunResult run = runOnce(binPath_, configPath_);

        ASSERT_TRUE(run.exitedNormally)
            << "process did not exit normally (killed/timed out) -- output:\n" << run.output;
        EXPECT_EQ(run.exitCode, 0)
            << "expected clean exit 0, got " << run.exitCode << " -- output:\n" << run.output;
        EXPECT_EQ(run.output.find("terminate called"), std::string::npos)
            << "abort signature present -- output:\n" << run.output;
        EXPECT_EQ(run.output.find("Segmentation fault"), std::string::npos)
            << "segfault signature present -- output:\n" << run.output;

        // /quit must actually have been dispatched pre-connection (T30's
        // narrow lifecycle exemption), not merely "the process happened to
        // die some other way" -- pin the positive signal too.
        EXPECT_NE(run.output.find("CMD outcome=applied registry=lifecycle"), std::string::npos)
            << "expected pre-connection /quit lifecycle dispatch ack -- output:\n" << run.output;
    }
}

// Companion case: a command that is NOT in the pre-connection lifecycle
// allowlist (movement/combat/etc.) must be held, not dropped or silently
// dispatched -- the gate at application.cpp's m_fullyConnected check exists
// for a real reason (loading-drift protection) and T30 must not have
// widened it. Observability side of the same fix: a script piping into a
// client that never connects must see *something* on stdout instead of
// silence.
TEST_F(HeadlessShutdownTest, NonLifecycleCommandIsHeldNotDroppedOrApplied) {
    std::string cmd = "printf '/attack\\n' | timeout 5 \"" + binPath_ +
        "\" -c \"" + configPath_ + "\" --no-graphics 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);  // expected to time out (nothing ever unblocks it) -- that's fine, we only read output

    EXPECT_NE(output.find("CMD outcome=held"), std::string::npos)
        << "expected a held/waiting outcome for a non-lifecycle command pre-connection -- output:\n"
        << output;
    EXPECT_EQ(output.find("CMD outcome=applied"), std::string::npos)
        << "a gameplay command must NOT be dispatched pre-connection (gate widened too far) -- output:\n"
        << output;
}
