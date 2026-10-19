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
#include "client/core/memory_ledger.h"

struct BenchMemorySnapshot {
    // Reused verbatim from ConstrainedTextureCache (unchanged since H02).
    size_t textureCacheBytesUsed = 0;
    size_t textureCacheBytesLimit = 0;
    size_t textureCacheCount = 0;

    // New in T69.
    size_t eqe1MappedBytes = 0;
    size_t eqe1ArchiveCount = 0;
    size_t zoneGeometryBytes = 0;
    // WHAT THIS MEASURES (T303, 2026-08-28): STATIC ARCHIVE PLACEABLES ONLY.
    // Summed over ZoneBenchData::objects, which zone_bench_loader.cpp fills
    // from the zone's own objects.wld/_obj.s3d placement instances on disk.
    // This tool never connects to a server, so the objects EQEmu SENDS at
    // zone-in -- the `object` table (ground spawns, forges, containers) and
    // the `doors` table -- are NOT counted here and are unmeasured by any
    // gate today. The production field of the same name
    // (renderer_diagnostics.cpp getMemoryReportBreakdown, over objectNodes_)
    // uses the identical per-buffer arithmetic but counts BOTH populations,
    // so an offline baseline and a live report may differ legitimately.
    // Do not read a blessed value here as coverage of the category.
    size_t placeableObjectsBytes = 0;
    size_t s3dZoneSourceBytes = 0;
    size_t gpuTextureBytes = 0;
    size_t vboEboBytes = 0;
    size_t framebufferBytes = 0;
    size_t framebufferBudgetBytes = 0;
    // T303: depends entirely on WHICH races were loaded. The gate pins that
    // (visual_gate.py MEMORY_COVERAGE_TEST_RACES = 1..5, ENTITY_COUNT = 5);
    // without --test-races the loader defaults to races 1..12
    // (zone_bench_loader.cpp). A blessed value guards those races only --
    // character memory for any race outside the list is unmeasured.
    size_t characterModelsBytes = 0;

    // T293: the per-subsystem budgets from ConstrainedRendererConfig, emitted
    // alongside the usage numbers so scripts/ci/visual_gate.py can assert
    // "usage <= budget" for these categories the way it already does for the
    // texture cache and the framebuffer. Read verbatim from the SAME
    // ConstrainedRendererConfig the loader built the texture cache from
    // (ConstrainedTextureCache::getConfig()) -- no second copy of the numbers
    // in CI. 0 means the active preset configures no budget for that category,
    // and the gate skips the assertion rather than asserting usage <= 0.
    size_t gpuTextureBudgetBytes = 0;
    size_t zoneGeometryBudgetBytes = 0;
    size_t placeableObjectsBudgetBytes = 0;
    size_t s3dZoneSourceBudgetBytes = 0;
    size_t vboEboBudgetBytes = 0;
    size_t characterModelsBudgetBytes = 0;
    // Address space, not RAM (T195/T108) -- eqe1MappedBytes is deliberately
    // excluded from totalEstimate for the same reason.
    size_t eqe1MappedBudgetBytes = 0;
    // Ceiling for totalEstimate itself (ConstrainedRendererConfig::
    // totalMemoryBudgetBytes, which has shipped on the orangepi preset since
    // long before T293 with nothing asserting against it).
    size_t totalEstimateBudgetBytes = 0;

    // T491: the allocator-level ledger (client/core/memory_ledger.h), taken
    // at the end of the run ("benchmark_end"), and the per-category budgets
    // from the SAME ConstrainedRendererConfig as the T293 budgets above
    // (memoryLedgerBudgetBytes; 0 = no budget, the gate skips it).
    eqt::memory::Snapshot ledger;
    // T491 addendum (board B801): the per-mapping census. The ledger's
    // `unaccounted` bucket is everything that never went through malloc, and
    // this is the only thing that says WHOSE it is.
    eqt::memory::MappingBreakdown mappings;
    std::array<size_t, eqt::memory::kCategoryCount> ledgerBudgetBytes{};

    // B952: the tex.impostor owner row as a JSON object ("" = no impostors,
    // and then the key is absent -- the gate's runs never create one).
    std::string impostorJson;

    size_t totalEstimate = 0;      // sum of every category above with a defined estimate
    size_t processRssBytes = 0;    // this process's own RSS (/proc/self/statm)
    size_t physFootprintBytes = 0; // macOS TASK_VM_INFO at this report point
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
