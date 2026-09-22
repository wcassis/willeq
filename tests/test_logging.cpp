#include <gtest/gtest.h>
#include "common/logging.h"
#include <sstream>
#include <regex>
#include <atomic>
#include <thread>
#include <chrono>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset to default state before each test
        SetLogLevel(LOG_NONE);
        for (int i = 0; i < MOD_COUNT; i++) {
            SetModuleLogLevel(static_cast<LogModule>(i), -1);
        }
    }

    void TearDown() override {
        // Reset after each test
        SetLogLevel(LOG_NONE);
    }
};

// =============================================================================
// Log Level Tests
// =============================================================================

TEST_F(LoggingTest, DefaultLevel_IsNone) {
    EXPECT_EQ(GetLogLevel(), LOG_NONE);
}

TEST_F(LoggingTest, SetLogLevel_Works) {
    SetLogLevel(LOG_DEBUG);
    EXPECT_EQ(GetLogLevel(), LOG_DEBUG);

    SetLogLevel(LOG_TRACE);
    EXPECT_EQ(GetLogLevel(), LOG_TRACE);

    SetLogLevel(LOG_NONE);
    EXPECT_EQ(GetLogLevel(), LOG_NONE);
}

TEST_F(LoggingTest, LogLevelIncrease_Works) {
    SetLogLevel(LOG_NONE);

    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_FATAL);

    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_ERROR);

    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_WARN);

    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_INFO);

    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_DEBUG);

    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_TRACE);

    // Should not go above TRACE
    LogLevelIncrease();
    EXPECT_EQ(GetLogLevel(), LOG_TRACE);
}

TEST_F(LoggingTest, LogLevelDecrease_Works) {
    SetLogLevel(LOG_TRACE);

    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_DEBUG);

    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_INFO);

    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_WARN);

    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_ERROR);

    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_FATAL);

    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_NONE);

    // Should not go below NONE
    LogLevelDecrease();
    EXPECT_EQ(GetLogLevel(), LOG_NONE);
}

// =============================================================================
// Module Level Tests
// =============================================================================

TEST_F(LoggingTest, ModuleLevel_DefaultIsNegativeOne) {
    // -1 means "use global level"
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), -1);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_GRAPHICS), -1);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
}

TEST_F(LoggingTest, SetModuleLogLevel_Works) {
    SetModuleLogLevel(MOD_NET, LOG_DEBUG);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_DEBUG);

    SetModuleLogLevel(MOD_GRAPHICS, LOG_TRACE);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_GRAPHICS), LOG_TRACE);

    // Other modules should be unchanged
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
}

// =============================================================================
// ShouldLog Tests
// =============================================================================

TEST_F(LoggingTest, ShouldLog_ErrorAlwaysAtNone) {
    SetLogLevel(LOG_NONE);

    // ERROR and FATAL should always log (except if module explicitly suppresses)
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_ERROR));
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_FATAL));

    // Higher levels should not log at NONE
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_WARN));
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_INFO));
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_DEBUG));
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_TRACE));
}

TEST_F(LoggingTest, ShouldLog_RespectsGlobalLevel) {
    SetLogLevel(LOG_INFO);

    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_FATAL));
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_ERROR));
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_WARN));
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_INFO));
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_DEBUG));
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_TRACE));
}

TEST_F(LoggingTest, ShouldLog_ModuleOverridesGlobal) {
    SetLogLevel(LOG_INFO);  // Global at INFO
    SetModuleLogLevel(MOD_NET, LOG_TRACE);  // NET at TRACE

    // NET module should log at TRACE level
    EXPECT_TRUE(ShouldLog(MOD_NET, LOG_TRACE));
    EXPECT_TRUE(ShouldLog(MOD_NET, LOG_DEBUG));

    // Other modules should respect global level
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_TRACE));
    EXPECT_FALSE(ShouldLog(MOD_MAIN, LOG_DEBUG));
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_INFO));
}

TEST_F(LoggingTest, ShouldLog_ModuleCanRestrictBelowGlobal) {
    SetLogLevel(LOG_DEBUG);  // Global at DEBUG
    SetModuleLogLevel(MOD_GRAPHICS, LOG_WARN);  // GRAPHICS at WARN only

    // GRAPHICS module should only log at WARN and above
    EXPECT_TRUE(ShouldLog(MOD_GRAPHICS, LOG_WARN));
    EXPECT_FALSE(ShouldLog(MOD_GRAPHICS, LOG_INFO));
    EXPECT_FALSE(ShouldLog(MOD_GRAPHICS, LOG_DEBUG));

    // Other modules should respect global level
    EXPECT_TRUE(ShouldLog(MOD_MAIN, LOG_DEBUG));
}

// =============================================================================
// Name Parsing Tests
// =============================================================================

TEST_F(LoggingTest, ParseLevelName_Works) {
    EXPECT_EQ(ParseLevelName("NONE"), LOG_NONE);
    EXPECT_EQ(ParseLevelName("OFF"), LOG_NONE);
    EXPECT_EQ(ParseLevelName("FATAL"), LOG_FATAL);
    EXPECT_EQ(ParseLevelName("ERROR"), LOG_ERROR);
    EXPECT_EQ(ParseLevelName("WARN"), LOG_WARN);
    EXPECT_EQ(ParseLevelName("INFO"), LOG_INFO);
    EXPECT_EQ(ParseLevelName("DEBUG"), LOG_DEBUG);
    EXPECT_EQ(ParseLevelName("TRACE"), LOG_TRACE);

    // Unknown should return NONE
    EXPECT_EQ(ParseLevelName("UNKNOWN"), LOG_NONE);
    EXPECT_EQ(ParseLevelName(""), LOG_NONE);
}

TEST_F(LoggingTest, TryParseModuleName_Works) {
    LogModule mod;
    EXPECT_TRUE(TryParseModuleName("NET", mod));
    EXPECT_EQ(mod, MOD_NET);
    EXPECT_TRUE(TryParseModuleName("NET_PACKET", mod));
    EXPECT_EQ(mod, MOD_NET_PACKET);
    EXPECT_TRUE(TryParseModuleName("LOGIN", mod));
    EXPECT_EQ(mod, MOD_LOGIN);
    EXPECT_TRUE(TryParseModuleName("WORLD", mod));
    EXPECT_EQ(mod, MOD_WORLD);
    EXPECT_TRUE(TryParseModuleName("ZONE", mod));
    EXPECT_EQ(mod, MOD_ZONE);
    EXPECT_TRUE(TryParseModuleName("ENTITY", mod));
    EXPECT_EQ(mod, MOD_ENTITY);
    EXPECT_TRUE(TryParseModuleName("MOVEMENT", mod));
    EXPECT_EQ(mod, MOD_MOVEMENT);
    EXPECT_TRUE(TryParseModuleName("COMBAT", mod));
    EXPECT_EQ(mod, MOD_COMBAT);
    EXPECT_TRUE(TryParseModuleName("INVENTORY", mod));
    EXPECT_EQ(mod, MOD_INVENTORY);
    EXPECT_TRUE(TryParseModuleName("GRAPHICS", mod));
    EXPECT_EQ(mod, MOD_GRAPHICS);
    EXPECT_TRUE(TryParseModuleName("GRAPHICS_LOAD", mod));
    EXPECT_EQ(mod, MOD_GRAPHICS_LOAD);
    EXPECT_TRUE(TryParseModuleName("CAMERA", mod));
    EXPECT_EQ(mod, MOD_CAMERA);
    EXPECT_TRUE(TryParseModuleName("INPUT", mod));
    EXPECT_EQ(mod, MOD_INPUT);
    EXPECT_TRUE(TryParseModuleName("AUDIO", mod));
    EXPECT_EQ(mod, MOD_AUDIO);
    EXPECT_TRUE(TryParseModuleName("PATHFIND", mod));
    EXPECT_EQ(mod, MOD_PATHFIND);
    EXPECT_TRUE(TryParseModuleName("MAP", mod));
    EXPECT_EQ(mod, MOD_MAP);
    EXPECT_TRUE(TryParseModuleName("UI", mod));
    EXPECT_EQ(mod, MOD_UI);
    EXPECT_TRUE(TryParseModuleName("CONFIG", mod));
    EXPECT_EQ(mod, MOD_CONFIG);
    EXPECT_TRUE(TryParseModuleName("MAIN", mod));
    EXPECT_EQ(mod, MOD_MAIN);

    // Unknown: false, and the out parameter must not be silently filled with
    // MAIN (B850 -- that was the silent mapping).
    EXPECT_FALSE(TryParseModuleName("UNKNOWN", mod));
    EXPECT_FALSE(TryParseModuleName("", mod));
}

TEST_F(LoggingTest, GetModuleName_Works) {
    EXPECT_STREQ(GetModuleName(MOD_NET), "NET");
    EXPECT_STREQ(GetModuleName(MOD_GRAPHICS), "GRAPHICS");
    EXPECT_STREQ(GetModuleName(MOD_MAIN), "MAIN");
    EXPECT_STREQ(GetModuleName(MOD_COMBAT), "COMBAT");
}

TEST_F(LoggingTest, GetLevelName_Works) {
    EXPECT_STREQ(GetLevelName(LOG_FATAL), "FATAL");
    EXPECT_STREQ(GetLevelName(LOG_ERROR), "ERROR");
    EXPECT_STREQ(GetLevelName(LOG_WARN), "WARN ");
    EXPECT_STREQ(GetLevelName(LOG_INFO), "INFO ");
    EXPECT_STREQ(GetLevelName(LOG_DEBUG), "DEBUG");
    EXPECT_STREQ(GetLevelName(LOG_TRACE), "TRACE");
}

// =============================================================================
// InitLogging Command-Line Parsing Tests
// =============================================================================

TEST_F(LoggingTest, InitLogging_ParsesLogLevel) {
    const char* argv[] = {"program", "--log-level=DEBUG"};
    int argc = 2;

    InitLogging(argc, const_cast<char**>(argv));

    EXPECT_EQ(GetLogLevel(), LOG_DEBUG);
}

TEST_F(LoggingTest, InitLogging_ParsesModuleLevel) {
    const char* argv[] = {"program", "--log-module=NET:TRACE"};
    int argc = 2;

    InitLogging(argc, const_cast<char**>(argv));

    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_TRACE);
}

TEST_F(LoggingTest, InitLogging_ParsesMultipleModules) {
    const char* argv[] = {"program", "--log-module=NET:TRACE", "--log-module=GRAPHICS:DEBUG"};
    int argc = 3;

    InitLogging(argc, const_cast<char**>(argv));

    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_TRACE);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_GRAPHICS), LOG_DEBUG);
}

TEST_F(LoggingTest, InitLogging_ParsesLevelAndModules) {
    const char* argv[] = {"program", "--log-level=INFO", "--log-module=NET:DEBUG"};
    int argc = 3;

    InitLogging(argc, const_cast<char**>(argv));

    EXPECT_EQ(GetLogLevel(), LOG_INFO);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_DEBUG);
}

// =============================================================================
// B842: comma-separated --log-module lists and loud rejection of bad levels
// =============================================================================
// The pre-fix code split the value on the FIRST ':' and handed the rest
// ("INFO,GRAPHICS:INFO") to ParseLevelName(), which fell back to LOG_NONE --
// silencing MAIN entirely and never setting GRAPHICS at all.

TEST_F(LoggingTest, InitLogging_ParsesCommaSeparatedModuleList) {
    const char* argv[] = {"program", "--log-module=MAIN:INFO,GRAPHICS:INFO"};
    int argc = 2;

    InitLogging(argc, const_cast<char**>(argv));

    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), LOG_INFO);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_GRAPHICS), LOG_INFO);
    // A module not named in the list keeps "use global level"
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), -1);
}

TEST_F(LoggingTest, InitLogging_RejectsUnparseableLevelName) {
    // Same stderr-capture pattern the T31 drain-thread test below uses for
    // stdout: the rejection must be LOUD (printed, naming the bad token)
    // and must not silently map the module to any level.
    char tmpl[] = "/tmp/willeq_b842_stderr_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    const char* argv[] = {"program", "--log-module=MAIN:BOGUS"};
    int argc = 2;
    InitLogging(argc, const_cast<char**>(argv));

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("BOGUS"), std::string::npos)
        << "rejection must name the bad token; captured stderr: [" << content << "]";
    // MAIN must NOT have been silently mapped to a level (LOG_NONE or
    // anything else): it stays at -1 = "use global level".
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
}

TEST_F(LoggingTest, InitLogging_BadPairDoesNotPoisonGoodPairs) {
    char tmpl[] = "/tmp/willeq_b842_stderr2_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    const char* argv[] = {"program", "--log-module=NET:TRACE,MAIN:BOGUS,UI:DEBUG"};
    int argc = 2;
    InitLogging(argc, const_cast<char**>(argv));

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("BOGUS"), std::string::npos)
        << "rejection must name the bad token; captured stderr: [" << content << "]";
    // The good pairs around the bad one still apply
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_TRACE);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_UI), LOG_DEBUG);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
}

// =============================================================================
// B850: a typo'd MODULE name must not be silently mapped to MAIN
// =============================================================================
// The pre-fix ApplyLogModulePair() handed the module half to ParseModuleName(),
// whose fallback is MOD_MAIN -- so --log-module=NTE:DEBUG (a typo of NET) set
// MAIN to DEBUG and said nothing. Same defect class B842 closed for levels:
// the rejection must be LOUD (name the bad token on stderr) and the module
// state must be untouched.

TEST_F(LoggingTest, InitLogging_RejectsUnknownModuleName) {
    // Same stderr-capture pattern as InitLogging_RejectsUnparseableLevelName.
    char tmpl[] = "/tmp/willeq_b850_stderr_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    const char* argv[] = {"program", "--log-module=NTE:DEBUG"};
    int argc = 2;
    InitLogging(argc, const_cast<char**>(argv));

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("NTE"), std::string::npos)
        << "rejection must name the bad module; captured stderr: [" << content << "]";
    // MAIN must NOT have been silently mapped to DEBUG: it stays at -1.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
    // And no module at all may have been set by the typo.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), -1);
}

TEST_F(LoggingTest, InitLogging_UnknownModuleDoesNotPoisonGoodPairs) {
    char tmpl[] = "/tmp/willeq_b850_stderr2_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    const char* argv[] = {"program", "--log-module=NET:TRACE,NTE:DEBUG,UI:DEBUG"};
    int argc = 2;
    InitLogging(argc, const_cast<char**>(argv));

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("NTE"), std::string::npos)
        << "rejection must name the bad module; captured stderr: [" << content << "]";
    // The good pairs around the bad one still apply
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_TRACE);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_UI), LOG_DEBUG);
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
}

// The same silent-mapping existed on the whitelist/blacklist paths:
// ApplyLogOnly/ApplyLogExclude handed every token to ParseModuleName's
// MOD_MAIN fallback. In a whitelist that keeps MAIN at the global level
// (defeating the filter); in a blacklist it silences MAIN.

TEST_F(LoggingTest, ApplyLogOnly_UnknownNameIgnoredNotMappedToMain) {
    char tmpl[] = "/tmp/willeq_b850_only_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    ApplyLogOnly("NET,NTE");

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("NTE"), std::string::npos)
        << "rejection must name the bad module; captured stderr: [" << content << "]";
    // NET is the only whitelisted module: it follows the global level.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), -1);
    // MAIN was NOT whitelisted by the typo: it stays suppressed.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), LOG_NONE);
}

TEST_F(LoggingTest, ApplyLogExclude_UnknownNameIgnoredNotSilencingMain) {
    char tmpl[] = "/tmp/willeq_b850_exclude_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    ApplyLogExclude("NET,NTE");

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("NTE"), std::string::npos)
        << "rejection must name the bad module; captured stderr: [" << content << "]";
    // The typo must not have silenced MAIN.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
    // The real token still applied.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_NET), LOG_NONE);
}

// B850 criterion 3: the empty/too-long pair message used to print a stray
// '[' before and ']' after the token. Pin the fixed shape.

TEST_F(LoggingTest, InitLogging_TooLongPairMessageHasNoStrayBrackets) {
    char tmpl[] = "/tmp/willeq_b850_long_XXXXXX";
    int file_fd = mkstemp(tmpl);
    ASSERT_GE(file_fd, 0);
    int saved_stderr = dup(2);
    ASSERT_GE(saved_stderr, 0);
    ASSERT_GE(dup2(file_fd, 2), 0);
    close(file_fd);

    // 96+ chars in one "pair": overflows ApplyLogModulePair's 96-byte spec.
    const std::string longPair = std::string(120, 'X') + ":DEBUG";
    ASSERT_GE(longPair.size(), 96u);
    const std::string flag = "--log-module=" + longPair;
    const char* argv[] = {"program", flag.c_str()};
    int argc = 2;
    InitLogging(argc, const_cast<char**>(argv));

    fflush(stderr);
    std::cerr.flush();
    ASSERT_GE(dup2(saved_stderr, 2), 0);
    close(saved_stderr);

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("too long"), std::string::npos)
        << "the rejection must still fire; captured stderr: [" << content << "]";
    size_t start = content.find("too long");
    std::string after = (start == std::string::npos) ? content : content.substr(start);
    // The pre-fix text was ...": \"[" + token + "]\"" -- the stray '[' right
    // after the opening quote and the ']' right before the closing one.
    EXPECT_EQ(after.find("\"\["), std::string::npos)
        << "stray '[' after the opening quote: [" << after.substr(0, 120) << "]";
    // Nothing may have been applied by the oversized pair.
    EXPECT_EQ(LogManager::Instance().GetModuleLevel(MOD_MAIN), -1);
}

// =============================================================================
// Legacy Compatibility Tests
// =============================================================================

TEST_F(LoggingTest, LegacyDebugLevel_Works) {
    SetDebugLevel(0);
    EXPECT_EQ(GetDebugLevel(), 0);
    EXPECT_FALSE(IsDebugEnabled());

    SetDebugLevel(1);
    EXPECT_EQ(GetDebugLevel(), 1);
    EXPECT_TRUE(IsDebugEnabled());

    SetDebugLevel(3);
    EXPECT_EQ(GetDebugLevel(), 3);
    EXPECT_TRUE(IsDebugEnabled());
}

TEST_F(LoggingTest, TrackedTarget_Works) {
    SetTrackedTargetId(0);
    EXPECT_FALSE(IsTrackedTarget(0));
    EXPECT_FALSE(IsTrackedTarget(123));

    SetTrackedTargetId(123);
    EXPECT_EQ(GetTrackedTargetId(), 123);
    EXPECT_TRUE(IsTrackedTarget(123));
    EXPECT_FALSE(IsTrackedTarget(456));
    EXPECT_FALSE(IsTrackedTarget(0));
}

// =============================================================================
// Timestamp Tests
// =============================================================================

TEST_F(LoggingTest, FormatTimestamp_ProducesValidFormat) {
    char buffer[32];
    FormatTimestamp(buffer, sizeof(buffer));

    // Should produce format like "2025-12-20 10:30:45.123"
    std::string timestamp(buffer);

    // Check length (should be 23 characters: YYYY-MM-DD HH:MM:SS.mmm)
    EXPECT_EQ(timestamp.length(), 23);

    // Check format with regex
    std::regex timestamp_regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})");
    EXPECT_TRUE(std::regex_match(timestamp, timestamp_regex));
}

// =============================================================================
// Module Count Test
// =============================================================================

TEST_F(LoggingTest, ModuleCount_MatchesExpected) {
    // MOD_COUNT should be 22 (the number of defined modules; T410 added
    // MOD_SCENARIO for the --scenario lane's verdict trace)
    EXPECT_EQ(MOD_COUNT, 22);
}

// =============================================================================
// Steady-State Delivery Test (T31)
// =============================================================================
// Pins the defect: ThreadLogBuffer only flipped active/drain sides on
// LOG_FATAL or thread exit, so LOG_INFO/WARN/DEBUG/non-fatal LOG_ERROR
// never reached stdout during normal operation (a long-lived thread that
// never hits a FATAL). A unit test over logging.h's level/gating logic
// alone (all the tests above) does NOT catch this, because those never
// exercise the append -> drain -> stdout path at all. And a standalone
// program that logs a couple of lines and immediately exits doesn't
// catch it either: ThreadLogBuffer's destructor (~ThreadLogBuffer ->
// LogManager::unregisterBuffer) does an unconditional drain regardless
// of the active/inactive flip, so a short-lived repro "passes" by
// catching that exit-time safety net, not steady-state delivery. See
// T31 Review (refactor/T31_logging_flush.md) for the timestamped
// transcript proving this on both the standalone repro and this test
// itself (run against unfixed code, exit code 1; against fixed, exit
// code 0).
//
// This test avoids both traps: it keeps the logging thread ALIVE (no
// exit, no join) for several drain cycles after logging, with no FATAL
// anywhere, and checks that the line reached stdout *during* that
// window -- not after the thread eventually exits.
TEST_F(LoggingTest, SteadyState_NonFatalLogReachesStdoutWithoutFatalOrThreadExit) {
    SetLogLevel(LOG_DEBUG);

    char tmpl[] = "/tmp/willeq_test_logging_steady_state_XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    close(fd);

    // Redirect fd 1 (stdout) to a temp file so we can inspect what the
    // drain thread actually wrote while the logging thread is still
    // alive. Redirecting the raw fd (not just the FILE*/ostream) covers
    // both std::cout (drain thread) and any C stdio use.
    fflush(stdout);
    std::cout.flush();
    int file_fd = open(tmpl, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(file_fd, 0);
    int saved_stdout = dup(1);
    ASSERT_GE(saved_stdout, 0);
    ASSERT_GE(dup2(file_fd, 1), 0);
    close(file_fd);

    std::atomic<bool> keep_alive{true};
    std::thread worker([&keep_alive]() {
        LOG_INFO(MOD_MAIN, "T31_PIN_MARKER steady state line");
        // Stay alive well past several drain cycles WITHOUT exiting or
        // hitting a FATAL -- this is the "app steady state" the wrinkle
        // describes, as opposed to a standalone repro that exits
        // immediately after logging.
        while (keep_alive.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Wait 8 drain cycles (50ms cadence -> 400ms) while the thread is
    // STILL ALIVE, then inspect the file -- before joining/exiting it.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    fflush(stdout);
    std::cout.flush();
    ASSERT_GE(dup2(saved_stdout, 1), 0);
    close(saved_stdout);

    keep_alive.store(false, std::memory_order_relaxed);
    worker.join();

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("T31_PIN_MARKER steady state line"), std::string::npos)
        << "non-FATAL LOG_INFO did not reach stdout within 400ms (8 drain "
           "cycles) while the logging thread was still alive (no FATAL, "
           "no thread exit). Captured stdout during the window: ["
        << content << "]";
}

// =============================================================================
// Quiet-after-burst delivery (T504)
// =============================================================================
// Pins the defect the T504 audit measured: a thread that logs TWO lines
// inside one drain cycle and then goes quiet. The first append flips the
// sides; the second lands on the new active side while the drain side is
// still undrained, so the owner's flip is refused -- and no later append
// ever retries it. The second line then sits in the active side until the
// thread's NEXT log line (measured: a zone's `ready` block arrived 2.5
// minutes late, with the shutdown lines). The T31 test above logs one line
// and cannot see this. Fix: the drain thread flips an idle thread's
// non-empty active side itself (appends now write under sideMutex).
TEST_F(LoggingTest, QuietAfterBurst_SecondLineReachesStdoutWithoutAnotherAppend) {
    SetLogLevel(LOG_DEBUG);

    char tmpl[] = "/tmp/willeq_test_logging_quiet_burst_XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    close(fd);
    fflush(stdout);
    std::cout.flush();
    int file_fd = open(tmpl, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(file_fd, 0);
    int saved_stdout = dup(1);
    ASSERT_GE(saved_stdout, 0);
    ASSERT_GE(dup2(file_fd, 1), 0);
    close(file_fd);

    std::atomic<bool> keep_alive{true};
    std::thread worker([&keep_alive]() {
        // Two lines back to back: the second is appended before the drain
        // thread (50 ms cadence) can have drained the first.
        LOG_INFO(MOD_MAIN, "T504_BURST_MARKER_A first line");
        LOG_INFO(MOD_MAIN, "T504_BURST_MARKER_B second line, then silence");
        while (keep_alive.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    fflush(stdout);
    std::cout.flush();
    ASSERT_GE(dup2(saved_stdout, 1), 0);
    close(saved_stdout);

    keep_alive.store(false, std::memory_order_relaxed);
    worker.join();

    std::ifstream in(tmpl);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    unlink(tmpl);

    EXPECT_NE(content.find("T504_BURST_MARKER_A"), std::string::npos) << content;
    EXPECT_NE(content.find("T504_BURST_MARKER_B"), std::string::npos)
        << "the second line of a burst did not reach stdout within 400ms while "
           "the thread stayed alive and quiet. Captured: [" << content << "]";
}
