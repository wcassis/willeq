// gles2_etc1_benchmark - Test ETC1 hardware decode via OpenGL ES 2.0
//
// Creates an EGL context with EGL_OPENGL_ES_API (not desktop GL) and benchmarks
// ETC1 compressed texture uploads. The Mali 400 exposes GL_OES_compressed_ETC1_RGB8_texture
// only through its GLES2 profile, not through Lima's desktop GL 2.1 compatibility layer.
//
// Usage:
//   ./gles2_etc1_benchmark              # auto-detect (try DRM first, then X11)
//   ./gles2_etc1_benchmark --drm        # force DRM/GBM
//   ./gles2_etc1_benchmark --x11        # force X11/EGL
//   ./gles2_etc1_benchmark --iterations N  # upload iterations (default 500)

#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include <gbm.h>
#endif

// Compressed format tokens (in case headers are incomplete)
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif

struct FormatInfo {
    GLenum token;
    const char* name;
    int bytesPerBlock;   // bytes per 4x4 block
};

static const FormatInfo g_formats[] = {
    { GL_ETC1_RGB8_OES,                  "ETC1_RGB8_OES",          8 },
    { GL_COMPRESSED_RGB_S3TC_DXT1_EXT,   "S3TC_DXT1 (BC1)",       8 },
    { GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,  "S3TC_DXT3 (BC2)",      16 },
    { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,  "S3TC_DXT5 (BC3)",      16 },
    { GL_COMPRESSED_RGB8_ETC2,           "ETC2_RGB8",              8 },
    { GL_COMPRESSED_RGBA8_ETC2_EAC,      "ETC2_RGBA8_EAC",       16 },
    { GL_COMPRESSED_RGBA_ASTC_4x4_KHR,  "ASTC_4x4",             16 },
};
static const int g_numFormats = sizeof(g_formats) / sizeof(g_formats[0]);

static const char* formatName(GLenum token) {
    for (int i = 0; i < g_numFormats; i++)
        if (g_formats[i].token == token) return g_formats[i].name;
    return nullptr;
}

static const FormatInfo* findFormat(GLenum token) {
    for (int i = 0; i < g_numFormats; i++)
        if (g_formats[i].token == token) return &g_formats[i];
    return nullptr;
}

// --- EGL context (GLES2) ---

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

    // GLES2 API - this is the key difference from gpu_texture_formats
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

    // Request GLES 2.0 context
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

// --- Benchmarks ---

static void drainGLErrors() {
    while (glGetError() != GL_NO_ERROR) {}
}

struct BenchResult {
    const char* name;
    GLenum format;
    size_t dataBytes;
    double avgUs;
    bool glError;
};

static BenchResult benchCompressed(const FormatInfo& fmt, int width, int height, int iterations) {
    BenchResult r = {};
    r.name = fmt.name;
    r.format = fmt.token;

    int blocksX = (width + 3) / 4;
    int blocksY = (height + 3) / 4;
    size_t dataSize = blocksX * blocksY * fmt.bytesPerBlock;
    r.dataBytes = dataSize;

    std::vector<uint8_t> data(dataSize, 0);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    drainGLErrors();

    // Warmup
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt.token, width, height, 0,
                           static_cast<GLsizei>(dataSize), data.data());
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        r.glError = true;
        glDeleteTextures(1, &tex);
        return r;
    }
    glFinish();

    // Timed
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt.token, width, height, 0,
                               static_cast<GLsizei>(dataSize), data.data());
    }
    glFinish();
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.avgUs = (totalMs * 1000.0) / iterations;

    glDeleteTextures(1, &tex);
    return r;
}

static BenchResult benchRGBA(int width, int height, int iterations) {
    BenchResult r = {};
    r.name = "RGBA8 (uncompressed)";
    r.format = GL_RGBA;

    size_t dataSize = width * height * 4;
    r.dataBytes = dataSize;

    std::vector<uint8_t> data(dataSize, 0);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    drainGLErrors();

    // Warmup
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glFinish();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    }
    glFinish();
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.avgUs = (totalMs * 1000.0) / iterations;

    glDeleteTextures(1, &tex);
    return r;
}

// Also benchmark at atlas page size (2048x2048) to see real-world atlas upload cost
static BenchResult benchCompressedLarge(const FormatInfo& fmt, int width, int height, int iterations) {
    BenchResult r = {};
    r.name = fmt.name;
    r.format = fmt.token;

    // Check max texture size first
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (width > maxSize || height > maxSize) {
        r.glError = true;
        return r;
    }

    int blocksX = (width + 3) / 4;
    int blocksY = (height + 3) / 4;
    size_t dataSize = blocksX * blocksY * fmt.bytesPerBlock;
    r.dataBytes = dataSize;

    std::vector<uint8_t> data(dataSize, 0);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    drainGLErrors();

    // Warmup
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt.token, width, height, 0,
                           static_cast<GLsizei>(dataSize), data.data());
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        r.glError = true;
        glDeleteTextures(1, &tex);
        return r;
    }
    glFinish();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt.token, width, height, 0,
                               static_cast<GLsizei>(dataSize), data.data());
    }
    glFinish();
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    r.avgUs = (totalMs * 1000.0) / iterations;

    glDeleteTextures(1, &tex);
    return r;
}

static void printResult(const BenchResult& r, double baselineUs) {
    if (r.glError) {
        printf("  %-28s  rejected (GL error) - not supported\n", r.name);
    } else {
        double ratio = r.avgUs / baselineUs;
        const char* verdict;
        if (ratio < 0.7)       verdict = ">>> HARDWARE (faster than uncompressed)";
        else if (ratio < 1.1)  verdict = "    INCONCLUSIVE";
        else                   verdict = "    SOFTWARE (slower than uncompressed)";
        printf("  %-28s  %8zu bytes  %8.1f us/upload  ratio=%.2fx  %s\n",
               r.name, r.dataBytes, r.avgUs, ratio, verdict);
    }
}

// --- Main ---

int main(int argc, char* argv[]) {
    enum Mode { AUTO, FORCE_DRM, FORCE_X11 };
    Mode mode = AUTO;
    int iterations = 500;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) mode = FORCE_DRM;
        else if (strcmp(argv[i], "--x11") == 0) mode = FORCE_X11;
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
            if (iterations < 1) iterations = 500;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--drm|--x11] [--iterations N]\n", argv[0]);
            printf("\nTests ETC1 hardware decode via OpenGL ES 2.0 context.\n");
            printf("Mali 400 exposes ETC1 only through GLES2, not desktop GL.\n");
            return 0;
        }
    }

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
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* slVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("GL Vendor:   %s\n", vendor ? vendor : "unknown");
    printf("GL Renderer: %s\n", renderer ? renderer : "unknown");
    printf("GL Version:  %s\n", version ? version : "unknown");
    printf("GLSL:        %s\n\n", slVersion ? slVersion : "unknown");

    GLint maxTexSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    printf("Max texture size: %d\n\n", maxTexSize);

    // --- Extensions ---
    printf("=== Relevant GLES2 extensions ===\n");
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    std::string extStr = extensions ? extensions : "";

    const char* extChecks[] = {
        "GL_OES_compressed_ETC1_RGB8_texture",
        "GL_EXT_compressed_ETC1_RGB8_sub_texture",
        "GL_EXT_texture_compression_s3tc",
        "GL_EXT_texture_compression_dxt1",
        "GL_ANGLE_texture_compression_dxt3",
        "GL_ANGLE_texture_compression_dxt5",
        "GL_KHR_texture_compression_astc_ldr",
        "GL_OES_texture_npot",
        "GL_OES_depth_texture",
        "GL_OES_depth24",
        "GL_OES_packed_depth_stencil",
        "GL_OES_element_index_uint",
        "GL_OES_vertex_array_object",
        "GL_OES_standard_derivatives",
        "GL_OES_texture_half_float",
        "GL_OES_texture_half_float_linear",
        "GL_EXT_blend_minmax",
        "GL_EXT_frag_depth",
        "GL_EXT_shadow_samplers",
        "GL_OES_EGL_image",
        "GL_OES_EGL_image_external",
        nullptr
    };
    for (int i = 0; extChecks[i]; i++) {
        bool found = extStr.find(extChecks[i]) != std::string::npos;
        printf("  %-48s %s\n", extChecks[i], found ? "YES" : "no");
    }

    // --- Compressed format list ---
    printf("\n=== Driver-reported compressed formats (GLES2) ===\n");
    GLint numFormats = 0;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numFormats);
    std::vector<GLint> fmtList(numFormats);
    if (numFormats > 0) {
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, fmtList.data());
    }

    bool etc1InList = false;
    for (int i = 0; i < numFormats; i++) {
        GLenum tok = static_cast<GLenum>(fmtList[i]);
        const char* name = formatName(tok);
        if (name)
            printf("  0x%04X  %s\n", tok, name);
        else
            printf("  0x%04X  (unknown)\n", tok);
        if (tok == GL_ETC1_RGB8_OES) etc1InList = true;
    }
    printf("  Total: %d formats\n", numFormats);
    printf("  ETC1_RGB8_OES in list: %s\n", etc1InList ? "YES" : "NO");

    // --- Benchmark: 256x256 ---
    const int texW = 256, texH = 256;
    printf("\n=== Upload benchmark: %dx%d, %d iterations ===\n\n", texW, texH, iterations);

    BenchResult baseline = benchRGBA(texW, texH, iterations);
    printf("  %-28s  %8zu bytes  %8.1f us/upload  (baseline)\n",
           baseline.name, baseline.dataBytes, baseline.avgUs);

    // Benchmark all known formats
    for (int i = 0; i < g_numFormats; i++) {
        BenchResult r = benchCompressed(g_formats[i], texW, texH, iterations);
        printResult(r, baseline.avgUs);
    }

    // --- Benchmark: 2048x2048 (atlas page size) ---
    if (maxTexSize >= 2048) {
        const int atlasW = 2048, atlasH = 2048;
        int atlasIter = iterations / 10;
        if (atlasIter < 10) atlasIter = 10;
        printf("\n=== Upload benchmark: %dx%d (atlas page), %d iterations ===\n\n",
               atlasW, atlasH, atlasIter);

        BenchResult baselineLarge = benchRGBA(atlasW, atlasH, atlasIter);
        printf("  %-28s  %8zu bytes  %8.1f us/upload  (baseline)\n",
               baselineLarge.name, baselineLarge.dataBytes, baselineLarge.avgUs);

        // Only benchmark ETC1 and S3TC DXT1 at atlas size
        const GLenum atlasFormats[] = {
            GL_ETC1_RGB8_OES,
            GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
        };
        for (GLenum tok : atlasFormats) {
            const FormatInfo* fi = findFormat(tok);
            if (!fi) continue;
            BenchResult r = benchCompressedLarge(*fi, atlasW, atlasH, atlasIter);
            printResult(r, baselineLarge.avgUs);
        }
    }

    // --- GLES2 shader capability check ---
    printf("\n=== GLES2 shader compilation test ===\n");

    // Test a minimal vertex + fragment shader pair to confirm GLES2 shaders work
    const char* vtxSrc =
        "attribute vec4 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "varying vec2 vTexCoord;\n"
        "uniform mat4 uMVP;\n"
        "void main() {\n"
        "    gl_Position = uMVP * aPosition;\n"
        "    vTexCoord = aTexCoord;\n"
        "}\n";

    const char* fragSrc =
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "uniform sampler2D uTexture;\n"
        "uniform float uTileScale;\n"
        "uniform vec2 uTileOffset;\n"
        "void main() {\n"
        "    vec2 atlasUV = uTileOffset + fract(vTexCoord) * uTileScale;\n"
        "    gl_FragColor = texture2D(uTexture, atlasUV);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vtxSrc, nullptr);
    glCompileShader(vs);
    GLint compiled = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &compiled);
    if (compiled) {
        printf("  Vertex shader:   OK\n");
    } else {
        char log[512];
        glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
        printf("  Vertex shader:   FAILED: %s\n", log);
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &compiled);
    if (compiled) {
        printf("  Fragment shader: OK\n");
    } else {
        char log[512];
        glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        printf("  Fragment shader: FAILED: %s\n", log);
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (linked) {
        printf("  Program link:    OK\n");

        // Check uniform/attribute locations
        GLint locMVP = glGetUniformLocation(prog, "uMVP");
        GLint locTex = glGetUniformLocation(prog, "uTexture");
        GLint locScale = glGetUniformLocation(prog, "uTileScale");
        GLint locOffset = glGetUniformLocation(prog, "uTileOffset");
        GLint locPos = glGetAttribLocation(prog, "aPosition");
        GLint locTC = glGetAttribLocation(prog, "aTexCoord");
        printf("  Uniforms:  uMVP=%d  uTexture=%d  uTileScale=%d  uTileOffset=%d\n",
               locMVP, locTex, locScale, locOffset);
        printf("  Attribs:   aPosition=%d  aTexCoord=%d\n", locPos, locTC);
    } else {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        printf("  Program link:    FAILED: %s\n", log);
    }

    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    // --- Summary ---
    printf("\n=== Interpretation ===\n");
    printf("  ratio < 0.7x  : Hardware decode (GPU decodes natively, less data transfer)\n");
    printf("  ratio ~ 1.0x  : Inconclusive\n");
    printf("  ratio > 1.1x  : Software decode (CPU decodes before GPU upload)\n");
    printf("\n  If ETC1 shows HARDWARE and shaders compile, this Mali 400 can use\n");
    printf("  GLES2 with ETC1 atlas textures for optimal rendering.\n");

    cleanup(egl);
    return 0;
}
