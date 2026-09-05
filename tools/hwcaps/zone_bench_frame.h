#pragma once
//
// zone_bench_frame.h — gles2_zone_benchmark's per-frame machinery.
//
// T387: the camera-path record/replay types, the scene setup, the draw
// submission and the frame statistics were 660 lines of
// tools/hwcaps/gles2_zone_benchmark.cpp (3,060 lines — over the 1,500 ceiling
// M00 enforces tree-wide; board B167). Bodies moved verbatim to
// zone_bench_frame.cpp; the only change is that these definitions are no
// longer `static`, because main's TU and zone_bench_modes.cpp both call them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "zone_bench_loader.h"
#include "zone_bench_options.h"

namespace EQT::Graphics { class ZoneShaderManager; }

// ======== Q01: moving camera-path record/replay ========
// One resolved camera pose per rendered frame; see zone_bench_frame.cpp's
// block comment for why the whole trace is built up front.
struct TracePose {
    glm::vec3 pos;
    glm::vec3 dir;
    float heading = 0.0f;
    float pitch = 0.0f;
};

// One line of a loaded --path-file: "x,y,z,heading,pitch" (EQ coords +
// degrees -- exactly the --camera-pose convention, see printUsage()).
struct PathFilePose { float x, y, z, heading, pitch; };

bool loadPathFile(const std::string& path, std::vector<PathFilePose>& out);
void writePathFile(const std::string& path, const Options& opts,
                   const std::vector<TracePose>& traceMeasured);

void setupLighting(ZoneBench::ZoneBenchData& data, const Options& opts);
void setupCamera(ZoneBench::ZoneBenchData& data,
                 float eqX, float eqY, float eqZ,
                 float dirX, float dirY, float dirZ);

int renderDrawEntries(ZoneBench::ZoneBenchData& data,
                      const std::vector<ZoneBench::DrawEntry>& entries,
                      bool depthTest,
                      float* outSubmitMs = nullptr,
                      bool mergeZoneDraws = false,
                      int* outActualDraws = nullptr,
                      bool bakedUnmergedZoneDraws = false);

int countPolygons(const std::vector<ZoneBench::DrawEntry>& entries);

std::pair<int, int> countMaterialFamilies(
    const std::vector<ZoneBench::DrawEntry>& entries,
    EQT::Graphics::ZoneShaderManager* zoneShader);

#ifdef EQT_HAS_GLES2
void dumpFilterState(ZoneBench::ZoneBenchData& data,
                     const std::vector<ZoneBench::DrawEntry>& entries,
                     int count);
#endif

void poseToDirection(float headingDeg, float pitchDeg,
                     float& dirX, float& dirY, float& dirZ);

void computeFrameStats(ZoneBench::ZoneBenchData& data,
                       uint8_t clearR, uint8_t clearG, uint8_t clearB,
                       size_t& distinctColors, double& skyFraction);

void runSyntheticDrawTest(ZoneBench::ZoneBenchData& data, int numFrames);
