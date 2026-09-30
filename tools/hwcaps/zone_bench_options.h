#pragma once
//
// zone_bench_options.h — gles2_zone_benchmark's CLI surface.
//
// T387: struct Options, printUsage() and parseArgs() were the first 620 lines
// of tools/hwcaps/gles2_zone_benchmark.cpp (3,060 lines — over the 1,500
// ceiling M00 enforces tree-wide; board B167). The struct is the type every
// other extracted part takes, so it lives in this header; the two functions
// live in zone_bench_options.cpp. Both are verbatim.

#include <string>
#include <vector>
#include <cstdint>

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
    // P49 (refactor/P49_drm_fail_loud.md): by default the benchmark REFUSES
    // to run when the achieved resolution (post device-creation
    // getScreenSize()) differs from --res -- see the check right after
    // ZoneBench::loadZone() in main(). Pass this to downgrade that to a
    // warning and proceed anyway; the CSV always records the ACTUAL
    // resolution regardless of this flag (see zone_bench_metrics.h's
    // setActualResolution()).
    bool allowModeFallback = false;
    // P03: flag-gated load-phase timing (cold/warm zone-load-time claim).
    // Default off — output byte-identical to before when absent.
    bool reportLoadTime = false;
    // P04: flag-gated per-step RSS/pool sampling during zone load.
    bool loadMemAudit = false;
    // P11: run-time occluder budget override (maxOccluderRegions). -1 =
    // unset — P42: the loader now resolves the default from the
    // ConstrainedRendererConfig it already builds (orangepi -> 256,
    // P14/P39-blessed) instead of a hardcoded 16 that had drifted from
    // production (see zone_bench_loader.cpp's Step 10).
    int occluderBudget = -1;
    // B944: --texture-memory-bytes N overrides the texture cache's eviction
    // ceiling (textureMemoryBytes) for this run. 0 = the preset's value.
    size_t textureMemoryBytes = 0;

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
    bool glCounters = false;  // P38: enable driver glBindTexture/glTexParameteri call counters (off by default)
    bool gpuSyncProbe = false;  // P52: enable per-phase GPU-sync characterization instrumentation (off by default)
    int maxDraws = 0;         // 0 = unlimited
    int maxPolys = 0;         // 0 = unlimited
    int lightCount = -1;      // -1 = use zone defaults

    // Entities
    int entityCount = 0;          // Number of static entities to place
    float entityRenderDist = 300.0f;
    bool batchEntities = false;   // Merge same-material entity buffers into single draws
    bool compositeEntities = false; // Simulate 1-draw-per-entity (pre-baked composite texture)
    // P08D-RETAKE: --composite-entities' draw dispatch uses renderCompositeEntities(), a
    // custom manual-draw stand-in (P08c-era) with its OWN frustum-cull test
    // (data.frustumCuller->testSphere), NOT Irrlicht's normal scene-graph draw that
    // production's EntityRenderer actually uses for every entity, composite or not. That
    // mismatch (different cull test, different draw call, different z/state handling) is a
    // confound for A/B quality comparisons: it changes MORE than just "which texture",
    // so a diff against the default (non-composite) path isn't isolating what P08d's
    // production composite-mesh-building code actually changes. This flag keeps the real
    // composite mesh-building/loading (setCompositeEntityPath + preloadModelData) but
    // renders entities through the SAME default Irrlicht scene-graph path as the baseline
    // -- an apples-to-apples comparison of production's actual render mechanism.
    bool compositeSceneGraph = false;
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
    int objectAnimFrame = -1;     // B936: --object-anim-frame N (-1 = geometry pose)

    // Analysis modes (portal culling DOES NOT WORK and has NEVER WORKED with WillEQ)
    // bool analyzePortals = false;
    // bool detectPortals = false;
    // std::string portalCsvFile;

    // Animation
    bool animate = false;         // Enable entity skeletal animation

    // P46c: benchmark-only opt-out from applyEntityShaderMaterialPromotion()
    // (P46b's fix; zone_bench_loader.cpp). When set, entity mesh buffers are
    // left on whatever material Irrlicht's driver/mesh_building.cpp assigned
    // them by default -- EMT_SOLID (built-in fixed-function Solid3D) for
    // unmasked buffers, EMT_TRANSPARENT_ALPHA_CHANNEL_REF (P37's built-in
    // AlphaTest3D) for masked buffers -- instead of promoting them to the
    // custom per-pixel-lit GLSL shader family. Exists to produce a
    // lighting-neutral BEFORE/AFTER bless pair that isolates the alpha
    // cutout alone, with zero lighting-model delta between fda2e34 (which
    // never had promotion at all) and tip (which always promotes by
    // default). Default false -- promotion stays on unless this flag is
    // passed; output byte-identical to before this flag existed when
    // absent.
    bool noEntityShaderPromotion = false;

    // P47: magenta entity viewer -- see zone_bench_entity_viewer.h/.cpp and
    // refactor/P47_magenta_entity_viewer.md. Renders ONE race model LARGE
    // against a solid magenta clear color with no zone/object geometry, at
    // a fixed set of angles, for a trivially human-readable masked-alpha
    // check (magenta inside the silhouette = alpha gap). Mutually exclusive
    // with every other run mode below (checked after arg parsing) -- when
    // set, --entities/--test-races/--animate/--no-entity-shader-promotion
    // are all overridden to the correct proven configuration for the
    // requested leg (RGBA: promotion off, per P46c/P46d; ETC1: promotion
    // on, per P46b) rather than requiring the caller to know that wiring.
    bool entityViewer = false;
    int entityViewerRace = 0;               // required: EQ race ID
    std::string entityViewerRaceLabel;      // cosmetic, e.g. "SKE" (defaults to the numeric ID)
    std::string entityViewerMode;           // derived: "rgba" / "etc1" / "etc1-composite"
    int entityViewerFrame = 0;              // deterministic pose = frame * 33.3ms
    std::string entityViewerOutDir;         // required: existing directory for PNG output
    // P48: composite leg. Requires --eqe1-path (composite sheet alpha only
    // exists in the ETC1/EQE1 format -- see entity_composite_builder.cpp).
    // Routes the viewer's entity through RaceModelLoader's real composite
    // eligibility gate (race_model_loader.cpp's sheet-capability check)
    // instead of forcing compositeEntities=false the way the plain ETC1/RGBA
    // legs do -- this is what lets the viewer demonstrate P48's masked
    // composite alpha-test on the SAME production promotion path the
    // per-part legs already exercise.
    bool entityViewerComposite = false;

    // Diagnostic
    bool treeTest = false;        // Render single tree object, screenshot, exit
    bool compositeEntityTest = false; // Render 1 of each race/gender, screenshot, exit
    int testRace = 0;                 // Race ID to test (0 = all)
    int testVariant = 0;              // Texture variant (0=naked, 1=cloth, 2=chain, 3=plate)
    std::vector<uint16_t> testRaceIds; // P08f --test-races: explicit race-ID cycle for --entities

    // Output
    bool dump = false;            // Print per-frame stats to stdout
    std::string dumpPngPath;      // H01: save the last measured frame as a PNG
    std::string dumpPngPrefix;    // H02: save designated measured frames as PREFIX_f<N>.png
    std::vector<int> dumpFrames;  // H02: measured-frame indices (0-based) to dump when dumpPngPrefix is set
    std::string memoryJsonPath;   // H02: write texture-cache memory stats as JSON
    // P08: draw-composition histogram (draws/region, polys/draw distribution,
    // tiny-draw count) at designated frames (reuses --dump-frames), one CSV
    // row per listed frame appended to this file. Independent of
    // --dump-png-prefix -- only --dump-frames + this flag are required.
    std::string drawHistogramFile;

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

    // P09 A-gate: cross-region merge primitive probe (separate-vbo vs
    // shared-coalesced vs streamed-ibo vs client-index).
    bool mergeProbe = false;
    std::string mergeProbeCsv;

    // P09 Phase B: cross-region same-material zone draw merge prototype.
    // Default OFF everywhere (visual gate / all existing paths untouched).
    bool mergeZoneDraws = false;

    // P09c Phase C: baked-unmerged comparison mode (same baked world-space
    // buffers as --merge-zone-draws, but one draw call per region/buffer,
    // identity transform, no coalescing). Benchmark-only, default OFF.
    // Mutually exclusive with --merge-zone-draws (mergeZoneDraws wins if
    // both are passed -- see the dispatch in renderDrawEntries()).
    bool bakedUnmergedZoneDraws = false;

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

void printUsage(const char* prog);
Options parseArgs(int argc, char** argv);
