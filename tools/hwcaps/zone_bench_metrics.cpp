#include "zone_bench_metrics.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace ZoneBench {

void MetricsCollector::writeCSV(const std::string& filename) const {
    FILE* f = fopen(filename.c_str(), "w");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open CSV file: %s\n", filename.c_str());
        return;
    }

    // P01 experiment 2: alpha_test_draws/solid_draws are trailing columns,
    // always present (0 when --material-counts was not passed) so existing
    // consumers keyed by column name (csv.DictReader et al.) are unaffected.
    fprintf(f, "frame,pos_x,pos_y,pos_z,heading,pitch,cpu_ms,gpu_ms,total_ms,"
               "draws,state_changes,polys,regions_vis,regions_drawn,regions_occluded,anim_cpu_ms,"
               "alpha_test_draws,solid_draws\n");

    for (const auto& m : frames_) {
        fprintf(f, "%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%.3f,%d,%d\n",
                m.frameNum, m.posX, m.posY, m.posZ,
                m.heading, m.pitch,
                m.cpuDrawMs, m.gpuMs, m.totalMs,
                m.drawCalls, m.stateChanges, m.polyCount,
                m.regionsVisible, m.regionsDrawn, m.regionsOccluded,
                m.animationCpuMs,
                m.alphaTestDraws, m.solidDraws);
    }

    fclose(f);
    printf("CSV written: %s (%zu frames)\n", filename.c_str(), frames_.size());
}

static void printStats(const char* name, const char* unit,
                       std::vector<float>& values) {
    if (values.empty()) return;
    std::sort(values.begin(), values.end());
    float sum = std::accumulate(values.begin(), values.end(), 0.0f);
    float avg = sum / values.size();
    float mn = values.front();
    float mx = values.back();
    size_t p95idx = static_cast<size_t>(values.size() * 0.95f);
    if (p95idx >= values.size()) p95idx = values.size() - 1;
    float p95 = values[p95idx];
    printf("  %-20s  min=%-8.2f avg=%-8.2f p95=%-8.2f max=%-8.2f %s\n",
           name, mn, avg, p95, mx, unit);
}

static void printStatsInt(const char* name, std::vector<float>& values) {
    if (values.empty()) return;
    std::sort(values.begin(), values.end());
    float sum = std::accumulate(values.begin(), values.end(), 0.0f);
    float avg = sum / values.size();
    printf("  %-20s  min=%-8.0f avg=%-8.0f max=%-8.0f\n",
           name, values.front(), avg, values.back());
}

void MetricsCollector::printSummary() const {
    if (frames_.empty()) {
        printf("No frames recorded.\n");
        return;
    }

    printf("\n=== Zone Benchmark Summary (%zu frames) ===\n", frames_.size());

    std::vector<float> cpuMs, gpuMs, totalMs, draws, stateChanges, polys, regVis, regDrawn, regOccl, animMs;
    for (const auto& m : frames_) {
        cpuMs.push_back(m.cpuDrawMs);
        gpuMs.push_back(m.gpuMs);
        totalMs.push_back(m.totalMs);
        draws.push_back(static_cast<float>(m.drawCalls));
        stateChanges.push_back(static_cast<float>(m.stateChanges));
        polys.push_back(static_cast<float>(m.polyCount));
        regVis.push_back(static_cast<float>(m.regionsVisible));
        regDrawn.push_back(static_cast<float>(m.regionsDrawn));
        regOccl.push_back(static_cast<float>(m.regionsOccluded));
        animMs.push_back(m.animationCpuMs);
    }

    // Only print animation stats if any frame had animation time
    bool hasAnimation = false;
    for (float v : animMs) { if (v > 0) { hasAnimation = true; break; } }

    if (hasAnimation) printStats("Animation CPU", "ms", animMs);
    printStats("CPU draw", "ms", cpuMs);
    printStats("GPU sync", "ms", gpuMs);
    printStats("Total frame", "ms", totalMs);
    printStatsInt("Draw calls", draws);
    printStatsInt("State changes", stateChanges);
    printStatsInt("Polygons", polys);
    printStatsInt("Regions visible", regVis);
    printStatsInt("Regions drawn", regDrawn);
    printStatsInt("Regions occluded", regOccl);

    float avgTotal = std::accumulate(totalMs.begin(), totalMs.end(), 0.0f) / totalMs.size();
    printf("  Avg FPS: %.1f\n", 1000.0f / avgTotal);
    printf("=========================================\n\n");
}

void MetricsCollector::dumpFrame(const FrameMetrics& m) {
    printf("F%04d pos=(%.1f,%.1f,%.1f) hdg=%.1f pit=%.1f | "
           "polys=%d draws=%d st=%d | "
           "reg vis=%d drn=%d occ=%d | "
           "cpu=%.2f gpu=%.2f tot=%.2f ms\n",
           m.frameNum, m.posX, m.posY, m.posZ, m.heading, m.pitch,
           m.polyCount, m.drawCalls, m.stateChanges,
           m.regionsVisible, m.regionsDrawn, m.regionsOccluded,
           m.cpuDrawMs, m.gpuMs, m.totalMs);
}

} // namespace ZoneBench
