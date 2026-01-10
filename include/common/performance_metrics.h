#ifndef EQT_PERFORMANCE_METRICS_H
#define EQT_PERFORMANCE_METRICS_H

#include <chrono>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <cstdint>

namespace EQT {

// Categories for organizing metrics
enum class MetricCategory {
    Startup,    // Application startup (config, graphics init, login, world, zone handshake)
    Zoning,     // Zone loading (S3D, WLD, geometry, models, textures)
    Gameplay    // Per-frame metrics (frame time, entity updates, rendering)
};

// Single timing measurement
struct TimingEntry {
    std::string name;
    MetricCategory category;
    int64_t durationMs;
    int64_t startTimeMs;  // Relative to program start
};

// Statistics for repeated measurements (like frame times)
struct TimingStats {
    std::string name;
    int64_t count = 0;
    int64_t totalMs = 0;
    int64_t minMs = INT64_MAX;
    int64_t maxMs = 0;

    double avgMs() const { return count > 0 ? static_cast<double>(totalMs) / count : 0.0; }
};

// RAII timer for automatic measurement
class ScopedTimer {
public:
    ScopedTimer(const std::string& name, MetricCategory category);
    ~ScopedTimer();

    // Disable copy
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string name_;
    MetricCategory category_;
    std::chrono::steady_clock::time_point start_;
};

// Main performance metrics tracker (singleton)
class PerformanceMetrics {
public:
    static PerformanceMetrics& instance();

    // Manual timing API
    void startTimer(const std::string& name, MetricCategory category);
    int64_t stopTimer(const std::string& name);  // Returns duration in ms

    // Record a completed timing
    void recordTiming(const std::string& name, MetricCategory category, int64_t durationMs);

    // Record a sample for statistics (frame times, etc.)
    void recordSample(const std::string& name, int64_t durationMs);

    // Get all timings for a category
    std::vector<TimingEntry> getTimings(MetricCategory category) const;

    // Get statistics for a named metric
    TimingStats getStats(const std::string& name) const;

    // Get total time for a category
    int64_t getCategoryTotalMs(MetricCategory category) const;

    // Reset all metrics
    void reset();

    // Reset just gameplay metrics (for per-session tracking)
    void resetGameplay();

    // Generate summary report
    std::string generateReport() const;

    // Get time since program start
    int64_t getElapsedMs() const;

    // Mark zone load start/end for tracking
    void markZoneLoadStart(const std::string& zoneName);
    void markZoneLoadEnd();

    // Get current zone being loaded (empty if not loading)
    const std::string& getCurrentZoneName() const { return currentZoneName_; }

private:
    PerformanceMetrics();
    ~PerformanceMetrics() = default;

    // Disable copy
    PerformanceMetrics(const PerformanceMetrics&) = delete;
    PerformanceMetrics& operator=(const PerformanceMetrics&) = delete;

    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point programStart_;

    // Active timers (name -> start time)
    std::map<std::string, std::chrono::steady_clock::time_point> activeTimers_;
    std::map<std::string, MetricCategory> timerCategories_;

    // Completed timings
    std::vector<TimingEntry> timings_;

    // Statistics for repeated measurements
    std::map<std::string, TimingStats> stats_;

    // Zone loading tracking
    std::string currentZoneName_;
    std::chrono::steady_clock::time_point zoneLoadStart_;
};

// Convenience macros for timing
#define PERF_TIMER(name, category) \
    EQT::ScopedTimer _perf_timer_##__LINE__(name, category)

#define PERF_TIMER_STARTUP(name) \
    EQT::ScopedTimer _perf_timer_##__LINE__(name, EQT::MetricCategory::Startup)

#define PERF_TIMER_ZONING(name) \
    EQT::ScopedTimer _perf_timer_##__LINE__(name, EQT::MetricCategory::Zoning)

#define PERF_TIMER_GAMEPLAY(name) \
    EQT::ScopedTimer _perf_timer_##__LINE__(name, EQT::MetricCategory::Gameplay)

// Helper to get category name
inline const char* getCategoryName(MetricCategory cat) {
    switch (cat) {
        case MetricCategory::Startup: return "Startup";
        case MetricCategory::Zoning: return "Zoning";
        case MetricCategory::Gameplay: return "Gameplay";
        default: return "Unknown";
    }
}

} // namespace EQT

#endif // EQT_PERFORMANCE_METRICS_H
