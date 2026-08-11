// T69: full per-subsystem memory accounting, split out of
// gles2_zone_benchmark.cpp into its own TU (see zone_bench_memory.h).
//
// This benchmark tool never constructs an IrrlichtRenderer -- ZoneBenchData
// (zone_bench_loader.h) is a separate, lighter-weight object graph (its own
// driver/textureCache/raceModelLoader/zone, not IrrlichtRenderer's scene
// nodes) -- so IrrlichtRenderer::getMemoryReportBreakdown()
// (src/client/graphics/renderer_diagnostics.cpp) itself cannot be called
// here without wiring up a full IrrlichtRenderer inside a benchmark tool, a
// structural change well beyond the T69 unit's scope (flagged per that
// plan's STOP conditions -- see refactor/T69_memory_enforcement_gate.md
// Review).
//
// What IS reused, exactly, category by category:
//   - Texture cache:      ConstrainedTextureCache::getCurrentUsage/
//                          getMemoryLimit/getTextureCount() -- the same
//                          methods getMemoryReportBreakdown() calls, same
//                          class, just this tool's own instance.
//   - S3D zone source:    S3DZone::getMemoryUsage() -- same method.
//   - Character models:   RaceModelLoader::getMemoryStats() -- same method
//                          (only populated when --entities loads race
//                          models; zero/absent for the golden zones' plain
//                          zone-geometry benchmark runs).
//   - GPU textures/VBOs:  gles2GetGpuTextureMemoryUsage/
//                          gles2GetHWBufferMemoryUsage/-Count() -- the exact
//                          same extern driver-level functions
//                          renderer_diagnostics.cpp calls, defined once in
//                          COpenGLES2Driver.cpp.
//   - Framebuffer budget: ConstrainedTextureCache::getConfig() returns the
//                          SAME ConstrainedRendererConfig
//                          zone_bench_loader.cpp built at load time (OrangePi
//                          preset) -- calculateFramebufferUsage() is called
//                          on it, the identical method
//                          getMemoryReportBreakdown() uses.
//   - EQE1 flat packs:    summed from ZoneBenchData::eqe1Loaders directly
//                          (same EQE1Loader class IrrlichtRenderer uses,
//                          this tool's own loaded instances).
//
// What is NOT reused, and why: zone geometry / placeable object byte totals
// (vertex+index bytes across mesh buffers). IrrlichtRenderer sums these from
// its own Irrlicht scene nodes (regionMeshNodes_/zoneMeshNode_/
// objectNodes_); ZoneBenchData holds the equivalent meshes in its own
// structures (ZoneBenchData::regions map<size_t,RegionData>,
// ZoneBenchData::objects vector<PlacedObject>) that never touch
// IrrlichtRenderer. There is no shared production entry point for "sum
// vertex/index bytes across an IMesh's buffers" outside that inline
// arithmetic (identical four-line formula, `vertexCount * sizeof(S3DVertex)
// + indexCount * sizeof(uint16_t)`, in both places) -- writing it twice is a
// disclosed, minimal duplication of a constant-factor formula, not a second
// independent *tally* of the same object graph (contrast T57/T60, where two
// code paths recomputed conflicting numbers from the SAME underlying data).
//
// NOT measured by this benchmark at all (always zero/absent in its JSON,
// not a "0 bytes" claim about production): entities (game-side estimate),
// doors, spell database, HCMap/collision (no collision map loaded here),
// audio (no AudioManager in this tool), network. These categories are real
// in the live client and are exercised by /pmem, but this benchmark's
// object graph never touches them -- see the T69 Review for the full list
// this implies for CI coverage.

#include "zone_bench_memory.h"

#include <cstdio>

#include <irrlicht.h>

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/race_model_loader.h"

#ifdef EQT_HAS_GLES2
// Same driver-level accounting IrrlichtRenderer::getMemoryReportBreakdown()
// uses (renderer_diagnostics.cpp) -- defined once in the GLES2 driver
// (docker/irrlicht-drm/COpenGLES2Driver.cpp) and declared extern at every
// call site (renderer_diagnostics.cpp does the same). This benchmark links
// the driver directly, so calling these is reuse of the real driver-side
// tally, not a second one.
extern size_t gles2GetHWBufferMemoryUsage(void* driver);
extern size_t gles2GetHWBufferCount(void* driver);
extern size_t gles2GetGpuTextureMemoryUsage(void* driver);
#endif

#ifdef __linux__
#include <unistd.h>
#endif

#ifdef __linux__
static size_t readProcessRssBytes() {
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long vmPages = 0, rssPages = 0;
    size_t rssBytes = 0;
    if (fscanf(f, "%lu %lu", &vmPages, &rssPages) == 2) {
        long pageSize = sysconf(_SC_PAGESIZE);
        rssBytes = static_cast<size_t>(rssPages) * static_cast<size_t>(pageSize);
    }
    fclose(f);
    return rssBytes;
}
#else
static size_t readProcessRssBytes() { return 0; }
#endif

static size_t sumMeshBufferBytes(irr::scene::IMesh* mesh) {
    if (!mesh) return 0;
    size_t bytes = 0;
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        auto* buf = mesh->getMeshBuffer(i);
        if (!buf) continue;
        bytes += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
        bytes += buf->getIndexCount() * sizeof(uint16_t);
    }
    return bytes;
}

BenchMemorySnapshot computeMemorySnapshot(ZoneBench::ZoneBenchData& data, int width, int height) {
    BenchMemorySnapshot snap;

    if (data.textureCache) {
        snap.textureCacheBytesUsed = data.textureCache->getCurrentUsage();
        snap.textureCacheBytesLimit = data.textureCache->getMemoryLimit();
        snap.textureCacheCount = data.textureCache->getTextureCount();

        // Framebuffer budget/usage: same ConstrainedRendererConfig (OrangePi
        // preset) the loader built the texture cache from -- see block
        // comment above.
        const auto& cfg = data.textureCache->getConfig();
        snap.framebufferBudgetBytes = cfg.framebufferMemoryBytes;
        snap.framebufferBytes = cfg.calculateFramebufferUsage(width, height);
        snap.totalEstimate += snap.framebufferBytes;
    }

    for (auto& [id, region] : data.regions) {
        snap.zoneGeometryBytes += sumMeshBufferBytes(region.mesh);
    }
    snap.totalEstimate += snap.zoneGeometryBytes;

    for (auto& obj : data.objects) {
        snap.placeableObjectsBytes += sumMeshBufferBytes(obj.mesh);
    }
    snap.totalEstimate += snap.placeableObjectsBytes;

    for (auto& loader : data.eqe1Loaders) {
        if (loader) ++snap.eqe1ArchiveCount;
    }
    // EQE1Loader doesn't expose a per-loader byte count here (unlike
    // IrrlichtRenderer's getEQE1MappedBytesTotal(), a renderer-level
    // aggregate over eqe1Loaders_ this tool has no access to without
    // constructing an IrrlichtRenderer) -- reported as archive count only;
    // see the T69 Review.

    if (data.zone) {
        snap.s3dZoneSourceBytes = data.zone->getMemoryUsage();
        snap.totalEstimate += snap.s3dZoneSourceBytes;
    }

    if (data.raceModelLoader) {
        auto stats = data.raceModelLoader->getMemoryStats();
        size_t texTotal = stats.globalTextureBytes + stats.numberedTextureBytes
                        + stats.armorTextureBytes + stats.zoneTextureBytes
                        + stats.otherChrTextureBytes;
        snap.characterModelsBytes = texTotal + stats.modelGeometryBytes + stats.modelSkeletonBytes
                        + stats.characterModelBytes + stats.animatedMeshBytes
                        + stats.irrlichtMeshBytes;
        snap.totalEstimate += snap.characterModelsBytes;
    }

#ifdef EQT_HAS_GLES2
    if (data.driver) {
        snap.gpuTextureBytes = gles2GetGpuTextureMemoryUsage(data.driver);
        snap.totalEstimate += snap.gpuTextureBytes;

        size_t hwbCount = gles2GetHWBufferCount(data.driver);
        if (hwbCount > 0) {
            snap.vboEboBytes = gles2GetHWBufferMemoryUsage(data.driver);
            snap.totalEstimate += snap.vboEboBytes;
        }
    }
#endif

    snap.processRssBytes = readProcessRssBytes();
    if (snap.processRssBytes > snap.totalEstimate) {
        snap.untrackedBytes = snap.processRssBytes - snap.totalEstimate;
    }

    return snap;
}

void writeMemoryJson(const std::string& path, const std::string& zoneName,
                      const BenchMemorySnapshot& snap) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        printf("[memory-json] %s: FAILED to open for write\n", path.c_str());
        return;
    }
    fprintf(f,
            "{\n"
            "  \"zone\": \"%s\",\n"
            "  \"textureCacheBytesUsed\": %zu,\n"
            "  \"textureCacheBytesLimit\": %zu,\n"
            "  \"textureCount\": %zu,\n"
            "  \"eqe1MappedBytes\": %zu,\n"
            "  \"eqe1ArchiveCount\": %zu,\n"
            "  \"zoneGeometryBytes\": %zu,\n"
            "  \"placeableObjectsBytes\": %zu,\n"
            "  \"s3dZoneSourceBytes\": %zu,\n"
            "  \"gpuTextureBytes\": %zu,\n"
            "  \"vboEboBytes\": %zu,\n"
            "  \"framebufferBytes\": %zu,\n"
            "  \"framebufferBudgetBytes\": %zu,\n"
            "  \"characterModelsBytes\": %zu,\n"
            "  \"totalEstimate\": %zu,\n"
            "  \"processRssBytes\": %zu,\n"
            "  \"untrackedBytes\": %zu\n"
            "}\n",
            zoneName.c_str(),
            snap.textureCacheBytesUsed, snap.textureCacheBytesLimit, snap.textureCacheCount,
            snap.eqe1MappedBytes, snap.eqe1ArchiveCount,
            snap.zoneGeometryBytes, snap.placeableObjectsBytes,
            snap.s3dZoneSourceBytes, snap.gpuTextureBytes, snap.vboEboBytes,
            snap.framebufferBytes, snap.framebufferBudgetBytes,
            snap.characterModelsBytes,
            snap.totalEstimate, snap.processRssBytes, snap.untrackedBytes);
    fclose(f);
    printf("[memory-json] %s: saved\n", path.c_str());
}
