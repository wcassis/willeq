// gles2_zone_benchmark — Offline zone rendering benchmark
//
// Loads a real EQ zone via the production S3D/WLD pipeline, replays a
// deterministic camera path (from navmesh or orbit fallback), and measures
// per-frame rendering metrics with A/B toggles for each optimization.
//
// Usage:
//   ./gles2_zone_benchmark --zone qeynos2 --eq-path /path/to/EQ \
//       --atlas-path /path/to/atlas --drm --frames 300 --csv results.csv
//
// See --help for full options.

#include "zone_bench_loader.h"
#include "zone_bench_camera.h"
#include "zone_bench_metrics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <irrlicht.h>

// Include full definitions for types used via unique_ptr/shared_ptr in ZoneBenchData
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/zone_bsp.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

// ======== CLI Options ========

struct Options {
    // Zone
    std::string zoneName = "qeynos2";
    std::string eqPath;
    std::string atlasPath;
    std::string navmeshPath;

    // Display
    int width = 800;
    int height = 600;
    bool useDRM = false;
    bool forceX11 = false;

    // Benchmark
    int frames = 300;
    int warmup = 10;
    float speed = 100.0f;    // Camera speed in EQ units/sec
    uint32_t seed = 42;
    int navWaypoints = 20;
    std::string csvFile;

    // A/B toggles (Z4)
    bool materialSort = true;
    bool portalCull = true;
    bool frustumCull = true;
    bool occlusionCull = false;   // Software occlusion culling (off by default, enable with --occlusion-cull)
    bool frontToBack = true;
    bool useAtlas = true;
    bool depthTest = true;
    int maxDraws = 0;         // 0 = unlimited
    int maxPolys = 0;         // 0 = unlimited
    int lightCount = -1;      // -1 = use zone defaults

    // Entities
    int entityCount = 0;          // Number of static entities to place
    float entityRenderDist = 300.0f;
    bool batchEntities = false;   // Merge same-material entity buffers into single draws
    bool compositeEntities = false; // Simulate 1-draw-per-entity (pre-baked composite texture)
    bool lodEntities = false;     // LOD tiers: composite near, billboard far
    float lodTier2Dist = 80.0f;   // Distance for billboard switch
    bool trainMode = false;       // Cluster entities together (train simulation)

    // Analysis modes
    bool analyzePortals = false;
    bool detectPortals = false;
    std::string portalCsvFile;

    // Synthetic (Z5)
    bool syntheticDraws = false;
};

static void printUsage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Zone options:\n");
    printf("  --zone NAME       Zone name (default: qeynos2)\n");
    printf("  --eq-path PATH    Path to EQ client directory (required)\n");
    printf("  --atlas-path PATH Path to atlas directory\n");
    printf("  --navmesh-path P  Path to navmesh directory\n");
    printf("\nDisplay options:\n");
    printf("  --drm             Use DRM/KMS framebuffer\n");
    printf("  --x11             Use X11/EGL\n");
    printf("  --res W H         Resolution (default: 800 600)\n");
    printf("\nBenchmark options:\n");
    printf("  --frames N        Frames to measure (default: 300)\n");
    printf("  --warmup N        Warmup frames (default: 10)\n");
    printf("  --speed F         Camera speed EQ units/sec (default: 100)\n");
    printf("  --seed N          Random seed for navmesh path (default: 42)\n");
    printf("  --waypoints N     Navmesh waypoints (default: 20)\n");
    printf("  --csv FILE        Write per-frame CSV\n");
    printf("\nA/B toggle flags:\n");
    printf("  --no-material-sort   Disable material-key draw sorting\n");
    printf("  --no-portal-cull     Disable PVS culling\n");
    printf("  --no-frustum-cull    Disable frustum culling\n");
    printf("  --occlusion-cull     Enable software occlusion culling\n");
    printf("  --no-front-to-back   Disable distance sorting\n");
    printf("  --no-atlas           Use per-texture meshes instead of atlas\n");
    printf("  --no-depth-test      Disable GL depth test\n");
    printf("  --max-draws N        Cap draw calls per frame\n");
    printf("  --max-polys N        Cap polygons per frame\n");
    printf("  --lighting N         Point light count (0-8)\n");
    printf("\nEntity options:\n");
    printf("  --entities N         Place N static entities along path (default: 0)\n");
    printf("  --entity-dist F      Entity render distance (default: 300)\n");
    printf("  --batch-entities     Batch same-material entity parts into single draws\n");
    printf("  --composite-entities Simulate 1-draw-per-entity (composite texture)\n");
    printf("  --lod-entities       LOD tiers: composite near + batched billboards far\n");
    printf("  --lod-dist F         Billboard switch distance (default: 80)\n");
    printf("  --train              Cluster entities together (train simulation)\n");
    printf("\nAnalysis modes:\n");
    printf("  --analyze-portals    Analyze portal geometry contact and exit\n");
    printf("  --detect-portals     Detect portals from occluder gaps and exit\n");
    printf("  --portal-csv FILE    Write portal analysis to CSV\n");
    printf("\nSynthetic tests:\n");
    printf("  --synthetic-draws    Run draw call overhead isolation test\n");
}

static Options parseArgs(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]); exit(0);
        }
        else if (strcmp(argv[i], "--zone") == 0 && i + 1 < argc) opts.zoneName = argv[++i];
        else if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) opts.eqPath = argv[++i];
        else if (strcmp(argv[i], "--atlas-path") == 0 && i + 1 < argc) opts.atlasPath = argv[++i];
        else if (strcmp(argv[i], "--navmesh-path") == 0 && i + 1 < argc) opts.navmeshPath = argv[++i];
        else if (strcmp(argv[i], "--drm") == 0) opts.useDRM = true;
        else if (strcmp(argv[i], "--x11") == 0) opts.forceX11 = true;
        else if (strcmp(argv[i], "--res") == 0 && i + 2 < argc) {
            opts.width = atoi(argv[++i]); opts.height = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) opts.frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) opts.warmup = atoi(argv[++i]);
        else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) opts.speed = atof(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) opts.seed = atoi(argv[++i]);
        else if (strcmp(argv[i], "--waypoints") == 0 && i + 1 < argc) opts.navWaypoints = atoi(argv[++i]);
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) opts.csvFile = argv[++i];
        else if (strcmp(argv[i], "--no-material-sort") == 0) opts.materialSort = false;
        else if (strcmp(argv[i], "--no-portal-cull") == 0) opts.portalCull = false;
        else if (strcmp(argv[i], "--no-frustum-cull") == 0) opts.frustumCull = false;
        else if (strcmp(argv[i], "--occlusion-cull") == 0) opts.occlusionCull = true;
        else if (strcmp(argv[i], "--no-front-to-back") == 0) opts.frontToBack = false;
        else if (strcmp(argv[i], "--no-atlas") == 0) opts.useAtlas = false;
        else if (strcmp(argv[i], "--no-depth-test") == 0) opts.depthTest = false;
        else if (strcmp(argv[i], "--max-draws") == 0 && i + 1 < argc) opts.maxDraws = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-polys") == 0 && i + 1 < argc) opts.maxPolys = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lighting") == 0 && i + 1 < argc) opts.lightCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entities") == 0 && i + 1 < argc) opts.entityCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entity-dist") == 0 && i + 1 < argc) opts.entityRenderDist = atof(argv[++i]);
        else if (strcmp(argv[i], "--batch-entities") == 0) opts.batchEntities = true;
        else if (strcmp(argv[i], "--composite-entities") == 0) opts.compositeEntities = true;
        else if (strcmp(argv[i], "--lod-entities") == 0) opts.lodEntities = true;
        else if (strcmp(argv[i], "--lod-dist") == 0 && i + 1 < argc) opts.lodTier2Dist = atof(argv[++i]);
        else if (strcmp(argv[i], "--train") == 0) opts.trainMode = true;
        else if (strcmp(argv[i], "--analyze-portals") == 0) opts.analyzePortals = true;
        else if (strcmp(argv[i], "--detect-portals") == 0) opts.detectPortals = true;
        else if (strcmp(argv[i], "--portal-csv") == 0 && i + 1 < argc) opts.portalCsvFile = argv[++i];
        else if (strcmp(argv[i], "--synthetic-draws") == 0) opts.syntheticDraws = true;
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]); exit(1);
        }
    }
    return opts;
}

// ======== Rendering ========

// Set up default lighting uniforms on the shader system.
static void setupLighting(ZoneBench::ZoneBenchData& data, const Options& opts) {
    if (!data.zoneShader) return;

    // Ambient + sun
    data.zoneShader->setAmbientColor(0.3f, 0.3f, 0.35f);
    data.zoneShader->setSunDirection(0.3f, -0.8f, 0.5f);
    data.zoneShader->setSunColor(0.8f, 0.75f, 0.65f);
    data.zoneShader->setTintColor(1.0f, 1.0f, 1.0f);

    // Fog (based on OrangePi preset: clip 300, fog 60-95%)
    float clipDist = 300.0f;
    data.zoneShader->setFog(clipDist * 0.6f, clipDist * 0.95f,
                            0.6f, 0.65f, 0.7f, 1.0f);

    // Point lights from zone data
    int numLights = 0;
    if (opts.lightCount >= 0) {
        numLights = std::min(opts.lightCount, 8);
    } else if (data.zone) {
        // Use zone lights (up to 7, index 0 reserved for player light)
        auto& lights = data.zone->lights;
        numLights = std::min(static_cast<int>(lights.size()), 7);
        for (int i = 0; i < numLights; ++i) {
            auto& l = lights[i];
            data.zoneShader->setPointLight(i + 1,  // index 0 = player light
                                           l->x, l->z, l->y,  // EQ→Irrlicht
                                           l->r, l->g, l->b,
                                           1.0f, 0.0f, l->radius > 0 ? 1.0f / (l->radius * l->radius) : 0.001f);
        }
    }
    data.zoneShader->setNumPointLights(numLights);

    // Player light (per-pixel, index 0) — will be updated per frame at camera pos
    data.zoneShader->setPlayerLightColor(1.0f, 0.95f, 0.8f);
    data.zoneShader->setPlayerLightAtten(1.0f, 0.0f, 0.0005f);
}

// Set up projection and view matrices for a given camera state (EQ coords).
static void setupCamera(ZoneBench::ZoneBenchData& data,
                         float eqX, float eqY, float eqZ,
                         float dirX, float dirY, float dirZ) {
    // Projection matrix
    float fov = 60.0f * 3.14159265f / 180.0f;
    float aspect = static_cast<float>(data.device->getVideoDriver()->getScreenSize().Width) /
                   static_cast<float>(data.device->getVideoDriver()->getScreenSize().Height);
    float nearClip = 1.0f;
    float farClip = 300.0f;  // Match OrangePi preset

    irr::core::matrix4 proj;
    proj.buildProjectionMatrixPerspectiveFovLH(fov, aspect, nearClip, farClip);
    data.driver->setTransform(irr::video::ETS_PROJECTION, proj);

    // View matrix: EQ coords (x, y, z-up) → Irrlicht (x, z, y-up)
    irr::core::vector3df irrPos(eqX, eqZ, eqY);
    irr::core::vector3df irrTarget(eqX + dirX, eqZ + dirZ, eqY + dirY);
    irr::core::vector3df irrUp(0, 1, 0);  // Irrlicht Y-up

    irr::core::matrix4 view;
    view.buildCameraLookAtMatrixLH(irrPos, irrTarget, irrUp);
    data.driver->setTransform(irr::video::ETS_VIEW, view);

    // Update frustum culler (EQ coords)
    if (data.frustumCuller) {
        data.frustumCuller->update(eqX, eqY, eqZ, dirX, dirY, dirZ,
                                   fov, aspect, nearClip, farClip);
    }

    // Update shader camera position (Irrlicht coords)
    if (data.zoneShader) {
        data.zoneShader->setCameraPos(irrPos.X, irrPos.Y, irrPos.Z);
        data.zoneShader->setPlayerLightPos(irrPos.X, irrPos.Y, irrPos.Z);
    }
}

// Draw all entries: setMaterial + drawMeshBuffer for each.
// Returns state change count.
static int renderDrawEntries(ZoneBench::ZoneBenchData& data,
                        const std::vector<ZoneBench::DrawEntry>& entries,
                        bool depthTest) {
#ifdef EQT_HAS_GLES2
    if (!depthTest) {
        glDisable(GL_DEPTH_TEST);
    }
#else
    (void)depthTest;
#endif

    int stateChanges = 0;
    uint32_t prevMatKey = UINT32_MAX;

    for (const auto& de : entries) {
        data.driver->setTransform(irr::video::ETS_WORLD, de.worldMat);
        data.driver->setMaterial(de.buffer->getMaterial());
        data.driver->drawMeshBuffer(de.buffer);

        if (de.materialKey != prevMatKey) {
            ++stateChanges;
            prevMatKey = de.materialKey;
        }
    }

#ifdef EQT_HAS_GLES2
    if (!depthTest) {
        glEnable(GL_DEPTH_TEST);
    }
#endif

    return stateChanges;
}

// Count total polygons in draw entries.
static int countPolygons(const std::vector<ZoneBench::DrawEntry>& entries) {
    int total = 0;
    for (const auto& de : entries) {
        total += de.buffer->getIndexCount() / 3;
    }
    return total;
}

// ======== Synthetic Draw Call Test (Z5) ========

static void runSyntheticDrawTest(ZoneBench::ZoneBenchData& data, int numFrames) {
    printf("\n=== Synthetic Draw Call Overhead Test ===\n");

    // Find a mesh buffer with geometry to use as our test draw
    irr::scene::IMeshBuffer* testBuf = nullptr;
    irr::core::matrix4 identityMat;

    for (auto& [idx, rd] : data.regions) {
        if (rd.mesh && rd.mesh->getMeshBufferCount() > 0) {
            testBuf = rd.mesh->getMeshBuffer(0);
            if (testBuf && testBuf->getVertexCount() > 0) {
                identityMat.setTranslation(rd.position);
                break;
            }
        }
    }

    if (!testBuf) {
        printf("  ERROR: No mesh buffer found for synthetic test\n");
        return;
    }

    int testPolys = testBuf->getIndexCount() / 3;
    printf("  Test buffer: %d vertices, %d polys\n",
           testBuf->getVertexCount(), testPolys);

    // Sweep draw call counts
    int drawCounts[] = {1, 10, 50, 100, 200, 500, 1000};
    int numSweeps = sizeof(drawCounts) / sizeof(drawCounts[0]);

    // Test A: Same material (no state changes)
    printf("\n  --- Same material (no state changes) ---\n");
    printf("  %8s  %10s  %10s  %10s  %10s\n",
           "Draws", "CPU ms", "GPU ms", "Total ms", "us/draw");

    for (int s = 0; s < numSweeps; ++s) {
        int N = drawCounts[s];
        // Warmup
        for (int w = 0; w < 5; ++w) {
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
            data.driver->setMaterial(testBuf->getMaterial());
            for (int d = 0; d < N; ++d)
                data.driver->drawMeshBuffer(testBuf);
#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            data.driver->endScene();
        }

        // Measure
        double cpuSum = 0, gpuSum = 0, totalSum = 0;
        for (int f = 0; f < numFrames; ++f) {
            auto t0 = std::chrono::steady_clock::now();
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
            data.driver->setMaterial(testBuf->getMaterial());

            auto drawStart = std::chrono::steady_clock::now();
            for (int d = 0; d < N; ++d)
                data.driver->drawMeshBuffer(testBuf);
            auto drawEnd = std::chrono::steady_clock::now();

#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            auto gpuEnd = std::chrono::steady_clock::now();
            data.driver->endScene();

            cpuSum += std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
            gpuSum += std::chrono::duration<double, std::milli>(gpuEnd - drawEnd).count();
            totalSum += std::chrono::duration<double, std::milli>(gpuEnd - t0).count();
        }

        double avgCpu = cpuSum / numFrames;
        double avgGpu = gpuSum / numFrames;
        double avgTotal = totalSum / numFrames;
        double usPerDraw = (avgCpu * 1000.0) / N;

        printf("  %8d  %10.2f  %10.2f  %10.2f  %10.1f\n",
               N, avgCpu, avgGpu, avgTotal, usPerDraw);
    }

    // Test B: Different material per draw (force state changes)
    printf("\n  --- Material switch per draw ---\n");
    printf("  %8s  %10s  %10s  %10s  %10s\n",
           "Draws", "CPU ms", "GPU ms", "Total ms", "us/draw");

    // Collect distinct mesh buffers with different materials
    std::vector<std::pair<irr::scene::IMeshBuffer*, irr::core::matrix4>> distinctBuffers;
    for (auto& [idx, rd] : data.regions) {
        if (!rd.mesh) continue;
        irr::core::matrix4 mat;
        mat.setTranslation(rd.position);
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            auto* buf = rd.mesh->getMeshBuffer(i);
            if (buf && buf->getVertexCount() > 0) {
                distinctBuffers.push_back({buf, mat});
                if (distinctBuffers.size() >= 1000) break;
            }
        }
        if (distinctBuffers.size() >= 1000) break;
    }

    for (int s = 0; s < numSweeps; ++s) {
        int N = drawCounts[s];
        if (N > static_cast<int>(distinctBuffers.size())) {
            printf("  %8d  (skipped, only %zu distinct buffers)\n", N, distinctBuffers.size());
            continue;
        }

        // Warmup
        for (int w = 0; w < 5; ++w) {
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            for (int d = 0; d < N; ++d) {
                auto& [buf, mat] = distinctBuffers[d];
                data.driver->setTransform(irr::video::ETS_WORLD, mat);
                data.driver->setMaterial(buf->getMaterial());
                data.driver->drawMeshBuffer(buf);
            }
#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            data.driver->endScene();
        }

        // Measure
        double cpuSum = 0, gpuSum = 0, totalSum = 0;
        for (int f = 0; f < numFrames; ++f) {
            auto t0 = std::chrono::steady_clock::now();
            data.driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));

            auto drawStart = std::chrono::steady_clock::now();
            for (int d = 0; d < N; ++d) {
                auto& [buf, mat] = distinctBuffers[d];
                data.driver->setTransform(irr::video::ETS_WORLD, mat);
                data.driver->setMaterial(buf->getMaterial());
                data.driver->drawMeshBuffer(buf);
            }
            auto drawEnd = std::chrono::steady_clock::now();

#ifdef EQT_HAS_GLES2
            glFinish();
#endif
            auto gpuEnd = std::chrono::steady_clock::now();
            data.driver->endScene();

            cpuSum += std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
            gpuSum += std::chrono::duration<double, std::milli>(gpuEnd - drawEnd).count();
            totalSum += std::chrono::duration<double, std::milli>(gpuEnd - t0).count();
        }

        double avgCpu = cpuSum / numFrames;
        double avgGpu = gpuSum / numFrames;
        double avgTotal = totalSum / numFrames;
        double usPerDraw = (avgCpu * 1000.0) / N;

        printf("  %8d  %10.2f  %10.2f  %10.2f  %10.1f\n",
               N, avgCpu, avgGpu, avgTotal, usPerDraw);
    }
}

// ======== Main ========

int main(int argc, char** argv) {
    Options opts = parseArgs(argc, argv);

    if (opts.eqPath.empty()) {
        fprintf(stderr, "ERROR: --eq-path is required\n");
        printUsage(argv[0]);
        return 1;
    }

    printf("=== gles2_zone_benchmark ===\n");
    printf("Zone: %s, Resolution: %dx%d, Frames: %d\n",
           opts.zoneName.c_str(), opts.width, opts.height, opts.frames);
    printf("Toggles: materialSort=%d portalCull=%d frustumCull=%d occlusionCull=%d "
           "frontToBack=%d atlas=%d depthTest=%d entities=%d\n",
           opts.materialSort, opts.portalCull, opts.frustumCull, opts.occlusionCull,
           opts.frontToBack, opts.useAtlas, opts.depthTest, opts.entityCount);
    if (opts.maxDraws > 0) printf("  maxDraws=%d\n", opts.maxDraws);
    if (opts.maxPolys > 0) printf("  maxPolys=%d\n", opts.maxPolys);
    printf("\n");

    // ── Load zone ──
    ZoneBench::BenchConfig benchConfig;
    benchConfig.zoneName = opts.zoneName;
    benchConfig.eqPath = opts.eqPath;
    benchConfig.atlasPath = opts.atlasPath;
    benchConfig.width = opts.width;
    benchConfig.height = opts.height;
    benchConfig.useDRM = opts.useDRM && !opts.forceX11;
    benchConfig.useAtlas = opts.useAtlas;
    benchConfig.entityCount = opts.entityCount;

    auto data = ZoneBench::loadZone(benchConfig);
    if (!data) {
        fprintf(stderr, "ERROR: Failed to load zone\n");
        return 1;
    }

    // ── Portal analysis modes ──
    if (opts.analyzePortals) {
        ZoneBench::analyzePortals(*data, opts.portalCsvFile);
        return 0;
    }
    if (opts.detectPortals) {
        ZoneBench::detectPortalsFromOccluders(*data, opts.portalCsvFile);
        return 0;
    }

    // ── Find a good surface-level position ──
    float surfX, surfY, surfZ;
    ZoneBench::findSurfacePosition(*data, surfX, surfY, surfZ);

    // ── Set up camera path ──
    ZoneBench::CameraPath camera;

    // Try navmesh first
    bool hasNavPath = false;
    if (!opts.navmeshPath.empty()) {
        std::string navFile = opts.navmeshPath;
        if (!navFile.empty() && navFile.back() != '/') navFile += '/';
        navFile += opts.zoneName + ".nav";
        hasNavPath = camera.generateFromNavmesh(navFile, opts.navWaypoints, opts.seed,
                                                 glm::vec3(surfX, surfY, surfZ));
    }

    // Fallback to orbit at surface level
    if (!hasNavPath) {
        float cx = (data->boundsMinX + data->boundsMaxX) * 0.5f;
        float cy = (data->boundsMinY + data->boundsMaxY) * 0.5f;
        float extent = std::max(data->boundsMaxX - data->boundsMinX,
                                data->boundsMaxY - data->boundsMinY);
        float radius = extent * 0.3f;  // Orbit at 30% of zone extent
        printf("Using orbit fallback: center=(%.0f,%.0f,%.0f) radius=%.0f\n",
               cx, cy, surfZ, radius);
        camera.generateOrbit(cx, cy, surfZ, radius, 10.0f);
    }

    if (camera.empty()) {
        fprintf(stderr, "ERROR: Failed to generate camera path\n");
        return 1;
    }

    // ── Place entities along path ──
    if (!data->entities.empty()) {
        // Sample positions from the camera path at even intervals
        std::vector<glm::vec3> pathPositions;
        float pathLen = camera.totalLength();
        int samples = std::max(static_cast<int>(data->entities.size()), 100);
        for (int i = 0; i < samples; ++i) {
            float t = pathLen * i / samples;
            pathPositions.push_back(camera.getPositionAt(t));
        }
        if (opts.trainMode) {
            // Place train at surface position, override camera to approach path
            glm::vec3 trainPos(surfX, surfY, surfZ);
            ZoneBench::placeEntitiesTrain(*data, trainPos);
            camera = ZoneBench::CameraPath();
            camera.generateApproach(surfX, surfY, surfZ, 250.0f, 6.0f);
            printf("Train mode: %d entities at (%.0f,%.0f,%.0f), approach from 250 units\n",
                   opts.entityCount, surfX, surfY, surfZ);
        } else {
            ZoneBench::placeEntitiesAlongPath(*data, pathPositions);
        }
    }

    // ── Set up lighting ──
    setupLighting(*data, opts);

    // ── Run synthetic test if requested ──
    if (opts.syntheticDraws) {
        runSyntheticDrawTest(*data, opts.frames);
        return 0;
    }

    // ── Benchmark loop ──
    printf("\nStarting benchmark: %d warmup + %d measured frames\n",
           opts.warmup, opts.frames);

    ZoneBench::MetricsCollector metrics;
    metrics.reserve(opts.frames);

    std::vector<ZoneBench::DrawEntry> drawEntries;
    drawEntries.reserve(2048);

    float dt = 1.0f / 30.0f;  // Fixed timestep for determinism
    float pathDist = 0;
    int totalFrames = opts.warmup + opts.frames;

    for (int frame = 0; frame < totalFrames; ++frame) {
        bool measuring = (frame >= opts.warmup);

        // Advance camera along path
        pathDist += opts.speed * dt;
        glm::vec3 camPos = camera.getPositionAt(pathDist);
        glm::vec3 camDir = camera.getDirectionAt(pathDist);

        // Set up camera transforms
        setupCamera(*data, camPos.x, camPos.y, camPos.z,
                     camDir.x, camDir.y, camDir.z);

        // Update shader frame
        if (data->zoneShader) {
            data->zoneShader->beginFrame();
        }

        // Begin frame
        data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));

        if (measuring) {
            metrics.beginFrame(frame - opts.warmup, camPos.x, camPos.y, camPos.z);
        }

        // Collect draw entries with visibility culling
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);
        ZoneBench::CameraState camState{camPos.x, camPos.y, camPos.z,
                                         camDir.x, camDir.y, camDir.z,
                                         fov, aspect};
        int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
        ZoneBench::collectDrawEntries(
            *data, camState,
            opts.materialSort, opts.portalCull, opts.frustumCull, opts.occlusionCull,
            opts.frontToBack, opts.maxDraws, opts.maxPolys,
            drawEntries, regionsVisible, regionsDrawn, regionsOccluded);

        // Collect and render entities
        int entityDrawCalls = 0, entityPolys = 0;
        if (!data->entities.empty()) {
            if (opts.lodEntities) {
                // LOD: composite near + batched billboards far
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
                int tier1 = 0, tier2 = 0;
                entityDrawCalls = ZoneBench::renderLodEntities(
                    *data, camState, opts.entityRenderDist, opts.lodTier2Dist,
                    opts.frustumCull, entityPolys, tier1, tier2);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            } else if (opts.compositeEntities) {
                // Composite: 1 draw per entity (simulates pre-baked composite texture)
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
                entityDrawCalls = ZoneBench::renderCompositeEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            } else if (opts.batchEntities) {
                // Batched: merge same-material entity parts into single draws
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
                entityDrawCalls = ZoneBench::renderBatchedEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            } else {
                // Unbatched: individual draw calls per entity mesh buffer
                ZoneBench::collectEntityDrawEntries(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    drawEntries, entityDrawCalls, entityPolys);
                int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest);
                int polyCount = countPolygons(drawEntries);

                if (measuring) {
                    metrics.endDraw(static_cast<int>(drawEntries.size()),
                                   stateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            }
        } else {
            // No entities — zone only
            int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest);
            int polyCount = countPolygons(drawEntries);

            if (measuring) {
                metrics.endDraw(static_cast<int>(drawEntries.size()),
                               stateChanges, polyCount,
                               regionsVisible, regionsDrawn);
            }
        }

        // GPU sync
#ifdef EQT_HAS_GLES2
        glFinish();
#endif

        if (measuring) {
            metrics.endGpuSync();
            metrics.endFrame();  // Measure before endScene (vsync page flip blocks)
        }

        data->driver->endScene();

        // Progress
        if (frame > 0 && frame % 100 == 0) {
            printf("  Frame %d/%d...\n", frame, totalFrames);
        }
    }

    // ── Output results ──
    metrics.printSummary();

    if (!opts.csvFile.empty()) {
        metrics.writeCSV(opts.csvFile);
    }

    printf("Benchmark complete.\n");
    return 0;
}
