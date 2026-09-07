// gles2_fog_volume_benchmark - Benchmark analytic fog volume rendering on GLES2
//
// Tests the cost of rendering volumetric fog spheres using analytic density
// integration (Beer-Lambert transmittance) on Mali 400 hardware.
//
// Renders the same alley scene as the perpixel benchmark, then overlays
// alpha-blended fog volume spheres at light positions. Measures the cost
// of the fog pass in isolation and combined with the zone lighting pass.
//
// Density function variants (from "Analytic Fog Rendering With Volumetric
// Primitives" by matejlou):
//   - Quadratic:  (1 - r^2)^1  -- polynomial only, cheapest
//   - Quartic:    (1 - r^2)^2  -- degree-5 polynomial, smooth edges
//   - Linear:     (1 - r)      -- requires sqrt + log in antiderivative
//   - Gaussian:   exp(-r^2)    -- requires exp + erf approximation
//
// Each variant is tested with 1, 2, 4, and 8 fog spheres at two radii
// (small=3, large=6) to measure how overdraw scales.
//
// Usage:
//   ./gles2_fog_volume_benchmark              # auto-detect (DRM first, then X11)
//   ./gles2_fog_volume_benchmark --drm        # force DRM/GBM
//   ./gles2_fog_volume_benchmark --x11        # force X11/EGL
//   ./gles2_fog_volume_benchmark --frames N   # frames per test (default 300)
//   ./gles2_fog_volume_benchmark --res W H    # resolution (default 1280 720)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#endif
#include "fog_volume/fog_volume_bench.h"
// T385 (board B167): the ten GLSL ES sources this benchmark compiles moved to
// tools/hwcaps/fog_volume/fog_volume_shaders.cpp -- they were ~500 lines of the
// 1,711 this file used to be.
#include "fog_volume/fog_volume_shaders.h"

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int numFrames = 300;
    bool forceDRM = false, forceX11 = false;
    int width = 1280, height = 720;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) forceDRM = true;
        else if (strcmp(argv[i], "--x11") == 0) forceX11 = true;
        else if (strcmp(argv[i], "--frames") == 0 && i+1 < argc) numFrames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--res") == 0 && i+2 < argc) {
            width = atoi(argv[++i]);
            height = atoi(argv[++i]);
        }
    }

    printf("=== GLES2 Analytic Fog Volume Benchmark ===\n");
    printf("Resolution: %dx%d, Frames: %d per test\n\n", width, height, numFrames);

    EGLState state;
    state.width = width;
    state.height = height;
    bool ok = false;

#ifdef EQT_HAS_DRM
    if (!forceX11) ok = initDRM(state);
#endif
    if (!ok && !forceDRM) ok = initX11(state);
    if (!ok) {
        fprintf(stderr, "Failed to initialize EGL context\n");
        return 1;
    }

    printf("GL: %s\n", glGetString(GL_RENDERER));
    printf("GL: %s\n\n", glGetString(GL_VERSION));

    // ---- Build zone scene ----
    std::vector<Vertex> zoneVerts;
    std::vector<uint16_t> zoneIndices;
    buildAlleyScene(zoneVerts, zoneIndices);
    int zonePolyCount = (int)zoneIndices.size() / 3;
    printf("Zone scene: %d vertices, %d triangles (5 walls+floor+ceiling, subdivided)\n",
           (int)zoneVerts.size(), zonePolyCount);

    GLuint zoneVbo, zoneIbo;
    glGenBuffers(1, &zoneVbo);
    glBindBuffer(GL_ARRAY_BUFFER, zoneVbo);
    glBufferData(GL_ARRAY_BUFFER, zoneVerts.size() * sizeof(Vertex), zoneVerts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &zoneIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zoneIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, zoneIndices.size() * sizeof(uint16_t), zoneIndices.data(), GL_STATIC_DRAW);

    GLuint texture = createCheckerTexture(256);

    // ---- Build fog quad ----
    GLuint fogVbo, fogIbo;
    buildFogQuad(fogVbo, fogIbo);

    // ---- Compile zone shader ----
    printf("Compiling shaders...\n");
    GLuint vsZone = compileShader(GL_VERTEX_SHADER, VS_ZONE);
    GLuint fsZone = compileShader(GL_FRAGMENT_SHADER, FS_ZONE);
    GLuint progZone = (vsZone && fsZone) ? linkProgram(vsZone, fsZone) : 0;
    if (!progZone) {
        fprintf(stderr, "Zone shader compilation failed\n");
        cleanup(state);
        return 1;
    }

    // ---- Compile fog shaders ----
    GLuint vsFog = compileShader(GL_VERTEX_SHADER, VS_FOG);
    if (!vsFog) {
        fprintf(stderr, "Fog VS compilation failed\n");
        cleanup(state);
        return 1;
    }

    struct FogVariant {
        const char* name;
        const char* fsSrc;
        GLuint program;
        bool usesLUT;
    };

    std::vector<FogVariant> fogVariants = {
        {"QUADRATIC (1-r^2)",       FS_FOG_QUADRATIC,     0, false},
        {"QUARTIC (1-r^2)^2",       FS_FOG_QUARTIC,       0, false},
        {"LINEAR (1-r)",            FS_FOG_LINEAR,         0, false},
        {"GAUSSIAN exp(-r^2)",      FS_FOG_GAUSSIAN,       0, false},
    };

    // Create LUT textures for sqrt and exp (if FP16 textures supported)
    GLuint sqrtLUT = 0, expLUT = 0;
    float sqrtLUTScale = 0.0f, expLUTScale = 0.0f;
    if (hasHalfFloatTextures()) {
        // sqrt LUT: disc ∈ [0, 4] for unit sphere intersection
        float sqrtMaxVal = 4.0f;
        sqrtLUT = createSqrtLUT(sqrtMaxVal);
        sqrtLUTScale = 1.0f / sqrtMaxVal;

        // exp LUT: density = fogDensity(2.0) * radius * integral
        // For radius=6 and max integral ~1, density up to ~12. Use 16 for headroom.
        float expMaxVal = 16.0f;
        expLUT = createExpLUT(expMaxVal);
        expLUTScale = 1.0f / expMaxVal;

        fogVariants.push_back({"LUT QUADRATIC (sqrt+exp)", FS_FOG_LUT_QUADRATIC, 0, true});
        fogVariants.push_back({"LUT QUARTIC (sqrt+exp)",   FS_FOG_LUT_QUARTIC,   0, true});

        printf("LUT: sqrt(256x1 FP16, maxVal=%.0f), exp(256x1 FP16, maxVal=%.0f)\n", sqrtMaxVal, expMaxVal);
    } else {
        printf("LUT: FP16 textures not available, skipping LUT variants\n");
    }

    int numFogVariants = static_cast<int>(fogVariants.size());

    for (int i = 0; i < numFogVariants; i++) {
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fogVariants[i].fsSrc);
        if (!fs) {
            fprintf(stderr, "Fog FS '%s' compilation failed\n", fogVariants[i].name);
            cleanup(state);
            return 1;
        }
        // Fog shader uses only aPosition (attrib 0)
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vsFog);
        glAttachShader(prog, fs);
        glBindAttribLocation(prog, 0, "aPosition");
        glLinkProgram(prog);
        GLint linkOk;
        glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);
        if (!linkOk) {
            char log[512];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            fprintf(stderr, "Fog program '%s' link error:\n%s\n", fogVariants[i].name, log);
            cleanup(state);
            return 1;
        }
        fogVariants[i].program = prog;
        glDeleteShader(fs);
    }

    printf("  Zone program: 1, Fog programs: %d\n\n", numFogVariants);

    float aspect = (float)state.width / state.height;
    glViewport(0, 0, state.width, state.height);

    // Camera setup
    float proj[16], view[16], viewProj[16], tmp[16];
    mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
    mat4LookAt(view, 0.0f, 1.6f, 1.0f,  0.0f, 1.6f, 10.0f,  0.0f, 1.0f, 0.0f);
    mat4Multiply(viewProj, proj, view);

    float cameraPos[] = { 0.0f, 1.6f, 1.0f };

    // Camera basis vectors (looking down +Z, Y-up)
    // Right = +X, Up = +Y for this simple look-at
    float cameraRight[] = { 1.0f, 0.0f, 0.0f };
    float cameraUp[] = { 0.0f, 1.0f, 0.0f };

    // ================================================================
    // PHASE 1: Zone-only baseline
    // ================================================================
    printf("============================================================\n");
    printf("PHASE 1: Zone-only baseline (lightweight, 8 VS lights)\n");
    printf("============================================================\n\n");

    BenchResult zoneBaseline = benchZoneOnly(state, progZone, zoneVbo, zoneIbo,
                                             (int)zoneIndices.size(), texture,
                                             numFrames, aspect, viewProj);
    printf("  avg: %.2f ms  min: %.2f ms  max: %.2f ms  (~%.0f FPS)\n\n",
           zoneBaseline.avgMs, zoneBaseline.minMs, zoneBaseline.maxMs,
           1000.0 / zoneBaseline.avgMs);

    // ================================================================
    // PHASE 2: Fog-only (isolate fog FS cost)
    // ================================================================
    printf("============================================================\n");
    printf("PHASE 2: Fog-only (no zone geometry, isolate FS cost)\n");
    printf("============================================================\n\n");

    struct FogTestConfig {
        int numSpheres;
        float radius;
        const char* label;
    };

    FogTestConfig fogConfigs[] = {
        {1, 3.0f, "1 sphere, r=3 (small)"},
        {1, 6.0f, "1 sphere, r=6 (large)"},
        {2, 3.0f, "2 spheres, r=3"},
        {4, 3.0f, "4 spheres, r=3"},
        {4, 6.0f, "4 spheres, r=6"},
        {8, 3.0f, "8 spheres, r=3"},
        {8, 6.0f, "8 spheres, r=6"},
    };
    const int numFogConfigs = sizeof(fogConfigs) / sizeof(fogConfigs[0]);

    // Results storage: [variant][config]
    struct TestResult {
        const char* variantName;
        const char* configLabel;
        BenchResult result;
    };
    std::vector<TestResult> fogOnlyResults;

    for (int v = 0; v < numFogVariants; v++) {
        printf("--- %s ---\n", fogVariants[v].name);
        for (int c = 0; c < numFogConfigs; c++) {
            BenchResult r = benchFogOnly(state, fogVariants[v].program, fogVbo, fogIbo,
                                          fogConfigs[c].numSpheres, fogConfigs[c].radius,
                                          numFrames, viewProj, cameraPos, cameraRight, cameraUp,
                                          fogVariants[v].usesLUT ? sqrtLUT : 0, sqrtLUTScale,
                                          fogVariants[v].usesLUT ? expLUT : 0, expLUTScale);
            printf("  %-25s  avg: %6.2f ms  (~%.0f FPS)\n",
                   fogConfigs[c].label, r.avgMs, 1000.0 / r.avgMs);
            fogOnlyResults.push_back({fogVariants[v].name, fogConfigs[c].label, r});
        }
        printf("\n");
    }

    // ================================================================
    // PHASE 3: Zone + fog combined (realistic scenario)
    // ================================================================
    printf("============================================================\n");
    printf("PHASE 3: Zone + fog combined (realistic frame)\n");
    printf("============================================================\n\n");

    // Test each variant with the most likely production configs:
    // 4 spheres (typical torch count in a dungeon hallway) at both radii
    struct CombinedTestResult {
        const char* variantName;
        const char* configLabel;
        BenchResult combined;
        double fogDeltaMs;  // combined - zoneBaseline
    };
    std::vector<CombinedTestResult> combinedResults;

    FogTestConfig combinedConfigs[] = {
        {2, 3.0f, "2 spheres, r=3"},
        {4, 3.0f, "4 spheres, r=3"},
        {4, 6.0f, "4 spheres, r=6"},
        {8, 3.0f, "8 spheres, r=3"},
    };
    const int numCombinedConfigs = sizeof(combinedConfigs) / sizeof(combinedConfigs[0]);

    for (int v = 0; v < numFogVariants; v++) {
        printf("--- %s ---\n", fogVariants[v].name);
        for (int c = 0; c < numCombinedConfigs; c++) {
            BenchResult r = benchZonePlusFog(state, progZone, zoneVbo, zoneIbo,
                                              (int)zoneIndices.size(), texture,
                                              fogVariants[v].program, fogVbo, fogIbo,
                                              combinedConfigs[c].numSpheres, combinedConfigs[c].radius,
                                              numFrames, aspect, viewProj, cameraPos, cameraRight, cameraUp,
                                              fogVariants[v].usesLUT ? sqrtLUT : 0, sqrtLUTScale,
                                              fogVariants[v].usesLUT ? expLUT : 0, expLUTScale);
            double delta = r.avgMs - zoneBaseline.avgMs;
            printf("  %-25s  avg: %6.2f ms  (~%.0f FPS)  fog cost: %+.2f ms\n",
                   combinedConfigs[c].label, r.avgMs, 1000.0 / r.avgMs, delta);
            combinedResults.push_back({fogVariants[v].name, combinedConfigs[c].label, r, delta});
        }
        printf("\n");
    }

    // ================================================================
    // Summary
    // ================================================================
    printf("============================================================\n");
    printf("SUMMARY\n");
    printf("============================================================\n\n");

    printf("Zone baseline: %.2f ms (~%.0f FPS)\n\n", zoneBaseline.avgMs, 1000.0 / zoneBaseline.avgMs);

    printf("--- Fog-only (isolated FS cost) ---\n");
    printf("%-25s  %-25s  %7s  %7s\n", "Density Function", "Config", "ms", "FPS");
    printf("%-25s  %-25s  %7s  %7s\n", "----------------", "------", "--", "---");
    for (const auto& r : fogOnlyResults) {
        printf("%-25s  %-25s  %7.2f  %7.0f\n",
               r.variantName, r.configLabel, r.result.avgMs, 1000.0 / r.result.avgMs);
    }

    printf("\n--- Zone + fog combined (production scenario) ---\n");
    printf("%-25s  %-25s  %7s  %7s  %10s\n", "Density Function", "Config", "ms", "FPS", "fog cost");
    printf("%-25s  %-25s  %7s  %7s  %10s\n", "----------------", "------", "--", "---", "--------");
    for (const auto& r : combinedResults) {
        printf("%-25s  %-25s  %7.2f  %7.0f  %+9.2fms\n",
               r.variantName, r.configLabel, r.combined.avgMs,
               1000.0 / r.combined.avgMs, r.fogDeltaMs);
    }

    printf("\n--- Budget analysis (33.3ms frame budget @ 30 FPS) ---\n");
    printf("Zone baseline uses %.1f ms (%.0f%% of budget)\n",
           zoneBaseline.avgMs, zoneBaseline.avgMs / 33.3 * 100.0);
    printf("\nBest-case fog cost (QUADRATIC, 4 spheres r=3):\n");
    for (const auto& r : combinedResults) {
        if (strcmp(r.variantName, "QUADRATIC (1-r^2)") == 0 &&
            strcmp(r.configLabel, "4 spheres, r=3") == 0) {
            printf("  Combined: %.1f ms (%.0f%% of budget), fog adds %.1f ms\n",
                   r.combined.avgMs, r.combined.avgMs / 33.3 * 100.0, r.fogDeltaMs);
            printf("  Headroom remaining: %.1f ms\n", 33.3 - r.combined.avgMs);
        }
    }

    // ---- Cleanup ----
    glDeleteProgram(progZone);
    glDeleteShader(vsZone);
    glDeleteShader(fsZone);
    glDeleteShader(vsFog);
    for (int i = 0; i < numFogVariants; i++)
        glDeleteProgram(fogVariants[i].program);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &zoneVbo);
    glDeleteBuffers(1, &zoneIbo);
    glDeleteBuffers(1, &fogVbo);
    glDeleteBuffers(1, &fogIbo);
    if (sqrtLUT) glDeleteTextures(1, &sqrtLUT);
    if (expLUT) glDeleteTextures(1, &expLUT);

    cleanup(state);
    return 0;
}
