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
    // P07d (--pp-budget-mode radius, off by default): computed light effective
    // range R and the count of zone regions demoted to lightweight this frame
    // (see refactor/P07d_radius_demotion.md). Both 0 unless radius mode is
    // active for this run.
    float lightRadius = 0.0f;
    int regionsDemotedRadius = 0;
    // P08: per-phase CPU decomposition of cpuDrawMs (steady_clock reads
    // only, no syscalls -- near-zero overhead, always populated).
    // collectCullMs: zone draw-entry collection + PVS/frustum/occlusion
    //   culling + adaptive-lighting per-region decision (everything in
    //   ZoneBench::collectDrawEntries() except the final material sort),
    //   plus object/entity draw-entry collection (collectObjectDrawEntries/
    //   collectEntityDrawEntries, small relative to zone collection).
    // sortMs: the final material-key sort (std::sort over outEntries) in
    //   collectDrawEntries(). 0 if material sort is disabled.
    // submissionMs: the setMaterial+drawMeshBuffer loop (renderDrawEntries())
    //   -- in the default (unbatched) entity mode this is ONE combined call
    //   covering zone + entity draw entries together (they share one sorted
    //   vector); in the batched/composite/LOD/direct-GL entity modes it
    //   covers only the zone-geometry renderDrawEntries() call, entity
    //   submission there falls into otherMs (documented limitation, those
    //   modes are not the P08 canonical config).
    // swapMs: driver->endScene() (buffer swap / page flip) for THIS frame,
    //   captured separately because it happens AFTER totalMs is stamped
    //   (vsync page-flip would otherwise pollute totalMs) -- add swapMs to
    //   totalMs for true wall-clock frame pacing.
    // otherMs: cpuDrawMs - collectCullMs - sortMs - submissionMs (residual:
    //   object collection, entity collection in the unbatched path, matrix/
    //   material-override bookkeeping between phases). Clamped to >=0.
    float collectCullMs = 0;
    float sortMs = 0;
    float submissionMs = 0;
    float swapMs = 0;
    float otherMs = 0;
    // P08b: distinct entities that survived distance/frustum/occlusion cull
    // this frame (collectEntityDrawEntries' outEntitiesVisible). Only
    // populated in the default/unbatched entity path (the P08/P08b canonical
    // config); 0 for the batched/composite/LOD/direct-GL entity modes and
    // for zone-only (--entities 0) runs.
    int entitiesVisible = 0;
    // P38 audit: driver-side GL call counters (docker/irrlicht-drm/
    // gl_call_counters.h), populated only when --gl-counters is passed;
    // 0/0 otherwise (counters disabled, no overhead).
    unsigned texBinds = 0;
    unsigned texParamCalls = 0;
};

class MetricsCollector {
public:
    void reserve(int frameCount) { frames_.reserve(frameCount); }

    // P49 (refactor/P49_drm_fail_loud.md, P43 follow-up): the ACHIEVED
    // render resolution (irr::video::IVideoDriver::getScreenSize() *after*
    // device creation), as opposed to whatever --res requested -- on DRM/
    // ARM, a requested resolution with no exact matching connector mode
    // silently falls back to the connector's preferred mode (P43 root
    // cause). Call once, right after device creation, before any frame is
    // recorded. Written into every CSV row as trailing columns so no
    // dataset can ever be silently mislabeled with the requested-but-not-
    // achieved resolution again, regardless of whether the caller also
    // inspected the refusal/--allow-mode-fallback logic in main().
    void setActualResolution(int width, int height) {
        actualWidth_ = width;
        actualHeight_ = height;
    }

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

    // P08: per-phase CPU timing (call any time between beginFrame() and
    // endFrame(); typically right after collectDrawEntries()/
    // renderDrawEntries() return with their optional timing out-params).
    // otherMs is derived once all three phase numbers and cpuDrawMs are
    // known -- call finalizePhaseTiming() after endDraw() and after
    // setPhaseTiming() have both run for the frame.
    void setPhaseTiming(float collectCullMs, float sortMs, float submissionMs) {
        current_.collectCullMs = collectCullMs;
        current_.sortMs = sortMs;
        current_.submissionMs = submissionMs;
    }

    void finalizePhaseTiming() {
        float other = current_.cpuDrawMs - current_.collectCullMs -
                      current_.sortMs - current_.submissionMs;
        current_.otherMs = (other > 0.0f) ? other : 0.0f;
    }

    // P08: swap/present time for the frame just finished. Called after
    // endScene() returns, i.e. AFTER endFrame() already pushed the frame
    // into frames_ -- patches the just-pushed record in place, since
    // totalMs is deliberately stamped before endScene() (vsync page-flip
    // would otherwise pollute it; see the call site comment).
    void setSwapMsForLastFrame(float swapMs) {
        if (!frames_.empty()) frames_.back().swapMs = swapMs;
    }

    // P01 experiment 2: record the material-family draw histogram for the
    // frame currently being built (call between endDraw() and endFrame()).
    void setMaterialCounts(int alphaTestDraws, int solidDraws) {
        current_.alphaTestDraws = alphaTestDraws;
        current_.solidDraws = solidDraws;
    }

    // P07d: record radius-mode stats for the frame currently being built
    // (call any time between beginFrame() and endFrame()).
    void setRadiusStats(float lightRadius, int regionsDemotedRadius) {
        current_.lightRadius = lightRadius;
        current_.regionsDemotedRadius = regionsDemotedRadius;
    }

    // P08b: record the distinct-visible-entity count for the frame currently
    // being built (call any time between beginFrame() and endFrame()).
    void setEntitiesVisible(int entitiesVisible) {
        current_.entitiesVisible = entitiesVisible;
    }

    // P38: record this frame's GL call counters (see gl_call_counters.h).
    void setGLCounters(unsigned texBinds, unsigned texParamCalls) {
        current_.texBinds = texBinds;
        current_.texParamCalls = texParamCalls;
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
    // P49: actual achieved resolution, 0/0 if setActualResolution() was
    // never called (existing callers/tests unaffected -- csv columns are
    // simply 0/0, same append-only backward-compat convention as every
    // other trailing-column addition in this file).
    int actualWidth_ = 0;
    int actualHeight_ = 0;
};

} // namespace ZoneBench
