// gles2_derivatives_test - Test GL_OES_standard_derivatives on GLES2
//
// Creates a GLES2 EGL context and tests whether dFdx(), dFdy(), and fwidth()
// return correct values on the current GPU. Renders to an FBO and reads back
// results via glReadPixels.
//
// The claim that Lima's fwidth() returns degenerate values (NaN) was never
// tested on hardware. This tool provides a definitive answer.
//
// Usage:
//   ./gles2_derivatives_test              # auto-detect (try DRM first, then X11)
//   ./gles2_derivatives_test --drm        # force DRM/GBM
//   ./gles2_derivatives_test --x11        # force X11/EGL

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

// --- EGL context setup (same pattern as gles2_etc1_benchmark) ---

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

// --- Test shaders ---

// Vertex shader: pass through position and UVs (0..1 across the quad)
static const char* VS_SOURCE = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vUV;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vUV = aTexCoord;
}
)";

// Test 1: Write dFdx(vUV.x) and dFdy(vUV.y) to R and G channels.
// For a fullscreen quad with UV [0..1] rendered to a WxH FBO:
//   dFdx(vUV.x) should be ~1.0/W per fragment
//   dFdy(vUV.y) should be ~1.0/H per fragment
// We scale by the FBO size so expected output is ~1.0 (encoded as ~255).
static const char* FS_DFDX_DFDY = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec2 vUV;
uniform float uWidth;
uniform float uHeight;
void main() {
    float dx = dFdx(vUV.x) * uWidth;
    float dy = dFdy(vUV.y) * uHeight;
    gl_FragColor = vec4(dx, dy, 0.0, 1.0);
}
)";

// Test 2: Write fwidth(vUV.x) and fwidth(vUV.y) to R and G.
// fwidth(x) = abs(dFdx(x)) + abs(dFdy(x))
// For vUV.x: dFdx = 1/W, dFdy = 0 → fwidth = 1/W → scaled = 1.0
// For vUV.y: dFdx = 0, dFdy = 1/H → fwidth = 1/H → scaled = 1.0
static const char* FS_FWIDTH = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec2 vUV;
uniform float uWidth;
uniform float uHeight;
void main() {
    float fw_x = fwidth(vUV.x) * uWidth;
    float fw_y = fwidth(vUV.y) * uHeight;
    gl_FragColor = vec4(fw_x, fw_y, 0.0, 1.0);
}
)";

// Test 3: The actual use case — fwidth() on a texture sample's alpha channel.
// We create a 2x2 texture with a known alpha gradient and check if fwidth(alpha)
// is sane (non-NaN, non-zero, reasonable magnitude).
static const char* FS_FWIDTH_ALPHA = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec2 vUV;
uniform sampler2D uTexture;
uniform float uWidth;
void main() {
    float alpha = texture2D(uTexture, vUV).a;
    float fw = fwidth(alpha);
    // Encode: R = alpha value, G = fwidth * scale, B = 1.0 if fw is finite (not NaN)
    float isFinite = (fw == fw) ? 1.0 : 0.0;  // NaN != NaN
    gl_FragColor = vec4(alpha, fw * uWidth, isFinite, 1.0);
}
)";

// Test 4: The alpha threshold formula from the actual shader code:
//   threshold = clamp(0.5 - fwidth(alpha), 0.1, 0.5)
// Check if the result is in [0.1, 0.5] range and not NaN.
static const char* FS_ALPHA_THRESHOLD = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
varying vec2 vUV;
uniform sampler2D uTexture;
void main() {
    float alpha = texture2D(uTexture, vUV).a;
    float fw = fwidth(alpha);
    float threshold = clamp(0.5 - fw, 0.1, 0.5);
    // R = threshold, G = fwidth raw, B = 1.0 if both are finite
    float isFinite = ((fw == fw) && (threshold == threshold)) ? 1.0 : 0.0;
    gl_FragColor = vec4(threshold, fw, isFinite, 1.0);
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

// --- Fullscreen quad ---

static void drawQuad() {
    // Position: fullscreen NDC quad
    // TexCoord: [0,0] to [1,1]
    float verts[] = {
        // pos.x, pos.y, uv.x, uv.y
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, &verts[0]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, &verts[2]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

// --- Read back and analyze ---

struct PixelStats {
    float avgR, avgG, avgB;
    float minR, minG, minB;
    float maxR, maxG, maxB;
    bool hasNaN;  // B channel < 0.5 indicates NaN detected in shader
    int nanCount;
};

static PixelStats readAndAnalyze(const FBO& fbo) {
    std::vector<uint8_t> pixels(fbo.width * fbo.height * 4);
    glReadPixels(0, 0, fbo.width, fbo.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    PixelStats s = {};
    s.minR = s.minG = s.minB = 999.0f;
    s.maxR = s.maxG = s.maxB = -999.0f;

    double sumR = 0, sumG = 0, sumB = 0;
    int count = fbo.width * fbo.height;

    for (int i = 0; i < count; i++) {
        float r = pixels[i * 4 + 0] / 255.0f;
        float g = pixels[i * 4 + 1] / 255.0f;
        float b = pixels[i * 4 + 2] / 255.0f;

        sumR += r; sumG += g; sumB += b;
        if (r < s.minR) s.minR = r;
        if (r > s.maxR) s.maxR = r;
        if (g < s.minG) s.minG = g;
        if (g > s.maxG) s.maxG = g;
        if (b < s.minB) s.minB = b;
        if (b > s.maxB) s.maxB = b;

        // B channel < 0.5 means NaN was detected in the shader
        if (b < 0.5f) {
            s.hasNaN = true;
            s.nanCount++;
        }
    }

    s.avgR = static_cast<float>(sumR / count);
    s.avgG = static_cast<float>(sumG / count);
    s.avgB = static_cast<float>(sumB / count);
    return s;
}

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
    printf("GL_RENDERER:  %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION:   %s\n", glGetString(GL_VERSION));

    // Check for extension
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    bool hasDerivatives = extensions && strstr(extensions, "GL_OES_standard_derivatives");
    printf("GL_OES_standard_derivatives: %s\n\n", hasDerivatives ? "YES" : "NO");

    if (!hasDerivatives) {
        printf("RESULT: Extension not available. Cannot test.\n");
        cleanup(egl);
        return 1;
    }

    // Create FBO for rendering
    const int FBO_SIZE = 32;
    FBO fbo;
    if (!createFBO(fbo, FBO_SIZE, FBO_SIZE)) {
        fprintf(stderr, "Failed to create FBO\n");
        cleanup(egl);
        return 1;
    }

    glViewport(0, 0, FBO_SIZE, FBO_SIZE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Compile vertex shader (shared by all tests)
    GLuint vs = compileShader(GL_VERTEX_SHADER, VS_SOURCE);
    if (!vs) {
        fprintf(stderr, "Failed to compile vertex shader\n");
        cleanup(egl);
        return 1;
    }

    // Create a 4x4 texture with known alpha gradient for tests 3 & 4
    GLuint gradientTex;
    {
        uint8_t texData[4 * 4 * 4]; // 4x4 RGBA
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                int i = (y * 4 + x) * 4;
                texData[i + 0] = 255;  // R
                texData[i + 1] = 255;  // G
                texData[i + 2] = 255;  // B
                // Alpha gradient: 0 at left, 255 at right
                texData[i + 3] = static_cast<uint8_t>(x * 85);  // 0, 85, 170, 255
            }
        }
        glGenTextures(1, &gradientTex);
        glBindTexture(GL_TEXTURE_2D, gradientTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    int totalTests = 0;
    int passedTests = 0;

    // ---- Test 1: dFdx / dFdy ----
    printf("=== Test 1: dFdx(vUV.x), dFdy(vUV.y) ===\n");
    {
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS_DFDX_DFDY);
        if (!fs) {
            printf("  FAIL: Fragment shader did not compile\n");
            totalTests++;
        } else {
            GLuint prog = linkProgram(vs, fs);
            if (!prog) {
                printf("  FAIL: Program did not link\n");
                totalTests++;
            } else {
                glUseProgram(prog);
                glUniform1f(glGetUniformLocation(prog, "uWidth"), static_cast<float>(FBO_SIZE));
                glUniform1f(glGetUniformLocation(prog, "uHeight"), static_cast<float>(FBO_SIZE));

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                drawQuad();
                glFinish();

                PixelStats s = readAndAnalyze(fbo);
                printf("  dFdx(vUV.x) * W:  avg=%.3f  min=%.3f  max=%.3f  (expected ~1.0)\n",
                       s.avgR, s.minR, s.maxR);
                printf("  dFdy(vUV.y) * H:  avg=%.3f  min=%.3f  max=%.3f  (expected ~1.0)\n",
                       s.avgG, s.minG, s.maxG);

                totalTests++;
                bool pass = (s.avgR > 0.8f && s.avgR < 1.2f &&
                             s.avgG > 0.8f && s.avgG < 1.2f);
                printf("  Result: %s\n\n", pass ? "PASS" : "FAIL");
                if (pass) passedTests++;

                glDeleteProgram(prog);
            }
            glDeleteShader(fs);
        }
    }

    // ---- Test 2: fwidth ----
    printf("=== Test 2: fwidth(vUV.x), fwidth(vUV.y) ===\n");
    {
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS_FWIDTH);
        if (!fs) {
            printf("  FAIL: Fragment shader did not compile\n");
            totalTests++;
        } else {
            GLuint prog = linkProgram(vs, fs);
            if (!prog) {
                printf("  FAIL: Program did not link\n");
                totalTests++;
            } else {
                glUseProgram(prog);
                glUniform1f(glGetUniformLocation(prog, "uWidth"), static_cast<float>(FBO_SIZE));
                glUniform1f(glGetUniformLocation(prog, "uHeight"), static_cast<float>(FBO_SIZE));

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                drawQuad();
                glFinish();

                PixelStats s = readAndAnalyze(fbo);
                printf("  fwidth(vUV.x) * W:  avg=%.3f  min=%.3f  max=%.3f  (expected ~1.0)\n",
                       s.avgR, s.minR, s.maxR);
                printf("  fwidth(vUV.y) * H:  avg=%.3f  min=%.3f  max=%.3f  (expected ~1.0)\n",
                       s.avgG, s.minG, s.maxG);

                totalTests++;
                bool pass = (s.avgR > 0.8f && s.avgR < 1.2f &&
                             s.avgG > 0.8f && s.avgG < 1.2f);
                printf("  Result: %s\n\n", pass ? "PASS" : "FAIL");
                if (pass) passedTests++;

                glDeleteProgram(prog);
            }
            glDeleteShader(fs);
        }
    }

    // ---- Test 3: fwidth on texture alpha ----
    printf("=== Test 3: fwidth(texture alpha) — NaN check ===\n");
    {
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS_FWIDTH_ALPHA);
        if (!fs) {
            printf("  FAIL: Fragment shader did not compile\n");
            totalTests++;
        } else {
            GLuint prog = linkProgram(vs, fs);
            if (!prog) {
                printf("  FAIL: Program did not link\n");
                totalTests++;
            } else {
                glUseProgram(prog);
                glUniform1f(glGetUniformLocation(prog, "uWidth"), static_cast<float>(FBO_SIZE));
                glUniform1i(glGetUniformLocation(prog, "uTexture"), 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gradientTex);

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                drawQuad();
                glFinish();

                PixelStats s = readAndAnalyze(fbo);
                printf("  alpha value:         avg=%.3f  min=%.3f  max=%.3f\n",
                       s.avgR, s.minR, s.maxR);
                printf("  fwidth(alpha)*W:     avg=%.3f  min=%.3f  max=%.3f\n",
                       s.avgG, s.minG, s.maxG);
                printf("  isFinite (B chan):    avg=%.3f  (1.0=all finite, <1.0=has NaN)\n",
                       s.avgB);
                printf("  NaN fragments:       %d / %d\n", s.nanCount, FBO_SIZE * FBO_SIZE);

                totalTests++;
                bool pass = !s.hasNaN && s.avgG > 0.0f;
                printf("  Result: %s\n\n", pass ? "PASS" : "FAIL");
                if (pass) passedTests++;

                glDeleteProgram(prog);
            }
            glDeleteShader(fs);
        }
    }

    // ---- Test 4: Actual alpha threshold formula ----
    printf("=== Test 4: clamp(0.5 - fwidth(alpha), 0.1, 0.5) — real use case ===\n");
    {
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS_ALPHA_THRESHOLD);
        if (!fs) {
            printf("  FAIL: Fragment shader did not compile\n");
            totalTests++;
        } else {
            GLuint prog = linkProgram(vs, fs);
            if (!prog) {
                printf("  FAIL: Program did not link\n");
                totalTests++;
            } else {
                glUseProgram(prog);
                glUniform1i(glGetUniformLocation(prog, "uTexture"), 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, gradientTex);

                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                drawQuad();
                glFinish();

                PixelStats s = readAndAnalyze(fbo);
                printf("  threshold:           avg=%.3f  min=%.3f  max=%.3f  (expected 0.1-0.5)\n",
                       s.avgR, s.minR, s.maxR);
                printf("  fwidth(alpha) raw:   avg=%.3f  min=%.3f  max=%.3f\n",
                       s.avgG, s.minG, s.maxG);
                printf("  isFinite (B chan):    avg=%.3f  (1.0=all finite, <1.0=has NaN)\n",
                       s.avgB);
                printf("  NaN fragments:       %d / %d\n", s.nanCount, FBO_SIZE * FBO_SIZE);

                totalTests++;
                bool pass = !s.hasNaN &&
                            s.minR >= 0.08f && s.maxR <= 0.52f;  // within clamp range + tolerance
                printf("  Result: %s\n\n", pass ? "PASS" : "FAIL");
                if (pass) passedTests++;

                glDeleteProgram(prog);
            }
            glDeleteShader(fs);
        }
    }

    // Summary
    printf("=== SUMMARY ===\n");
    printf("Tests passed: %d / %d\n", passedTests, totalTests);
    if (passedTests == totalTests) {
        printf("\nGL_OES_standard_derivatives works correctly.\n");
        printf("fwidth() can be safely enabled for alpha threshold smoothing.\n");
    } else {
        printf("\nGL_OES_standard_derivatives has issues on this GPU.\n");
        printf("Keep fwidth() disabled.\n");
    }

    // Cleanup
    glDeleteTextures(1, &gradientTex);
    glDeleteShader(vs);
    destroyFBO(fbo);
    cleanup(egl);

    return (passedTests == totalTests) ? 0 : 1;
}
