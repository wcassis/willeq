// gles2_texture_read_benchmark - Measure texture read costs on GLES2 (Mali 400)
//
// Renders a fullscreen quad with various fragment shaders to isolate:
//   1. Baseline FS cost (no texture read)
//   2. Single texture read (varying UV — pipelined)
//   3. Dependent texture read (UV computed from varying — stalls pipeline)
//   4. Two texture reads (scene + LUT — the production use case)
//   5. FP16 vs RGBA8 texture format
//   6. LUT size sensitivity (64, 256, 1024 texels)
//
// Usage:
//   ./gles2_texture_read_benchmark              # auto-detect (DRM first, then X11)
//   ./gles2_texture_read_benchmark --drm        # force DRM/GBM
//   ./gles2_texture_read_benchmark --x11        # force X11/EGL
//   ./gles2_texture_read_benchmark --frames N   # frames per test (default 300)
//   ./gles2_texture_read_benchmark --res W H    # resolution (default 1280 720)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
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

// ============================================================================
// EGL/DRM setup (same pattern as other benchmarks)
// ============================================================================

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

    drmModeRes* res = drmModeGetResources(state.drmFd);
    if (!res) {
        fprintf(stderr, "DRM: drmModeGetResources failed\n");
        close(state.drmFd);
        return false;
    }

    for (int i = 0; i < res->count_connectors && !state.modeFound; i++) {
        drmModeConnector* conn = drmModeGetConnector(state.drmFd, res->connectors[i]);
        if (!conn || conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
            if (conn) drmModeFreeConnector(conn);
            continue;
        }
        state.connectorId = conn->connector_id;

        for (int m = 0; m < conn->count_modes; m++) {
            if ((int)conn->modes[m].hdisplay == state.width &&
                (int)conn->modes[m].vdisplay == state.height) {
                state.mode = conn->modes[m];
                state.modeFound = true;
                break;
            }
        }
        if (!state.modeFound && conn->count_modes > 0) {
            state.mode = conn->modes[0];
            state.width = state.mode.hdisplay;
            state.height = state.mode.vdisplay;
            state.modeFound = true;
        }

        drmModeEncoder* enc = drmModeGetEncoder(state.drmFd, conn->encoder_id);
        if (enc) {
            state.crtcId = enc->crtc_id;
            drmModeFreeEncoder(enc);
        }
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);

    if (!state.modeFound) {
        fprintf(stderr, "DRM: no suitable mode found\n");
        close(state.drmFd);
        return false;
    }

    printf("DRM: Using mode %dx%d@%dHz\n", state.mode.hdisplay, state.mode.vdisplay, state.mode.vrefresh);

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

    printf("EGL %d.%d (DRM/GBM, GLES2)\n", major, minor);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "DRM: eglBindAPI failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "DRM: eglChooseConfig failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    state.gbmSurface = gbm_surface_create(state.gbmDevice, state.width, state.height,
                                            GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!state.gbmSurface) {
        fprintf(stderr, "DRM: gbm_surface_create failed\n");
        eglTerminate(state.display);
        gbm_device_destroy(state.gbmDevice);
        close(state.drmFd);
        return false;
    }

    state.surface = eglCreateWindowSurface(state.display, state.config,
                                            (EGLNativeWindowType)state.gbmSurface, nullptr);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
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

static void drmPresent(EGLState& state) {
    eglSwapBuffers(state.display, state.surface);
    struct gbm_bo* bo = gbm_surface_lock_front_buffer(state.gbmSurface);
    if (!bo) return;

    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t fb = 0;
    drmModeAddFB(state.drmFd, state.width, state.height, 24, 32, stride, handle, &fb);

    static bool first = true;
    if (first) {
        drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode);
        first = false;
    } else {
        int ret = drmModePageFlip(state.drmFd, state.crtcId, fb, 0, nullptr);
        if (ret) drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode);
    }

    static struct gbm_bo* prevBo = nullptr;
    static uint32_t prevFb = 0;
    if (prevBo) {
        drmModeRmFB(state.drmFd, prevFb);
        gbm_surface_release_buffer(state.gbmSurface, prevBo);
    }
    prevBo = bo;
    prevFb = fb;
}
#endif

static bool initX11(EGLState& state) {
    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY) return false;

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) return false;

    printf("EGL %d.%d (X11/default, GLES2)\n", major, minor);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs) || numConfigs == 0) {
        eglTerminate(state.display);
        return false;
    }

    EGLint pbufferAttribs[] = { EGL_WIDTH, state.width, EGL_HEIGHT, state.height, EGL_NONE };
    state.surface = eglCreatePbufferSurface(state.display, state.config, pbufferAttribs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
        eglTerminate(state.display);
        return false;
    }

    eglMakeCurrent(state.display, state.surface, state.surface, state.context);
    return true;
}

static void cleanup(EGLState& state) {
    eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(state.display, state.context);
    eglDestroySurface(state.display, state.surface);
    eglTerminate(state.display);
#ifdef EQT_HAS_DRM
    if (state.isDRM) {
        if (state.gbmSurface) gbm_surface_destroy(state.gbmSurface);
        if (state.gbmDevice) gbm_device_destroy(state.gbmDevice);
        if (state.drmFd >= 0) close(state.drmFd);
    }
#endif
}

// ============================================================================
// Shader compilation
// ============================================================================

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
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
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ============================================================================
// Shader sources — fullscreen quad with various FS complexity
// ============================================================================

// Shared VS: fullscreen quad, passes UV and a simulated worldPos varying
static const char* VS_FULLSCREEN = R"(
precision highp float;
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    // Simulate worldPos varying (as in per-pixel lighting VS)
    vWorldPos = vec3(aPosition * 10.0, 5.0);
}
)";

// 1. Baseline: no texture read, just output a solid color from varying
static const char* FS_BASELINE = R"(
precision mediump float;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    gl_FragColor = vec4(vTexCoord, 0.5, 1.0);
}
)";

// 2. Single pipelined texture read (UV from varying — hardware can prefetch)
static const char* FS_ONE_READ = R"(
precision mediump float;
uniform sampler2D uTexture;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

// 3. Dependent texture read: UV computed from varying math (pipeline stall)
static const char* FS_DEPENDENT_READ = R"(
precision mediump float;
uniform sampler2D uLUT;
uniform float uLUTScale;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    // Compute UV from varying (dependent read — can't prefetch)
    float d2 = dot(vWorldPos, vWorldPos);
    gl_FragColor = texture2D(uLUT, vec2(d2 * uLUTScale, 0.5));
}
)";

// 4. Pipelined read + dependent read (the production LUT use case)
static const char* FS_TWO_READS = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform sampler2D uLUT;
uniform float uLUTScale;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float d2 = dot(vWorldPos, vWorldPos);
    vec4 lut = texture2D(uLUT, vec2(d2 * uLUTScale, 0.5));
    gl_FragColor = texColor * lut.r + lut.a;
}
)";

// 5. Two pipelined reads (both from varyings — no dependency)
static const char* FS_TWO_PIPELINED = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform sampler2D uLUT;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lut = texture2D(uLUT, vTexCoord);
    gl_FragColor = texColor * lut.r + lut.a;
}
)";

// 6. Dependent read + ALU (simulates the full LUT lighting math)
static const char* FS_DEPENDENT_PLUS_ALU = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform sampler2D uLUT;
uniform float uLUTScale;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uLUT, vec2(d2 * uLUTScale, 0.5));
    float invD = lut.r;
    float pLa = lut.a;
    // Simulate NdotL with a made-up normal (same ALU as production)
    vec3 N = normalize(vec3(0.0, 1.0, 0.0));
    float pLn = max(dot(N, pLv * invD), 0.0);
    vec3 pLight = uLightColor * pLn * pLa;

    gl_FragColor = vec4(texColor.rgb + pLight * texColor.rgb, texColor.a);
}
)";

// 7. Same ALU as #6 but computed without LUT (for direct comparison)
static const char* FS_ALU_NO_LUT = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform float uLUTScale;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform vec3 uLightAtten;
varying vec2 vTexCoord;
varying vec3 vWorldPos;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uLightPos - vWorldPos;
    float d2 = dot(pLv, pLv) + 0.0001;
    float invD = inversesqrt(d2);
    float pLa = 1.0 / (uLightAtten.x + uLightAtten.z * d2 + 0.0001);
    vec3 N = normalize(vec3(0.0, 1.0, 0.0));
    float pLn = max(dot(N, pLv * invD), 0.0);
    vec3 pLight = uLightColor * pLn * pLa;

    gl_FragColor = vec4(texColor.rgb + pLight * texColor.rgb, texColor.a);
}
)";

// ============================================================================
// FP16 helpers and texture creation
// ============================================================================

static uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint16_t sign = (x >> 16) & 0x8000;
    int exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint16_t frac = (x >> 13) & 0x03FF;
    if (exp <= 0) return sign;
    if (exp >= 31) return sign | 0x7C00;
    return sign | (exp << 10) | frac;
}

static bool hasHalfFloatTextures() {
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    if (!ext) return false;
    return strstr(ext, "GL_OES_texture_half_float") != nullptr &&
           strstr(ext, "GL_OES_texture_half_float_linear") != nullptr;
}

static GLuint createCheckerTexture(int size) {
    std::vector<uint8_t> pixels(size * size * 3);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool check = ((x / (size/8)) + (y / (size/8))) % 2 == 0;
            uint8_t val = check ? 180 : 100;
            int i = (y * size + x) * 3;
            pixels[i] = val; pixels[i+1] = val; pixels[i+2] = val;
        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

// Create an Nx1 LUT texture. Format depends on hasFP16 flag.
// Stores: L = inversesqrt(d²+eps), A = 1/(c+q*d²+eps)
static GLuint createLUT(int width, bool fp16, float attenConst, float attenQuad, float maxD2) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    if (fp16) {
        std::vector<uint16_t> data(width * 2);
        for (int i = 0; i < width; i++) {
            float t = (float)i / (float)(width - 1);
            float d2 = t * maxD2 + 0.0001f;
            data[i * 2 + 0] = floatToHalf(1.0f / sqrtf(d2));
            data[i * 2 + 1] = floatToHalf(1.0f / (attenConst + attenQuad * d2 + 0.0001f));
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width, 1, 0,
                     GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES, data.data());
    } else {
        // RGBA8 fallback: encode values as 8-bit [0,1] with clamping
        std::vector<uint8_t> data(width * 4);
        for (int i = 0; i < width; i++) {
            float t = (float)i / (float)(width - 1);
            float d2 = t * maxD2 + 0.0001f;
            float invD = 1.0f / sqrtf(d2);
            float atten = 1.0f / (attenConst + attenQuad * d2 + 0.0001f);
            // Clamp to [0,1] and scale — invD can be large near zero
            float invDNorm = fminf(invD * 0.1f, 1.0f);  // scale down, will scale up in shader
            data[i * 4 + 0] = (uint8_t)(invDNorm * 255.0f);
            data[i * 4 + 1] = (uint8_t)(fminf(atten, 1.0f) * 255.0f);
            data[i * 4 + 2] = 0;
            data[i * 4 + 3] = (uint8_t)(fminf(atten, 1.0f) * 255.0f);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "LUT creation failed (GL error 0x%x), size=%d, fp16=%d\n", err, width, fp16);
        glDeleteTextures(1, &tex);
        return 0;
    }
    return tex;
}

// ============================================================================
// Fullscreen quad geometry
// ============================================================================

static GLuint createFullscreenQuad() {
    // Two triangles covering [-1,1] NDC with UV [0,1]
    float verts[] = {
        // pos.x  pos.y  uv.x  uv.y
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    return vbo;
}

// ============================================================================
// Benchmark runner
// ============================================================================

struct BenchResult {
    double avgMs;
    double minMs;
    double maxMs;
};

static BenchResult runBench(EGLState& state, GLuint program, GLuint vbo,
                             GLuint sceneTex, GLuint lutTex, float lutScale,
                             int numFrames) {
    glUseProgram(program);

    // Set uniforms (safe to query non-existent ones — returns -1)
    GLint locTex = glGetUniformLocation(program, "uTexture");
    GLint locLUT = glGetUniformLocation(program, "uLUT");
    GLint locScale = glGetUniformLocation(program, "uLUTScale");
    GLint locLightPos = glGetUniformLocation(program, "uLightPos");
    GLint locLightColor = glGetUniformLocation(program, "uLightColor");
    GLint locLightAtten = glGetUniformLocation(program, "uLightAtten");

    if (locTex >= 0) glUniform1i(locTex, 0);
    if (locLUT >= 0) glUniform1i(locLUT, 1);
    if (locScale >= 0) glUniform1f(locScale, lutScale);
    if (locLightPos >= 0) {
        float pos[] = {0.0f, 3.0f, 1.0f};
        glUniform3fv(locLightPos, 1, pos);
    }
    if (locLightColor >= 0) {
        float col[] = {1.35f, 1.05f, 0.6f};
        glUniform3fv(locLightColor, 1, col);
    }
    if (locLightAtten >= 0) {
        float att[] = {1.0f, 0.0f, 19.0f / (52.0f * 52.0f)};
        glUniform3fv(locLightAtten, 1, att);
    }

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    if (lutTex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lutTex);
        glActiveTexture(GL_TEXTURE0);
    }

    // Bind geometry
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);

    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, state.width, state.height);

    // Warmup
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
    }

    // Timed frames
    std::vector<double> times;
    times.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif
        auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    BenchResult r;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : times) {
        sum += t;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }
    r.avgMs = sum / times.size();
    r.minMs = mn;
    r.maxMs = mx;
    return r;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int numFrames = 300;
    bool forceDRM = false, forceX11 = false;
    int width = 1280, height = 720;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) forceDRM = true;
        else if (strcmp(argv[i], "--x11") == 0) forceX11 = true;
        else if (strcmp(argv[i], "--frames") == 0 && i+1 < argc) numFrames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--res") == 0 && i+2 < argc) {
            width = atoi(argv[++i]);
            height = atoi(argv[++i]);
        }
    }

    printf("=== GLES2 Texture Read Cost Benchmark ===\n");
    printf("Resolution: %dx%d, Frames: %d per test\n\n", width, height, numFrames);

    EGLState state;
    state.width = width;
    state.height = height;
    bool ok = false;

#ifdef EQT_HAS_DRM
    if (!forceX11) ok = initDRM(state);
#endif
    if (!ok && !forceDRM) ok = initX11(state);
    if (!ok) {
        fprintf(stderr, "Failed to initialize EGL context\n");
        return 1;
    }

    printf("GL: %s\n", glGetString(GL_RENDERER));
    printf("GL: %s\n\n", glGetString(GL_VERSION));

    bool hasFP16 = hasHalfFloatTextures();
    printf("Extensions: OES_texture_half_float = %s\n", hasFP16 ? "YES" : "no");
    printf("            OES_texture_half_float_linear = %s\n\n", hasFP16 ? "YES" : "no");

    // Create resources
    GLuint vbo = createFullscreenQuad();
    GLuint sceneTex = createCheckerTexture(256);

    float attenConst = 1.0f;
    float attenQuad = 19.0f / (52.0f * 52.0f);
    float maxD2 = 4096.0f;
    float lutScale = 1.0f / maxD2;

    // Create LUT textures of various sizes and formats
    GLuint lutFP16_256 = 0, lutFP16_64 = 0, lutFP16_1024 = 0;
    GLuint lutRGBA8_256 = 0;

    if (hasFP16) {
        lutFP16_64 = createLUT(64, true, attenConst, attenQuad, maxD2);
        lutFP16_256 = createLUT(256, true, attenConst, attenQuad, maxD2);
        lutFP16_1024 = createLUT(1024, true, attenConst, attenQuad, maxD2);
    }
    lutRGBA8_256 = createLUT(256, false, attenConst, attenQuad, maxD2);

    printf("LUT textures created: FP16 64=%u 256=%u 1024=%u, RGBA8 256=%u\n\n",
           lutFP16_64, lutFP16_256, lutFP16_1024, lutRGBA8_256);

    // Compile shared VS
    GLuint vs = compileShader(GL_VERTEX_SHADER, VS_FULLSCREEN);
    if (!vs) { cleanup(state); return 1; }

    // Compile all FS variants and link programs
    struct ProgDef {
        const char* name;
        const char* fsSrc;
        GLuint program;
        GLuint fs;
    };
    ProgDef progs[] = {
        {"baseline",           FS_BASELINE,           0, 0},
        {"one_read",           FS_ONE_READ,           0, 0},
        {"dependent_read",     FS_DEPENDENT_READ,     0, 0},
        {"two_reads",          FS_TWO_READS,          0, 0},
        {"two_pipelined",      FS_TWO_PIPELINED,      0, 0},
        {"dependent_plus_alu", FS_DEPENDENT_PLUS_ALU, 0, 0},
        {"alu_no_lut",         FS_ALU_NO_LUT,         0, 0},
    };
    int numProgs = sizeof(progs) / sizeof(progs[0]);

    printf("Compiling shaders...\n");
    for (int i = 0; i < numProgs; i++) {
        progs[i].fs = compileShader(GL_FRAGMENT_SHADER, progs[i].fsSrc);
        if (progs[i].fs)
            progs[i].program = linkProgram(vs, progs[i].fs);
        if (!progs[i].program) {
            fprintf(stderr, "Failed to compile: %s\n", progs[i].name);
            cleanup(state);
            return 1;
        }
    }
    printf("  Programs compiled: %d\n\n", numProgs);

    // Helper to find program by name
    auto findProg = [&](const char* name) -> GLuint {
        for (int i = 0; i < numProgs; i++)
            if (strcmp(progs[i].name, name) == 0) return progs[i].program;
        return 0;
    };

    // ========================================================================
    // PHASE 1: Core texture read costs
    // ========================================================================

    struct TestDef {
        const char* name;
        GLuint program;
        GLuint lutTex;
    };

    GLuint defaultLUT = lutFP16_256 ? lutFP16_256 : lutRGBA8_256;

    printf("============================================================\n");
    printf("PHASE 1: Texture read cost isolation\n");
    printf("============================================================\n\n");

    TestDef phase1[] = {
        {"No texture read (baseline)",       findProg("baseline"),       0},
        {"1 pipelined read (scene tex)",     findProg("one_read"),       0},
        {"1 dependent read (LUT only)",      findProg("dependent_read"), defaultLUT},
        {"2 reads: pipelined + pipelined",   findProg("two_pipelined"), defaultLUT},
        {"2 reads: pipelined + dependent",   findProg("two_reads"),     defaultLUT},
    };
    int numPhase1 = sizeof(phase1) / sizeof(phase1[0]);

    struct TestResult {
        const char* name;
        BenchResult r1, r2;
        double avg() const { return (r1.avgMs + r2.avgMs) / 2.0; }
    };

    std::vector<TestResult> results1(numPhase1);

    for (int round = 0; round < 2; round++) {
        printf("--- Round %d ---\n", round + 1);
        for (int i = 0; i < numPhase1; i++) {
            BenchResult r = runBench(state, phase1[i].program, vbo,
                                      sceneTex, phase1[i].lutTex, lutScale, numFrames);
            if (round == 0) {
                results1[i].name = phase1[i].name;
                results1[i].r1 = r;
            } else {
                results1[i].r2 = r;
            }
            printf("  %-42s  avg: %.2f ms  (~%.0f FPS)\n",
                   phase1[i].name, r.avgMs, 1000.0 / r.avgMs);
        }
        printf("\n");
    }

    double baseAvg = results1[0].avg();
    printf("--- PHASE 1 SUMMARY ---\n");
    printf("%-42s  %7s  %7s  %8s\n", "Test", "ms", "FPS", "vs base");
    printf("%-42s  %7s  %7s  %8s\n", "----", "--", "---", "-------");
    for (int i = 0; i < numPhase1; i++) {
        double avg = results1[i].avg();
        printf("%-42s  %7.2f  %7.0f  %+7.2fms\n",
               results1[i].name, avg, 1000.0 / avg, avg - baseAvg);
    }
    printf("\n");

    // ========================================================================
    // PHASE 2: FP16 vs RGBA8 format comparison
    // ========================================================================

    printf("============================================================\n");
    printf("PHASE 2: FP16 vs RGBA8 texture format\n");
    printf("============================================================\n\n");

    struct FmtTest {
        const char* name;
        GLuint lutTex;
    };

    std::vector<FmtTest> phase2;
    if (lutFP16_256)
        phase2.push_back({"Dependent read: FP16 256x1", lutFP16_256});
    if (lutRGBA8_256)
        phase2.push_back({"Dependent read: RGBA8 256x1", lutRGBA8_256});

    std::vector<TestResult> results2(phase2.size());

    for (int round = 0; round < 2; round++) {
        printf("--- Round %d ---\n", round + 1);
        for (int i = 0; i < (int)phase2.size(); i++) {
            BenchResult r = runBench(state, findProg("dependent_read"), vbo,
                                      sceneTex, phase2[i].lutTex, lutScale, numFrames);
            if (round == 0) {
                results2[i].name = phase2[i].name;
                results2[i].r1 = r;
            } else {
                results2[i].r2 = r;
            }
            printf("  %-42s  avg: %.2f ms  (~%.0f FPS)\n",
                   phase2[i].name, r.avgMs, 1000.0 / r.avgMs);
        }
        printf("\n");
    }

    printf("--- PHASE 2 SUMMARY ---\n");
    printf("%-42s  %7s  %7s\n", "Test", "ms", "FPS");
    printf("%-42s  %7s  %7s\n", "----", "--", "---");
    for (int i = 0; i < (int)results2.size(); i++) {
        double avg = results2[i].avg();
        printf("%-42s  %7.2f  %7.0f\n", results2[i].name, avg, 1000.0 / avg);
    }
    printf("\n");

    // ========================================================================
    // PHASE 3: LUT size sensitivity
    // ========================================================================

    printf("============================================================\n");
    printf("PHASE 3: LUT size sensitivity (FP16)\n");
    printf("============================================================\n\n");

    struct SizeTest {
        const char* name;
        GLuint lutTex;
    };

    std::vector<SizeTest> phase3;
    if (lutFP16_64)   phase3.push_back({"Dependent read: FP16 64x1",   lutFP16_64});
    if (lutFP16_256)  phase3.push_back({"Dependent read: FP16 256x1",  lutFP16_256});
    if (lutFP16_1024) phase3.push_back({"Dependent read: FP16 1024x1", lutFP16_1024});

    std::vector<TestResult> results3(phase3.size());

    if (phase3.empty()) {
        printf("  (skipped — no FP16 texture support)\n\n");
    } else {
        for (int round = 0; round < 2; round++) {
            printf("--- Round %d ---\n", round + 1);
            for (int i = 0; i < (int)phase3.size(); i++) {
                BenchResult r = runBench(state, findProg("dependent_read"), vbo,
                                          sceneTex, phase3[i].lutTex, lutScale, numFrames);
                if (round == 0) {
                    results3[i].name = phase3[i].name;
                    results3[i].r1 = r;
                } else {
                    results3[i].r2 = r;
                }
                printf("  %-42s  avg: %.2f ms  (~%.0f FPS)\n",
                       phase3[i].name, r.avgMs, 1000.0 / r.avgMs);
            }
            printf("\n");
        }

        printf("--- PHASE 3 SUMMARY ---\n");
        printf("%-42s  %7s  %7s\n", "Test", "ms", "FPS");
        printf("%-42s  %7s  %7s\n", "----", "--", "---");
        for (int i = 0; i < (int)results3.size(); i++) {
            double avg = results3[i].avg();
            printf("%-42s  %7.2f  %7.0f\n", results3[i].name, avg, 1000.0 / avg);
        }
        printf("\n");
    }

    // ========================================================================
    // PHASE 4: LUT vs ALU (the key comparison)
    // ========================================================================

    printf("============================================================\n");
    printf("PHASE 4: LUT vs ALU — full lighting math comparison\n");
    printf("============================================================\n\n");

    TestDef phase4[] = {
        {"ALU: inversesqrt + rcp (OPT C math)", findProg("alu_no_lut"),         0},
        {"LUT: FP16 texture (same result)",      findProg("dependent_plus_alu"), defaultLUT},
    };
    int numPhase4 = sizeof(phase4) / sizeof(phase4[0]);

    std::vector<TestResult> results4(numPhase4);

    for (int round = 0; round < 2; round++) {
        printf("--- Round %d ---\n", round + 1);
        for (int i = 0; i < numPhase4; i++) {
            BenchResult r = runBench(state, phase4[i].program, vbo,
                                      sceneTex, phase4[i].lutTex, lutScale, numFrames);
            if (round == 0) {
                results4[i].name = phase4[i].name;
                results4[i].r1 = r;
            } else {
                results4[i].r2 = r;
            }
            printf("  %-42s  avg: %.2f ms  (~%.0f FPS)\n",
                   phase4[i].name, r.avgMs, 1000.0 / r.avgMs);
        }
        printf("\n");
    }

    printf("--- PHASE 4 SUMMARY ---\n");
    printf("%-42s  %7s  %7s  %8s\n", "Test", "ms", "FPS", "delta");
    printf("%-42s  %7s  %7s  %8s\n", "----", "--", "---", "-----");
    double aluAvg = results4[0].avg();
    for (int i = 0; i < numPhase4; i++) {
        double avg = results4[i].avg();
        printf("%-42s  %7.2f  %7.0f  %+7.2fms\n",
               results4[i].name, avg, 1000.0 / avg, avg - aluAvg);
    }
    printf("\n");

    // ========================================================================
    // Overall summary
    // ========================================================================

    printf("============================================================\n");
    printf("KEY FINDINGS\n");
    printf("============================================================\n\n");

    double noTex = results1[0].avg();
    double onePipe = results1[1].avg();
    double oneDep = results1[2].avg();
    double twoPipe = results1[3].avg();
    double twoDep = results1[4].avg();

    printf("Cost of 1 pipelined texture read:    %+.2f ms\n", onePipe - noTex);
    printf("Cost of 1 dependent texture read:    %+.2f ms\n", oneDep - noTex);
    printf("Dependent read penalty vs pipelined: %+.2f ms\n", oneDep - onePipe);
    printf("Cost of 2nd pipelined read:          %+.2f ms  (from 1-read baseline)\n", twoPipe - onePipe);
    printf("Cost of 2nd dependent read:          %+.2f ms  (from 1-read baseline)\n", twoDep - onePipe);
    printf("\n");

    if (numPhase4 == 2) {
        double lutAvg = results4[1].avg();
        printf("LUT vs ALU (full lighting):\n");
        printf("  ALU (inversesqrt + rcp):  %.2f ms\n", aluAvg);
        printf("  LUT (FP16 texture):       %.2f ms\n", lutAvg);
        printf("  Delta:                    %+.2f ms (%s)\n",
               lutAvg - aluAvg,
               lutAvg < aluAvg ? "LUT WINS" : lutAvg > aluAvg ? "ALU WINS" : "TIE");
    }
    printf("\n");

    // Cleanup
    for (int i = 0; i < numProgs; i++) {
        if (progs[i].program) glDeleteProgram(progs[i].program);
        if (progs[i].fs) glDeleteShader(progs[i].fs);
    }
    glDeleteShader(vs);
    glDeleteTextures(1, &sceneTex);
    if (lutFP16_64) glDeleteTextures(1, &lutFP16_64);
    if (lutFP16_256) glDeleteTextures(1, &lutFP16_256);
    if (lutFP16_1024) glDeleteTextures(1, &lutFP16_1024);
    if (lutRGBA8_256) glDeleteTextures(1, &lutRGBA8_256);
    glDeleteBuffers(1, &vbo);

    cleanup(state);
    return 0;
}
