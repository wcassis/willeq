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
#include "zone_bench_entity_viewer.h"
#include "zone_bench_histogram.h"
#include "zone_bench_merge.h"
#include "tool_race_db.h"
#include "zone_bench_merge_probe.h"
#include "zone_bench_metrics.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <utility>
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
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/zone_bsp.h"

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include "gl_call_counters.h"  // P38: driver-side glBindTexture/glTexParameteri counters
#include "gpu_sync_probe.h"    // P52: per-phase GPU-sync characterization instrumentation
#include "gpu_sync_bench_hooks.h"  // P52: call-site wrappers (kept out of this file to limit its growth)

#endif

// T69: full per-subsystem memory-JSON accounting (BenchMemorySnapshot,
// computeMemorySnapshot(), writeMemoryJson()) -- split into its own TU;
// see zone_bench_memory.h/.cpp for the reuse-vs-recompute accounting and
// why (check_file_sizes.sh's FAIL ceiling was already exceeded by this file
// before this unit).
#include "zone_bench_memory.h"

#include "zone_bench_options.h"

#include "zone_bench_frame.h"
#include "zone_bench_modes.h"


// ======== Main ========

int main(int argc, char** argv) {
    // T491: same start-up scoping as the client's main().
    eqt::memory::setThreadCategory(eqt::memory::Category::Startup);
    InitLogging(argc, argv);
    Options opts = parseArgs(argc, argv);

    // T270: race lookups are db-only now -- see tools/tool_race_db.h.
    openWilleqRaceDatabase();

    // P02: --lighting-model is a thin convenience wrapper over the existing
    // --adaptive-lighting/--zone-pp-budget mechanism (see Options::LightingModel
    // comment above); LightingModel::Default (flag never passed) touches
    // nothing, so default behavior is byte-for-byte unchanged.
    if (opts.lightingModel == Options::LightingModel::PerVertexOrLightweight) {
        opts.adaptiveLighting = true;
        opts.perPixelZoneBudget = 0;
        printf("[lighting-model] pervertex/lightweight requested -- forcing "
               "adaptiveLighting=true, zone-pp-budget=0 (all zone regions "
               "lightweight-shaded).\n");
    } else if (opts.lightingModel == Options::LightingModel::PerPixel) {
        printf("[lighting-model] perpixel requested -- default benchmark "
               "behavior (zone_bench_loader.cpp always calls "
               "setPerPixelPlayerLight(true); adaptiveLighting left as set by "
               "other flags, default off = all regions per-pixel).\n");
    }

    // P47: entity-viewer mode forces the entity-loading options to the
    // proven-correct one-variable configuration for the requested leg,
    // rather than requiring the caller to separately pass
    // --entities/--test-races/--animate/--no-entity-shader-promotion and
    // risk getting the pairing wrong (see zone_bench_loader.cpp's
    // applyEntityShaderMaterialPromotion() Review comment and
    // refactor/P46c_neutral_pair_stop.md / P46d_cache_key_fix.md for why
    // this pairing matters).
    if (opts.entityViewer) {
        if (opts.entityViewerRace <= 0) {
            fprintf(stderr, "ERROR: --entity-viewer requires --entity-viewer-race N\n");
            return 1;
        }
        if (opts.entityViewerOutDir.empty()) {
            fprintf(stderr, "ERROR: --entity-viewer requires --entity-viewer-out-dir DIR\n");
            return 1;
        }
        if (opts.entityViewerComposite && opts.eqe1Path.empty()) {
            fprintf(stderr, "ERROR: --entity-viewer-composite requires --eqe1-path "
                            "(composite sheet alpha only exists in the ETC1/EQE1 format)\n");
            return 1;
        }
        opts.entityCount = 1;
        opts.testRaceIds = { static_cast<uint16_t>(opts.entityViewerRace) };
        opts.animate = true;
        // P48: the composite leg routes the entity through the REAL composite
        // eligibility gate (race_model_loader.cpp's sheet-capability check,
        // zone_bench_loader.cpp's --composite-entities wiring) instead of
        // forcing multi-buffer -- this is what lets the viewer demonstrate a
        // masked race's composite alpha-test rather than only its per-part
        // alpha-test (which the plain ETC1/RGBA legs already covered, P47).
        // An unmasked or stale-sheet (no alpha companion) race composited
        // this way still renders correctly (EMT_SOLID, matches pre-P48) --
        // the eligibility gate and mesh_building.cpp's hasAlpha-gated
        // MaterialType make this automatic, no viewer-side branching needed.
        opts.compositeEntities = opts.entityViewerComposite;
        // RGBA leg (no --eqe1-path): the built-in alpha-test discard shader
        // reads unit-0 alpha directly and needs promotion OFF (P46c/P46d).
        // ETC1 leg (--eqe1-path set, composite or per-part): promotion must
        // stay ON -- it is the only code path that binds the companion alpha
        // texture to unit 1 (P46b/applyEntityShaderMaterialPromotion()), and
        // mesh_building.cpp's buildCompositeMeshFromGeometry() (P48) marks
        // composite buffers with the SAME EMT_TRANSPARENT_ALPHA_CHANNEL_REF
        // marker the per-part path uses, so the SAME promotion call picks it
        // up with no separate composite-specific plumbing.
        opts.noEntityShaderPromotion = opts.eqe1Path.empty();
        opts.entityViewerMode = opts.eqe1Path.empty() ? "rgba"
                               : (opts.entityViewerComposite ? "etc1-composite" : "etc1");
    }

    if (opts.eqPath.empty()) {
        fprintf(stderr, "ERROR: --eq-path is required\n");
        printUsage(argv[0]);
        return 1;
    }

    printf("=== gles2_zone_benchmark ===\n");
    printf("Zone: %s, Resolution: %dx%d, Frames: %d\n",
           opts.zoneName.c_str(), opts.width, opts.height, opts.frames);
    printf("Toggles: materialSort=%d pvsCull=%d frustumCull=%d occlusionCull=%d "
           "frontToBack=%d depthTest=%d entities=%d animate=%d\n",
           opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
           opts.frontToBack, opts.depthTest, opts.entityCount, opts.animate);
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
    benchConfig.testRaceIds = opts.testRaceIds;
    benchConfig.animate = opts.animate;
    benchConfig.compositeEntities = opts.compositeEntities;
    benchConfig.loadObjects = opts.loadObjects;
    benchConfig.objectAnimFrame = opts.objectAnimFrame;
    benchConfig.shaderDebugMode = opts.shaderDebugMode;
    benchConfig.loadMemAudit = opts.loadMemAudit;
    benchConfig.occluderBudget = opts.occluderBudget;
    benchConfig.noEntityShaderPromotion = opts.noEntityShaderPromotion;
    benchConfig.textureMemoryBytesOverride = opts.textureMemoryBytes;

#ifdef EQT_HAS_GLES2
    // P52: must enable before loadZone() creates the driver (capability
    // probe runs once at driver init -- see gpu_sync_probe.h).
    if (opts.gpuSyncProbe) eqtEnableGPUSyncProbe(true);
#endif

    auto loadT0 = std::chrono::steady_clock::now();
    auto data = ZoneBench::loadZone(benchConfig);
    auto loadT1 = std::chrono::steady_clock::now();
    if (!data) {
        fprintf(stderr, "ERROR: Failed to load zone\n");
        return 1;
    }
    if (opts.reportLoadTime) {
        double loadMs = std::chrono::duration<double, std::milli>(loadT1 - loadT0).count();
        printf("LOAD_PHASE_MS=%.2f\n", loadMs);
    }

    // P49 (refactor/P49_drm_fail_loud.md, P43 follow-up): P43 root-caused
    // every historical Pi "400x300" run to a DRM mode fallback -- the
    // requested resolution had no exact match among the connector's
    // advertised modes, so CIrrDeviceFB silently substituted the
    // preferred mode (2560x1440) and overwrote Width/Height, with nobody
    // noticing because nothing compared achieved vs requested. Compare
    // here, right after device creation, before any frame is measured:
    // refuse to run (like a hard assertion failure, not a warning) unless
    // --allow-mode-fallback opts in, and ALWAYS record the achieved
    // resolution into the CSV (setActualResolution() below) so a dataset
    // can never again be silently mislabeled even when the caller does
    // pass --allow-mode-fallback.
    int actualWidth = static_cast<int>(data->driver->getScreenSize().Width);
    int actualHeight = static_cast<int>(data->driver->getScreenSize().Height);
    if (actualWidth != opts.width || actualHeight != opts.height) {
        fprintf(stderr,
                "%s: achieved resolution %dx%d != requested %dx%d -- the display "
                "did not have an exact matching mode and the device silently fell "
                "back (see docker/irrlicht-drm/CIrrDeviceFB.cpp DRM mode-fallback "
                "log line above, and refactor/P43_res_visibility_root_cause.md).\n",
                opts.allowModeFallback ? "WARNING" : "ERROR",
                actualWidth, actualHeight, opts.width, opts.height);
        if (!opts.allowModeFallback) {
            fprintf(stderr,
                    "Refusing to run at a silently-substituted resolution "
                    "(pass --allow-mode-fallback to run anyway; the CSV will "
                    "still record the ACTUAL %dx%d resolution either way).\n",
                    actualWidth, actualHeight);
            return 1;
        }
    }
    // metrics.setActualResolution(actualWidth, actualHeight) is called once
    // `metrics` is declared, below.

#ifdef EQT_HAS_GLES2
    if (opts.glCounters) eqtEnableGLCallCounters(true);
#endif

    // P09 Phase B: build the cross-region same-material merge buffers once,
    // right after zone geometry load, before any draw. No-op cost when both
    // flags are off (function not called at all). P09c Phase C's
    // baked-unmerged mode reuses the exact same baked buffers (see
    // renderBakedUnmergedDrawEntries), so it needs them built too.
    if (opts.mergeZoneDraws || opts.bakedUnmergedZoneDraws) {
        ZoneBench::buildMergedZoneBuffers(*data);
    }

    // Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
    // if (opts.analyzePortals) {
    //     ZoneBench::analyzePortals(*data, opts.portalCsvFile);
    //     return 0;
    // }
    // if (opts.detectPortals) {
    //     ZoneBench::detectPortalsFromOccluders(*data, opts.portalCsvFile);
    //     return 0;
    // }

    // ── P47: magenta entity viewer ──
    if (opts.entityViewer) {
        printf("=== Entity Viewer (P47): race=%d mode=%s frame=%d out-dir=%s ===\n",
               opts.entityViewerRace, opts.entityViewerMode.c_str(), opts.entityViewerFrame,
               opts.entityViewerOutDir.c_str());
        ZoneBench::EntityViewerConfig evCfg;
        evCfg.raceId = static_cast<uint16_t>(opts.entityViewerRace);
        evCfg.raceLabel = opts.entityViewerRaceLabel;
        evCfg.modeLabel = opts.entityViewerMode;
        evCfg.animFrame = opts.entityViewerFrame;
        evCfg.outDir = opts.entityViewerOutDir;

        auto results = ZoneBench::runEntityViewer(*data, evCfg);
        if (results.empty()) {
            fprintf(stderr, "Entity viewer produced no results (see ENTITY_VIEWER ERROR above)\n");
            return 1;
        }
        int failures = 0;
        for (const auto& r : results) {
            printf("ENTITY_VIEWER_RESULT race=%d mode=%s angle=%s magenta_in_bbox=%d "
                   "bbox=%d,%d,%d,%d png=%s ok=%d\n",
                   opts.entityViewerRace, evCfg.modeLabel.c_str(), r.angleName.c_str(),
                   r.magentaInBbox, r.bboxX0, r.bboxY0, r.bboxX1, r.bboxY1,
                   r.pngPath.c_str(), r.ok ? 1 : 0);
            if (!r.ok) failures++;
        }
        printf("Entity viewer complete: %zu angle(s), %d failure(s).\n", results.size(), failures);
        return failures > 0 ? 1 : 0;
    }

    // ── Tree test: render one tree, screenshot, exit ──
    if (opts.treeTest) {
        return runTreeTest(data, opts);
    }

    // ── Composite entity test mode ──
    if (opts.compositeEntityTest) {
        return runCompositeEntityTest(data, opts);
    }

    // ── Find a good surface-level position ──
    // T242: ranked candidates -- the first is the classic "surface position"
    // (largest surface-level region); the rest are navmesh-seed fallbacks.
    auto surfaceCandidates = ZoneBench::findSurfaceCandidates(*data, 16);
    float surfX = surfaceCandidates.front().x;
    float surfY = surfaceCandidates.front().y;
    float surfZ = surfaceCandidates.front().z;
    printf("  Surface position: (%.1f, %.1f, %.1f) [%d polys in region]\n",
           surfX, surfY, surfZ, surfaceCandidates.front().polys);

    // ── Set up camera path ──
    ZoneBench::CameraPath camera;
    std::vector<PathFilePose> loadedPath;   // Q01: populated only in --path-file replay
    bool usingPathFile = !opts.pathFile.empty();

    if (usingPathFile) {
        // Q01: replay -- replaces navmesh/orbit path generation entirely.
        if (!loadPathFile(opts.pathFile, loadedPath)) {
            return 1;
        }
        if (opts.frames > static_cast<int>(loadedPath.size())) {
            fprintf(stderr,
                    "ERROR: --frames %d exceeds --path-file '%s' length (%zu poses)\n",
                    opts.frames, opts.pathFile.c_str(), loadedPath.size());
            return 1;
        }
        if (opts.frames < static_cast<int>(loadedPath.size())) {
            fprintf(stderr,
                    "WARNING: --frames %d < --path-file '%s' length (%zu poses); "
                    "replaying prefix (first %d poses)\n",
                    opts.frames, opts.pathFile.c_str(), loadedPath.size(), opts.frames);
        }
        printf("Replaying camera path from %s (%d of %zu poses)\n",
               opts.pathFile.c_str(), opts.frames, loadedPath.size());
    } else {
        // Try navmesh first
        bool hasNavPath = false;
        if (!opts.navmeshPath.empty()) {
            std::string navFile = opts.navmeshPath;
            if (!navFile.empty() && navFile.back() != '/') navFile += '/';
            navFile += opts.zoneName + ".nav";
            std::vector<glm::vec3> seeds;
            for (const auto& c : surfaceCandidates) seeds.emplace_back(c.x, c.y, c.z);
            hasNavPath = camera.generateFromNavmesh(navFile, opts.navWaypoints, opts.seed, seeds);
        }

        // Fallback to orbit at surface level
        if (!hasNavPath) {
            float cx, cy, cz;
            float extent = std::max(data->boundsMaxX - data->boundsMinX,
                                    data->boundsMaxY - data->boundsMinY);
            float radius = (opts.orbitRadius > 0.0f) ? opts.orbitRadius : extent * 0.3f;  // Orbit at 30% of zone extent

            if (opts.orbitAnchorSurface) {
                // H02: anchor to the densest surface-level region instead of the
                // raw bounding-box center — on tall/uneven zones (caves, tiered
                // outdoor terrain) the bbox-center + fixed-height orbit can put
                // the camera in empty sky or embedded rock for the whole path.
                cx = surfX;
                cy = surfY;
                cz = surfZ + opts.orbitHeightOffset;
            } else {
                cx = (data->boundsMinX + data->boundsMaxX) * 0.5f;
                cy = (data->boundsMinY + data->boundsMaxY) * 0.5f;
                cz = surfZ;
            }
            // Q01 (deferred.md "Benchmark camera path is not reproducible
            // across builds" / P06 review): this fallback used to be silent
            // unless you already knew to look for it. Make it loud -- name
            // the flag that avoids it.
            fprintf(stderr,
                    "WARNING: no navmesh path available (opts.navmeshPath=%s%s); "
                    "falling back to a synthetic ORBIT camera path, not the real "
                    "navmesh-driven path. Pass --navmesh-path /mnt/projects/maps/nav "
                    "(or wherever the zone's .nav file lives) for the real moving path.\n",
                    opts.navmeshPath.empty() ? "<empty>" : opts.navmeshPath.c_str(),
                    opts.navmeshPath.empty() ? "" : ", load failed");
            printf("Using orbit fallback: center=(%.0f,%.0f,%.0f) radius=%.0f%s\n",
                   cx, cy, cz, radius, opts.orbitAnchorSurface ? " [surface-anchored]" : "");
            camera.generateOrbit(cx, cy, cz, radius, 10.0f);
        }

        if (camera.empty()) {
            fprintf(stderr, "ERROR: Failed to generate camera path\n");
            return 1;
        }
    }

    // ── Train mode: override the camera BEFORE the trace is resolved ──
    // (must happen before the Q01 trace build below, since the trace reads
    // whatever `camera` ends up being). Unsupported combined with
    // --path-file: the file already fixed the trajectory, so there is
    // nothing left for trainMode to override -- reject that combination
    // explicitly rather than silently ignoring one of the two flags.
    if (opts.trainMode && !data->entities.empty()) {
        if (usingPathFile) {
            fprintf(stderr, "ERROR: --train is not supported with --path-file\n");
            return 1;
        }
        glm::vec3 trainPos(surfX, surfY, surfZ);
        ZoneBench::placeEntitiesTrain(*data, trainPos);
        camera = ZoneBench::CameraPath();
        camera.generateApproach(surfX, surfY, surfZ, 250.0f, 6.0f);
        printf("Train mode: %d entities at (%.0f,%.0f,%.0f), approach from 250 units\n",
               opts.entityCount, surfX, surfY, surfZ);
    }

    // ── Q01: resolve the per-frame trace (warmup + measured) ──
    // See the "Q01: moving camera-path record/replay" comment block above
    // main() for why this exists: one array, built once, that both entity
    // placement and the render loop read from, so record and replay share
    // exactly one notion of "what pose does frame N have". Not used for
    // --camera-pose/--pose-search/--synthetic-draws/--tree-test, which
    // return earlier or don't touch the moving path.
    int totalFrames = opts.warmup + opts.frames;
    std::vector<TracePose> fullTrace;
    fullTrace.reserve(totalFrames);
    if (usingPathFile) {
        TracePose hold;
        hold.pos = glm::vec3(loadedPath[0].x, loadedPath[0].y, loadedPath[0].z);
        hold.heading = loadedPath[0].heading;
        hold.pitch = loadedPath[0].pitch;
        poseToDirection(hold.heading, hold.pitch, hold.dir.x, hold.dir.y, hold.dir.z);
        // Q01 design point A4: the path file has no warmup poses (it only
        // records rendered MEASURED frames), so warmup holds pose[0]. Warmup
        // frames are never measured/dumped, so this only needs to prime GPU
        // state/caches, not reproduce a specific trajectory.
        for (int i = 0; i < opts.warmup; ++i) fullTrace.push_back(hold);
        for (int i = 0; i < opts.frames; ++i) {
            const auto& lp = loadedPath[i];
            TracePose tp;
            tp.pos = glm::vec3(lp.x, lp.y, lp.z);
            tp.heading = lp.heading;
            tp.pitch = lp.pitch;
            poseToDirection(tp.heading, tp.pitch, tp.dir.x, tp.dir.y, tp.dir.z);
            fullTrace.push_back(tp);
        }
    } else {
        float dt = 1.0f / 30.0f;
        float pathDist = 0.0f;
        for (int f = 0; f < totalFrames; ++f) {
            pathDist += opts.speed * dt;
            TracePose tp;
            tp.pos = camera.getPositionAt(pathDist);
            tp.dir = camera.getDirectionAt(pathDist);
            // Same convention as the CSV columns: heading 0=North/+Y, CW; pitch positive=up.
            tp.heading = atan2f(tp.dir.x, tp.dir.y) * 180.0f / 3.14159265f;
            if (tp.heading < 0) tp.heading += 360.0f;
            float horizLen = sqrtf(tp.dir.x * tp.dir.x + tp.dir.y * tp.dir.y);
            tp.pitch = atan2f(tp.dir.z, horizLen) * 180.0f / 3.14159265f;
            fullTrace.push_back(tp);
        }
    }

    if (!opts.dumpPathFile.empty()) {
        std::vector<TracePose> measured(fullTrace.begin() + opts.warmup, fullTrace.end());
        writePathFile(opts.dumpPathFile, opts, measured);
    }

    // ── Place entities along path ──
    // (trainMode already placed its entities above, before the trace build.)
    if (!data->entities.empty() && !opts.trainMode) {
        // Q01: sample from the resolved MEASURED-frame trace (what will
        // actually be rendered / what a --path-file replay has available),
        // not the raw navmesh polyline's own arc length -- the latter has no
        // equivalent once the navmesh isn't available (replay). This changes
        // exactly where entities land relative to pre-Q01 runs (see design
        // point A3), but is the only way replay can reproduce placement
        // without the navmesh.
        std::vector<glm::vec3> pathPositions;
        pathPositions.reserve(opts.frames);
        for (int i = 0; i < opts.frames; ++i) pathPositions.push_back(fullTrace[opts.warmup + i].pos);
        ZoneBench::placeEntitiesAlongPath(*data, pathPositions);
    }

    // ── Configure adaptive LOD budget ──
    data->adaptiveLodConfig.maxEntitiesPerScene = opts.entityBudget;

    // ── Configure adaptive lighting ──
    data->adaptiveLighting = opts.adaptiveLighting;
    data->perPixelEntityBudget = opts.perPixelBudget;
    data->perPixelZoneBudget = opts.perPixelZoneBudget;
    data->zonePPAreaMode = opts.zonePPAreaMode;
    data->zonePPAreaBudgetFactor = opts.zonePPAreaBudgetFactor;
    data->zonePPAreaFloor = opts.zonePPAreaFloor;
    data->zonePPRadiusMode = opts.zonePPRadiusMode;
    data->zonePPRadiusMargin = opts.zonePPRadiusMargin;
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

    // ── H02b: interesting-view search (opt-in, bless-time diagnostic only) ──
    // Never used by the CI gate; run manually to find candidate poses, then
    // hard-code the winners as --camera-pose flags in the manifest.
    if (opts.poseSearch > 0) {
        return runPoseSearch(data, opts);
    }

    // ── Run synthetic test if requested ──
    if (opts.syntheticDraws) {
        runSyntheticDrawTest(*data, opts.frames);
        return 0;
    }

    // ── P09 A-gate: cross-region merge primitive probe ──
    if (opts.mergeProbe) {
        ZoneBench::runMergeProbeTest(*data, opts.frames, opts.mergeProbeCsv.c_str());
        return 0;
    }

    ZoneBench::MetricsCollector metrics;
    metrics.setActualResolution(actualWidth, actualHeight);

    // ── H02b: static multi-pose capture ──
    // When --camera-pose was given (one or more times), skip the moving
    // navmesh/orbit camera path entirely: render exactly one frame per pose,
    // pose index i -> frame index i, which lines up 1:1 with the existing
    // --dump-frames / manifest "designatedFrames" mechanism below with no
    // changes needed to it or to the CI gate script.
    if (!opts.cameraPoses.empty()) {
        return runStaticPoseCapture(data, opts, metrics);
    }

    // ── Benchmark loop ──
    printf("\nStarting benchmark: %d warmup + %d measured frames\n",
           opts.warmup, opts.frames);

    metrics.reserve(opts.frames);
    metrics.setDumpPerFrame(opts.dump);

    std::vector<ZoneBench::DrawEntry> drawEntries;
    drawEntries.reserve(2048);

    // P08: draw-composition histogram (--draw-histogram + --dump-frames).
    FILE* histFile = nullptr;
    bool histHeaderWritten = false;
    if (!opts.drawHistogramFile.empty()) {
        histFile = fopen(opts.drawHistogramFile.c_str(), "w");
        if (!histFile) {
            fprintf(stderr, "ERROR: cannot open --draw-histogram file: %s\n",
                    opts.drawHistogramFile.c_str());
        }
    }

    // Q01: totalFrames/fullTrace were resolved above (before entity
    // placement) -- both record (navmesh/orbit) and replay (--path-file)
    // populate the same per-frame trace, so the loop below no longer cares
    // which mode it's in.
    // T491 lifecycle points: "ready" before the first frame, "steady" after
    // the last, both to stdout in the same [memledger] schema as the client.
    printf("%s\n", eqt::memory::toLogLine(eqt::memory::snapshot("ready")).c_str());
    fflush(stdout);
    eqt::memory::setThreadCategory(eqt::memory::Category::IrrlichtScene);
    const uint64_t ledgerAllocsBeforeFrames = eqt::memory::snapshot("frames").allocCount;
    for (int frame = 0; frame < totalFrames; ++frame) {
        bool measuring = (frame >= opts.warmup);

        // Camera pose for this frame, resolved up front (Q01).
        const TracePose& tp = fullTrace[frame];
        glm::vec3 camPos = tp.pos;
        glm::vec3 camDir = tp.dir;

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
            metrics.beginFrame(frame - opts.warmup, camPos.x, camPos.y, camPos.z,
                               tp.heading, tp.pitch);
#ifdef EQT_HAS_GLES2
            if (opts.glCounters) eqtResetGLCallCounters();
#endif
        }

        // Tick entity animations (skeletal skinning)
        // Only animate entities within budget, sorted front-to-back,
        // and only if they pass visibility (render distance + frustum).
        if (opts.animate && !data->animatedNodes.empty()) {
            if (measuring) metrics.beginAnimation();

            // Build distance-sorted index list (front to back)
            struct EntityDist {
                int index;
                float distSq;
            };
            static std::vector<EntityDist> sortedEntities;
            sortedEntities.clear();
            sortedEntities.reserve(data->entities.size());

            float renderDistSq = opts.entityRenderDist * opts.entityRenderDist;
            for (int i = 0; i < static_cast<int>(data->entities.size()); ++i) {
                if (!data->animatedNodes[i]) continue;
                auto& ent = data->entities[i];
                float dx = ent.eqX - camPos.x;
                float dy = ent.eqY - camPos.y;
                float dz = ent.eqZ - camPos.z;
                float dSq = dx*dx + dy*dy + dz*dz;

                // Render distance cull
                if (dSq > renderDistSq) continue;

                // Frustum cull
                if (data->frustumCuller && data->frustumCuller->isEnabled()) {
                    if (!data->frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                        continue;
                }

                sortedEntities.push_back({i, dSq});
            }

            // Sort front to back
            std::sort(sortedEntities.begin(), sortedEntities.end(),
                      [](const EntityDist& a, const EntityDist& b) {
                          return a.distSq < b.distSq;
                      });

            // Animate only up to entity budget
            int animBudget = opts.entityBudget;
            int animCount = std::min(static_cast<int>(sortedEntities.size()), animBudget);
            irr::u32 simTimeMs = static_cast<irr::u32>(frame * 33.3f);

            for (int j = 0; j < animCount; ++j) {
                int i = sortedEntities[j].index;
                data->animatedNodes[i]->OnAnimate(simTimeMs);
            }

            if (measuring) metrics.endAnimation();

            // Update entity mesh pointers to use animated instance meshes
            // (only for entities that were actually animated this frame)
            for (int j = 0; j < animCount; ++j) {
                int i = sortedEntities[j].index;
                auto* instMesh = data->animatedNodes[i]->getInstanceMesh();
                if (instMesh) {
                    data->entities[i].mesh = instMesh;
                }
            }
        }

        // Collect draw entries with visibility culling
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);
        ZoneBench::CameraState camState{camPos.x, camPos.y, camPos.z,
                                         camDir.x, camDir.y, camDir.z,
                                         fov, aspect};
        int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
        float lightRadius = 0.0f;
        int regionsDemotedRadius = 0;
        // P08: phaseCollectCullMs accumulates zone-region collection/culling
        // (from collectDrawEntries' own timing) plus object/entity draw-entry
        // collection below (small relative to zone collection); phaseSortMs
        // is the zone material-key sort only; phaseSubmitMs (set per-branch
        // below) is the setMaterial+drawMeshBuffer loop time.
        float phaseCollectCullMs = 0.0f, phaseSortMs = 0.0f, phaseSubmitMs = 0.0f;
        int measuredFrameIdx = frame - opts.warmup;
        bool wantHistogram = measuring && histFile != nullptr && !opts.dumpFrames.empty() &&
            std::find(opts.dumpFrames.begin(), opts.dumpFrames.end(), measuredFrameIdx)
                != opts.dumpFrames.end();
        std::vector<int> histDrawsPerRegion, histPolysPerDraw;
        ZoneBench::collectDrawEntries(
            *data, camState,
            opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
            opts.frontToBack, opts.maxDraws, opts.maxPolys,
            drawEntries, regionsVisible, regionsDrawn, regionsOccluded,
            &lightRadius, &regionsDemotedRadius,
            &phaseCollectCullMs, &phaseSortMs,
            wantHistogram ? &histDrawsPerRegion : nullptr,
            wantHistogram ? &histPolysPerDraw : nullptr);
        if (measuring) metrics.setRadiusStats(lightRadius, regionsDemotedRadius);
        if (wantHistogram) {
            ZoneBench::DrawHistogram hist;
            hist.frame = measuredFrameIdx;
            hist.regionsVisible = regionsVisible;
            hist.regionsDrawn = regionsDrawn;
            hist.regionsOccluded = regionsOccluded;
            hist.stateChanges = 0;  // zone-only state changes not separately tracked at this point
            hist.drawsPerRegion = histDrawsPerRegion;
            hist.polysPerDraw = histPolysPerDraw;
            ZoneBench::writeDrawHistogramCSV(histFile, hist, !histHeaderWritten);
            histHeaderWritten = true;
            if (opts.dump) ZoneBench::printDrawHistogram(hist);

            // P08 design point 6: occlusion headroom ground truth (uncapped
            // occluder budget vs production's capped pass) at the same
            // pinned poses -- one-shot, printed to stdout for log capture.
            ZoneBench::OcclusionHeadroom headroom = ZoneBench::computeOcclusionHeadroomUncapped(
                *data, camState, opts.pvsCull, opts.frustumCull, regionsOccluded);
            printf("[occlusion-headroom] frame=%d regions_pvs=%d occluded_capped=%d "
                   "occluded_uncapped=%d headroom=%d\n",
                   measuredFrameIdx, headroom.regionsVisiblePVS,
                   headroom.regionsOccludedCapped, headroom.regionsOccludedUncapped,
                   headroom.regionsOccludedUncapped - headroom.regionsOccludedCapped);
        }

        // Collect object draw entries (trees, lamps, etc.)
        int objectDrawCalls = 0, objectPolys = 0;
        if (!data->objects.empty()) {
            const auto objCollectStart = std::chrono::steady_clock::now();
            ZoneBench::collectObjectDrawEntries(
                *data, camState, opts.objectRenderDist,
                opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                drawEntries, objectDrawCalls, objectPolys);
            phaseCollectCullMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - objCollectStart).count();
        }

        // P52: proxy-scope GPU query around the combined zone+entity draw
        // submission below -- see gpu_sync_bench_hooks.h for why this tool
        // can't reproduce the production zone-pass/entity-pass split.
#ifdef EQT_HAS_GLES2
        ZoneBench::gpuSyncBeginSubmissionPhase();
#endif

        // Collect and render entities
        int entityDrawCalls = 0, entityPolys = 0;
        if (!data->entities.empty()) {
            if (opts.directGLEntities && opts.compositeEntities) {
                // Direct GL + composite: 1 draw per entity via raw GL.
                // P08: out of scope (entity draw path) -- phaseSubmitMs here
                // covers only the zone-geometry renderDrawEntries() call;
                // the direct-GL entity submission below falls into otherMs.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderDirectGLCompositeEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.directGLEntities) {
                // Direct GL per-body-part: bypass Irrlicht, same draw count as unbatched.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderDirectGLEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.adaptiveLod) {
                // Adaptive LOD: proactive frame-time prediction with billboard fallback.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                int tier1 = 0, tier2 = 0;
                float entityMs = 0;
                float prevFrameMs = metrics.frames().empty() ? 0 :
                    metrics.frames().back().totalMs;
                entityDrawCalls = ZoneBench::renderAdaptiveLodEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    prevFrameMs, entityPolys, tier1, tier2, entityMs);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.lodEntities) {
                // LOD: composite near + batched billboards far.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                int tier1 = 0, tier2 = 0;
                entityDrawCalls = ZoneBench::renderLodEntities(
                    *data, camState, opts.entityRenderDist, opts.lodTier2Dist,
                    opts.frustumCull, entityPolys, tier1, tier2);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.compositeEntities && !opts.compositeSceneGraph) {
                // Composite: 1 draw per entity (simulates pre-baked composite texture).
                // P08: out of scope (entity draw path), same caveat as above.
                // P08D-RETAKE: --composite-scenegraph skips this branch (see opts struct
                // comment) so real composite meshes fall through to the default entity
                // path below instead of this custom stand-in draw.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderCompositeEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else if (opts.batchEntities) {
                // Batched: merge same-material entity parts into single draws.
                // P08: out of scope (entity draw path), same caveat as above.
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest, &phaseSubmitMs);
                entityDrawCalls = ZoneBench::renderBatchedEntities(
                    *data, camState, opts.entityRenderDist, opts.frustumCull,
                    entityPolys);
                int polyCount = countPolygons(drawEntries) + entityPolys;
                int totalDraws = static_cast<int>(drawEntries.size()) + entityDrawCalls;
                int totalStateChanges = stateChangesZone + entityDrawCalls;

                if (measuring) {
                    metrics.endDraw(totalDraws, totalStateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.finalizePhaseTiming();
                }
            } else {
                // Unbatched (P08 canonical config -- default, no entity mode
                // flag set): entity draw entries are appended to the SAME
                // drawEntries vector as zone entries (sorted together), so
                // the single renderDrawEntries() call below submits zone +
                // entity draws combined -- phaseSubmitMs is a true combined
                // submission-phase number here, not zone-only.
                const auto entCollectStart = std::chrono::steady_clock::now();
                int entitiesVisible = 0;
                ZoneBench::collectEntityDrawEntries(
                    *data, camState, opts.entityRenderDist,
                    opts.frustumCull, opts.occlusionCull,
                    drawEntries, entityDrawCalls, entityPolys, entitiesVisible);
                phaseCollectCullMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - entCollectStart).count();
                int actualDraws = 0;
                int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest,
                                                      &phaseSubmitMs, opts.mergeZoneDraws, &actualDraws,
                                                      opts.bakedUnmergedZoneDraws);
                int polyCount = countPolygons(drawEntries);

                if (measuring) {
                    metrics.endDraw(actualDraws,
                                   stateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
                    metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                    metrics.setEntitiesVisible(entitiesVisible);
                    metrics.finalizePhaseTiming();
                }
            }
        } else {
            // No entities — zone only (P08 canonical isolation config, used
            // to quantify entity draw-call contribution separately).
            int actualDraws = 0;
            int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest,
                                                  &phaseSubmitMs, opts.mergeZoneDraws, &actualDraws,
                                                  opts.bakedUnmergedZoneDraws);
            int polyCount = countPolygons(drawEntries);

            if (measuring) {
                metrics.endDraw(actualDraws,
                               stateChanges, polyCount,
                               regionsVisible, regionsDrawn);
                metrics.setPhaseTiming(phaseCollectCullMs, phaseSortMs, phaseSubmitMs);
                metrics.finalizePhaseTiming();
            }
        }

#ifdef EQT_HAS_GLES2
        ZoneBench::gpuSyncEndSubmissionPhase();
#endif

        // P38: snapshot driver GL call counters before glFinish() -- they
        // count submission-phase glBindTexture/glTexParameteri calls, not
        // GPU-side work, so must be read before the sync point below.
#ifdef EQT_HAS_GLES2
        if (measuring && opts.glCounters) {
            metrics.setGLCounters(eqtGetGLTexBinds(), eqtGetGLTexParams());
        }
#endif

        // GPU sync. P52: glFinish() is exactly the "CPU blocks on GPU sync"
        // point the plan names; timed only when measuring and the probe is on.
#ifdef EQT_HAS_GLES2
        ZoneBench::gpuSyncTimedFinish(measuring && opts.gpuSyncProbe, glFinish);
#endif

        if (measuring) {
            metrics.endGpuSync();
            metrics.endFrame();  // Measure before endScene (vsync page flip blocks)
        }

        // H01: dump the last frame as a PNG for the x86/Xvfb verification harness.
        // Must read the framebuffer before endScene()'s swap/page-flip.
        if (!opts.dumpPngPath.empty() && frame == totalFrames - 1) {
            irr::video::IImage* shot = data->driver->createScreenShot();
            if (shot) {
                bool ok = data->driver->writeImageToFile(shot, opts.dumpPngPath.c_str());
                shot->drop();
                printf("[dump-png] %s: %s\n", opts.dumpPngPath.c_str(),
                       ok ? "saved" : "FAILED to save");
            } else {
                printf("[dump-png] %s: createScreenShot() FAILED\n", opts.dumpPngPath.c_str());
            }
        }

        // H02: dump designated measured frames (golden-image CI gate) as
        // PREFIX_f<N>.png. Same read-before-swap requirement as --dump-png.
        if (measuring && !opts.dumpPngPrefix.empty() && !opts.dumpFrames.empty()) {
            int measuredFrame = frame - opts.warmup;
            if (std::find(opts.dumpFrames.begin(), opts.dumpFrames.end(), measuredFrame)
                != opts.dumpFrames.end()) {
                irr::video::IImage* shot = data->driver->createScreenShot();
                std::string path = opts.dumpPngPrefix + "_f" + std::to_string(measuredFrame) + ".png";
                if (shot) {
                    bool ok = data->driver->writeImageToFile(shot, path.c_str());
                    shot->drop();
                    printf("[dump-frames] %s: %s\n", path.c_str(), ok ? "saved" : "FAILED to save");
                } else {
                    printf("[dump-frames] %s: createScreenShot() FAILED\n", path.c_str());
                }
            }
        }

        // P08: time endScene() (buffer swap / page flip) separately -- it
        // happens after totalMs is already stamped (comment above), so this
        // is genuinely new information, not a subset of any existing column.
        // Patches the frame record already pushed by endFrame() above.
        const auto swapStart = std::chrono::steady_clock::now();
#ifdef EQT_HAS_GLES2
        // P52: GPUSyncPhase::Swap query + CPU-side stats, see gpu_sync_bench_hooks.h.
        ZoneBench::gpuSyncWrapSwap([&]() { data->driver->endScene(); },
                                    metrics, measuring, opts.gpuSyncProbe);
#else
        data->driver->endScene();
#endif
        if (measuring) {
            float swapMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - swapStart).count();
            metrics.setSwapMsForLastFrame(swapMs);
        }

        // Progress
        if (frame > 0 && frame % 100 == 0) {
            printf("  Frame %d/%d...\n", frame, totalFrames);
        }
    }

    // ── Output results ──
    metrics.printSummary();
    {
        const auto steady = eqt::memory::snapshot("steady");
        printf("%s\n", eqt::memory::toLogLine(steady).c_str());
        if (totalFrames > 0) {
            // T491: allocation rate of the frame loop, for the ledger-overhead
            // arithmetic (allocs/frame x measured ns/alloc).
            printf("[memledger] frame_allocs total=%llu frames=%d per_frame=%.1f\n",
                   static_cast<unsigned long long>(steady.allocCount - ledgerAllocsBeforeFrames),
                   totalFrames,
                   static_cast<double>(steady.allocCount - ledgerAllocsBeforeFrames) / totalFrames);
        }
        fflush(stdout);
    }

    if (!opts.csvFile.empty()) {
        metrics.writeCSV(opts.csvFile);
    }

    if (histFile) {
        fclose(histFile);
        printf("Draw histogram written: %s\n", opts.drawHistogramFile.c_str());
    }

    // H02/T69: report/emit the full per-subsystem memory breakdown for the
    // memory-ceiling CI gate (see the block comment above computeMemorySnapshot()).
    if (data->textureCache) {
        BenchMemorySnapshot snap = computeMemorySnapshot(*data, opts.width, opts.height);
        printf("Texture cache: %zu / %zu bytes (%zu textures)\n",
               snap.textureCacheBytesUsed, snap.textureCacheBytesLimit, snap.textureCacheCount);
        printf("Total tracked: %zu bytes | RSS: %zu bytes | untracked: %zu bytes\n",
               snap.totalEstimate, snap.processRssBytes, snap.untrackedBytes);
        if (!opts.memoryJsonPath.empty()) {
            writeMemoryJson(opts.memoryJsonPath, opts.zoneName, snap);
        }
    }

    printf("Benchmark complete.\n");
    return 0;
}
