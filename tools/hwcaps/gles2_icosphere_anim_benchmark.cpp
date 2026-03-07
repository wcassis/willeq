// gles2_icosphere_anim_benchmark - Benchmark animated icosphere fog volumes on GLES2
//
// Tests the cost of various animation techniques for icosphere fog volumes
// on Mali 400 hardware. All animations use the VS (free on Mali 400) or
// CPU-side uniform updates — no FS work beyond trivial vColor output.
//
// Phases:
//   1. Static baseline (uniform-only, no animation)
//   2. Scale pulsing (uniform radius oscillation per frame)
//   3. Color flicker (CPU random intensity per emitter per frame)
//   4. Vertex jitter (VS displaces verts along normals, time uniform)
//   5. Combined (scale + flicker + jitter together)
//   6. VBO re-upload (CPU rebuilds vertex colors every frame — worst case)
//   7. Sphere count scaling (4, 8, 16 spheres with combined animation)
//   8. Subdivision scaling (80 vs 320 tris/sphere with combined animation)
//
// All tests run zone + icospheres combined to measure realistic frame cost.
//
// Usage:
//   ./gles2_icosphere_anim_benchmark              # auto-detect
//   ./gles2_icosphere_anim_benchmark --drm        # force DRM/GBM
//   ./gles2_icosphere_anim_benchmark --x11        # force X11/EGL
//   ./gles2_icosphere_anim_benchmark --frames N   # frames per test (default 300)
//   ./gles2_icosphere_anim_benchmark --res W H    # resolution (default 1280 720)

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
    for (int i = 0; devices[i]; i++) { state.drmFd = open(devices[i], O_RDWR); if (state.drmFd >= 0) break; }
    if (state.drmFd < 0) { fprintf(stderr, "DRM: could not open device\n"); return false; }
    drmModeRes* res = drmModeGetResources(state.drmFd);
    if (!res) { close(state.drmFd); return false; }
    for (int i = 0; i < res->count_connectors && !state.modeFound; i++) {
        drmModeConnector* conn = drmModeGetConnector(state.drmFd, res->connectors[i]);
        if (!conn || conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) { if (conn) drmModeFreeConnector(conn); continue; }
        state.connectorId = conn->connector_id;
        for (int m = 0; m < conn->count_modes; m++) {
            if ((int)conn->modes[m].hdisplay == state.width && (int)conn->modes[m].vdisplay == state.height) { state.mode = conn->modes[m]; state.modeFound = true; break; }
        }
        if (!state.modeFound && conn->count_modes > 0) { state.mode = conn->modes[0]; state.width = state.mode.hdisplay; state.height = state.mode.vdisplay; state.modeFound = true; }
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
    EGLint configAttribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
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
    uint32_t handle = gbm_bo_get_handle(bo).u32, stride = gbm_bo_get_stride(bo), fb = 0;
    drmModeAddFB(state.drmFd, state.width, state.height, 24, 32, stride, handle, &fb);
    static bool first = true;
    if (first) { drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode); first = false; }
    else { int ret = drmModePageFlip(state.drmFd, state.crtcId, fb, 0, nullptr); if (ret) drmModeSetCrtc(state.drmFd, state.crtcId, fb, 0, 0, &state.connectorId, 1, &state.mode); }
    static struct gbm_bo* prevBo = nullptr; static uint32_t prevFb = 0;
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
    EGLint configAttribs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
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
    if (state.isDRM) { if (state.gbmSurface) gbm_surface_destroy(state.gbmSurface); if (state.gbmDevice) gbm_device_destroy(state.gbmDevice); if (state.drmFd >= 0) close(state.drmFd); }
#endif
}

// ============================================================================
// Shader compilation
// ============================================================================

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log); fprintf(stderr, "Shader compile error:\n%s\n", log); glDeleteShader(shader); return 0; }
    return shader;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    glBindAttribLocation(prog, 1, "aNormal");
    glBindAttribLocation(prog, 2, "aColor");
    glBindAttribLocation(prog, 3, "aTexCoord0");
    glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(prog, sizeof(log), nullptr, log); fprintf(stderr, "Program link error:\n%s\n", log); glDeleteProgram(prog); return 0; }
    return prog;
}

// ============================================================================
// Matrix math
// ============================================================================

static void mat4Identity(float m[16]) { memset(m, 0, 64); m[0]=m[5]=m[10]=m[15]=1.0f; }

static void mat4Perspective(float m[16], float fovDeg, float aspect, float near, float far) {
    memset(m, 0, 64); float f = 1.0f / tanf(fovDeg * 3.14159f / 360.0f);
    m[0]=f/aspect; m[5]=f; m[10]=(far+near)/(near-far); m[11]=-1.0f; m[14]=(2.0f*far*near)/(near-far);
}

static void mat4LookAt(float m[16], float ex, float ey, float ez, float ax, float ay, float az, float ux, float uy, float uz) {
    float fx=ax-ex, fy=ay-ey, fz=az-ez;
    float fl=sqrtf(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
    float sx=fy*uz-fz*uy, sy=fz*ux-fx*uz, sz=fx*uy-fy*ux;
    float sl=sqrtf(sx*sx+sy*sy+sz*sz); sx/=sl; sy/=sl; sz/=sl;
    float uux=sy*fz-sz*fy, uuy=sz*fx-sx*fz, uuz=sx*fy-sy*fx;
    mat4Identity(m);
    m[0]=sx; m[4]=sy; m[8]=sz; m[1]=uux; m[5]=uuy; m[9]=uuz; m[2]=-fx; m[6]=-fy; m[10]=-fz;
    m[12]=-(sx*ex+sy*ey+sz*ez); m[13]=-(uux*ex+uuy*ey+uuz*ez); m[14]=(fx*ex+fy*ey+fz*ez);
}

static void mat4Multiply(float out[16], const float a[16], const float b[16]) {
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { out[i+j*4] = 0; for (int k = 0; k < 4; k++) out[i+j*4] += a[i+k*4] * b[k+j*4]; }
}

// ============================================================================
// Zone scene geometry (same alley as other benchmarks)
// ============================================================================

struct Vertex {
    float pos[3];
    float normal[3];
    float color[4];
    float uv[2];
};

static void buildSubdividedQuad(float x0, float y0, float z0, float x1, float y1, float z1,
    float x2, float y2, float z2, float nx, float ny, float nz,
    int divU, int divV, std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    float x3=x1+(x2-x0), y3=y1+(y2-y0), z3=z1+(z2-z0);
    uint16_t base = (uint16_t)verts.size();
    for (int v = 0; v <= divV; v++) { float tv = (float)v/divV;
        for (int u = 0; u <= divU; u++) { float tu = (float)u/divU;
            Vertex vert;
            vert.pos[0]=(1-tu)*(1-tv)*x0+tu*(1-tv)*x1+(1-tu)*tv*x2+tu*tv*x3;
            vert.pos[1]=(1-tu)*(1-tv)*y0+tu*(1-tv)*y1+(1-tu)*tv*y2+tu*tv*y3;
            vert.pos[2]=(1-tu)*(1-tv)*z0+tu*(1-tv)*z1+(1-tu)*tv*z2+tu*tv*z3;
            vert.normal[0]=nx; vert.normal[1]=ny; vert.normal[2]=nz;
            vert.color[0]=0.8f; vert.color[1]=0.8f; vert.color[2]=0.8f; vert.color[3]=1.0f;
            vert.uv[0]=tu; vert.uv[1]=tv;
            verts.push_back(vert);
        }
    }
    for (int v = 0; v < divV; v++) for (int u = 0; u < divU; u++) {
        uint16_t i0=base+v*(divU+1)+u, i1=i0+1, i2=i0+(divU+1), i3=i2+1;
        indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
        indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
    }
}

static void buildAlleyScene(std::vector<Vertex>& verts, std::vector<uint16_t>& indices) {
    float w=3.0f, d=20.0f, h=8.0f; int div=4;
    buildSubdividedQuad(-w,0,0, w,0,0, -w,0,d, 0,1,0, div,div*2, verts,indices);
    buildSubdividedQuad(-w,0,0, -w,0,d, -w,h,0, 1,0,0, div*2,div, verts,indices);
    buildSubdividedQuad(w,0,d, w,0,0, w,h,d, -1,0,0, div*2,div, verts,indices);
    buildSubdividedQuad(w,0,d, -w,0,d, w,h,d, 0,0,-1, div,div, verts,indices);
    buildSubdividedQuad(-w,h,d, w,h,d, -w,h,0, 0,-1,0, div,div*2, verts,indices);
}

static GLuint createCheckerTexture(int size) {
    std::vector<uint8_t> pixels(size*size*3);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        bool check = ((x/(size/8))+(y/(size/8)))%2==0;
        uint8_t val = check ? 180 : 100; int i=(y*size+x)*3;
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
// Icosphere mesh builder
// ============================================================================

struct IcoVertex {
    float pos[3];     // position (relative to sphere center)
    float normal[3];  // unit normal (for jitter displacement)
    float color[4];   // RGBA
};

struct IcoSphere {
    std::vector<IcoVertex> verts;
    std::vector<uint16_t> indices;
    int vertOffset;  // offset into combined VBO
    int indexOffset;  // offset into combined IBO
    int indexCount;
};

struct FogEmitter {
    float x, y, z;
    float r, g, b;
    float radius;
    float intensity;
};

static void buildIcosphere(int subdivisions, std::vector<IcoVertex>& verts, std::vector<uint16_t>& indices) {
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    float icoV[][3] = {
        {-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},
        {0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
        {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1},
    };
    for (int i = 0; i < 12; i++) {
        float l = sqrtf(icoV[i][0]*icoV[i][0]+icoV[i][1]*icoV[i][1]+icoV[i][2]*icoV[i][2]);
        icoV[i][0]/=l; icoV[i][1]/=l; icoV[i][2]/=l;
    }
    int icoI[] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };
    struct Tri { int i0, i1, i2; };
    std::vector<float> pts;
    for (int i = 0; i < 12; i++) { pts.push_back(icoV[i][0]); pts.push_back(icoV[i][1]); pts.push_back(icoV[i][2]); }
    std::vector<Tri> tris;
    for (int i = 0; i < 20; i++) tris.push_back({icoI[i*3], icoI[i*3+1], icoI[i*3+2]});

    for (int s = 0; s < subdivisions; s++) {
        std::vector<Tri> newTris;
        for (const auto& tri : tris) {
            auto mid = [&](int a, int b) -> int {
                int idx = (int)pts.size()/3;
                float mx=(pts[a*3]+pts[b*3])*0.5f, my=(pts[a*3+1]+pts[b*3+1])*0.5f, mz=(pts[a*3+2]+pts[b*3+2])*0.5f;
                float l=sqrtf(mx*mx+my*my+mz*mz); pts.push_back(mx/l); pts.push_back(my/l); pts.push_back(mz/l);
                return idx;
            };
            int m01=mid(tri.i0,tri.i1), m12=mid(tri.i1,tri.i2), m20=mid(tri.i2,tri.i0);
            newTris.push_back({tri.i0,m01,m20}); newTris.push_back({tri.i1,m12,m01});
            newTris.push_back({tri.i2,m20,m12}); newTris.push_back({m01,m12,m20});
        }
        tris = newTris;
    }

    uint16_t base = (uint16_t)verts.size();
    int numPts = (int)pts.size()/3;
    for (int i = 0; i < numPts; i++) {
        IcoVertex v;
        v.pos[0] = pts[i*3]; v.pos[1] = pts[i*3+1]; v.pos[2] = pts[i*3+2];
        v.normal[0] = pts[i*3]; v.normal[1] = pts[i*3+1]; v.normal[2] = pts[i*3+2]; // unit sphere: pos == normal
        v.color[0]=1.0f; v.color[1]=1.0f; v.color[2]=1.0f; v.color[3]=1.0f; // set per-instance later
        verts.push_back(v);
    }
    for (const auto& tri : tris) {
        indices.push_back(base+tri.i0); indices.push_back(base+tri.i1); indices.push_back(base+tri.i2);
    }
}

// ============================================================================
// Shaders
// ============================================================================

// Zone shader (lightweight, same as other benchmarks)
static const char* VS_ZONE = R"(
precision highp float;
attribute vec3 aPosition; attribute vec3 aNormal; attribute vec4 aColor; attribute vec2 aTexCoord0;
uniform mat4 mWorldViewProj; uniform mat4 mWorld;
uniform vec3 uSunDir; uniform vec3 uSunColor; uniform vec3 uAmbientColor; uniform vec3 uTintColor;
uniform float uFogStart; uniform float uFogEnd;
uniform vec3 uLightPos[8]; uniform vec3 uLightColor[8]; uniform vec3 uLightAtten[8];
varying vec4 vColor; varying vec2 vTexCoord; varying float vFogFactor;
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
static const char* FS_ZONE = R"(
precision mediump float;
uniform sampler2D uTexture; uniform vec4 uFogColor;
varying vec4 vColor; varying vec2 vTexCoord; varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    gl_FragColor = mix(uFogColor, texColor * vColor, vFogFactor);
}
)";

// Static icosphere shader (no animation — baseline)
static const char* VS_ICO_STATIC = R"(
precision highp float;
attribute vec3 aPosition;  // unit sphere position
attribute vec3 aNormal;    // unit normal (same as position for unit sphere)
attribute vec4 aColor;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;

varying vec4 vColor;

void main() {
    vec3 worldPos = uCenter + aPosition * uRadius;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vColor = aColor;
}
)";

// Scale pulsing: radius varies per frame via uniform
// (same shader as static — scale is just a different uRadius value)

// Vertex jitter: displace along normal by time-varying amount
static const char* VS_ICO_JITTER = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uTime;
uniform float uJitterAmount;  // max displacement as fraction of radius

varying vec4 vColor;

void main() {
    // Per-vertex jitter: use position components to create pseudo-random phase
    float phase = aPosition.x * 7.13 + aPosition.y * 11.37 + aPosition.z * 5.79;
    float jitter = sin(uTime * 4.0 + phase) * uJitterAmount;

    vec3 worldPos = uCenter + aPosition * uRadius * (1.0 + jitter) ;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vColor = aColor;
}
)";

// Trivial FS for all icosphere variants
static const char* FS_ICO = R"(
precision mediump float;
varying vec4 vColor;
void main() { gl_FragColor = vColor; }
)";

// ============================================================================
// Simple xorshift PRNG for flicker
// ============================================================================

static uint32_t xorshift32(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static float randomFloat(uint32_t& state) {
    return (float)(xorshift32(state) & 0xFFFF) / 65535.0f;
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
        else if (strcmp(argv[i], "--res") == 0 && i+2 < argc) { width = atoi(argv[++i]); height = atoi(argv[++i]); }
    }

    printf("=== GLES2 Icosphere Animation Benchmark ===\n");
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

    float aspect = (float)state.width / state.height;
    glViewport(0, 0, state.width, state.height);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    // Camera
    float proj[16], view[16], viewProj[16];
    mat4Perspective(proj, 60.0f, aspect, 0.1f, 100.0f);
    mat4LookAt(view, 0.0f, 1.6f, 1.0f, 0.0f, 1.6f, 10.0f, 0.0f, 1.0f, 0.0f);
    mat4Multiply(viewProj, proj, view);

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

    // ---- Build icosphere templates (subdivision 1 and 2) ----
    std::vector<IcoVertex> icoVerts1, icoVerts2;
    std::vector<uint16_t> icoIndices1, icoIndices2;
    buildIcosphere(1, icoVerts1, icoIndices1);  // 80 tris
    buildIcosphere(2, icoVerts2, icoIndices2);  // 320 tris
    printf("Icosphere subdiv 1: %d verts, %d tris\n", (int)icoVerts1.size(), (int)icoIndices1.size()/3);
    printf("Icosphere subdiv 2: %d verts, %d tris\n", (int)icoVerts2.size(), (int)icoIndices2.size()/3);

    // ---- Emitter definitions ----
    // We define up to 16 emitters along the alley for scaling tests
    FogEmitter allEmitters[] = {
        { 0.0f, 1.5f,  3.0f,  1.0f, 0.6f, 0.2f,  3.0f, 1.2f},
        {-2.5f, 3.5f,  6.0f,  1.0f, 0.7f, 0.3f,  2.5f, 0.8f},
        { 2.5f, 3.5f,  9.0f,  1.0f, 0.7f, 0.3f,  2.5f, 0.8f},
        { 0.0f, 2.0f, 12.0f,  1.0f, 0.5f, 0.15f, 3.5f, 1.0f},
        {-2.5f, 3.5f, 14.0f,  1.0f, 0.7f, 0.3f,  2.5f, 0.8f},
        { 1.5f, 1.0f, 16.0f,  0.9f, 0.4f, 0.1f,  2.0f, 0.9f},
        { 0.0f, 6.5f, 18.0f,  0.8f, 0.8f, 0.6f,  2.0f, 0.7f},
        { 0.0f, 2.0f, 19.0f,  1.0f, 0.5f, 0.15f, 3.0f, 1.0f},
        // Extra 8 for 16-sphere test (mirrored and offset)
        { 0.0f, 1.5f,  4.0f,  0.8f, 0.5f, 0.15f, 2.5f, 0.9f},
        {-2.0f, 3.0f,  7.0f,  1.0f, 0.6f, 0.25f, 2.0f, 0.7f},
        { 2.0f, 3.0f, 10.0f,  1.0f, 0.6f, 0.25f, 2.0f, 0.7f},
        { 0.0f, 1.5f, 13.0f,  0.9f, 0.4f, 0.1f,  2.5f, 0.8f},
        {-2.0f, 3.5f, 15.0f,  1.0f, 0.7f, 0.3f,  2.0f, 0.7f},
        { 2.0f, 1.5f, 17.0f,  0.9f, 0.5f, 0.2f,  2.5f, 0.8f},
        {-1.0f, 5.0f, 18.5f,  0.8f, 0.7f, 0.5f,  2.0f, 0.6f},
        { 1.0f, 2.0f, 19.5f,  1.0f, 0.5f, 0.15f, 2.5f, 0.9f},
    };

    // ---- Compile shaders ----
    printf("\nCompiling shaders...\n");
    GLuint vsZone = compileShader(GL_VERTEX_SHADER, VS_ZONE);
    GLuint fsZone = compileShader(GL_FRAGMENT_SHADER, FS_ZONE);
    GLuint progZone = linkProgram(vsZone, fsZone);

    GLuint vsIcoStatic = compileShader(GL_VERTEX_SHADER, VS_ICO_STATIC);
    GLuint fsIco = compileShader(GL_FRAGMENT_SHADER, FS_ICO);
    GLuint progIcoStatic = linkProgram(vsIcoStatic, fsIco);

    GLuint vsIcoJitter = compileShader(GL_VERTEX_SHADER, VS_ICO_JITTER);
    GLuint progIcoJitter = linkProgram(vsIcoJitter, fsIco);

    if (!progZone || !progIcoStatic || !progIcoJitter) {
        fprintf(stderr, "Shader compilation failed\n");
        cleanup(state); return 1;
    }
    printf("  All programs compiled.\n\n");

    // ---- Helper: build instanced icosphere VBO for N emitters ----
    // Sets vertex colors based on emitter color/intensity
    auto buildInstancedIco = [&](const FogEmitter* emitters, int numEmitters, int subdivisions,
                                  GLuint& vbo, GLuint& ibo, int& totalIndices,
                                  std::vector<IcoVertex>& outVerts) {
        auto& templateVerts = (subdivisions == 1) ? icoVerts1 : icoVerts2;
        auto& templateIndices = (subdivisions == 1) ? icoIndices1 : icoIndices2;
        int vertsPerSphere = (int)templateVerts.size();
        int indicesPerSphere = (int)templateIndices.size();

        outVerts.clear();
        std::vector<uint16_t> allIndices;

        for (int e = 0; e < numEmitters; e++) {
            uint16_t base = (uint16_t)outVerts.size();
            float alpha = 0.15f * emitters[e].intensity;
            for (int i = 0; i < vertsPerSphere; i++) {
                IcoVertex v = templateVerts[i];
                // Keep unit-sphere positions — we'll transform via uniforms per draw call
                v.color[0] = emitters[e].r * emitters[e].intensity;
                v.color[1] = emitters[e].g * emitters[e].intensity;
                v.color[2] = emitters[e].b * emitters[e].intensity;
                v.color[3] = alpha;
                outVerts.push_back(v);
            }
            for (int i = 0; i < indicesPerSphere; i++) {
                allIndices.push_back(base + templateIndices[i]);
            }
        }

        totalIndices = (int)allIndices.size();
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, outVerts.size()*sizeof(IcoVertex), outVerts.data(), GL_DYNAMIC_DRAW);
        glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size()*sizeof(uint16_t), allIndices.data(), GL_STATIC_DRAW);
    };

    // ---- Helper: draw zone ----
    auto drawZone = [&]() {
        glUseProgram(progZone);
        float world[16]; mat4Identity(world);
        GLint loc;
        if ((loc=glGetUniformLocation(progZone,"mWorldViewProj"))>=0) glUniformMatrix4fv(loc,1,GL_FALSE,viewProj);
        if ((loc=glGetUniformLocation(progZone,"mWorld"))>=0) glUniformMatrix4fv(loc,1,GL_FALSE,world);
        float sunDir[]={0,-1,0}, sunColor[]={0,0,0}, ambient[]={0.05f,0.05f,0.08f}, tint[]={0.3f,0.3f,0.4f};
        if ((loc=glGetUniformLocation(progZone,"uSunDir"))>=0) glUniform3fv(loc,1,sunDir);
        if ((loc=glGetUniformLocation(progZone,"uSunColor"))>=0) glUniform3fv(loc,1,sunColor);
        if ((loc=glGetUniformLocation(progZone,"uAmbientColor"))>=0) glUniform3fv(loc,1,ambient);
        if ((loc=glGetUniformLocation(progZone,"uTintColor"))>=0) glUniform3fv(loc,1,tint);
        if ((loc=glGetUniformLocation(progZone,"uFogStart"))>=0) glUniform1f(loc,50.0f);
        if ((loc=glGetUniformLocation(progZone,"uFogEnd"))>=0) glUniform1f(loc,200.0f);
        float fogColor[]={0.1f,0.1f,0.15f,1.0f};
        if ((loc=glGetUniformLocation(progZone,"uFogColor"))>=0) glUniform4fv(loc,1,fogColor);
        if ((loc=glGetUniformLocation(progZone,"uTexture"))>=0) glUniform1i(loc,0);
        float lp[24]={},lc[24]={},la[24]={};
        for (int i=0;i<4;i++){lp[i*3]=allEmitters[i].x;lp[i*3+1]=allEmitters[i].y;lp[i*3+2]=allEmitters[i].z;
            lc[i*3]=allEmitters[i].r*0.8f;lc[i*3+1]=allEmitters[i].g*0.8f;lc[i*3+2]=allEmitters[i].b*0.8f;
            la[i*3]=1.0f;la[i*3+2]=19.0f/(40.0f*40.0f);}
        for(int i=4;i<8;i++) la[i*3]=1.0f;
        if ((loc=glGetUniformLocation(progZone,"uLightPos[0]"))>=0) glUniform3fv(loc,8,lp);
        if ((loc=glGetUniformLocation(progZone,"uLightColor[0]"))>=0) glUniform3fv(loc,8,lc);
        if ((loc=glGetUniformLocation(progZone,"uLightAtten[0]"))>=0) glUniform3fv(loc,8,la);

        glBindBuffer(GL_ARRAY_BUFFER, zoneVbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zoneIbo);
        int stride=sizeof(Vertex);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(Vertex,pos));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(Vertex,normal));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(Vertex,color));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(Vertex,uv));
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, checkerTex);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glDrawElements(GL_TRIANGLES, (int)zoneIndices.size(), GL_UNSIGNED_SHORT, nullptr);
        glDisableVertexAttribArray(0); glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2); glDisableVertexAttribArray(3);
    };

    // ---- Helper: draw icospheres (per-emitter draw calls with individual uniforms) ----
    auto drawIcos = [&](GLuint program, GLuint vbo, GLuint ibo,
                         const FogEmitter* emitters, int numEmitters, int subdivisions,
                         float time, bool doJitter, float scaleMultiplier,
                         const float* flickerIntensities) {
        auto& templateVerts = (subdivisions == 1) ? icoVerts1 : icoVerts2;
        auto& templateIndices = (subdivisions == 1) ? icoIndices1 : icoIndices2;
        int vertsPerSphere = (int)templateVerts.size();
        int indicesPerSphere = (int)templateIndices.size();

        glUseProgram(program);
        GLint locVP = glGetUniformLocation(program, "uViewProj");
        GLint locCenter = glGetUniformLocation(program, "uCenter");
        GLint locRadius = glGetUniformLocation(program, "uRadius");
        GLint locTime = glGetUniformLocation(program, "uTime");
        GLint locJitter = glGetUniformLocation(program, "uJitterAmount");

        if (locVP >= 0) glUniformMatrix4fv(locVP, 1, GL_FALSE, viewProj);
        if (locTime >= 0) glUniform1f(locTime, time);
        if (locJitter >= 0) glUniform1f(locJitter, doJitter ? 0.08f : 0.0f);

        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE); glEnable(GL_DEPTH_TEST);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        int stride = sizeof(IcoVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(IcoVertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(IcoVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(IcoVertex, color));

        for (int e = 0; e < numEmitters; e++) {
            float center[] = {emitters[e].x, emitters[e].y, emitters[e].z};
            float r = emitters[e].radius * scaleMultiplier;
            if (flickerIntensities) r *= flickerIntensities[e];
            if (locCenter >= 0) glUniform3fv(locCenter, 1, center);
            if (locRadius >= 0) glUniform1f(locRadius, r);
            int indexOff = e * indicesPerSphere;
            glDrawElements(GL_TRIANGLES, indicesPerSphere, GL_UNSIGNED_SHORT, (void*)(intptr_t)(indexOff * sizeof(uint16_t)));
        }

        glDisableVertexAttribArray(0); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    };

    // ---- Helper: run a timed benchmark ----
    auto runBench = [&](auto drawFunc) -> BenchResult {
        for (int i = 0; i < 5; i++) { drawFunc(0.0f); glFinish(); }
        std::vector<double> times;
        times.reserve(numFrames);
        for (int i = 0; i < numFrames; i++) {
            float t = (float)i * 0.016f;  // simulate ~60fps timing
            auto t0 = std::chrono::steady_clock::now();
            drawFunc(t);
            glFinish();
#ifdef EQT_HAS_DRM
            if (state.isDRM) drmPresent(state);
#endif
            auto t1 = std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        return computeStats(times);
    };

    // ==================================================================
    // PHASE 1: Zone-only baseline
    // ==================================================================
    printf("============================================================\n");
    printf("PHASE 1: Zone-only baseline\n");
    printf("============================================================\n");
    auto baseR = runBench([&](float) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();
    });
    printf("  avg: %.2f ms  (~%.0f FPS)\n\n", baseR.avgMs, 1000.0/baseR.avgMs);

    // ==================================================================
    // PHASE 2: Animation variants (4 emitters, subdiv 1 = 80 tri/sphere)
    // ==================================================================
    printf("============================================================\n");
    printf("PHASE 2: Animation variants (4 emitters, 80 tri/sphere)\n");
    printf("============================================================\n\n");

    int numEmitters = 4;
    GLuint icoVbo4, icoIbo4; int icoTotalIdx4;
    std::vector<IcoVertex> icoVertsBuf4;
    buildInstancedIco(allEmitters, numEmitters, 1, icoVbo4, icoIbo4, icoTotalIdx4, icoVertsBuf4);
    printf("  %d emitters x 80 tris = %d total tris\n\n", numEmitters, icoTotalIdx4/3);

    // 2a: Static
    printf("--- 2a: Static (no animation) ---\n");
    auto r2a = runBench([&](float t) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();
        drawIcos(progIcoStatic, icoVbo4, icoIbo4, allEmitters, numEmitters, 1, t, false, 1.0f, nullptr);
    });
    printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r2a.avgMs, 1000.0/r2a.avgMs, r2a.avgMs-baseR.avgMs);

    // 2b: Scale pulsing (uniform only, different radius each frame)
    printf("--- 2b: Scale pulsing (uniform radius oscillation) ---\n");
    auto r2b = runBench([&](float t) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();
        float scale = 1.0f + 0.15f * sinf(t * 3.0f);
        drawIcos(progIcoStatic, icoVbo4, icoIbo4, allEmitters, numEmitters, 1, t, false, scale, nullptr);
    });
    printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r2b.avgMs, 1000.0/r2b.avgMs, r2b.avgMs-baseR.avgMs);

    // 2c: Color flicker (CPU random per emitter, update uniform color per draw)
    printf("--- 2c: Color flicker (CPU random per-emitter intensity) ---\n");
    uint32_t rng = 42;
    auto r2c = runBench([&](float t) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();
        float flicker[16];
        for (int i = 0; i < numEmitters; i++)
            flicker[i] = 0.7f + 0.3f * randomFloat(rng);
        drawIcos(progIcoStatic, icoVbo4, icoIbo4, allEmitters, numEmitters, 1, t, false, 1.0f, flicker);
    });
    printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r2c.avgMs, 1000.0/r2c.avgMs, r2c.avgMs-baseR.avgMs);

    // 2d: Vertex jitter (VS displacement along normals)
    printf("--- 2d: Vertex jitter (VS normal displacement) ---\n");
    auto r2d = runBench([&](float t) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();
        drawIcos(progIcoJitter, icoVbo4, icoIbo4, allEmitters, numEmitters, 1, t, true, 1.0f, nullptr);
    });
    printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r2d.avgMs, 1000.0/r2d.avgMs, r2d.avgMs-baseR.avgMs);

    // 2e: Combined (scale + flicker + jitter)
    printf("--- 2e: Combined (scale + flicker + jitter) ---\n");
    rng = 42;
    auto r2e = runBench([&](float t) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();
        float scale = 1.0f + 0.15f * sinf(t * 3.0f);
        float flicker[16];
        for (int i = 0; i < numEmitters; i++)
            flicker[i] = 0.7f + 0.3f * randomFloat(rng);
        drawIcos(progIcoJitter, icoVbo4, icoIbo4, allEmitters, numEmitters, 1, t, true, scale, flicker);
    });
    printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r2e.avgMs, 1000.0/r2e.avgMs, r2e.avgMs-baseR.avgMs);

    // 2f: VBO re-upload (CPU rebuilds vertex colors every frame — worst case DMA)
    printf("--- 2f: VBO re-upload (CPU rebuild colors every frame) ---\n");
    rng = 42;
    auto r2f = runBench([&](float t) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawZone();

        // Modify vertex colors on CPU
        int vertsPerSphere = (int)icoVerts1.size();
        for (int e = 0; e < numEmitters; e++) {
            float flick = 0.7f + 0.3f * randomFloat(rng);
            float cr = allEmitters[e].r * allEmitters[e].intensity * flick;
            float cg = allEmitters[e].g * allEmitters[e].intensity * flick;
            float cb = allEmitters[e].b * allEmitters[e].intensity * flick;
            float ca = 0.15f * allEmitters[e].intensity * flick;
            for (int i = 0; i < vertsPerSphere; i++) {
                int idx = e * vertsPerSphere + i;
                icoVertsBuf4[idx].color[0] = cr;
                icoVertsBuf4[idx].color[1] = cg;
                icoVertsBuf4[idx].color[2] = cb;
                icoVertsBuf4[idx].color[3] = ca;
            }
        }
        // Re-upload entire VBO
        glBindBuffer(GL_ARRAY_BUFFER, icoVbo4);
        glBufferSubData(GL_ARRAY_BUFFER, 0, icoVertsBuf4.size()*sizeof(IcoVertex), icoVertsBuf4.data());

        drawIcos(progIcoStatic, icoVbo4, icoIbo4, allEmitters, numEmitters, 1, t, false, 1.0f, nullptr);
    });
    printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r2f.avgMs, 1000.0/r2f.avgMs, r2f.avgMs-baseR.avgMs);

    glDeleteBuffers(1, &icoVbo4); glDeleteBuffers(1, &icoIbo4);

    // ==================================================================
    // PHASE 3: Sphere count scaling (combined animation, subdiv 1)
    // ==================================================================
    printf("============================================================\n");
    printf("PHASE 3: Sphere count scaling (combined anim, 80 tri/sphere)\n");
    printf("============================================================\n\n");

    int counts[] = {4, 8, 16};
    for (int ci = 0; ci < 3; ci++) {
        int n = counts[ci];
        GLuint vbo, ibo; int totalIdx;
        std::vector<IcoVertex> buf;
        buildInstancedIco(allEmitters, n, 1, vbo, ibo, totalIdx, buf);

        printf("--- %d spheres (%d tris) ---\n", n, totalIdx/3);
        rng = 42;
        auto r = runBench([&](float t) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawZone();
            float scale = 1.0f + 0.15f * sinf(t * 3.0f);
            float flicker[16];
            for (int i = 0; i < n; i++) flicker[i] = 0.7f + 0.3f * randomFloat(rng);
            drawIcos(progIcoJitter, vbo, ibo, allEmitters, n, 1, t, true, scale, flicker);
        });
        printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r.avgMs, 1000.0/r.avgMs, r.avgMs-baseR.avgMs);

        glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ibo);
    }

    // ==================================================================
    // PHASE 4: Subdivision scaling (4 emitters, combined animation)
    // ==================================================================
    printf("============================================================\n");
    printf("PHASE 4: Subdivision scaling (4 emitters, combined anim)\n");
    printf("============================================================\n\n");

    int subdivs[] = {1, 2};
    for (int si = 0; si < 2; si++) {
        int sd = subdivs[si];
        GLuint vbo, ibo; int totalIdx;
        std::vector<IcoVertex> buf;
        buildInstancedIco(allEmitters, 4, sd, vbo, ibo, totalIdx, buf);

        int trisPerSphere = (sd == 1) ? 80 : 320;
        printf("--- subdiv %d (%d tri/sphere, %d total) ---\n", sd, trisPerSphere, totalIdx/3);
        rng = 42;
        auto r = runBench([&](float t) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawZone();
            float scale = 1.0f + 0.15f * sinf(t * 3.0f);
            float flicker[16];
            for (int i = 0; i < 4; i++) flicker[i] = 0.7f + 0.3f * randomFloat(rng);
            drawIcos(progIcoJitter, vbo, ibo, allEmitters, 4, sd, t, true, scale, flicker);
        });
        printf("  avg: %.2f ms  (~%.0f FPS)  delta: %+.2f ms\n\n", r.avgMs, 1000.0/r.avgMs, r.avgMs-baseR.avgMs);

        glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ibo);
    }

    // ==================================================================
    // Summary
    // ==================================================================
    printf("============================================================\n");
    printf("SUMMARY\n");
    printf("============================================================\n\n");
    printf("Zone baseline: %.2f ms (~%.0f FPS)\n\n", baseR.avgMs, 1000.0/baseR.avgMs);
    printf("%-45s  %7s  %7s  %10s\n", "Test", "ms", "FPS", "delta");
    printf("%-45s  %7s  %7s  %10s\n", "----", "--", "---", "-----");
    printf("%-45s  %7.2f  %7.0f  %10s\n", "Zone baseline", baseR.avgMs, 1000.0/baseR.avgMs, "-");
    printf("%-45s  %7.2f  %7.0f  %+9.2fms\n", "4x Static", r2a.avgMs, 1000.0/r2a.avgMs, r2a.avgMs-baseR.avgMs);
    printf("%-45s  %7.2f  %7.0f  %+9.2fms\n", "4x Scale pulse", r2b.avgMs, 1000.0/r2b.avgMs, r2b.avgMs-baseR.avgMs);
    printf("%-45s  %7.2f  %7.0f  %+9.2fms\n", "4x Color flicker", r2c.avgMs, 1000.0/r2c.avgMs, r2c.avgMs-baseR.avgMs);
    printf("%-45s  %7.2f  %7.0f  %+9.2fms\n", "4x Vertex jitter", r2d.avgMs, 1000.0/r2d.avgMs, r2d.avgMs-baseR.avgMs);
    printf("%-45s  %7.2f  %7.0f  %+9.2fms\n", "4x Combined (scale+flicker+jitter)", r2e.avgMs, 1000.0/r2e.avgMs, r2e.avgMs-baseR.avgMs);
    printf("%-45s  %7.2f  %7.0f  %+9.2fms\n", "4x VBO re-upload (worst case)", r2f.avgMs, 1000.0/r2f.avgMs, r2f.avgMs-baseR.avgMs);

    printf("\nFrame budget: 33.3ms @ 30 FPS\n");
    printf("Combined 4x animation headroom: %.1f ms (%.0f%% of budget)\n",
           33.3 - r2e.avgMs, r2e.avgMs / 33.3 * 100.0);

    // Cleanup
    glDeleteProgram(progZone); glDeleteProgram(progIcoStatic); glDeleteProgram(progIcoJitter);
    glDeleteShader(vsZone); glDeleteShader(fsZone);
    glDeleteShader(vsIcoStatic); glDeleteShader(vsIcoJitter); glDeleteShader(fsIco);
    glDeleteTextures(1, &checkerTex);
    glDeleteBuffers(1, &zoneVbo); glDeleteBuffers(1, &zoneIbo);

    cleanup(state);
    return 0;
}
