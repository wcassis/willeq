// tools/gl_points/gl_points.h -- the seam between the three translation units
// of test_gl_points.
//
// T453 split tools/test_gl_points.cpp (1,206 lines) into three contiguous
// ranges: the EGL/DRM/X11 bring-up and GL error/shader helpers
// (gl_points_egl.cpp), the FBO, the pixel readback analyser, the four GLSL ES
// sources and the pass/fail tally (gl_points_fbo.cpp), and the six-test main()
// (the original file). Every body moved verbatim; the only edits a TU boundary
// forced are the ones here -- the four shared structs, which may be defined
// once, and the declarations below, which is where the `static` keyword and
// analyzeRegion's default tolerance now live. main() was left whole: its six
// test blocks share locals, so cutting it would not be a structural move.

#pragma once

#include <cstdint>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#ifdef EQT_HAS_DRM
#include <gbm.h>
#endif

// ============================================================
// EGL context management (same pattern as other hwcaps tools)
// ============================================================

struct EGLState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
#ifdef EQT_HAS_DRM
    int drmFd = -1;
    struct gbm_device* gbmDevice = nullptr;
    struct gbm_surface* gbmSurface = nullptr;
#endif
    bool isDRM = false;
};

// ============================================================
// FBO setup
// ============================================================

struct FBO {
    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthRb = 0;
    int width = 0;
    int height = 0;
};

// ============================================================
// Pixel readback and analysis
// ============================================================

struct PixelStats {
    int totalPixels;
    int nonBlackPixels;     // any pixel with R+G+B > 0
    int matchingPixels;     // pixels matching expected color within tolerance
    uint8_t maxR, maxG, maxB, maxA;
    uint8_t sampleR, sampleG, sampleB, sampleA;  // first non-black pixel found
    int sampleX, sampleY;
};

struct TestResult {
    const char* name;
    bool passed;
    int pixelsFound;
    int pixelsExpectedMin;
    char detail[256];
};

// ---------------------------------------------------------------------------
// gl_points_egl.cpp
#ifdef EQT_HAS_DRM
bool initDRM(EGLState& state);
#endif
bool initX11(EGLState& state);
void cleanup(EGLState& state);
void drainGLErrors();
bool checkGLError(const char* context);
GLuint compileShader(GLenum type, const char* source, const char* label);
GLuint linkProgram(GLuint vs, GLuint fs, const char* label);

// ---------------------------------------------------------------------------
// gl_points_fbo.cpp
bool createFBO(FBO& fbo, int width, int height);
void destroyFBO(FBO& fbo);
PixelStats analyzeRegion(const std::vector<uint8_t>& pixels, int fbWidth, int fbHeight,
                         int rx, int ry, int rw, int rh,
                         uint8_t expectR, uint8_t expectG, uint8_t expectB,
                         int tolerance = 20);
extern const char* basicVS;
extern const char* basicFS;
extern const char* pointCoordVS;
extern const char* pointCoordFS;
extern int g_numPassed;
extern int g_numFailed;
extern int g_numTotal;
void reportTest(const TestResult& t);

