#pragma once

// WillEQ Logging System
// Standards-compliant logging per .agent/debug_logging_standards.md
//
// Features:
// - Multiple severity levels (NONE through TRACE)
// - Module-based filtering
// - Runtime level configuration
// - Consistent output formatting with timestamps
// - FATAL/ERROR always output at level NONE

#include <iostream>
#include <cstdint>
#include <ctime>
#include <chrono>
#include <array>
#include <mutex>
#include <fmt/format.h>

// =============================================================================
// Log Levels
// =============================================================================
// Levels are hierarchical. Setting a level enables that level and all levels
// above it (lower numeric value). FATAL and ERROR always output at NONE.

enum LogLevel {
    LOG_NONE  = 0,  // Quiet mode - only FATAL/ERROR output
    LOG_FATAL = 1,  // Unrecoverable errors causing termination
    LOG_ERROR = 2,  // Errors preventing normal operation
    LOG_WARN  = 3,  // Unexpected but handled conditions
    LOG_INFO  = 4,  // Significant operational events
    LOG_DEBUG = 5,  // Detailed debugging information
    LOG_TRACE = 6   // Granular execution flow
};

// =============================================================================
// Log Modules
// =============================================================================
// Each logical component has a module identifier for selective filtering.

enum LogModule {
    MOD_NET = 0,        // Network layer (connections, packet send/receive)
    MOD_NET_PACKET,     // Packet parsing and serialization
    MOD_LOGIN,          // Login server communication
    MOD_WORLD,          // World server communication
    MOD_ZONE,           // Zone server communication
    MOD_ENTITY,         // Entity tracking and updates
    MOD_MOVEMENT,       // Player and entity movement
    MOD_COMBAT,         // Combat system
    MOD_SPELL,          // Spell casting system
    MOD_INVENTORY,      // Inventory management
    MOD_GRAPHICS,       // Rendering system
    MOD_GRAPHICS_LOAD,  // Asset loading (models, textures, zones)
    MOD_CAMERA,         // Camera control
    MOD_INPUT,          // User input handling
    MOD_AUDIO,          // Sound system
    MOD_PATHFIND,       // Pathfinding and navigation
    MOD_MAP,            // Zone map and collision
    MOD_UI,             // User interface
    MOD_CONFIG,         // Configuration loading/saving
    MOD_MAIN,           // Main application logic
    MOD_COUNT           // Number of modules (must be last)
};

// Module name strings for output formatting
inline const char* GetModuleName(LogModule mod) {
    static const char* names[] = {
        "NET", "NET_PACKET", "LOGIN", "WORLD", "ZONE",
        "ENTITY", "MOVEMENT", "COMBAT", "SPELL", "INVENTORY",
        "GRAPHICS", "GRAPHICS_LOAD", "CAMERA", "INPUT", "AUDIO",
        "PATHFIND", "MAP", "UI", "CONFIG", "MAIN"
    };
    if (mod >= 0 && mod < MOD_COUNT) {
        return names[mod];
    }
    return "UNKNOWN";
}

// Parse module name from string (for command-line/config)
inline LogModule ParseModuleName(const char* name) {
    static const struct { const char* name; LogModule mod; } mapping[] = {
        {"NET", MOD_NET}, {"NET_PACKET", MOD_NET_PACKET},
        {"LOGIN", MOD_LOGIN}, {"WORLD", MOD_WORLD}, {"ZONE", MOD_ZONE},
        {"ENTITY", MOD_ENTITY}, {"MOVEMENT", MOD_MOVEMENT},
        {"COMBAT", MOD_COMBAT}, {"SPELL", MOD_SPELL}, {"INVENTORY", MOD_INVENTORY},
        {"GRAPHICS", MOD_GRAPHICS}, {"GRAPHICS_LOAD", MOD_GRAPHICS_LOAD},
        {"CAMERA", MOD_CAMERA}, {"INPUT", MOD_INPUT}, {"AUDIO", MOD_AUDIO},
        {"PATHFIND", MOD_PATHFIND}, {"MAP", MOD_MAP}, {"UI", MOD_UI},
        {"CONFIG", MOD_CONFIG}, {"MAIN", MOD_MAIN}
    };
    for (const auto& m : mapping) {
        if (strcmp(name, m.name) == 0) return m.mod;
    }
    return MOD_MAIN; // Default fallback
}

// =============================================================================
// Log Level Names
// =============================================================================

inline const char* GetLevelName(LogLevel level) {
    switch (level) {
        case LOG_NONE:  return "NONE ";
        case LOG_FATAL: return "FATAL";
        case LOG_ERROR: return "ERROR";
        case LOG_WARN:  return "WARN ";
        case LOG_INFO:  return "INFO ";
        case LOG_DEBUG: return "DEBUG";
        case LOG_TRACE: return "TRACE";
        default:        return "?????";
    }
}

// Parse level name from string (for command-line/config)
inline LogLevel ParseLevelName(const char* name) {
    if (strcmp(name, "NONE") == 0 || strcmp(name, "OFF") == 0) return LOG_NONE;
    if (strcmp(name, "FATAL") == 0) return LOG_FATAL;
    if (strcmp(name, "ERROR") == 0) return LOG_ERROR;
    if (strcmp(name, "WARN") == 0)  return LOG_WARN;
    if (strcmp(name, "INFO") == 0)  return LOG_INFO;
    if (strcmp(name, "DEBUG") == 0) return LOG_DEBUG;
    if (strcmp(name, "TRACE") == 0) return LOG_TRACE;
    return LOG_NONE; // Default fallback
}

// =============================================================================
// Logging State Management
// =============================================================================

class LogManager {
public:
    static LogManager& Instance() {
        static LogManager instance;
        return instance;
    }

    // Global log level - affects all modules unless overridden
    int GetGlobalLevel() const { return m_global_level; }
    void SetGlobalLevel(int level) { m_global_level = level; }

    // Per-module log levels (-1 means use global level)
    int GetModuleLevel(LogModule mod) const {
        if (mod >= 0 && mod < MOD_COUNT) {
            return m_module_levels[mod];
        }
        return -1;
    }

    void SetModuleLevel(LogModule mod, int level) {
        if (mod >= 0 && mod < MOD_COUNT) {
            m_module_levels[mod] = level;
        }
    }

    // Check if a log message should be output
    bool ShouldLog(LogModule mod, LogLevel level) const {
        // FATAL and ERROR always output (unless module explicitly OFF)
        if (level <= LOG_ERROR) {
            int mod_level = (mod >= 0 && mod < MOD_COUNT) ? m_module_levels[mod] : -1;
            // Only suppress if module explicitly set to a level below this message
            if (mod_level >= 0 && mod_level < level) {
                return false;
            }
            return true;
        }

        // For other levels, check against effective level
        int effective_level = m_global_level;
        if (mod >= 0 && mod < MOD_COUNT && m_module_levels[mod] >= 0) {
            effective_level = m_module_levels[mod];
        }
        return level <= effective_level;
    }

    // Increase global level (for signal handler)
    void IncreaseLevel() {
        if (m_global_level < LOG_TRACE) {
            m_global_level++;
        }
    }

    // Decrease global level (for signal handler)
    void DecreaseLevel() {
        if (m_global_level > LOG_NONE) {
            m_global_level--;
        }
    }

    // Get mutex for thread-safe output
    std::mutex& GetMutex() { return m_mutex; }

private:
    LogManager() : m_global_level(LOG_NONE) {
        // Initialize all module levels to -1 (use global)
        m_module_levels.fill(-1);
    }

    int m_global_level;
    std::array<int, MOD_COUNT> m_module_levels;
    std::mutex m_mutex;
};

// =============================================================================
// Convenience Functions
// =============================================================================

inline int GetLogLevel() {
    return LogManager::Instance().GetGlobalLevel();
}

inline void SetLogLevel(int level) {
    LogManager::Instance().SetGlobalLevel(level);
}

inline void SetModuleLogLevel(LogModule mod, int level) {
    LogManager::Instance().SetModuleLevel(mod, level);
}

inline bool ShouldLog(LogModule mod, LogLevel level) {
    return LogManager::Instance().ShouldLog(mod, level);
}

// =============================================================================
// Legacy Debug Level Support (for gradual migration)
// =============================================================================

inline int& GetDebugLevel() {
    static int level = 0;
    return level;
}

inline void SetDebugLevel(int level) {
    GetDebugLevel() = level;
}

inline bool IsDebugEnabled() {
    return GetDebugLevel() >= 1;
}

// Tracked target spawn ID for debug output (legacy support)
inline uint16_t& GetTrackedTargetId() {
    static uint16_t targetId = 0;
    return targetId;
}

inline void SetTrackedTargetId(uint16_t spawnId) {
    GetTrackedTargetId() = spawnId;
}

inline bool IsTrackedTarget(uint16_t spawnId) {
    return spawnId != 0 && GetTrackedTargetId() == spawnId;
}

// =============================================================================
// Timestamp Formatting
// =============================================================================

inline void FormatTimestamp(char* buffer, size_t size) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    int written = snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        static_cast<int>(ms.count()));
    (void)written; // Suppress unused warning
}

// =============================================================================
// Core Logging Macro
// =============================================================================

#ifdef EQT_DEBUG

#define EQT_LOG_IMPL(module, level, ...) \
    do { \
        if (ShouldLog(module, level)) { \
            char _ts_buf[32]; \
            FormatTimestamp(_ts_buf, sizeof(_ts_buf)); \
            std::lock_guard<std::mutex> _lock(LogManager::Instance().GetMutex()); \
            try { \
                auto& _out = (level <= LOG_ERROR) ? std::cerr : std::cout; \
                _out << "[" << _ts_buf << "] [" << GetLevelName(level) << "] [" \
                     << GetModuleName(module) << "] " << fmt::format(__VA_ARGS__) << std::endl; \
            } catch (...) { \
                auto& _out = (level <= LOG_ERROR) ? std::cerr : std::cout; \
                _out << "[" << _ts_buf << "] [" << GetLevelName(level) << "] [" \
                     << GetModuleName(module) << "] (format error)" << std::endl; \
            } \
        } \
    } while(0)

// New standard macros - use these for all new code
#define LOG_FATAL(module, ...) EQT_LOG_IMPL(module, LOG_FATAL, __VA_ARGS__)
#define LOG_ERROR(module, ...) EQT_LOG_IMPL(module, LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(module, ...)  EQT_LOG_IMPL(module, LOG_WARN, __VA_ARGS__)
#define LOG_INFO(module, ...)  EQT_LOG_IMPL(module, LOG_INFO, __VA_ARGS__)
#define LOG_DEBUG(module, ...) EQT_LOG_IMPL(module, LOG_DEBUG, __VA_ARGS__)
#define LOG_TRACE(module, ...) EQT_LOG_IMPL(module, LOG_TRACE, __VA_ARGS__)

// Conditional logging - only evaluate if condition is true AND level enabled
#define LOG_DEBUG_IF(module, condition, ...) \
    do { \
        if ((condition) && ShouldLog(module, LOG_DEBUG)) { \
            EQT_LOG_IMPL(module, LOG_DEBUG, __VA_ARGS__); \
        } \
    } while(0)

// Lazy evaluation for expensive computations
#define LOG_DEBUG_LAZY(module, msg, expr) \
    do { \
        if (ShouldLog(module, LOG_DEBUG)) { \
            EQT_LOG_IMPL(module, LOG_DEBUG, "{}{}", msg, (expr)()); \
        } \
    } while(0)

// =============================================================================
// Legacy Macro Compatibility Layer
// =============================================================================
// These maintain backward compatibility during migration. They use MOD_MAIN
// as a default module. Migrate to new LOG_* macros when touching this code.

#define EQT_LOG(level, level_name, ...) \
    do { \
        if (GetLogLevel() >= level) { \
            char _ts_buf[32]; \
            FormatTimestamp(_ts_buf, sizeof(_ts_buf)); \
            std::lock_guard<std::mutex> _lock(LogManager::Instance().GetMutex()); \
            try { \
                std::cout << "[" << _ts_buf << "] [" level_name "] " << fmt::format(__VA_ARGS__) << std::endl; \
            } catch (...) { \
                std::cout << "[" << _ts_buf << "] [" level_name "] (format error)" << std::endl; \
            } \
        } \
    } while(0)

// Legacy macros - use during transition, then migrate to LOG_* versions
#define LogError(...)   EQT_LOG_IMPL(MOD_MAIN, LOG_ERROR, __VA_ARGS__)
#define LogWarning(...) EQT_LOG_IMPL(MOD_MAIN, LOG_WARN, __VA_ARGS__)
#define LogInfo(...)    EQT_LOG_IMPL(MOD_MAIN, LOG_INFO, __VA_ARGS__)
#define LogDebug(...)   EQT_LOG_IMPL(MOD_MAIN, LOG_DEBUG, __VA_ARGS__)
#define LogTrace(...)   EQT_LOG_IMPL(MOD_MAIN, LOG_TRACE, __VA_ARGS__)

// Legacy category macros - migrate to LOG_*(MOD_*, ...) format
#define LogNetClient(...)       EQT_LOG_IMPL(MOD_NET, LOG_DEBUG, __VA_ARGS__)
#define LogNetClientDetail(...) EQT_LOG_IMPL(MOD_NET, LOG_TRACE, __VA_ARGS__)
#define LogPacket(...)          EQT_LOG_IMPL(MOD_NET_PACKET, LOG_TRACE, __VA_ARGS__)
#define LogPathfinding(...)     EQT_LOG_IMPL(MOD_PATHFIND, LOG_DEBUG, __VA_ARGS__)
#define LogCombat(...)          EQT_LOG_IMPL(MOD_COMBAT, LOG_DEBUG, __VA_ARGS__)
#define LogChat(...)            EQT_LOG_IMPL(MOD_MAIN, LOG_DEBUG, __VA_ARGS__)

// Target-specific logging - always prints if spawn_id matches tracked target
#define LogTargetEntity(spawn_id, ...) \
    do { \
        if (IsTrackedTarget(spawn_id) || ShouldLog(MOD_ENTITY, LOG_DEBUG)) { \
            char _ts_buf[32]; \
            FormatTimestamp(_ts_buf, sizeof(_ts_buf)); \
            std::lock_guard<std::mutex> _lock(LogManager::Instance().GetMutex()); \
            try { \
                std::cout << "[" << _ts_buf << "] [DEBUG] [ENTITY:" << spawn_id << "] " \
                          << fmt::format(__VA_ARGS__) << std::endl; \
            } catch (...) { \
                std::cout << "[" << _ts_buf << "] [DEBUG] [ENTITY:" << spawn_id << "] (format error)" << std::endl; \
            } \
        } \
    } while(0)

#else // !EQT_DEBUG

// =============================================================================
// Release Build - Only FATAL/ERROR enabled
// =============================================================================

#define EQT_LOG_IMPL(module, level, ...) \
    do { \
        if (level <= LOG_ERROR && ShouldLog(module, level)) { \
            char _ts_buf[32]; \
            FormatTimestamp(_ts_buf, sizeof(_ts_buf)); \
            std::lock_guard<std::mutex> _lock(LogManager::Instance().GetMutex()); \
            try { \
                std::cerr << "[" << _ts_buf << "] [" << GetLevelName(level) << "] [" \
                          << GetModuleName(module) << "] " << fmt::format(__VA_ARGS__) << std::endl; \
            } catch (...) { \
                std::cerr << "[" << _ts_buf << "] [" << GetLevelName(level) << "] [" \
                          << GetModuleName(module) << "] (format error)" << std::endl; \
            } \
        } \
    } while(0)

// New standard macros - FATAL/ERROR work in release, others are no-ops
#define LOG_FATAL(module, ...) EQT_LOG_IMPL(module, LOG_FATAL, __VA_ARGS__)
#define LOG_ERROR(module, ...) EQT_LOG_IMPL(module, LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(module, ...)  ((void)0)
#define LOG_INFO(module, ...)  ((void)0)
#define LOG_DEBUG(module, ...) ((void)0)
#define LOG_TRACE(module, ...) ((void)0)

#define LOG_DEBUG_IF(module, condition, ...) ((void)0)
#define LOG_DEBUG_LAZY(module, msg, expr)    ((void)0)

// Legacy macros - no-ops in release except Error
#define EQT_LOG(level, level_name, ...) ((void)0)
#define LogError(...)   EQT_LOG_IMPL(MOD_MAIN, LOG_ERROR, __VA_ARGS__)
#define LogWarning(...) ((void)0)
#define LogInfo(...)    ((void)0)
#define LogDebug(...)   ((void)0)
#define LogTrace(...)   ((void)0)

#define LogNetClient(...)       ((void)0)
#define LogNetClientDetail(...) ((void)0)
#define LogPacket(...)          ((void)0)
#define LogPathfinding(...)     ((void)0)
#define LogCombat(...)          ((void)0)
#define LogChat(...)            ((void)0)
#define LogTargetEntity(spawn_id, ...) ((void)0)

#endif // EQT_DEBUG

// =============================================================================
// Signal Handler Support
// =============================================================================
// Call these from signal handlers to adjust log level at runtime:
//   signal(SIGUSR1, [](int) { LogLevelIncrease(); });
//   signal(SIGUSR2, [](int) { LogLevelDecrease(); });

inline void LogLevelIncrease() {
    LogManager::Instance().IncreaseLevel();
}

inline void LogLevelDecrease() {
    LogManager::Instance().DecreaseLevel();
}

// =============================================================================
// Initialization Helper
// =============================================================================
// Call from main() to set up logging from command-line or config

inline void InitLogging(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        // --log-level=LEVEL
        if (strncmp(arg, "--log-level=", 12) == 0) {
            SetLogLevel(ParseLevelName(arg + 12));
        }
        // --log-module=MODULE:LEVEL
        else if (strncmp(arg, "--log-module=", 13) == 0) {
            const char* spec = arg + 13;
            const char* colon = strchr(spec, ':');
            if (colon) {
                char modName[32] = {0};
                size_t len = colon - spec;
                if (len < sizeof(modName)) {
                    strncpy(modName, spec, len);
                    LogModule mod = ParseModuleName(modName);
                    LogLevel level = ParseLevelName(colon + 1);
                    SetModuleLogLevel(mod, level);
                }
            }
        }
    }
}

// =============================================================================
// JSON Config Helper
// =============================================================================
// Include <json/json.h> before using this function
// Parses a "logging" section from config file:
// {
//   "logging": {
//     "level": "DEBUG",
//     "modules": {
//       "NET": "TRACE",
//       "GRAPHICS": "INFO"
//     }
//   }
// }

#ifdef JSONCPP_VERSION_STRING
inline void InitLoggingFromJson(const Json::Value& config) {
    if (!config.isMember("logging")) {
        return;
    }

    const Json::Value& logging = config["logging"];

    // Global level
    if (logging.isMember("level") && logging["level"].isString()) {
        std::string levelStr = logging["level"].asString();
        SetLogLevel(ParseLevelName(levelStr.c_str()));
    }

    // Per-module levels
    if (logging.isMember("modules") && logging["modules"].isObject()) {
        const Json::Value& modules = logging["modules"];
        for (const auto& modName : modules.getMemberNames()) {
            if (modules[modName].isString()) {
                LogModule mod = ParseModuleName(modName.c_str());
                LogLevel level = ParseLevelName(modules[modName].asString().c_str());
                SetModuleLogLevel(mod, level);
            }
        }
    }
}
#endif
