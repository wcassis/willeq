#pragma once

// T69: full per-subsystem memory accounting for the benchmark's
// --memory-json output, split into its own TU (check_file_sizes.sh's
// FAIL ceiling was already at 3044/3000 lines in gles2_zone_benchmark.cpp
// before this unit -- new code goes in a new TU rather than pushing an
// already-over-budget file further over).
//
// See zone_bench_memory.cpp's block comment for exactly which categories
// reuse IrrlichtRenderer::getMemoryReportBreakdown()'s underlying accounting
// methods (ConstrainedTextureCache, S3DZone, RaceModelLoader, the GLES2
// driver's gles2Get*MemoryUsage() externs, ConstrainedRendererConfig) versus
// which are necessarily recomputed because ZoneBenchData never constructs an
// IrrlichtRenderer.

#include <cstddef>
#include <string>

#include "zone_bench_loader.h"

struct BenchMemorySnapshot {
    // Reused verbatim from ConstrainedTextureCache (unchanged since H02).
    size_t textureCacheBytesUsed = 0;
    size_t textureCacheBytesLimit = 0;
    size_t textureCacheCount = 0;

    // New in T69.
    size_t eqe1MappedBytes = 0;
    size_t eqe1ArchiveCount = 0;
    size_t zoneGeometryBytes = 0;
    size_t placeableObjectsBytes = 0;
    size_t s3dZoneSourceBytes = 0;
    size_t gpuTextureBytes = 0;
    size_t vboEboBytes = 0;
    size_t framebufferBytes = 0;
    size_t framebufferBudgetBytes = 0;
    size_t characterModelsBytes = 0;

    size_t totalEstimate = 0;      // sum of every category above with a defined estimate
    size_t processRssBytes = 0;    // this process's own RSS (/proc/self/statm)
    size_t untrackedBytes = 0;     // processRssBytes - totalEstimate (0 if RSS unavailable or <= total)
};

// Computes the full breakdown from a loaded ZoneBenchData at the given
// resolution (needed for the framebuffer budget/usage pair).
BenchMemorySnapshot computeMemorySnapshot(ZoneBench::ZoneBenchData& data, int width, int height);

// Writes the snapshot as JSON to `path` (same shape --memory-json has
// always used, extended with the new T69 fields). No-op with a stderr-style
// printf on failure to open, matching the pre-T69 behavior.
void writeMemoryJson(const std::string& path, const std::string& zoneName,
                      const BenchMemorySnapshot& snap);
