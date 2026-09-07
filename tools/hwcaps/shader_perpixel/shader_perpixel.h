// shader_perpixel.h — shared seam for the gles2_shader_perpixel_benchmark
// tool (T447).
//
// The tool was one 1,463-line TU riding the 1,500-line ceiling. It is now four
// files split by functional phase:
//
//   tools/hwcaps/gles2_shader_perpixel_benchmark.cpp  runBenchmark() + main()
//   tools/hwcaps/shader_perpixel/shader_perpixel_egl.cpp     EGL/DRM/X11
//                                                   context + GLSL compile
//   tools/hwcaps/shader_perpixel/shader_perpixel_shaders.cpp the 13 GLSL ES
//                                                   sources under test
//   tools/hwcaps/shader_perpixel/shader_perpixel_scene.cpp   scene geometry,
//                                                   checker texture, FP16
//                                                   attenuation LUT, matrices
//
// Everything below was `static` in the single TU; nothing else consumes this
// header, so the names keep their original spellings.

#pragma once

#include <cstdint>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#endif

// ---------------------------------------------------------------------------
// EGL/DRM setup (shader_perpixel_egl.cpp)
// ---------------------------------------------------------------------------

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

#ifdef EQT_HAS_DRM
bool initDRM(EGLState& state);
void drmPresent(EGLState& state);
#endif
bool initX11(EGLState& state);
void cleanup(EGLState& state);

GLuint compileShader(GLenum type, const char* src);
GLuint linkProgram(GLuint vs, GLuint fs);

// ---------------------------------------------------------------------------
// The GLSL ES sources under test (shader_perpixel_shaders.cpp)
// ---------------------------------------------------------------------------

extern const char* VS_PERPIXEL;
extern const char* FS_PERPIXEL_BRANCHED;
extern const char* FS_PERPIXEL_BRANCHLESS;
extern const char* FS_OPT_INVERSESQRT;
extern const char* FS_OPT_QUADRATIC;
extern const char* FS_OPT_OMNI;
extern const char* FS_OPT_OMNI_QUAD;
extern const char* VS_PERVERTEX_PLIGHT;
extern const char* FS_PERVERTEX_PLIGHT;
extern const char* VS_LIGHTWEIGHT;
extern const char* FS_LIGHTWEIGHT;
extern const char* FS_LUT_QUADRATIC;
extern const char* FS_LUT_OMNI_QUAD;

// ---------------------------------------------------------------------------
// Scene, texture, LUT and matrices (shader_perpixel_scene.cpp)
// ---------------------------------------------------------------------------

struct Vertex {
    float pos[3];
    float normal[3];
    float color[4];
    float uv[2];
};

void buildAlleyScene(std::vector<Vertex>& verts, std::vector<uint16_t>& indices);
GLuint createCheckerTexture(int size);
bool hasHalfFloatTextures();
GLuint createAttenLUT(float attenConst, float attenQuad, float maxD2);

void mat4Identity(float m[16]);
void mat4Perspective(float m[16], float fovDeg, float aspect, float near, float far);
void mat4LookAt(float m[16], float eyeX, float eyeY, float eyeZ,
                float atX, float atY, float atZ,
                float upX, float upY, float upZ);
void mat4Multiply(float out[16], const float a[16], const float b[16]);
