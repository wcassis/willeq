// gles2_fog_volume_benchmark - Benchmark analytic fog volume rendering on GLES2
//
// Tests the cost of rendering volumetric fog spheres using analytic density
// integration (Beer-Lambert transmittance) on Mali 400 hardware.
//
// Renders the same alley scene as the perpixel benchmark, then overlays
// alpha-blended fog volume spheres at light positions. Measures the cost
// of the fog pass in isolation and combined with the zone lighting pass.
//
// Density function variants (from "Analytic Fog Rendering With Volumetric
// Primitives" by matejlou):
//   - Quadratic:  (1 - r^2)^1  -- polynomial only, cheapest
//   - Quartic:    (1 - r^2)^2  -- degree-5 polynomial, smooth edges
//   - Linear:     (1 - r)      -- requires sqrt + log in antiderivative
//   - Gaussian:   exp(-r^2)    -- requires exp + erf approximation
//
// Each variant is tested with 1, 2, 4, and 8 fog spheres at two radii
// (small=3, large=6) to measure how overdraw scales.
//
// Usage:
//   ./gles2_fog_volume_benchmark              # auto-detect (DRM first, then X11)
//   ./gles2_fog_volume_benchmark --drm        # force DRM/GBM
//   ./gles2_fog_volume_benchmark --x11        # force X11/EGL
//   ./gles2_fog_volume_benchmark --frames N   # frames per test (default 300)
//   ./gles2_fog_volume_benchmark --res W H    # resolution (default 1280 720)

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
// EGL/DRM setup (same pattern as perpixel benchmark)
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
// Zone lighting shaders (same as perpixel benchmark — lightweight baseline)
// ============================================================================

static const char* VS_ZONE = R"(
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

static const char* FS_ZONE = R"(
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
// Fog volume shaders
//
// Fog volumes are rendered as back-face billboards (camera-facing quads).
// The VS passes the ray origin and direction to the FS, which computes
// ray-sphere intersection and evaluates the analytic density integral.
//
// The fog volume VS is shared across all density variants. Each density
// function has its own FS to avoid branches (Mali 400 FS branches are
// extremely expensive).
// ============================================================================

// Fog volume vertex shader
// Renders a screen-aligned quad for each fog sphere. The quad is sized
// to cover the sphere's projected extent. The FS computes ray-sphere
// intersection per fragment.
static const char* VS_FOG = R"(
precision highp float;

attribute vec3 aPosition;  // quad corners: (-1,-1), (1,-1), (-1,1), (1,1)

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform highp vec3 uSphereCenter;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;   // camera position in sphere-local space
varying highp vec3 vRayDir;      // ray direction (not normalized — FS normalizes)

void main() {
    // Billboard quad corners in world space, sized to sphere radius * sqrt(2)
    // to ensure full sphere coverage from any view angle
    float extent = uSphereRadius * 1.5;
    vec3 worldPos = uSphereCenter
                  + uCameraRight * aPosition.x * extent
                  + uCameraUp * aPosition.y * extent;

    gl_Position = uViewProj * vec4(worldPos, 1.0);

    // Transform to sphere-local space (centered at origin, unit radius)
    // so density functions work in normalized [0,1] radius
    vRayOrigin = (uCameraPos - uSphereCenter) / uSphereRadius;
    vRayDir = (worldPos - uCameraPos) / uSphereRadius;
}
)";

// Ray-sphere intersection helper (shared preamble for all fog FS variants).
// Returns false if the ray misses the unit sphere. Sets tNear/tFar to the
// entry/exit distances along the ray.
//
// Inlined as a string prefix for each FS variant to avoid GLSL function
// call overhead (Mali 400 may not inline functions efficiently).
static const char* FS_FOG_PREAMBLE = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;    // overall density multiplier
uniform float uSphereRadius;  // for depth comparison

varying vec3 vRayOrigin;
varying vec3 vRayDir;
)";

// --- QUADRATIC density: f(r) = (1 - r^2), cheapest ---
// Cross-section: g(t) = 1 - (a*t^2 + b*t + c) where a=dot(d,d), b=2*dot(o,d), c=dot(o,o)
// Antiderivative: F(t) = t - (a*t^3/3 + b*t^2/2 + c*t) = t*(1 - c - b*t/2 - a*t^2/3)
// Wait — expanding: g(t) = 1 - a*t^2 - b*t - c
// F(t) = (1-c)*t - b*t^2/2 - a*t^3/3
static const char* FS_FOG_QUADRATIC = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    // Ray-sphere intersection (unit sphere)
    float a_i = dot(rd, rd);  // = 1.0 for normalized rd
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    // Miss — discard (no fog contribution)
    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    // Shift ray origin to tNear for numerical precision
    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    // Analytic integral of (1 - r^2) along the ray segment [0, L]
    // where r^2 = dot(o + rd*t, o + rd*t) = a*t^2 + b*t + c
    float a = dot(rd, rd);     // 1.0
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // Antiderivative F(t) = (1-c)*t - b*t^2/2 - a*t^3/3
    // Integral = F(L) - F(0) = F(L)
    float integral = L * ((1.0 - c) + L * (-b * 0.5 + L * (-a / 3.0)));

    // Beer-Lambert transmittance
    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// --- QUARTIC density: f(r) = (1 - r^2)^2, smooth edges ---
// Cross-section: g(t) = (1 - (a*t^2 + b*t + c))^2
// Let h(t) = 1 - a*t^2 - b*t - c, then g(t) = h(t)^2
// h(t)^2 = (1-c)^2 - 2(1-c)*b*t + (b^2 - 2(1-c)*a)*t^2 + 2*a*b*t^3 + a^2*t^4
// Antiderivative is a degree-5 polynomial
static const char* FS_FOG_QUARTIC = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // h(t) = (1-c) - b*t - a*t^2
    // g(t) = h(t)^2 -- expand and integrate term by term
    float h0 = 1.0 - c;  // h(0)
    // Coefficients of h(t)^2 = h0^2 + (-2*h0*b)*t + (b^2 - 2*h0*a)*t^2 + (2*a*b)*t^3 + a^2*t^4
    float c0 = h0 * h0;
    float c1 = -2.0 * h0 * b;
    float c2 = b * b - 2.0 * h0 * a;
    float c3 = 2.0 * a * b;
    float c4 = a * a;

    // F(L) = c0*L + c1*L^2/2 + c2*L^3/3 + c3*L^4/4 + c4*L^5/5
    float integral = L * (c0 + L * (c1 * 0.5 + L * (c2 / 3.0 + L * (c3 * 0.25 + L * c4 * 0.2))));

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// --- LINEAR density: f(r) = 1 - r, requires sqrt ---
// r = sqrt(a*t^2 + b*t + c), so g(t) = 1 - sqrt(a*t^2 + b*t + c)
// Antiderivative of sqrt(quadratic) involves sqrt + log (see article).
// This is the expensive variant — tests whether sqrt+log cost is acceptable.
static const char* FS_FOG_LINEAR = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // Integral of (1 - sqrt(a*t^2 + b*t + c)) dt from 0 to L
    // = L - integral of sqrt(a*t^2 + b*t + c) dt from 0 to L
    //
    // Antiderivative of sqrt(a*t^2 + b*t + c):
    //   Let u = t + b/(2a), v = (b^2 - 4ac) / (4a^2)  (note: negative inside sphere)
    //   But we use v = (c - b^2/(4a)) / a = c/a - b^2/(4a^2) ... actually:
    //   sqrt(a*(t^2 + b/a*t + c/a)) = sqrt(a) * sqrt((t+b/2a)^2 + (c/a - b^2/4a^2))
    //
    // For numerical stability, evaluate at both endpoints:
    float sqrtA = sqrt(a);  // = 1.0 for normalized rd
    float bOverA = b / a;
    float v = c / a - bOverA * bOverA * 0.25;  // (4ac - b^2) / (4a^2), negative

    // F(t) = (u * sqrt(u^2 + v) + v * log(u + sqrt(u^2 + v))) / 2
    // where u = t + b/(2a)
    float u0 = bOverA * 0.5;
    float u1 = L + bOverA * 0.5;

    float r0sq = u0 * u0 + v;
    float r1sq = u1 * u1 + v;
    float r0 = sqrt(max(r0sq, 0.0));
    float r1 = sqrt(max(r1sq, 0.0));

    float F0 = u0 * r0 + v * log(max(u0 + r0, 0.001));
    float F1 = u1 * r1 + v * log(max(u1 + r1, 0.001));

    float sqrtIntegral = sqrtA * (F1 - F0) * 0.5;
    float integral = L - sqrtIntegral;

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// --- GAUSSIAN density: f(r) = exp(-r^2), requires exp + erf ---
// Cross-section: g(t) = exp(-(a*t^2 + b*t + c))
// Antiderivative involves the error function (erf).
static const char* FS_FOG_GAUSSIAN = R"(
precision mediump float;

uniform vec3 uFogColor;
uniform float uFogDensity;
uniform highp float uSphereRadius;

varying highp vec3 vRayOrigin;
varying highp vec3 vRayDir;

// Polynomial erf approximation (Abramowitz & Stegun, max error ~5e-4)
float erf_approx(float z) {
    float az = abs(z);
    float t = 1.0 / (1.0 + az * (0.278393 + az * (0.230389 + az * (0.000972 + az * 0.078108))));
    float r = 1.0 - t * t * t * t;  // 1 - (1/(1+p))^4
    return sign(z) * r;
}

void main() {
    vec3 rd = normalize(vRayDir);

    float a_i = dot(rd, rd);
    float b_i = 2.0 * dot(vRayOrigin, rd);
    float c_i = dot(vRayOrigin, vRayOrigin) - 1.0;
    float disc = b_i * b_i - 4.0 * a_i * c_i;

    // For Gaussian we still clip to the unit sphere to avoid computing
    // the infinite tail (which is negligible at r=1 where exp(-1)=0.37)
    if (disc < 0.0) discard;

    float sqrtDisc = sqrt(disc);
    float tNear = max((-b_i - sqrtDisc) / (2.0 * a_i), 0.0);
    float tFar = (-b_i + sqrtDisc) / (2.0 * a_i);
    if (tFar < 0.0) discard;

    vec3 o = vRayOrigin + rd * tNear;
    float L = tFar - tNear;

    float a = dot(rd, rd);
    float b = 2.0 * dot(o, rd);
    float c = dot(o, o);

    // Integral of exp(-(a*t^2 + b*t + c)) from 0 to L
    // Complete the square: a*t^2 + b*t + c = a*(t + b/2a)^2 + (c - b^2/4a)
    // = a*(t + b/2a)^2 + k  where k = c - b^2/(4a)
    float sqrtA = sqrt(a);
    float k = c - b * b / (4.0 * a);

    // Integral = sqrt(pi) * exp(-k) / (2*sqrt(a)) * (erf(sqrt(a)*(L+b/2a)) - erf(sqrt(a)*b/2a))
    float bOver2a = b / (2.0 * a);
    float erf0 = erf_approx(sqrtA * bOver2a);
    float erf1 = erf_approx(sqrtA * (L + bOver2a));

    float integral = 1.7724539 * exp(-k) * (erf1 - erf0) / (2.0 * sqrtA);  // sqrt(pi) = 1.7724539

    float density = uFogDensity * uSphereRadius * max(integral, 0.0);
    float transmittance = exp(-density);
    float alpha = 1.0 - transmittance;

    gl_FragColor = vec4(uFogColor * alpha, alpha);
}
)";

// ============================================================================
// Scene geometry (same alley as perpixel benchmark)
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
    float x3 = x1 + (x2 - x0);
    float y3 = y1 + (y2 - y0);
    float z3 = z1 + (z2 - z0);

    uint16_t baseIdx = (uint16_t)verts.size();

    for (int v = 0; v <= divisionsV; v++) {
        float tv = (float)v / divisionsV;
        for (int u = 0; u <= divisionsU; u++) {
            float tu = (float)u / divisionsU;

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
    float w = 3.0f, d = 20.0f, h = 8.0f;
    int div = 4;

    // Floor
    buildSubdividedQuad(-w, 0, 0,  w, 0, 0,  -w, 0, d,  0, 1, 0, div, div*2, verts, indices);
    // Left wall
    buildSubdividedQuad(-w, 0, 0,  -w, 0, d,  -w, h, 0,  1, 0, 0, div*2, div, verts, indices);
    // Right wall
    buildSubdividedQuad(w, 0, d,  w, 0, 0,  w, h, d,  -1, 0, 0, div*2, div, verts, indices);
    // Back wall
    buildSubdividedQuad(w, 0, d,  -w, 0, d,  w, h, d,  0, 0, -1, div, div, verts, indices);
    // Ceiling
    buildSubdividedQuad(-w, h, d,  w, h, d,  -w, h, 0,  0, -1, 0, div, div*2, verts, indices);
}

// Fog volume billboard quad (2 triangles, 4 vertices)
struct FogVertex {
    float pos[3];  // (-1,-1,0), (1,-1,0), (-1,1,0), (1,1,0)
};

static void buildFogQuad(GLuint& vbo, GLuint& ibo) {
    FogVertex verts[4] = {
        {{-1.0f, -1.0f, 0.0f}},
        {{ 1.0f, -1.0f, 0.0f}},
        {{-1.0f,  1.0f, 0.0f}},
        {{ 1.0f,  1.0f, 0.0f}},
    };
    uint16_t indices[6] = { 0, 1, 2, 1, 3, 2 };

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
}

// ============================================================================
// Checkerboard texture
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
// Matrix math (same as perpixel benchmark)
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
// Fog sphere positions along the alley
// ============================================================================

struct FogSphere {
    float x, y, z;   // center (Irrlicht Y-up)
    float r, g, b;   // fog color
};

// 8 fog spheres at light/fire positions along the alley
static const FogSphere fogSpheres[] = {
    // Campfire on the ground near player
    { 0.0f, 1.5f,  3.0f,   1.0f, 0.6f, 0.2f},
    // Wall torch left
    {-2.5f, 3.5f,  6.0f,   1.0f, 0.7f, 0.3f},
    // Wall torch right
    { 2.5f, 3.5f,  9.0f,   1.0f, 0.7f, 0.3f},
    // Brazier center
    { 0.0f, 2.0f, 12.0f,   1.0f, 0.5f, 0.15f},
    // Wall torch left
    {-2.5f, 3.5f, 14.0f,   1.0f, 0.7f, 0.3f},
    // Small fire right
    { 1.5f, 1.0f, 16.0f,   0.9f, 0.4f, 0.1f},
    // Ceiling lantern
    { 0.0f, 6.5f, 18.0f,   0.8f, 0.8f, 0.6f},
    // Deep brazier
    { 0.0f, 2.0f, 19.0f,   1.0f, 0.5f, 0.15f},
};
static const int NUM_FOG_SPHERES = sizeof(fogSpheres) / sizeof(fogSpheres[0]);

// ============================================================================
// Benchmark runner
// ============================================================================

struct BenchResult {
    double avgMs;
    double minMs;
    double maxMs;
};

// Draw the zone (opaque pass) — returns nothing, just draws
static void drawZonePass(GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount,
                          GLuint texture, float aspect, const float viewProj[16]) {
    glUseProgram(zoneProgram);

    float world[16];
    mat4Identity(world);

    GLint locWVP = glGetUniformLocation(zoneProgram, "mWorldViewProj");
    GLint locWorld = glGetUniformLocation(zoneProgram, "mWorld");
    GLint locSunDir = glGetUniformLocation(zoneProgram, "uSunDir");
    GLint locSunColor = glGetUniformLocation(zoneProgram, "uSunColor");
    GLint locAmbient = glGetUniformLocation(zoneProgram, "uAmbientColor");
    GLint locTint = glGetUniformLocation(zoneProgram, "uTintColor");
    GLint locFogStart = glGetUniformLocation(zoneProgram, "uFogStart");
    GLint locFogEnd = glGetUniformLocation(zoneProgram, "uFogEnd");
    GLint locFogColor = glGetUniformLocation(zoneProgram, "uFogColor");
    GLint locTexture = glGetUniformLocation(zoneProgram, "uTexture");
    GLint locLightPos = glGetUniformLocation(zoneProgram, "uLightPos[0]");
    GLint locLightColor = glGetUniformLocation(zoneProgram, "uLightColor[0]");
    GLint locLightAtten = glGetUniformLocation(zoneProgram, "uLightAtten[0]");

    if (locWVP >= 0) glUniformMatrix4fv(locWVP, 1, GL_FALSE, viewProj);
    if (locWorld >= 0) glUniformMatrix4fv(locWorld, 1, GL_FALSE, world);

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

    // Point lights at fog sphere positions (up to 8)
    float lightPos[8*3] = {}, lightColor[8*3] = {}, lightAtten[8*3] = {};
    for (int i = 0; i < NUM_FOG_SPHERES && i < 8; i++) {
        lightPos[i*3+0] = fogSpheres[i].x;
        lightPos[i*3+1] = fogSpheres[i].y;
        lightPos[i*3+2] = fogSpheres[i].z;
        lightColor[i*3+0] = fogSpheres[i].r * 0.8f;
        lightColor[i*3+1] = fogSpheres[i].g * 0.8f;
        lightColor[i*3+2] = fogSpheres[i].b * 0.8f;
        lightAtten[i*3+0] = 1.0f;
        lightAtten[i*3+1] = 0.0f;
        lightAtten[i*3+2] = 19.0f / (40.0f * 40.0f);
    }
    if (locLightPos >= 0) glUniform3fv(locLightPos, 8, lightPos);
    if (locLightColor >= 0) glUniform3fv(locLightColor, 8, lightColor);
    if (locLightAtten >= 0) glUniform3fv(locLightAtten, 8, lightAtten);

    glBindBuffer(GL_ARRAY_BUFFER, zoneVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zoneIbo);

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
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    glDrawElements(GL_TRIANGLES, zoneIndexCount, GL_UNSIGNED_SHORT, nullptr);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
}

// Draw fog volumes (transparent pass)
static void drawFogPass(GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                         int numSpheres, float radius,
                         const float viewProj[16], const float cameraPos[3],
                         const float cameraRight[3], const float cameraUp[3]) {
    glUseProgram(fogProgram);

    // Fog volumes are alpha-blended, depth-tested but not depth-written
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive for light-emitting fog
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // don't write depth

    GLint locViewProj = glGetUniformLocation(fogProgram, "uViewProj");
    GLint locCamPos = glGetUniformLocation(fogProgram, "uCameraPos");
    GLint locCamRight = glGetUniformLocation(fogProgram, "uCameraRight");
    GLint locCamUp = glGetUniformLocation(fogProgram, "uCameraUp");
    GLint locCenter = glGetUniformLocation(fogProgram, "uSphereCenter");
    GLint locRadius = glGetUniformLocation(fogProgram, "uSphereRadius");
    GLint locFogColor = glGetUniformLocation(fogProgram, "uFogColor");
    GLint locDensity = glGetUniformLocation(fogProgram, "uFogDensity");

    if (locViewProj >= 0) glUniformMatrix4fv(locViewProj, 1, GL_FALSE, viewProj);
    if (locCamPos >= 0) glUniform3fv(locCamPos, 1, cameraPos);
    if (locCamRight >= 0) glUniform3fv(locCamRight, 1, cameraRight);
    if (locCamUp >= 0) glUniform3fv(locCamUp, 1, cameraUp);
    if (locDensity >= 0) glUniform1f(locDensity, 2.0f);

    glBindBuffer(GL_ARRAY_BUFFER, fogVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fogIbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FogVertex), (void*)0);

    for (int i = 0; i < numSpheres && i < NUM_FOG_SPHERES; i++) {
        float center[] = { fogSpheres[i].x, fogSpheres[i].y, fogSpheres[i].z };
        float color[] = { fogSpheres[i].r, fogSpheres[i].g, fogSpheres[i].b };
        if (locCenter >= 0) glUniform3fv(locCenter, 1, center);
        if (locRadius >= 0) glUniform1f(locRadius, radius);
        if (locFogColor >= 0) glUniform3fv(locFogColor, 1, color);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    glDisableVertexAttribArray(0);

    // Restore state
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// Benchmark: fog pass only (clear + fog spheres, no zone geometry)
static BenchResult benchFogOnly(EGLState& state, GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                                 int numSpheres, float radius, int numFrames,
                                 const float viewProj[16], const float cameraPos[3],
                                 const float cameraRight[3], const float cameraUp[3]) {
    // Warmup
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp);
        glFinish();

#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif

        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
}

// Benchmark: zone + fog combined (realistic scenario)
static BenchResult benchZonePlusFog(EGLState& state,
                                     GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount, GLuint texture,
                                     GLuint fogProgram, GLuint fogVbo, GLuint fogIbo,
                                     int numSpheres, float radius, int numFrames, float aspect,
                                     const float viewProj[16], const float cameraPos[3],
                                     const float cameraRight[3], const float cameraUp[3]) {
    glViewport(0, 0, state.width, state.height);

    // Warmup
    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        drawFogPass(fogProgram, fogVbo, fogIbo, numSpheres, radius,
                    viewProj, cameraPos, cameraRight, cameraUp);
        glFinish();

#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif

        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
    result.avgMs = sum / frameTimes.size();
    result.minMs = mn;
    result.maxMs = mx;
    return result;
}

// Benchmark: zone only (baseline for delta computation)
static BenchResult benchZoneOnly(EGLState& state,
                                  GLuint zoneProgram, GLuint zoneVbo, GLuint zoneIbo, int zoneIndexCount,
                                  GLuint texture, int numFrames, float aspect, const float viewProj[16]) {
    glViewport(0, 0, state.width, state.height);

    for (int i = 0; i < 5; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        glFinish();
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(numFrames);

    for (int i = 0; i < numFrames; i++) {
        auto t0 = std::chrono::steady_clock::now();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZonePass(zoneProgram, zoneVbo, zoneIbo, zoneIndexCount, texture, aspect, viewProj);
        glFinish();
#ifdef EQT_HAS_DRM
        if (state.isDRM) drmPresent(state);
#endif
        auto t1 = std::chrono::steady_clock::now();
        frameTimes.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    BenchResult result;
    double sum = 0, mn = 1e9, mx = 0;
    for (double t : frameTimes) { sum += t; if (t < mn) mn = t; if (t > mx) mx = t; }
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

    printf("=== GLES2 Analytic Fog Volume Benchmark ===\n");
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

    // ---- Build zone scene ----
    std::vector<Vertex> zoneVerts;
    std::vector<uint16_t> zoneIndices;
    buildAlleyScene(zoneVerts, zoneIndices);
    int zonePolyCount = (int)zoneIndices.size() / 3;
    printf("Zone scene: %d vertices, %d triangles (5 walls+floor+ceiling, subdivided)\n",
           (int)zoneVerts.size(), zonePolyCount);

    GLuint zoneVbo, zoneIbo;
    glGenBuffers(1, &zoneVbo);
    glBindBuffer(GL_ARRAY_BUFFER, zoneVbo);
    glBufferData(GL_ARRAY_BUFFER, zoneVerts.size() * sizeof(Vertex), zoneVerts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &zoneIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zoneIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, zoneIndices.size() * sizeof(uint16_t), zoneIndices.data(), GL_STATIC_DRAW);

    GLuint texture = createCheckerTexture(256);

    // ---- Build fog quad ----
    GLuint fogVbo, fogIbo;
    buildFogQuad(fogVbo, fogIbo);

    // ---- Compile zone shader ----
    printf("Compiling shaders...\n");
    GLuint vsZone = compileShader(GL_VERTEX_SHADER, VS_ZONE);
    GLuint fsZone = compileShader(GL_FRAGMENT_SHADER, FS_ZONE);
    GLuint progZone = (vsZone && fsZone) ? linkProgram(vsZone, fsZone) : 0;
    if (!progZone) {
        fprintf(stderr, "Zone shader compilation failed\n");
        cleanup(state);
        return 1;
    }

    // ---- Compile fog shaders ----
    GLuint vsFog = compileShader(GL_VERTEX_SHADER, VS_FOG);
    if (!vsFog) {
        fprintf(stderr, "Fog VS compilation failed\n");
        cleanup(state);
        return 1;
    }

    struct FogVariant {
        const char* name;
        const char* fsSrc;
        GLuint program;
    };

    FogVariant fogVariants[] = {
        {"QUADRATIC (1-r^2)",       FS_FOG_QUADRATIC, 0},
        {"QUARTIC (1-r^2)^2",       FS_FOG_QUARTIC,   0},
        {"LINEAR (1-r)",            FS_FOG_LINEAR,     0},
        {"GAUSSIAN exp(-r^2)",      FS_FOG_GAUSSIAN,   0},
    };
    const int numFogVariants = sizeof(fogVariants) / sizeof(fogVariants[0]);

    for (int i = 0; i < numFogVariants; i++) {
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fogVariants[i].fsSrc);
        if (!fs) {
            fprintf(stderr, "Fog FS '%s' compilation failed\n", fogVariants[i].name);
            cleanup(state);
            return 1;
        }
        // Fog shader uses only aPosition (attrib 0)
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vsFog);
        glAttachShader(prog, fs);
        glBindAttribLocation(prog, 0, "aPosition");
        glLinkProgram(prog);
        GLint linkOk;
        glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);
        if (!linkOk) {
            char log[512];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            fprintf(stderr, "Fog program '%s' link error:\n%s\n", fogVariants[i].name, log);
            cleanup(state);
            return 1;
        }
        fogVariants[i].program = prog;
        glDeleteShader(fs);
    }

    printf("  Zone program: 1, Fog programs: %d\n\n", numFogVariants);

    float aspect = (float)state.width / state.height;
    glViewport(0, 0, state.width, state.height);

    // Camera setup
    float proj[16], view[16], viewProj[16], tmp[16];
    mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
    mat4LookAt(view, 0.0f, 1.6f, 1.0f,  0.0f, 1.6f, 10.0f,  0.0f, 1.0f, 0.0f);
    mat4Multiply(viewProj, proj, view);

    float cameraPos[] = { 0.0f, 1.6f, 1.0f };

    // Camera basis vectors (looking down +Z, Y-up)
    // Right = +X, Up = +Y for this simple look-at
    float cameraRight[] = { 1.0f, 0.0f, 0.0f };
    float cameraUp[] = { 0.0f, 1.0f, 0.0f };

    // ================================================================
    // PHASE 1: Zone-only baseline
    // ================================================================
    printf("============================================================\n");
    printf("PHASE 1: Zone-only baseline (lightweight, 8 VS lights)\n");
    printf("============================================================\n\n");

    BenchResult zoneBaseline = benchZoneOnly(state, progZone, zoneVbo, zoneIbo,
                                             (int)zoneIndices.size(), texture,
                                             numFrames, aspect, viewProj);
    printf("  avg: %.2f ms  min: %.2f ms  max: %.2f ms  (~%.0f FPS)\n\n",
           zoneBaseline.avgMs, zoneBaseline.minMs, zoneBaseline.maxMs,
           1000.0 / zoneBaseline.avgMs);

    // ================================================================
    // PHASE 2: Fog-only (isolate fog FS cost)
    // ================================================================
    printf("============================================================\n");
    printf("PHASE 2: Fog-only (no zone geometry, isolate FS cost)\n");
    printf("============================================================\n\n");

    struct FogTestConfig {
        int numSpheres;
        float radius;
        const char* label;
    };

    FogTestConfig fogConfigs[] = {
        {1, 3.0f, "1 sphere, r=3 (small)"},
        {1, 6.0f, "1 sphere, r=6 (large)"},
        {2, 3.0f, "2 spheres, r=3"},
        {4, 3.0f, "4 spheres, r=3"},
        {4, 6.0f, "4 spheres, r=6"},
        {8, 3.0f, "8 spheres, r=3"},
        {8, 6.0f, "8 spheres, r=6"},
    };
    const int numFogConfigs = sizeof(fogConfigs) / sizeof(fogConfigs[0]);

    // Results storage: [variant][config]
    struct TestResult {
        const char* variantName;
        const char* configLabel;
        BenchResult result;
    };
    std::vector<TestResult> fogOnlyResults;

    for (int v = 0; v < numFogVariants; v++) {
        printf("--- %s ---\n", fogVariants[v].name);
        for (int c = 0; c < numFogConfigs; c++) {
            BenchResult r = benchFogOnly(state, fogVariants[v].program, fogVbo, fogIbo,
                                          fogConfigs[c].numSpheres, fogConfigs[c].radius,
                                          numFrames, viewProj, cameraPos, cameraRight, cameraUp);
            printf("  %-25s  avg: %6.2f ms  (~%.0f FPS)\n",
                   fogConfigs[c].label, r.avgMs, 1000.0 / r.avgMs);
            fogOnlyResults.push_back({fogVariants[v].name, fogConfigs[c].label, r});
        }
        printf("\n");
    }

    // ================================================================
    // PHASE 3: Zone + fog combined (realistic scenario)
    // ================================================================
    printf("============================================================\n");
    printf("PHASE 3: Zone + fog combined (realistic frame)\n");
    printf("============================================================\n\n");

    // Test each variant with the most likely production configs:
    // 4 spheres (typical torch count in a dungeon hallway) at both radii
    struct CombinedTestResult {
        const char* variantName;
        const char* configLabel;
        BenchResult combined;
        double fogDeltaMs;  // combined - zoneBaseline
    };
    std::vector<CombinedTestResult> combinedResults;

    FogTestConfig combinedConfigs[] = {
        {2, 3.0f, "2 spheres, r=3"},
        {4, 3.0f, "4 spheres, r=3"},
        {4, 6.0f, "4 spheres, r=6"},
        {8, 3.0f, "8 spheres, r=3"},
    };
    const int numCombinedConfigs = sizeof(combinedConfigs) / sizeof(combinedConfigs[0]);

    for (int v = 0; v < numFogVariants; v++) {
        printf("--- %s ---\n", fogVariants[v].name);
        for (int c = 0; c < numCombinedConfigs; c++) {
            BenchResult r = benchZonePlusFog(state, progZone, zoneVbo, zoneIbo,
                                              (int)zoneIndices.size(), texture,
                                              fogVariants[v].program, fogVbo, fogIbo,
                                              combinedConfigs[c].numSpheres, combinedConfigs[c].radius,
                                              numFrames, aspect, viewProj, cameraPos, cameraRight, cameraUp);
            double delta = r.avgMs - zoneBaseline.avgMs;
            printf("  %-25s  avg: %6.2f ms  (~%.0f FPS)  fog cost: %+.2f ms\n",
                   combinedConfigs[c].label, r.avgMs, 1000.0 / r.avgMs, delta);
            combinedResults.push_back({fogVariants[v].name, combinedConfigs[c].label, r, delta});
        }
        printf("\n");
    }

    // ================================================================
    // Summary
    // ================================================================
    printf("============================================================\n");
    printf("SUMMARY\n");
    printf("============================================================\n\n");

    printf("Zone baseline: %.2f ms (~%.0f FPS)\n\n", zoneBaseline.avgMs, 1000.0 / zoneBaseline.avgMs);

    printf("--- Fog-only (isolated FS cost) ---\n");
    printf("%-25s  %-25s  %7s  %7s\n", "Density Function", "Config", "ms", "FPS");
    printf("%-25s  %-25s  %7s  %7s\n", "----------------", "------", "--", "---");
    for (const auto& r : fogOnlyResults) {
        printf("%-25s  %-25s  %7.2f  %7.0f\n",
               r.variantName, r.configLabel, r.result.avgMs, 1000.0 / r.result.avgMs);
    }

    printf("\n--- Zone + fog combined (production scenario) ---\n");
    printf("%-25s  %-25s  %7s  %7s  %10s\n", "Density Function", "Config", "ms", "FPS", "fog cost");
    printf("%-25s  %-25s  %7s  %7s  %10s\n", "----------------", "------", "--", "---", "--------");
    for (const auto& r : combinedResults) {
        printf("%-25s  %-25s  %7.2f  %7.0f  %+9.2fms\n",
               r.variantName, r.configLabel, r.combined.avgMs,
               1000.0 / r.combined.avgMs, r.fogDeltaMs);
    }

    printf("\n--- Budget analysis (33.3ms frame budget @ 30 FPS) ---\n");
    printf("Zone baseline uses %.1f ms (%.0f%% of budget)\n",
           zoneBaseline.avgMs, zoneBaseline.avgMs / 33.3 * 100.0);
    printf("\nBest-case fog cost (QUADRATIC, 4 spheres r=3):\n");
    for (const auto& r : combinedResults) {
        if (strcmp(r.variantName, "QUADRATIC (1-r^2)") == 0 &&
            strcmp(r.configLabel, "4 spheres, r=3") == 0) {
            printf("  Combined: %.1f ms (%.0f%% of budget), fog adds %.1f ms\n",
                   r.combined.avgMs, r.combined.avgMs / 33.3 * 100.0, r.fogDeltaMs);
            printf("  Headroom remaining: %.1f ms\n", 33.3 - r.combined.avgMs);
        }
    }

    // ---- Cleanup ----
    glDeleteProgram(progZone);
    glDeleteShader(vsZone);
    glDeleteShader(fsZone);
    glDeleteShader(vsFog);
    for (int i = 0; i < numFogVariants; i++)
        glDeleteProgram(fogVariants[i].program);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &zoneVbo);
    glDeleteBuffers(1, &zoneIbo);
    glDeleteBuffers(1, &fogVbo);
    glDeleteBuffers(1, &fogIbo);

    cleanup(state);
    return 0;
}
