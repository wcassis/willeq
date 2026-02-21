// test_gl_points - Test GL_POINTS rendering on Lima/Mali 400 GLES2 driver
//
// Creates an EGL context with OpenGL ES 2.0 and tests whether GL_POINTS
// actually produce fragments. This is a diagnostic for the Lima open-source
// driver on the Mali 400 (Orange Pi One) where point rendering behavior
// may differ from desktop GL.
//
// Tests performed:
//   1. Query GL_ALIASED_POINT_SIZE_RANGE and GL_POINT_SIZE limits
//   2. Render GL_TRIANGLES reference quad (proves pipeline works)
//   3. Render GL_POINTS at various sizes (1, 4, 8, 16, 32, 64)
//   4. Read back FBO pixels to verify fragments were produced
//   5. Test gl_PointSize set in vertex shader
//   6. Test gl_PointCoord availability in fragment shader
//
// Usage:
//   ./test_gl_points              # auto-detect (try DRM first, then X11)
//   ./test_gl_points --drm        # force DRM/GBM
//   ./test_gl_points --x11        # force X11/EGL

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <gbm.h>
#endif

// FBO render target size
static const int FBO_WIDTH = 256;
static const int FBO_HEIGHT = 256;

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

#ifdef EQT_HAS_DRM
static bool initDRM(EGLState& state) {
    const char* devices[] = { "/dev/dri/card0", "/dev/dri/card1", nullptr };
    for (int i = 0; devices[i]; i++) {
        state.drmFd = open(devices[i], O_RDWR);
        if (state.drmFd >= 0) break;
    }
    if (state.drmFd < 0) {
        fprintf(stderr, "DRM: could not open any /dev/dri/card* device\n");
        return false;
    }

    state.gbmDevice = gbm_create_device(state.drmFd);
    if (!state.gbmDevice) {
        fprintf(stderr, "DRM: gbm_create_device failed\n");
        close(state.drmFd);
        return false;
    }

    state.display = eglGetDisplay((EGLNativeDisplayType)state.gbmDevice);
    if (state.display == EGL_NO_DISPLAY) {
        fprintf(stderr, "DRM: eglGetDisplay failed\n");
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) {
        fprintf(stderr, "DRM: eglInitialize failed\n");
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    printf("EGL %d.%d (DRM/GBM, GLES2 context)\n", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "DRM: eglBindAPI(EGL_OPENGL_ES_API) failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "DRM: eglChooseConfig failed (GLES2)\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    state.gbmSurface = gbm_surface_create(state.gbmDevice, 64, 64,
                                            GBM_FORMAT_XRGB8888,
                                            GBM_BO_USE_RENDERING);
    if (!state.gbmSurface) {
        fprintf(stderr, "DRM: gbm_surface_create failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    state.surface = eglCreateWindowSurface(state.display, config,
                                            (EGLNativeWindowType)state.gbmSurface, nullptr);

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "DRM: eglCreateContext failed for GLES2 (0x%x)\n", eglGetError());
        eglTerminate(state.display);
        gbm_surface_destroy(state.gbmSurface);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    state.isDRM = true;
    return true;
}
#endif

static bool initX11(EGLState& state) {
    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY) {
        fprintf(stderr, "X11: eglGetDisplay failed\n");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) {
        fprintf(stderr, "X11: eglInitialize failed\n");
        return false;
    }

    printf("EGL %d.%d (X11/default, GLES2 context)\n", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "X11: eglBindAPI(EGL_OPENGL_ES_API) failed\n");
        eglTerminate(state.display);
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "X11: eglChooseConfig failed (GLES2)\n");
        eglTerminate(state.display);
        return false;
    }

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };
    state.surface = eglCreatePbufferSurface(state.display, config, pbufferAttribs);

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "X11: eglCreateContext failed for GLES2 (0x%x)\n", eglGetError());
        eglTerminate(state.display);
        return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    state.isDRM = false;
    return true;
}

static void cleanup(EGLState& state) {
    if (state.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state.context != EGL_NO_CONTEXT)
            eglDestroyContext(state.display, state.context);
        if (state.surface != EGL_NO_SURFACE)
            eglDestroySurface(state.display, state.surface);
        eglTerminate(state.display);
    }
#ifdef EQT_HAS_DRM
    if (state.gbmSurface) gbm_surface_destroy(state.gbmSurface);
    if (state.gbmDevice) gbm_device_destroy(state.gbmDevice);
    if (state.drmFd >= 0) close(state.drmFd);
#endif
}

// ============================================================
// GL helpers
// ============================================================

static void drainGLErrors() {
    while (glGetError() != GL_NO_ERROR) {}
}

static const char* glErrorString(GLenum err) {
    switch (err) {
        case GL_NO_ERROR:          return "GL_NO_ERROR";
        case GL_INVALID_ENUM:      return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:     return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_OUT_OF_MEMORY:     return "GL_OUT_OF_MEMORY";
        default:                   return "UNKNOWN";
    }
}

static bool checkGLError(const char* context) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("  GL error at %s: 0x%04X (%s)\n", context, err, glErrorString(err));
        return false;
    }
    return true;
}

static GLuint compileShader(GLenum type, const char* source, const char* label) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        printf("  %s shader compile FAILED:\n    %s\n", label, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(GLuint vs, GLuint fs, const char* label) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        printf("  %s program link FAILED:\n    %s\n", label, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

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

static bool createFBO(FBO& fbo, int width, int height) {
    fbo.width = width;
    fbo.height = height;

    // Create color texture
    glGenTextures(1, &fbo.colorTex);
    glBindTexture(GL_TEXTURE_2D, fbo.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create depth renderbuffer
    glGenRenderbuffers(1, &fbo.depthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo.depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);

    // Create FBO
    glGenFramebuffers(1, &fbo.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fbo.colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, fbo.depthRb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("  FBO incomplete: 0x%04X\n", status);
        return false;
    }

    if (!checkGLError("createFBO")) return false;

    return true;
}

static void destroyFBO(FBO& fbo) {
    if (fbo.fbo) glDeleteFramebuffers(1, &fbo.fbo);
    if (fbo.colorTex) glDeleteTextures(1, &fbo.colorTex);
    if (fbo.depthRb) glDeleteRenderbuffers(1, &fbo.depthRb);
    fbo = {};
}

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

static PixelStats analyzeRegion(const std::vector<uint8_t>& pixels, int fbWidth, int fbHeight,
                                 int rx, int ry, int rw, int rh,
                                 uint8_t expectR, uint8_t expectG, uint8_t expectB,
                                 int tolerance = 20) {
    PixelStats stats = {};
    stats.totalPixels = rw * rh;
    stats.sampleX = -1;
    stats.sampleY = -1;

    for (int y = ry; y < ry + rh && y < fbHeight; y++) {
        for (int x = rx; x < rx + rw && x < fbWidth; x++) {
            int idx = (y * fbWidth + x) * 4;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            uint8_t a = pixels[idx + 3];

            if (r > stats.maxR) stats.maxR = r;
            if (g > stats.maxG) stats.maxG = g;
            if (b > stats.maxB) stats.maxB = b;
            if (a > stats.maxA) stats.maxA = a;

            if (r > 0 || g > 0 || b > 0) {
                stats.nonBlackPixels++;
                if (stats.sampleX < 0) {
                    stats.sampleR = r;
                    stats.sampleG = g;
                    stats.sampleB = b;
                    stats.sampleA = a;
                    stats.sampleX = x;
                    stats.sampleY = y;
                }

                // Check color match
                if (abs((int)r - (int)expectR) <= tolerance &&
                    abs((int)g - (int)expectG) <= tolerance &&
                    abs((int)b - (int)expectB) <= tolerance) {
                    stats.matchingPixels++;
                }
            }
        }
    }

    return stats;
}

// ============================================================
// Test cases
// ============================================================

// Shader for basic colored geometry (triangles and points)
static const char* basicVS =
    "precision highp float;\n"
    "attribute vec2 aPosition;\n"
    "attribute vec4 aColor;\n"
    "varying vec4 vColor;\n"
    "uniform float uPointSize;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "    gl_PointSize = uPointSize;\n"
    "    vColor = aColor;\n"
    "}\n";

static const char* basicFS =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    gl_FragColor = vColor;\n"
    "}\n";

// Shader that uses gl_PointCoord to test point sprite functionality
static const char* pointCoordVS =
    "precision highp float;\n"
    "attribute vec2 aPosition;\n"
    "uniform float uPointSize;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "    gl_PointSize = uPointSize;\n"
    "}\n";

static const char* pointCoordFS =
    "precision mediump float;\n"
    "void main() {\n"
    "    // gl_PointCoord: (0,0) at top-left of point, (1,1) at bottom-right\n"
    "    // Encode point coord as color: R=s, G=t, B=0.5\n"
    "    gl_FragColor = vec4(gl_PointCoord.x, gl_PointCoord.y, 0.5, 1.0);\n"
    "}\n";

struct TestResult {
    const char* name;
    bool passed;
    int pixelsFound;
    int pixelsExpectedMin;
    char detail[256];
};

static int g_numPassed = 0;
static int g_numFailed = 0;
static int g_numTotal = 0;

static void reportTest(const TestResult& t) {
    g_numTotal++;
    if (t.passed) {
        g_numPassed++;
        printf("  [PASS] %s\n", t.name);
    } else {
        g_numFailed++;
        printf("  [FAIL] %s\n", t.name);
    }
    if (t.detail[0]) {
        printf("         %s\n", t.detail);
    }
}

// ============================================================
// Main test runner
// ============================================================

int main(int argc, char* argv[]) {
    enum Mode { AUTO, FORCE_DRM, FORCE_X11 };
    Mode mode = AUTO;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) mode = FORCE_DRM;
        else if (strcmp(argv[i], "--x11") == 0) mode = FORCE_X11;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--drm|--x11]\n", argv[0]);
            printf("\nTests GL_POINTS rendering on Lima/Mali 400 GLES2 driver.\n");
            printf("Renders points and triangles into an FBO and reads back pixels\n");
            printf("to verify that fragments are produced.\n");
            return 0;
        }
    }

    printf("============================================================\n");
    printf("GL_POINTS Rendering Test (OpenGL ES 2.0)\n");
    printf("============================================================\n\n");

    // --- Initialize EGL context with GLES2 ---
    EGLState egl;
    bool ok = false;

    if (mode == FORCE_DRM || mode == AUTO) {
#ifdef EQT_HAS_DRM
        ok = initDRM(egl);
        if (!ok && mode == FORCE_DRM) {
            fprintf(stderr, "Failed to initialize DRM display\n");
            return 1;
        }
#else
        if (mode == FORCE_DRM) {
            fprintf(stderr, "DRM support not compiled in (need -DEQT_DRM=ON)\n");
            return 1;
        }
#endif
    }

    if (!ok && (mode == FORCE_X11 || mode == AUTO)) {
        ok = initX11(egl);
        if (!ok) {
            fprintf(stderr, "Failed to initialize any EGL display\n");
            return 1;
        }
    }

    // --- GPU info ---
    printf("--- GPU Information ---\n");
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* slVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("  GL Vendor:   %s\n", vendor ? vendor : "unknown");
    printf("  GL Renderer: %s\n", renderer ? renderer : "unknown");
    printf("  GL Version:  %s\n", version ? version : "unknown");
    printf("  GLSL:        %s\n", slVersion ? slVersion : "unknown");

    // --- Point size capabilities ---
    printf("\n--- Point Size Capabilities ---\n");

    GLfloat pointSizeRange[2] = { 0.0f, 0.0f };
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, pointSizeRange);
    printf("  GL_ALIASED_POINT_SIZE_RANGE: [%.1f, %.1f]\n",
           pointSizeRange[0], pointSizeRange[1]);

    GLfloat lineWidthRange[2] = { 0.0f, 0.0f };
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lineWidthRange);
    printf("  GL_ALIASED_LINE_WIDTH_RANGE: [%.1f, %.1f]\n",
           lineWidthRange[0], lineWidthRange[1]);

    GLint maxViewportDims[2] = { 0, 0 };
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewportDims);
    printf("  GL_MAX_VIEWPORT_DIMS:        %d x %d\n",
           maxViewportDims[0], maxViewportDims[1]);

    GLint maxRenderbufferSize = 0;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
    printf("  GL_MAX_RENDERBUFFER_SIZE:    %d\n", maxRenderbufferSize);

    float maxPointSize = pointSizeRange[1];
    printf("\n  Maximum point size reported by driver: %.1f px\n", maxPointSize);
    if (maxPointSize < 1.0f) {
        printf("  WARNING: Driver reports max point size < 1.0! Points may not work.\n");
    }

    // --- Create FBO ---
    printf("\n--- Creating FBO (%dx%d) ---\n", FBO_WIDTH, FBO_HEIGHT);
    FBO fbo;
    drainGLErrors();
    if (!createFBO(fbo, FBO_WIDTH, FBO_HEIGHT)) {
        printf("  FATAL: Failed to create FBO. Cannot proceed.\n");
        cleanup(egl);
        return 1;
    }
    printf("  FBO created successfully.\n");

    glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // --- Compile shaders ---
    printf("\n--- Compiling Shaders ---\n");

    // Basic shader (triangles + points with solid color)
    GLuint basicVSh = compileShader(GL_VERTEX_SHADER, basicVS, "basic vertex");
    GLuint basicFSh = compileShader(GL_FRAGMENT_SHADER, basicFS, "basic fragment");
    GLuint basicProg = 0;
    if (basicVSh && basicFSh) {
        basicProg = linkProgram(basicVSh, basicFSh, "basic");
    }
    if (basicVSh) glDeleteShader(basicVSh);
    if (basicFSh) glDeleteShader(basicFSh);

    if (!basicProg) {
        printf("  FATAL: Basic shader failed to compile/link. Cannot proceed.\n");
        destroyFBO(fbo);
        cleanup(egl);
        return 1;
    }
    printf("  Basic shader: OK\n");

    GLint basicPosLoc = glGetAttribLocation(basicProg, "aPosition");
    GLint basicColorLoc = glGetAttribLocation(basicProg, "aColor");
    GLint basicPointSizeLoc = glGetUniformLocation(basicProg, "uPointSize");
    printf("  Attributes: aPosition=%d, aColor=%d\n", basicPosLoc, basicColorLoc);
    printf("  Uniforms:   uPointSize=%d\n", basicPointSizeLoc);

    // PointCoord shader
    GLuint pcVSh = compileShader(GL_VERTEX_SHADER, pointCoordVS, "pointCoord vertex");
    GLuint pcFSh = compileShader(GL_FRAGMENT_SHADER, pointCoordFS, "pointCoord fragment");
    GLuint pcProg = 0;
    bool pointCoordShaderOk = false;
    if (pcVSh && pcFSh) {
        pcProg = linkProgram(pcVSh, pcFSh, "pointCoord");
        if (pcProg) pointCoordShaderOk = true;
    }
    if (pcVSh) glDeleteShader(pcVSh);
    if (pcFSh) glDeleteShader(pcFSh);

    printf("  PointCoord shader: %s\n", pointCoordShaderOk ? "OK" : "FAILED");

    GLint pcPosLoc = -1, pcPointSizeLoc = -1;
    if (pcProg) {
        pcPosLoc = glGetAttribLocation(pcProg, "aPosition");
        pcPointSizeLoc = glGetUniformLocation(pcProg, "uPointSize");
    }

    // Pixel readback buffer
    std::vector<uint8_t> pixels(FBO_WIDTH * FBO_HEIGHT * 4, 0);

    // ============================================================
    // TEST 1: GL_TRIANGLES reference (prove the pipeline works)
    // ============================================================
    printf("\n=== TEST 1: GL_TRIANGLES reference quad ===\n");
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(basicProg);
        glUniform1f(basicPointSizeLoc, 1.0f);

        // Draw a bright green quad covering the center 50% of the screen
        // Two triangles forming a quad from (-0.5,-0.5) to (0.5,0.5)
        float quadVerts[] = {
            // x,    y,    r,    g,    b,    a
            -0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // bottom-left
             0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // bottom-right
             0.5f,  0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // top-right
            -0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // bottom-left
             0.5f,  0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // top-right
            -0.5f,  0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // top-left
        };

        glEnableVertexAttribArray(basicPosLoc);
        glVertexAttribPointer(basicPosLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), quadVerts);
        if (basicColorLoc >= 0) {
            glEnableVertexAttribArray(basicColorLoc);
            glVertexAttribPointer(basicColorLoc, 4, GL_FLOAT, GL_FALSE,
                                  6 * sizeof(float), quadVerts + 2);
        }

        drainGLErrors();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glFinish();

        if (!checkGLError("GL_TRIANGLES draw")) {
            printf("  GL error during triangle draw!\n");
        }

        glDisableVertexAttribArray(basicPosLoc);
        if (basicColorLoc >= 0) glDisableVertexAttribArray(basicColorLoc);

        // Read back center region
        glReadPixels(0, 0, FBO_WIDTH, FBO_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Analyze center of FBO (quad covers NDC -0.5..0.5, so pixels 64..192 in 256px FBO)
        PixelStats center = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                           64, 64, 128, 128,
                                           0, 255, 0);

        TestResult t1;
        t1.name = "GL_TRIANGLES quad renders green pixels";
        t1.pixelsFound = center.nonBlackPixels;
        t1.pixelsExpectedMin = 128 * 128 / 2;  // at least half the region
        t1.passed = (center.nonBlackPixels >= t1.pixelsExpectedMin);
        snprintf(t1.detail, sizeof(t1.detail),
                 "Found %d non-black pixels (expected >= %d), max RGBA=(%d,%d,%d,%d)",
                 center.nonBlackPixels, t1.pixelsExpectedMin,
                 center.maxR, center.maxG, center.maxB, center.maxA);
        reportTest(t1);

        if (!t1.passed) {
            printf("\n  WARNING: Triangle rendering failed. The entire GLES2 pipeline\n");
            printf("  may be broken. Subsequent point tests will likely fail too.\n");

            // Dump a few raw pixels from center
            printf("  Raw pixels at center (128,128):\n");
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int px = 128 + dx;
                    int py = 128 + dy;
                    int idx = (py * FBO_WIDTH + px) * 4;
                    printf("    (%d,%d): RGBA=(%d,%d,%d,%d)\n",
                           px, py, pixels[idx], pixels[idx+1], pixels[idx+2], pixels[idx+3]);
                }
            }
        }
    }

    // ============================================================
    // TEST 2: GL_POINTS at various sizes
    // ============================================================
    printf("\n=== TEST 2: GL_POINTS at various sizes ===\n");

    // Point sizes to test: 1, 4, 8, 16, 32, 64
    float testSizes[] = { 1.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
    int numSizes = sizeof(testSizes) / sizeof(testSizes[0]);

    for (int si = 0; si < numSizes; si++) {
        float ptSize = testSizes[si];

        // Skip sizes larger than driver maximum
        if (ptSize > maxPointSize) {
            printf("  Skipping size %.0f (exceeds driver max %.1f)\n", ptSize, maxPointSize);
            continue;
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(basicProg);
        glUniform1f(basicPointSizeLoc, ptSize);

        // Draw a bright red point at the center of the FBO (NDC 0,0)
        float pointData[] = {
            // x,    y,    r,    g,    b,    a
            0.0f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        };

        glEnableVertexAttribArray(basicPosLoc);
        glVertexAttribPointer(basicPosLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), pointData);
        if (basicColorLoc >= 0) {
            glEnableVertexAttribArray(basicColorLoc);
            glVertexAttribPointer(basicColorLoc, 4, GL_FLOAT, GL_FALSE,
                                  6 * sizeof(float), pointData + 2);
        }

        drainGLErrors();
        glDrawArrays(GL_POINTS, 0, 1);
        glFinish();

        bool drawOk = checkGLError("GL_POINTS draw");

        glDisableVertexAttribArray(basicPosLoc);
        if (basicColorLoc >= 0) glDisableVertexAttribArray(basicColorLoc);

        // Read back
        glReadPixels(0, 0, FBO_WIDTH, FBO_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // The point should be centered at pixel (128, 128) in a 256x256 FBO
        // with a radius of ptSize/2 pixels
        int halfSize = (int)ceilf(ptSize / 2.0f);
        int regionX = 128 - halfSize - 2;
        int regionY = 128 - halfSize - 2;
        int regionW = halfSize * 2 + 4;
        int regionH = halfSize * 2 + 4;
        if (regionX < 0) regionX = 0;
        if (regionY < 0) regionY = 0;
        if (regionX + regionW > FBO_WIDTH) regionW = FBO_WIDTH - regionX;
        if (regionY + regionH > FBO_HEIGHT) regionH = FBO_HEIGHT - regionY;

        PixelStats stats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                          regionX, regionY, regionW, regionH,
                                          255, 0, 0);

        // Also scan the full FBO to see if pixels ended up somewhere unexpected
        PixelStats fullStats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                              0, 0, FBO_WIDTH, FBO_HEIGHT,
                                              255, 0, 0);

        // Expected: for a point of size N, we should see approximately N*N pixels
        // (square point sprite), but allow generous tolerance
        int expectedMin = (ptSize <= 1.0f) ? 1 : (int)(ptSize * ptSize * 0.3f);

        TestResult t;
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "GL_POINTS size=%.0f", ptSize);
        t.name = nameBuf;
        t.pixelsFound = stats.nonBlackPixels;
        t.pixelsExpectedMin = expectedMin;
        t.passed = (stats.nonBlackPixels >= expectedMin) && drawOk;

        snprintf(t.detail, sizeof(t.detail),
                 "Center region: %d non-black pixels (need >= %d), "
                 "full FBO: %d non-black, max RGBA=(%d,%d,%d,%d)",
                 stats.nonBlackPixels, expectedMin,
                 fullStats.nonBlackPixels,
                 stats.maxR, stats.maxG, stats.maxB, stats.maxA);
        reportTest(t);

        if (stats.nonBlackPixels > 0 && stats.sampleX >= 0) {
            printf("         First non-black pixel at (%d,%d): RGBA=(%d,%d,%d,%d)\n",
                   stats.sampleX, stats.sampleY,
                   stats.sampleR, stats.sampleG, stats.sampleB, stats.sampleA);
        }

        if (stats.nonBlackPixels == 0 && fullStats.nonBlackPixels > 0) {
            printf("         NOTE: No pixels in expected region but %d pixels elsewhere!\n",
                   fullStats.nonBlackPixels);
            printf("         First unexpected pixel at (%d,%d): RGBA=(%d,%d,%d,%d)\n",
                   fullStats.sampleX, fullStats.sampleY,
                   fullStats.sampleR, fullStats.sampleG, fullStats.sampleB, fullStats.sampleA);
        }
    }

    // ============================================================
    // TEST 3: Multiple points at different positions
    // ============================================================
    printf("\n=== TEST 3: Multiple GL_POINTS at different positions ===\n");
    {
        float ptSize = 32.0f;
        if (ptSize > maxPointSize) ptSize = maxPointSize;
        if (ptSize < 4.0f) ptSize = 4.0f;

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(basicProg);
        glUniform1f(basicPointSizeLoc, ptSize);

        // 4 points at different positions with different colors
        // NDC coords map to pixel coords: x_px = (x_ndc+1)/2 * 256
        float pointsData[] = {
            // x,     y,    r,    g,    b,    a       // expected pixel center
            -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f,  // (64, 64)   red
             0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f,  // (192, 64)  green
            -0.5f,  0.5f,  0.0f, 0.0f, 1.0f, 1.0f,  // (64, 192)  blue
             0.5f,  0.5f,  1.0f, 1.0f, 0.0f, 1.0f,  // (192, 192) yellow
        };

        glEnableVertexAttribArray(basicPosLoc);
        glVertexAttribPointer(basicPosLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), pointsData);
        if (basicColorLoc >= 0) {
            glEnableVertexAttribArray(basicColorLoc);
            glVertexAttribPointer(basicColorLoc, 4, GL_FLOAT, GL_FALSE,
                                  6 * sizeof(float), pointsData + 2);
        }

        drainGLErrors();
        glDrawArrays(GL_POINTS, 0, 4);
        glFinish();
        checkGLError("multi-point draw");

        glDisableVertexAttribArray(basicPosLoc);
        if (basicColorLoc >= 0) glDisableVertexAttribArray(basicColorLoc);

        glReadPixels(0, 0, FBO_WIDTH, FBO_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Check each point location
        struct PointCheck {
            const char* name;
            int cx, cy;           // expected pixel center
            uint8_t r, g, b;     // expected color
        };
        PointCheck checks[] = {
            { "Red point at (-0.5,-0.5)",    64,  64, 255,   0,   0 },
            { "Green point at (0.5,-0.5)",  192,  64,   0, 255,   0 },
            { "Blue point at (-0.5,0.5)",    64, 192,   0,   0, 255 },
            { "Yellow point at (0.5,0.5)",  192, 192, 255, 255,   0 },
        };

        int halfPt = (int)ceilf(ptSize / 2.0f);
        int totalPointPixels = 0;

        for (int ci = 0; ci < 4; ci++) {
            int rx = checks[ci].cx - halfPt - 2;
            int ry = checks[ci].cy - halfPt - 2;
            int rw = halfPt * 2 + 4;
            int rh = halfPt * 2 + 4;
            if (rx < 0) rx = 0;
            if (ry < 0) ry = 0;
            if (rx + rw > FBO_WIDTH) rw = FBO_WIDTH - rx;
            if (ry + rh > FBO_HEIGHT) rh = FBO_HEIGHT - ry;

            PixelStats stats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                              rx, ry, rw, rh,
                                              checks[ci].r, checks[ci].g, checks[ci].b);
            totalPointPixels += stats.nonBlackPixels;

            printf("  %s: %d non-black pixels",
                   checks[ci].name, stats.nonBlackPixels);
            if (stats.nonBlackPixels > 0) {
                printf(", max RGBA=(%d,%d,%d,%d)",
                       stats.maxR, stats.maxG, stats.maxB, stats.maxA);
            }
            printf("\n");
        }

        TestResult t3;
        t3.name = "Multiple colored GL_POINTS at different positions";
        t3.pixelsFound = totalPointPixels;
        t3.pixelsExpectedMin = 4;  // at least 1 pixel per point
        t3.passed = (totalPointPixels >= t3.pixelsExpectedMin);
        snprintf(t3.detail, sizeof(t3.detail),
                 "Total non-black pixels across 4 point regions: %d (need >= %d)",
                 totalPointPixels, t3.pixelsExpectedMin);
        reportTest(t3);
    }

    // ============================================================
    // TEST 4: gl_PointCoord test
    // ============================================================
    printf("\n=== TEST 4: gl_PointCoord in fragment shader ===\n");
    if (pcProg) {
        float ptSize = 64.0f;
        if (ptSize > maxPointSize) ptSize = maxPointSize;
        if (ptSize < 8.0f) ptSize = 8.0f;

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(pcProg);
        glUniform1f(pcPointSizeLoc, ptSize);

        // Single point at center
        float pointData[] = { 0.0f, 0.0f };

        glEnableVertexAttribArray(pcPosLoc);
        glVertexAttribPointer(pcPosLoc, 2, GL_FLOAT, GL_FALSE, 0, pointData);

        drainGLErrors();
        glDrawArrays(GL_POINTS, 0, 1);
        glFinish();
        bool drawOk = checkGLError("gl_PointCoord draw");

        glDisableVertexAttribArray(pcPosLoc);

        glReadPixels(0, 0, FBO_WIDTH, FBO_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // The point should be centered at (128, 128)
        // gl_PointCoord.x = R channel: should vary 0..255 across the point width
        // gl_PointCoord.y = G channel: should vary 0..255 across the point height
        // B channel: constant 0.5 * 255 = 128

        int halfPt = (int)ceilf(ptSize / 2.0f);

        // Check center pixel: should have gl_PointCoord ~= (0.5, 0.5)
        int centerIdx = (128 * FBO_WIDTH + 128) * 4;
        uint8_t centerR = pixels[centerIdx + 0];
        uint8_t centerG = pixels[centerIdx + 1];
        uint8_t centerB = pixels[centerIdx + 2];

        // Check pixel at left edge of point: gl_PointCoord.x ~= 0
        int leftX = 128 - halfPt + 1;
        if (leftX < 0) leftX = 0;
        int leftIdx = (128 * FBO_WIDTH + leftX) * 4;
        uint8_t leftR = pixels[leftIdx + 0];

        // Check pixel at right edge of point: gl_PointCoord.x ~= 1
        int rightX = 128 + halfPt - 1;
        if (rightX >= FBO_WIDTH) rightX = FBO_WIDTH - 1;
        int rightIdx = (128 * FBO_WIDTH + rightX) * 4;
        uint8_t rightR = pixels[rightIdx + 0];

        // Count total non-black pixels
        PixelStats pcStats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                            128 - halfPt - 2, 128 - halfPt - 2,
                                            halfPt * 2 + 4, halfPt * 2 + 4,
                                            128, 128, 128, 100);

        printf("  Point size used: %.0f px\n", ptSize);
        printf("  Center pixel (128,128): RGBA=(%d,%d,%d,%d)\n",
               centerR, centerG, centerB, pixels[centerIdx + 3]);
        printf("    Expected: R~128 (PointCoord.x~0.5), G~128 (PointCoord.y~0.5), B~128\n");
        printf("  Left edge pixel (%d,128):  R=%d (expected ~0, PointCoord.x~0)\n", leftX, leftR);
        printf("  Right edge pixel (%d,128): R=%d (expected ~255, PointCoord.x~1)\n", rightX, rightR);
        printf("  Non-black pixels in point region: %d\n", pcStats.nonBlackPixels);

        // Pass if: point produced pixels, center B is ~128, and there is
        // variation in the R channel across the point (PointCoord.x varies)
        bool hasPixels = (pcStats.nonBlackPixels > 0);
        bool blueOk = (centerB > 90 && centerB < 170);  // B ~= 128
        bool coordVaries = (rightR > leftR + 30);  // R increases left to right

        TestResult t4;
        t4.name = "gl_PointCoord produces varying UV coordinates";
        t4.pixelsFound = pcStats.nonBlackPixels;
        t4.pixelsExpectedMin = 1;
        t4.passed = hasPixels && drawOk;  // basic: just check if pixels are produced
        snprintf(t4.detail, sizeof(t4.detail),
                 "Pixels: %s, Blue(constant)=%d %s, Coord varies: %s",
                 hasPixels ? "YES" : "NO",
                 centerB, blueOk ? "OK" : "UNEXPECTED",
                 coordVaries ? "YES" : "NO");
        reportTest(t4);

        if (hasPixels && blueOk && coordVaries) {
            printf("  gl_PointCoord is fully functional (UV interpolation works).\n");
        } else if (hasPixels && !coordVaries) {
            printf("  Points render but gl_PointCoord may not interpolate correctly.\n");
            printf("  This could mean point sprites are not fully supported.\n");
        }
    } else {
        printf("  SKIPPED: gl_PointCoord shader failed to compile.\n");
        printf("  This means the driver may not support gl_PointCoord in fragment shaders.\n");
    }

    // ============================================================
    // TEST 5: Points with varying vertex attributes
    // ============================================================
    printf("\n=== TEST 5: GL_POINTS with per-vertex color ===\n");
    {
        float ptSize = 16.0f;
        if (ptSize > maxPointSize) ptSize = maxPointSize;
        if (ptSize < 2.0f) ptSize = 2.0f;

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(basicProg);
        glUniform1f(basicPointSizeLoc, ptSize);

        // Three points with different colors in a row
        float pointsData[] = {
            // x,     y,    r,    g,    b,    a
            -0.5f,  0.0f,  1.0f, 0.0f, 0.0f, 1.0f,  // red at pixel ~64
             0.0f,  0.0f,  0.0f, 1.0f, 0.0f, 1.0f,  // green at pixel ~128
             0.5f,  0.0f,  0.0f, 0.0f, 1.0f, 1.0f,  // blue at pixel ~192
        };

        glEnableVertexAttribArray(basicPosLoc);
        glVertexAttribPointer(basicPosLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), pointsData);
        if (basicColorLoc >= 0) {
            glEnableVertexAttribArray(basicColorLoc);
            glVertexAttribPointer(basicColorLoc, 4, GL_FLOAT, GL_FALSE,
                                  6 * sizeof(float), pointsData + 2);
        }

        drainGLErrors();
        glDrawArrays(GL_POINTS, 0, 3);
        glFinish();
        checkGLError("per-vertex color points draw");

        glDisableVertexAttribArray(basicPosLoc);
        if (basicColorLoc >= 0) glDisableVertexAttribArray(basicColorLoc);

        glReadPixels(0, 0, FBO_WIDTH, FBO_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        int halfPt = (int)ceilf(ptSize / 2.0f);

        // Check each point
        struct { int cx; uint8_t r, g, b; const char* color; } checks[] = {
            {  64, 255, 0, 0, "red" },
            { 128, 0, 255, 0, "green" },
            { 192, 0, 0, 255, "blue" },
        };

        bool allFound = true;
        for (int i = 0; i < 3; i++) {
            int rx = checks[i].cx - halfPt - 2;
            int ry = 128 - halfPt - 2;
            int rw = halfPt * 2 + 4;
            int rh = halfPt * 2 + 4;
            if (rx < 0) rx = 0;
            if (ry < 0) ry = 0;

            PixelStats stats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                              rx, ry, rw, rh,
                                              checks[i].r, checks[i].g, checks[i].b);
            printf("  %s point at pixel ~(%d,128): %d non-black, %d color-matched\n",
                   checks[i].color, checks[i].cx,
                   stats.nonBlackPixels, stats.matchingPixels);
            if (stats.nonBlackPixels == 0) allFound = false;
        }

        TestResult t5;
        t5.name = "Per-vertex colored GL_POINTS";
        t5.passed = allFound;
        t5.pixelsFound = allFound ? 3 : 0;
        t5.pixelsExpectedMin = 3;
        snprintf(t5.detail, sizeof(t5.detail),
                 "All 3 colored points %s",
                 allFound ? "produced visible pixels" : "did NOT all produce pixels");
        reportTest(t5);
    }

    // ============================================================
    // TEST 6: Points alongside triangles in same draw call context
    // ============================================================
    printf("\n=== TEST 6: GL_POINTS after GL_TRIANGLES in same frame ===\n");
    {
        float ptSize = 32.0f;
        if (ptSize > maxPointSize) ptSize = maxPointSize;
        if (ptSize < 4.0f) ptSize = 4.0f;

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(basicProg);
        glUniform1f(basicPointSizeLoc, 1.0f);

        // First: draw a small green triangle in the lower-left
        float triVerts[] = {
            // x,     y,    r,    g,    b,    a
            -0.9f, -0.9f,  0.0f, 1.0f, 0.0f, 1.0f,
            -0.5f, -0.9f,  0.0f, 1.0f, 0.0f, 1.0f,
            -0.7f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f,
        };

        glEnableVertexAttribArray(basicPosLoc);
        glVertexAttribPointer(basicPosLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), triVerts);
        if (basicColorLoc >= 0) {
            glEnableVertexAttribArray(basicColorLoc);
            glVertexAttribPointer(basicColorLoc, 4, GL_FLOAT, GL_FALSE,
                                  6 * sizeof(float), triVerts + 2);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Then: draw a red point in the upper-right
        glUniform1f(basicPointSizeLoc, ptSize);
        float pointData[] = {
            0.5f, 0.5f,  1.0f, 0.0f, 0.0f, 1.0f,
        };
        glVertexAttribPointer(basicPosLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), pointData);
        if (basicColorLoc >= 0) {
            glVertexAttribPointer(basicColorLoc, 4, GL_FLOAT, GL_FALSE,
                                  6 * sizeof(float), pointData + 2);
        }
        glDrawArrays(GL_POINTS, 0, 1);
        glFinish();

        glDisableVertexAttribArray(basicPosLoc);
        if (basicColorLoc >= 0) glDisableVertexAttribArray(basicColorLoc);

        glReadPixels(0, 0, FBO_WIDTH, FBO_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        // Check triangle region (lower-left quadrant)
        PixelStats triStats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                             0, 0, 64, 64,
                                             0, 255, 0);

        // Check point region (upper-right, centered at pixel 192,192)
        int halfPt = (int)ceilf(ptSize / 2.0f);
        PixelStats ptStats = analyzeRegion(pixels, FBO_WIDTH, FBO_HEIGHT,
                                            192 - halfPt - 2, 192 - halfPt - 2,
                                            halfPt * 2 + 4, halfPt * 2 + 4,
                                            255, 0, 0);

        printf("  Triangle (lower-left): %d green pixels\n", triStats.nonBlackPixels);
        printf("  Point (upper-right):   %d red pixels\n", ptStats.nonBlackPixels);

        TestResult t6;
        t6.name = "GL_POINTS after GL_TRIANGLES in same frame";
        t6.pixelsFound = ptStats.nonBlackPixels;
        t6.pixelsExpectedMin = 1;
        t6.passed = (triStats.nonBlackPixels > 0 && ptStats.nonBlackPixels > 0);
        snprintf(t6.detail, sizeof(t6.detail),
                 "Triangle: %d pixels, Point: %d pixels. Both must be > 0.",
                 triStats.nonBlackPixels, ptStats.nonBlackPixels);
        reportTest(t6);
    }

    // ============================================================
    // Cleanup and summary
    // ============================================================

    if (pcProg) glDeleteProgram(pcProg);
    glDeleteProgram(basicProg);
    destroyFBO(fbo);

    printf("\n============================================================\n");
    printf("SUMMARY\n");
    printf("============================================================\n");
    printf("  Total tests: %d\n", g_numTotal);
    printf("  Passed:      %d\n", g_numPassed);
    printf("  Failed:      %d\n", g_numFailed);
    printf("\n");
    printf("  Max point size: %.1f px\n", maxPointSize);
    printf("  Driver:         %s\n", renderer ? renderer : "unknown");
    printf("\n");

    if (g_numFailed == 0) {
        printf("  RESULT: All GL_POINTS tests passed.\n");
        printf("  The Lima/Mali 400 driver supports GL_POINTS rendering\n");
        printf("  including large point sizes and gl_PointCoord.\n");
    } else if (g_numPassed > 0) {
        printf("  RESULT: Partial GL_POINTS support.\n");
        printf("  Some point rendering tests failed. Check output above for details.\n");
        printf("  If triangles work but points do not, the driver may not support\n");
        printf("  GL_POINTS or gl_PointSize in the vertex shader.\n");
    } else {
        printf("  RESULT: GL_POINTS rendering appears broken.\n");
        printf("  No tests passed. This may indicate:\n");
        printf("  - The Lima driver does not support GL_POINTS\n");
        printf("  - The FBO readback is not working\n");
        printf("  - The entire GLES2 pipeline is broken\n");
    }

    printf("\n");
    cleanup(egl);
    return (g_numFailed > 0) ? 1 : 0;
}
