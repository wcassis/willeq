// gles2_npot_test - Test NPOT texture support on GLES2
//
// Creates a GLES2 EGL context and tests whether non-power-of-two textures
// work with GL_REPEAT wrapping and mipmaps. Renders to an FBO and reads back
// results via glReadPixels to detect driver failures.
//
// The Mali 400 handoff notes claim that NPOT textures via GL_OES_texture_npot
// cannot use mipmaps or GL_REPEAT wrapping. This tool verifies that claim.
//
// Usage:
//   ./gles2_npot_test              # auto-detect (try DRM first, then X11)
//   ./gles2_npot_test --drm        # force DRM/GBM
//   ./gles2_npot_test --x11        # force X11/EGL

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <gbm.h>
#endif

// --- EGL context setup (same pattern as gles2_derivatives_test) ---

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

// --- GL helpers ---

static const char* glErrorString(GLenum err) {
    switch (err) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM (0x0500)";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE (0x0501)";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION (0x0502)";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION (0x0506)";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY (0x0505)";
        default: return "UNKNOWN";
    }
}

static GLenum checkGLError(const char* label) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("  %s: %s\n", label, glErrorString(err));
    }
    return err;
}

static void drainGLErrors() {
    while (glGetError() != GL_NO_ERROR) {}
}

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "Shader compile error:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    glBindAttribLocation(prog, 1, "aTexCoord");
    glLinkProgram(prog);

    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// --- Shaders ---

static const char* VS_SOURCE = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* FS_SOURCE = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

// --- FBO ---

struct FBO {
    GLuint fbo = 0;
    GLuint colorTex = 0;
    int width = 0;
    int height = 0;
};

static bool createFBO(FBO& fbo, int w, int h) {
    fbo.width = w;
    fbo.height = h;

    glGenTextures(1, &fbo.colorTex);
    glBindTexture(GL_TEXTURE_2D, fbo.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &fbo.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.colorTex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete: 0x%x\n", status);
        return false;
    }
    return true;
}

static void destroyFBO(FBO& fbo) {
    if (fbo.fbo) glDeleteFramebuffers(1, &fbo.fbo);
    if (fbo.colorTex) glDeleteTextures(1, &fbo.colorTex);
    fbo.fbo = 0;
    fbo.colorTex = 0;
}

// --- Fullscreen quad with configurable UV range ---

static void drawQuad(float uvMax) {
    float verts[] = {
        // pos.x, pos.y, uv.x, uv.y
        -1.0f, -1.0f,  0.0f,  0.0f,
         1.0f, -1.0f,  uvMax, 0.0f,
        -1.0f,  1.0f,  0.0f,  uvMax,
         1.0f,  1.0f,  uvMax, uvMax,
    };

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, &verts[0]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, &verts[2]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

// --- Checkerboard texture creation ---

// Creates a checkerboard texture: red (0xFF,0x00,0x00) and green (0x00,0xFF,0x00)
// in blockSize x blockSize pixel blocks.
static std::vector<uint8_t> createCheckerboard(int width, int height, int blockSize) {
    std::vector<uint8_t> data(width * height * 4);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int bx = (x / blockSize) & 1;
            int by = (y / blockSize) & 1;
            bool isRed = ((bx ^ by) == 0);
            int i = (y * width + x) * 4;
            data[i + 0] = isRed ? 0xFF : 0x00;  // R
            data[i + 1] = isRed ? 0x00 : 0xFF;  // G
            data[i + 2] = 0x00;                   // B
            data[i + 3] = 0xFF;                   // A
        }
    }
    return data;
}

// --- Pixel readback and analysis ---

struct PixelStats {
    float avgR, avgG, avgB;
    bool allBlack;   // all pixels are (0,0,0)
    bool allWhite;   // all pixels are (255,255,255)
};

static PixelStats readAndAnalyze(const FBO& fbo) {
    std::vector<uint8_t> pixels(fbo.width * fbo.height * 4);
    glReadPixels(0, 0, fbo.width, fbo.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    PixelStats s = {};
    double sumR = 0, sumG = 0, sumB = 0;
    int count = fbo.width * fbo.height;
    int blackCount = 0;
    int whiteCount = 0;

    for (int i = 0; i < count; i++) {
        uint8_t r = pixels[i * 4 + 0];
        uint8_t g = pixels[i * 4 + 1];
        uint8_t b = pixels[i * 4 + 2];

        sumR += r / 255.0;
        sumG += g / 255.0;
        sumB += b / 255.0;

        if (r == 0 && g == 0 && b == 0) blackCount++;
        if (r == 255 && g == 255 && b == 255) whiteCount++;
    }

    s.avgR = static_cast<float>(sumR / count);
    s.avgG = static_cast<float>(sumG / count);
    s.avgB = static_cast<float>(sumB / count);
    s.allBlack = (blackCount == count);
    s.allWhite = (whiteCount == count);
    return s;
}

// --- Test configuration ---

struct TestConfig {
    int testNum;
    const char* label;
    int texWidth;
    int texHeight;
    GLenum wrapMode;
    GLenum minFilter;
    bool useMipmaps;
    float uvMax;       // 1.0 for clamp tests, 2.0 for repeat tests
};

// --- Main ---

int main(int argc, char* argv[]) {
    bool forceDRM = false;
    bool forceX11 = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) forceDRM = true;
        else if (strcmp(argv[i], "--x11") == 0) forceX11 = true;
        else {
            fprintf(stderr, "Usage: %s [--drm|--x11]\n", argv[0]);
            return 1;
        }
    }

    EGLState egl;
    bool ok = false;

    if (forceX11) {
        ok = initX11(egl);
    } else if (forceDRM) {
#ifdef EQT_HAS_DRM
        ok = initDRM(egl);
#else
        fprintf(stderr, "DRM support not compiled in\n");
        return 1;
#endif
    } else {
        // Auto-detect: try DRM first, then X11
#ifdef EQT_HAS_DRM
        ok = initDRM(egl);
#endif
        if (!ok) ok = initX11(egl);
    }

    if (!ok) {
        fprintf(stderr, "Failed to create GLES2 context\n");
        return 1;
    }

    // Print GL info
    printf("\n=== GLES2 NPOT Texture Test ===\n");
    printf("GL_RENDERER:  %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION:   %s\n", glGetString(GL_VERSION));

    // Check for NPOT extension
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    bool hasNPOT = extensions && strstr(extensions, "GL_OES_texture_npot");
    printf("GL_OES_texture_npot: %s\n\n", hasNPOT ? "YES" : "NO");

    // Create FBO for rendering (64x64 is enough for color validation)
    const int FBO_W = 64;
    const int FBO_H = 64;
    FBO fbo;
    if (!createFBO(fbo, FBO_W, FBO_H)) {
        fprintf(stderr, "Failed to create FBO\n");
        cleanup(egl);
        return 1;
    }

    glViewport(0, 0, FBO_W, FBO_H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Compile shader program
    GLuint vs = compileShader(GL_VERTEX_SHADER, VS_SOURCE);
    if (!vs) {
        fprintf(stderr, "Failed to compile vertex shader\n");
        cleanup(egl);
        return 1;
    }
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS_SOURCE);
    if (!fs) {
        fprintf(stderr, "Failed to compile fragment shader\n");
        glDeleteShader(vs);
        cleanup(egl);
        return 1;
    }
    GLuint prog = linkProgram(vs, fs);
    if (!prog) {
        fprintf(stderr, "Failed to link program\n");
        glDeleteShader(vs);
        glDeleteShader(fs);
        cleanup(egl);
        return 1;
    }

    glUseProgram(prog);
    GLint texLoc = glGetUniformLocation(prog, "uTexture");
    glUniform1i(texLoc, 0);
    glActiveTexture(GL_TEXTURE0);

    // Define test matrix
    TestConfig tests[] = {
        // POT controls — must all pass
        { 1, "POT 256x256, CLAMP_TO_EDGE, LINEAR (no mip)",
          256, 256, GL_CLAMP_TO_EDGE, GL_LINEAR, false, 1.0f },
        { 2, "POT 256x256, REPEAT, LINEAR (no mip)",
          256, 256, GL_REPEAT, GL_LINEAR, false, 2.0f },
        { 3, "POT 256x256, REPEAT, LINEAR_MIPMAP_LINEAR (mipmap)",
          256, 256, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, true, 2.0f },

        // NPOT tests — these probe Mali 400 restrictions
        { 4, "NPOT 300x300, CLAMP_TO_EDGE, LINEAR (no mip)",
          300, 300, GL_CLAMP_TO_EDGE, GL_LINEAR, false, 1.0f },
        { 5, "NPOT 300x300, REPEAT, LINEAR (no mip)",
          300, 300, GL_REPEAT, GL_LINEAR, false, 2.0f },
        { 6, "NPOT 300x300, REPEAT, LINEAR_MIPMAP_LINEAR (mipmap)",
          300, 300, GL_REPEAT, GL_LINEAR_MIPMAP_LINEAR, true, 2.0f },
        { 7, "NPOT 300x300, CLAMP_TO_EDGE, LINEAR_MIPMAP_LINEAR (mipmap)",
          300, 300, GL_CLAMP_TO_EDGE, GL_LINEAR_MIPMAP_LINEAR, true, 1.0f },

        // Extreme NPOT (odd dimensions)
        { 8, "NPOT 127x253, CLAMP_TO_EDGE, LINEAR (no mip)",
          127, 253, GL_CLAMP_TO_EDGE, GL_LINEAR, false, 1.0f },
        { 9, "NPOT 127x253, REPEAT, LINEAR (no mip)",
          127, 253, GL_REPEAT, GL_LINEAR, false, 2.0f },
    };
    int numTests = sizeof(tests) / sizeof(tests[0]);

    int totalPassed = 0;

    // Track NPOT capability categories
    bool npotClampNoMip = false;    // test 4
    bool npotRepeatNoMip = false;   // test 5
    bool npotRepeatMip = false;     // test 6
    bool npotClampMip = false;      // test 7
    bool npotOddClamp = false;     // test 8
    bool npotOddRepeat = false;    // test 9

    for (int t = 0; t < numTests; t++) {
        const TestConfig& tc = tests[t];
        printf("--- Test %d: %s ---\n", tc.testNum, tc.label);

        drainGLErrors();

        // Create checkerboard texture
        auto texData = createCheckerboard(tc.texWidth, tc.texHeight, 8);
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);

        // Upload
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tc.texWidth, tc.texHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, texData.data());
        GLenum uploadErr = checkGLError("glTexImage2D");
        printf("  Upload: %s\n", uploadErr == GL_NO_ERROR ? "OK" : "ERROR");

        // Set wrap mode
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, tc.wrapMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tc.wrapMode);

        // Set filter
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tc.minFilter);

        // Generate mipmaps if needed
        GLenum mipErr = GL_NO_ERROR;
        if (tc.useMipmaps) {
            drainGLErrors();
            glGenerateMipmap(GL_TEXTURE_2D);
            mipErr = checkGLError("glGenerateMipmap");
            printf("  glGenerateMipmap: %s\n", mipErr == GL_NO_ERROR ? "OK" : "ERROR");
        }

        // Render to FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
        glViewport(0, 0, FBO_W, FBO_H);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, tex);
        drainGLErrors();
        drawQuad(tc.uvMax);
        GLenum drawErr = checkGLError("glDrawArrays");
        printf("  Draw: %s\n", drawErr == GL_NO_ERROR ? "OK" : "ERROR");

        glFinish();

        // Read back and analyze
        drainGLErrors();
        PixelStats s = readAndAnalyze(fbo);
        GLenum readErr = checkGLError("glReadPixels");
        printf("  ReadPixels: %s\n", readErr == GL_NO_ERROR ? "OK" : "ERROR");

        printf("  Avg color: R=%.3f G=%.3f B=%.3f (expected ~0.5/0.5/0.0)\n",
               s.avgR, s.avgG, s.avgB);

        // Determine pass/fail
        bool hasGLError = (uploadErr != GL_NO_ERROR || drawErr != GL_NO_ERROR ||
                           readErr != GL_NO_ERROR || mipErr != GL_NO_ERROR);
        bool colorOK = (s.avgR > 0.3f && s.avgR < 0.7f &&
                        s.avgG > 0.3f && s.avgG < 0.7f &&
                        s.avgB < 0.1f);
        bool pass = !hasGLError && colorOK && !s.allBlack && !s.allWhite;

        if (s.allBlack) printf("  WARNING: All pixels are black (driver failure)\n");
        if (s.allWhite) printf("  WARNING: All pixels are white (driver failure)\n");

        printf("  Result: %s\n\n", pass ? "PASS" : "FAIL");

        if (pass) totalPassed++;

        // Track per-category results
        switch (tc.testNum) {
            case 4: npotClampNoMip = pass; break;
            case 5: npotRepeatNoMip = pass; break;
            case 6: npotRepeatMip = pass; break;
            case 7: npotClampMip = pass; break;
            case 8: npotOddClamp = pass; break;
            case 9: npotOddRepeat = pass; break;
        }

        glDeleteTextures(1, &tex);
    }

    // Summary
    printf("=== SUMMARY ===\n");
    printf("Tests passed: %d / %d\n\n", totalPassed, numTests);
    printf("NPOT + CLAMP_TO_EDGE (no mipmap):  %s\n", npotClampNoMip ? "WORKS" : "FAILS");
    printf("NPOT + GL_REPEAT (no mipmap):      %s\n", npotRepeatNoMip ? "WORKS" : "FAILS");
    printf("NPOT + mipmaps + REPEAT:           %s\n", npotRepeatMip ? "WORKS" : "FAILS");
    printf("NPOT + mipmaps + CLAMP:            %s\n", npotClampMip ? "WORKS" : "FAILS");
    printf("NPOT odd dims + CLAMP:             %s\n", npotOddClamp ? "WORKS" : "FAILS");
    printf("NPOT odd dims + REPEAT:            %s\n", npotOddRepeat ? "WORKS" : "FAILS");

    printf("\nRecommendation: ");
    if (totalPassed == numTests) {
        printf("Full NPOT support available. GL_REPEAT and mipmaps work on NPOT textures.\n");
    } else if (npotClampNoMip && !npotRepeatNoMip) {
        printf("Use GL_CLAMP_TO_EDGE for NPOT textures, no mipmaps. GL_REPEAT requires POT.\n");
    } else if (npotClampNoMip && npotRepeatNoMip && !npotClampMip) {
        printf("NPOT textures work with any wrap mode but cannot use mipmaps.\n");
    } else if (!npotClampNoMip) {
        printf("NPOT textures not supported at all. Pad all textures to POT.\n");
    } else {
        printf("Partial NPOT support. Use GL_CLAMP_TO_EDGE for NPOT, no mipmaps.\n");
    }

    // Cleanup
    glDeleteProgram(prog);
    glDeleteShader(fs);
    glDeleteShader(vs);
    destroyFBO(fbo);
    cleanup(egl);

    return (totalPassed == numTests) ? 0 : 1;
}
