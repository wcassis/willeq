// gles2_shader_perpixel_benchmark - Benchmark per-pixel vs per-vertex lighting on GLES2
//
// Creates a simple scene (textured quads simulating an alley) and renders it with:
//   1. Lightweight fragment shader (all lighting per-vertex, trivial FS)
//   2. Per-pixel fragment shader (player light computed per-fragment in FS)
//
// Reports frame times for each variant to isolate shader cost from the full renderer.
//
// Usage:
//   ./gles2_shader_perpixel_benchmark              # auto-detect (DRM first, then X11)
//   ./gles2_shader_perpixel_benchmark --drm        # force DRM/GBM
//   ./gles2_shader_perpixel_benchmark --x11        # force X11/EGL
//   ./gles2_shader_perpixel_benchmark --frames N   # frames per test (default 300)
//   ./gles2_shader_perpixel_benchmark --res W H    # resolution (default 1280 720)
//
// T447 split the tool four ways; the seam is
// tools/hwcaps/shader_perpixel/shader_perpixel.h and the EGL setup, the GLSL
// sources and the scene builders live beside it. What stayed here is the
// benchmark runner and main().

#include "hwcaps/shader_perpixel/shader_perpixel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

// ============================================================================
// Benchmark runner
// ============================================================================

struct BenchResult {
    double avgMs;
    double minMs;
    double maxMs;
};

static BenchResult runBenchmark(EGLState& state, GLuint program, GLuint vbo, GLuint ibo,
                                 int indexCount, GLuint texture, int numFrames,
                                 float aspect, bool isPerPixel, int numPointLights = 2,
                                 GLuint lutTexture = 0, float lutScale = 0.0f) {
    glUseProgram(program);

    // Camera: standing in the alley looking down it
    float proj[16], view[16], world[16], wvp[16], tmp[16];
    mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
    mat4LookAt(view, 0.0f, 1.6f, 1.0f,  0.0f, 1.6f, 10.0f,  0.0f, 1.0f, 0.0f);
    mat4Identity(world);
    mat4Multiply(tmp, view, world);  // view * world
    mat4Multiply(wvp, proj, tmp);    // proj * view * world

    // Set uniforms
    GLint locWVP = glGetUniformLocation(program, "mWorldViewProj");
    GLint locWorld = glGetUniformLocation(program, "mWorld");
    GLint locSunDir = glGetUniformLocation(program, "uSunDir");
    GLint locSunColor = glGetUniformLocation(program, "uSunColor");
    GLint locAmbient = glGetUniformLocation(program, "uAmbientColor");
    GLint locTint = glGetUniformLocation(program, "uTintColor");
    GLint locFogStart = glGetUniformLocation(program, "uFogStart");
    GLint locFogEnd = glGetUniformLocation(program, "uFogEnd");
    GLint locFogColor = glGetUniformLocation(program, "uFogColor");
    GLint locTexture = glGetUniformLocation(program, "uTexture");
    GLint locLightPos = glGetUniformLocation(program, "uLightPos[0]");
    GLint locLightColor = glGetUniformLocation(program, "uLightColor[0]");
    GLint locLightAtten = glGetUniformLocation(program, "uLightAtten[0]");

    if (locWVP >= 0) glUniformMatrix4fv(locWVP, 1, GL_FALSE, wvp);
    if (locWorld >= 0) glUniformMatrix4fv(locWorld, 1, GL_FALSE, world);

    // Night-time lighting: dim ambient, no sun, dark tint
    float sunDir[] = {0.0f, -1.0f, 0.0f};
    float sunColor[] = {0.0f, 0.0f, 0.0f};
    float ambient[] = {0.05f, 0.05f, 0.08f};
    float tint[] = {0.3f, 0.3f, 0.4f};
    if (locSunDir >= 0) glUniform3fv(locSunDir, 1, sunDir);
    if (locSunColor >= 0) glUniform3fv(locSunColor, 1, sunColor);
    if (locAmbient >= 0) glUniform3fv(locAmbient, 1, ambient);
    if (locTint >= 0) glUniform3fv(locTint, 1, tint);
    if (locFogStart >= 0) glUniform1f(locFogStart, 50.0f);
    if (locFogEnd >= 0) glUniform1f(locFogEnd, 200.0f);
    float fogColor[] = {0.1f, 0.1f, 0.15f, 1.0f};
    if (locFogColor >= 0) glUniform4fv(locFogColor, 1, fogColor);
    if (locTexture >= 0) glUniform1i(locTexture, 0);

    // Point lights: player light at index 0, torch at index 1
    float lightPos[8*3] = {};
    float lightColor[8*3] = {};
    float lightAtten[8*3] = {};
    // Light 0: player light (above player)
    lightPos[0] = 0.0f; lightPos[1] = 3.0f; lightPos[2] = 1.0f;
    lightColor[0] = 0.9f * 0.5f * 3.0f; lightColor[1] = 0.7f * 0.5f * 3.0f; lightColor[2] = 0.4f * 0.5f * 3.0f;
    lightAtten[0] = 1.0f; lightAtten[1] = 0.0f; lightAtten[2] = 19.0f / (52.0f * 52.0f);
    // Light 1: torch behind player
    lightPos[3] = 0.0f; lightPos[4] = 4.0f; lightPos[5] = -2.0f;
    lightColor[3] = 1.0f; lightColor[4] = 0.7f; lightColor[5] = 0.3f;
    lightAtten[3] = 1.0f; lightAtten[4] = 0.0f; lightAtten[5] = 19.0f / (40.0f * 40.0f);
    // Remaining lights: zero (attConstant=1 to avoid division issues)
    for (int i = 2; i < 8; i++) lightAtten[i*3] = 1.0f;

    if (locLightPos >= 0) glUniform3fv(locLightPos, 8, lightPos);
    if (locLightColor >= 0) glUniform3fv(locLightColor, 8, lightColor);
    if (locLightAtten >= 0) glUniform3fv(locLightAtten, 8, lightAtten);

    // Per-pixel variant: numPointLights and player light FS uniforms
    if (isPerPixel) {
        GLint locNumLights = glGetUniformLocation(program, "uNumPointLights");
        GLint locPLPos = glGetUniformLocation(program, "uPlayerLightPos");
        GLint locPLColor = glGetUniformLocation(program, "uPlayerLightColor");
        GLint locPLAtten = glGetUniformLocation(program, "uPlayerLightAtten");
        if (locNumLights >= 0) glUniform1i(locNumLights, numPointLights);
        if (locPLPos >= 0) glUniform3fv(locPLPos, 1, &lightPos[0]);
        if (locPLColor >= 0) glUniform3fv(locPLColor, 1, &lightColor[0]);
        if (locPLAtten >= 0) glUniform3fv(locPLAtten, 1, &lightAtten[0]);
    }

    // Bind LUT texture to unit 1 if available
    if (lutTexture) {
        GLint locAttenLUT = glGetUniformLocation(program, "uAttenLUT");
        GLint locLUTScale = glGetUniformLocation(program, "uLUTScale");
        if (locAttenLUT >= 0) glUniform1i(locAttenLUT, 1);
        if (locLUTScale >= 0) glUniform1f(locLUTScale, lutScale);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lutTexture);
        glActiveTexture(GL_TEXTURE0);
    }

    // Bind geometry
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    int stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glViewport(0, 0, state.width, state.height);

    // Warmup frames (5)
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        glFinish();
    }

    // Timed frames
    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        glFinish();

#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif

        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        frameTimes.push_back(ms);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);

    // Compute stats
    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) {
        sum += t;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
}

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

    printf("=== GLES2 Per-Pixel Lighting Benchmark ===\n");
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

    // Build scene
    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    buildAlleyScene(verts, indices);
    int polyCount = (int)indices.size() / 3;
    printf("Scene: %d vertices, %d triangles (4 walls+floor, subdivided)\n\n",
           (int)verts.size(), polyCount);

    // Upload geometry
    GLuint vbo, ibo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    GLuint texture = createCheckerTexture(256);

    // Create attenuation LUT (FP16) if half-float textures are supported
    // Uses same attenuation params as player light: constant=1.0, quadratic=19/(52²)
    float attenConst = 1.0f;
    float attenQuad = 19.0f / (52.0f * 52.0f);
    float lutMaxD2 = 4096.0f;  // 64 units range (covers well beyond light radius)
    float lutScale = 1.0f / lutMaxD2;
    GLuint lutTexture = 0;
    bool hasLUT = false;

    if (hasHalfFloatTextures()) {
        lutTexture = createAttenLUT(attenConst, attenQuad, lutMaxD2);
        if (lutTexture) {
            hasLUT = true;
            printf("LUT: Created 256x1 FP16 attenuation LUT (atten=%.1f+%.6f*d², maxD²=%.0f)\n",
                   attenConst, attenQuad, lutMaxD2);
        } else {
            printf("LUT: FP16 texture creation failed, LUT tests skipped\n");
        }
    } else {
        printf("LUT: GL_OES_texture_half_float/_linear not available, LUT tests skipped\n");
    }

    // Compile shaders
    printf("Compiling shaders...\n");

    GLuint vsPerPixel = compileShader(GL_VERTEX_SHADER, VS_PERPIXEL);

    GLuint fsBranched = compileShader(GL_FRAGMENT_SHADER, FS_PERPIXEL_BRANCHED);
    GLuint progBranched = (vsPerPixel && fsBranched) ? linkProgram(vsPerPixel, fsBranched) : 0;

    GLuint fsBranchless = compileShader(GL_FRAGMENT_SHADER, FS_PERPIXEL_BRANCHLESS);
    GLuint progBranchless = (vsPerPixel && fsBranchless) ? linkProgram(vsPerPixel, fsBranchless) : 0;

    GLuint fsOptInvSqrt = compileShader(GL_FRAGMENT_SHADER, FS_OPT_INVERSESQRT);
    GLuint progOptInvSqrt = (vsPerPixel && fsOptInvSqrt) ? linkProgram(vsPerPixel, fsOptInvSqrt) : 0;

    GLuint fsOptQuad = compileShader(GL_FRAGMENT_SHADER, FS_OPT_QUADRATIC);
    GLuint progOptQuad = (vsPerPixel && fsOptQuad) ? linkProgram(vsPerPixel, fsOptQuad) : 0;

    GLuint fsOptOmni = compileShader(GL_FRAGMENT_SHADER, FS_OPT_OMNI);
    GLuint progOptOmni = (vsPerPixel && fsOptOmni) ? linkProgram(vsPerPixel, fsOptOmni) : 0;

    GLuint fsOptOmniQuad = compileShader(GL_FRAGMENT_SHADER, FS_OPT_OMNI_QUAD);
    GLuint progOptOmniQuad = (vsPerPixel && fsOptOmniQuad) ? linkProgram(vsPerPixel, fsOptOmniQuad) : 0;

    GLuint vsPerVertexPL = compileShader(GL_VERTEX_SHADER, VS_PERVERTEX_PLIGHT);
    GLuint fsPerVertexPL = compileShader(GL_FRAGMENT_SHADER, FS_PERVERTEX_PLIGHT);
    GLuint progPerVertexPL = (vsPerVertexPL && fsPerVertexPL) ? linkProgram(vsPerVertexPL, fsPerVertexPL) : 0;

    GLuint vsLightweight = compileShader(GL_VERTEX_SHADER, VS_LIGHTWEIGHT);
    GLuint fsLightweight = compileShader(GL_FRAGMENT_SHADER, FS_LIGHTWEIGHT);
    GLuint progLightweight = (vsLightweight && fsLightweight) ? linkProgram(vsLightweight, fsLightweight) : 0;

    // LUT shader variants (only if LUT texture was created successfully)
    GLuint fsLUTQuad = 0, progLUTQuad = 0;
    GLuint fsLUTOmniQuad = 0, progLUTOmniQuad = 0;
    if (hasLUT) {
        fsLUTQuad = compileShader(GL_FRAGMENT_SHADER, FS_LUT_QUADRATIC);
        progLUTQuad = (vsPerPixel && fsLUTQuad) ? linkProgram(vsPerPixel, fsLUTQuad) : 0;

        fsLUTOmniQuad = compileShader(GL_FRAGMENT_SHADER, FS_LUT_OMNI_QUAD);
        progLUTOmniQuad = (vsPerPixel && fsLUTOmniQuad) ? linkProgram(vsPerPixel, fsLUTOmniQuad) : 0;
    }

    if (!progBranched || !progBranchless || !progOptInvSqrt || !progOptQuad ||
        !progOptOmni || !progOptOmniQuad || !progPerVertexPL || !progLightweight) {
        fprintf(stderr, "Shader compilation failed\n");
        cleanup(state);
        return 1;
    }

    int progCount = 8;
    if (progLUTQuad) progCount++;
    if (progLUTOmniQuad) progCount++;
    printf("  Programs compiled: %d%s\n\n", progCount,
           hasLUT ? " (including LUT variants)" : "");

    float aspect = (float)state.width / state.height;

    // Helper struct for test results
    struct TestResult {
        const char* name;
        BenchResult r1, r2;
        double avg() const { return (r1.avgMs + r2.avgMs) / 2.0; }
    };

    // All tests: run each variant twice for order-bias checking
    // All per-pixel tests use numPointLights=1 (player light only) to isolate FS cost
    struct TestDef {
        const char* name;
        GLuint program;
        bool isPerPixel;
        int numLights;
        GLuint lutTexture;
        float lutScale;
    };
    std::vector<TestDef> tests = {
        {"LIGHTWEIGHT (baseline)",                      progLightweight,  false, 0, 0, 0.0f},
        {"PP BRANCHED (original, if check)",            progBranched,     true,  1, 0, 0.0f},
        {"PP BRANCHLESS (current, no branch)",          progBranchless,   true,  1, 0, 0.0f},
        {"OPT B: inversesqrt (skip normalize, 1 isqrt)", progOptInvSqrt, true,  1, 0, 0.0f},
        {"OPT C: quadratic (isqrt + quad-only atten)",  progOptQuad,     true,  1, 0, 0.0f},
        {"OPT D: omni (distance-only, full atten)",     progOptOmni,     true,  1, 0, 0.0f},
        {"OPT E: omni+quad (no sqrt at all)",           progOptOmniQuad, true,  1, 0, 0.0f},
        {"OPT F: per-vertex plight (trivial FS)",       progPerVertexPL, true,  1, 0, 0.0f},
    };
    if (progLUTQuad) {
        tests.push_back({"LUT G: OPT C via FP16 LUT (isqrt+atten)", progLUTQuad, true, 1, lutTexture, lutScale});
    }
    if (progLUTOmniQuad) {
        tests.push_back({"LUT H: OPT E via FP16 LUT (atten only)",  progLUTOmniQuad, true, 1, lutTexture, lutScale});
    }
    const int numTests = (int)tests.size();

    std::vector<TestResult> results(numTests);

    // Round 1
    printf("=== ROUND 1 ===\n\n");
    for (int i = 0; i < numTests; i++) {
        printf("--- %s ---\n", tests[i].name);
        results[i].name = tests[i].name;
        results[i].r1 = runBenchmark(state, tests[i].program, vbo, ibo,
                                      (int)indices.size(), texture, numFrames,
                                      aspect, tests[i].isPerPixel, tests[i].numLights,
                                      tests[i].lutTexture, tests[i].lutScale);
        printf("  avg: %.2f ms  min: %.2f ms  max: %.2f ms  (~%.0f FPS)\n\n",
               results[i].r1.avgMs, results[i].r1.minMs, results[i].r1.maxMs,
               1000.0 / results[i].r1.avgMs);
    }

    // Round 2
    printf("=== ROUND 2 (order bias check) ===\n\n");
    for (int i = 0; i < numTests; i++) {
        printf("--- %s ---\n", tests[i].name);
        results[i].r2 = runBenchmark(state, tests[i].program, vbo, ibo,
                                      (int)indices.size(), texture, numFrames,
                                      aspect, tests[i].isPerPixel, tests[i].numLights,
                                      tests[i].lutTexture, tests[i].lutScale);
        printf("  avg: %.2f ms  min: %.2f ms  max: %.2f ms  (~%.0f FPS)\n\n",
               results[i].r2.avgMs, results[i].r2.minMs, results[i].r2.maxMs,
               1000.0 / results[i].r2.avgMs);
    }

    // Summary
    double lwAvg = results[0].avg();
    printf("=== SUMMARY ===\n");
    printf("%-45s  %7s  %7s  %8s\n", "Variant", "ms", "FPS", "vs LW");
    printf("%-45s  %7s  %7s  %8s\n", "-------", "--", "---", "-----");
    for (int i = 0; i < numTests; i++) {
        double avg = results[i].avg();
        double diff = avg - lwAvg;
        printf("%-45s  %7.2f  %7.0f  %+7.2fms\n",
               results[i].name, avg, 1000.0 / avg, diff);
    }

    // Cleanup
    glDeleteProgram(progBranched);
    glDeleteProgram(progBranchless);
    glDeleteProgram(progOptInvSqrt);
    glDeleteProgram(progOptQuad);
    glDeleteProgram(progOptOmni);
    glDeleteProgram(progOptOmniQuad);
    glDeleteProgram(progPerVertexPL);
    glDeleteProgram(progLightweight);
    if (progLUTQuad) glDeleteProgram(progLUTQuad);
    if (progLUTOmniQuad) glDeleteProgram(progLUTOmniQuad);
    glDeleteShader(vsPerPixel);
    glDeleteShader(fsBranched);
    glDeleteShader(fsBranchless);
    glDeleteShader(fsOptInvSqrt);
    glDeleteShader(fsOptQuad);
    glDeleteShader(fsOptOmni);
    glDeleteShader(fsOptOmniQuad);
    glDeleteShader(vsPerVertexPL);
    glDeleteShader(fsPerVertexPL);
    glDeleteShader(vsLightweight);
    glDeleteShader(fsLightweight);
    if (fsLUTQuad) glDeleteShader(fsLUTQuad);
    if (fsLUTOmniQuad) glDeleteShader(fsLUTOmniQuad);
    glDeleteTextures(1, &texture);
    if (lutTexture) glDeleteTextures(1, &lutTexture);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ibo);

    cleanup(state);
    return 0;
}
