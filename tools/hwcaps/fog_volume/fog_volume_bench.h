// tools/hwcaps/fog_volume/fog_volume_bench.h -- the seam between the four
// translation units of gles2_fog_volume_benchmark.
//
// T453 split tools/hwcaps/gles2_fog_volume_benchmark.cpp (1,220 lines) into
// four contiguous ranges: the EGL/DRM/X11 platform and shader-compile helpers
// (fog_volume_egl.cpp), the LUTs, alley geometry, checker texture, matrix
// helpers and fog-sphere table (fog_volume_scene.cpp), the draw passes and the
// three benchmark runners (fog_volume_passes.cpp), and main() (the original
// file). Every body moved verbatim; the only edits a TU boundary forced are
// the ones here -- the five shared structs, which may be defined once, and the
// declarations below, which is where the `static` keyword and the default
// arguments of drawFogPass/benchFogOnly/benchZonePlusFog now live. This file
// sits beside fog_volume_shaders.h, which T385 created for the same reason.

#pragma once

#include <cstdint>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#ifdef EQT_HAS_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#endif

struct EGLState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
    int width = 1280;
    int height = 720;
#ifdef EQT_HAS_DRM
    int drmFd = -1;
    struct gbm_device* gbmDevice = nullptr;
    struct gbm_surface* gbmSurface = nullptr;
    uint32_t crtcId = 0;
    uint32_t connectorId = 0;
    drmModeModeInfo mode = {};
    bool modeFound = false;
#endif
    bool isDRM = false;
};

// ============================================================================
// Scene geometry (same alley as perpixel benchmark)
// ============================================================================

struct Vertex {
    float pos[3];
    float normal[3];
    float color[4];
    float uv[2];
};

// Fog volume billboard quad (2 triangles, 4 vertices)
struct FogVertex {
    float pos[3];  // (-1,-1,0), (1,-1,0), (-1,1,0), (1,1,0)
};

// ============================================================================
// Fog sphere positions along the alley
// ============================================================================

struct FogSphere {
    float x, y, z;   // center (Irrlicht Y-up)
    float r, g, b;   // fog color
};

// ============================================================================
// Benchmark runner
// ============================================================================

struct BenchResult {
    double avgMs;
    double minMs;
    double maxMs;
};

// ---------------------------------------------------------------------------
// fog_volume_egl.cpp
bool initDRM(EGLState& state);
void drmPresent(EGLState& state);
bool initX11(EGLState& state);
void cleanup(EGLState& state);
GLuint compileShader(GLenum type, const char* src);
GLuint linkProgram(GLuint vs, GLuint fs);

// ---------------------------------------------------------------------------
// fog_volume_scene.cpp
bool hasHalfFloatTextures();
GLuint createSqrtLUT(float maxVal);
GLuint createExpLUT(float maxVal);
void buildAlleyScene(std::vector<Vertex>& verts, std::vector<uint16_t>& indices);
void buildFogQuad(GLuint& vbo, GLuint& ibo);
GLuint createCheckerTexture(int size);
void mat4Identity(float m[16]);
void mat4Perspective(float m[16], float fovDeg, float aspect, float near, float far);
void mat4LookAt(float m[16], float eyeX, float eyeY, float eyeZ,
                float atX, float atY, float atZ,
                float upX, float upY, float upZ);
void mat4Multiply(float out[16], const float a[16], const float b[16]);
extern const FogSphere fogSpheres[];
extern const int NUM_FOG_SPHERES;

// ---------------------------------------------------------------------------
// fog_volume_passes.cpp
void drawZonePass(GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount,
                  GLuint texture, float aspect, const float viewProj[16]);
void drawFogPass(GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                 int numSpheres, float radius,
                 const float viewProj[16], const float cameraPos[3],
                 const float cameraRight[3], const float cameraUp[3],
                 GLuint sqrtLUT = 0, float sqrtLUTScale = 0.0f,
                 GLuint expLUT = 0, float expLUTScale = 0.0f);
BenchResult benchFogOnly(EGLState& state, GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                         int numSpheres, float radius, int numFrames,
                         const float viewProj[16], const float cameraPos[3],
                         const float cameraRight[3], const float cameraUp[3],
                         GLuint sqrtLUT = 0, float sqrtLUTScale = 0.0f,
                         GLuint expLUT = 0, float expLUTScale = 0.0f);
BenchResult benchZonePlusFog(EGLState& state,
                             GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount, GLuint texture,
                             GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                             int numSpheres, float radius, int numFrames, float aspect,
                             const float viewProj[16], const float cameraPos[3],
                             const float cameraRight[3], const float cameraUp[3],
                             GLuint sqrtLUT = 0, float sqrtLUTScale = 0.0f,
                             GLuint expLUT = 0, float expLUTScale = 0.0f);
BenchResult benchZoneOnly(EGLState& state,
                          GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount,
                          GLuint texture, int numFrames, float aspect, const float viewProj[16]);

