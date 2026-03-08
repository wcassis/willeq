// gles2_shader_perpixel_benchmark - Benchmark per-pixel vs per-vertex lighting on GLES2
//
// Creates a simple scene (textured quads simulating an alley) and renders it with:
//   1. Lightweight fragment shader (all lighting per-vertex, trivial FS)
//   2. Per-pixel fragment shader (player light computed per-fragment in FS)
//
// Reports frame times for each variant to isolate shader cost from the full renderer.
//
// Usage:
//   ./gles2_shader_perpixel_benchmark              # auto-detect (DRM first, then X11)
//   ./gles2_shader_perpixel_benchmark --drm        # force DRM/GBM
//   ./gles2_shader_perpixel_benchmark --x11        # force X11/EGL
//   ./gles2_shader_perpixel_benchmark --frames N   # frames per test (default 300)
//   ./gles2_shader_perpixel_benchmark --res W H    # resolution (default 1280 720)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
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
#include <xf86drmMode.h>
#include <gbm.h>
#endif

// ============================================================================
// EGL/DRM setup (reused from gles2_etc1_benchmark pattern)
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

    // Find connector and mode
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

        // Find matching mode
        for (int m = 0; m < conn->count_modes; m++) {
            if ((int)conn->modes[m].hdisplay == state.width &&
                (int)conn->modes[m].vdisplay == state.height) {
                state.mode = conn->modes[m];
                state.modeFound = true;
                break;
            }
        }
        if (!state.modeFound && conn->count_modes > 0) {
            // Fallback to first mode
            state.mode = conn->modes[0];
            state.width = state.mode.hdisplay;
            state.height = state.mode.vdisplay;
            state.modeFound = true;
        }

        // Find CRTC
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

// Page flip for DRM (presents to display)
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
// Shader sources — copied from zone_shader.cpp
// ============================================================================

// Shared vertex shader (per-pixel variant): lights 1-7 per-vertex, passes worldPos/worldNormal
static const char* VS_PERPIXEL = R"(
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
uniform int uNumPointLights;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vWorldPos = worldPos;
    vWorldNormal = worldN;

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));

    vec3 pointLighting = vec3(0.0);
    if (uNumPointLights > 1) {
        for (int i = 1; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            pointLighting += uLightColor[i] * nl * atten;
        }
    }

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Per-pixel fragment shader — BRANCHED (player light computed per-fragment, with if check)
static const char* FS_PERPIXEL_BRANCHED = R"(
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

    vec3 pLight = vec3(0.0);
    if (uPlayerLightColor.x + uPlayerLightColor.y + uPlayerLightColor.z > 0.0) {
        vec3 pLv = uPlayerLightPos - vWorldPos;
        float pLd = length(pLv) + 0.001;
        float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * pLd
                            + uPlayerLightAtten.z * pLd * pLd + 0.0001);
        float pLn = max(dot(normalize(vWorldNormal), pLv / pLd), 0.0);
        pLight = uPlayerLightColor * pLn * pLa;
    }

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Per-pixel fragment shader — BRANCHLESS (always computes player light)
// Current code: length() + normalize() + division + full attenuation
// Expensive ops: length (sqrt), normalize (inversesqrt+mul), pLv/pLd (div), 1.0/atten (div)
static const char* FS_PERPIXEL_BRANCHLESS = R"(
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
    float pLd = length(pLv) + 0.001;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * pLd
                        + uPlayerLightAtten.z * pLd * pLd + 0.0001);
    float pLn = max(dot(normalize(vWorldNormal), pLv / pLd), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT B: Single inversesqrt — one inversesqrt gives both d and normalized lightDir.
// Skip FS normalize of vWorldNormal (VS already normalized, interpolation keeps ~unit length).
// Saves: 1 sqrt (from length), 1 inversesqrt (from normalize), 1 division (pLv/pLd).
static const char* FS_OPT_INVERSESQRT = R"(
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
    float d = d2 * invD;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * d
                        + uPlayerLightAtten.z * d2 + 0.0001);
    float pLn = max(dot(vWorldNormal, pLv * invD), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT C: Quadratic-only attenuation — drop linear term, so no need for d (only d²).
// Still uses inversesqrt for NdotL light direction.
// Saves vs B: d = d2*invD multiply, one less multiply in attenuation.
static const char* FS_OPT_QUADRATIC = R"(
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

// OPT D: Omnidirectional — distance-only falloff, no NdotL.
// Still uses full attenuation (linear + quadratic) requiring inversesqrt for d.
// Looks like a point glow rather than directional lighting.
static const char* FS_OPT_OMNI = R"(
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
    float d = d2 * invD;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * d
                        + uPlayerLightAtten.z * d2 + 0.0001);
    vec3 pLight = uPlayerLightColor * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT E: Omni + quadratic-only — minimal math, no sqrt/inversesqrt at all.
// Just dot product for d², reciprocal for attenuation. Cheapest possible per-pixel.
static const char* FS_OPT_OMNI_QUAD = R"(
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
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.z * d2 + 0.0001);
    vec3 pLight = uPlayerLightColor * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// OPT F: Per-vertex player light — compute player light in VS, pass contribution as varying.
// 4 varyings (vColor, vTexCoord, vFogFactor, vPlayerLight) instead of 5.
// FS is trivial: just add vPlayerLight * texColor. Tests varying interpolation vs FS compute.
static const char* VS_PERVERTEX_PLIGHT = R"(
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
uniform int uNumPointLights;

uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform vec3 uPlayerLightAtten;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vPlayerLight;

void main() {
    vec4 pos = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 baseLighting = min(uAmbientColor + sunNdotL * uSunColor, vec3(1.0));

    // VS point lights 1-7
    vec3 pointLighting = vec3(0.0);
    if (uNumPointLights > 1) {
        for (int i = 1; i < 8; i++) {
            vec3 lVec = uLightPos[i] - worldPos;
            float d = length(lVec) + 0.001;
            float atten = 1.0 / (uLightAtten[i].x
                                + uLightAtten[i].y * d
                                + uLightAtten[i].z * d * d + 0.0001);
            float nl = max(dot(worldN, normalize(lVec)), 0.0);
            pointLighting += uLightColor[i] * nl * atten;
        }
    }

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);

    // Player light computed per-vertex, passed as varying
    vec3 pLv = uPlayerLightPos - worldPos;
    float pLd = length(pLv) + 0.001;
    float pLa = 1.0 / (uPlayerLightAtten.x + uPlayerLightAtten.y * pLd
                        + uPlayerLightAtten.z * pLd * pLd + 0.0001);
    float pLn = max(dot(worldN, pLv / pLd), 0.0);
    vPlayerLight = uPlayerLightColor * pLn * pLa;

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// OPT F fragment shader — trivial FS, just adds interpolated player light
static const char* FS_PERVERTEX_PLIGHT = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform vec4 uFogColor;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vPlayerLight;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = vec4(texColor.rgb * vColor.rgb + vPlayerLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// Lightweight vertex shader: all 8 lights per-vertex, no worldPos/worldNormal output
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
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);

    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// Lightweight fragment shader: trivial tex * vColor + fog
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

// ============================================================================
// Texture LUT variants — replace ALU (inversesqrt, reciprocal) with FP16
// texture lookups. LUT is a 256x1 GL_LUMINANCE_ALPHA + GL_HALF_FLOAT_OES
// texture where L = inversesqrt(d²), A = 1/(c + q*d²).
// ============================================================================

// LUT G: OPT C equivalent — directional NdotL + quadratic attenuation from LUT.
// Replaces inversesqrt(d²) and 1/(c+q*d²) with a single texture2D lookup.
// Same visual result as OPT C, but ALU → texture fetch.
static const char* FS_LUT_QUADRATIC = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAttenLUT;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));
    float invD = lut.r;
    float pLa = lut.a;
    float pLn = max(dot(vWorldNormal, pLv * invD), 0.0);
    vec3 pLight = uPlayerLightColor * pLn * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// LUT H: OPT E equivalent — omni + quadratic attenuation from LUT.
// Replaces 1/(c+q*d²) with a single texture2D lookup. No inversesqrt needed.
static const char* FS_LUT_OMNI_QUAD = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAttenLUT;
uniform vec4 uFogColor;
uniform vec3 uPlayerLightPos;
uniform vec3 uPlayerLightColor;
uniform float uLUTScale;

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;

void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);

    vec3 pLv = uPlayerLightPos - vWorldPos;
    float d2 = dot(pLv, pLv);
    float pLa = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5)).a;
    vec3 pLight = uPlayerLightColor * pLa;

    vec4 lit = vec4(texColor.rgb * vColor.rgb + pLight * texColor.rgb, texColor.a * vColor.a);
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// Scene geometry: an alley (3 walls + floor, ~100 polys with subdivisions)
// ============================================================================

struct Vertex {
    float pos[3];
    float normal[3];
    float color[4];
    float uv[2];
};

// Build a subdivided quad (for more realistic per-vertex lighting interpolation)
// Returns vertices and indices for a grid of triangles.
static void buildSubdividedQuad(
    float x0, float y0, float z0,   // corner 0
    float x1, float y1, float z1,   // corner 1 (along U)
    float x2, float y2, float z2,   // corner 2 (along V, opposite corner)
    float nx, float ny, float nz,   // face normal
    int divisionsU, int divisionsV,
    std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    // Corner 3 = corner1 + (corner2 - corner0)
    float x3 = x1 + (x2 - x0);
    float y3 = y1 + (y2 - y0);
    float z3 = z1 + (z2 - z0);

    uint16_t baseIdx = (uint16_t)verts.size();

    for (int v = 0; v <= divisionsV; v++) {
        float tv = (float)v / divisionsV;
        for (int u = 0; u <= divisionsU; u++) {
            float tu = (float)u / divisionsU;

            // Bilinear interpolation
            float px = (1-tu)*(1-tv)*x0 + tu*(1-tv)*x1 + (1-tu)*tv*x2 + tu*tv*x3;
            float py = (1-tu)*(1-tv)*y0 + tu*(1-tv)*y1 + (1-tu)*tv*y2 + tu*tv*y3;
            float pz = (1-tu)*(1-tv)*z0 + tu*(1-tv)*z1 + (1-tu)*tv*z2 + tu*tv*z3;

            Vertex vert;
            vert.pos[0] = px; vert.pos[1] = py; vert.pos[2] = pz;
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
    // Alley dimensions: 6 wide, 20 deep, 8 tall (Irrlicht Y-up)
    // Player stands at origin looking down +Z
    float w = 3.0f, d = 20.0f, h = 8.0f;
    int div = 4;  // subdivisions per wall

    // Floor (Y=0, normal up)
    buildSubdividedQuad(-w, 0, 0,  w, 0, 0,  -w, 0, d,  0, 1, 0, div, div*2, verts, indices);

    // Left wall (X=-w, normal +X)
    buildSubdividedQuad(-w, 0, 0,  -w, 0, d,  -w, h, 0,  1, 0, 0, div*2, div, verts, indices);

    // Right wall (X=+w, normal -X)
    buildSubdividedQuad(w, 0, d,  w, 0, 0,  w, h, d,  -1, 0, 0, div*2, div, verts, indices);

    // Back wall (Z=d, normal -Z)
    buildSubdividedQuad(w, 0, d,  -w, 0, d,  w, h, d,  0, 0, -1, div, div, verts, indices);
}

// ============================================================================
// Simple checkerboard texture
// ============================================================================

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

// ============================================================================
// FP16 conversion and attenuation LUT creation
// ============================================================================

static uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint16_t sign = (x >> 16) & 0x8000;
    int exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint16_t frac = (x >> 13) & 0x03FF;
    if (exp <= 0) return sign;       // underflow to zero
    if (exp >= 31) return sign | 0x7C00;  // overflow to inf
    return sign | (exp << 10) | frac;
}

// Check if GL_OES_texture_half_float and GL_OES_texture_half_float_linear are available
static bool hasHalfFloatTextures() {
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    if (!ext) return false;
    return strstr(ext, "GL_OES_texture_half_float") != nullptr &&
           strstr(ext, "GL_OES_texture_half_float_linear") != nullptr;
}

// Create a 256x1 GL_LUMINANCE_ALPHA FP16 attenuation LUT.
// L channel = inversesqrt(d² + epsilon)
// A channel = 1.0 / (attenConst + attenQuad * d² + epsilon)
// d² is mapped to [0, maxD2] across the 256 texels.
static GLuint createAttenLUT(float attenConst, float attenQuad, float maxD2) {
    const int LUT_SIZE = 256;
    // Each texel is 2 x FP16 = 4 bytes (luminance + alpha)
    uint16_t data[LUT_SIZE * 2];

    for (int i = 0; i < LUT_SIZE; i++) {
        float t = (float)i / (float)(LUT_SIZE - 1);
        float d2 = t * maxD2;
        float d2e = d2 + 0.0001f;  // epsilon to match shader

        float invD = 1.0f / sqrtf(d2e);
        float atten = 1.0f / (attenConst + attenQuad * d2e + 0.0001f);

        data[i * 2 + 0] = floatToHalf(invD);    // luminance
        data[i * 2 + 1] = floatToHalf(atten);   // alpha
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, LUT_SIZE, 1, 0,
                 GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "LUT creation failed (GL error 0x%x)\n", err);
        glDeleteTextures(1, &tex);
        return 0;
    }

    return tex;
}

// ============================================================================
// Simple 4x4 identity/projection matrices
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
// Benchmark runner
// ============================================================================

struct BenchResult {
    double avgMs;
    double minMs;
    double maxMs;
};

static BenchResult runBenchmark(EGLState& state, GLuint program, GLuint vbo, GLuint ibo,
                                 int indexCount, GLuint texture, int numFrames,
                                 float aspect, bool isPerPixel, int numPointLights = 2,
                                 GLuint lutTexture = 0, float lutScale = 0.0f) {
    glUseProgram(program);

    // Camera: standing in the alley looking down it
    float proj[16], view[16], world[16], wvp[16], tmp[16];
    mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
    mat4LookAt(view, 0.0f, 1.6f, 1.0f,  0.0f, 1.6f, 10.0f,  0.0f, 1.0f, 0.0f);
    mat4Identity(world);
    mat4Multiply(tmp, view, world);  // view * world
    mat4Multiply(wvp, proj, tmp);    // proj * view * world

    // Set uniforms
    GLint locWVP = glGetUniformLocation(program, "mWorldViewProj");
    GLint locWorld = glGetUniformLocation(program, "mWorld");
    GLint locSunDir = glGetUniformLocation(program, "uSunDir");
    GLint locSunColor = glGetUniformLocation(program, "uSunColor");
    GLint locAmbient = glGetUniformLocation(program, "uAmbientColor");
    GLint locTint = glGetUniformLocation(program, "uTintColor");
    GLint locFogStart = glGetUniformLocation(program, "uFogStart");
    GLint locFogEnd = glGetUniformLocation(program, "uFogEnd");
    GLint locFogColor = glGetUniformLocation(program, "uFogColor");
    GLint locTexture = glGetUniformLocation(program, "uTexture");
    GLint locLightPos = glGetUniformLocation(program, "uLightPos[0]");
    GLint locLightColor = glGetUniformLocation(program, "uLightColor[0]");
    GLint locLightAtten = glGetUniformLocation(program, "uLightAtten[0]");

    if (locWVP >= 0) glUniformMatrix4fv(locWVP, 1, GL_FALSE, wvp);
    if (locWorld >= 0) glUniformMatrix4fv(locWorld, 1, GL_FALSE, world);

    // Night-time lighting: dim ambient, no sun, dark tint
    float sunDir[] = {0.0f, -1.0f, 0.0f};
    float sunColor[] = {0.0f, 0.0f, 0.0f};
    float ambient[] = {0.05f, 0.05f, 0.08f};
    float tint[] = {0.3f, 0.3f, 0.4f};
    if (locSunDir >= 0) glUniform3fv(locSunDir, 1, sunDir);
    if (locSunColor >= 0) glUniform3fv(locSunColor, 1, sunColor);
    if (locAmbient >= 0) glUniform3fv(locAmbient, 1, ambient);
    if (locTint >= 0) glUniform3fv(locTint, 1, tint);
    if (locFogStart >= 0) glUniform1f(locFogStart, 50.0f);
    if (locFogEnd >= 0) glUniform1f(locFogEnd, 200.0f);
    float fogColor[] = {0.1f, 0.1f, 0.15f, 1.0f};
    if (locFogColor >= 0) glUniform4fv(locFogColor, 1, fogColor);
    if (locTexture >= 0) glUniform1i(locTexture, 0);

    // Point lights: player light at index 0, torch at index 1
    float lightPos[8*3] = {};
    float lightColor[8*3] = {};
    float lightAtten[8*3] = {};
    // Light 0: player light (above player)
    lightPos[0] = 0.0f; lightPos[1] = 3.0f; lightPos[2] = 1.0f;
    lightColor[0] = 0.9f * 0.5f * 3.0f; lightColor[1] = 0.7f * 0.5f * 3.0f; lightColor[2] = 0.4f * 0.5f * 3.0f;
    lightAtten[0] = 1.0f; lightAtten[1] = 0.0f; lightAtten[2] = 19.0f / (52.0f * 52.0f);
    // Light 1: torch behind player
    lightPos[3] = 0.0f; lightPos[4] = 4.0f; lightPos[5] = -2.0f;
    lightColor[3] = 1.0f; lightColor[4] = 0.7f; lightColor[5] = 0.3f;
    lightAtten[3] = 1.0f; lightAtten[4] = 0.0f; lightAtten[5] = 19.0f / (40.0f * 40.0f);
    // Remaining lights: zero (attConstant=1 to avoid division issues)
    for (int i = 2; i < 8; i++) lightAtten[i*3] = 1.0f;

    if (locLightPos >= 0) glUniform3fv(locLightPos, 8, lightPos);
    if (locLightColor >= 0) glUniform3fv(locLightColor, 8, lightColor);
    if (locLightAtten >= 0) glUniform3fv(locLightAtten, 8, lightAtten);

    // Per-pixel variant: numPointLights and player light FS uniforms
    if (isPerPixel) {
        GLint locNumLights = glGetUniformLocation(program, "uNumPointLights");
        GLint locPLPos = glGetUniformLocation(program, "uPlayerLightPos");
        GLint locPLColor = glGetUniformLocation(program, "uPlayerLightColor");
        GLint locPLAtten = glGetUniformLocation(program, "uPlayerLightAtten");
        if (locNumLights >= 0) glUniform1i(locNumLights, numPointLights);
        if (locPLPos >= 0) glUniform3fv(locPLPos, 1, &lightPos[0]);
        if (locPLColor >= 0) glUniform3fv(locPLColor, 1, &lightColor[0]);
        if (locPLAtten >= 0) glUniform3fv(locPLAtten, 1, &lightAtten[0]);
    }

    // Bind LUT texture to unit 1 if available
    if (lutTexture) {
        GLint locAttenLUT = glGetUniformLocation(program, "uAttenLUT");
        GLint locLUTScale = glGetUniformLocation(program, "uLUTScale");
        if (locAttenLUT >= 0) glUniform1i(locAttenLUT, 1);
        if (locLUTScale >= 0) glUniform1f(locLUTScale, lutScale);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lutTexture);
        glActiveTexture(GL_TEXTURE0);
    }

    // Bind geometry
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
    glDepthFunc(GL_LESS);
    glViewport(0, 0, state.width, state.height);

    // Warmup frames (5)
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
        glFinish();
    }

    // Timed frames
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
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        frameTimes.push_back(ms);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);

    // Compute stats
    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) {
        sum += t;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
    }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
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

    printf("=== GLES2 Per-Pixel Lighting Benchmark ===\n");
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

    // Build scene
    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    buildAlleyScene(verts, indices);
    int polyCount = (int)indices.size() / 3;
    printf("Scene: %d vertices, %d triangles (4 walls+floor, subdivided)\n\n",
           (int)verts.size(), polyCount);

    // Upload geometry
    GLuint vbo, ibo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    GLuint texture = createCheckerTexture(256);

    // Create attenuation LUT (FP16) if half-float textures are supported
    // Uses same attenuation params as player light: constant=1.0, quadratic=19/(52²)
    float attenConst = 1.0f;
    float attenQuad = 19.0f / (52.0f * 52.0f);
    float lutMaxD2 = 4096.0f;  // 64 units range (covers well beyond light radius)
    float lutScale = 1.0f / lutMaxD2;
    GLuint lutTexture = 0;
    bool hasLUT = false;

    if (hasHalfFloatTextures()) {
        lutTexture = createAttenLUT(attenConst, attenQuad, lutMaxD2);
        if (lutTexture) {
            hasLUT = true;
            printf("LUT: Created 256x1 FP16 attenuation LUT (atten=%.1f+%.6f*d², maxD²=%.0f)\n",
                   attenConst, attenQuad, lutMaxD2);
        } else {
            printf("LUT: FP16 texture creation failed, LUT tests skipped\n");
        }
    } else {
        printf("LUT: GL_OES_texture_half_float/_linear not available, LUT tests skipped\n");
    }

    // Compile shaders
    printf("Compiling shaders...\n");

    GLuint vsPerPixel = compileShader(GL_VERTEX_SHADER, VS_PERPIXEL);

    GLuint fsBranched = compileShader(GL_FRAGMENT_SHADER, FS_PERPIXEL_BRANCHED);
    GLuint progBranched = (vsPerPixel && fsBranched) ? linkProgram(vsPerPixel, fsBranched) : 0;

    GLuint fsBranchless = compileShader(GL_FRAGMENT_SHADER, FS_PERPIXEL_BRANCHLESS);
    GLuint progBranchless = (vsPerPixel && fsBranchless) ? linkProgram(vsPerPixel, fsBranchless) : 0;

    GLuint fsOptInvSqrt = compileShader(GL_FRAGMENT_SHADER, FS_OPT_INVERSESQRT);
    GLuint progOptInvSqrt = (vsPerPixel && fsOptInvSqrt) ? linkProgram(vsPerPixel, fsOptInvSqrt) : 0;

    GLuint fsOptQuad = compileShader(GL_FRAGMENT_SHADER, FS_OPT_QUADRATIC);
    GLuint progOptQuad = (vsPerPixel && fsOptQuad) ? linkProgram(vsPerPixel, fsOptQuad) : 0;

    GLuint fsOptOmni = compileShader(GL_FRAGMENT_SHADER, FS_OPT_OMNI);
    GLuint progOptOmni = (vsPerPixel && fsOptOmni) ? linkProgram(vsPerPixel, fsOptOmni) : 0;

    GLuint fsOptOmniQuad = compileShader(GL_FRAGMENT_SHADER, FS_OPT_OMNI_QUAD);
    GLuint progOptOmniQuad = (vsPerPixel && fsOptOmniQuad) ? linkProgram(vsPerPixel, fsOptOmniQuad) : 0;

    GLuint vsPerVertexPL = compileShader(GL_VERTEX_SHADER, VS_PERVERTEX_PLIGHT);
    GLuint fsPerVertexPL = compileShader(GL_FRAGMENT_SHADER, FS_PERVERTEX_PLIGHT);
    GLuint progPerVertexPL = (vsPerVertexPL && fsPerVertexPL) ? linkProgram(vsPerVertexPL, fsPerVertexPL) : 0;

    GLuint vsLightweight = compileShader(GL_VERTEX_SHADER, VS_LIGHTWEIGHT);
    GLuint fsLightweight = compileShader(GL_FRAGMENT_SHADER, FS_LIGHTWEIGHT);
    GLuint progLightweight = (vsLightweight && fsLightweight) ? linkProgram(vsLightweight, fsLightweight) : 0;

    // LUT shader variants (only if LUT texture was created successfully)
    GLuint fsLUTQuad = 0, progLUTQuad = 0;
    GLuint fsLUTOmniQuad = 0, progLUTOmniQuad = 0;
    if (hasLUT) {
        fsLUTQuad = compileShader(GL_FRAGMENT_SHADER, FS_LUT_QUADRATIC);
        progLUTQuad = (vsPerPixel && fsLUTQuad) ? linkProgram(vsPerPixel, fsLUTQuad) : 0;

        fsLUTOmniQuad = compileShader(GL_FRAGMENT_SHADER, FS_LUT_OMNI_QUAD);
        progLUTOmniQuad = (vsPerPixel && fsLUTOmniQuad) ? linkProgram(vsPerPixel, fsLUTOmniQuad) : 0;
    }

    if (!progBranched || !progBranchless || !progOptInvSqrt || !progOptQuad ||
        !progOptOmni || !progOptOmniQuad || !progPerVertexPL || !progLightweight) {
        fprintf(stderr, "Shader compilation failed\n");
        cleanup(state);
        return 1;
    }

    int progCount = 8;
    if (progLUTQuad) progCount++;
    if (progLUTOmniQuad) progCount++;
    printf("  Programs compiled: %d%s\n\n", progCount,
           hasLUT ? " (including LUT variants)" : "");

    float aspect = (float)state.width / state.height;

    // Helper struct for test results
    struct TestResult {
        const char* name;
        BenchResult r1, r2;
        double avg() const { return (r1.avgMs + r2.avgMs) / 2.0; }
    };

    // All tests: run each variant twice for order-bias checking
    // All per-pixel tests use numPointLights=1 (player light only) to isolate FS cost
    struct TestDef {
        const char* name;
        GLuint program;
        bool isPerPixel;
        int numLights;
        GLuint lutTexture;
        float lutScale;
    };
    std::vector<TestDef> tests = {
        {"LIGHTWEIGHT (baseline)",                      progLightweight,  false, 0, 0, 0.0f},
        {"PP BRANCHED (original, if check)",            progBranched,     true,  1, 0, 0.0f},
        {"PP BRANCHLESS (current, no branch)",          progBranchless,   true,  1, 0, 0.0f},
        {"OPT B: inversesqrt (skip normalize, 1 isqrt)", progOptInvSqrt, true,  1, 0, 0.0f},
        {"OPT C: quadratic (isqrt + quad-only atten)",  progOptQuad,     true,  1, 0, 0.0f},
        {"OPT D: omni (distance-only, full atten)",     progOptOmni,     true,  1, 0, 0.0f},
        {"OPT E: omni+quad (no sqrt at all)",           progOptOmniQuad, true,  1, 0, 0.0f},
        {"OPT F: per-vertex plight (trivial FS)",       progPerVertexPL, true,  1, 0, 0.0f},
    };
    if (progLUTQuad) {
        tests.push_back({"LUT G: OPT C via FP16 LUT (isqrt+atten)", progLUTQuad, true, 1, lutTexture, lutScale});
    }
    if (progLUTOmniQuad) {
        tests.push_back({"LUT H: OPT E via FP16 LUT (atten only)",  progLUTOmniQuad, true, 1, lutTexture, lutScale});
    }
    const int numTests = (int)tests.size();

    std::vector<TestResult> results(numTests);

    // Round 1
    printf("=== ROUND 1 ===\n\n");
    for (int i = 0; i < numTests; i++) {
        printf("--- %s ---\n", tests[i].name);
        results[i].name = tests[i].name;
        results[i].r1 = runBenchmark(state, tests[i].program, vbo, ibo,
                                      (int)indices.size(), texture, numFrames,
                                      aspect, tests[i].isPerPixel, tests[i].numLights,
                                      tests[i].lutTexture, tests[i].lutScale);
        printf("  avg: %.2f ms  min: %.2f ms  max: %.2f ms  (~%.0f FPS)\n\n",
               results[i].r1.avgMs, results[i].r1.minMs, results[i].r1.maxMs,
               1000.0 / results[i].r1.avgMs);
    }

    // Round 2
    printf("=== ROUND 2 (order bias check) ===\n\n");
    for (int i = 0; i < numTests; i++) {
        printf("--- %s ---\n", tests[i].name);
        results[i].r2 = runBenchmark(state, tests[i].program, vbo, ibo,
                                      (int)indices.size(), texture, numFrames,
                                      aspect, tests[i].isPerPixel, tests[i].numLights,
                                      tests[i].lutTexture, tests[i].lutScale);
        printf("  avg: %.2f ms  min: %.2f ms  max: %.2f ms  (~%.0f FPS)\n\n",
               results[i].r2.avgMs, results[i].r2.minMs, results[i].r2.maxMs,
               1000.0 / results[i].r2.avgMs);
    }

    // Summary
    double lwAvg = results[0].avg();
    printf("=== SUMMARY ===\n");
    printf("%-45s  %7s  %7s  %8s\n", "Variant", "ms", "FPS", "vs LW");
    printf("%-45s  %7s  %7s  %8s\n", "-------", "--", "---", "-----");
    for (int i = 0; i < numTests; i++) {
        double avg = results[i].avg();
        double diff = avg - lwAvg;
        printf("%-45s  %7.2f  %7.0f  %+7.2fms\n",
               results[i].name, avg, 1000.0 / avg, diff);
    }

    // Cleanup
    glDeleteProgram(progBranched);
    glDeleteProgram(progBranchless);
    glDeleteProgram(progOptInvSqrt);
    glDeleteProgram(progOptQuad);
    glDeleteProgram(progOptOmni);
    glDeleteProgram(progOptOmniQuad);
    glDeleteProgram(progPerVertexPL);
    glDeleteProgram(progLightweight);
    if (progLUTQuad) glDeleteProgram(progLUTQuad);
    if (progLUTOmniQuad) glDeleteProgram(progLUTOmniQuad);
    glDeleteShader(vsPerPixel);
    glDeleteShader(fsBranched);
    glDeleteShader(fsBranchless);
    glDeleteShader(fsOptInvSqrt);
    glDeleteShader(fsOptQuad);
    glDeleteShader(fsOptOmni);
    glDeleteShader(fsOptOmniQuad);
    glDeleteShader(vsPerVertexPL);
    glDeleteShader(fsPerVertexPL);
    glDeleteShader(vsLightweight);
    glDeleteShader(fsLightweight);
    if (fsLUTQuad) glDeleteShader(fsLUTQuad);
    if (fsLUTOmniQuad) glDeleteShader(fsLUTOmniQuad);
    glDeleteTextures(1, &texture);
    if (lutTexture) glDeleteTextures(1, &lutTexture);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ibo);

    cleanup(state);
    return 0;
}
