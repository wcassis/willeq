// gles2_program_switch_benchmark - Benchmark per-light-count shader programs and switching cost
//
// Tests:
//   Phase 1: Individual programs with 0-8 unrolled VS point lights + OPT C per-pixel FS
//   Phase 2: Program switching cost — multiple draw calls per frame with program changes
//   Phase 3: Program count pressure — does compiling more programs slow down rendering?
//
// Usage:
//   ./gles2_program_switch_benchmark              # auto-detect (DRM first, then X11)
//   ./gles2_program_switch_benchmark --drm        # force DRM/GBM
//   ./gles2_program_switch_benchmark --x11        # force X11/EGL
//   ./gles2_program_switch_benchmark --frames N   # frames per test (default 300)
//   ./gles2_program_switch_benchmark --res W H    # resolution (default 1280 720)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#ifdef EQT_HAS_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#endif

// ============================================================================
// EGL/DRM setup
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
    if (state.drmFd < 0) return false;

    drmModeRes* res = drmModeGetResources(state.drmFd);
    if (!res) { close(state.drmFd); return false; }

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
        if (enc) { state.crtcId = enc->crtc_id; drmModeFreeEncoder(enc); }
        drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);
    if (!state.modeFound) { close(state.drmFd); return false; }

    printf("DRM: Using mode %dx%d@%dHz\n", state.mode.hdisplay, state.mode.vdisplay, state.mode.vrefresh);

    state.gbmDevice = gbm_create_device(state.drmFd);
    if (!state.gbmDevice) { close(state.drmFd); return false; }

    state.display = eglGetDisplay((EGLNativeDisplayType)state.gbmDevice);
    if (state.display == EGL_NO_DISPLAY) {
        gbm_device_destroy(state.gbmDevice); close(state.drmFd); return false;
    }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) {
        gbm_device_destroy(state.gbmDevice); close(state.drmFd); return false;
    }
    printf("EGL %d.%d (DRM/GBM, GLES2)\n", major, minor);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    EGLint numConfigs;
    eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs);

    state.gbmSurface = gbm_surface_create(state.gbmDevice, state.width, state.height,
                                            GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    state.surface = eglCreateWindowSurface(state.display, state.config,
                                            (EGLNativeWindowType)state.gbmSurface, nullptr);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) {
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
    if (prevBo) { drmModeRmFB(state.drmFd, prevFb); gbm_surface_release_buffer(state.gbmSurface, prevBo); }
    prevBo = bo; prevFb = fb;
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
        EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    EGLint numConfigs;
    eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs);
    EGLint pbufferAttribs[] = { EGL_WIDTH, state.width, EGL_HEIGHT, state.height, EGL_NONE };
    state.surface = eglCreatePbufferSurface(state.display, state.config, pbufferAttribs);
    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) { eglTerminate(state.display); return false; }
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
// Shader compilation helpers
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
    glBindAttribLocation(prog, 1, "aNormal");
    glBindAttribLocation(prog, 2, "aColor");
    glBindAttribLocation(prog, 3, "aTexCoord0");
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
// VS generator: creates unrolled VS for exactly N point lights (no branching)
// ============================================================================

// Generate a per-pixel VS with exactly numLights unrolled point lights (indices 1..numLights).
// Light 0 (player light) is always handled per-pixel in the FS.
// No loops, no branches — just straight-line code.
static std::string generateVS(int numLights) {
    std::string s;
    s += "precision highp float;\n";
    s += "\n";
    s += "attribute vec3 aPosition;\n";
    s += "attribute vec3 aNormal;\n";
    s += "attribute vec4 aColor;\n";
    s += "attribute vec2 aTexCoord0;\n";
    s += "\n";
    s += "uniform mat4 mWorldViewProj;\n";
    s += "uniform mat4 mWorld;\n";
    s += "\n";
    s += "uniform vec3 uSunDir;\n";
    s += "uniform vec3 uSunColor;\n";
    s += "uniform vec3 uAmbientColor;\n";
    s += "uniform vec3 uTintColor;\n";
    s += "uniform float uFogStart;\n";
    s += "uniform float uFogEnd;\n";
    s += "\n";
    if (numLights > 0) {
        // Only declare light uniforms if we have VS lights
        char buf[128];
        snprintf(buf, sizeof(buf), "uniform vec3 uLightPos[%d];\n", numLights + 1);  // +1 because index 0 is reserved
        s += buf;
        snprintf(buf, sizeof(buf), "uniform vec3 uLightColor[%d];\n", numLights + 1);
        s += buf;
        snprintf(buf, sizeof(buf), "uniform vec3 uLightAtten[%d];\n", numLights + 1);
        s += buf;
    }
    s += "\n";
    s += "varying vec4 vColor;\n";
    s += "varying vec2 vTexCoord;\n";
    s += "varying float vFogFactor;\n";
    s += "varying vec3 vWorldPos;\n";
    s += "varying vec3 vWorldNormal;\n";
    s += "\n";
    s += "void main() {\n";
    s += "    vec4 pos = vec4(aPosition, 1.0);\n";
    s += "    gl_Position = mWorldViewProj * pos;\n";
    s += "    vTexCoord = aTexCoord0;\n";
    s += "\n";
    s += "    vec3 worldPos = (mWorld * pos).xyz;\n";
    s += "    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);\n";
    s += "\n";
    s += "    vWorldPos = worldPos;\n";
    s += "    vWorldNormal = worldN;\n";
    s += "\n";
    s += "    vec3 sunL = normalize(-uSunDir);\n";
    s += "    float sunNdotL = max(dot(worldN, sunL), 0.0);\n";
    s += "    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));\n";
    s += "\n";

    if (numLights > 0) {
        s += "    vec3 pointLighting = vec3(0.0);\n";
        // Unrolled lights 1..numLights
        for (int i = 1; i <= numLights; i++) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                "    {\n"
                "        vec3 lVec = uLightPos[%d] - worldPos;\n"
                "        float d = length(lVec) + 0.001;\n"
                "        float atten = 1.0 / (uLightAtten[%d].x\n"
                "                            + uLightAtten[%d].y * d\n"
                "                            + uLightAtten[%d].z * d * d + 0.0001);\n"
                "        float nl = max(dot(worldN, normalize(lVec)), 0.0);\n"
                "        pointLighting += uLightColor[%d] * nl * atten;\n"
                "    }\n",
                i, i, i, i, i);
            s += buf;
        }
        s += "\n";
        s += "    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);\n";
    } else {
        s += "    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor;\n";
    }

    s += "\n";
    s += "    float fogDist = length((mWorldViewProj * pos).xyz);\n";
    s += "    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);\n";
    s += "}\n";

    return s;
}

// ============================================================================
// OPT C fragment shader (quadratic attenuation, single inversesqrt, branchless)
// ============================================================================

static const char* FS_OPT_C = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv) + 0.0001;
    float invD = inversesqrt(d2);
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.z * d2 + 0.0001);
    float pLn = max(dot(vWorldNormal, pLv * invD), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight FS (no per-pixel light, trivial)
static const char* FS_LIGHTWEIGHT = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight VS (all 8 lights per-vertex, for baseline)
static const char* VS_LIGHTWEIGHT = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;

uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec3 uLightAtten[8];

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));

    vec3 pointLighting = vec3(0.0);
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4((baseLighting + pointLighting) * uTintColor, 1.0) * aColor;

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// ============================================================================
// Scene geometry
// ============================================================================

struct Vertex {
    float pos[3];
    float normal[3];
    float color[4];
    float uv[2];
};

static void buildSubdividedQuad(
    float x0, float y0, float z0,
    float x1, float y1, float z1,
    float x2, float y2, float z2,
    float nx, float ny, float nz,
    int divisionsU, int divisionsV,
    std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    float x3 = x1 + (x2 - x0), y3 = y1 + (y2 - y0), z3 = z1 + (z2 - z0);
    uint16_t baseIdx = (uint16_t)verts.size();
    for (int v = 0; v <= divisionsV; v++) {
        float tv = (float)v / divisionsV;
        for (int u = 0; u <= divisionsU; u++) {
            float tu = (float)u / divisionsU;
            Vertex vert;
            vert.pos[0] = (1-tu)*(1-tv)*x0 + tu*(1-tv)*x1 + (1-tu)*tv*x2 + tu*tv*x3;
            vert.pos[1] = (1-tu)*(1-tv)*y0 + tu*(1-tv)*y1 + (1-tu)*tv*y2 + tu*tv*y3;
            vert.pos[2] = (1-tu)*(1-tv)*z0 + tu*(1-tv)*z1 + (1-tu)*tv*z2 + tu*tv*z3;
            vert.normal[0] = nx; vert.normal[1] = ny; vert.normal[2] = nz;
            vert.color[0] = 0.8f; vert.color[1] = 0.8f; vert.color[2] = 0.8f; vert.color[3] = 1.0f;
            vert.uv[0] = tu; vert.uv[1] = tv;
            verts.push_back(vert);
        }
    }
    for (int v = 0; v < divisionsV; v++) {
        for (int u = 0; u < divisionsU; u++) {
            uint16_t i0 = baseIdx + v * (divisionsU + 1) + u;
            uint16_t i1 = i0 + 1;
            uint16_t i2 = i0 + (divisionsU + 1);
            uint16_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
    }
}

static void buildAlleyScene(std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    float w = 3.0f, d = 20.0f, h = 8.0f;
    int div = 4;
    buildSubdividedQuad(-w, 0, 0,  w, 0, 0,  -w, 0, d,  0, 1, 0, div, div*2, verts, indices);
    buildSubdividedQuad(-w, 0, 0,  -w, 0, d,  -w, h, 0,  1, 0, 0, div*2, div, verts, indices);
    buildSubdividedQuad(w, 0, d,  w, 0, 0,  w, h, d,  -1, 0, 0, div*2, div, verts, indices);
    buildSubdividedQuad(w, 0, d,  -w, 0, d,  w, h, d,  0, 0, -1, div, div, verts, indices);
}

static GLuint createCheckerTexture(int size) {
    std::vector<uint8_t> pixels(size * size * 3);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            bool check = ((x / (size/8)) + (y / (size/8))) % 2 == 0;
            uint8_t val = check ? 180 : 100;
            int i = (y * size + x) * 3;
            pixels[i] = val; pixels[i+1] = val; pixels[i+2] = val;
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

// ============================================================================
// Matrix helpers
// ============================================================================

static void mat4Identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4Perspective(float m[16], float fovDeg, float aspect, float near, float far) {
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fovDeg * 3.14159f / 360.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void mat4LookAt(float m[16], float eyeX, float eyeY, float eyeZ,
                        float atX, float atY, float atZ,
                        float upX, float upY, float upZ) {
    float fx = atX - eyeX, fy = atY - eyeY, fz = atZ - eyeZ;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy*upZ - fz*upY, sy = fz*upX - fx*upZ, sz = fx*upY - fy*upX;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= sl; sy /= sl; sz /= sl;
    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;
    mat4Identity(m);
    m[0] = sx; m[4] = sy; m[8] = sz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[12] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
    m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[14] = (fx*eyeX + fy*eyeY + fz*eyeZ);
}

static void mat4Multiply(float out[16], const float a[16], const float b[16]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            out[i + j*4] = 0;
            for (int k = 0; k < 4; k++)
                out[i + j*4] += a[i + k*4] * b[k + j*4];
        }
}

// ============================================================================
// Uniform setup helper — sets all uniforms for a program
// ============================================================================

struct SceneUniforms {
    float wvp[16];
    float world[16];
    float lightPos[8*3];
    float lightColor[8*3];
    float lightAtten[8*3];
    float playerLightPos[3];
    float playerLightColor[3];
    float playerLightAtten[3];
};

static void setupUniforms(GLuint program, const SceneUniforms& u) {
    GLint loc;
    loc = glGetUniformLocation(program, "mWorldViewProj");
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, u.wvp);
    loc = glGetUniformLocation(program, "mWorld");
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, u.world);

    float sunDir[] = {0.0f, -1.0f, 0.0f};
    float sunColor[] = {0.0f, 0.0f, 0.0f};
    float ambient[] = {0.05f, 0.05f, 0.08f};
    float tint[] = {0.3f, 0.3f, 0.4f};
    float fogColor[] = {0.1f, 0.1f, 0.15f, 1.0f};

    loc = glGetUniformLocation(program, "uSunDir");
    if (loc >= 0) glUniform3fv(loc, 1, sunDir);
    loc = glGetUniformLocation(program, "uSunColor");
    if (loc >= 0) glUniform3fv(loc, 1, sunColor);
    loc = glGetUniformLocation(program, "uAmbientColor");
    if (loc >= 0) glUniform3fv(loc, 1, ambient);
    loc = glGetUniformLocation(program, "uTintColor");
    if (loc >= 0) glUniform3fv(loc, 1, tint);
    loc = glGetUniformLocation(program, "uFogStart");
    if (loc >= 0) glUniform1f(loc, 50.0f);
    loc = glGetUniformLocation(program, "uFogEnd");
    if (loc >= 0) glUniform1f(loc, 200.0f);
    loc = glGetUniformLocation(program, "uFogColor");
    if (loc >= 0) glUniform4fv(loc, 1, fogColor);
    loc = glGetUniformLocation(program, "uTexture");
    if (loc >= 0) glUniform1i(loc, 0);

    loc = glGetUniformLocation(program, "uLightPos[0]");
    if (loc >= 0) glUniform3fv(loc, 8, u.lightPos);
    loc = glGetUniformLocation(program, "uLightColor[0]");
    if (loc >= 0) glUniform3fv(loc, 8, u.lightColor);
    loc = glGetUniformLocation(program, "uLightAtten[0]");
    if (loc >= 0) glUniform3fv(loc, 8, u.lightAtten);

    loc = glGetUniformLocation(program, "uPlayerLightPos");
    if (loc >= 0) glUniform3fv(loc, 1, u.playerLightPos);
    loc = glGetUniformLocation(program, "uPlayerLightColor");
    if (loc >= 0) glUniform3fv(loc, 1, u.playerLightColor);
    loc = glGetUniformLocation(program, "uPlayerLightAtten");
    if (loc >= 0) glUniform3fv(loc, 1, u.playerLightAtten);
}

// ============================================================================
// Benchmark result
// ============================================================================

struct BenchResult {
    double avgMs, minMs, maxMs;
};

static BenchResult computeStats(const std::vector<double>& times) {
    BenchResult r;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : times) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    r.avgMs = sum / times.size();
    r.minMs = mn;
    r.maxMs = mx;
    return r;
}

// ============================================================================
// Phase 1: Single-program benchmark (renders scene with one program)
// ============================================================================

static BenchResult runSingleProgram(EGLState& state, GLuint program, GLuint vbo, GLuint ibo,
                                     int indexCount, GLuint texture, int numFrames,
                                     const SceneUniforms& uniforms) {
    glUseProgram(program);
    setupUniforms(program, uniforms);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    int stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, state.width, state.height);

    // Warmup
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);
    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        glFinish();
#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif
        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    return computeStats(frameTimes);
}

// ============================================================================
// Phase 2: Multi-draw switching benchmark
// Renders N draw calls per frame, optionally switching programs between them.
// ============================================================================

static BenchResult runMultiDraw(EGLState& state,
                                 const std::vector<GLuint>& programs,
                                 const SceneUniforms& uniforms,
                                 GLuint vbo, GLuint ibo, int indexCount, GLuint texture,
                                 int numFrames, int drawsPerFrame, bool switchPrograms) {
    // Pre-setup: bind geometry (shared across all draws)
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    int stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, state.width, state.height);

    // Pre-setup uniforms for all programs
    for (GLuint prog : programs) {
        glUseProgram(prog);
        setupUniforms(prog, uniforms);
    }

    // Warmup
    glUseProgram(programs[0]);
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);
    int numProgs = (int)programs.size();

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (int d = 0; d < drawsPerFrame; d++) {
            if (switchPrograms) {
                // Cycle through programs: simulates different geometry needing different light counts
                glUseProgram(programs[d % numProgs]);
            }
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        }

        glFinish();
#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif
        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    return computeStats(frameTimes);
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

    printf("=== GLES2 Program Switch Benchmark ===\n");
    printf("Resolution: %dx%d, Frames: %d per test\n\n", width, height, numFrames);

    EGLState state;
    state.width = width;
    state.height = height;
    bool ok = false;
#ifdef EQT_HAS_DRM
    if (!forceX11) ok = initDRM(state);
#endif
    if (!ok && !forceDRM) ok = initX11(state);
    if (!ok) { fprintf(stderr, "Failed to initialize EGL\n"); return 1; }

    printf("GL: %s\n", glGetString(GL_RENDERER));
    printf("GL: %s\n\n", glGetString(GL_VERSION));

    // Build scene
    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    buildAlleyScene(verts, indices);
    printf("Scene: %d vertices, %d triangles\n\n", (int)verts.size(), (int)indices.size() / 3);

    GLuint vbo, ibo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);
    GLuint texture = createCheckerTexture(256);

    // Setup camera and uniforms
    float aspect = (float)state.width / state.height;
    SceneUniforms uniforms = {};
    {
        float proj[16], view[16], tmp[16];
        mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
        mat4LookAt(view, 0.0f, 1.6f, 1.0f,  0.0f, 1.6f, 10.0f,  0.0f, 1.0f, 0.0f);
        mat4Identity(uniforms.world);
        mat4Multiply(tmp, view, uniforms.world);
        mat4Multiply(uniforms.wvp, proj, tmp);
    }

    // Player light
    float radius = 52.0f;
    float quad = 19.0f / (radius * radius);
    uniforms.playerLightPos[0] = 0.0f; uniforms.playerLightPos[1] = 3.0f; uniforms.playerLightPos[2] = 1.0f;
    uniforms.playerLightColor[0] = 0.9f * 0.5f * 3.0f;
    uniforms.playerLightColor[1] = 0.7f * 0.5f * 3.0f;
    uniforms.playerLightColor[2] = 0.4f * 0.5f * 3.0f;
    uniforms.playerLightAtten[0] = 1.0f; uniforms.playerLightAtten[1] = 0.0f; uniforms.playerLightAtten[2] = quad;

    // Point lights at various positions along the alley (torches on walls)
    struct PointLight { float x, y, z, r, g, b, radius; };
    PointLight sceneLights[] = {
        { -2.5f, 4.0f,  3.0f,  1.0f, 0.7f, 0.3f, 40.0f },  // left wall torch near
        {  2.5f, 4.0f,  7.0f,  1.0f, 0.7f, 0.3f, 40.0f },  // right wall torch
        { -2.5f, 4.0f, 11.0f,  0.8f, 0.5f, 0.2f, 35.0f },  // left wall torch far
        {  2.5f, 4.0f, 15.0f,  0.8f, 0.5f, 0.2f, 35.0f },  // right wall torch far
        {  0.0f, 6.0f, 19.0f,  0.6f, 0.6f, 0.8f, 50.0f },  // ceiling light at back
        { -1.0f, 2.0f,  5.0f,  0.5f, 1.0f, 0.5f, 30.0f },  // green campfire
        {  1.0f, 2.0f, 13.0f,  1.0f, 0.3f, 0.3f, 25.0f },  // red brazier
    };

    // Light index 0 is reserved for player light (handled in FS).
    // Fill indices 1-7 with scene lights.
    uniforms.lightAtten[0] = 1.0f;  // index 0: placeholder constant=1
    for (int i = 0; i < 7; i++) {
        int idx = (i + 1) * 3;
        uniforms.lightPos[idx]   = sceneLights[i].x;
        uniforms.lightPos[idx+1] = sceneLights[i].y;
        uniforms.lightPos[idx+2] = sceneLights[i].z;
        uniforms.lightColor[idx]   = sceneLights[i].r;
        uniforms.lightColor[idx+1] = sceneLights[i].g;
        uniforms.lightColor[idx+2] = sceneLights[i].b;
        float q = 19.0f / (sceneLights[i].radius * sceneLights[i].radius);
        uniforms.lightAtten[idx]   = 1.0f;
        uniforms.lightAtten[idx+1] = 0.0f;
        uniforms.lightAtten[idx+2] = q;
    }

    // ====================================================================
    // Compile all programs
    // ====================================================================

    printf("Compiling shaders...\n");

    // Compile OPT C fragment shader (shared by all per-pixel VS variants)
    GLuint fsOptC = compileShader(GL_FRAGMENT_SHADER, FS_OPT_C);
    if (!fsOptC) { cleanup(state); return 1; }

    // Compile lightweight
    GLuint vsLW = compileShader(GL_VERTEX_SHADER, VS_LIGHTWEIGHT);
    GLuint fsLW = compileShader(GL_FRAGMENT_SHADER, FS_LIGHTWEIGHT);
    GLuint progLW = (vsLW && fsLW) ? linkProgram(vsLW, fsLW) : 0;
    if (!progLW) { cleanup(state); return 1; }

    // Compile per-pixel programs for 0-7 VS point lights (unrolled, no branching)
    // progPP[n] = per-pixel VS with n unrolled VS point lights + OPT C FS
    GLuint progPP[8] = {};
    GLuint vsPP[8] = {};
    for (int n = 0; n <= 7; n++) {
        std::string vsSrc = generateVS(n);
        vsPP[n] = compileShader(GL_VERTEX_SHADER, vsSrc.c_str());
        if (!vsPP[n]) { fprintf(stderr, "Failed to compile VS for %d lights\n", n); cleanup(state); return 1; }
        progPP[n] = linkProgram(vsPP[n], fsOptC);
        if (!progPP[n]) { fprintf(stderr, "Failed to link program for %d lights\n", n); cleanup(state); return 1; }
    }

    printf("  Compiled: 1 lightweight + 8 per-pixel (0-7 VS lights) = 9 programs\n\n");

    int idxCount = (int)indices.size();

    // ====================================================================
    // Phase 1: Individual programs — cost per light count
    // ====================================================================

    printf("============================================================\n");
    printf("PHASE 1: Per-light-count programs (OPT C FS, unrolled VS)\n");
    printf("============================================================\n\n");

    struct P1Result { int numLights; BenchResult r1, r2; };
    std::vector<P1Result> p1results;

    // Lightweight baseline
    {
        printf("--- Lightweight (baseline, looped 8 VS lights) ---\n");
        BenchResult r1 = runSingleProgram(state, progLW, vbo, ibo, idxCount, texture, numFrames, uniforms);
        printf("  R1: avg %.2f ms (~%.0f FPS)\n", r1.avgMs, 1000.0/r1.avgMs);
        BenchResult r2 = runSingleProgram(state, progLW, vbo, ibo, idxCount, texture, numFrames, uniforms);
        printf("  R2: avg %.2f ms (~%.0f FPS)\n\n", r2.avgMs, 1000.0/r2.avgMs);
        p1results.push_back({-1, r1, r2});
    }

    // Per-pixel programs 0-7
    for (int n = 0; n <= 7; n++) {
        printf("--- PP %d VS lights (unrolled) + OPT C FS ---\n", n);
        BenchResult r1 = runSingleProgram(state, progPP[n], vbo, ibo, idxCount, texture, numFrames, uniforms);
        printf("  R1: avg %.2f ms (~%.0f FPS)\n", r1.avgMs, 1000.0/r1.avgMs);
        BenchResult r2 = runSingleProgram(state, progPP[n], vbo, ibo, idxCount, texture, numFrames, uniforms);
        printf("  R2: avg %.2f ms (~%.0f FPS)\n\n", r2.avgMs, 1000.0/r2.avgMs);
        p1results.push_back({n, r1, r2});
    }

    // Phase 1 summary
    printf("--- PHASE 1 SUMMARY ---\n");
    printf("%-35s  %7s  %7s\n", "Variant", "avg ms", "FPS");
    printf("%-35s  %7s  %7s\n", "-------", "------", "---");
    for (auto& r : p1results) {
        double avg = (r.r1.avgMs + r.r2.avgMs) / 2.0;
        char name[64];
        if (r.numLights < 0)
            snprintf(name, sizeof(name), "Lightweight (8 VS lights, looped)");
        else
            snprintf(name, sizeof(name), "PP %d VS lights (unrolled) + OPT C", r.numLights);
        printf("%-35s  %7.2f  %7.0f\n", name, avg, 1000.0/avg);
    }
    printf("\n");

    // ====================================================================
    // Phase 2: Program switching cost
    // ====================================================================

    printf("============================================================\n");
    printf("PHASE 2: Program switching cost (multiple draws per frame)\n");
    printf("============================================================\n\n");

    // Test scenarios:
    // A) 10 draws/frame, same program (PP 0 lights) — baseline multi-draw
    // B) 10 draws/frame, switching every draw among 2 programs (PP 0, PP 3)
    // C) 10 draws/frame, switching every draw among 4 programs (PP 0,1,3,5)
    // D) 10 draws/frame, switching every draw among all 8 programs (PP 0-7)
    // E) 10 draws/frame, switching between PP and LW (simulates mixed scene)

    const int drawsPerFrame = 10;

    struct P2Test {
        const char* name;
        std::vector<GLuint> programs;
        bool switching;
    };

    std::vector<P2Test> p2tests = {
        {"10 draws, 1 program (PP0, no switch)",       {progPP[0]}, false},
        {"10 draws, 2 programs (PP0,PP3)",             {progPP[0], progPP[3]}, true},
        {"10 draws, 4 programs (PP0,PP1,PP3,PP5)",     {progPP[0], progPP[1], progPP[3], progPP[5]}, true},
        {"10 draws, 8 programs (PP0-PP7)",             {progPP[0], progPP[1], progPP[2], progPP[3],
                                                        progPP[4], progPP[5], progPP[6], progPP[7]}, true},
        {"10 draws, PP0+LW alternating",               {progPP[0], progLW}, true},
        {"10 draws, PP3+LW alternating",               {progPP[3], progLW}, true},
    };

    struct P2Result { const char* name; BenchResult r1, r2; };
    std::vector<P2Result> p2results;

    for (auto& test : p2tests) {
        printf("--- %s ---\n", test.name);
        BenchResult r1 = runMultiDraw(state, test.programs, uniforms, vbo, ibo, idxCount,
                                       texture, numFrames, drawsPerFrame, test.switching);
        printf("  R1: avg %.2f ms (~%.0f FPS)\n", r1.avgMs, 1000.0/r1.avgMs);
        BenchResult r2 = runMultiDraw(state, test.programs, uniforms, vbo, ibo, idxCount,
                                       texture, numFrames, drawsPerFrame, test.switching);
        printf("  R2: avg %.2f ms (~%.0f FPS)\n\n", r2.avgMs, 1000.0/r2.avgMs);
        p2results.push_back({test.name, r1, r2});
    }

    // Phase 2 summary
    printf("--- PHASE 2 SUMMARY ---\n");
    printf("%-45s  %7s  %7s  %8s\n", "Scenario", "avg ms", "FPS", "vs base");
    printf("%-45s  %7s  %7s  %8s\n", "--------", "------", "---", "-------");
    double p2base = (p2results[0].r1.avgMs + p2results[0].r2.avgMs) / 2.0;
    for (auto& r : p2results) {
        double avg = (r.r1.avgMs + r.r2.avgMs) / 2.0;
        printf("%-45s  %7.2f  %7.0f  %+7.2fms\n", r.name, avg, 1000.0/avg, avg - p2base);
    }
    printf("\n");

    // ====================================================================
    // Phase 3: Program count pressure
    // Does having many compiled programs slow down rendering even when
    // using only one of them?
    // ====================================================================

    printf("============================================================\n");
    printf("PHASE 3: Program count pressure\n");
    printf("============================================================\n\n");

    // We already have 9 programs compiled. Render with PP0 as baseline.
    printf("--- Baseline: PP0 with 9 programs compiled ---\n");
    BenchResult p3base = runSingleProgram(state, progPP[0], vbo, ibo, idxCount, texture, numFrames, uniforms);
    printf("  avg: %.2f ms (~%.0f FPS)\n\n", p3base.avgMs, 1000.0/p3base.avgMs);

    // Compile 9 more dummy programs (different constants to prevent dedup)
    printf("Compiling 9 additional dummy programs (total: 18)...\n");
    std::vector<GLuint> dummyProgs;
    std::vector<GLuint> dummyShaders;
    for (int i = 0; i < 9; i++) {
        // Generate slightly different FS with unique constant
        char fsSrc[1024];
        snprintf(fsSrc, sizeof(fsSrc),
            "precision mediump float;\n"
            "uniform sampler2D uTexture;\n"
            "uniform vec4 uFogColor;\n"
            "varying vec4 vColor;\n"
            "varying vec2 vTexCoord;\n"
            "varying float vFogFactor;\n"
            "void main() {\n"
            "    vec4 texColor = texture2D(uTexture, vTexCoord);\n"
            "    vec4 lit = texColor * vColor * %.4f;\n"
            "    gl_FragColor = mix(uFogColor, lit, vFogFactor);\n"
            "}\n",
            0.999f - i * 0.001f);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
        GLuint prog = fs ? linkProgram(vsLW, fs) : 0;
        if (prog) { dummyProgs.push_back(prog); dummyShaders.push_back(fs); }
    }
    printf("  Compiled %d dummy programs\n", (int)dummyProgs.size());

    printf("--- PP0 with 18 programs compiled ---\n");
    BenchResult p3_18 = runSingleProgram(state, progPP[0], vbo, ibo, idxCount, texture, numFrames, uniforms);
    printf("  avg: %.2f ms (~%.0f FPS)\n\n", p3_18.avgMs, 1000.0/p3_18.avgMs);

    // Compile 9 more (total 27)
    printf("Compiling 9 more dummy programs (total: 27)...\n");
    for (int i = 9; i < 18; i++) {
        char fsSrc[1024];
        snprintf(fsSrc, sizeof(fsSrc),
            "precision mediump float;\n"
            "uniform sampler2D uTexture;\n"
            "uniform vec4 uFogColor;\n"
            "varying vec4 vColor;\n"
            "varying vec2 vTexCoord;\n"
            "varying float vFogFactor;\n"
            "void main() {\n"
            "    vec4 texColor = texture2D(uTexture, vTexCoord);\n"
            "    vec4 lit = texColor * vColor * %.4f;\n"
            "    gl_FragColor = mix(uFogColor, lit, vFogFactor);\n"
            "}\n",
            0.989f - i * 0.001f);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
        GLuint prog = fs ? linkProgram(vsLW, fs) : 0;
        if (prog) { dummyProgs.push_back(prog); dummyShaders.push_back(fs); }
    }
    printf("  Total compiled: %d programs\n", 9 + (int)dummyProgs.size());

    printf("--- PP0 with 27 programs compiled ---\n");
    BenchResult p3_27 = runSingleProgram(state, progPP[0], vbo, ibo, idxCount, texture, numFrames, uniforms);
    printf("  avg: %.2f ms (~%.0f FPS)\n\n", p3_27.avgMs, 1000.0/p3_27.avgMs);

    printf("--- PHASE 3 SUMMARY ---\n");
    printf("%-30s  %7s  %7s  %8s\n", "Total programs compiled", "avg ms", "FPS", "vs base");
    printf("%-30s  %7s  %7s  %8s\n", "-----------------------", "------", "---", "-------");
    printf("%-30s  %7.2f  %7.0f  %+7.2fms\n", "9 programs", p3base.avgMs, 1000.0/p3base.avgMs, 0.0);
    printf("%-30s  %7.2f  %7.0f  %+7.2fms\n", "18 programs", p3_18.avgMs, 1000.0/p3_18.avgMs, p3_18.avgMs - p3base.avgMs);
    printf("%-30s  %7.2f  %7.0f  %+7.2fms\n", "27 programs", p3_27.avgMs, 1000.0/p3_27.avgMs, p3_27.avgMs - p3base.avgMs);
    printf("\n");

    // Cleanup
    for (GLuint p : dummyProgs) glDeleteProgram(p);
    for (GLuint s : dummyShaders) glDeleteShader(s);
    for (int n = 0; n <= 7; n++) { glDeleteProgram(progPP[n]); glDeleteShader(vsPP[n]); }
    glDeleteProgram(progLW);
    glDeleteShader(vsLW);
    glDeleteShader(fsLW);
    glDeleteShader(fsOptC);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ibo);

    cleanup(state);
    return 0;
}
