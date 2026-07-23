#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ZoneBench {

struct FrameMetrics {
    int frameNum = 0;
    float posX = 0, posY = 0, posZ = 0;  // Camera position (EQ coords)
    float heading = 0;        // Camera heading in degrees (0=North, CW)
    float pitch = 0;          // Camera pitch in degrees (positive=up)
    float cpuDrawMs = 0;      // CPU draw submit time (excluding glFinish)
    float gpuMs = 0;          // glFinish duration
    float totalMs = 0;        // Total frame time
    int drawCalls = 0;        // drawMeshBuffer invocations
    int stateChanges = 0;     // Material/texture/program switches
    int polyCount = 0;        // Triangles drawn
    float animationCpuMs = 0;  // Entity animation (skeletal skinning) CPU time
    int regionsVisible = 0;   // Regions passing PVS + frustum
    int regionsDrawn = 0;     // Regions actually drawn (after budget caps)
    int regionsOccluded = 0;  // Regions culled by software occlusion
    // P01 experiment 2 (--material-counts, off by default): draw-call
    // breakdown by material family. Both 0 unless setMaterialCounts() was
    // called for this frame.
    int alphaTestDraws = 0;
    int solidDraws = 0;
};

class MetricsCollector {
public:
    void reserve(int frameCount) { frames_.reserve(frameCount); }

    void beginFrame(int frameNum, float posX, float posY, float posZ,
                    float heading, float pitch) {
        current_ = {};
        current_.frameNum = frameNum;
        current_.posX = posX;
        current_.posY = posY;
        current_.posZ = posZ;
        current_.heading = heading;
        current_.pitch = pitch;
        frameStart_ = std::chrono::steady_clock::now();
        drawStart_ = frameStart_;
    }

    void beginAnimation() {
        animStart_ = std::chrono::steady_clock::now();
    }

    void endAnimation() {
        animEnd_ = std::chrono::steady_clock::now();
        current_.animationCpuMs = msElapsed(animStart_, animEnd_);
    }

    void endDraw(int drawCalls, int stateChanges, int polyCount,
                 int regionsVisible, int regionsDrawn, int regionsOccluded = 0) {
        drawEnd_ = std::chrono::steady_clock::now();
        current_.drawCalls = drawCalls;
        current_.stateChanges = stateChanges;
        current_.polyCount = polyCount;
        current_.regionsVisible = regionsVisible;
        current_.regionsDrawn = regionsDrawn;
        current_.regionsOccluded = regionsOccluded;
        current_.cpuDrawMs = msElapsed(drawStart_, drawEnd_);
    }

    // P01 experiment 2: record the material-family draw histogram for the
    // frame currently being built (call between endDraw() and endFrame()).
    void setMaterialCounts(int alphaTestDraws, int solidDraws) {
        current_.alphaTestDraws = alphaTestDraws;
        current_.solidDraws = solidDraws;
    }

    void endGpuSync() {
        gpuEnd_ = std::chrono::steady_clock::now();
        current_.gpuMs = msElapsed(drawEnd_, gpuEnd_);
    }

    void endFrame() {
        auto now = std::chrono::steady_clock::now();
        current_.totalMs = msElapsed(frameStart_, now);
        frames_.push_back(current_);
        if (dumpPerFrame_) dumpFrame(current_);
    }

    void setDumpPerFrame(bool enable) { dumpPerFrame_ = enable; }

    void writeCSV(const std::string& filename) const;
    void printSummary() const;

    const std::vector<FrameMetrics>& frames() const { return frames_; }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static float msElapsed(TimePoint a, TimePoint b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    }

    static void dumpFrame(const FrameMetrics& m);

    FrameMetrics current_;
    TimePoint frameStart_, drawStart_, drawEnd_, gpuEnd_, animStart_, animEnd_;
    std::vector<FrameMetrics> frames_;
    bool dumpPerFrame_ = false;
};

} // namespace ZoneBench
