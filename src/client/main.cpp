#include "client/application.h"
#include "client/eq.h"
#include "common/logging.h"

int main(int argc, char* argv[]) {
    auto config = eqt::Application::parseArguments(argc, argv);

    if (config.showHelp) {
        return 0;  // parseArguments already printed help
    }

    // Initialize logging from CLI args (--log-level, --log-module)
    InitLogging(argc, argv);

    // Set log level from -d flag
    EverQuest::SetDebugLevel(config.debugLevel);
    if (config.debugLevel > 0) {
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
