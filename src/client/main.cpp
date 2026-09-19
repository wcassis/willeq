#include <algorithm>
#include <cstring>
#include <string>
#include <iostream>

#if defined(__linux__)
#include <malloc.h>
#endif

#include "client/application.h"
#include "client/core/memory_ledger.h"
#include "client/eq.h"
#include "client/scenario/scenarios.h"
#include "common/logging.h"

int main(int argc, char* argv[]) {
#if defined(__linux__)
    // T127: glibc gives every thread that contends on the main arena its own
    // fresh arena (up to 8x the CPU count by default). This app spawns a
    // brand-new LoadingThread for every zone crossing to do the bulk of the
    // zone-build allocation (WLD/S3D parse, mesh/texture build -- hundreds of
    // MB of churn per crossing, see renderer_zone_load.cpp), and glibc does
    // not return a thread's arena pages to the OS just because the thread
    // exited and its allocations were freed -- the pages sit in that arena's
    // free lists, unreclaimed, while the NEXT crossing's LoadingThread gets
    // ANOTHER fresh arena. Measured directly (review/T127/): RSS growth over
    // a 6-crossing tour dropped from +297MB (all threads share the default
    // multi-arena policy) to +37MB with a single arena forced -- an 8x
    // reduction, and the single largest lever found for T127's zone-crossing
    // RSS growth, well ahead of the EQAnimatedMeshSceneNode refcount leak
    // fixed alongside it (animated_mesh_creation.cpp). A single shared arena
    // means allocations from different threads serialize on one lock instead
    // of running lock-free in separate arenas -- an acceptable tradeoff here
    // (this is not a many-core allocation-bound app; the constrained-device
    // target is a single-core-class Orange Pi) against an 8x RSS-growth cut
    // on a 512MB machine where that growth is currently fatal. Must run
    // before any other thread is created (Application::run() spawns the
    // game thread, audio threads, worker pool, etc.), so this is the first
    // statement in main().
    mallopt(M_ARENA_MAX, 1);
    // B841 (board): the two threshold experiments, env-gated so a run without
    // the variables is byte-for-byte the shipped behaviour (main.cpp's
    // M_ARENA_MAX is the only mallopt in the default path). Must sit here,
    // before any other thread starts, for the same reason M_ARENA_MAX does.
    {
        const eqt::memory::ArenaExperiments b841 = eqt::memory::arenaExperiments();
        if (b841.mmapThresholdBytes > 0) {
            mallopt(M_MMAP_THRESHOLD, b841.mmapThresholdBytes);
        }
        if (b841.trimThresholdBytes > 0) {
            mallopt(M_TRIM_THRESHOLD, b841.trimThresholdBytes);
        }
    }
#endif

    // T491: everything the main thread allocates before the loops start is
    // process start-up (device, fonts, DB, config). The loops re-scope below.
    eqt::memory::setThreadCategory(eqt::memory::Category::Startup);

    auto config = eqt::Application::parseArguments(argc, argv);

    if (config.showHelp) {
        return 0;  // parseArguments already printed help
    }
    if (config.parseError) {
        return 1;  // parseArguments already printed the fatal message
    }

    // T410 (board B138): validate --scenario before anything else starts.
    // An unknown name is "could not start", exit 3 -- the same code a missing
    // server gets, because in both cases the scenario never ran. Reporting it
    // as a plain usage error (1) would be indistinguishable from a scenario
    // that ran and FAILED, which is the whole distinction this lane exists
    // to make.
    if (!config.scenarioName.empty() && !eqt::scenario::IsScenarioName(config.scenarioName)) {
        std::cerr << "Unknown scenario '" << config.scenarioName << "'. Available:";
        for (const auto& name : eqt::scenario::ScenarioNames()) {
            std::cerr << " " << name;
        }
        std::cerr << "\n";
        return eqt::scenario::kScenarioCouldNotStart;
    }

    // Initialize logging from CLI args (--log-level, --log-module)
    InitLogging(argc, argv);

    // Was --log-level= given explicitly, AND to a value that actually
    // parses? EverQuest::SetDebugLevel() below unconditionally re-syncs the
    // global log level from -d (default 0 when absent), which otherwise
    // clobbers whatever --log-level= just set (deferred.md T31 finding,
    // T56 fix). Precedence: an explicit --log-level= wins over a numeric
    // -d, because silent precedence is what produced this bug in the first
    // place.
    //
    // ParseLevelName() (common/logging.h) has no validation of its own — an
    // unrecognized value silently parses to LOG_NONE, same as a real
    // "--log-level=NONE". Treating that as "explicit" would let a typo like
    // "--log-level=DEGUB" silently beat a correct "-d 3" and produce total
    // silence instead of the -d fallback the pre-T56 code gave for free
    // (review finding, 2026-08-10) — the same silent-precedence bug class
    // this unit exists to fix, just relocated. So validate by round-tripping
    // the parsed value back through GetLevelName() and comparing it
    // (case-sensitively, matching ParseLevelName's own strcmp) against what
    // was actually typed; GetLevelName() pads some names with a trailing
    // space for column alignment ("NONE ", "WARN ", "INFO "), so trim that
    // before comparing. "OFF" is a legitimate alias for NONE that does not
    // round-trip textually, so it is accepted explicitly. An unparseable
    // value leaves logLevelExplicit false (so -d applies exactly as if
    // --log-level= had never been given) and gets a loud WARN instead of
    // silently behaving like "--log-level=NONE". The WARN itself is queued
    // as a string here but only actually logged below, AFTER the final
    // level is resolved — logging it immediately would risk being gated out
    // by the transient LOG_NONE that InitLogging() just set from the bad
    // value (the exact silent-failure shape this whole fix exists to avoid).
    bool logLevelExplicit = false;
    int explicitLogLevel = 0;
    std::string invalidLogLevelValue;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strncmp(arg, "--log-level=", 12) == 0) {
            const char* value = arg + 12;
            LogLevel parsed = ParseLevelName(value);
            std::string roundTripped = GetLevelName(parsed);
            while (!roundTripped.empty() && roundTripped.back() == ' ') {
                roundTripped.pop_back();
            }
            bool validValue = (roundTripped == value) ||
                (parsed == LOG_NONE && strcmp(value, "OFF") == 0);
            if (validValue) {
                logLevelExplicit = true;
                explicitLogLevel = static_cast<int>(parsed);
            } else {
                invalidLogLevelValue = value;
            }
            break;
        }
    }

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

    if (!invalidLogLevelValue.empty()) {
        // Now that the final level is resolved (from -d, or NONE if -d was
        // also absent), this has the best chance of actually reaching
        // stdout instead of being gated out at NONE like an immediate log
        // call right at parse time would have been. Deliberately LOG_ERROR,
        // not LOG_WARN: ShouldLog() (logging.h) always lets FATAL/ERROR
        // through regardless of the current level, but gates WARN and below
        // on it. When --log-level= is invalid AND -d was never given, the
        // resolved level is NONE — a WARN here would be silently swallowed,
        // reproducing exactly the silence this fix exists to prevent (review
        // finding, 2026-08-10). An unrecognized CLI flag being ignored is a
        // user-facing error, not just an advisory, which independently
        // justifies ERROR — but the always-on-at-NONE behavior is why it
        // MUST be ERROR and not WARN. Do not "tidy" this back down to WARN.
        LOG_ERROR(MOD_MAIN,
            "--log-level={} is not a recognized level (accepted: NONE, OFF, FATAL, ERROR, "
            "WARN, INFO, DEBUG, TRACE) — ignoring it, -d takes precedence instead",
            invalidLogLevelValue);
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
        // T410: in the scenario lane an initialize() failure is
        // could-not-start (3), never a scenario failure (1).
        return config.scenarioName.empty() ? 1 : eqt::scenario::kScenarioCouldNotStart;
    }

    app.run();
    app.shutdown();

    // T410: the scenario lane's exit-code taxonomy -- 0 pass, 1 fail,
    // 2 timeout, 3 could-not-start. Read AFTER shutdown() on purpose: the
    // runner outlives the game mode, so a client stopped by anything other
    // than the runner itself (SIGTERM, /quit, an early camp) reports the
    // unfinished-run code rather than a false 0. Every non-scenario run
    // still returns 0 exactly as it did before this existed.
    int exitCode = 0;
    if (app.scenarioRequested()) {
        exitCode = app.scenarioExitCode();
        LOG_INFO(MOD_SCENARIO, "scenario '{}' -> exit {} ({}): {}",
            app.scenarioName(), exitCode,
            eqt::scenario::ScenarioExitCodeName(exitCode), app.scenarioReason());
        std::cerr << "SCENARIO " << app.scenarioName() << " "
                  << eqt::scenario::ScenarioExitCodeName(exitCode)
                  << " exit=" << exitCode << " reason=" << app.scenarioReason()
                  << "\n";
    }
    FlushThreadLog();
    return exitCode;
}
