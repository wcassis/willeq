#include <gtest/gtest.h>
#include "common/logging.h"
#include <sstream>
#include <regex>

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

TEST_F(LoggingTest, ParseModuleName_Works) {
    EXPECT_EQ(ParseModuleName("NET"), MOD_NET);
    EXPECT_EQ(ParseModuleName("NET_PACKET"), MOD_NET_PACKET);
    EXPECT_EQ(ParseModuleName("LOGIN"), MOD_LOGIN);
    EXPECT_EQ(ParseModuleName("WORLD"), MOD_WORLD);
    EXPECT_EQ(ParseModuleName("ZONE"), MOD_ZONE);
    EXPECT_EQ(ParseModuleName("ENTITY"), MOD_ENTITY);
    EXPECT_EQ(ParseModuleName("MOVEMENT"), MOD_MOVEMENT);
    EXPECT_EQ(ParseModuleName("COMBAT"), MOD_COMBAT);
    EXPECT_EQ(ParseModuleName("INVENTORY"), MOD_INVENTORY);
    EXPECT_EQ(ParseModuleName("GRAPHICS"), MOD_GRAPHICS);
    EXPECT_EQ(ParseModuleName("GRAPHICS_LOAD"), MOD_GRAPHICS_LOAD);
    EXPECT_EQ(ParseModuleName("CAMERA"), MOD_CAMERA);
    EXPECT_EQ(ParseModuleName("INPUT"), MOD_INPUT);
    EXPECT_EQ(ParseModuleName("AUDIO"), MOD_AUDIO);
    EXPECT_EQ(ParseModuleName("PATHFIND"), MOD_PATHFIND);
    EXPECT_EQ(ParseModuleName("MAP"), MOD_MAP);
    EXPECT_EQ(ParseModuleName("UI"), MOD_UI);
    EXPECT_EQ(ParseModuleName("CONFIG"), MOD_CONFIG);
    EXPECT_EQ(ParseModuleName("MAIN"), MOD_MAIN);

    // Unknown should return MOD_MAIN as fallback
    EXPECT_EQ(ParseModuleName("UNKNOWN"), MOD_MAIN);
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
    // MOD_COUNT should be 20 (the number of defined modules)
    EXPECT_EQ(MOD_COUNT, 20);
}
