#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ZoneBench {

struct FrameMetrics {
    int frameNum = 0;
    float posX = 0, posY = 0, posZ = 0;  // Camera position (EQ coords)
    float cpuDrawMs = 0;      // CPU draw submit time (excluding glFinish)
    float gpuMs = 0;          // glFinish duration
    float totalMs = 0;        // Total frame time
    int drawCalls = 0;        // drawMeshBuffer invocations
    int stateChanges = 0;     // Material/texture/program switches
    int polyCount = 0;        // Triangles drawn
    int regionsVisible = 0;   // Regions passing PVS + frustum
    int regionsDrawn = 0;     // Regions actually drawn (after budget caps)
};

class MetricsCollector {
public:
    void reserve(int frameCount) { frames_.reserve(frameCount); }

    void beginFrame(int frameNum, float posX, float posY, float posZ) {
        current_ = {};
        current_.frameNum = frameNum;
        current_.posX = posX;
        current_.posY = posY;
        current_.posZ = posZ;
        frameStart_ = std::chrono::steady_clock::now();
        drawStart_ = frameStart_;
    }

    void endDraw(int drawCalls, int stateChanges, int polyCount,
                 int regionsVisible, int regionsDrawn) {
        drawEnd_ = std::chrono::steady_clock::now();
        current_.drawCalls = drawCalls;
        current_.stateChanges = stateChanges;
        current_.polyCount = polyCount;
        current_.regionsVisible = regionsVisible;
        current_.regionsDrawn = regionsDrawn;
        current_.cpuDrawMs = msElapsed(drawStart_, drawEnd_);
    }

    void endGpuSync() {
        gpuEnd_ = std::chrono::steady_clock::now();
        current_.gpuMs = msElapsed(drawEnd_, gpuEnd_);
    }

    void endFrame() {
        auto now = std::chrono::steady_clock::now();
        current_.totalMs = msElapsed(frameStart_, now);
        frames_.push_back(current_);
    }

    void writeCSV(const std::string& filename) const;
    void printSummary() const;

    const std::vector<FrameMetrics>& frames() const { return frames_; }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static float msElapsed(TimePoint a, TimePoint b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    }

    FrameMetrics current_;
    TimePoint frameStart_, drawStart_, drawEnd_, gpuEnd_;
    std::vector<FrameMetrics> frames_;
};

} // namespace ZoneBench
