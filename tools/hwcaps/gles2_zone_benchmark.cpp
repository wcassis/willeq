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
#include "common/logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    // P03: flag-gated load-phase timing (cold/warm zone-load-time claim).
    // Default off — output byte-identical to before when absent.
    bool reportLoadTime = false;
    // P04: flag-gated per-step RSS/pool sampling during zone load.
    bool loadMemAudit = false;

    // Benchmark
    int frames = 300;
    int warmup = 10;
    float speed = 100.0f;    // Camera speed in EQ units/sec
    uint32_t seed = 42;
    int navWaypoints = 20;
    std::string csvFile;

    // A/B toggles (Z4)
    bool materialSort = true;
    bool pvsCull = true;
    bool frustumCull = true;
    bool occlusionCull = true;    // Software occlusion culling (on by default, disable with --no-occlusion-cull)
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
    // P06: zone budget mode -- mirrors production's adaptiveLightingBudgetMode.
    // Default false (count mode, unchanged behavior).
    bool zonePPAreaMode = false;
    float zonePPAreaBudgetFactor = 0.75f;
    // P07: hybrid floor -- first N regions (nearest-first) always per-pixel in
    // area mode, regardless of estimated area. Mirrors production's
    // adaptiveLightingAreaPPFloor. Ignored in count mode. Default 1.
    int zonePPAreaFloor = 1;
    // P07d: radius mode -- mirrors production's
    // AdaptiveLightingBudgetMode::Radius. Mutually exclusive with
    // zonePPAreaMode (--pp-budget-mode count|area|radius).
    bool zonePPRadiusMode = false;
    float zonePPRadiusMargin = 1.0f;
    bool trainMode = false;       // Cluster entities together (train simulation)

    // Q03: player-light fixture mirrored from production's lightstone model
    // (renderer_entities.cpp IrrlichtRenderer::setEntityLight, level 0-10).
    // Default 10 (max-level lightstone) -- replaces the old fixed
    // atten=(1,0,0.0005)/color=(1,0.95,0.8) fixture. 0 = no player light.
    int playerLightLevel = 10;

    // Zone objects
    bool loadObjects = false;     // Load zone objects (trees, lamps, etc.)
    float objectRenderDist = 300.0f;

    // Analysis modes (portal culling DOES NOT WORK and has NEVER WORKED with WillEQ)
    // bool analyzePortals = false;
    // bool detectPortals = false;
    // std::string portalCsvFile;

    // Animation
    bool animate = false;         // Enable entity skeletal animation

    // Diagnostic
    bool treeTest = false;        // Render single tree object, screenshot, exit
    bool compositeEntityTest = false; // Render 1 of each race/gender, screenshot, exit
    int testRace = 0;                 // Race ID to test (0 = all)
    int testVariant = 0;              // Texture variant (0=naked, 1=cloth, 2=chain, 3=plate)

    // Output
    bool dump = false;            // Print per-frame stats to stdout
    std::string dumpPngPath;      // H01: save the last measured frame as a PNG
    std::string dumpPngPrefix;    // H02: save designated measured frames as PREFIX_f<N>.png
    std::vector<int> dumpFrames;  // H02: measured-frame indices (0-based) to dump when dumpPngPrefix is set
    std::string memoryJsonPath;   // H02: write texture-cache memory stats as JSON

    // H02: orbit-fallback camera anchoring (opt-in; default preserves the
    // original zone-bounding-box-center behavior exactly). Anchoring the
    // orbit to the surface-position finder instead avoids the camera flying
    // through empty sky or embedded rock on tall/uneven zones where the
    // bounding-box center + auto radius can put the fixed-height orbit far
    // from any actual walkable geometry.
    bool orbitAnchorSurface = false;
    float orbitRadius = 0.0f;         // 0 = keep the existing auto radius (30% of extent)
    float orbitHeightOffset = 6.0f;   // Added to the anchor Z (approx. player eye height)

    // H02b: explicit static camera poses (EQ coords + heading/pitch degrees).
    // When non-empty, replaces the moving navmesh/orbit camera path entirely
    // with a sequence of static single-frame captures, one per pose --
    // frame index i corresponds 1:1 to cameraPoses[i], which lines up exactly
    // with the existing --dump-frames / manifest "designatedFrames" scheme.
    // --camera-pose is repeatable: one flag occurrence per pose.
    struct CameraPose { float x, y, z, heading, pitch; };
    std::vector<CameraPose> cameraPoses;

    // H02b: opt-in interesting-view search (bless-time diagnostic only, never
    // used by the CI gate). Samples candidate poses anchored at real zone
    // region bounding boxes (guaranteed to be near actual geometry, not empty
    // space) at several headings, renders each once, scores by
    // draws * distinctColors * nonSkyFraction, and prints the top scorers so
    // a human can pick and hard-code --camera-pose flags from the output.
    int poseSearch = 0;               // 0 = disabled; N = number of anchor points to sample
    int poseSearchHeadings = 4;       // Headings tried per anchor point
    float poseSearchPitch = -5.0f;    // Fixed pitch (degrees, negative = looking down slightly)
    float poseSearchEyeHeight = 6.0f; // Added to each anchor's floor Z (approx. player eye height)
    std::string poseSearchCsv;        // Optional: write all sampled candidates to CSV

    // Q01: moving camera-path record/replay. --dump-path writes the resolved
    // moving-path camera trajectory (one line per rendered MEASURED frame,
    // "x,y,z,heading,pitch" -- exactly the --camera-pose convention) so a
    // later run can reproduce it exactly with --path-file, instead of
    // re-deriving a (possibly different) path from the navmesh/orbit
    // generator every session (deferred.md "Benchmark camera path is not
    // reproducible across builds"). Mutually exclusive with --camera-pose
    // and --pose-search (both replace the moving path with something else
    // entirely already).
    std::string dumpPathFile;
    std::string pathFile;
    std::string fullCommandLine;      // For --dump-path header provenance

    // Synthetic (Z5)
    bool syntheticDraws = false;

    // H02d: shader debug mode for offline diagnostic capture (mirrors the
    // renderer's /shaderdebug console command). 0 = off.
    int shaderDebugMode = 0;

    // P01: per-material-type draw-call histogram in the CSV (flag-gated --
    // investigation-only, see refactor/P01_eqe1_perf_investigation.md
    // experiment 2). Off by default: adds two trailing CSV columns
    // (alpha_test_draws, solid_draws) computed from each frame's already-built
    // drawEntries list, no renderer changes needed.
    bool materialCounts = false;

    // P02 experiment A: convenience wrapper around the existing
    // --adaptive-lighting / --zone-pp-budget mechanism, letting the caller
    // ask for "all zone regions per-pixel" (the benchmark's unconditional
    // default -- zone_bench_loader.cpp's setPerPixelPlayerLight(true) is
    // always called, so with adaptiveLighting off every region draws with
    // the per-pixel-lit material) vs "all zone regions lightweight" in one
    // flag, for the 6b per-pixel-vs-lightweight A/B
    // (refactor/P02_hardware_resume.md). Source inspection (zone_shader.cpp)
    // shows "pervertex" and "lightweight" name the SAME shader variant (the
    // *_LIGHTWEIGHT_SRC programs: no per-pixel player light, all 8 point
    // lights per-vertex) -- both spellings are accepted and behave
    // identically, documented as such rather than pretending they're
    // distinct. "perpixel" (the default if the flag is never passed) leaves
    // adaptiveLighting/perPixelZoneBudget exactly as set by other flags, so
    // omitting --lighting-model entirely reproduces prior behavior exactly.
    enum class LightingModel { Default, PerPixel, PerVertexOrLightweight };
    LightingModel lightingModel = LightingModel::Default;

    // P02 experiment C: small runtime GL filter-state spot-check (H2 was
    // answered from source inspection in P01; this confirms it against the
    // real driver). 0 = off; N = sample the first N draw entries of the
    // first rendered pose, bind each entry's unit-0 (main) and unit-1
    // (companion, if any) texture, and print the actual
    // GL_TEXTURE_MIN_FILTER/MAG_FILTER GL returns.
    int dumpFilters = 0;
};

static void printUsage(const char* prog) {
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
    printf("\nObject options:\n");
    printf("  --objects            Load zone objects (trees, lamps, fences)\n");
    printf("  --object-dist F      Object render distance (default: 300)\n");
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

static Options parseArgs(int argc, char** argv) {
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
        // Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
        // else if (strcmp(argv[i], "--analyze-portals") == 0) opts.analyzePortals = true;
        // else if (strcmp(argv[i], "--detect-portals") == 0) opts.detectPortals = true;
        // else if (strcmp(argv[i], "--portal-csv") == 0 && i + 1 < argc) opts.portalCsvFile = argv[++i];
        else if (strcmp(argv[i], "--objects") == 0) opts.loadObjects = true;
        else if (strcmp(argv[i], "--tree-test") == 0) { opts.treeTest = true; opts.loadObjects = true; }
        else if (strcmp(argv[i], "--composite-entity-test") == 0) {
            opts.compositeEntityTest = true;
            opts.compositeEntities = true;
            if (opts.entityCount == 0) opts.entityCount = 24;
        }
        else if (strcmp(argv[i], "--test-race") == 0 && i + 1 < argc) opts.testRace = atoi(argv[++i]);
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

// ======== Q01: moving camera-path record/replay ========
//
// A "trace" is one resolved camera pose per rendered frame (warmup +
// measured), independent of whether it came from live navmesh/orbit path
// evaluation (record/normal mode) or was loaded verbatim from a --path-file
// (replay mode). Building this ONE array up front, before entity placement
// and before the render loop, is what makes replay reproduce entity
// placement: entity placement (zone_bench_loader.cpp
// placeEntitiesAlongPath) is fed the trace's MEASURED-frame positions
// directly (see design point A3 in refactor/Q01_path_replay_harness_align.md)
// instead of resampling the raw navmesh polyline by arc length -- a scheme
// that has no equivalent once the navmesh isn't available (replay). The
// render loop then just indexes into the trace instead of re-walking
// pathDist/CameraPath::getPositionAt() itself, so record and replay share
// exactly one code path for "what pose does frame N have".
struct TracePose {
    glm::vec3 pos;
    glm::vec3 dir;
    float heading = 0.0f;
    float pitch = 0.0f;
};

// One line of a loaded --path-file: "x,y,z,heading,pitch" (EQ coords +
// degrees -- exactly the --camera-pose convention, see printUsage()).
struct PathFilePose { float x, y, z, heading, pitch; };

// Parse a --dump-path-format file: '#'-prefixed / blank lines ignored,
// everything else must be "x,y,z,heading,pitch". Returns false (with a
// stderr ERROR) on any I/O or parse failure.
static bool loadPathFile(const std::string& path, std::vector<PathFilePose>& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        fprintf(stderr, "ERROR: --path-file: cannot open '%s'\n", path.c_str());
        return false;
    }
    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        ++lineNo;
        // Trim leading whitespace for the comment/blank check.
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0' || *p == '\r') continue;

        PathFilePose pose{};
        int n = sscanf(p, "%f,%f,%f,%f,%f",
                        &pose.x, &pose.y, &pose.z, &pose.heading, &pose.pitch);
        if (n != 5) {
            fprintf(stderr, "ERROR: --path-file: %s:%d: expected \"x,y,z,heading,pitch\", got: %s",
                    path.c_str(), lineNo, line);
            fclose(f);
            return false;
        }
        out.push_back(pose);
    }
    fclose(f);
    if (out.empty()) {
        fprintf(stderr, "ERROR: --path-file: '%s' has no pose lines\n", path.c_str());
        return false;
    }
    return true;
}

// Write the trace's measured-frame poses to a --dump-path file. traceMeasured
// is exactly opts.frames entries (the same poses fed to
// placeEntitiesAlongPath()); CSV frame row i corresponds to path line i.
static void writePathFile(const std::string& path, const Options& opts,
                           const std::vector<TracePose>& traceMeasured) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "ERROR: --dump-path: cannot open '%s' for write\n", path.c_str());
        return;
    }
    fprintf(f, "# Q01 camera path dump\n");
    fprintf(f, "# zone: %s\n", opts.zoneName.c_str());
    fprintf(f, "# seed: %u\n", opts.seed);
    fprintf(f, "# entities: %d\n", opts.entityCount);
    fprintf(f, "# frames: %d\n", opts.frames);
    fprintf(f, "# command: %s\n", opts.fullCommandLine.c_str());
    fprintf(f, "# format: x,y,z,heading,pitch (EQ coords + degrees, --camera-pose convention)\n");
    fprintf(f, "# one line per rendered MEASURED (post-warmup) frame; CSV frame row i == path line i\n");
    // %.9g round-trips an IEEE-754 float32 exactly (9 significant digits is
    // the documented minimum) -- required for the replay determinism
    // acceptance test (pose columns byte-identical to this file).
    for (const auto& tp : traceMeasured) {
        fprintf(f, "%.9g,%.9g,%.9g,%.9g,%.9g\n", tp.pos.x, tp.pos.y, tp.pos.z, tp.heading, tp.pitch);
    }
    fclose(f);
    printf("[dump-path] %s: saved (%zu poses)\n", path.c_str(), traceMeasured.size());
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

    // Q03: player light (per-pixel, index 0) — position is updated per frame
    // at the camera position in setupCamera() (unchanged by this unit); the
    // camera pose is an eye-height point, and production's lightstone sits
    // at player position + 3z (Irrlicht up), i.e. roughly eye height too —
    // see IrrlichtRenderer::setEntityLight, renderer_entities.cpp:98-156 —
    // so the existing camera-collocated placement is treated as equivalent
    // and is NOT changed here.
    //
    // Color/radius/attenuation are mirrored EXACTLY from that same
    // production formula (verified chain: SLight.Attenuation is set in
    // setEntityLight to (1, 0, 19/radius^2) and flows untouched through
    // SimulationInput::playerLightAtt{Constant,Linear,Quadratic}
    // (renderer_sim_bridge.cpp:301-305) -> SelectedLight::attConstant/
    // attLinear/attQuadratic (sim_visibility.cpp:581-583) ->
    // ZoneShader::setPlayerLightAtten (renderer_sim_bridge.cpp:760); the x3
    // "boost" applied at renderer_sim_bridge.cpp:745/754-761 multiplies only
    // the diffuse COLOR uniform sent to the shader for display, never the
    // attenuation triple or the SLight/simulation-input values themselves,
    // and is not part of what this benchmark fixture or the radius-mode R
    // solve consume, so it is intentionally not reproduced here):
    //   intensity = level/10, radius = 20 + intensity*80
    //   color: r = min(1, 0.9 + 0.1*intensity), g = min(1, 0.7 + 0.2*intensity),
    //          b = min(1, 0.4 + 0.2*intensity); each is then scaled by
    //          `intensity` to match the DiffuseColor production stores
    //          (SColorf(r*intensity, g*intensity, b*intensity, 1)).
    //   attenuation = (1, 0, 19/radius^2)
    // Level 0 = no player light: production removes the light scene node
    // entirely (setEntityLight, lightLevel==0 branch); the benchmark has no
    // equivalent "remove the light" step (index 0 is always sent to the
    // shader), so the same zero-contribution result is reproduced instead by
    // zeroing the color uniform -- with color (0,0,0) the per-pixel player
    // light term is exactly 0 regardless of the attenuation triple, which
    // is the observable behavior production's node-removal produces.
    {
        int level = opts.playerLightLevel;
        if (level <= 0) {
            data.zoneShader->setPlayerLightColor(0.0f, 0.0f, 0.0f);
            data.zoneShader->setPlayerLightAtten(1.0f, 0.0f, 0.0f);
        } else {
            float intensity = level / 10.0f;
            float radius = 20.0f + intensity * 80.0f;
            float r = std::min(1.0f, 0.9f + intensity * 0.1f);
            float g = std::min(1.0f, 0.7f + intensity * 0.2f);
            float b = std::min(1.0f, 0.4f + intensity * 0.2f);
            float quad = 19.0f / (radius * radius);
            data.zoneShader->setPlayerLightColor(r * intensity, g * intensity, b * intensity);
            data.zoneShader->setPlayerLightAtten(1.0f, 0.0f, quad);
        }
    }
    // Q03: attenuation-mirror proof -- read back what the shader uniforms
    // actually hold (not just what we computed above) via the same
    // accessors the R solve (zone_bench_loader.cpp) and per-frame draw path
    // consume, gated on --dump so it doesn't spam ordinary runs.
    if (opts.dump) {
        const float* c = data.zoneShader->playerLightColor();
        const float* a = data.zoneShader->playerLightAtten();
        printf("[player-light] level=%d color=(%.6f,%.6f,%.6f) atten=(%.6f,%.6f,%.9f)\n",
               opts.playerLightLevel, c[0], c[1], c[2], a[0], a[1], a[2]);
    }
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

// P01 experiment 2: classify each draw entry's effective material type (after
// any materialOverride from adaptive lighting) into "alpha-test family"
// (solid vs alphaTest+alphaTestRGBA+wind, heavy or lightweight variants) vs
// "solid family", using ZoneShaderManager's own type IDs so this never
// hard-codes irrlicht material-type enum values. Returns {alphaTestDraws,
// solidDraws}; entries whose type matches neither known family (e.g.
// fixed-function fallback) are counted under solidDraws as the conservative
// default (matches production's non-alpha-test path).
static std::pair<int, int> countMaterialFamilies(
    const std::vector<ZoneBench::DrawEntry>& entries,
    EQT::Graphics::ZoneShaderManager* zoneShader) {
    int alphaTestDraws = 0, solidDraws = 0;
    if (!zoneShader) {
        return {0, static_cast<int>(entries.size())};
    }
    const irr::s32 alphaTypes[] = {
        zoneShader->getMaterialTypeAlphaTest(),
        zoneShader->getMaterialTypeAlphaTestRGBA(),
        zoneShader->getMaterialTypeLWAlphaTest(),
        zoneShader->getMaterialTypeLWAlphaTestRGBA(),
        zoneShader->getMaterialTypeWindAlphaTest(),
        zoneShader->getMaterialTypeLWWindAlphaTest(),
        zoneShader->getMaterialTypeAtlasAlpha(),
        zoneShader->getMaterialTypeLWAtlasAlpha(),
    };
    for (const auto& de : entries) {
        irr::s32 effectiveType = (de.materialOverride >= 0)
            ? de.materialOverride
            : static_cast<irr::s32>(de.buffer->getMaterial().MaterialType);
        bool isAlpha = false;
        for (irr::s32 t : alphaTypes) {
            if (t >= 0 && t == effectiveType) { isAlpha = true; break; }
        }
        if (isAlpha) ++alphaTestDraws; else ++solidDraws;
    }
    return {alphaTestDraws, solidDraws};
}

#ifdef EQT_HAS_GLES2
// P02 experiment C: runtime GL filter-state spot-check. P01 answered H2
// (ETC1-vs-RGBA filter/mip asymmetry) entirely from source inspection of
// constrained_texture_cache.cpp; this confirms the actual driver-reported
// state matches for a handful of real draw entries. Binds unit 0 (main) and
// unit 1 (companion, if the material uses one) via Irrlicht's own
// setMaterial() -- same technique the pre-existing --tree-test diagnostic
// uses -- then reads back GL_TEXTURE_MIN_FILTER/MAG_FILTER directly from the
// driver, no assumptions about what was requested at upload time.
static const char* glFilterName(GLint f) {
    switch (f) {
        case GL_NEAREST: return "GL_NEAREST";
        case GL_LINEAR: return "GL_LINEAR";
        case GL_NEAREST_MIPMAP_NEAREST: return "GL_NEAREST_MIPMAP_NEAREST";
        case GL_LINEAR_MIPMAP_NEAREST: return "GL_LINEAR_MIPMAP_NEAREST";
        case GL_NEAREST_MIPMAP_LINEAR: return "GL_NEAREST_MIPMAP_LINEAR";
        case GL_LINEAR_MIPMAP_LINEAR: return "GL_LINEAR_MIPMAP_LINEAR";
        default: return "?";
    }
}

static void dumpFilterState(ZoneBench::ZoneBenchData& data,
                             const std::vector<ZoneBench::DrawEntry>& entries,
                             int count) {
    printf("[dump-filters] sampling first %d of %zu draw entries:\n",
           count, entries.size());
    int n = std::min<int>(count, static_cast<int>(entries.size()));
    for (int i = 0; i < n; ++i) {
        const auto& de = entries[i];
        const irr::video::SMaterial& mat = de.buffer->getMaterial();
        data.driver->setMaterial(mat);

        GLint tex0 = 0, tex1 = 0;
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex1);
        glActiveTexture(GL_TEXTURE0);

        auto dumpUnit = [&](int unit, GLint texName, const char* label) {
            if (texName == 0) {
                printf("  entry[%d] unit%d (%s): no texture bound\n", i, unit, label);
                return;
            }
            glActiveTexture(unit == 0 ? GL_TEXTURE0 : GL_TEXTURE1);
            GLint minF = 0, magF = 0;
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
            printf("  entry[%d] unit%d (%s) glTex=%d: MIN=%s MAG=%s\n",
                   i, unit, label, texName, glFilterName(minF), glFilterName(magF));
        };
        dumpUnit(0, tex0, "main");
        dumpUnit(1, tex1, "companion");
        glActiveTexture(GL_TEXTURE0);
    }
}
#endif

// H02b: convert a heading/pitch pose (degrees; heading 0=North/+Y, CW; pitch
// positive=up) into an EQ-coordinate direction vector, matching the
// convention the benchmark loop already uses when it derives heading/pitch
// from a direction vector for metrics (see the `heading = atan2f(...)` code
// in the main loop below) -- this is the exact inverse of that computation.
static void poseToDirection(float headingDeg, float pitchDeg,
                             float& dirX, float& dirY, float& dirZ) {
    const float d2r = 3.14159265f / 180.0f;
    float headingRad = headingDeg * d2r;
    float pitchRad = pitchDeg * d2r;
    float horiz = cosf(pitchRad);
    dirX = horiz * sinf(headingRad);
    dirY = horiz * cosf(headingRad);
    dirZ = sinf(pitchRad);
}

// H02b: read back the current framebuffer and compute (distinct color count,
// fraction of pixels equal to the clear/background color). Sampled at a
// stride to keep this cheap -- used by --pose-search scoring and available
// for ad-hoc diagnostics. Not used by the CI gate itself (that does its own
// PIL-based comparison in Python on the dumped PNGs).
static void computeFrameStats(ZoneBench::ZoneBenchData& data,
                               uint8_t clearR, uint8_t clearG, uint8_t clearB,
                               size_t& distinctColors, double& skyFraction) {
    distinctColors = 0;
    skyFraction = 1.0;
    irr::video::IImage* shot = data.driver->createScreenShot();
    if (!shot) return;

    irr::core::dimension2d<irr::u32> dim = shot->getDimension();
    const int stride = 2;  // sample every 2nd pixel in each axis (4x fewer samples)
    std::unordered_set<uint32_t> seen;
    seen.reserve(4096);
    size_t total = 0, skyCount = 0;
    for (irr::u32 y = 0; y < dim.Height; y += stride) {
        for (irr::u32 x = 0; x < dim.Width; x += stride) {
            irr::video::SColor c = shot->getPixel(x, y);
            uint32_t key = (c.getRed() << 16) | (c.getGreen() << 8) | c.getBlue();
            seen.insert(key);
            if (c.getRed() == clearR && c.getGreen() == clearG && c.getBlue() == clearB) {
                ++skyCount;
            }
            ++total;
        }
    }
    shot->drop();
    distinctColors = seen.size();
    skyFraction = total > 0 ? static_cast<double>(skyCount) / static_cast<double>(total) : 1.0;
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
    InitLogging(argc, argv);
    Options opts = parseArgs(argc, argv);

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
    benchConfig.animate = opts.animate;
    benchConfig.compositeEntities = opts.compositeEntities;
    benchConfig.loadObjects = opts.loadObjects;
    benchConfig.shaderDebugMode = opts.shaderDebugMode;
    benchConfig.loadMemAudit = opts.loadMemAudit;

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

    // Portal culling DOES NOT WORK and has NEVER WORKED with WillEQ.
    // if (opts.analyzePortals) {
    //     ZoneBench::analyzePortals(*data, opts.portalCsvFile);
    //     return 0;
    // }
    // if (opts.detectPortals) {
    //     ZoneBench::detectPortalsFromOccluders(*data, opts.portalCsvFile);
    //     return 0;
    // }

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

    // ── Composite entity test mode ──
    if (opts.compositeEntityTest) {
        const char* variantNames[] = {"naked", "cloth", "chain", "plate", "leather"};
        const char* variantName = (opts.testVariant >= 0 && opts.testVariant <= 4)
            ? variantNames[opts.testVariant] : "unknown";
        printf("=== Composite Entity Test (variant %d: %s) ===\n",
               opts.testVariant, variantName);

        // Build entity list: either one specific race (male+female) or all 12
        struct RaceGender { uint16_t raceId; uint8_t gender; const char* label; };
        std::vector<RaceGender> testEntities;

        RaceGender allRaces[] = {
            {1,0,"HUM"},{1,1,"HUF"},{2,0,"BAM"},{2,1,"BAF"},
            {3,0,"ERM"},{3,1,"ERF"},{4,0,"ELM"},{4,1,"ELF"},
            {5,0,"HIM"},{5,1,"HIF"},{6,0,"DAM"},{6,1,"DAF"},
            {7,0,"HAM"},{7,1,"HAF"},{8,0,"DWM"},{8,1,"DWF"},
            {9,0,"TRM"},{9,1,"TRF"},{10,0,"OGM"},{10,1,"OGF"},
            {11,0,"HOM"},{11,1,"HOF"},{12,0,"GNM"},{12,1,"GNF"},
        };

        if (opts.testRace > 0 && opts.testRace <= 12) {
            // Just male + female of the specified race
            testEntities.push_back(allRaces[(opts.testRace - 1) * 2]);
            testEntities.push_back(allRaces[(opts.testRace - 1) * 2 + 1]);
            printf("  Testing race %d: %s / %s\n", opts.testRace,
                   testEntities[0].label, testEntities[1].label);
        } else {
            for (const auto& r : allRaces) testEntities.push_back(r);
            printf("  Testing all %zu race/gender combos\n", testEntities.size());
        }

        int numEntities = static_cast<int>(testEntities.size());

        // Clear any model data from the generic preload (which used variant 0)
        // so we can reload with the requested variant.
        data->raceModelLoader->clearCache();

        // Preload composite data for all genders being tested
        printf("  Preloading composite data (variant %d)...\n", opts.testVariant);
        for (int i = 0; i < numEntities; ++i) {
            data->raceModelLoader->preloadModelData(
                testEntities[i].raceId, testEntities[i].gender, opts.testVariant);
        }
        data->raceModelLoader->promotePreparedModels();
        data->textureCache->uploadAllPending();

        data->entities.resize(numEntities);
        data->animatedNodes.resize(numEntities, nullptr);

        int loadedCount = 0;
        for (int i = 0; i < numEntities; ++i) {
            auto* mesh = data->raceModelLoader->getMeshForRace(
                testEntities[i].raceId, testEntities[i].gender);
            data->textureCache->uploadAllPending();

            if (!mesh) {
                printf("    %s: NO MESH\n", testEntities[i].label);
                continue;
            }
            data->entities[i].mesh = mesh;

            // Create animated node if requested
            if (opts.animate) {
                auto* node = data->raceModelLoader->createAnimatedNode(
                    testEntities[i].raceId, testEntities[i].gender,
                    data->smgr->getRootSceneNode());
                if (node) {
                    data->animatedNodes[i] = node;
                    node->playAnimation("o01", true);
                    data->textureCache->uploadAllPending();
                    if (node->getInstanceMesh())
                        data->entities[i].mesh = node->getInstanceMesh();
                }
            }

            int bufCount = data->entities[i].mesh->getMeshBufferCount();
            const char* texName = "(none)";
            if (bufCount > 0) {
                auto* tex = data->entities[i].mesh->getMeshBuffer(0)->getMaterial().getTexture(0);
                if (tex) texName = tex->getName().getPath().c_str();
            }
            printf("    %s: %d buffers, tex0='%s'\n", testEntities[i].label, bufCount, texName);
            loadedCount++;
        }
        printf("  Loaded %d/%d entities\n", loadedCount, numEntities);

        // Place entities side by side, spaced apart
        float surfX, surfY, surfZ;
        ZoneBench::findSurfacePosition(*data, surfX, surfY, surfZ);

        float spacing = 20.0f;
        float totalWidth = (numEntities - 1) * spacing;
        float startX = surfX - totalWidth * 0.5f;
        float entityY = surfY + 40.0f;  // 40 units in front of camera

        for (int i = 0; i < numEntities; ++i) {
            if (!data->entities[i].mesh) continue;

            float eqX = startX + i * spacing;
            data->entities[i].eqX = eqX;
            data->entities[i].eqY = entityY;
            data->entities[i].eqZ = surfZ - 6.0f;

            float meshOffsetY = -data->entities[i].mesh->getBoundingBox().MinEdge.Y;
            data->entities[i].position = irr::core::vector3df(
                eqX, data->entities[i].eqZ + meshOffsetY, entityY);

            if (data->animatedNodes[i]) {
                data->animatedNodes[i]->setPosition(data->entities[i].position);
                data->animatedNodes[i]->updateAbsolutePosition();
            }
            data->entities[i].priority = ZoneBench::ZoneBenchData::EntityPriority::Target;

            printf("    %s placed at EQ (%.1f, %.1f, %.1f)\n",
                   testEntities[i].label, eqX, entityY, data->entities[i].eqZ);
        }

        // Camera: behind and looking at entities
        float camEqX = surfX;
        float camEqY = surfY;
        float camEqZ = surfZ;
        float camDirX = 0, camDirY = 1, camDirZ = 0;

        printf("  Camera at EQ (%.1f, %.1f, %.1f) looking +Y toward entities at Y=%.1f\n",
               camEqX, camEqY, camEqZ, entityY);

        // Bright daytime lighting
        if (data->zoneShader) {
            data->zoneShader->setAmbientColor(0.6f, 0.6f, 0.6f);
            data->zoneShader->setSunDirection(0.3f, -0.8f, 0.2f);
            data->zoneShader->setSunColor(0.5f, 0.5f, 0.4f);
            data->zoneShader->setTintColor(1.0f, 1.0f, 1.0f);
            data->zoneShader->setFog(9000.0f, 9999.0f, 0.5f, 0.5f, 0.5f, 1.0f);
            data->zoneShader->setNumPointLights(0);
        }

        // Render loop — runs until --frames count
        int testFrames = opts.frames;
        printf("  Rendering %d frames (%.1f sec at 30fps)...\n",
               testFrames, testFrames / 30.0f);

        irr::core::matrix4 identityMat;
        for (int frame = 0; frame < testFrames; ++frame) {
            setupCamera(*data, camEqX, camEqY, camEqZ, camDirX, camDirY, camDirZ);
            data->driver->beginScene(true, true, irr::video::SColor(255, 80, 80, 120));
            if (data->zoneShader) data->zoneShader->beginFrame();

            // Animate
            if (opts.animate) {
                irr::u32 simTimeMs = static_cast<irr::u32>(frame * 33.3f);
                for (int i = 0; i < numEntities; ++i) {
                    if (!data->animatedNodes[i]) continue;
                    data->animatedNodes[i]->OnAnimate(simTimeMs);
                    auto* instMesh = data->animatedNodes[i]->getInstanceMesh();
                    if (instMesh) data->entities[i].mesh = instMesh;
                }
            }

            // Draw each entity (composite = 1 buffer, multi-buffer = merge with first mat)
            for (int i = 0; i < numEntities; ++i) {
                auto* mesh = data->entities[i].mesh;
                if (!mesh) continue;

                std::vector<irr::video::S3DVertex> verts;
                std::vector<irr::u16> indices;
                irr::video::SMaterial mat;
                bool hasMat = false;

                for (irr::u32 b = 0; b < mesh->getMeshBufferCount(); ++b) {
                    auto* buf = mesh->getMeshBuffer(b);
                    if (!buf || buf->getVertexCount() == 0) continue;
                    if (!hasMat) { mat = buf->getMaterial(); hasMat = true; }

                    irr::u32 base = static_cast<irr::u32>(verts.size());
                    if (base + buf->getVertexCount() > 65535) break;

                    auto* sv = static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                    for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                        irr::video::S3DVertex vert = sv[v];
                        vert.Pos += data->entities[i].position;
                        verts.push_back(vert);
                    }
                    auto* si = buf->getIndices();
                    for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                        indices.push_back(static_cast<irr::u16>(si[idx] + base));
                }

                if (!indices.empty() && hasMat) {
                    data->driver->setTransform(irr::video::ETS_WORLD, identityMat);
                    data->driver->setMaterial(mat);
                    data->driver->drawVertexPrimitiveList(
                        verts.data(), verts.size(),
                        indices.data(), indices.size() / 3,
                        irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                        irr::video::EIT_16BIT);
                }
            }

            data->driver->endScene();
        }

        printf("Composite entity test complete (%d frames rendered).\n", testFrames);
        return 0;
    }

    // ── Find a good surface-level position ──
    float surfX, surfY, surfZ;
    ZoneBench::findSurfacePosition(*data, surfX, surfY, surfZ);

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
            hasNavPath = camera.generateFromNavmesh(navFile, opts.navWaypoints, opts.seed,
                                                     glm::vec3(surfX, surfY, surfZ));
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
        printf("\n=== Pose search: %d anchors x %d headings ===\n",
               opts.poseSearch, opts.poseSearchHeadings);

        // Candidate anchor points: region bounding-box centers/floors, spread
        // evenly across the (already sorted-by-key) region map so candidates
        // are guaranteed to be near real loaded geometry, not empty space.
        std::vector<const ZoneBench::RegionData*> regionList;
        regionList.reserve(data->regions.size());
        for (auto& [idx, rd] : data->regions) regionList.push_back(&rd);

        int n = static_cast<int>(regionList.size());
        int take = std::min(opts.poseSearch, n);
        if (take <= 0) {
            fprintf(stderr, "ERROR: no regions loaded, cannot pose-search\n");
            return 1;
        }

        struct Candidate {
            float x, y, z, heading, pitch;
            int draws = 0;
            size_t distinctColors = 0;
            double skyFraction = 1.0;
            double score = 0.0;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(take * opts.poseSearchHeadings);

        std::vector<ZoneBench::DrawEntry> drawEntries;
        drawEntries.reserve(2048);
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

        FILE* csvOut = nullptr;
        if (!opts.poseSearchCsv.empty()) {
            csvOut = fopen(opts.poseSearchCsv.c_str(), "w");
            if (csvOut) fprintf(csvOut, "x,y,z,heading,pitch,draws,distinct_colors,sky_fraction,score\n");
        }

        for (int i = 0; i < take; ++i) {
            int ridx = i * n / take;
            const ZoneBench::RegionData* rd = regionList[ridx];
            float cx = (rd->eqMinX + rd->eqMaxX) * 0.5f;
            float cy = (rd->eqMinY + rd->eqMaxY) * 0.5f;
            // Anchor near the region's floor (bbox min Z) rather than its
            // center -- a good approximation of "standing inside this chunk
            // of geometry" for both open outdoor regions and enclosed rooms.
            float cz = rd->eqMinZ + opts.poseSearchEyeHeight;

            for (int h = 0; h < opts.poseSearchHeadings; ++h) {
                float heading = 360.0f * h / opts.poseSearchHeadings;
                float pitch = opts.poseSearchPitch;

                float dirX, dirY, dirZ;
                poseToDirection(heading, pitch, dirX, dirY, dirZ);
                setupCamera(*data, cx, cy, cz, dirX, dirY, dirZ);
                if (data->zoneShader) data->zoneShader->beginFrame();
                data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
                if (data->zoneShader) data->zoneShader->beginFrame();

                ZoneBench::CameraState camState{cx, cy, cz, dirX, dirY, dirZ, fov, aspect};
                int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
                drawEntries.clear();
                ZoneBench::collectDrawEntries(
                    *data, camState,
                    opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                    opts.frontToBack, opts.maxDraws, opts.maxPolys,
                    drawEntries, regionsVisible, regionsDrawn, regionsOccluded);
                if (!data->objects.empty()) {
                    int objDraws = 0, objPolys = 0;
                    ZoneBench::collectObjectDrawEntries(
                        *data, camState, opts.objectRenderDist,
                        opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                        drawEntries, objDraws, objPolys);
                }
                renderDrawEntries(*data, drawEntries, opts.depthTest);
#ifdef EQT_HAS_GLES2
                glFinish();
#endif
                Candidate c;
                c.x = cx; c.y = cy; c.z = cz; c.heading = heading; c.pitch = pitch;
                c.draws = static_cast<int>(drawEntries.size());
                computeFrameStats(*data, 40, 40, 60, c.distinctColors, c.skyFraction);
                double nonSkyFraction = 1.0 - c.skyFraction;
                c.score = static_cast<double>(c.draws) *
                          static_cast<double>(c.distinctColors) * nonSkyFraction;

                data->driver->endScene();

                if (csvOut) {
                    fprintf(csvOut, "%.1f,%.1f,%.1f,%.1f,%.1f,%d,%zu,%.4f,%.1f\n",
                            c.x, c.y, c.z, c.heading, c.pitch, c.draws,
                            c.distinctColors, c.skyFraction, c.score);
                }
                candidates.push_back(c);
            }
        }
        if (csvOut) fclose(csvOut);

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        int topK = std::min<int>(20, static_cast<int>(candidates.size()));
        printf("\nTop %d candidate poses (by draws * distinctColors * nonSkyFraction):\n", topK);
        printf("  %10s %10s %10s %8s %6s %6s %8s %8s %10s\n",
               "x", "y", "z", "heading", "pitch", "draws", "colors", "sky%", "score");
        for (int i = 0; i < topK; ++i) {
            const auto& c = candidates[i];
            printf("  %10.1f %10.1f %10.1f %8.1f %6.1f %6d %8zu %7.1f%% %10.1f  --camera-pose %.1f,%.1f,%.1f,%.1f,%.1f\n",
                   c.x, c.y, c.z, c.heading, c.pitch, c.draws, c.distinctColors,
                   c.skyFraction * 100.0, c.score, c.x, c.y, c.z, c.heading, c.pitch);
        }
        return 0;
    }

    // ── Run synthetic test if requested ──
    if (opts.syntheticDraws) {
        runSyntheticDrawTest(*data, opts.frames);
        return 0;
    }

    ZoneBench::MetricsCollector metrics;

    // ── H02b: static multi-pose capture ──
    // When --camera-pose was given (one or more times), skip the moving
    // navmesh/orbit camera path entirely: render exactly one frame per pose,
    // pose index i -> frame index i, which lines up 1:1 with the existing
    // --dump-frames / manifest "designatedFrames" mechanism below with no
    // changes needed to it or to the CI gate script.
    if (!opts.cameraPoses.empty()) {
        printf("\n=== Static pose capture: %zu poses ===\n", opts.cameraPoses.size());
        metrics.reserve(static_cast<int>(opts.cameraPoses.size()));
        metrics.setDumpPerFrame(opts.dump);

        std::vector<ZoneBench::DrawEntry> drawEntries;
        drawEntries.reserve(2048);
        float fov = 60.0f * 3.14159265f / 180.0f;
        float aspect = static_cast<float>(opts.width) / static_cast<float>(opts.height);

        for (size_t i = 0; i < opts.cameraPoses.size(); ++i) {
            const auto& pose = opts.cameraPoses[i];
            float dirX, dirY, dirZ;
            poseToDirection(pose.heading, pose.pitch, dirX, dirY, dirZ);

            setupCamera(*data, pose.x, pose.y, pose.z, dirX, dirY, dirZ);
            if (data->zoneShader) data->zoneShader->beginFrame();
            data->driver->beginScene(true, true, irr::video::SColor(255, 40, 40, 60));
            if (data->zoneShader) data->zoneShader->beginFrame();

            metrics.beginFrame(static_cast<int>(i), pose.x, pose.y, pose.z, pose.heading, pose.pitch);

            ZoneBench::CameraState camState{pose.x, pose.y, pose.z, dirX, dirY, dirZ, fov, aspect};
            int regionsVisible = 0, regionsDrawn = 0, regionsOccluded = 0;
            float lightRadius = 0.0f;
            int regionsDemotedRadius = 0;
            drawEntries.clear();
            ZoneBench::collectDrawEntries(
                *data, camState,
                opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                opts.frontToBack, opts.maxDraws, opts.maxPolys,
                drawEntries, regionsVisible, regionsDrawn, regionsOccluded,
                &lightRadius, &regionsDemotedRadius);
            metrics.setRadiusStats(lightRadius, regionsDemotedRadius);
            if (!data->objects.empty()) {
                int objDraws = 0, objPolys = 0;
                ZoneBench::collectObjectDrawEntries(
                    *data, camState, opts.objectRenderDist,
                    opts.pvsCull, opts.frustumCull, opts.occlusionCull,
                    drawEntries, objDraws, objPolys);
            }
            int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest);
            int polyCount = countPolygons(drawEntries);
            metrics.endDraw(static_cast<int>(drawEntries.size()), stateChanges, polyCount,
                            regionsVisible, regionsDrawn, regionsOccluded);
            if (opts.materialCounts) {
                auto counts = countMaterialFamilies(drawEntries, data->zoneShader.get());
                metrics.setMaterialCounts(counts.first, counts.second);
            }

#ifdef EQT_HAS_GLES2
            if (opts.dumpFilters > 0 && i == 0) {
                dumpFilterState(*data, drawEntries, opts.dumpFilters);
            }
            glFinish();
#endif
            metrics.endGpuSync();
            metrics.endFrame();

            if (!opts.dumpPngPrefix.empty()) {
                irr::video::IImage* shot = data->driver->createScreenShot();
                std::string path = opts.dumpPngPrefix + "_f" + std::to_string(i) + ".png";
                if (shot) {
                    bool ok = data->driver->writeImageToFile(shot, path.c_str());
                    shot->drop();
                    printf("[dump-frames] %s: %s\n", path.c_str(), ok ? "saved" : "FAILED to save");
                } else {
                    printf("[dump-frames] %s: createScreenShot() FAILED\n", path.c_str());
                }
            }

            data->driver->endScene();
        }

        metrics.printSummary();
        if (!opts.csvFile.empty()) metrics.writeCSV(opts.csvFile);

        if (data->textureCache) {
            size_t used = data->textureCache->getCurrentUsage();
            size_t limit = data->textureCache->getMemoryLimit();
            size_t count = data->textureCache->getTextureCount();
            printf("Texture cache: %zu / %zu bytes (%zu textures)\n", used, limit, count);
            if (!opts.memoryJsonPath.empty()) {
                FILE* f = fopen(opts.memoryJsonPath.c_str(), "w");
                if (f) {
                    fprintf(f,
                            "{\n"
                            "  \"zone\": \"%s\",\n"
                            "  \"textureCacheBytesUsed\": %zu,\n"
                            "  \"textureCacheBytesLimit\": %zu,\n"
                            "  \"textureCount\": %zu\n"
                            "}\n",
                            opts.zoneName.c_str(), used, limit, count);
                    fclose(f);
                    printf("[memory-json] %s: saved\n", opts.memoryJsonPath.c_str());
                } else {
                    printf("[memory-json] %s: FAILED to open for write\n", opts.memoryJsonPath.c_str());
                }
            }
        }

        printf("Benchmark complete.\n");
        return 0;
    }

    // ── Benchmark loop ──
    printf("\nStarting benchmark: %d warmup + %d measured frames\n",
           opts.warmup, opts.frames);

    metrics.reserve(opts.frames);
    metrics.setDumpPerFrame(opts.dump);

    std::vector<ZoneBench::DrawEntry> drawEntries;
    drawEntries.reserve(2048);

    // Q01: totalFrames/fullTrace were resolved above (before entity
    // placement) -- both record (navmesh/orbit) and replay (--path-file)
    // populate the same per-frame trace, so the loop below no longer cares
    // which mode it's in.
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
        ZoneBench::collectDrawEntries(
            *data, camState,
            opts.materialSort, opts.pvsCull, opts.frustumCull, opts.occlusionCull,
            opts.frontToBack, opts.maxDraws, opts.maxPolys,
            drawEntries, regionsVisible, regionsDrawn, regionsOccluded,
            &lightRadius, &regionsDemotedRadius);
        if (measuring) metrics.setRadiusStats(lightRadius, regionsDemotedRadius);

        // Collect object draw entries (trees, lamps, etc.)
        int objectDrawCalls = 0, objectPolys = 0;
        if (!data->objects.empty()) {
            ZoneBench::collectObjectDrawEntries(
                *data, camState, opts.objectRenderDist,
                opts.pvsCull, opts.frustumCull, opts.occlusionCull,
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
                                   regionsVisible, regionsDrawn, regionsOccluded);
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
                                   regionsVisible, regionsDrawn, regionsOccluded);
                }
            } else if (opts.adaptiveLod) {
                // Adaptive LOD: proactive frame-time prediction with billboard fallback
                int stateChangesZone = renderDrawEntries(*data, drawEntries, opts.depthTest);
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
                                   regionsVisible, regionsDrawn, regionsOccluded);
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
                                   regionsVisible, regionsDrawn, regionsOccluded);
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
                                   regionsVisible, regionsDrawn, regionsOccluded);
                }
            } else {
                // Unbatched: individual draw calls per entity mesh buffer
                ZoneBench::collectEntityDrawEntries(
                    *data, camState, opts.entityRenderDist,
                    opts.frustumCull, opts.occlusionCull,
                    drawEntries, entityDrawCalls, entityPolys);
                int stateChanges = renderDrawEntries(*data, drawEntries, opts.depthTest);
                int polyCount = countPolygons(drawEntries);

                if (measuring) {
                    metrics.endDraw(static_cast<int>(drawEntries.size()),
                                   stateChanges, polyCount,
                                   regionsVisible, regionsDrawn, regionsOccluded);
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

    // H02: report/emit texture-cache memory usage for the memory-ceiling CI gate.
    if (data->textureCache) {
        size_t used = data->textureCache->getCurrentUsage();
        size_t limit = data->textureCache->getMemoryLimit();
        size_t count = data->textureCache->getTextureCount();
        printf("Texture cache: %zu / %zu bytes (%zu textures)\n", used, limit, count);
        if (!opts.memoryJsonPath.empty()) {
            FILE* f = fopen(opts.memoryJsonPath.c_str(), "w");
            if (f) {
                fprintf(f,
                        "{\n"
                        "  \"zone\": \"%s\",\n"
                        "  \"textureCacheBytesUsed\": %zu,\n"
                        "  \"textureCacheBytesLimit\": %zu,\n"
                        "  \"textureCount\": %zu\n"
                        "}\n",
                        opts.zoneName.c_str(), used, limit, count);
                fclose(f);
                printf("[memory-json] %s: saved\n", opts.memoryJsonPath.c_str());
            } else {
                printf("[memory-json] %s: FAILED to open for write\n", opts.memoryJsonPath.c_str());
            }
        }
    }

    printf("Benchmark complete.\n");
    return 0;
}
