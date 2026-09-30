// zone_bench_options.cpp — gles2_zone_benchmark's --help text and argv parser.
//
// T387: printUsage() and parseArgs() extracted verbatim from
// gles2_zone_benchmark.cpp (3,060 lines, over the 1,500 ceiling; board B167).
// struct Options is in zone_bench_options.h. `static` dropped from both
// definitions — they now have an external caller — and nothing else changed,
// so `--help` is byte-identical.

#include "zone_bench_options.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void printUsage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Zone options:\n");
    printf("  --zone NAME       Zone name (default: qeynos2)\n");
    printf("  --eq-path PATH    Path to EQ client directory (required)\n");
    printf("  --eqe1-path PATH  Path to EQE1 texture directory\n");
    printf("  --report-load-time  Print LOAD_PHASE_MS=<ms> after loadZone() (P03)\n");
    printf("  --navmesh-path P  Path to navmesh directory (e.g. /mnt/projects/maps/nav).\n");
    printf("                    Moving-path runs (no --camera-pose/--pose-search) need this\n");
    printf("                    to drive the camera along real navmesh waypoints; WITHOUT it\n");
    printf("                    (or if the zone's .nav file fails to load) the benchmark\n");
    printf("                    silently fell back to a synthetic orbit camera -- Q01 makes\n");
    printf("                    this loud: an unmissable stderr WARNING is printed naming\n");
    printf("                    the fallback and this flag.\n");
    printf("\nDisplay options:\n");
    printf("  --drm             Use DRM/KMS framebuffer\n");
    printf("  --x11             Use X11/EGL\n");
    printf("  --res W H         Resolution (default: 800 600)\n");
    printf("  --allow-mode-fallback  Run even if the achieved resolution != --res "
           "(DRM mode fallback, P43/P49); default: refuse. CSV always records the "
           "ACTUAL resolution regardless (actual_width/actual_height columns).\n");
    printf("\nBenchmark options:\n");
    printf("  --frames N        Frames to measure (default: 300)\n");
    printf("  --warmup N        Warmup frames (default: 10)\n");
    printf("  --speed F         Camera speed EQ units/sec (default: 100)\n");
    printf("  --seed N          Random seed for navmesh path (default: 42)\n");
    printf("  --waypoints N     Navmesh waypoints (default: 20)\n");
    printf("  --csv FILE        Write per-frame CSV\n");
    printf("\nA/B toggle flags:\n");
    printf("  --no-material-sort   Disable material-key draw sorting\n");
    printf("  --no-pvs             Disable PVS bitvector culling\n");
    printf("  --no-frustum-cull    Disable frustum culling\n");
    printf("  --no-occlusion-cull  Disable software occlusion culling\n");
    printf("  --no-front-to-back   Disable distance sorting\n");
    printf("  --no-depth-test      Disable GL depth test\n");
    printf("  --max-draws N        Cap draw calls per frame\n");
    printf("  --max-polys N        Cap polygons per frame\n");
    printf("  --lighting N         Point light count (0-8)\n");
    printf("  --player-light-level N  Player lightstone level 0-10, mirrors production's\n");
    printf("                       renderer_entities.cpp lightstone formula exactly\n");
    printf("                       (radius/color/attenuation); 0 = no player light\n");
    printf("                       (default: 10, max-level lightstone)\n");
    printf("\nEntity options:\n");
    printf("  --entities N         Place N static entities along path (default: 0)\n");
    printf("  --entity-dist F      Entity render distance (default: 300)\n");
    printf("  --batch-entities     Batch same-material entity parts into single draws\n");
    printf("  --composite-entities Simulate 1-draw-per-entity (composite texture)\n");
    printf("  --direct-gl          Bypass Irrlicht for entities (direct GL calls)\n");
    printf("  --lod-entities       LOD tiers: composite near + batched billboards far\n");
    printf("  --adaptive-lod       Predictive LOD: billboard excess entities to stay in budget\n");
    printf("  --occluder-budget N  Software-occlusion region budget (maxOccluderRegions); N or \"uncapped\" (default: resolved from the loader's ConstrainedRendererConfig, orangepi preset -> 256, P14/P39-blessed; P42)\n");
    printf("  --gl-counters        P38: record driver glBindTexture/glTexParameteri calls per frame (tex_binds/tex_param_calls CSV columns; off by default, zero overhead when absent)\n");
    printf("  --gpu-sync-probe     P52: per-phase GPU-sync timer queries + CPU sync-point attribution (gpu_*_ns/swap_buffers_ms/flip_wait_ms/finish_readpixels_ms CSV columns; off by default, zero overhead when absent)\n");
    printf("  --entity-budget N    Max composite entities per frame (default: 55)\n");
    printf("  --adaptive-lighting  Per-pixel lighting near, per-vertex far (zone + entities)\n");
    printf("  --perpixel-budget N  Max entities with per-pixel lighting (default: 15)\n");
    printf("  --zone-pp-budget N   Max zone regions with per-pixel lighting (default: 20)\n");
    printf("  --pp-budget-mode M   count|area|radius (default: count). area accumulates estimated\n");
    printf("                       projected screen area nearest-first until it exceeds\n");
    printf("                       --pp-area-factor * screen area, then lightweight (P06)\n");
    printf("  --pp-area-factor F   Area-mode budget factor k (default: 0.75)\n");
    printf("  --pp-area-floor N    Area-mode: first N nearest regions always per-pixel (P07, default: 1)\n");
    printf("  --pp-radius-margin F Radius-mode: multiplies computed light range R (P07d, default: 1.0)\n");
    printf("  --lod-dist F         Billboard switch distance (default: 80)\n");
    printf("  --train              Cluster entities together (train simulation)\n");
    printf("  --animate            Enable skeletal animation on entities\n");
    printf("  --no-entity-shader-promotion  Skip applyEntityShaderMaterialPromotion() --\n");
    printf("                       entities stay on built-in EMT_SOLID/EMT_TRANSPARENT_ALPHA_CHANNEL_REF\n");
    printf("                       materials (P46c: lighting-neutral bless capture). Default off.\n");
    printf("\nMagenta entity viewer (P47 -- renders ONE race model LARGE against solid\n");
    printf("magenta with no zone/object geometry, at fixed angles; magenta inside the\n");
    printf("silhouette = an alpha gap. Overrides --entities/--test-races/--animate/\n");
    printf("--no-entity-shader-promotion to the proven-correct config for the requested\n");
    printf("leg -- RGBA (no --eqe1-path): promotion off; ETC1 (--eqe1-path set): promotion\n");
    printf("on. See refactor/P47_magenta_entity_viewer.md):\n");
    printf("  --entity-viewer             Enable viewer mode (exits after rendering)\n");
    printf("  --entity-viewer-race N      EQ race ID to render (required)\n");
    printf("  --entity-viewer-race-label S  Cosmetic label for filenames/logs (default: numeric ID)\n");
    printf("  --entity-viewer-frame N     Animation frame index -> pose = N*33.3ms (default: 0)\n");
    printf("  --entity-viewer-out-dir DIR Directory to write PNGs into (required, must exist)\n");
    printf("  --entity-viewer-composite   P48: composite leg -- routes the entity through\n");
    printf("                              the real composite eligibility gate instead of\n");
    printf("                              forcing multi-buffer; requires --eqe1-path\n");
    printf("\nObject options:\n");
    printf("  --objects            Load zone objects (trees, lamps, fences)\n");
    printf("  --object-dist F      Object render distance (default: 300)\n");
    printf("  --object-anim-frame N  B936: pose every keyframe-animated object (grass,\n");
    printf("                       plants, flags) at keyframe N mod its frame count, once,\n");
    printf("                       before upload -- a fixed animation time for pixel\n");
    printf("                       comparisons (default: no keyframe, the geometry pose).\n");
    printf("                       WILLEQ_SHARE_OBJECT_MESHES=0 builds a mesh per placement\n");
    printf("                       (the pre-T504 layout) for an A/B in one binary.\n");
    // Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
    // printf("\nAnalysis modes:\n");
    // printf("  --analyze-portals    Analyze portal geometry contact and exit\n");
    // printf("  --detect-portals     Detect portals from occluder gaps and exit\n");
    // printf("  --portal-csv FILE    Write portal analysis to CSV\n");
    printf("\nOutput:\n");
    printf("  --dump               Print per-frame stats (polys, draws, camera) to stdout\n");
    printf("  --dump-png FILE      Save the last measured frame as a PNG (H01 verification)\n");
    printf("  --dump-frames LIST   Comma-separated measured-frame indices to dump (H02, e.g. 0,30,59)\n");
    printf("  --dump-png-prefix P  With --dump-frames: write P_f<N>.png per listed frame\n");
    printf("  --memory-json FILE   Write texture-cache memory stats (bytes used/limit/count) as JSON (H02)\n");
    printf("  --draw-histogram FILE  With --dump-frames: append one CSV row per listed frame with\n");
    printf("                       zone draw-composition stats (draws/region, polys/draw distribution,\n");
    printf("                       tiny-draw (<32 polys) count -- P08, one-shot, not per-frame)\n");
    printf("\nOrbit-fallback camera anchoring (H02, opt-in, default off):\n");
    printf("  --orbit-anchor-surface  Anchor the orbit-fallback camera to the surface-position\n");
    printf("                          finder's XY instead of the zone bounding-box center\n");
    printf("  --orbit-radius F        Override the orbit radius in EQ units (default: auto, 30%% of extent)\n");
    printf("  --orbit-height-offset F Added to the anchor Z for camera height (default: 6.0)\n");
    printf("\nStatic camera poses (H02b, opt-in -- replaces the moving camera path):\n");
    printf("  --camera-pose X,Y,Z,HEADING,PITCH  Static camera pose (EQ coords, degrees).\n");
    printf("                          Repeatable: one flag per pose. Frame index i in\n");
    printf("                          --dump-frames/CSV corresponds to the i-th --camera-pose.\n");
    printf("\nMoving camera-path record/replay (Q01 -- reproduces the navmesh/orbit path\n");
    printf("across sessions; the CSV already carries pos/heading/pitch per frame, this is\n");
    printf("the replay half):\n");
    printf("  --dump-path FILE     Write the resolved moving-path camera trajectory to FILE:\n");
    printf("                       one line per rendered MEASURED frame (post-warmup),\n");
    printf("                       \"x,y,z,heading,pitch\" (EQ coords + degrees, exactly the\n");
    printf("                       --camera-pose convention). CSV frame row i corresponds to\n");
    printf("                       path line i. Works with navmesh- or orbit-generated paths.\n");
    printf("  --path-file FILE     Replay a camera path dumped by --dump-path, replacing\n");
    printf("                       navmesh/orbit path generation entirely (byte-identical\n");
    printf("                       poses and entity placement vs. the original recording).\n");
    printf("                       Mutually exclusive with --camera-pose/--pose-search.\n");
    printf("                       --frames greater than the path length is an ERROR;\n");
    printf("                       fewer is a prefix replay (stderr warning). Warmup frames\n");
    printf("                       hold pose[0] (path file has no warmup poses).\n");
    printf("\nInteresting-view search (H02b, opt-in, bless-time diagnostic only):\n");
    printf("  --pose-search N         Sample N candidate poses anchored at zone region\n");
    printf("                          bounding boxes, score each, print top scorers\n");
    printf("  --pose-search-headings N  Headings tried per anchor point (default: 4)\n");
    printf("  --pose-search-pitch F   Fixed pitch in degrees (default: -5.0)\n");
    printf("  --pose-search-eye-height F  Added to anchor floor Z (default: 6.0)\n");
    printf("  --pose-search-csv FILE  Write all sampled candidates + scores to CSV\n");
    printf("\nDiagnostic:\n");
    printf("  --tree-test          Render single tree, screenshot, exit\n");
    printf("  --composite-entity-test  Render 1 of each race/gender with composite textures\n");
    printf("  --test-race N            Test specific race ID (1=HUM, 2=BAM, etc.) default: all\n");
    printf("  --test-races R1,R2,...   P08f: place --entities N cycling through these EQ race\n");
    printf("                       IDs instead of the default player-race set {1..12} -- lets\n");
    printf("                       zone-native NPC races (e.g. BAT=34,ORC=17,RAT=35,ZOM=67) be\n");
    printf("                       placed for composite spot-checks. Requires --entities N.\n");
    printf("  --test-variant N         Texture variant (0=naked, 1=cloth, 2=chain, 3=plate, 4=leather)\n");
    printf("\nSynthetic tests:\n");
    printf("  --synthetic-draws    Run draw call overhead isolation test\n");
    printf("\nDiagnostics (H02d):\n");
    printf("  --shader-debug N     Render via ZoneShaderManager's debug visualization\n");
    printf("  --material-counts    Add alpha_test_draws,solid_draws columns to --csv (P01)\n");
    printf("                       program (0=off; see zone_shader.cpp for the mode list,\n");
    printf("                       e.g. 7=alpha, 11=rawTexAlpha, 12=companionAlpha)\n");
    printf("  --lighting-model M   perpixel|pervertex|lightweight (P02, default: perpixel,\n");
    printf("                       i.e. unchanged benchmark behavior). pervertex and\n");
    printf("                       lightweight are the same shader variant on this codebase\n");
    printf("                       (see zone_shader.cpp *_LIGHTWEIGHT_SRC) -- both names\n");
    printf("                       accepted. Implemented as --adaptive-lighting with\n");
    printf("                       --zone-pp-budget forced to 0.\n");
    printf("  --dump-filters N     Print actual GL min/mag texture filters for the first N\n");
    printf("                       draw entries of the first pose (P02 H2 runtime spot-check)\n");
    printf("\nLogging (handled by InitLogging):\n");
    printf("  --log-level=LEVEL    Set log level: NONE, FATAL, ERROR, WARN, INFO, DEBUG, TRACE\n");
    printf("  --log-only=MOD,...   Only show listed modules\n");
    printf("  --log-module=MOD:LVL Per-module override\n");
}

Options parseArgs(int argc, char** argv) {
    Options opts;
    // Q01: captured for --dump-path header provenance ("full generating command").
    for (int i = 0; i < argc; ++i) {
        if (i > 0) opts.fullCommandLine += ' ';
        opts.fullCommandLine += argv[i];
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]); exit(0);
        }
        else if (strcmp(argv[i], "--zone") == 0 && i + 1 < argc) opts.zoneName = argv[++i];
        else if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) opts.eqPath = argv[++i];
        else if (strcmp(argv[i], "--eqe1-path") == 0 && i + 1 < argc) opts.eqe1Path = argv[++i];
        else if (strcmp(argv[i], "--navmesh-path") == 0 && i + 1 < argc) opts.navmeshPath = argv[++i];
        else if (strcmp(argv[i], "--load-mem-audit") == 0) opts.loadMemAudit = true;
        else if (strcmp(argv[i], "--drm") == 0) opts.useDRM = true;
        else if (strcmp(argv[i], "--report-load-time") == 0) opts.reportLoadTime = true;
        else if (strcmp(argv[i], "--x11") == 0) opts.forceX11 = true;
        else if (strcmp(argv[i], "--res") == 0 && i + 2 < argc) {
            opts.width = atoi(argv[++i]); opts.height = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--allow-mode-fallback") == 0) opts.allowModeFallback = true;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) opts.frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) opts.warmup = atoi(argv[++i]);
        else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) opts.speed = atof(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) opts.seed = atoi(argv[++i]);
        else if (strcmp(argv[i], "--waypoints") == 0 && i + 1 < argc) opts.navWaypoints = atoi(argv[++i]);
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) opts.csvFile = argv[++i];
        else if (strcmp(argv[i], "--no-material-sort") == 0) opts.materialSort = false;
        else if (strcmp(argv[i], "--no-pvs") == 0) opts.pvsCull = false;
        else if (strcmp(argv[i], "--no-frustum-cull") == 0) opts.frustumCull = false;
        else if (strcmp(argv[i], "--no-occlusion-cull") == 0) opts.occlusionCull = false;
        else if (strcmp(argv[i], "--occlusion-cull") == 0) opts.occlusionCull = true;  // legacy flag, same as default
        else if (strcmp(argv[i], "--no-front-to-back") == 0) opts.frontToBack = false;
        else if (strcmp(argv[i], "--no-depth-test") == 0) opts.depthTest = false;
        else if (strcmp(argv[i], "--max-draws") == 0 && i + 1 < argc) opts.maxDraws = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-polys") == 0 && i + 1 < argc) opts.maxPolys = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lighting") == 0 && i + 1 < argc) opts.lightCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--player-light-level") == 0 && i + 1 < argc) {
            opts.playerLightLevel = atoi(argv[++i]);
            if (opts.playerLightLevel < 0 || opts.playerLightLevel > 10) {
                fprintf(stderr, "ERROR: --player-light-level must be 0-10 (got %d)\n", opts.playerLightLevel);
                exit(1);
            }
        }
        else if (strcmp(argv[i], "--entities") == 0 && i + 1 < argc) opts.entityCount = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entity-dist") == 0 && i + 1 < argc) opts.entityRenderDist = atof(argv[++i]);
        else if (strcmp(argv[i], "--batch-entities") == 0) opts.batchEntities = true;
        else if (strcmp(argv[i], "--composite-entities") == 0) opts.compositeEntities = true;
        else if (strcmp(argv[i], "--composite-scenegraph") == 0) { opts.compositeEntities = true; opts.compositeSceneGraph = true; }
        else if (strcmp(argv[i], "--direct-gl") == 0) opts.directGLEntities = true;
        else if (strcmp(argv[i], "--lod-entities") == 0) opts.lodEntities = true;
        else if (strcmp(argv[i], "--adaptive-lod") == 0) opts.adaptiveLod = true;
        else if (strcmp(argv[i], "--entity-budget") == 0 && i + 1 < argc) opts.entityBudget = atoi(argv[++i]);
        else if (strcmp(argv[i], "--adaptive-lighting") == 0) opts.adaptiveLighting = true;
        else if (strcmp(argv[i], "--perpixel-budget") == 0 && i + 1 < argc) opts.perPixelBudget = atoi(argv[++i]);
        else if (strcmp(argv[i], "--zone-pp-budget") == 0 && i + 1 < argc) opts.perPixelZoneBudget = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pp-budget-mode") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            if (strcmp(mode, "area") == 0) { opts.zonePPAreaMode = true; opts.zonePPRadiusMode = false; }
            else if (strcmp(mode, "radius") == 0) { opts.zonePPRadiusMode = true; opts.zonePPAreaMode = false; }
            else if (strcmp(mode, "count") == 0) { opts.zonePPAreaMode = false; opts.zonePPRadiusMode = false; }
            else { fprintf(stderr, "Unknown --pp-budget-mode '%s' (expected count|area|radius)\n", mode); exit(1); }
        }
        else if (strcmp(argv[i], "--pp-area-factor") == 0 && i + 1 < argc) opts.zonePPAreaBudgetFactor = static_cast<float>(atof(argv[++i]));
        else if (strcmp(argv[i], "--pp-area-floor") == 0 && i + 1 < argc) opts.zonePPAreaFloor = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pp-radius-margin") == 0 && i + 1 < argc) opts.zonePPRadiusMargin = static_cast<float>(atof(argv[++i]));
        else if (strcmp(argv[i], "--lod-dist") == 0 && i + 1 < argc) opts.lodTier2Dist = atof(argv[++i]);
        else if (strcmp(argv[i], "--train") == 0) opts.trainMode = true;
        else if (strcmp(argv[i], "--animate") == 0) opts.animate = true;
        else if (strcmp(argv[i], "--no-entity-shader-promotion") == 0) opts.noEntityShaderPromotion = true;
        // P47: magenta entity viewer.
        else if (strcmp(argv[i], "--entity-viewer") == 0) opts.entityViewer = true;
        else if (strcmp(argv[i], "--entity-viewer-race") == 0 && i + 1 < argc) opts.entityViewerRace = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entity-viewer-race-label") == 0 && i + 1 < argc) opts.entityViewerRaceLabel = argv[++i];
        else if (strcmp(argv[i], "--entity-viewer-frame") == 0 && i + 1 < argc) opts.entityViewerFrame = atoi(argv[++i]);
        else if (strcmp(argv[i], "--entity-viewer-out-dir") == 0 && i + 1 < argc) opts.entityViewerOutDir = argv[++i];
        else if (strcmp(argv[i], "--entity-viewer-composite") == 0) opts.entityViewerComposite = true;
        // Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
        // else if (strcmp(argv[i], "--analyze-portals") == 0) opts.analyzePortals = true;
        // else if (strcmp(argv[i], "--detect-portals") == 0) opts.detectPortals = true;
        // else if (strcmp(argv[i], "--portal-csv") == 0 && i + 1 < argc) opts.portalCsvFile = argv[++i];
        else if (strcmp(argv[i], "--objects") == 0) opts.loadObjects = true;
        else if (strcmp(argv[i], "--object-anim-frame") == 0 && i + 1 < argc) opts.objectAnimFrame = atoi(argv[++i]);
        else if (strcmp(argv[i], "--tree-test") == 0) { opts.treeTest = true; opts.loadObjects = true; }
        else if (strcmp(argv[i], "--composite-entity-test") == 0) {
            opts.compositeEntityTest = true;
            opts.compositeEntities = true;
            if (opts.entityCount == 0) opts.entityCount = 24;
        }
        else if (strcmp(argv[i], "--test-race") == 0 && i + 1 < argc) opts.testRace = atoi(argv[++i]);
        else if (strcmp(argv[i], "--test-races") == 0 && i + 1 < argc) {
            std::string list = argv[++i];
            size_t pos = 0;
            while (pos < list.size()) {
                size_t comma = list.find(',', pos);
                std::string tok = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) opts.testRaceIds.push_back(static_cast<uint16_t>(atoi(tok.c_str())));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
        else if (strcmp(argv[i], "--test-variant") == 0 && i + 1 < argc) opts.testVariant = atoi(argv[++i]);
        else if (strcmp(argv[i], "--object-dist") == 0 && i + 1 < argc) opts.objectRenderDist = atof(argv[++i]);
        else if (strcmp(argv[i], "--dump") == 0) opts.dump = true;
        else if (strcmp(argv[i], "--dump-png") == 0 && i + 1 < argc) opts.dumpPngPath = argv[++i];
        else if (strcmp(argv[i], "--dump-frames") == 0 && i + 1 < argc) {
            opts.dumpFrames.clear();
            std::string spec = argv[++i];
            size_t pos = 0;
            while (pos < spec.size()) {
                size_t comma = spec.find(',', pos);
                std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                if (!tok.empty()) opts.dumpFrames.push_back(atoi(tok.c_str()));
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
        else if (strcmp(argv[i], "--dump-png-prefix") == 0 && i + 1 < argc) opts.dumpPngPrefix = argv[++i];
        else if (strcmp(argv[i], "--memory-json") == 0 && i + 1 < argc) opts.memoryJsonPath = argv[++i];
        else if (strcmp(argv[i], "--draw-histogram") == 0 && i + 1 < argc) opts.drawHistogramFile = argv[++i];
        else if (strcmp(argv[i], "--orbit-anchor-surface") == 0) opts.orbitAnchorSurface = true;
        else if (strcmp(argv[i], "--orbit-radius") == 0 && i + 1 < argc) opts.orbitRadius = atof(argv[++i]);
        else if (strcmp(argv[i], "--orbit-height-offset") == 0 && i + 1 < argc) opts.orbitHeightOffset = atof(argv[++i]);
        else if (strcmp(argv[i], "--camera-pose") == 0 && i + 1 < argc) {
            std::string spec = argv[++i];
            float vals[5] = {0, 0, 0, 0, 0};
            size_t pos = 0;
            for (int k = 0; k < 5 && pos <= spec.size(); ++k) {
                size_t comma = spec.find(',', pos);
                std::string tok = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                vals[k] = atof(tok.c_str());
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            opts.cameraPoses.push_back({vals[0], vals[1], vals[2], vals[3], vals[4]});
        }
        else if (strcmp(argv[i], "--pose-search") == 0 && i + 1 < argc) opts.poseSearch = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pose-search-headings") == 0 && i + 1 < argc) opts.poseSearchHeadings = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pose-search-pitch") == 0 && i + 1 < argc) opts.poseSearchPitch = atof(argv[++i]);
        else if (strcmp(argv[i], "--pose-search-eye-height") == 0 && i + 1 < argc) opts.poseSearchEyeHeight = atof(argv[++i]);
        else if (strcmp(argv[i], "--pose-search-csv") == 0 && i + 1 < argc) opts.poseSearchCsv = argv[++i];
        else if (strcmp(argv[i], "--dump-path") == 0 && i + 1 < argc) opts.dumpPathFile = argv[++i];
        else if (strcmp(argv[i], "--path-file") == 0 && i + 1 < argc) opts.pathFile = argv[++i];
        else if (strcmp(argv[i], "--synthetic-draws") == 0) opts.syntheticDraws = true;
        else if (strcmp(argv[i], "--merge-probe") == 0) opts.mergeProbe = true;
        else if (strcmp(argv[i], "--merge-probe-csv") == 0 && i + 1 < argc) opts.mergeProbeCsv = argv[++i];
        else if (strcmp(argv[i], "--merge-zone-draws") == 0) opts.mergeZoneDraws = true;
        else if (strcmp(argv[i], "--baked-unmerged-zone-draws") == 0) opts.bakedUnmergedZoneDraws = true;
        else if (strcmp(argv[i], "--gl-counters") == 0) opts.glCounters = true;
        else if (strcmp(argv[i], "--gpu-sync-probe") == 0) opts.gpuSyncProbe = true;
        else if (strcmp(argv[i], "--occluder-budget") == 0 && i + 1 < argc) {
            // P11: accept an integer, or "uncapped" (approximated as a very
            // large region count -- the loader has fewer regions than this
            // in every canonical zone, so it behaves as unlimited).
            const char* v = argv[++i];
            if (strcmp(v, "uncapped") == 0) opts.occluderBudget = 1 << 20;
            else opts.occluderBudget = atoi(v);
        }
        else if (strcmp(argv[i], "--shader-debug") == 0 && i + 1 < argc) opts.shaderDebugMode = atoi(argv[++i]);
        else if (strcmp(argv[i], "--material-counts") == 0) opts.materialCounts = true;
        else if (strcmp(argv[i], "--lighting-model") == 0 && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "perpixel") opts.lightingModel = Options::LightingModel::PerPixel;
            else if (v == "pervertex" || v == "lightweight") opts.lightingModel = Options::LightingModel::PerVertexOrLightweight;
            else {
                fprintf(stderr, "--lighting-model: unknown value '%s' (expected perpixel|pervertex|lightweight)\n", v.c_str());
                exit(1);
            }
        }
        else if (strcmp(argv[i], "--dump-filters") == 0 && i + 1 < argc) opts.dumpFilters = atoi(argv[++i]);
        // --log-level=, --log-only=, --log-module= handled by InitLogging()
        else if (strncmp(argv[i], "--log-", 6) == 0) { /* skip */ }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]); exit(1);
        }
    }
    // Q01: --path-file replaces navmesh/orbit path generation entirely, same
    // as --camera-pose/--pose-search already do -- combining them is
    // ambiguous about which camera scheme should win, so reject it outright.
    if (!opts.pathFile.empty() && (!opts.cameraPoses.empty() || opts.poseSearch > 0)) {
        fprintf(stderr, "ERROR: --path-file is mutually exclusive with --camera-pose/--pose-search\n");
        exit(1);
    }
    return opts;
}

