#include "common/performance_metrics.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace EQT {

// ============================================================================
// ScopedTimer Implementation
// ============================================================================

ScopedTimer::ScopedTimer(const std::string& name, MetricCategory category)
    : name_(name)
    , category_(category)
    , start_(std::chrono::steady_clock::now())
{
}

ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::steady_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    PerformanceMetrics::instance().recordTiming(name_, category_, durationMs);
}

// ============================================================================
// PerformanceMetrics Implementation
// ============================================================================

PerformanceMetrics& PerformanceMetrics::instance() {
    static PerformanceMetrics instance;
    return instance;
}

PerformanceMetrics::PerformanceMetrics()
    : programStart_(std::chrono::steady_clock::now())
{
}

void PerformanceMetrics::startTimer(const std::string& name, MetricCategory category) {
    std::lock_guard<std::mutex> lock(mutex_);
    activeTimers_[name] = std::chrono::steady_clock::now();
    timerCategories_[name] = category;
}

int64_t PerformanceMetrics::stopTimer(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = activeTimers_.find(name);
    if (it == activeTimers_.end()) {
        return -1;  // Timer not found
    }

    auto end = std::chrono::steady_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - it->second).count();
    auto startMs = std::chrono::duration_cast<std::chrono::milliseconds>(it->second - programStart_).count();

    MetricCategory category = MetricCategory::Startup;
    auto catIt = timerCategories_.find(name);
    if (catIt != timerCategories_.end()) {
        category = catIt->second;
        timerCategories_.erase(catIt);
    }

    TimingEntry entry;
    entry.name = name;
    entry.category = category;
    entry.durationMs = durationMs;
    entry.startTimeMs = startMs;
    timings_.push_back(entry);

    activeTimers_.erase(it);
    return durationMs;
}

void PerformanceMetrics::recordTiming(const std::string& name, MetricCategory category, int64_t durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto startMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - programStart_).count() - durationMs;

    TimingEntry entry;
    entry.name = name;
    entry.category = category;
    entry.durationMs = durationMs;
    entry.startTimeMs = startMs;
    timings_.push_back(entry);
}

void PerformanceMetrics::recordSample(const std::string& name, int64_t durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto& stat = stats_[name];
    stat.name = name;
    stat.count++;
    stat.totalMs += durationMs;
    stat.minMs = std::min(stat.minMs, durationMs);
    stat.maxMs = std::max(stat.maxMs, durationMs);
}

std::vector<TimingEntry> PerformanceMetrics::getTimings(MetricCategory category) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TimingEntry> result;
    for (const auto& entry : timings_) {
        if (entry.category == category) {
            result.push_back(entry);
        }
    }
    return result;
}

TimingStats PerformanceMetrics::getStats(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = stats_.find(name);
    if (it != stats_.end()) {
        return it->second;
    }
    return TimingStats{name, 0, 0, 0, 0};
}

int64_t PerformanceMetrics::getCategoryTotalMs(MetricCategory category) const {
    std::lock_guard<std::mutex> lock(mutex_);

    int64_t total = 0;
    for (const auto& entry : timings_) {
        if (entry.category == category) {
            total += entry.durationMs;
        }
    }
    return total;
}

void PerformanceMetrics::reset() {
    std::lock_guard<std::mutex> lock(mutex_);

    activeTimers_.clear();
    timerCategories_.clear();
    timings_.clear();
    stats_.clear();
    currentZoneName_.clear();
    programStart_ = std::chrono::steady_clock::now();
}

void PerformanceMetrics::resetGameplay() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove gameplay timings
    timings_.erase(
        std::remove_if(timings_.begin(), timings_.end(),
            [](const TimingEntry& e) { return e.category == MetricCategory::Gameplay; }),
        timings_.end());

    // Clear gameplay stats
    stats_.clear();
}

int64_t PerformanceMetrics::getElapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - programStart_).count();
}

void PerformanceMetrics::markZoneLoadStart(const std::string& zoneName) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentZoneName_ = zoneName;
    zoneLoadStart_ = std::chrono::steady_clock::now();
}

void PerformanceMetrics::markZoneLoadEnd() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!currentZoneName_.empty()) {
        auto end = std::chrono::steady_clock::now();
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - zoneLoadStart_).count();
        auto startMs = std::chrono::duration_cast<std::chrono::milliseconds>(zoneLoadStart_ - programStart_).count();

        TimingEntry entry;
        entry.name = "Zone Load: " + currentZoneName_;
        entry.category = MetricCategory::Zoning;
        entry.durationMs = durationMs;
        entry.startTimeMs = startMs;
        timings_.push_back(entry);

        currentZoneName_.clear();
    }
}

std::string PerformanceMetrics::generateReport() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    ss << "\n";
    ss << "================================================================================\n";
    ss << "                         PERFORMANCE METRICS REPORT\n";
    ss << "================================================================================\n";
    ss << "\n";

    // Group timings by category
    std::vector<TimingEntry> startupTimings, zoningTimings, gameplayTimings;
    for (const auto& entry : timings_) {
        switch (entry.category) {
            case MetricCategory::Startup:
                startupTimings.push_back(entry);
                break;
            case MetricCategory::Zoning:
                zoningTimings.push_back(entry);
                break;
            case MetricCategory::Gameplay:
                gameplayTimings.push_back(entry);
                break;
        }
    }

    // Sort each category by start time
    auto sortByStart = [](const TimingEntry& a, const TimingEntry& b) {
        return a.startTimeMs < b.startTimeMs;
    };
    std::sort(startupTimings.begin(), startupTimings.end(), sortByStart);
    std::sort(zoningTimings.begin(), zoningTimings.end(), sortByStart);
    std::sort(gameplayTimings.begin(), gameplayTimings.end(), sortByStart);

    // Startup Section
    ss << "STARTUP TIMINGS\n";
    ss << "--------------------------------------------------------------------------------\n";
    int64_t startupTotal = 0;
    for (const auto& entry : startupTimings) {
        ss << "  " << std::setw(40) << std::left << entry.name
           << std::setw(8) << std::right << entry.durationMs << " ms\n";
        startupTotal += entry.durationMs;
    }
    if (!startupTimings.empty()) {
        ss << "  " << std::setw(40) << std::left << "TOTAL"
           << std::setw(8) << std::right << startupTotal << " ms\n";
    } else {
        ss << "  (no startup timings recorded)\n";
    }
    ss << "\n";

    // Zoning Section
    ss << "ZONING TIMINGS\n";
    ss << "--------------------------------------------------------------------------------\n";
    int64_t zoningTotal = 0;
    for (const auto& entry : zoningTimings) {
        ss << "  " << std::setw(40) << std::left << entry.name
           << std::setw(8) << std::right << entry.durationMs << " ms\n";
        zoningTotal += entry.durationMs;
    }
    if (!zoningTimings.empty()) {
        ss << "  " << std::setw(40) << std::left << "TOTAL"
           << std::setw(8) << std::right << zoningTotal << " ms\n";
    } else {
        ss << "  (no zoning timings recorded)\n";
    }
    ss << "\n";

    // Gameplay Statistics Section
    ss << "GAMEPLAY STATISTICS\n";
    ss << "--------------------------------------------------------------------------------\n";
    if (!stats_.empty()) {
        ss << "  " << std::setw(25) << std::left << "Metric"
           << std::setw(10) << std::right << "Count"
           << std::setw(10) << std::right << "Avg(ms)"
           << std::setw(10) << std::right << "Min(ms)"
           << std::setw(10) << std::right << "Max(ms)" << "\n";
        ss << "  " << std::string(65, '-') << "\n";

        for (const auto& [name, stat] : stats_) {
            ss << "  " << std::setw(25) << std::left << name
               << std::setw(10) << std::right << stat.count
               << std::setw(10) << std::right << stat.avgMs()
               << std::setw(10) << std::right << stat.minMs
               << std::setw(10) << std::right << stat.maxMs << "\n";
        }
    } else {
        ss << "  (no gameplay statistics recorded)\n";
    }
    ss << "\n";

    // Summary
    ss << "SUMMARY\n";
    ss << "--------------------------------------------------------------------------------\n";
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - programStart_).count();
    ss << "  Total elapsed time:    " << elapsed << " ms (" << (elapsed / 1000.0) << " sec)\n";
    ss << "  Startup time:          " << startupTotal << " ms\n";
    ss << "  Zoning time:           " << zoningTotal << " ms\n";

    // Calculate average FPS if we have frame time stats
    auto frameIt = stats_.find("Frame Time");
    if (frameIt != stats_.end() && frameIt->second.avgMs() > 0) {
        double avgFps = 1000.0 / frameIt->second.avgMs();
        ss << "  Average FPS:           " << std::setprecision(1) << avgFps << "\n";
    }

    ss << "================================================================================\n";

    return ss.str();
}

} // namespace EQT
