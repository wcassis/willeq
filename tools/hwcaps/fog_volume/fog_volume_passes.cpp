// fog_volume_passes.cpp -- the fog volume benchmark's two draw passes (the
// opaque zone pass and the alpha-blended fog volume pass) and the three timed
// runners built on them: fog only, zone only, and zone plus fog.
//
// Split out of tools/hwcaps/gles2_fog_volume_benchmark.cpp by T453
// (contiguous range 606..894 of the 1,220-line original, moved verbatim;
// struct BenchResult went to fog_volume_bench.h so it may be defined once, and
// the default arguments of drawFogPass, benchFogOnly and benchZonePlusFog are
// now stated on their declarations there -- a default may be given once).

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


// Draw the zone (opaque pass) — returns nothing, just draws
void drawZonePass(GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount,
                          GLuint texture, float aspect, const float viewProj[16]) {
    glUseProgram(zoneProgram);

    float world[16];
    mat4Identity(world);

    GLint locWVP = glGetUniformLocation(zoneProgram, "mWorldViewProj");
    GLint locWorld = glGetUniformLocation(zoneProgram, "mWorld");
    GLint locSunDir = glGetUniformLocation(zoneProgram, "uSunDir");
    GLint locSunColor = glGetUniformLocation(zoneProgram, "uSunColor");
    GLint locAmbient = glGetUniformLocation(zoneProgram, "uAmbientColor");
    GLint locTint = glGetUniformLocation(zoneProgram, "uTintColor");
    GLint locFogStart = glGetUniformLocation(zoneProgram, "uFogStart");
    GLint locFogEnd = glGetUniformLocation(zoneProgram, "uFogEnd");
    GLint locFogColor = glGetUniformLocation(zoneProgram, "uFogColor");
    GLint locTexture = glGetUniformLocation(zoneProgram, "uTexture");
    GLint locLightPos = glGetUniformLocation(zoneProgram, "uLightPos[0]");
    GLint locLightColor = glGetUniformLocation(zoneProgram, "uLightColor[0]");
    GLint locLightAtten = glGetUniformLocation(zoneProgram, "uLightAtten[0]");

    if (locWVP >= 0) glUniformMatrix4fv(locWVP, 1, GL_FALSE, viewProj);
    if (locWorld >= 0) glUniformMatrix4fv(locWorld, 1, GL_FALSE, world);

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

    // Point lights at fog sphere positions (up to 8)
    float lightPos[8*3] = {}, lightColor[8*3] = {}, lightAtten[8*3] = {};
    for (int i = 0; i < NUM_FOG_SPHERES && i < 8; i++) {
        lightPos[i*3+0] = fogSpheres[i].x;
        lightPos[i*3+1] = fogSpheres[i].y;
        lightPos[i*3+2] = fogSpheres[i].z;
        lightColor[i*3+0] = fogSpheres[i].r * 0.8f;
        lightColor[i*3+1] = fogSpheres[i].g * 0.8f;
        lightColor[i*3+2] = fogSpheres[i].b * 0.8f;
        lightAtten[i*3+0] = 1.0f;
        lightAtten[i*3+1] = 0.0f;
        lightAtten[i*3+2] = 19.0f / (40.0f * 40.0f);
    }
    if (locLightPos >= 0) glUniform3fv(locLightPos, 8, lightPos);
    if (locLightColor >= 0) glUniform3fv(locLightColor, 8, lightColor);
    if (locLightAtten >= 0) glUniform3fv(locLightAtten, 8, lightAtten);

    glBindBuffer(GL_ARRAY_BUFFER, zoneVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zoneIbo);

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
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glDrawElements(GL_TRIANGLES, zoneIndexCount, GL_UNSIGNED_SHORT, nullptr);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
}

// Draw fog volumes (transparent pass)
void drawFogPass(GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                         int numSpheres, float radius,
                         const float viewProj[16], const float cameraPos[3],
                         const float cameraRight[3], const float cameraUp[3],
                         GLuint sqrtLUT, float sqrtLUTScale,
                         GLuint expLUT, float expLUTScale) {
    glUseProgram(fogProgram);

    // Fog volumes are alpha-blended, depth-tested but not depth-written
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive for light-emitting fog
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // don't write depth

    GLint locViewProj = glGetUniformLocation(fogProgram, "uViewProj");
    GLint locCamPos = glGetUniformLocation(fogProgram, "uCameraPos");
    GLint locCamRight = glGetUniformLocation(fogProgram, "uCameraRight");
    GLint locCamUp = glGetUniformLocation(fogProgram, "uCameraUp");
    GLint locCenter = glGetUniformLocation(fogProgram, "uSphereCenter");
    GLint locRadius = glGetUniformLocation(fogProgram, "uSphereRadius");
    GLint locFogColor = glGetUniformLocation(fogProgram, "uFogColor");
    GLint locDensity = glGetUniformLocation(fogProgram, "uFogDensity");

    if (locViewProj >= 0) glUniformMatrix4fv(locViewProj, 1, GL_FALSE, viewProj);
    if (locCamPos >= 0) glUniform3fv(locCamPos, 1, cameraPos);
    if (locCamRight >= 0) glUniform3fv(locCamRight, 1, cameraRight);
    if (locCamUp >= 0) glUniform3fv(locCamUp, 1, cameraUp);
    if (locDensity >= 0) glUniform1f(locDensity, 2.0f);

    // Bind LUT textures if the shader uses them
    GLint locSqrtLUT = glGetUniformLocation(fogProgram, "uSqrtLUT");
    GLint locSqrtScale = glGetUniformLocation(fogProgram, "uSqrtLUTScale");
    GLint locExpLUT = glGetUniformLocation(fogProgram, "uExpLUT");
    GLint locExpScale = glGetUniformLocation(fogProgram, "uExpLUTScale");
    if (locSqrtLUT >= 0 && sqrtLUT > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sqrtLUT);
        glUniform1i(locSqrtLUT, 0);
    }
    if (locSqrtScale >= 0) glUniform1f(locSqrtScale, sqrtLUTScale);
    if (locExpLUT >= 0 && expLUT > 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, expLUT);
        glUniform1i(locExpLUT, 1);
        glActiveTexture(GL_TEXTURE0);
    }
    if (locExpScale >= 0) glUniform1f(locExpScale, expLUTScale);

    glBindBuffer(GL_ARRAY_BUFFER, fogVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fogIbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FogVertex), (void*)0);

    for (int i = 0; i < numSpheres && i < NUM_FOG_SPHERES; i++) {
        float center[] = { fogSpheres[i].x, fogSpheres[i].y, fogSpheres[i].z };
        float color[] = { fogSpheres[i].r, fogSpheres[i].g, fogSpheres[i].b };
        if (locCenter >= 0) glUniform3fv(locCenter, 1, center);
        if (locRadius >= 0) glUniform1f(locRadius, radius);
        if (locFogColor >= 0) glUniform3fv(locFogColor, 1, color);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    glDisableVertexAttribArray(0);

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// Benchmark: fog pass only (clear + fog spheres, no zone geometry)
BenchResult benchFogOnly(EGLState& state, GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                                 int numSpheres, float radius, int numFrames,
                                 const float viewProj[16], const float cameraPos[3],
                                 const float cameraRight[3], const float cameraUp[3],
                                 GLuint sqrtLUT, float sqrtLUTScale,
                                 GLuint expLUT, float expLUTScale) {
    // Warmup
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp,
                    sqrtLUT, sqrtLUTScale, expLUT, expLUTScale);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp,
                    sqrtLUT, sqrtLUTScale, expLUT, expLUTScale);
        glFinish();

#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif

        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
}

// Benchmark: zone + fog combined (realistic scenario)
BenchResult benchZonePlusFog(EGLState& state,
                                     GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount, GLuint texture,
                                     GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                                     int numSpheres, float radius, int numFrames, float aspect,
                                     const float viewProj[16], const float cameraPos[3],
                                     const float cameraRight[3], const float cameraUp[3],
                                     GLuint sqrtLUT, float sqrtLUTScale,
                                     GLuint expLUT, float expLUTScale) {
    glViewport(0, 0, state.width, state.height);

    // Warmup
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp,
                    sqrtLUT, sqrtLUTScale, expLUT, expLUTScale);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp,
                    sqrtLUT, sqrtLUTScale, expLUT, expLUTScale);
        glFinish();

#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif

        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
}

// Benchmark: zone only (baseline for delta computation)
BenchResult benchZoneOnly(EGLState& state,
                                  GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount,
                                  GLuint texture, int numFrames, float aspect, const float viewProj[16]) {
    glViewport(0, 0, state.width, state.height);

    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        glFinish();
#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif
        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
}
