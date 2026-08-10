#include <algorithm>
#include <cstring>

#include "client/application.h"
#include "client/eq.h"
#include "common/logging.h"

int main(int argc, char* argv[]) {
    auto config = eqt::Application::parseArguments(argc, argv);

    if (config.showHelp) {
        return 0;  // parseArguments already printed help
    }
    if (config.parseError) {
        return 1;  // parseArguments already printed the fatal message
    }

    // Initialize logging from CLI args (--log-level, --log-module)
    InitLogging(argc, argv);

    // Was --log-level= given explicitly? EverQuest::SetDebugLevel() below
    // unconditionally re-syncs the global log level from -d (default 0 when
    // absent), which otherwise clobbers whatever --log-level= just set
    // (deferred.md T31 finding, T56 fix). Precedence: an explicit
    // --log-level= wins over a numeric -d, because silent precedence is what
    // produced this bug in the first place. Capture the level InitLogging()
    // just applied so it can be restored after SetDebugLevel() below.
    bool logLevelExplicit = false;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--log-level=", 12) == 0) {
            logLevelExplicit = true;
            break;
        }
    }
    const int explicitLogLevel = GetLogLevel();

    // Set log level from -d flag
    EverQuest::SetDebugLevel(config.debugLevel);
    if (logLevelExplicit) {
        // Restore the explicit --log-level=, overriding whatever
        // SetDebugLevel() just derived from -d (0 if -d was not given).
        SetLogLevel(explicitLogLevel);
        const int debugDerivedLevel = std::min(config.debugLevel, static_cast<int>(LOG_TRACE));
        if (config.debugLevel > 0 && debugDerivedLevel != explicitLogLevel) {
            LOG_INFO(MOD_MAIN,
                "--log-level={} and -d {} disagree; --log-level= wins (effective level: {})",
                GetLevelName(static_cast<LogLevel>(explicitLogLevel)), config.debugLevel,
                GetLevelName(static_cast<LogLevel>(explicitLogLevel)));
        }
    } else if (config.debugLevel > 0) {
        SetLogLevel(config.debugLevel);
    }

    LOG_INFO(MOD_MAIN, "Starting WillEQ with debug level {}, config file: {}, pathfinding: {}",
        config.debugLevel, config.configFile, config.pathfindingEnabled ? "enabled" : "disabled");
    LOG_INFO(MOD_MAIN, "Log level: {} (use --log-level=LEVEL to change)",
        GetLevelName(static_cast<LogLevel>(GetLogLevel())));

    // Load config file
    LOG_INFO(MOD_MAIN, "Loading config file: {}", config.configFile);
    if (!eqt::Application::loadConfigFile(config.configFile, config)) {
        LOG_FATAL(MOD_MAIN, "Failed to load config file: {}", config.configFile);
        return 1;
    }

    // Create and run application
    eqt::Application app;
    if (!app.initialize(config)) {
        LOG_FATAL(MOD_MAIN, "Failed to initialize application");
        return 1;
    }

    app.run();
    app.shutdown();
    FlushThreadLog();
    return 0;
}
