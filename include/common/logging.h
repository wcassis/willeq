#pragma once

// WillEQ Logging System
// Standards-compliant logging per docs/standards/debug_logging_standards.md
//
// Features:
// - Multiple severity levels (NONE through TRACE)
// - Module-based filtering
// - Runtime level configuration
// - Consistent output formatting with timestamps
// - FATAL/ERROR always output at level NONE

#include <algorithm>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <ctime>
#include <chrono>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
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
    MOD_SHADER,         // Shader compilation, uniforms, material swaps
    MOD_CAMERA,         // Camera control
    MOD_INPUT,          // User input handling
    MOD_AUDIO,          // Sound system
    MOD_PATHFIND,       // Pathfinding and navigation
    MOD_MAP,            // Zone map and collision
    MOD_UI,             // User interface
    MOD_CONFIG,         // Configuration loading/saving
    MOD_MAIN,           // Main application logic
    MOD_SCENARIO,       // T410: --scenario lane trace (pinned above --log-level)
    MOD_COUNT           // Number of modules (must be last)
};

// Module name strings for output formatting
inline const char* GetModuleName(LogModule mod) {
    static const char* names[] = {
        "NET", "NET_PACKET", "LOGIN", "WORLD", "ZONE",
        "ENTITY", "MOVEMENT", "COMBAT", "SPELL", "INVENTORY",
        "GRAPHICS", "GRAPHICS_LOAD", "SHADER", "CAMERA", "INPUT", "AUDIO",
        "PATHFIND", "MAP", "UI", "CONFIG", "MAIN", "SCENARIO"
    };
    if (mod >= 0 && mod < MOD_COUNT) {
        return names[mod];
    }
    return "UNKNOWN";
}

// B850: try-parse a module name, reporting whether it is a known module.
// Every site that takes a name from a USER (argv, config file, in-game
// command) must go through this one, or a typo ("NTE" for NET) is
// silently mapped to MAIN -- the same silent-mapping defect class B842
// closed for level names. The pre-B850 ParseModuleName() wrapper carried
// the MOD_MAIN fallback and existed only so those sites could ignore the
// result; once every one of them checks the bool the wrapper had zero
// callers and the ZeroCallers census (CI hygiene stage) refused a merge
// that would have carried a dead fallback around forever. There is no
// "any module will do" caller left, so the fallback is gone with it.
inline bool TryParseModuleName(const char* name, LogModule& out) {
    static const struct { const char* name; LogModule mod; } mapping[] = {
        {"NET", MOD_NET}, {"NET_PACKET", MOD_NET_PACKET},
        {"LOGIN", MOD_LOGIN}, {"WORLD", MOD_WORLD}, {"ZONE", MOD_ZONE},
        {"ENTITY", MOD_ENTITY}, {"MOVEMENT", MOD_MOVEMENT},
        {"COMBAT", MOD_COMBAT}, {"SPELL", MOD_SPELL}, {"INVENTORY", MOD_INVENTORY},
        {"GRAPHICS", MOD_GRAPHICS}, {"GRAPHICS_LOAD", MOD_GRAPHICS_LOAD},
        {"SHADER", MOD_SHADER},
        {"CAMERA", MOD_CAMERA}, {"INPUT", MOD_INPUT}, {"AUDIO", MOD_AUDIO},
        {"PATHFIND", MOD_PATHFIND}, {"MAP", MOD_MAP}, {"UI", MOD_UI},
        {"CONFIG", MOD_CONFIG}, {"MAIN", MOD_MAIN},
        {"SCENARIO", MOD_SCENARIO}
    };
    for (const auto& m : mapping) {
        if (strcmp(name, m.name) == 0) { out = m.mod; return true; }
    }
    return false;
}

// Forward declaration: the rejection messages in ApplyLogOnly/ApplyLogExclude
// and ApplyLogModulePair name the valid set; the definition is below them.
inline std::string GetAllModuleNames();

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

    ~LogManager() {
        stopDrainThread();
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

    // --- Drain thread and buffer registry ---

    // Forward declaration — ThreadLogBuffer registers itself here
    struct BufferEntry {
        std::string* buffers;        // Pointer to the double-buffer array [2] (stdout)
        std::string* errBuffers;     // Pointer to the error double-buffer array [2] (stderr)
        std::atomic<int>* activeIdx; // Pointer to the writer's active index
        // T114: guards the INACTIVE side's std::string content (the side the
        // drain thread reads/clears) against the owning thread's flush(),
        // which also reads that same side's .empty() to decide whether it's
        // safe to flip. TSAN caught this as a real, high-volume data race
        // (owning thread's flush() vs. drainAllBuffers(), both touching
        // buffers[1-old]/errBuffers[1-old] with no happens-before edge --
        // activeIdx's acquire/release only orders the SIDE THAT'S BEING
        // SWITCHED TO, not the peek at the other side). One mutex per
        // ThreadLogBuffer, contended only between that buffer's owning
        // thread and the single shared drain thread -- not a global lock
        // across all threads' buffers.
        std::mutex* sideMutex;
    };

    void registerBuffer(std::string* buffers, std::string* errBuffers,
                        std::atomic<int>* activeIdx, std::mutex* sideMutex) {
        std::lock_guard<std::mutex> lock(m_registry_mutex);
        m_buffers.push_back({buffers, errBuffers, activeIdx, sideMutex});
        ensureDrainThread();
    }

    // B1079 (GitLab #399): the play-box log. The SAME lines the drain thread
    // writes to stdout/stderr also go to `path`, flushed on every drain
    // cycle (~50 ms cadence) so the file holds what the player saw even when
    // the process dies mid-zone-in -- the 2026-10-15 defect: the XU4's
    // play.sh teed stdout today, and a killed process lost its log.
    //
    // stdout/stderr still carry everything (an unwritable path is not a
    // stdout silencer); the file is ADDITIVE. An unwritable path logs one
    // stderr line and continues, never a crash at startup.
    bool SetLogFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_file_mutex);
        m_log_file.open(path, std::ios::out | std::ios::app);
        if (!m_log_file.is_open()) {
            std::cerr << "[logging] Failed to open --log-file path " << path
                      << " -- continuing without the file\n";
            return false;
        }
        m_log_file_path = path;
        return true;
    }

    const std::string& LogFilePath() const { return m_log_file_path; }

    void unregisterBuffer(std::string* buffers) {
        std::lock_guard<std::mutex> lock(m_registry_mutex);
        // Drain any remaining content before removing — this runs under
        // m_registry_mutex, the same lock the drain thread holds when
        // writing to stdout/stderr, so no lock ordering issue.
        bool wroteOut = false, wroteErr = false;
        std::string cycleOut, cycleErr;  // B1079: same lines to the file sink
        // Find the entry to get errBuffers pointer
        std::string* errBufs = nullptr;
        for (const auto& e : m_buffers) {
            if (e.buffers == buffers) { errBufs = e.errBuffers; break; }
        }
        for (int i = 0; i < 2; ++i) {
            if (!buffers[i].empty()) {
                std::cout << buffers[i];
                cycleOut += buffers[i];
                buffers[i].clear();
                wroteOut = true;
            }
            if (errBufs && !errBufs[i].empty()) {
                std::cerr << errBufs[i];
                cycleErr += errBufs[i];
                errBufs[i].clear();
                wroteErr = true;
            }
        }
        if (wroteOut) std::cout.flush();
        if (wroteErr) std::cerr.flush();
        writeLogFile(cycleOut, cycleErr);
        m_buffers.erase(
            std::remove_if(m_buffers.begin(), m_buffers.end(),
                [buffers](const BufferEntry& e) { return e.buffers == buffers; }),
            m_buffers.end());
    }

private:
    LogManager() : m_global_level(LOG_NONE) {
        m_module_levels.fill(-1);
    }

    void ensureDrainThread() {
        if (m_drain_running.load(std::memory_order_relaxed)) return;
        m_drain_running.store(true, std::memory_order_release);
        m_drain_thread = std::thread(&LogManager::drainLoop, this);
    }

    void stopDrainThread() {
        if (!m_drain_running.load(std::memory_order_relaxed)) return;
        m_drain_running.store(false, std::memory_order_release);
        if (m_drain_thread.joinable()) m_drain_thread.join();
        // Final drain
        drainAllBuffers();
        // Close the file AFTER the join: the drain thread is the only other
        // writer, so once it is gone no flush can race the close.
        {
            std::lock_guard<std::mutex> lock(m_file_mutex);
            if (m_log_file.is_open()) m_log_file.close();
        }
    }

    void drainLoop() {
        try {
            while (m_drain_running.load(std::memory_order_relaxed)) {
                drainAllBuffers();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] Log drain thread crashed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[FATAL] Log drain thread crashed with unknown exception" << std::endl;
        }
    }

    void drainAllBuffers() {
        bool wroteOut = false, wroteErr = false;
        // B1079: accumulates the exact text written to stdout/stderr this
        // cycle so the --log-file gets the same lines (the buffers are
        // thread-local std::strings that get cleared after the console
        // write, so the copy happens before the clear).
        std::string cycleOut, cycleErr;
        {
        std::lock_guard<std::mutex> lock(m_registry_mutex);
        for (auto& entry : m_buffers) {
            // T114: entry.sideMutex serializes this read-index +
            // touch-inactive-side sequence against the owning thread's
            // flush() (which does the symmetric check-inactive-side +
            // flip under the SAME mutex) -- see BufferEntry::sideMutex's
            // comment. Nested inside m_registry_mutex (already held for
            // this whole loop); always acquired in this order (registry
            // then per-entry), never the reverse, so no new deadlock risk.
            std::lock_guard<std::mutex> sideLock(*entry.sideMutex);
            // Read the active index — the writer is using this side.
            // We read the OTHER side (the one the writer flipped away from).
            int active = entry.activeIdx->load(std::memory_order_relaxed);
            int drainSide = 1 - active;
            // T504: a thread that logs a burst and then goes QUIET never
            // flips again (the flip only ran from its own append), so its
            // last lines sat in the active side until its next log line --
            // measured: a zone's `ready` block arrived 2.5 minutes late,
            // with the shutdown lines. Under sideMutex the owner cannot be
            // mid-append, so when the drain side is empty and the active
            // side is not, flip it here and drain what the owner wrote.
            if (entry.buffers[drainSide].empty() && entry.errBuffers[drainSide].empty()
                && (!entry.buffers[active].empty() || !entry.errBuffers[active].empty())) {
                entry.activeIdx->store(drainSide, std::memory_order_relaxed);
                drainSide = active;
            }
            std::string& drainBuf = entry.buffers[drainSide];
            if (!drainBuf.empty()) {
                std::cout << drainBuf;
                cycleOut += drainBuf;
                drainBuf.clear();
                wroteOut = true;
            }
            std::string& drainErrBuf = entry.errBuffers[drainSide];
            if (!drainErrBuf.empty()) {
                std::cerr << drainErrBuf;
                cycleErr += drainErrBuf;
                drainErrBuf.clear();
                wroteErr = true;
            }
        }
        }
        if (wroteOut) std::cout.flush();
        if (wroteErr) std::cerr.flush();
        // B1079: the same lines to the --log-file sink, flushed too (a killed
        // process must not lose the tail). Written only here and in
        // unregisterBuffer; m_file_mutex guards against SetLogFile/stopDrain.
        writeLogFile(cycleOut, cycleErr);
    }

    // B1079: one flush of the two drained buffers to the --log-file. Empty
    // buffers are a no-op. `out`/`err` are the exact strings written to
    // stdout/stderr (possibly empty).
    void writeLogFile(const std::string& out, const std::string& err) {
        std::lock_guard<std::mutex> lock(m_file_mutex);
        if (!m_log_file.is_open()) return;
        if (!out.empty()) m_log_file << out;
        if (!err.empty()) m_log_file << err;
        if (!out.empty() || !err.empty()) m_log_file.flush();
    }

    int m_global_level;
    std::array<int, MOD_COUNT> m_module_levels;

    // Buffer registry
    std::mutex m_registry_mutex;
    std::vector<BufferEntry> m_buffers;

    // B1079: the --log-file sink (see SetLogFile). Written only by the drain
    // thread; m_file_mutex serializes open/close (main thread, at startup)
    // against the drain writes.
    std::mutex m_file_mutex;
    std::ofstream m_log_file;
    std::string m_log_file_path;

    // Drain thread
    std::thread m_drain_thread;
    std::atomic<bool> m_drain_running{false};
};

// =============================================================================
// Thread-Local Log Buffer — Lock-Free Double-Buffered Logging
// =============================================================================
// Each thread appends to its active buffer. FlushThreadLog() flips the active
// index (one atomic store). A dedicated drain thread in LogManager reads the
// inactive buffer and writes to stdout. No mutex or I/O on the calling thread.
// FATAL/ERROR bypass the buffer for immediate visibility.

struct ThreadLogBuffer {
    std::string buffers[2];
    std::string errBuffers[2];  // Separate buffer for ERROR/FATAL → stderr
    std::atomic<int> activeIdx{0};
    bool registered = false;
    // T114: see LogManager::BufferEntry::sideMutex's comment -- guards the
    // inactive-side peek/flip here in flush() against LogManager's drain
    // thread reading/clearing the same side in drainAllBuffers(). One
    // instance per ThreadLogBuffer (thread_local, one per logging thread),
    // so contention is only ever between this buffer's owning thread and
    // the single shared drain thread, never across unrelated threads.
    std::mutex sideMutex;

    ThreadLogBuffer() = default;

    ~ThreadLogBuffer() {
        if (registered) {
            LogManager::Instance().unregisterBuffer(buffers);
        }
    }

    void ensureRegistered() {
        if (!registered) {
            registered = true;
            LogManager::Instance().registerBuffer(buffers, errBuffers, &activeIdx, &sideMutex);
        }
    }

    std::string& active() { return buffers[activeIdx.load(std::memory_order_relaxed)]; }
    std::string& activeErr() { return errBuffers[activeIdx.load(std::memory_order_relaxed)]; }

    void append(const char* ts, const char* level, const char* module,
                const std::string& msg) {
        ensureRegistered();
        // T504: the write itself is under sideMutex so the drain thread may
        // flip an IDLE thread's sides (see drainAllBuffers).
        std::lock_guard<std::mutex> writeLock(sideMutex);
        auto& data = active();
        data += '[';
        data += ts;
        data += "] [";
        data += level;
        data += "] [";
        data += module;
        data += "] ";
        data += msg;
        data += '\n';
        // Attempt a self-flip so the drain thread picks this line up within
        // one drain cycle. Under sideMutex already (writeLock).
        flushLocked();
    }

    // ERROR/FATAL: appends to stderr buffer (drained to stderr by drain thread)
    void appendError(const char* ts, const char* level, const char* module,
                     const std::string& msg) {
        ensureRegistered();
        // T504: the write itself is under sideMutex so the drain thread may
        // flip an IDLE thread's sides (see drainAllBuffers).
        std::lock_guard<std::mutex> writeLock(sideMutex);
        auto& data = activeErr();
        data += '[';
        data += ts;
        data += "] [";
        data += level;
        data += "] [";
        data += module;
        data += "] ";
        data += msg;
        data += '\n';
        flushLocked();
    }

    void appendEntity(const char* ts, uint16_t spawn_id,
                      const std::string& msg) {
        ensureRegistered();
        // T504: the write itself is under sideMutex so the drain thread may
        // flip an IDLE thread's sides (see drainAllBuffers).
        std::lock_guard<std::mutex> writeLock(sideMutex);
        auto& data = active();
        data += '[';
        data += ts;
        data += "] [DEBUG] [ENTITY:";
        data += std::to_string(spawn_id);
        data += "] ";
        data += msg;
        data += '\n';
        flushLocked();
    }

    // Self-flip the active/inactive sides. Called by THIS buffer's owning
    // thread only (from append()/appendError()/appendEntity() after every
    // write, and from FlushThreadLog() for LOG_FATAL) — never from the
    // drain thread, which only ever reads the inactive side. Because a
    // given ThreadLogBuffer is thread_local, "owning thread" and "the
    // thread calling flush()" are always the same thread, so there is no
    // concurrent append() in flight when this runs: no lock is needed.
    //
    // Cost: two atomic loads/stores (relaxed/acquire/release, no mutex)
    // plus up to four std::string::empty() checks (O(1) — a size
    // comparison, not a scan). No I/O happens here; the drain thread
    // still owns all std::cout/std::cerr writes on its own 50ms cadence.
    // At the default log level (NONE), ShouldLog() already gates
    // LOG_WARN/INFO/DEBUG/TRACE before append() is ever reached, so this
    // function isn't even called for those levels in production — the
    // hot path is unchanged from before this fix. See T31 Review for the
    // measured cost at a raised level.
    void flush() {
        std::lock_guard<std::mutex> lock(sideMutex);
        flushLocked();
    }

    // The flip itself; sideMutex must be held by the caller.
    void flushLocked() {
        int old = activeIdx.load(std::memory_order_relaxed);
        if (active().empty() && activeErr().empty()) return;
        // T114: sideMutex guards the peek at buffers[1-old]/errBuffers[1-old]
        // (the drain thread's side) and the flip together -- TSAN caught
        // this unlocked peek racing against drainAllBuffers()'s read+clear
        // of the exact same strings (see BufferEntry::sideMutex's comment).
        // Only this critical section is locked; active()/activeErr() above
        // (this thread's OWN side) and the actual data += appends in
        // append()/appendError()/appendEntity() stay lock-free, since only
        // this (owning) thread ever touches the active side.
        // Only flip if the drain side has been drained (is empty).
        // If it still has content, skip — prevents the writer from
        // flipping back to the side the drain thread is reading.
        if (!buffers[1 - old].empty() || !errBuffers[1 - old].empty()) return;
        activeIdx.store(1 - old, std::memory_order_relaxed);
    }
};

inline ThreadLogBuffer& GetThreadLogBuffer() {
    thread_local ThreadLogBuffer buf;
    return buf;
}

inline void FlushThreadLog() {
    GetThreadLogBuffer().flush();
}

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

// Apply whitelist filter: set all modules NOT in the comma-separated list to LOG_NONE.
// Modules in the list get reset to -1 (use global level).
// B850: a name the mapping does not know is no longer silently mapped to
// MAIN (which, in a whitelist, would keep MAIN at the global level and
// defeat the filter) -- it is skipped and named on one stderr line.
inline void ApplyLogOnly(const char* modules) {
    // First, set all modules to LOG_NONE
    for (int i = 0; i < MOD_COUNT; i++) {
        SetModuleLogLevel(static_cast<LogModule>(i), LOG_NONE);
    }
    // Then, reset listed modules to use global level
    std::string unknown;
    const char* p = modules;
    while (*p) {
        // Skip leading whitespace and commas
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        // Find end of module name
        const char* start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        char modName[32] = {0};
        size_t len = p - start;
        if (len > 0 && len < sizeof(modName)) {
            strncpy(modName, start, len);
            // Convert to uppercase for matching
            for (size_t i = 0; i < len; i++) {
                if (modName[i] >= 'a' && modName[i] <= 'z')
                    modName[i] -= 32;
            }
            LogModule mod;
            if (TryParseModuleName(modName, mod)) {
                SetModuleLogLevel(mod, -1);  // Use global level
            } else {
                if (!unknown.empty()) unknown += ", ";
                unknown += modName;
            }
        }
    }
    if (!unknown.empty()) {
        std::cerr << "[logging] --log-only: unknown module name(s) ignored: "
                  << unknown << " (valid: " << GetAllModuleNames() << ")\n";
    }
}

// Apply blacklist filter: set listed modules to LOG_NONE.
// B850: unknown names were silently mapped to MAIN (silencing it) -- now
// skipped and named on one stderr line.
inline void ApplyLogExclude(const char* modules) {
    std::string unknown;
    const char* p = modules;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        char modName[32] = {0};
        size_t len = p - start;
        if (len > 0 && len < sizeof(modName)) {
            strncpy(modName, start, len);
            for (size_t i = 0; i < len; i++) {
                if (modName[i] >= 'a' && modName[i] <= 'z')
                    modName[i] -= 32;
            }
            LogModule mod;
            if (TryParseModuleName(modName, mod)) {
                SetModuleLogLevel(mod, LOG_NONE);
            } else {
                if (!unknown.empty()) unknown += ", ";
                unknown += modName;
            }
        }
    }
    if (!unknown.empty()) {
        std::cerr << "[logging] --log-exclude: unknown module name(s) ignored: "
                  << unknown << " (valid: " << GetAllModuleNames() << ")\n";
    }
}

// Clear all module filters (reset all to use global level).
inline void ClearModuleFilters() {
    for (int i = 0; i < MOD_COUNT; i++) {
        SetModuleLogLevel(static_cast<LogModule>(i), -1);
    }
}

// Get a comma-separated string of all module names.
inline std::string GetAllModuleNames() {
    std::string result;
    for (int i = 0; i < MOD_COUNT; i++) {
        if (i > 0) result += ", ";
        result += GetModuleName(static_cast<LogModule>(i));
    }
    return result;
}

// Get a summary of current module filter state.
inline std::string GetModuleFilterStatus() {
    std::string active, suppressed;
    int globalLevel = GetLogLevel();
    for (int i = 0; i < MOD_COUNT; i++) {
        int modLevel = LogManager::Instance().GetModuleLevel(static_cast<LogModule>(i));
        const char* name = GetModuleName(static_cast<LogModule>(i));
        if (modLevel == LOG_NONE) {
            if (!suppressed.empty()) suppressed += ", ";
            suppressed += name;
        } else if (modLevel > 0) {
            if (!active.empty()) active += ", ";
            active += name;
            active += ":";
            active += GetLevelName(static_cast<LogLevel>(modLevel));
        }
    }
    std::string result = "Global level: ";
    result += GetLevelName(static_cast<LogLevel>(globalLevel));
    if (!active.empty()) {
        result += " | Active overrides: ";
        result += active;
    }
    if (!suppressed.empty()) {
        result += " | Suppressed: ";
        result += suppressed;
    }
    return result;
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
    // Sync LogManager level: maps debug level 0-6 to log levels
    // 0=NONE, 1=FATAL, 2=ERROR, 3=WARN, 4=INFO, 5=DEBUG, 6=TRACE
    int logLevel = std::min(level, static_cast<int>(LOG_TRACE));
    LogManager::Instance().SetGlobalLevel(logLevel);
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

    // Formatted into a local first: the callers pass a 32-byte buffer and GCC's
    // -Wformat-truncation (an error in CI) sizes the worst case of seven ints
    // at 77 bytes. The real output is 23 characters; the copy below is bounded.
    char tmp[80];
    int written = snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
        static_cast<int>(ms.count()));
    if (written < 0) written = 0;
    const size_t n = std::min(static_cast<size_t>(written), size > 0 ? size - 1 : 0);
    if (size > 0) { memcpy(buffer, tmp, n); buffer[n] = '\0'; }
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
            if (level <= LOG_ERROR) { \
                try { \
                    GetThreadLogBuffer().appendError(_ts_buf, GetLevelName(level), \
                        GetModuleName(module), fmt::format(__VA_ARGS__)); \
                } catch (...) { \
                    GetThreadLogBuffer().appendError(_ts_buf, GetLevelName(level), \
                        GetModuleName(module), "(format error)"); \
                } \
            } else { \
                try { \
                    GetThreadLogBuffer().append(_ts_buf, GetLevelName(level), \
                        GetModuleName(module), fmt::format(__VA_ARGS__)); \
                } catch (...) { \
                    GetThreadLogBuffer().append(_ts_buf, GetLevelName(level), \
                        GetModuleName(module), "(format error)"); \
                } \
            } \
        } \
    } while(0)

// New standard macros - use these for all new code
// FATAL forces an immediate buffer flip so the drain thread picks it up ASAP
#define LOG_FATAL(module, ...) do { EQT_LOG_IMPL(module, LOG_FATAL, __VA_ARGS__); FlushThreadLog(); } while(0)
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
            if (level <= LOG_ERROR) { \
                try { \
                    GetThreadLogBuffer().appendError(_ts_buf, level_name, \
                        "MAIN", fmt::format(__VA_ARGS__)); \
                } catch (...) { \
                    GetThreadLogBuffer().appendError(_ts_buf, level_name, \
                        "MAIN", "(format error)"); \
                } \
            } else { \
                try { \
                    GetThreadLogBuffer().append(_ts_buf, level_name, \
                        "MAIN", fmt::format(__VA_ARGS__)); \
                } catch (...) { \
                    GetThreadLogBuffer().append(_ts_buf, level_name, \
                        "MAIN", "(format error)"); \
                } \
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
            try { \
                GetThreadLogBuffer().appendEntity(_ts_buf, spawn_id, \
                    fmt::format(__VA_ARGS__)); \
            } catch (...) { \
                GetThreadLogBuffer().appendEntity(_ts_buf, spawn_id, \
                    "(format error)"); \
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
            try { \
                GetThreadLogBuffer().appendError(_ts_buf, GetLevelName(level), \
                    GetModuleName(module), fmt::format(__VA_ARGS__)); \
            } catch (...) { \
                GetThreadLogBuffer().appendError(_ts_buf, GetLevelName(level), \
                    GetModuleName(module), "(format error)"); \
            } \
        } \
    } while(0)

// New standard macros - FATAL/ERROR work in release, others are no-ops
#define LOG_FATAL(module, ...) do { EQT_LOG_IMPL(module, LOG_FATAL, __VA_ARGS__); FlushThreadLog(); } while(0)
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

// B842: parse one --log-module= pair ("MOD:LEVEL"). Returns false and leaves
// the module level untouched when the level name does not parse. The
// pre-B842 code split the value on the FIRST ':' and handed the remainder to
// ParseLevelName(), which falls back to LOG_NONE on garbage -- so a typo or
// a comma-separated list (both "legitimate" looking inputs) silently
// SILENCED the module instead of being reported (T500's Pi run: an
// otherwise perfect exit 0 with zero ledger lines). Rejection is loud: the
// bad token is printed to stderr and the module keeps "use global level".
// (An exit() here would be wrong: InitLogging() is a plain inline in a
// shared header, and the caller keeps the rest of the flag working.)
inline bool ApplyLogModulePair(const char* pair, size_t len) {
    // Copy to a scratch buffer so we can NUL-terminate at the ':' and scan
    // it without mutating the (possibly read-only) argv.
    char spec[96] = {0};
    if (len == 0 || len >= sizeof(spec)) {
        // B850: the pre-fix message printed a stray '[' before and ']' after
        // the token ("\"[NTE:DEBUG]\"") -- a leftover of some older framing.
        const std::string preview(pair, len > 0 && len < sizeof(spec) ? len : 80);
        std::cerr << "[logging] ignoring --log-module pair (empty or too long): \""
                  << preview << (len > 80 ? "..." : "") << "\"\n";
        return false;
    }
    std::copy_n(pair, len, spec);

    char* colon = strchr(spec, ':');
    if (!colon) {
        std::cerr << "[logging] ignoring --log-module pair without 'MOD:LEVEL' form: \""
                  << spec << "\"\n";
        return false;
    }
    *colon = '\0';
    // B850: a USER-TYPED module name goes through the try-parse, not
    // ParseModuleName()'s MOD_MAIN fallback -- "NTE:DEBUG" (a typo of NET)
    // used to set MAIN to DEBUG and say nothing. Same silent-mapping class
    // as B842's level names.
    LogModule mod;
    if (!TryParseModuleName(spec, mod)) {
        std::cerr << "[logging] ignoring --log-module pair with unknown module "
                     "name '" << spec << "' in \"" << spec << ":" << (colon + 1)
                  << "\" (valid: " << GetAllModuleNames() << ")\n";
        return false;
    }
    std::string levelStr(colon + 1);
    // Round-trip check (main.cpp uses the same pattern for --log-level=):
    // GetLevelName() pads some names for column alignment ("NONE ", "WARN ",
    // "INFO "), so trim before comparing. "OFF" is a legitimate alias for
    // NONE that does not round-trip textually.
    const char* levelName = GetLevelName(ParseLevelName(levelStr.c_str()));
    std::string canonical(levelName);
    while (!canonical.empty() && canonical.back() == ' ') canonical.pop_back();
    if (levelStr != canonical && levelStr != "OFF") {
        std::cerr << "[logging] ignoring --log-module pair with unparseable level "
                     "name '" << levelStr << "' in \"" << spec << ":" << levelStr
                  << "\" (valid: NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE)\n";
        return false;
    }
    SetModuleLogLevel(mod, ParseLevelName(levelStr.c_str()));
    return true;
}

inline void InitLogging(int argc, char* argv[]) {
    // Two-pass parsing: first apply level/only/exclude, then per-module overrides
    const char* logOnly = nullptr;
    const char* logExclude = nullptr;

    // Pass 1: collect level, only, exclude
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strncmp(arg, "--log-level=", 12) == 0) {
            SetLogLevel(ParseLevelName(arg + 12));
        }
        else if (strncmp(arg, "--log-only=", 11) == 0) {
            logOnly = arg + 11;
        }
        else if (strncmp(arg, "--log-exclude=", 14) == 0) {
            logExclude = arg + 14;
        }
    }

    // Apply only/exclude (only takes precedence if both specified)
    if (logOnly) {
        ApplyLogOnly(logOnly);
    } else if (logExclude) {
        ApplyLogExclude(logExclude);
    }

    // Pass 2: apply per-module overrides (these win over only/exclude).
    // B842: the value is a COMMA-SEPARATED list of MOD:LEVEL pairs, so one
    // flag can set several modules at once. A bad pair is rejected (named on
    // stderr, module untouched) without poisoning the pairs around it.
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strncmp(arg, "--log-module=", 13) == 0) {
            const char* spec = arg + 13;
            const char* pairStart = spec;
            for (const char* p = spec;; ++p) {
                if (*p == ',' || *p == '\0') {
                    ApplyLogModulePair(pairStart, p - pairStart);
                    if (*p == '\0') break;
                    pairStart = p + 1;
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

    // Whitelist/blacklist filters (only takes precedence over exclude)
    if (logging.isMember("only") && logging["only"].isArray()) {
        std::string modules;
        for (const auto& mod : logging["only"]) {
            if (mod.isString()) {
                if (!modules.empty()) modules += ",";
                modules += mod.asString();
            }
        }
        if (!modules.empty()) {
            ApplyLogOnly(modules.c_str());
        }
    } else if (logging.isMember("exclude") && logging["exclude"].isArray()) {
        std::string modules;
        for (const auto& mod : logging["exclude"]) {
            if (mod.isString()) {
                if (!modules.empty()) modules += ",";
                modules += mod.asString();
            }
        }
        if (!modules.empty()) {
            ApplyLogExclude(modules.c_str());
        }
    }

    // Per-module levels (override only/exclude). B850: an unknown key was
    // silently mapped to MAIN -- now it is skipped and named on one stderr
    // line (config files are typed by the user, same as argv).
    if (logging.isMember("modules") && logging["modules"].isObject()) {
        const Json::Value& modules = logging["modules"];
        std::string unknown;
        for (const auto& modName : modules.getMemberNames()) {
            if (!modules[modName].isString()) continue;
            LogModule mod;
            if (TryParseModuleName(modName.c_str(), mod)) {
                SetModuleLogLevel(mod, ParseLevelName(modules[modName].asString().c_str()));
            } else {
                if (!unknown.empty()) unknown += ", ";
                unknown += modName;
            }
        }
        if (!unknown.empty()) {
            std::cerr << "[logging] config logging.modules: unknown module name(s) "
                      << "ignored: " << unknown << " (valid: " << GetAllModuleNames() << ")\n";
        }
    }
}
#endif
