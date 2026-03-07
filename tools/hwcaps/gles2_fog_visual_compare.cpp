// gles2_fog_visual_compare - Visual comparison of fog rendering approaches on GLES2
//
// Renders the same alley scene with 3 alternative fog approaches (plus baseline)
// and saves screenshots as PPM files. Also reports frame times.
//
// Approaches:
//   1. Baseline (zone only, no fog)
//   2. Icosphere mesh: vertex-colored low-poly spheres with alpha falloff
//   3. Textured billboard: pre-computed radial gradient quad
//   4. Per-vertex zone fog: fire warmth computed in zone VS, zero extra draws
//
// Output: fog_baseline.ppm, fog_icosphere.ppm, fog_billboard.ppm, fog_pervertex.ppm
//
// Usage:
//   ./gles2_fog_visual_compare              # auto-detect (DRM first, then X11)
//   ./gles2_fog_visual_compare --drm        # force DRM/GBM
//   ./gles2_fog_visual_compare --x11        # force X11/EGL
//   ./gles2_fog_visual_compare --res W H    # resolution (default 1280 720)
//   ./gles2_fog_visual_compare --frames N   # frames per test for timing (default 60)

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
// EGL/DRM setup (same as other benchmarks)
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
    if (state.drmFd < 0) { fprintf(stderr, "DRM: could not open device\n"); return false; }

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
    if (state.display == EGL_NO_DISPLAY) { gbm_device_destroy(state.gbmDevice); close(state.drmFd); return false; }

    EGLint major, minor;
    if (!eglInitialize(state.display, &major, &minor)) { gbm_device_destroy(state.gbmDevice); close(state.drmFd); return false; }
    printf("EGL %d.%d (DRM/GBM, GLES2)\n", major, minor);

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    EGLint numConfigs;
    eglChooseConfig(state.display, configAttribs, &state.config, 1, &numConfigs);

    state.gbmSurface = gbm_surface_create(state.gbmDevice, state.width, state.height, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    state.surface = eglCreateWindowSurface(state.display, state.config, (EGLNativeWindowType)state.gbmSurface, nullptr);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state.context = eglCreateContext(state.display, state.config, EGL_NO_CONTEXT, contextAttribs);
    if (state.context == EGL_NO_CONTEXT) { eglTerminate(state.display); gbm_surface_destroy(state.gbmSurface); gbm_device_destroy(state.gbmDevice); close(state.drmFd); return false; }

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
    if (first) { drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode); first = false; }
    else { int ret = drmModePageFlip(state.drmFd, state.crtcId, fb, 0, nullptr); if (ret) drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode); }
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
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE
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
// Shader compilation
// ============================================================================

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
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
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Program link error:\n%s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ============================================================================
// Matrix math
// ============================================================================

static void mat4Identity(float m[16]) {
    memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4Perspective(float m[16], float fovDeg, float aspect, float near, float far) {
    memset(m, 0, 64);
    float f = 1.0f / tanf(fovDeg * 3.14159f / 360.0f);
    m[0] = f / aspect; m[5] = f;
    m[10] = (far + near) / (near - far); m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void mat4LookAt(float m[16], float ex, float ey, float ez, float ax, float ay, float az, float ux, float uy, float uz) {
    float fx = ax-ex, fy = ay-ey, fz = az-ez;
    float fl = sqrtf(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
    float sx = fy*uz-fz*uy, sy = fz*ux-fx*uz, sz = fx*uy-fy*ux;
    float sl = sqrtf(sx*sx+sy*sy+sz*sz); sx/=sl; sy/=sl; sz/=sl;
    float uux = sy*fz-sz*fy, uuy = sz*fx-sx*fz, uuz = sx*fy-sy*fx;
    mat4Identity(m);
    m[0]=sx; m[4]=sy; m[8]=sz; m[1]=uux; m[5]=uuy; m[9]=uuz; m[2]=-fx; m[6]=-fy; m[10]=-fz;
    m[12]=-(sx*ex+sy*ey+sz*ez); m[13]=-(uux*ex+uuy*ey+uuz*ez); m[14]=(fx*ex+fy*ey+fz*ez);
}

static void mat4Multiply(float out[16], const float a[16], const float b[16]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            out[i+j*4] = 0;
            for (int k = 0; k < 4; k++) out[i+j*4] += a[i+k*4] * b[k+j*4];
        }
}

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
    float x0, float y0, float z0, float x1, float y1, float z1,
    float x2, float y2, float z2, float nx, float ny, float nz,
    int divU, int divV, std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    float x3=x1+(x2-x0), y3=y1+(y2-y0), z3=z1+(z2-z0);
    uint16_t base = (uint16_t)verts.size();
    for (int v = 0; v <= divV; v++) {
        float tv = (float)v / divV;
        for (int u = 0; u <= divU; u++) {
            float tu = (float)u / divU;
            Vertex vert;
            vert.pos[0] = (1-tu)*(1-tv)*x0 + tu*(1-tv)*x1 + (1-tu)*tv*x2 + tu*tv*x3;
            vert.pos[1] = (1-tu)*(1-tv)*y0 + tu*(1-tv)*y1 + (1-tu)*tv*y2 + tu*tv*y3;
            vert.pos[2] = (1-tu)*(1-tv)*z0 + tu*(1-tv)*z1 + (1-tu)*tv*z2 + tu*tv*z3;
            vert.normal[0]=nx; vert.normal[1]=ny; vert.normal[2]=nz;
            vert.color[0]=0.8f; vert.color[1]=0.8f; vert.color[2]=0.8f; vert.color[3]=1.0f;
            vert.uv[0]=tu; vert.uv[1]=tv;
            verts.push_back(vert);
        }
    }
    for (int v = 0; v < divV; v++)
        for (int u = 0; u < divU; u++) {
            uint16_t i0 = base + v*(divU+1)+u, i1=i0+1, i2=i0+(divU+1), i3=i2+1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
}

static void buildAlleyScene(std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    float w=3.0f, d=20.0f, h=8.0f; int div=4;
    buildSubdividedQuad(-w,0,0, w,0,0, -w,0,d, 0,1,0, div,div*2, verts,indices);   // floor
    buildSubdividedQuad(-w,0,0, -w,0,d, -w,h,0, 1,0,0, div*2,div, verts,indices);  // left
    buildSubdividedQuad(w,0,d, w,0,0, w,h,d, -1,0,0, div*2,div, verts,indices);    // right
    buildSubdividedQuad(w,0,d, -w,0,d, w,h,d, 0,0,-1, div,div, verts,indices);     // back
    buildSubdividedQuad(-w,h,d, w,h,d, -w,h,0, 0,-1,0, div,div*2, verts,indices);  // ceiling
}

static GLuint createCheckerTexture(int size) {
    std::vector<uint8_t> pixels(size * size * 3);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            bool check = ((x/(size/8)) + (y/(size/8))) % 2 == 0;
            uint8_t val = check ? 180 : 100;
            int i = (y*size+x)*3;
            pixels[i]=val; pixels[i+1]=val; pixels[i+2]=val;
        }
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}

// ============================================================================
// Fog emitter positions (shared across all approaches)
// ============================================================================

struct FogEmitter {
    float x, y, z;      // center (Irrlicht Y-up)
    float r, g, b;       // warm color
    float radius;         // effect radius
    float intensity;      // brightness multiplier
};

static const FogEmitter emitters[] = {
    { 0.0f, 1.5f,  3.0f,   1.0f, 0.6f, 0.2f,   3.0f, 1.2f},  // campfire near player
    {-2.5f, 3.5f,  6.0f,   1.0f, 0.7f, 0.3f,   2.5f, 0.8f},  // wall torch left
    { 2.5f, 3.5f,  9.0f,   1.0f, 0.7f, 0.3f,   2.5f, 0.8f},  // wall torch right
    { 0.0f, 2.0f, 12.0f,   1.0f, 0.5f, 0.15f,  3.5f, 1.0f},  // brazier center
};
static const int NUM_EMITTERS = sizeof(emitters) / sizeof(emitters[0]);

// ============================================================================
// APPROACH 1: Zone shader (baseline, no fog)
// ============================================================================

static const char* VS_ZONE_BASE = R"(
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
        float atten = 1.0 / (uLightAtten[i].x + uLightAtten[i].y * d + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting, 0.0);
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

static const char* FS_ZONE_BASE = R"(
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
// APPROACH 4: Per-vertex zone fog (fire warmth in zone VS)
// Adds emitter glow contribution per vertex — zero extra draw calls
// ============================================================================

static const char* VS_ZONE_FOGGY = R"(
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

// Fog emitters (reuse light arrays — emitters at indices 0-3)
uniform vec3 uFogEmitterPos[4];
uniform vec3 uFogEmitterColor[4];
uniform float uFogEmitterRadius[4];
uniform float uFogEmitterIntensity[4];
uniform int uNumFogEmitters;

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
        float atten = 1.0 / (uLightAtten[i].x + uLightAtten[i].y * d + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        pointLighting += uLightColor[i] * nl * atten;
    }

    // Fog emitter contribution: omnidirectional warm glow based on distance
    // Uses smooth quadratic falloff: (1 - (d/r)^2)^2 * intensity
    // This simulates "visible air" — adds additive color regardless of surface normal
    vec3 fogGlow = vec3(0.0);
    for (int i = 0; i < 4; i++) {
        if (i >= uNumFogEmitters) break;
        vec3 toEmitter = uFogEmitterPos[i] - worldPos;
        float d2 = dot(toEmitter, toEmitter);
        float r = uFogEmitterRadius[i];
        float r2 = r * r;
        if (d2 < r2) {
            float t = 1.0 - d2 / r2;      // [0,1] — 1 at center, 0 at edge
            float falloff = t * t;          // smooth quartic-like falloff
            fogGlow += uFogEmitterColor[i] * falloff * uFogEmitterIntensity[i];
        }
    }

    vColor = vec4(baseLighting * uTintColor, 1.0) * aColor + vec4(pointLighting + fogGlow, 0.0);
    float fogDist = length((mWorldViewProj * pos).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// ============================================================================
// APPROACH 2: Icosphere mesh with vertex-colored alpha falloff
// ============================================================================

struct SimpleVertex {
    float pos[3];
    float color[4];
};

// Build an icosphere with vertex colors fading from center to edge
static void buildIcosphere(float cx, float cy, float cz, float radius,
                            float r, float g, float b, float intensity,
                            int subdivisions,
                            std::vector<SimpleVertex>& verts, std::vector<uint16_t>& indices)
{
    // Start with icosahedron
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    float icoVerts[][3] = {
        {-1, t,0}, {1, t,0}, {-1,-t,0}, {1,-t,0},
        {0,-1, t}, {0, 1, t}, {0,-1,-t}, {0, 1,-t},
        { t,0,-1}, { t,0, 1}, {-t,0,-1}, {-t,0, 1},
    };
    // Normalize to unit sphere
    for (int i = 0; i < 12; i++) {
        float l = sqrtf(icoVerts[i][0]*icoVerts[i][0] + icoVerts[i][1]*icoVerts[i][1] + icoVerts[i][2]*icoVerts[i][2]);
        icoVerts[i][0] /= l; icoVerts[i][1] /= l; icoVerts[i][2] /= l;
    }

    int icoIndices[] = {
        0,11,5,  0,5,1,  0,1,7,  0,7,10,  0,10,11,
        1,5,9,   5,11,4, 11,10,2, 10,7,6,  7,1,8,
        3,9,4,   3,4,2,  3,2,6,   3,6,8,   3,8,9,
        4,9,5,   2,4,11, 6,2,10,  8,6,7,   9,8,1,
    };

    // Working lists for subdivision
    struct Tri { int i0, i1, i2; };
    std::vector<float> pts;
    for (int i = 0; i < 12; i++) { pts.push_back(icoVerts[i][0]); pts.push_back(icoVerts[i][1]); pts.push_back(icoVerts[i][2]); }
    std::vector<Tri> tris;
    for (int i = 0; i < 20; i++) tris.push_back({icoIndices[i*3], icoIndices[i*3+1], icoIndices[i*3+2]});

    // Subdivide
    for (int s = 0; s < subdivisions; s++) {
        std::vector<Tri> newTris;
        // Simple midpoint subdivision (no dedup — produces more verts but simpler code)
        for (const auto& tri : tris) {
            auto midpoint = [&](int a, int b) -> int {
                int idx = (int)pts.size() / 3;
                float mx = (pts[a*3]+pts[b*3])*0.5f, my = (pts[a*3+1]+pts[b*3+1])*0.5f, mz = (pts[a*3+2]+pts[b*3+2])*0.5f;
                float l = sqrtf(mx*mx+my*my+mz*mz);
                pts.push_back(mx/l); pts.push_back(my/l); pts.push_back(mz/l);
                return idx;
            };
            int m01 = midpoint(tri.i0, tri.i1);
            int m12 = midpoint(tri.i1, tri.i2);
            int m20 = midpoint(tri.i2, tri.i0);
            newTris.push_back({tri.i0, m01, m20});
            newTris.push_back({tri.i1, m12, m01});
            newTris.push_back({tri.i2, m20, m12});
            newTris.push_back({m01, m12, m20});
        }
        tris = newTris;
    }

    uint16_t base = (uint16_t)verts.size();
    int numPts = (int)pts.size() / 3;
    for (int i = 0; i < numPts; i++) {
        SimpleVertex sv;
        sv.pos[0] = cx + pts[i*3+0] * radius;
        sv.pos[1] = cy + pts[i*3+1] * radius;
        sv.pos[2] = cz + pts[i*3+2] * radius;

        // Alpha falls off from center: use distance from center (always = radius for sphere surface)
        // For a solid sphere effect, we want the silhouette edges to be more transparent
        // Use the vertex normal dot view direction as a proxy for edge vs center
        // But simpler: just use a constant alpha that makes the sphere look like a glow
        // The "fog" look comes from seeing the back faces through the front faces
        float alpha = 0.15f * intensity;  // subtle, semi-transparent
        sv.color[0] = r * intensity;
        sv.color[1] = g * intensity;
        sv.color[2] = b * intensity;
        sv.color[3] = alpha;
        verts.push_back(sv);
    }
    for (const auto& tri : tris) {
        indices.push_back(base + tri.i0);
        indices.push_back(base + tri.i1);
        indices.push_back(base + tri.i2);
    }
}

// Simple shader for icosphere: just interpolated vertex color
static const char* VS_ICOSPHERE = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec4 aColor;
uniform mat4 uViewProj;
varying vec4 vColor;

void main() {
    gl_Position = uViewProj * vec4(aPosition, 1.0);
    vColor = aColor;
}
)";

static const char* FS_ICOSPHERE = R"(
precision mediump float;
varying vec4 vColor;

void main() {
    gl_FragColor = vColor;
}
)";

// ============================================================================
// APPROACH 3: Textured billboard (pre-computed radial gradient)
// ============================================================================

struct BillboardVertex {
    float pos[3];
    float uv[2];
};

static GLuint createRadialGradientTexture(int size) {
    std::vector<uint8_t> pixels(size * size * 4);
    float center = (float)(size - 1) * 0.5f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float dx = (x - center) / center;
            float dy = (y - center) / center;
            float d2 = dx*dx + dy*dy;
            float alpha = 0.0f;
            if (d2 < 1.0f) {
                float t = 1.0f - d2;
                alpha = t * t;  // quartic falloff
            }
            int i = (y * size + x) * 4;
            pixels[i+0] = 255;  // white — color comes from uniform tint
            pixels[i+1] = 255;
            pixels[i+2] = 255;
            pixels[i+3] = (uint8_t)(alpha * 255.0f);
        }
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static const char* VS_BILLBOARD = R"(
precision highp float;
attribute vec3 aPosition;   // (-1,-1), (1,-1), (-1,1), (1,1)
attribute vec2 aTexCoord0;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform highp float uRadius;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;

varying vec2 vTexCoord;

void main() {
    vec3 worldPos = uCenter + uCameraRight * aPosition.x * uRadius + uCameraUp * aPosition.y * uRadius;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vTexCoord = aTexCoord0;
}
)";

static const char* FS_BILLBOARD = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec3 uColor;
uniform float uIntensity;
varying vec2 vTexCoord;

void main() {
    vec4 texel = texture2D(uTexture, vTexCoord);
    float alpha = texel.a * uIntensity * 0.3;
    gl_FragColor = vec4(uColor * alpha, alpha);
}
)";

// ============================================================================
// Screenshot saving (PPM format)
// ============================================================================

static void saveScreenshot(const char* filename, int width, int height) {
    std::vector<uint8_t> pixels(width * height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    FILE* f = fopen(filename, "wb");
    if (!f) { fprintf(stderr, "Could not open %s for writing\n", filename); return; }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    // Flip vertically (GL reads bottom-up)
    for (int y = height - 1; y >= 0; y--)
        fwrite(&pixels[y * width * 3], 1, width * 3, f);
    fclose(f);
    printf("  Saved: %s\n", filename);
}

// ============================================================================
// Rendering helpers
// ============================================================================

static void setZoneUniforms(GLuint program, const float viewProj[16]) {
    float world[16]; mat4Identity(world);
    GLint loc;
    if ((loc = glGetUniformLocation(program, "mWorldViewProj")) >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, viewProj);
    if ((loc = glGetUniformLocation(program, "mWorld")) >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, world);

    float sunDir[] = {0.0f, -1.0f, 0.0f}, sunColor[] = {0.0f, 0.0f, 0.0f};
    float ambient[] = {0.05f, 0.05f, 0.08f}, tint[] = {0.3f, 0.3f, 0.4f};
    if ((loc = glGetUniformLocation(program, "uSunDir")) >= 0) glUniform3fv(loc, 1, sunDir);
    if ((loc = glGetUniformLocation(program, "uSunColor")) >= 0) glUniform3fv(loc, 1, sunColor);
    if ((loc = glGetUniformLocation(program, "uAmbientColor")) >= 0) glUniform3fv(loc, 1, ambient);
    if ((loc = glGetUniformLocation(program, "uTintColor")) >= 0) glUniform3fv(loc, 1, tint);
    if ((loc = glGetUniformLocation(program, "uFogStart")) >= 0) glUniform1f(loc, 50.0f);
    if ((loc = glGetUniformLocation(program, "uFogEnd")) >= 0) glUniform1f(loc, 200.0f);
    float fogColor[] = {0.1f, 0.1f, 0.15f, 1.0f};
    if ((loc = glGetUniformLocation(program, "uFogColor")) >= 0) glUniform4fv(loc, 1, fogColor);
    if ((loc = glGetUniformLocation(program, "uTexture")) >= 0) glUniform1i(loc, 0);

    // Point lights at emitter positions
    float lightPos[24]={}, lightColor[24]={}, lightAtten[24]={};
    for (int i = 0; i < NUM_EMITTERS && i < 8; i++) {
        lightPos[i*3]=emitters[i].x; lightPos[i*3+1]=emitters[i].y; lightPos[i*3+2]=emitters[i].z;
        lightColor[i*3]=emitters[i].r*0.8f; lightColor[i*3+1]=emitters[i].g*0.8f; lightColor[i*3+2]=emitters[i].b*0.8f;
        lightAtten[i*3]=1.0f; lightAtten[i*3+1]=0.0f; lightAtten[i*3+2]=19.0f/(40.0f*40.0f);
    }
    for (int i = NUM_EMITTERS; i < 8; i++) lightAtten[i*3] = 1.0f;
    if ((loc = glGetUniformLocation(program, "uLightPos[0]")) >= 0) glUniform3fv(loc, 8, lightPos);
    if ((loc = glGetUniformLocation(program, "uLightColor[0]")) >= 0) glUniform3fv(loc, 8, lightColor);
    if ((loc = glGetUniformLocation(program, "uLightAtten[0]")) >= 0) glUniform3fv(loc, 8, lightAtten);
}

static void drawZone(GLuint program, GLuint vbo, GLuint ibo, int indexCount, GLuint texture) {
    glUseProgram(program);
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
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2); glDisableVertexAttribArray(3);
}

// ============================================================================
// Timing helper
// ============================================================================

struct FrameTimer {
    EGLState& state;
    int numFrames;

    double timeFrames(auto drawFunc) {
        // Warmup
        for (int i = 0; i < 3; i++) { drawFunc(); glFinish(); }
        // Timed
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < numFrames; i++) {
            drawFunc();
            glFinish();
#ifdef EQT_HAS_DRM
            if (state.isDRM) drmPresent(state);
#endif
        }
        auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count() / numFrames;
    }
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int numFrames = 60;
    bool forceDRM = false, forceX11 = false;
    int width = 1280, height = 720;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--drm") == 0) forceDRM = true;
        else if (strcmp(argv[i], "--x11") == 0) forceX11 = true;
        else if (strcmp(argv[i], "--frames") == 0 && i+1 < argc) numFrames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--res") == 0 && i+2 < argc) { width = atoi(argv[++i]); height = atoi(argv[++i]); }
    }

    printf("=== GLES2 Fog Visual Comparison ===\n");
    printf("Resolution: %dx%d, Frames: %d per test\n\n", width, height, numFrames);

    EGLState state;
    state.width = width; state.height = height;
    bool ok = false;
#ifdef EQT_HAS_DRM
    if (!forceX11) ok = initDRM(state);
#endif
    if (!ok && !forceDRM) ok = initX11(state);
    if (!ok) { fprintf(stderr, "Failed to init EGL\n"); return 1; }

    printf("GL: %s\n", glGetString(GL_RENDERER));
    printf("GL: %s\n\n", glGetString(GL_VERSION));

    // Camera
    float aspect = (float)state.width / state.height;
    float proj[16], view[16], viewProj[16];
    mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
    mat4LookAt(view, 0.0f, 1.6f, 1.0f,  0.0f, 1.6f, 10.0f,  0.0f, 1.0f, 0.0f);
    mat4Multiply(viewProj, proj, view);
    float camRight[] = {1.0f, 0.0f, 0.0f};
    float camUp[] = {0.0f, 1.0f, 0.0f};

    glViewport(0, 0, state.width, state.height);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    // ---- Build zone ----
    std::vector<Vertex> zoneVerts;
    std::vector<uint16_t> zoneIndices;
    buildAlleyScene(zoneVerts, zoneIndices);
    GLuint zoneVbo, zoneIbo;
    glGenBuffers(1, &zoneVbo); glBindBuffer(GL_ARRAY_BUFFER, zoneVbo);
    glBufferData(GL_ARRAY_BUFFER, zoneVerts.size()*sizeof(Vertex), zoneVerts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &zoneIbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zoneIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, zoneIndices.size()*sizeof(uint16_t), zoneIndices.data(), GL_STATIC_DRAW);
    GLuint checkerTex = createCheckerTexture(256);

    printf("Zone: %d verts, %d tris\n", (int)zoneVerts.size(), (int)zoneIndices.size()/3);

    // ---- Compile zone shaders ----
    printf("Compiling shaders...\n");
    GLuint vsBase = compileShader(GL_VERTEX_SHADER, VS_ZONE_BASE);
    GLuint fsBase = compileShader(GL_FRAGMENT_SHADER, FS_ZONE_BASE);
    GLuint progBase = linkProgram(vsBase, fsBase);

    GLuint vsFoggy = compileShader(GL_VERTEX_SHADER, VS_ZONE_FOGGY);
    GLuint progFoggy = linkProgram(vsFoggy, fsBase);  // same FS

    // ---- Compile icosphere shader ----
    GLuint vsIco = compileShader(GL_VERTEX_SHADER, VS_ICOSPHERE);
    GLuint fsIco = compileShader(GL_FRAGMENT_SHADER, FS_ICOSPHERE);
    GLuint progIco = linkProgram(vsIco, fsIco);

    // ---- Compile billboard shader ----
    GLuint vsBill = compileShader(GL_VERTEX_SHADER, VS_BILLBOARD);
    GLuint fsBill = compileShader(GL_FRAGMENT_SHADER, FS_BILLBOARD);
    GLuint progBill = linkProgram(vsBill, fsBill);

    if (!progBase || !progFoggy || !progIco || !progBill) {
        fprintf(stderr, "Shader compilation failed\n");
        cleanup(state);
        return 1;
    }
    printf("  All programs compiled.\n\n");

    // ---- Build icosphere meshes ----
    std::vector<SimpleVertex> icoVerts;
    std::vector<uint16_t> icoIndices;
    for (int i = 0; i < NUM_EMITTERS; i++) {
        buildIcosphere(emitters[i].x, emitters[i].y, emitters[i].z, emitters[i].radius,
                       emitters[i].r, emitters[i].g, emitters[i].b, emitters[i].intensity,
                       1, icoVerts, icoIndices);  // 1 subdivision = 80 tris per sphere
    }
    GLuint icoVbo, icoIbo;
    glGenBuffers(1, &icoVbo); glBindBuffer(GL_ARRAY_BUFFER, icoVbo);
    glBufferData(GL_ARRAY_BUFFER, icoVerts.size()*sizeof(SimpleVertex), icoVerts.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &icoIbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icoIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, icoIndices.size()*sizeof(uint16_t), icoIndices.data(), GL_STATIC_DRAW);
    printf("Icospheres: %d verts, %d tris (%d spheres)\n", (int)icoVerts.size(), (int)icoIndices.size()/3, NUM_EMITTERS);

    // ---- Build billboard quad ----
    BillboardVertex billVerts[4] = {
        {{-1,-1,0}, {0,0}}, {{1,-1,0}, {1,0}}, {{-1,1,0}, {0,1}}, {{1,1,0}, {1,1}},
    };
    uint16_t billIndices[6] = {0,1,2, 1,3,2};
    GLuint billVbo, billIbo;
    glGenBuffers(1, &billVbo); glBindBuffer(GL_ARRAY_BUFFER, billVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(billVerts), billVerts, GL_STATIC_DRAW);
    glGenBuffers(1, &billIbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, billIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(billIndices), billIndices, GL_STATIC_DRAW);
    GLuint gradientTex = createRadialGradientTexture(64);

    // ============================================================
    // RENDER 1: Baseline (zone only)
    // ============================================================
    printf("\n--- 1. Baseline (zone only) ---\n");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(progBase);
    setZoneUniforms(progBase, viewProj);
    drawZone(progBase, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);
    glFinish();
    saveScreenshot("fog_baseline.ppm", state.width, state.height);

    FrameTimer timer{state, numFrames};
    double baselineMs = timer.timeFrames([&](){
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(progBase); setZoneUniforms(progBase, viewProj);
        drawZone(progBase, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);
    });
    printf("  avg: %.2f ms (~%.0f FPS)\n", baselineMs, 1000.0/baselineMs);

    // ============================================================
    // RENDER 2: Icosphere mesh fog
    // ============================================================
    printf("\n--- 2. Icosphere mesh fog ---\n");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(progBase); setZoneUniforms(progBase, viewProj);
    drawZone(progBase, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);

    // Draw icospheres (additive blend, no depth write)
    glUseProgram(progIco);
    GLint locVP = glGetUniformLocation(progIco, "uViewProj");
    if (locVP >= 0) glUniformMatrix4fv(locVP, 1, GL_FALSE, viewProj);

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE); glEnable(GL_DEPTH_TEST);

    glBindBuffer(GL_ARRAY_BUFFER, icoVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icoIbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)offsetof(SimpleVertex, pos));
    glEnableVertexAttribArray(2);  // aColor at location 2
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)offsetof(SimpleVertex, color));
    glDrawElements(GL_TRIANGLES, (int)icoIndices.size(), GL_UNSIGNED_SHORT, nullptr);
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(2);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);

    glFinish();
    saveScreenshot("fog_icosphere.ppm", state.width, state.height);

    double icoMs = timer.timeFrames([&](){
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(progBase); setZoneUniforms(progBase, viewProj);
        drawZone(progBase, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);
        glUseProgram(progIco);
        if (locVP >= 0) glUniformMatrix4fv(locVP, 1, GL_FALSE, viewProj);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glBindBuffer(GL_ARRAY_BUFFER, icoVbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, icoIbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)offsetof(SimpleVertex, pos));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), (void*)offsetof(SimpleVertex, color));
        glDrawElements(GL_TRIANGLES, (int)icoIndices.size(), GL_UNSIGNED_SHORT, nullptr);
        glDisableVertexAttribArray(0); glDisableVertexAttribArray(2);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    });
    printf("  avg: %.2f ms (~%.0f FPS)  delta: %+.2f ms\n", icoMs, 1000.0/icoMs, icoMs-baselineMs);

    // ============================================================
    // RENDER 3: Textured billboard fog
    // ============================================================
    printf("\n--- 3. Textured billboard fog ---\n");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(progBase); setZoneUniforms(progBase, viewProj);
    drawZone(progBase, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);

    // Draw billboards
    glUseProgram(progBill);
    GLint bLocVP = glGetUniformLocation(progBill, "uViewProj");
    GLint bLocCenter = glGetUniformLocation(progBill, "uCenter");
    GLint bLocRadius = glGetUniformLocation(progBill, "uRadius");
    GLint bLocRight = glGetUniformLocation(progBill, "uCameraRight");
    GLint bLocUp = glGetUniformLocation(progBill, "uCameraUp");
    GLint bLocColor = glGetUniformLocation(progBill, "uColor");
    GLint bLocIntensity = glGetUniformLocation(progBill, "uIntensity");
    GLint bLocTex = glGetUniformLocation(progBill, "uTexture");

    if (bLocVP >= 0) glUniformMatrix4fv(bLocVP, 1, GL_FALSE, viewProj);
    if (bLocRight >= 0) glUniform3fv(bLocRight, 1, camRight);
    if (bLocUp >= 0) glUniform3fv(bLocUp, 1, camUp);
    if (bLocTex >= 0) glUniform1i(bLocTex, 0);

    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE); glEnable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gradientTex);

    glBindBuffer(GL_ARRAY_BUFFER, billVbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, billIbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex), (void*)offsetof(BillboardVertex, pos));
    glEnableVertexAttribArray(3);  // aTexCoord0 at location 3
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex), (void*)offsetof(BillboardVertex, uv));

    for (int i = 0; i < NUM_EMITTERS; i++) {
        float center[] = {emitters[i].x, emitters[i].y, emitters[i].z};
        float color[] = {emitters[i].r, emitters[i].g, emitters[i].b};
        if (bLocCenter >= 0) glUniform3fv(bLocCenter, 1, center);
        if (bLocRadius >= 0) glUniform1f(bLocRadius, emitters[i].radius);
        if (bLocColor >= 0) glUniform3fv(bLocColor, 1, color);
        if (bLocIntensity >= 0) glUniform1f(bLocIntensity, emitters[i].intensity);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }
    glDisableVertexAttribArray(0); glDisableVertexAttribArray(3);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);

    glFinish();
    saveScreenshot("fog_billboard.ppm", state.width, state.height);

    double billMs = timer.timeFrames([&](){
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(progBase); setZoneUniforms(progBase, viewProj);
        drawZone(progBase, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);
        glUseProgram(progBill);
        if (bLocVP >= 0) glUniformMatrix4fv(bLocVP, 1, GL_FALSE, viewProj);
        if (bLocRight >= 0) glUniform3fv(bLocRight, 1, camRight);
        if (bLocUp >= 0) glUniform3fv(bLocUp, 1, camUp);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, gradientTex);
        glBindBuffer(GL_ARRAY_BUFFER, billVbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, billIbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex), (void*)offsetof(BillboardVertex, pos));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex), (void*)offsetof(BillboardVertex, uv));
        for (int i = 0; i < NUM_EMITTERS; i++) {
            float center[] = {emitters[i].x, emitters[i].y, emitters[i].z};
            float color[] = {emitters[i].r, emitters[i].g, emitters[i].b};
            if (bLocCenter >= 0) glUniform3fv(bLocCenter, 1, center);
            if (bLocRadius >= 0) glUniform1f(bLocRadius, emitters[i].radius);
            if (bLocColor >= 0) glUniform3fv(bLocColor, 1, color);
            if (bLocIntensity >= 0) glUniform1f(bLocIntensity, emitters[i].intensity);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
        }
        glDisableVertexAttribArray(0); glDisableVertexAttribArray(3);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    });
    printf("  avg: %.2f ms (~%.0f FPS)  delta: %+.2f ms\n", billMs, 1000.0/billMs, billMs-baselineMs);

    // ============================================================
    // RENDER 4: Per-vertex zone fog
    // ============================================================
    printf("\n--- 4. Per-vertex zone fog ---\n");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(progFoggy);
    setZoneUniforms(progFoggy, viewProj);

    // Set fog emitter uniforms
    GLint loc;
    for (int i = 0; i < NUM_EMITTERS; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "uFogEmitterPos[%d]", i);
        if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) {
            float pos[] = {emitters[i].x, emitters[i].y, emitters[i].z};
            glUniform3fv(loc, 1, pos);
        }
        snprintf(buf, sizeof(buf), "uFogEmitterColor[%d]", i);
        if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) {
            float col[] = {emitters[i].r, emitters[i].g, emitters[i].b};
            glUniform3fv(loc, 1, col);
        }
        snprintf(buf, sizeof(buf), "uFogEmitterRadius[%d]", i);
        if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) glUniform1f(loc, emitters[i].radius);
        snprintf(buf, sizeof(buf), "uFogEmitterIntensity[%d]", i);
        if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) glUniform1f(loc, emitters[i].intensity);
    }
    if ((loc = glGetUniformLocation(progFoggy, "uNumFogEmitters")) >= 0) glUniform1i(loc, NUM_EMITTERS);

    drawZone(progFoggy, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);
    glFinish();
    saveScreenshot("fog_pervertex.ppm", state.width, state.height);

    double pvMs = timer.timeFrames([&](){
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(progFoggy); setZoneUniforms(progFoggy, viewProj);
        for (int i = 0; i < NUM_EMITTERS; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "uFogEmitterPos[%d]", i);
            if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) {
                float pos[] = {emitters[i].x, emitters[i].y, emitters[i].z};
                glUniform3fv(loc, 1, pos);
            }
            snprintf(buf, sizeof(buf), "uFogEmitterColor[%d]", i);
            if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) {
                float col[] = {emitters[i].r, emitters[i].g, emitters[i].b};
                glUniform3fv(loc, 1, col);
            }
            snprintf(buf, sizeof(buf), "uFogEmitterRadius[%d]", i);
            if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) glUniform1f(loc, emitters[i].radius);
            snprintf(buf, sizeof(buf), "uFogEmitterIntensity[%d]", i);
            if ((loc = glGetUniformLocation(progFoggy, buf)) >= 0) glUniform1f(loc, emitters[i].intensity);
        }
        if ((loc = glGetUniformLocation(progFoggy, "uNumFogEmitters")) >= 0) glUniform1i(loc, NUM_EMITTERS);
        drawZone(progFoggy, zoneVbo, zoneIbo, (int)zoneIndices.size(), checkerTex);
    });
    printf("  avg: %.2f ms (~%.0f FPS)  delta: %+.2f ms\n", pvMs, 1000.0/pvMs, pvMs-baselineMs);

    // ============================================================
    // Summary
    // ============================================================
    printf("\n============================================================\n");
    printf("SUMMARY\n");
    printf("============================================================\n\n");
    printf("%-30s  %7s  %7s  %10s\n", "Approach", "ms", "FPS", "delta");
    printf("%-30s  %7s  %7s  %10s\n", "--------", "--", "---", "-----");
    printf("%-30s  %7.2f  %7.0f  %10s\n", "1. Baseline (zone only)", baselineMs, 1000.0/baselineMs, "-");
    printf("%-30s  %7.2f  %7.0f  %+9.2fms\n", "2. Icosphere mesh", icoMs, 1000.0/icoMs, icoMs-baselineMs);
    printf("%-30s  %7.2f  %7.0f  %+9.2fms\n", "3. Textured billboard", billMs, 1000.0/billMs, billMs-baselineMs);
    printf("%-30s  %7.2f  %7.0f  %+9.2fms\n", "4. Per-vertex zone fog", pvMs, 1000.0/pvMs, pvMs-baselineMs);
    printf("\nScreenshots saved: fog_baseline.ppm, fog_icosphere.ppm, fog_billboard.ppm, fog_pervertex.ppm\n");

    // Cleanup
    glDeleteProgram(progBase); glDeleteProgram(progFoggy);
    glDeleteProgram(progIco); glDeleteProgram(progBill);
    glDeleteShader(vsBase); glDeleteShader(fsBase); glDeleteShader(vsFoggy);
    glDeleteShader(vsIco); glDeleteShader(fsIco);
    glDeleteShader(vsBill); glDeleteShader(fsBill);
    glDeleteTextures(1, &checkerTex); glDeleteTextures(1, &gradientTex);
    glDeleteBuffers(1, &zoneVbo); glDeleteBuffers(1, &zoneIbo);
    glDeleteBuffers(1, &icoVbo); glDeleteBuffers(1, &icoIbo);
    glDeleteBuffers(1, &billVbo); glDeleteBuffers(1, &billIbo);

    cleanup(state);
    return 0;
}
