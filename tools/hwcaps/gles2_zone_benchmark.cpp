// gles2_zone_benchmark — Offline zone rendering benchmark
//
// Loads a real EQ zone via the production S3D/WLD pipeline, replays a
// deterministic camera path (from navmesh or orbit fallback), and measures
// per-frame rendering metrics with A/B toggles for each optimization.
//
// Usage:
//   ./gles2_zone_benchmark --zone qeynos2 --eq-path /path/to/EQ \
//       --eqe1-path /path/to/eqe1 --drm --frames 300 --csv results.csv
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
#include <EGL/egl.h>


#endif

// ======== CLI Options ========

struct Options {
    // Zone
    std::string zoneName = "qeynos2";
    std::string eqPath;
    std::string eqe1Path;
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
    bool depthTest = true;
    int maxDraws = 0;         // 0 = unlimited
    int maxPolys = 0;         // 0 = unlimited
    int lightCount = -1;      // -1 = use zone defaults

    // Entities
    int entityCount = 0;          // Number of static entities to place
    float entityRenderDist = 300.0f;
    bool batchEntities = false;   // Merge same-material entity buffers into single draws
    bool compositeEntities = false; // Simulate 1-draw-per-entity (pre-baked composite texture)
    bool directGLEntities = false; // Bypass Irrlicht, direct GL calls per entity
    bool lodEntities = false;     // LOD tiers: composite near, billboard far
    float lodTier2Dist = 80.0f;   // Distance for billboard switch
    bool adaptiveLod = false;     // Predictive LOD: billboard excess entities
    int entityBudget = 55;        // Max composite entities per frame
    bool adaptiveLighting = false; // Per-pixel near, per-vertex far
    int perPixelBudget = 15;       // Max entities with per-pixel lighting
    int perPixelZoneBudget = 20;   // Max zone regions with per-pixel lighting
    bool trainMode = false;       // Cluster entities together (train simulation)

    // Zone objects
    bool loadObjects = false;     // Load zone objects (trees, lamps, etc.)
    float objectRenderDist = 300.0f;

    // Analysis modes
    bool analyzePortals = false;
    bool detectPortals = false;
    std::string portalCsvFile;

    // Diagnostic
    bool treeTest = false;        // Render single tree object, screenshot, exit

    // Synthetic (Z5)
    bool syntheticDraws = false;
};

static void printUsage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Zone options:\n");
    printf("  --zone NAME       Zone name (default: qeynos2)\n");
    printf("  --eq-path PATH    Path to EQ client directory (required)\n");
    printf("  --eqe1-path PATH  Path to EQE1 texture directory\n");
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
    printf("  --no-depth-test      Disable GL depth test\n");
    printf("  --max-draws N        Cap draw calls per frame\n");
    printf("  --max-polys N        Cap polygons per frame\n");
    printf("  --lighting N         Point light count (0-8)\n");
    printf("\nEntity options:\n");
    printf("  --entities N         Place N static entities along path (default: 0)\n");
    printf("  --entity-dist F      Entity render distance (default: 300)\n");
    printf("  --batch-entities     Batch same-material entity parts into single draws\n");
    printf("  --composite-entities Simulate 1-draw-per-entity (composite texture)\n");
    printf("  --direct-gl          Bypass Irrlicht for entities (direct GL calls)\n");
    printf("  --lod-entities       LOD tiers: composite near + batched billboards far\n");
    printf("  --adaptive-lod       Predictive LOD: billboard excess entities to stay in budget\n");
    printf("  --entity-budget N    Max composite entities per frame (default: 55)\n");
    printf("  --adaptive-lighting  Per-pixel lighting near, per-vertex far (zone + entities)\n");
    printf("  --perpixel-budget N  Max entities with per-pixel lighting (default: 15)\n");
    printf("  --zone-pp-budget N   Max zone regions with per-pixel lighting (default: 20)\n");
    printf("  --lod-dist F         Billboard switch distance (default: 80)\n");
    printf("  --train              Cluster entities together (train simulation)\n");
    printf("\nObject options:\n");
    printf("  --objects            Load zone objects (trees, lamps, fences)\n");
    printf("  --object-dist F      Object render distance (default: 300)\n");
    printf("\nAnalysis modes:\n");
    printf("  --analyze-portals    Analyze portal geometry contact and exit\n");
    printf("  --detect-portals     Detect portals from occluder gaps and exit\n");
    printf("  --portal-csv FILE    Write portal analysis to CSV\n");
    printf("\nDiagnostic:\n");
    printf("  --tree-test          Render single tree, screenshot, exit\n");
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
        else if (strcmp(argv[i], "--eqe1-path") == 0 && i + 1 < argc) opts.eqe1Path = argv[++i];
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
        else if (strcmp(argv[i], "--no-depth-test") == 0) opts.depthTest = false;
        else if (strcmp(argv[i], "--max-draws") == 0 && i + 1 < argc) opts.maxDraws = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-polys") == 0 && i + 1 < argc) opts.maxPolys = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lighting") == 0 && i + 1 < argc) opts.lightCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entities") == 0 && i + 1 < argc) opts.entityCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entity-dist") == 0 && i + 1 < argc) opts.entityRenderDist = atof(argv[++i]);
        else if (strcmp(argv[i], "--batch-entities") == 0) opts.batchEntities = true;
        else if (strcmp(argv[i], "--composite-entities") == 0) opts.compositeEntities = true;
        else if (strcmp(argv[i], "--direct-gl") == 0) opts.directGLEntities = true;
        else if (strcmp(argv[i], "--lod-entities") == 0) opts.lodEntities = true;
        else if (strcmp(argv[i], "--adaptive-lod") == 0) opts.adaptiveLod = true;
        else if (strcmp(argv[i], "--entity-budget") == 0 && i + 1 < argc) opts.entityBudget = atoi(argv[++i]);
        else if (strcmp(argv[i], "--adaptive-lighting") == 0) opts.adaptiveLighting = true;
        else if (strcmp(argv[i], "--perpixel-budget") == 0 && i + 1 < argc) opts.perPixelBudget = atoi(argv[++i]);
        else if (strcmp(argv[i], "--zone-pp-budget") == 0 && i + 1 < argc) opts.perPixelZoneBudget = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lod-dist") == 0 && i + 1 < argc) opts.lodTier2Dist = atof(argv[++i]);
        else if (strcmp(argv[i], "--train") == 0) opts.trainMode = true;
        else if (strcmp(argv[i], "--analyze-portals") == 0) opts.analyzePortals = true;
        else if (strcmp(argv[i], "--detect-portals") == 0) opts.detectPortals = true;
        else if (strcmp(argv[i], "--portal-csv") == 0 && i + 1 < argc) opts.portalCsvFile = argv[++i];
        else if (strcmp(argv[i], "--objects") == 0) opts.loadObjects = true;
        else if (strcmp(argv[i], "--tree-test") == 0) { opts.treeTest = true; opts.loadObjects = true; }
        else if (strcmp(argv[i], "--object-dist") == 0 && i + 1 < argc) opts.objectRenderDist = atof(argv[++i]);
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

    // Midnight lighting — dark ambient, no sun, player light is dominant
    data.zoneShader->setAmbientColor(0.02f, 0.02f, 0.04f);
    data.zoneShader->setSunDirection(0.0f, -1.0f, 0.0f);
    data.zoneShader->setSunColor(0.0f, 0.0f, 0.0f);
    data.zoneShader->setTintColor(0.15f, 0.15f, 0.2f);

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
    // Note: setPerPixelPlayerLight(true) is called in loadZone() before mesh building
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

        if (de.materialOverride >= 0) {
            // Apply material with overridden shader type
            irr::video::SMaterial mat = de.buffer->getMaterial();
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de.materialOverride);
            data.driver->setMaterial(mat);
        } else {
            data.driver->setMaterial(de.buffer->getMaterial());
        }
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
           "frontToBack=%d depthTest=%d entities=%d\n",
           opts.materialSort, opts.portalCull, opts.frustumCull, opts.occlusionCull,
           opts.frontToBack, opts.depthTest, opts.entityCount);
    if (opts.maxDraws > 0) printf("  maxDraws=%d\n", opts.maxDraws);
    if (opts.maxPolys > 0) printf("  maxPolys=%d\n", opts.maxPolys);
    printf("\n");

    // ── Load zone ──
    ZoneBench::BenchConfig benchConfig;
    benchConfig.zoneName = opts.zoneName;
    benchConfig.eqPath = opts.eqPath;
    benchConfig.eqe1Path = opts.eqe1Path;
    benchConfig.width = opts.width;
    benchConfig.height = opts.height;
    benchConfig.useDRM = opts.useDRM && !opts.forceX11;
    benchConfig.entityCount = opts.entityCount;
    benchConfig.loadObjects = opts.loadObjects;

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

    // ── Tree test: render one tree, screenshot, exit ──
    if (opts.treeTest) {
        printf("=== Tree Test ===\n");

        // Find TREE6 or TREE1 by name
        ZoneBench::ZoneBenchData::PlacedObject* treeObj = nullptr;
        for (auto& obj : data->objects) {
            if (!obj.mesh) continue;
            if (obj.name == "TREE6" || obj.name == "TREE1") {
                treeObj = &obj;
                break;
            }
        }

        if (!treeObj) {
            printf("No tree object found with alpha material\n");
            return 1;
        }

        printf("Tree '%s' found: %d buffers, pos=(%.1f, %.1f, %.1f)\n",
               treeObj->name.c_str(), treeObj->mesh->getMeshBufferCount(),
               treeObj->eqX, treeObj->eqY, treeObj->eqZ);
        for (irr::u32 i = 0; i < treeObj->mesh->getMeshBufferCount(); i++) {
            auto* buf = treeObj->mesh->getMeshBuffer(i);
            const auto& mat = buf->getMaterial();
            printf("  buf[%u]: verts=%u idx=%u matType=%d tex0=%p tex1=%p\n",
                   i, buf->getVertexCount(), buf->getIndexCount(),
                   (int)mat.MaterialType, (void*)mat.getTexture(0), (void*)mat.getTexture(1));
        }

        // Compute bounding box from ALL mesh buffers to frame the whole tree
        irr::core::vector3df bbMin(1e9f, 1e9f, 1e9f), bbMax(-1e9f, -1e9f, -1e9f);
        for (irr::u32 bi = 0; bi < treeObj->mesh->getMeshBufferCount(); bi++) {
            auto* buf = treeObj->mesh->getMeshBuffer(bi);
            auto* verts = (irr::video::S3DVertex*)buf->getVertices();
            for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
                bbMin.X = std::min(bbMin.X, verts[vi].Pos.X);
                bbMin.Y = std::min(bbMin.Y, verts[vi].Pos.Y);
                bbMin.Z = std::min(bbMin.Z, verts[vi].Pos.Z);
                bbMax.X = std::max(bbMax.X, verts[vi].Pos.X);
                bbMax.Y = std::max(bbMax.Y, verts[vi].Pos.Y);
                bbMax.Z = std::max(bbMax.Z, verts[vi].Pos.Z);
            }
        }
        irr::core::vector3df treeCenter = (bbMin + bbMax) * 0.5f;
        float treeHeight = bbMax.Y - bbMin.Y;
        float treeWidth = std::max(bbMax.X - bbMin.X, bbMax.Z - bbMin.Z);
        float treeDiag = sqrtf(treeHeight * treeHeight + treeWidth * treeWidth);

        printf("[TREE-TEST] Bounding box: (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)\n",
               bbMin.X, bbMin.Y, bbMin.Z, bbMax.X, bbMax.Y, bbMax.Z);
        printf("[TREE-TEST] Center: (%.1f,%.1f,%.1f), height=%.1f, width=%.1f\n",
               treeCenter.X, treeCenter.Y, treeCenter.Z, treeHeight, treeWidth);
        fflush(stdout);

        // Camera: back up enough to see the whole tree, look at center
        float camDist = treeDiag * 1.5f;
        irr::core::vector3df camPos = treeCenter + irr::core::vector3df(camDist, 0.0f, 0.0f);
        irr::core::vector3df lookAt = treeCenter;

        printf("[TREE-TEST] Camera at (%.1f,%.1f,%.1f) looking at (%.1f,%.1f,%.1f), dist=%.1f\n",
               camPos.X, camPos.Y, camPos.Z, lookAt.X, lookAt.Y, lookAt.Z, camDist);
        fflush(stdout);

        // Build view and projection matrices manually (don't rely on scene manager)
        irr::core::matrix4 viewMat;
        viewMat.buildCameraLookAtMatrixLH(camPos, lookAt, irr::core::vector3df(0, 1, 0));
        data->driver->setTransform(irr::video::ETS_VIEW, viewMat);

        float aspect = (float)opts.width / (float)opts.height;
        irr::core::matrix4 projMat;
        projMat.buildProjectionMatrixPerspectiveFovLH(0.8f, aspect, 0.1f, 1000.0f);
        data->driver->setTransform(irr::video::ETS_PROJECTION, projMat);

        // Set up lighting — bright, no fog, so tree is clearly visible
        if (data->zoneShader) {
            data->zoneShader->beginFrame();
            data->zoneShader->setAmbientColor(0.8f, 0.8f, 0.8f);
            data->zoneShader->setSunDirection(0.3f, -0.8f, 0.2f);
            data->zoneShader->setSunColor(0.5f, 0.5f, 0.4f);
            data->zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
            data->zoneShader->setFog(9000.0f, 9999.0f, 0.5f, 0.5f, 0.5f, 1.0f);  // fog far away
            data->zoneShader->setNumPointLights(0);
            data->zoneShader->setPlayerLightPos(camPos.X, camPos.Y, camPos.Z);
            data->zoneShader->setPlayerLightColor(1.0f, 0.95f, 0.8f);
            data->zoneShader->setPlayerLightAtten(1.0f, 0.0f, 0.00001f);  // wide radius
        }

        irr::core::matrix4 identity;

        // Helper: save screenshot to BMP
        auto saveScreenshot = [&](const char* filename) {
#ifdef EQT_HAS_GLES2
            glFinish();
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            int scrW = vp[2], scrH = vp[3];
            std::vector<uint8_t> pixels(scrW * scrH * 4);
            glReadPixels(0, 0, scrW, scrH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            // Count non-clear pixels
            int nonClear = 0;
            for (int p = 0; p < scrW*scrH; p++) {
                uint8_t r = pixels[p*4], g = pixels[p*4+1], b = pixels[p*4+2];
                if (!(r == 100 && g == 150 && b == 255)) nonClear++;
            }
            printf("[TREE-TEST] %s: %d/%d non-clear pixels (%.1f%%)\n",
                   filename, nonClear, scrW*scrH, 100.0*nonClear/(scrW*scrH));

            FILE* f = fopen(filename, "wb");
            if (f) {
                uint32_t rowBytes = scrW * 3;
                uint32_t rowPad = (4 - (rowBytes % 4)) % 4;
                uint32_t dataSize = (rowBytes + rowPad) * scrH;
                uint32_t fileSize = 54 + dataSize;
                uint8_t hdr[54] = {};
                hdr[0] = 'B'; hdr[1] = 'M';
                memcpy(hdr + 2, &fileSize, 4);
                uint32_t off = 54; memcpy(hdr + 10, &off, 4);
                uint32_t dibSize = 40; memcpy(hdr + 14, &dibSize, 4);
                int32_t sw = scrW, sh = scrH;
                memcpy(hdr + 18, &sw, 4); memcpy(hdr + 22, &sh, 4);
                uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
                uint16_t bpp = 24; memcpy(hdr + 28, &bpp, 2);
                fwrite(hdr, 54, 1, f);
                uint8_t pad[3] = {};
                for (int y = 0; y < scrH; y++) {
                    for (int x = 0; x < scrW; x++) {
                        int idx = (y * scrW + x) * 4;
                        uint8_t bgr[3] = { pixels[idx+2], pixels[idx+1], pixels[idx+0] };
                        fwrite(bgr, 3, 1, f);
                    }
                    if (rowPad) fwrite(pad, rowPad, 1, f);
                }
                fclose(f);
                printf("[TREE-TEST] Saved %s (%dx%d)\n", filename, scrW, scrH);
            }
            fflush(stdout);
#endif
        };

        // === Screenshot 1: SOLID shader (no alpha discard) ===
        data->driver->beginScene(true, true, irr::video::SColor(255, 100, 150, 255));
        if (data->zoneShader) data->zoneShader->beginFrame();

        data->driver->setTransform(irr::video::ETS_WORLD, identity);
        for (irr::u32 i = 0; i < treeObj->mesh->getMeshBufferCount(); i++) {
            irr::video::SMaterial mat = treeObj->mesh->getMeshBuffer(i)->getMaterial();
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                data->zoneShader->getActiveSolid());
            data->driver->setMaterial(mat);
            data->driver->drawMeshBuffer(treeObj->mesh->getMeshBuffer(i));
        }
        saveScreenshot("tree_test_solid.bmp");
        data->driver->endScene();

        // === Screenshot 2: FULLY RAW GL alpha test (bypass Irrlicht completely) ===
#ifdef EQT_HAS_GLES2
        {
            auto compile = [](GLenum type, const char* src) -> GLuint {
                GLuint s = glCreateShader(type);
                glShaderSource(s, 1, &src, nullptr);
                glCompileShader(s);
                GLint ok = 0;
                glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
                if (!ok) {
                    char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
                    printf("[TREE-TEST] Shader error: %s\n", log);
                }
                return s;
            };

            // Attribute locations matching Irrlicht's S3DVertex layout
            // Position=0, Normal=1, Color=2, TexCoord0=3
            const char* rawVS = R"(
                attribute vec3 aPos;
                attribute vec3 aNormal;
                attribute vec4 aColor;
                attribute vec2 aTexCoord;
                uniform mat4 uWVP;
                varying vec2 vTexCoord;
                void main() {
                    gl_Position = uWVP * vec4(aPos, 1.0);
                    vTexCoord = aTexCoord;
                }
            )";
            const char* rawFS = R"(
                precision mediump float;
                uniform sampler2D uTexture;
                uniform sampler2D uAlphaTexture;
                varying vec2 vTexCoord;
                void main() {
                    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
                    if (alpha < 0.5) discard;
                    gl_FragColor = texture2D(uTexture, vTexCoord);
                }
            )";

            GLuint vs = compile(GL_VERTEX_SHADER, rawVS);
            GLuint fs = compile(GL_FRAGMENT_SHADER, rawFS);
            GLuint rawProg = glCreateProgram();
            glAttachShader(rawProg, vs);
            glAttachShader(rawProg, fs);
            glBindAttribLocation(rawProg, 0, "aPos");
            glBindAttribLocation(rawProg, 1, "aNormal");
            glBindAttribLocation(rawProg, 2, "aColor");
            glBindAttribLocation(rawProg, 3, "aTexCoord");
            glLinkProgram(rawProg);
            glDeleteShader(vs);
            glDeleteShader(fs);

            GLint locWVP = glGetUniformLocation(rawProg, "uWVP");
            GLint locTex = glGetUniformLocation(rawProg, "uTexture");
            GLint locAlpha = glGetUniformLocation(rawProg, "uAlphaTexture");
            printf("[TREE-TEST] Raw program: id=%u, uWVP=%d, uTex=%d, uAlpha=%d\n",
                   rawProg, locWVP, locTex, locAlpha);
            fflush(stdout);

            // Clear
            glClearColor(100/255.0f, 150/255.0f, 255/255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);

            glUseProgram(rawProg);
            irr::core::matrix4 wvp = projMat * viewMat;
            glUniformMatrix4fv(locWVP, 1, GL_FALSE, wvp.pointer());
            glUniform1i(locTex, 0);
            glUniform1i(locAlpha, 1);

            // Draw each mesh buffer using raw GL
            for (irr::u32 bi = 0; bi < treeObj->mesh->getMeshBufferCount(); bi++) {
                auto* buf = treeObj->mesh->getMeshBuffer(bi);
                const auto& mat = buf->getMaterial();

                // Use Irrlicht to bind textures, then query GL for the bound names
                data->driver->setMaterial(mat);
                GLint glTex0 = 0, glTex1 = 0;
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &glTex0);
                glActiveTexture(GL_TEXTURE1);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &glTex1);
                glActiveTexture(GL_TEXTURE0);

                // Also check what's ACTUALLY bound after switching program
                glUseProgram(rawProg);
                GLint finalTex0 = 0, finalTex1 = 0;
                glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &finalTex0);
                glActiveTexture(GL_TEXTURE1);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &finalTex1);
                glActiveTexture(GL_TEXTURE0);

                printf("[TREE-TEST] Raw draw buf[%u]: verts=%u, idx=%u, afterSetMat tex0=%d tex1=%d, afterProgSwitch tex0=%d tex1=%d\n",
                       bi, buf->getVertexCount(), buf->getIndexCount(), glTex0, glTex1, finalTex0, finalTex1);
                // Log UV coordinates for foliage buffer
                if (buf->getVertexCount() <= 32) {
                    const irr::video::S3DVertex* v = (const irr::video::S3DVertex*)buf->getVertices();
                    for (irr::u32 vi = 0; vi < buf->getVertexCount(); vi++) {
                        printf("[TREE-TEST]   v[%u] pos=(%.2f,%.2f,%.2f) uv=(%.4f,%.4f)\n",
                               vi, v[vi].Pos.X, v[vi].Pos.Y, v[vi].Pos.Z,
                               v[vi].TCoords.X, v[vi].TCoords.Y);
                    }
                }
                fflush(stdout);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, glTex0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, glTex1);
                glActiveTexture(GL_TEXTURE0);

                // Bind VBO/EBO from Irrlicht's HW buffer
                // Use client-side arrays from the mesh buffer
                const irr::video::S3DVertex* verts = (const irr::video::S3DVertex*)buf->getVertices();
                const irr::u16* indices = buf->getIndices();
                GLsizei stride = sizeof(irr::video::S3DVertex);

                // Unbind VBO to use client pointers
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Pos);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Normal);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, &verts[0].Color);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, &verts[0].TCoords);
                glEnableVertexAttribArray(3);

                glDrawElements(GL_TRIANGLES, buf->getIndexCount(), GL_UNSIGNED_SHORT, indices);
            }

            glFinish();
            saveScreenshot("tree_test_alpha.bmp");

            // === Screenshot 3: Raw GL, NO discard (confirm geometry renders) ===
            const char* noDiscardFS = R"(
                precision mediump float;
                uniform sampler2D uTexture;
                varying vec2 vTexCoord;
                void main() {
                    gl_FragColor = texture2D(uTexture, vTexCoord);
                }
            )";
            GLuint ndFS = compile(GL_FRAGMENT_SHADER, noDiscardFS);
            GLuint ndVS = compile(GL_VERTEX_SHADER, rawVS);
            GLuint ndProg = glCreateProgram();
            glAttachShader(ndProg, ndVS);
            glAttachShader(ndProg, ndFS);
            glBindAttribLocation(ndProg, 0, "aPos");
            glBindAttribLocation(ndProg, 1, "aNormal");
            glBindAttribLocation(ndProg, 2, "aColor");
            glBindAttribLocation(ndProg, 3, "aTexCoord");
            glLinkProgram(ndProg);
            glDeleteShader(ndVS);
            glDeleteShader(ndFS);

            glClearColor(100/255.0f, 150/255.0f, 255/255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(ndProg);
            GLint ndWVP = glGetUniformLocation(ndProg, "uWVP");
            GLint ndTex = glGetUniformLocation(ndProg, "uTexture");
            glUniformMatrix4fv(ndWVP, 1, GL_FALSE, wvp.pointer());
            glUniform1i(ndTex, 0);

            // Draw only foliage buffer (buf[0]) with tex0 only
            {
                auto* buf = treeObj->mesh->getMeshBuffer(0);
                const auto& mat = buf->getMaterial();
                data->driver->setMaterial(mat);  // binds textures
                glUseProgram(ndProg);  // switch back to our program
                glUniformMatrix4fv(ndWVP, 1, GL_FALSE, wvp.pointer());
                glUniform1i(ndTex, 0);

                const irr::video::S3DVertex* verts = (const irr::video::S3DVertex*)buf->getVertices();
                const irr::u16* indices = buf->getIndices();
                GLsizei stride = sizeof(irr::video::S3DVertex);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, &verts[0].Pos);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, &verts[0].TCoords);
                glEnableVertexAttribArray(3);
                glDrawElements(GL_TRIANGLES, buf->getIndexCount(), GL_UNSIGNED_SHORT, indices);
            }
            glFinish();
            saveScreenshot("tree_test_rawsolid.bmp");
            glDeleteProgram(ndProg);

            // Swap to show on screen too
            eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));

            glDeleteProgram(rawProg);
        }
#endif

        printf("Tree test complete.\n");
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

    // ── Configure adaptive LOD budget ──
    data->compositeEntityBudget = opts.entityBudget;

    // ── Configure adaptive lighting ──
    data->adaptiveLighting = opts.adaptiveLighting;
    data->perPixelEntityBudget = opts.perPixelBudget;
    data->perPixelZoneBudget = opts.perPixelZoneBudget;
    if (data->zoneShader) {
        data->materialPerPixelSolid = data->zoneShader->getMaterialTypeSolid();
        data->materialPerPixelAlpha = data->zoneShader->getMaterialTypeAlphaTest();
        data->materialLWSolid = data->zoneShader->getMaterialTypeLWSolid();
        data->materialLWAlpha = data->zoneShader->getMaterialTypeLWAlphaTest();
        if (opts.adaptiveLighting) {
            printf("Adaptive lighting: entity PP budget=%d, zone PP budget=%d\n",
                   opts.perPixelBudget, opts.perPixelZoneBudget);
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
        if (data->zoneShader) data->zoneShader->beginFrame();

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

        // Collect object draw entries (trees, lamps, etc.)
        int objectDrawCalls = 0, objectPolys = 0;
        if (!data->objects.empty()) {
            ZoneBench::collectObjectDrawEntries(
                *data, camState, opts.objectRenderDist, opts.frustumCull,
                drawEntries, objectDrawCalls, objectPolys);
        }

        // Collect and render entities
        int entityDrawCalls = 0, entityPolys = 0;
        if (!data->entities.empty()) {
            if (opts.directGLEntities && opts.compositeEntities) {
                // Direct GL + composite: 1 draw per entity via raw GL
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
                entityDrawCalls = ZoneBench::renderDirectGLCompositeEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            } else if (opts.directGLEntities) {
                // Direct GL per-body-part: bypass Irrlicht, same draw count as unbatched
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
                entityDrawCalls = ZoneBench::renderDirectGLEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            } else if (opts.adaptiveLod) {
                // Adaptive LOD: reactive billboard swap based on frame budget
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
                int tier1 = 0, tier2 = 0;
                // Use previous frame's total time for budget tracking
                float prevFrameMs = metrics.frames().empty() ? 0 :
                    metrics.frames().back().totalMs;
                entityDrawCalls = ZoneBench::renderAdaptiveLodEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    prevFrameMs, entityPolys, tier1, tier2);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn);
                }
            } else if (opts.lodEntities) {
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
