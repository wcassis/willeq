// gpu_texture_formats - Test GPU hardware support for compressed texture formats
//
// Creates a minimal EGL context (GBM for DRM, or X11) and benchmarks
// compressed texture uploads to distinguish hardware vs software decode paths.
//
// Usage:
//   ./gpu_texture_formats              # auto-detect (try DRM first, then X11)
//   ./gpu_texture_formats --drm        # force DRM/GBM
//   ./gpu_texture_formats --x11        # force X11/EGL
//   ./gpu_texture_formats --iterations N  # upload iterations (default 500)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

#include <EGL/egl.h>
#include <GL/gl.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <gbm.h>
#endif

// Compressed format tokens (may not be in all GL headers)
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

// Function pointer types
typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum, GLint, GLenum, GLsizei,
                                               GLsizei, GLint, GLsizei, const void*);

static PFNGLCOMPRESSEDTEXIMAGE2DPROC pfnCompressedTexImage2D = nullptr;

struct FormatInfo {
    GLenum token;
    const char* name;
    int bytesPerBlock;   // bytes per 4x4 block
    int blockPixels;     // pixels per block (always 16 for 4x4)
    bool hasAlpha;
};

static const FormatInfo g_knownFormats[] = {
    { GL_ETC1_RGB8_OES,                  "ETC1_RGB8_OES",          8, 16, false },
    { GL_COMPRESSED_RGB_S3TC_DXT1_EXT,   "S3TC_DXT1 (BC1)",       8, 16, false },
    { GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,  "S3TC_DXT3 (BC2)",      16, 16, true  },
    { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,  "S3TC_DXT5 (BC3)",      16, 16, true  },
    { GL_COMPRESSED_RGB8_ETC2,           "ETC2_RGB8",              8, 16, false },
    { GL_COMPRESSED_RGBA8_ETC2_EAC,      "ETC2_RGBA8_EAC",       16, 16, true  },
    { GL_COMPRESSED_RGBA_ASTC_4x4_KHR,  "ASTC_4x4",             16, 16, true  },
};
static const int g_numKnownFormats = sizeof(g_knownFormats) / sizeof(g_knownFormats[0]);

static const char* formatTokenName(GLenum token) {
    for (int i = 0; i < g_numKnownFormats; i++) {
        if (g_knownFormats[i].token == token) return g_knownFormats[i].name;
    }
    return nullptr;
}

static const FormatInfo* findFormat(GLenum token) {
    for (int i = 0; i < g_numKnownFormats; i++) {
        if (g_knownFormats[i].token == token) return &g_knownFormats[i];
    }
    return nullptr;
}

// --- EGL context management ---

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
    // Try common DRM device paths
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

    printf("EGL %d.%d (DRM/GBM)\n", major, minor);

    eglBindAPI(EGL_OPENGL_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "DRM: eglChooseConfig failed\n");
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

    EGLint contextAttribs[] = { EGL_NONE };
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "DRM: eglCreateContext failed (0x%x)\n", eglGetError());
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

    printf("EGL %d.%d (X11/default)\n", major, minor);

    eglBindAPI(EGL_OPENGL_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "X11: eglChooseConfig failed\n");
        eglTerminate(state.display);
        return false;
    }

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, 64,
        EGL_HEIGHT, 64,
        EGL_NONE
    };
    state.surface = eglCreatePbufferSurface(state.display, config, pbufferAttribs);

    EGLint contextAttribs[] = { EGL_NONE };
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "X11: eglCreateContext failed (0x%x)\n", eglGetError());
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

// --- Texture upload benchmarks ---

struct BenchmarkResult {
    const char* name;
    GLenum format;
    int texWidth;
    int texHeight;
    int iterations;
    double totalMs;
    double avgUs;
    size_t dataBytes;
    bool glError;
};

// Drain any stale GL errors
static void drainGLErrors() {
    while (glGetError() != GL_NO_ERROR) {}
}

static BenchmarkResult benchmarkCompressedUpload(const FormatInfo& fmt, int width, int height,
                                                  int iterations) {
    BenchmarkResult result = {};
    result.name = fmt.name;
    result.format = fmt.token;
    result.texWidth = width;
    result.texHeight = height;
    result.iterations = iterations;

    int blocksX = (width + 3) / 4;
    int blocksY = (height + 3) / 4;
    size_t dataSize = blocksX * blocksY * fmt.bytesPerBlock;
    result.dataBytes = dataSize;

    // Allocate zeroed buffer (valid compressed data not required for timing)
    std::vector<uint8_t> data(dataSize, 0);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    drainGLErrors();

    // Warmup: single upload to prime any lazy initialization
    pfnCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt.token, width, height, 0,
                            static_cast<GLsizei>(dataSize), data.data());
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        result.glError = true;
        glDeleteTextures(1, &tex);
        return result;
    }
    glFinish();

    // Timed uploads
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        pfnCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt.token, width, height, 0,
                                static_cast<GLsizei>(dataSize), data.data());
    }
    glFinish();  // ensure all uploads complete
    auto end = std::chrono::high_resolution_clock::now();

    result.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    result.avgUs = (result.totalMs * 1000.0) / iterations;

    glDeleteTextures(1, &tex);
    return result;
}

static BenchmarkResult benchmarkUncompressedUpload(int width, int height, int iterations) {
    BenchmarkResult result = {};
    result.name = "RGBA8 (uncompressed)";
    result.format = GL_RGBA;
    result.texWidth = width;
    result.texHeight = height;
    result.iterations = iterations;

    size_t dataSize = width * height * 4;
    result.dataBytes = dataSize;

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

    result.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    result.avgUs = (result.totalMs * 1000.0) / iterations;

    glDeleteTextures(1, &tex);
    return result;
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
            printf("\nTests GPU hardware support for compressed texture formats.\n");
            printf("Compares upload times to distinguish hardware vs software decode.\n");
            return 0;
        }
    }

    // --- Initialize EGL context ---
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

    // --- Print GPU info ---
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    printf("GL Vendor:   %s\n", vendor ? vendor : "unknown");
    printf("GL Renderer: %s\n", renderer ? renderer : "unknown");
    printf("GL Version:  %s\n\n", version ? version : "unknown");

    // --- Resolve glCompressedTexImage2D ---
    pfnCompressedTexImage2D = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(
        eglGetProcAddress("glCompressedTexImage2D"));
    if (!pfnCompressedTexImage2D) {
        fprintf(stderr, "glCompressedTexImage2D not available\n");
        cleanup(egl);
        return 1;
    }

    // --- Query compressed texture formats ---
    GLint numFormats = 0;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numFormats);
    printf("=== Driver-reported compressed texture formats: %d ===\n", numFormats);

    std::vector<GLint> formats(numFormats);
    if (numFormats > 0) {
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.data());
    }

    std::vector<const FormatInfo*> supportedKnown;
    std::vector<GLenum> supportedUnknown;

    for (int i = 0; i < numFormats; i++) {
        GLenum tok = static_cast<GLenum>(formats[i]);
        const char* name = formatTokenName(tok);
        if (name) {
            printf("  0x%04X  %s\n", tok, name);
            const FormatInfo* fi = findFormat(tok);
            if (fi) supportedKnown.push_back(fi);
        } else {
            printf("  0x%04X  (unknown)\n", tok);
            supportedUnknown.push_back(tok);
        }
    }

    // Check if specific formats of interest are in the list
    printf("\n=== Format presence in driver list ===\n");
    bool etc1InList = false, dxt1InList = false;
    for (int i = 0; i < numFormats; i++) {
        if (static_cast<GLenum>(formats[i]) == GL_ETC1_RGB8_OES) etc1InList = true;
        if (static_cast<GLenum>(formats[i]) == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) dxt1InList = true;
    }
    printf("  ETC1_RGB8_OES:  %s\n", etc1InList ? "YES" : "NO");
    printf("  S3TC_DXT1:      %s\n", dxt1InList ? "YES" : "NO");
    printf("\n  Note: Formats in this list are ones the driver considers 'natively'\n");
    printf("  supported, but Mesa may still software-decode some of them.\n");
    printf("  The upload benchmark below gives a more definitive answer.\n");

    // --- Check relevant extensions ---
    printf("\n=== Relevant GL extensions ===\n");
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    std::string extStr = extensions ? extensions : "";
    const char* extChecks[] = {
        "GL_OES_compressed_ETC1_RGB8_texture",
        "GL_EXT_texture_compression_s3tc",
        "GL_EXT_texture_compression_dxt1",
        "GL_ANGLE_texture_compression_dxt3",
        "GL_ANGLE_texture_compression_dxt5",
        "GL_ARB_texture_compression",
        "GL_KHR_texture_compression_astc_ldr",
        "GL_ARB_ES2_compatibility",
        nullptr
    };
    for (int i = 0; extChecks[i]; i++) {
        bool found = extStr.find(extChecks[i]) != std::string::npos;
        printf("  %-48s %s\n", extChecks[i], found ? "YES" : "no");
    }

    // --- Benchmark uploads ---
    const int texW = 256;
    const int texH = 256;

    printf("\n=== Upload benchmark: %dx%d textures, %d iterations ===\n\n", texW, texH, iterations);

    // Baseline: uncompressed RGBA
    BenchmarkResult baseline = benchmarkUncompressedUpload(texW, texH, iterations);
    printf("%-28s  %8zu bytes  %8.1f us/upload  (baseline)\n",
           baseline.name, baseline.dataBytes, baseline.avgUs);

    // Benchmark each known supported compressed format
    std::vector<BenchmarkResult> results;
    for (const FormatInfo* fi : supportedKnown) {
        BenchmarkResult r = benchmarkCompressedUpload(*fi, texW, texH, iterations);
        results.push_back(r);

        if (r.glError) {
            printf("%-28s  %8zu bytes  GL ERROR (format not actually usable)\n",
                   r.name, r.dataBytes);
        } else {
            double ratio = r.avgUs / baseline.avgUs;
            const char* verdict;
            // Hardware decode: compressed upload should be faster than uncompressed
            // (less data to transfer, GPU decodes natively)
            // Software decode: compressed upload slower than or similar to uncompressed
            // (CPU must decode, then upload full-size result)
            if (ratio < 0.7) {
                verdict = ">>> HARDWARE (faster than uncompressed)";
            } else if (ratio < 1.1) {
                verdict = "    INCONCLUSIVE (similar to uncompressed)";
            } else {
                verdict = "    SOFTWARE (slower than uncompressed)";
            }
            printf("%-28s  %8zu bytes  %8.1f us/upload  ratio=%.2fx  %s\n",
                   r.name, r.dataBytes, r.avgUs, ratio, verdict);
        }
    }

    // Also try formats NOT in the driver list to see if they're still accepted
    printf("\n=== Probing formats not in driver list ===\n\n");
    for (int i = 0; i < g_numKnownFormats; i++) {
        bool alreadyTested = false;
        for (const FormatInfo* fi : supportedKnown) {
            if (fi->token == g_knownFormats[i].token) {
                alreadyTested = true;
                break;
            }
        }
        if (alreadyTested) continue;

        BenchmarkResult r = benchmarkCompressedUpload(g_knownFormats[i], texW, texH, iterations);
        if (r.glError) {
            printf("%-28s  rejected (GL error) - not supported\n", r.name);
        } else {
            double ratio = r.avgUs / baseline.avgUs;
            const char* verdict;
            if (ratio < 0.7) {
                verdict = ">>> HARDWARE (faster than uncompressed)";
            } else if (ratio < 1.1) {
                verdict = "    INCONCLUSIVE (similar to uncompressed)";
            } else {
                verdict = "    SOFTWARE (slower than uncompressed)";
            }
            printf("%-28s  %8zu bytes  %8.1f us/upload  ratio=%.2fx  %s\n",
                   r.name, r.dataBytes, r.avgUs, ratio, verdict);
            printf("  ^ Note: accepted despite NOT being in GL_COMPRESSED_TEXTURE_FORMATS\n");
        }
    }

    // --- Summary ---
    printf("\n=== Interpretation guide ===\n");
    printf("  ratio < 0.7x  : Compressed upload is significantly faster than RGBA upload.\n");
    printf("                  The GPU is decoding the format in hardware (less data to transfer,\n");
    printf("                  no CPU decode overhead).\n");
    printf("  ratio ~ 1.0x  : Similar speed. May be hardware-decoded but with overhead,\n");
    printf("                  or the test is bottlenecked elsewhere. Inconclusive.\n");
    printf("  ratio > 1.1x  : Compressed upload is slower than RGBA. The driver is\n");
    printf("                  software-decoding on the CPU before uploading to GPU.\n");

    cleanup(egl);
    return 0;
}
