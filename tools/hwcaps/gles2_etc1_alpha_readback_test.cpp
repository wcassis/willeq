// gles2_etc1_alpha_readback_test — Verify ETC1 alpha companion texture on GPU
//
// Loads tree70 and tree70_alpha from an EQE1 flat pack, uploads both as ETC1
// textures, renders a fullscreen quad sampling each, reads back pixels, and
// reports whether the GPU produces the expected values.
//
// This isolates the ETC1 hardware decoder from the full rendering pipeline
// to determine if alpha companion textures decode correctly on Mali 400.
//
// Usage:
//   ./gles2_etc1_alpha_readback_test --drm --eqe1 /path/to/qeynos2_obj.eqe1
//   ./gles2_etc1_alpha_readback_test --x11 --eqe1 /path/to/qeynos2_obj.eqe1

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

#include "client/graphics/eqe1_format.h"
#include "client/graphics/eqe1_loader.h"

#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

// ── EGL setup (copied from gles2_etc1_benchmark pattern) ──

struct EGLState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
#ifdef EQT_HAS_DRM
    int drmFd = -1;
    struct gbm_device* gbmDevice = nullptr;
    struct gbm_surface* gbmSurface = nullptr;
#endif
};

#ifdef EQT_HAS_DRM
static bool initDRM(EGLState& state, int w, int h) {
    const char* cards[] = {"/dev/dri/card0", "/dev/dri/card1", nullptr};
    for (int i = 0; cards[i]; i++) {
        state.drmFd = open(cards[i], O_RDWR);
        if (state.drmFd >= 0) break;
    }
    if (state.drmFd < 0) { fprintf(stderr, "DRM: cannot open card\n"); return false; }

    state.gbmDevice = gbm_create_device(state.drmFd);
    if (!state.gbmDevice) { fprintf(stderr, "DRM: gbm_create_device failed\n"); return false; }

    state.display = eglGetDisplay((EGLNativeDisplayType)state.gbmDevice);
    if (state.display == EGL_NO_DISPLAY) { fprintf(stderr, "DRM: eglGetDisplay failed\n"); return false; }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) {
        fprintf(stderr, "DRM: eglInitialize failed\n"); return false;
    }
    printf("EGL %d.%d\n", major, minor);

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(state.display, configAttribs, &config, 1, &numConfigs);
    if (numConfigs == 0) { fprintf(stderr, "DRM: no EGL config\n"); return false; }

    state.gbmSurface = gbm_surface_create(state.gbmDevice, w, h,
                                            GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!state.gbmSurface) { fprintf(stderr, "DRM: gbm_surface_create failed\n"); return false; }

    state.surface = eglCreateWindowSurface(state.display, config,
                                            (EGLNativeWindowType)state.gbmSurface, nullptr);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "DRM: eglCreateContext failed (0x%x)\n", eglGetError()); return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    return true;
}
#endif

static bool initX11(EGLState& state) {
    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY) { fprintf(stderr, "X11: eglGetDisplay failed\n"); return false; }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) {
        fprintf(stderr, "X11: eglInitialize failed\n"); return false;
    }
    printf("EGL %d.%d\n", major, minor);

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(state.display, configAttribs, &config, 1, &numConfigs);
    if (numConfigs == 0) { fprintf(stderr, "X11: no EGL config\n"); return false; }

    EGLint pbufferAttribs[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
    state.surface = eglCreatePbufferSurface(state.display, config, pbufferAttribs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "X11: eglCreateContext failed (0x%x)\n", eglGetError()); return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    return true;
}

// ── Shaders ──

// Passthrough — just sample one texture
static const char* VS_SRC = R"(
attribute vec2 aPos;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aPos * 0.5 + 0.5;
}
)";

static const char* FS_SRC = R"(
precision mediump float;
uniform sampler2D uTexture;
varying vec2 vTexCoord;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

// Alpha test — sample color from unit 0, alpha from unit 1, discard if alpha < 0.5
// This is the same logic as the production AlphaTest3D shader
static const char* FS_ALPHA_TEST = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
varying vec2 vTexCoord;
void main() {
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    if (alpha < 0.5) discard;
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

static GLuint buildProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, FS_SRC);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        fprintf(stderr, "Program link error: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ── Upload ETC1 texture ──

static GLuint uploadETC1(const uint8_t* data, uint32_t dataSize, int w, int h, int mipLevels) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    uint32_t offset = 0;
    int mw = w, mh = h;
    for (int level = 0; level < mipLevels; level++) {
        uint32_t blocksW = (mw + 3) / 4;
        uint32_t blocksH = (mh + 3) / 4;
        uint32_t levelSize = blocksW * blocksH * 8;

        glCompressedTexImage2D(GL_TEXTURE_2D, level, GL_ETC1_RGB8_OES,
                                mw, mh, 0, levelSize, data + offset);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            printf("  glCompressedTexImage2D level %d (%dx%d, %u bytes) ERROR=0x%x\n",
                   level, mw, mh, levelSize, err);
        }

        offset += levelSize;
        mw = mw > 4 ? mw / 2 : 4;
        mh = mh > 4 ? mh / 2 : 4;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    printf("  Uploaded ETC1 texture: %dx%d, %d mips, glName=%u, %u bytes\n",
           w, h, mipLevels, tex, offset);
    return tex;
}

// ── Render to FBO and readback ──

struct ReadbackResult {
    int width, height;
    std::vector<uint8_t> rgba;  // RGBA pixels
};

static ReadbackResult renderAndReadback(GLuint program, GLuint texture, int w, int h) {
    ReadbackResult result;
    result.width = w;
    result.height = h;
    result.rgba.resize(w * h * 4);

    // Create FBO with RGBA renderbuffer
    GLuint fbo = 0, rbo = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);

    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("  FBO not complete: 0x%x\n", status);
    }

    glViewport(0, 0, w, h);
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);  // Gray clear — distinguishes from texture data
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uTexture"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Fullscreen quad
    float verts[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFinish();

    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, result.rgba.data());
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("  glReadPixels ERROR=0x%x\n", err);
    }

    // Cleanup
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);

    return result;
}

static void analyzeReadback(const char* name, const ReadbackResult& rb) {
    int total = rb.width * rb.height;
    int bright = 0, dark = 0, mid = 0, gray = 0;
    uint64_t rSum = 0;

    for (int i = 0; i < total; i++) {
        uint8_t r = rb.rgba[i * 4 + 0];
        rSum += r;
        if (r > 200) bright++;
        else if (r < 50) dark++;
        else {
            mid++;
            if (r > 120 && r < 140) gray++;  // near the 0.5 clear color
        }
    }

    printf("\n=== %s readback (%dx%d) ===\n", name, rb.width, rb.height);
    printf("  R channel: avg=%.1f\n", (double)rSum / total);
    printf("  Bright (>200): %d (%.1f%%)\n", bright, 100.0 * bright / total);
    printf("  Dark   (<50):  %d (%.1f%%)\n", dark, 100.0 * dark / total);
    printf("  Mid  (50-200): %d (%.1f%%)\n", mid, 100.0 * mid / total);
    if (gray > 0) printf("  Gray (clear color ~128): %d — texture may not have rendered\n", gray);

    // Sample specific pixels
    auto px = [&](int x, int y) {
        int idx = (y * rb.width + x) * 4;
        return rb.rgba[idx];
    };

    printf("  Corners: TL=%d TR=%d BL=%d BR=%d\n",
           px(0, 0), px(rb.width-1, 0), px(0, rb.height-1), px(rb.width-1, rb.height-1));
    printf("  Center:  %d\n", px(rb.width/2, rb.height/2));

    // Print a small grid from center
    int cx = rb.width / 2, cy = rb.height / 2;
    printf("  Center 8x8 R values:\n");
    for (int dy = -4; dy < 4; dy++) {
        printf("    ");
        for (int dx = -4; dx < 4; dx++) {
            printf("%3d ", px(cx + dx, cy + dy));
        }
        printf("\n");
    }
}

// ── Main ──

int main(int argc, char* argv[]) {
    bool useDRM = false;
    bool useX11 = false;
    std::string eqe1Path;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) useDRM = true;
        else if (strcmp(argv[i], "--x11") == 0) useX11 = true;
        else if (strcmp(argv[i], "--eqe1") == 0 && i + 1 < argc) eqe1Path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--drm|--x11] --eqe1 <path/to/file.eqe1>\n", argv[0]);
            printf("  Tests ETC1 alpha companion texture readback on GPU\n");
            return 0;
        }
    }

    if (eqe1Path.empty()) {
        fprintf(stderr, "ERROR: --eqe1 required\n");
        return 1;
    }

    // Load EQE1
    EQT::Graphics::EQE1Loader loader;
    if (!loader.open(eqe1Path)) {
        fprintf(stderr, "ERROR: cannot open %s\n", eqe1Path.c_str());
        return 1;
    }

    // Look up textures
    auto* rgbEntry = loader.lookup("tree70");
    auto* alphaEntry = loader.lookup("tree70_alpha");
    auto* gpineRgb = loader.lookup("gpine");
    auto* gpineAlpha = loader.lookup("gpine_alpha");

    if (!rgbEntry) { fprintf(stderr, "ERROR: tree70 not found in EQE1\n"); return 1; }
    if (!alphaEntry) { fprintf(stderr, "ERROR: tree70_alpha not found in EQE1\n"); return 1; }

    printf("tree70 RGB:   %dx%d, %d mips, %u bytes\n",
           rgbEntry->width, rgbEntry->height, rgbEntry->mipLevels, rgbEntry->dataSize);
    printf("tree70_alpha: %dx%d, %d mips, %u bytes\n",
           alphaEntry->width, alphaEntry->height, alphaEntry->mipLevels, alphaEntry->dataSize);
    printf("  RGB first8:   %02x%02x%02x%02x%02x%02x%02x%02x\n",
           rgbEntry->data[0], rgbEntry->data[1], rgbEntry->data[2], rgbEntry->data[3],
           rgbEntry->data[4], rgbEntry->data[5], rgbEntry->data[6], rgbEntry->data[7]);
    printf("  Alpha first8: %02x%02x%02x%02x%02x%02x%02x%02x\n",
           alphaEntry->data[0], alphaEntry->data[1], alphaEntry->data[2], alphaEntry->data[3],
           alphaEntry->data[4], alphaEntry->data[5], alphaEntry->data[6], alphaEntry->data[7]);

    // Init EGL
    EGLState egl;
    bool ok = false;
#ifdef EQT_HAS_DRM
    if (useDRM || !useX11) {
        ok = initDRM(egl, 256, 256);
        if (ok) printf("Using DRM/GBM\n");
    }
#endif
    if (!ok) {
        ok = initX11(egl);
        if (ok) printf("Using X11/EGL pbuffer\n");
    }
    if (!ok) {
        fprintf(stderr, "ERROR: cannot init EGL\n");
        return 1;
    }

    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));

    // Check ETC1 support
    const char* exts = (const char*)glGetString(GL_EXTENSIONS);
    if (!strstr(exts, "ETC1")) {
        fprintf(stderr, "ERROR: GL_OES_compressed_ETC1_RGB8_texture not supported\n");
        return 1;
    }
    printf("ETC1 supported\n\n");

    // Build shader
    GLuint program = buildProgram();
    if (!program) { fprintf(stderr, "ERROR: shader build failed\n"); return 1; }

    // Upload textures
    printf("Uploading tree70 RGB:\n");
    GLuint rgbTex = uploadETC1(rgbEntry->data, rgbEntry->dataSize,
                                rgbEntry->width, rgbEntry->height, rgbEntry->mipLevels);

    printf("Uploading tree70_alpha:\n");
    GLuint alphaTex = uploadETC1(alphaEntry->data, alphaEntry->dataSize,
                                  alphaEntry->width, alphaEntry->height, alphaEntry->mipLevels);

    // Render and readback at 256x256 (full texture resolution)
    int readW = 256, readH = 256;

    auto rgbResult = renderAndReadback(program, rgbTex, readW, readH);
    analyzeReadback("tree70 RGB", rgbResult);

    auto alphaResult = renderAndReadback(program, alphaTex, readW, readH);
    analyzeReadback("tree70 ALPHA", alphaResult);

    // Also test gpine if available
    if (gpineRgb && gpineAlpha) {
        printf("\nUploading gpine RGB:\n");
        GLuint gpRgb = uploadETC1(gpineRgb->data, gpineRgb->dataSize,
                                   gpineRgb->width, gpineRgb->height, gpineRgb->mipLevels);
        printf("Uploading gpine_alpha:\n");
        GLuint gpAlpha = uploadETC1(gpineAlpha->data, gpineAlpha->dataSize,
                                     gpineAlpha->width, gpineAlpha->height, gpineAlpha->mipLevels);

        auto gpRgbResult = renderAndReadback(program, gpRgb, readW, readH);
        analyzeReadback("gpine RGB", gpRgbResult);

        auto gpAlphaResult = renderAndReadback(program, gpAlpha, readW, readH);
        analyzeReadback("gpine ALPHA", gpAlphaResult);
    }

    // Test: create a known pattern — upload a hand-crafted ETC1 block that should decode to white
    printf("\n=== Synthetic white ETC1 test ===\n");
    // ETC1 block for solid white: diff mode, base=(31,31,31), delta=(0,0,0), table 0, all pixel indices = 01 (positive small modifier)
    // Base R=31 (5-bit), G=31, B=31, diff=1, flip=0, table0=0, table1=0
    // Byte 0: R5|dR3 = (31<<3)|0 = 0xF8
    // Byte 1: G5|dG3 = (31<<3)|0 = 0xF8
    // Byte 2: B5|dB3 = (31<<3)|0 = 0xF8
    // Byte 3: table0(3)|table1(3)|diff(1)|flip(1) = 000|000|1|0 = 0x02
    // Bytes 4-7: pixel indices all = 01 (MSB=0, LSB=1) → LSBs all 1, MSBs all 0
    //   MSB word = 0x0000, LSB word = 0xFFFF → bytes 4-7 = 0x0000FFFF
    uint8_t whiteBlock[8] = { 0xF8, 0xF8, 0xF8, 0x02, 0x00, 0x00, 0xFF, 0xFF };
    // 4x4 block = one block for a 4x4 texture
    GLuint whiteTex = 0;
    glGenTextures(1, &whiteTex);
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 4, 4, 0, 8, whiteBlock);
    GLenum err = glGetError();
    printf("  Synthetic white upload: glError=%u\n", err);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto whiteResult = renderAndReadback(program, whiteTex, 4, 4);
    analyzeReadback("Synthetic WHITE", whiteResult);

    // Also test synthetic black
    uint8_t blackBlock[8] = { 0x00, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0x00, 0x00 };
    // Base = (0,0,0), all pixel indices = 10 (MSB=1, LSB=0) → negative small modifier → clamps to 0
    GLuint blackTex = 0;
    glGenTextures(1, &blackTex);
    glBindTexture(GL_TEXTURE_2D, blackTex);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 4, 4, 0, 8, blackBlock);
    err = glGetError();
    printf("  Synthetic black upload: glError=%u\n", err);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto blackResult = renderAndReadback(program, blackTex, 4, 4);
    analyzeReadback("Synthetic BLACK", blackResult);

    // ── Visual alpha test: render tree70 with alpha discard to screen ──
    printf("\n=== Visual Alpha Test (rendering to screen for 5 seconds) ===\n");
    {
        // Build alpha test program
        GLuint alphaVS = compileShader(GL_VERTEX_SHADER, VS_SRC);
        GLuint alphaFS = compileShader(GL_FRAGMENT_SHADER, FS_ALPHA_TEST);
        GLuint alphaProg = glCreateProgram();
        glAttachShader(alphaProg, alphaVS);
        glAttachShader(alphaProg, alphaFS);
        glBindAttribLocation(alphaProg, 0, "aPos");
        glLinkProgram(alphaProg);
        GLint linkOk = 0;
        glGetProgramiv(alphaProg, GL_LINK_STATUS, &linkOk);
        if (!linkOk) {
            char log[512];
            glGetProgramInfoLog(alphaProg, 512, nullptr, log);
            printf("  Alpha program link error: %s\n", log);
        }
        glDeleteShader(alphaVS);
        glDeleteShader(alphaFS);

        GLint locTex = glGetUniformLocation(alphaProg, "uTexture");
        GLint locAlpha = glGetUniformLocation(alphaProg, "uAlphaTexture");
        printf("  Alpha program: id=%u, uTexture=%d, uAlphaTexture=%d\n", alphaProg, locTex, locAlpha);

        // Render 4 quadrants to the screen:
        //   Top-left:     tree70 RGB only (passthrough, no alpha test)
        //   Top-right:    tree70 alpha only (passthrough, shows grayscale alpha)
        //   Bottom-left:  tree70 with alpha test (RGB + alpha discard) — THIS IS THE KEY TEST
        //   Bottom-right: gpine with alpha test (if available)

        // Render to default framebuffer (screen)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Get actual screen size
        EGLint screenW = 800, screenH = 600;
        eglQuerySurface(egl.display, egl.surface, EGL_WIDTH, &screenW);
        eglQuerySurface(egl.display, egl.surface, EGL_HEIGHT, &screenH);
        printf("  Screen: %dx%d\n", screenW, screenH);

        for (int frame = 0; frame < 150; frame++) {  // ~5 sec at 30fps
            glViewport(0, 0, screenW, screenH);
            glClearColor(0.2f, 0.3f, 0.8f, 1.0f);  // Blue background — easy to see alpha discard
            glClear(GL_COLOR_BUFFER_BIT);

            // Helper: draw a textured quad in NDC coords [x0,y0] to [x1,y1]
            auto drawQuad = [](float x0, float y0, float x1, float y1) {
                float verts[] = { x0, y0,  x1, y0,  x0, y1,  x1, y1 };
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
                glEnableVertexAttribArray(0);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            };

            // Top-left: tree70 RGB only (passthrough shader)
            glUseProgram(program);
            glUniform1i(glGetUniformLocation(program, "uTexture"), 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, rgbTex);
            drawQuad(-1.0f, 0.0f, 0.0f, 1.0f);

            // Top-right: tree70 alpha only (passthrough shader, shows grayscale)
            glBindTexture(GL_TEXTURE_2D, alphaTex);
            drawQuad(0.0f, 0.0f, 1.0f, 1.0f);

            // Bottom-left: tree70 with alpha test shader
            glUseProgram(alphaProg);
            glUniform1i(locTex, 0);
            glUniform1i(locAlpha, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, rgbTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, alphaTex);
            glActiveTexture(GL_TEXTURE0);  // restore
            drawQuad(-1.0f, -1.0f, 0.0f, 0.0f);

            // Bottom-right: gpine with alpha test (or tree70 again if no gpine)
            GLuint gpRgbTex2 = rgbTex, gpAlphaTex2 = alphaTex;
            if (gpineRgb && gpineAlpha) {
                // Reuse already-uploaded gpine textures (glName 3 and 4)
                gpRgbTex2 = 3;
                gpAlphaTex2 = 4;
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gpRgbTex2);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gpAlphaTex2);
            glActiveTexture(GL_TEXTURE0);
            drawQuad(0.0f, -1.0f, 1.0f, 0.0f);

            eglSwapBuffers(egl.display, egl.surface);
        }

        // Screenshot: read back the screen and write a BMP
        {
            std::vector<uint8_t> screenPixels(screenW * screenH * 4);
            glReadPixels(0, 0, screenW, screenH, GL_RGBA, GL_UNSIGNED_BYTE, screenPixels.data());

            // Write BMP (bottom-up)
            FILE* bmpFile = fopen("alpha_test_screenshot.bmp", "wb");
            if (bmpFile) {
                uint32_t rowBytes = screenW * 3;
                uint32_t rowPad = (4 - (rowBytes % 4)) % 4;
                uint32_t dataSize = (rowBytes + rowPad) * screenH;
                uint32_t fileSize = 54 + dataSize;

                // BMP header
                uint8_t hdr[54] = {};
                hdr[0] = 'B'; hdr[1] = 'M';
                memcpy(hdr + 2, &fileSize, 4);
                uint32_t off = 54; memcpy(hdr + 10, &off, 4);
                uint32_t dibSize = 40; memcpy(hdr + 14, &dibSize, 4);
                int32_t sw = screenW, sh = screenH;
                memcpy(hdr + 18, &sw, 4);
                memcpy(hdr + 22, &sh, 4);
                uint16_t planes = 1; memcpy(hdr + 26, &planes, 2);
                uint16_t bpp = 24; memcpy(hdr + 28, &bpp, 2);
                fwrite(hdr, 54, 1, bmpFile);

                // Pixel data (GL readback is bottom-up, BMP is also bottom-up — direct write)
                uint8_t pad[3] = {};
                for (int y = 0; y < screenH; y++) {
                    for (int x = 0; x < screenW; x++) {
                        int idx = (y * screenW + x) * 4;
                        uint8_t bgr[3] = { screenPixels[idx+2], screenPixels[idx+1], screenPixels[idx+0] };
                        fwrite(bgr, 3, 1, bmpFile);
                    }
                    if (rowPad) fwrite(pad, rowPad, 1, bmpFile);
                }
                fclose(bmpFile);
                printf("  Screenshot saved: alpha_test_screenshot.bmp (%dx%d)\n", screenW, screenH);
            }
        }

        printf("  Visual test complete. Layout:\n");
        printf("    Top-left:     tree70 RGB (no alpha test)\n");
        printf("    Top-right:    tree70 alpha channel (grayscale)\n");
        printf("    Bottom-left:  tree70 WITH alpha test (should show leaves on blue bg)\n");
        printf("    Bottom-right: gpine WITH alpha test (should show pine on blue bg)\n");

        // Also do a readback of the alpha-tested quad
        auto alphaTestResult = renderAndReadback(alphaProg, rgbTex, readW, readH);
        // Wait — renderAndReadback only binds one texture. Need a custom readback for dual-texture.

        // Custom FBO readback for alpha test
        {
            GLuint fbo2 = 0, rbo2 = 0;
            glGenFramebuffers(1, &fbo2);
            glGenRenderbuffers(1, &rbo2);
            glBindRenderbuffer(GL_RENDERBUFFER, rbo2);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, readW, readH);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo2);

            glViewport(0, 0, readW, readH);
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);  // Blue
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(alphaProg);
            glUniform1i(locTex, 0);
            glUniform1i(locAlpha, 1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, rgbTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, alphaTex);
            glActiveTexture(GL_TEXTURE0);

            float verts[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glEnableVertexAttribArray(0);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glFinish();

            ReadbackResult rb;
            rb.width = readW;
            rb.height = readH;
            rb.rgba.resize(readW * readH * 4);
            glReadPixels(0, 0, readW, readH, GL_RGBA, GL_UNSIGNED_BYTE, rb.rgba.data());

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo2);
            glDeleteRenderbuffers(1, &rbo2);

            analyzeReadback("tree70 ALPHA TEST (RGB+alpha discard)", rb);

            // Count blue pixels (clear color = discarded pixels)
            int bluePixels = 0;
            for (int i = 0; i < readW * readH; i++) {
                uint8_t r = rb.rgba[i*4+0], g = rb.rgba[i*4+1], b = rb.rgba[i*4+2];
                if (r == 0 && g == 0 && b > 200) bluePixels++;
            }
            printf("  Blue (discarded) pixels: %d (%.1f%%)\n", bluePixels, 100.0 * bluePixels / (readW * readH));
        }

        glDeleteProgram(alphaProg);
    }

    printf("\nDone.\n");

    // Cleanup
    eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl.display, egl.context);
    if (egl.surface != EGL_NO_SURFACE) eglDestroySurface(egl.display, egl.surface);
    eglTerminate(egl.display);

    return 0;
}
