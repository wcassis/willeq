// gles2_player_light_probe -- B965 (GitLab #283): why the per-pixel player
// light (zone_shader_sources_gles2.h, the uAttenLUT path) is blown out on Lima
// while llvmpipe draws it as blessed.
//
// One full-viewport quad per draw. The quad's world positions run along +X
// from the light: pixel column x is at distance d = (x + 0.5) * kMaxDist / W
// from the light (plus a fixed 5-unit drop for the "floor" normal), so one
// pixel row is the light's intensity curve over 0..kMaxDist units. The light
// sits at a world ORIGIN chosen on the command line (0,0,0 or a real gate pose)
// so world-coordinate precision is exercised as well as distance range.
//
// Every draw writes the production term pLight = uPlayerLightColor * pLn * lut.a
// (the gate's lightstone, level 9) scaled by 0.25 into RGB, or a diagnostic
// quantity; the middle row is read back with glReadPixels. Values only, no
// verdict: compare the same variant across llvmpipe and Lima.
//
// Variants:
//   prod            -- the production FS, verbatim math (mediump, FP16 LUT, linear)
//   prod_nearest    -- same, LUT sampled GL_NEAREST
//   prod_highp      -- same, `precision highp float` if GL_FRAGMENT_PRECISION_HIGH
//   prod_lut_fp32   -- same, LUT uploaded as GL_FLOAT (needs OES_texture_float)
//   vs_plv          -- pLv = uPlayerLightPos - worldPos computed in the VS (FP32 on
//                      Mali-400) and passed as a varying
//   fs_prescaled    -- production, but pLv scaled by 1/64 before the dot, so the
//                      FS never forms d^2 itself (d^2/4096 instead)
//   vs_prescaled    -- pLv/64 computed in the VS, dot in the FS
//   prod_clamped    -- production with the LUT coordinate min()'d to 1.0
//   prod_saturate   -- production with the coordinate clamp(x, 0, 1)'d (the shipped fix)
//   fs_prescaled_clamped -- fs_prescaled plus that clamp
//   diag_coord      -- R = clamp(d2*uLUTScale), G = clamp(d2*uLUTScale/64), B = d2>=60000
//   diag_lut_linear_coord -- the LUT sampled at d/16 (0..25), no d^2 involved
//   diag_lut        -- R = 2*lut.r, G = lut.a at the production coordinate
//   diag_plv_frac   -- R = fract(pLv.x/8) (precision of the world subtraction)
//   diag_highp      -- R = 1 if GL_FRAGMENT_PRECISION_HIGH is defined in the FS
//
// Usage: gles2_player_light_probe [--drm] [--res W H] [--origin X Y Z] [--csv FILE]

#include <irrlicht.h>
#include "client/graphics/gles2_shader_cache.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kMaxDist = 400.0f;   // columns span 0..400 world units
constexpr float kMaxD2 = 4096.0f;    // ZoneShaderManager::lutMaxD2_
constexpr float kLutScale = 1.0f / 4096.0f;
constexpr float kRadius = 20.0f + 0.9f * 80.0f;  // lightstone, level 9 (zone_bench_frame.cpp)
const float kColor[3] = {0.99f * 0.9f * 3.0f, 0.88f * 0.9f * 3.0f, 0.58f * 0.9f * 3.0f};
const float kAtten[3] = {1.0f, 0.0f, 19.0f / (kRadius * kRadius)};

// ---- the LUT, exactly as zone_shader.cpp builds it ----
uint16_t floatToHalf(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t mant = bits & 0x7FFFFF;
    if (exp > 15) return sign | 0x7C00;
    if (exp < -14) return sign;
    return sign | ((exp + 15) << 10) | (mant >> 13);
}
void lutValues(int i, float& invD, float& atten) {
    const float eps = 0.0001f;
    float d2 = kMaxD2 * (static_cast<float>(i) + 0.5f) / 256.0f;
    invD = 1.0f / std::sqrt(d2 + eps);
    atten = 1.0f / (kAtten[0] + kAtten[2] * d2 + eps);
}

const char* kVs = R"(
precision highp float;
attribute vec2 aClip;
attribute vec3 aWorld;
uniform vec3 uLightPosVS;  // same value as uPlayerLightPos; a VS (highp) and an FS
                           // (mediump) declaration of ONE uniform name fail to link
varying vec3 vWorldPos;
varying vec3 vPlv;
varying vec3 vPls;
void main() {
    gl_Position = vec4(aClip, 0.5, 1.0);
    vWorldPos = aWorld;
    vPlv = uLightPosVS - aWorld;
    vPls = (uLightPosVS - aWorld) * 0.015625;
}
)";

#define FS_HEAD \
    "uniform vec3 uPlayerLightPos;\n" \
    "uniform vec3 uPlayerLightColor;\n" \
    "uniform sampler2D uAttenLUT;\n" \
    "uniform float uLUTScale;\n" \
    "uniform vec3 uNormal;\n" \
    "varying vec3 vWorldPos;\n" \
    "varying vec3 vPlv;\n" \
    "varying vec3 vPls;\n"

// The production player-light lines (FRAGMENT_SHADER_SOLID_SRC), with the
// interpolated normal replaced by a uniform (the probe's quad is flat).
#define PROD_BODY \
    "void main() {\n" \
    "    vec3 pLv = uPlayerLightPos - vWorldPos;\n" \
    "    float d2 = dot(pLv, pLv);\n" \
    "    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));\n" \
    "    float pLn = max(dot(uNormal, pLv * lut.r), 0.0);\n" \
    "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n" \
    "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n" \
    "}\n"

const char* kProd = "precision mediump float;\n" FS_HEAD PROD_BODY;

const char* kProdHighp =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" FS_HEAD
    PROD_BODY;

const char* kVsPlv = "precision mediump float;\n" FS_HEAD
                     "void main() {\n"
                     "    vec3 pLv = vPlv;\n"
                     "    float d2 = dot(pLv, pLv);\n"
                     "    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));\n"
                     "    float pLn = max(dot(uNormal, pLv * lut.r), 0.0);\n"
                     "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n"
                     "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n"
                     "}\n";

const char* kFsPrescaled = "precision mediump float;\n" FS_HEAD
                           "void main() {\n"
                           "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
                           "    vec3 pLs = pLv * 0.015625;\n"
                           "    vec4 lut = texture2D(uAttenLUT, vec2(dot(pLs, pLs) * (uLUTScale * 4096.0), 0.5));\n"
                           "    float pLn = max(dot(uNormal, pLv * lut.r), 0.0);\n"
                           "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n"
                           "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n"
                           "}\n";

const char* kVsPrescaled = "precision mediump float;\n" FS_HEAD
                           "void main() {\n"
                           "    vec3 pLs = vPls;\n"
                           "    vec4 lut = texture2D(uAttenLUT, vec2(dot(pLs, pLs) * (uLUTScale * 4096.0), 0.5));\n"
                           "    float pLn = max(dot(uNormal, pLs * (lut.r * 64.0)), 0.0);\n"
                           "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n"
                           "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n"
                           "}\n";

// The production math with the LUT coordinate clamped to the LUT's own end
// (1.0 = the last texel, which CLAMP_TO_EDGE returns for any larger value).
const char* kProdClamped = "precision mediump float;\n" FS_HEAD
                           "void main() {\n"
                           "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
                           "    float d2 = dot(pLv, pLv);\n"
                           "    vec4 lut = texture2D(uAttenLUT, vec2(min(d2 * uLUTScale, 1.0), 0.5));\n"
                           "    float pLn = max(dot(uNormal, pLv * lut.r), 0.0);\n"
                           "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n"
                           "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n"
                           "}\n";

// The shipped fix (B965): the production math with the coordinate SATURATED,
// clamp(x, 0, 1) -- a free output modifier on Mali-400, where min() is an op.
const char* kProdSaturate = "precision mediump float;\n" FS_HEAD
                            "void main() {\n"
                            "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
                            "    float d2 = dot(pLv, pLv);\n"
                            "    vec4 lut = texture2D(uAttenLUT, vec2(clamp(d2 * uLUTScale, 0.0, 1.0), 0.5));\n"
                            "    float pLn = max(dot(uNormal, pLv * lut.r), 0.0);\n"
                            "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n"
                            "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n"
                            "}\n";

// Prescaled (d^2 never formed at full range) AND clamped.
const char* kFsPrescaledClamped =
    "precision mediump float;\n" FS_HEAD
    "void main() {\n"
    "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
    "    vec3 pLs = pLv * 0.015625;\n"
    "    vec4 lut = texture2D(uAttenLUT, vec2(min(dot(pLs, pLs) * (uLUTScale * 4096.0), 1.0), 0.5));\n"
    "    float pLn = max(dot(uNormal, pLv * lut.r), 0.0);\n"
    "    vec3 pLight = uPlayerLightColor * pLn * lut.a;\n"
    "    gl_FragColor = vec4(pLight * 0.25, 1.0);\n"
    "}\n";

// The LUT sampled at coordinate d/16 (0..25 across the row, formed from the
// VS-computed FP32 varying, so no FP16 overflow is involved): does the sampler
// return the last texel for every coordinate > 1 under CLAMP_TO_EDGE + LINEAR?
// R = 2*lut.r, G = lut.a.
const char* kDiagLutAtX = "precision mediump float;\n" FS_HEAD
                          "void main() {\n"
                          "    vec4 lut = texture2D(uAttenLUT, vec2(-vPlv.x * 0.0625, 0.5));\n"
                          "    gl_FragColor = vec4(clamp(lut.r * 2.0, 0.0, 1.0), clamp(lut.a, 0.0, 1.0), 0.0, 1.0);\n"
                          "}\n";

const char* kDiagCoord = "precision mediump float;\n" FS_HEAD
                         "void main() {\n"
                         "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
                         "    float d2 = dot(pLv, pLv);\n"
                         "    float c = d2 * uLUTScale;\n"
                         "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), clamp(c / 64.0, 0.0, 1.0), step(60000.0, d2), 1.0);\n"
                         "}\n";

const char* kDiagLut = "precision mediump float;\n" FS_HEAD
                       "void main() {\n"
                       "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
                       "    float d2 = dot(pLv, pLv);\n"
                       "    vec4 lut = texture2D(uAttenLUT, vec2(d2 * uLUTScale, 0.5));\n"
                       "    gl_FragColor = vec4(clamp(lut.r * 2.0, 0.0, 1.0), clamp(lut.a, 0.0, 1.0), 0.0, 1.0);\n"
                       "}\n";

const char* kDiagPlvFrac = "precision mediump float;\n" FS_HEAD
                           "void main() {\n"
                           "    vec3 pLv = uPlayerLightPos - vWorldPos;\n"
                           "    gl_FragColor = vec4(fract(-pLv.x / 8.0), clamp(-pLv.x / 512.0, 0.0, 1.0), 0.0, 1.0);\n"
                           "}\n";

const char* kDiagHighp =
    "precision mediump float;\n" FS_HEAD
    "void main() {\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "#else\n    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n#endif\n"
    "}\n";

GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        printf("[plight-probe] %s compile FAILED: %s\n", name, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* fsSrc, const char* name) {
    GLuint vs = compile(GL_VERTEX_SHADER, kVs, name);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc, name);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aClip");
    glBindAttribLocation(p, 1, "aWorld");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        printf("[plight-probe] %s link FAILED: %s\n", name, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

GLuint makeLut(bool fp32, GLint filter) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (fp32) {
        std::vector<float> d(512);
        for (int i = 0; i < 256; ++i) lutValues(i, d[i * 2], d[i * 2 + 1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 256, 1, 0, GL_LUMINANCE_ALPHA, GL_FLOAT, d.data());
    } else {
        std::vector<uint16_t> d(512);
        for (int i = 0; i < 256; ++i) {
            float a, b;
            lutValues(i, a, b);
            d[i * 2] = floatToHalf(a);
            d[i * 2 + 1] = floatToHalf(b);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 256, 1, 0, GL_LUMINANCE_ALPHA, GL_HALF_FLOAT_OES,
                     d.data());
    }
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("[plight-probe] LUT upload (fp32=%d) failed: 0x%x\n", fp32 ? 1 : 0, err);
        glDeleteTextures(1, &t);
        return 0;
    }
    return t;
}

const float kSampleD[] = {2, 4, 8, 16, 24, 32, 48, 64, 96, 128, 160, 192, 224, 250, 256, 260, 280, 300, 350, 399};

}  // namespace

int main(int argc, char** argv) {
    bool useDrm = false;
    int W = 800, H = 600;
    float O[3] = {0, 0, 0};
    const char* csvPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--drm")) useDrm = true;
        else if (!strcmp(argv[i], "--res") && i + 2 < argc) {
            W = atoi(argv[++i]);
            H = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--origin") && i + 3 < argc) {
            O[0] = static_cast<float>(atof(argv[++i]));
            O[1] = static_cast<float>(atof(argv[++i]));
            O[2] = static_cast<float>(atof(argv[++i]));
        } else if (!strcmp(argv[i], "--csv") && i + 1 < argc) {
            csvPath = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--drm] [--res W H] [--origin X Y Z] [--csv FILE]\n", argv[0]);
            return 2;
        }
    }

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(W, H);
    params.Fullscreen = true;
    params.Vsync = false;
#ifdef EQT_HAS_DRM
    if (useDrm) params.DeviceType = irr::EIDT_FRAMEBUFFER;
#else
    (void)useDrm;
#endif
    eqt::ConfigureGles2ShaderCache();
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: could not create the EDT_OGLES2 device\n");
        return 1;
    }
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    W = static_cast<int>(driver->getScreenSize().Width);
    H = static_cast<int>(driver->getScreenSize().Height);
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool hasFloat = ext && strstr(ext, "GL_OES_texture_float");
    GLint vsRange[2] = {0, 0}, vsPrec = 0, fsRange[2] = {0, 0}, fsPrec = 0, fsmRange[2] = {0, 0}, fsmPrec = 0;
    glGetShaderPrecisionFormat(GL_VERTEX_SHADER, GL_HIGH_FLOAT, vsRange, &vsPrec);
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, fsRange, &fsPrec);
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT, fsmRange, &fsmPrec);
    printf("[plight-probe] renderer=\"%s\" version=\"%s\" screen=%dx%d origin=(%.1f,%.1f,%.1f)\n",
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
           reinterpret_cast<const char*>(glGetString(GL_VERSION)), W, H, O[0], O[1], O[2]);
    printf("[plight-probe] precision vs_highp=(%d,%d,%d) fs_highp=(%d,%d,%d) fs_mediump=(%d,%d,%d) "
           "OES_texture_float=%d\n",
           vsRange[0], vsRange[1], vsPrec, fsRange[0], fsRange[1], fsPrec, fsmRange[0], fsmRange[1], fsmPrec,
           hasFloat ? 1 : 0);
    printf("[plight-probe] light color=(%.4f,%.4f,%.4f) atten=(%.1f,%.1f,%.9f)\n", kColor[0], kColor[1],
           kColor[2], kAtten[0], kAtten[1], kAtten[2]);

    GLuint lutLinear = makeLut(false, GL_LINEAR);
    GLuint lutNearest = makeLut(false, GL_NEAREST);
    GLuint lutF32 = hasFloat ? makeLut(true, GL_LINEAR) : 0;

    struct Variant {
        const char* name;
        const char* fs;
        GLuint lut;
    } variants[] = {
        {"prod", kProd, lutLinear},
        {"prod_nearest", kProd, lutNearest},
        {"prod_highp", kProdHighp, lutLinear},
        {"prod_lut_fp32", kProd, lutF32},
        {"vs_plv", kVsPlv, lutLinear},
        {"fs_prescaled", kFsPrescaled, lutLinear},
        {"vs_prescaled", kVsPrescaled, lutLinear},
        {"prod_clamped", kProdClamped, lutLinear},
        {"prod_saturate", kProdSaturate, lutLinear},
        {"fs_prescaled_clamped", kFsPrescaledClamped, lutLinear},
        {"diag_coord", kDiagCoord, lutLinear},
        {"diag_lut_linear_coord", kDiagLutAtX, lutLinear},
        {"diag_lut", kDiagLut, lutLinear},
        {"diag_plv_frac", kDiagPlvFrac, lutLinear},
        {"diag_highp", kDiagHighp, lutLinear},
    };
    // Two surfaces: a wall facing the light (N = -X, pLn = d * invD) and a
    // floor 5 units below it (N = +Y, pLn = 5 * invD).
    struct Surface {
        const char* name;
        float n[3];
        float drop;
    } surfaces[] = {{"wall", {-1, 0, 0}, 0.0f}, {"floor", {0, 1, 0}, 5.0f}};

    FILE* csv = csvPath ? fopen(csvPath, "w") : nullptr;
    if (csv) fprintf(csv, "variant,surface,x,d,r,g,b\n");

    std::vector<unsigned char> px(static_cast<size_t>(W) * H * 4);
    for (const Surface& s : surfaces) {
        // Clip-space quad; world X = O.x + 0 .. O.x + kMaxDist across the screen.
        const float clip[] = {-1, -1, 1, -1, 1, 1, -1, 1};
        const float world[] = {O[0], O[1] - s.drop, O[2], O[0] + kMaxDist, O[1] - s.drop, O[2],
                               O[0] + kMaxDist, O[1] - s.drop, O[2], O[0], O[1] - s.drop, O[2]};
        const unsigned short idx[] = {0, 1, 2, 0, 2, 3};
        for (const Variant& v : variants) {
            if (!v.lut) {
                printf("[plight-probe] variant=%s surface=%s UNAVAILABLE (no LUT)\n", v.name, s.name);
                continue;
            }
            GLuint prog = link(v.fs, v.name);
            if (!prog) continue;
            glViewport(0, 0, W, H);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            for (GLuint a = 0; a < 8; ++a) glDisableVertexAttribArray(a);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glUseProgram(prog);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, v.lut);
            glActiveTexture(GL_TEXTURE0);
            GLint loc;
            if ((loc = glGetUniformLocation(prog, "uAttenLUT")) >= 0) glUniform1i(loc, 2);
            if ((loc = glGetUniformLocation(prog, "uLUTScale")) >= 0) glUniform1f(loc, kLutScale);
            if ((loc = glGetUniformLocation(prog, "uPlayerLightPos")) >= 0) glUniform3fv(loc, 1, O);
            if ((loc = glGetUniformLocation(prog, "uLightPosVS")) >= 0) glUniform3fv(loc, 1, O);
            if ((loc = glGetUniformLocation(prog, "uPlayerLightColor")) >= 0) glUniform3fv(loc, 1, kColor);
            if ((loc = glGetUniformLocation(prog, "uNormal")) >= 0) glUniform3fv(loc, 1, s.n);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, clip);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, world);
            glEnableVertexAttribArray(1);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
            glFinish();
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            glUseProgram(0);
            glDeleteProgram(prog);

            const int row = H / 2;
            std::string line;
            char buf[64];
            for (float d : kSampleD) {
                int x = static_cast<int>(d * W / kMaxDist);
                if (x >= W) x = W - 1;
                const unsigned char* p = &px[(static_cast<size_t>(row) * W + x) * 4];
                snprintf(buf, sizeof(buf), " d%g=%d/%d/%d", d, p[0], p[1], p[2]);
                line += buf;
            }
            printf("[plight-probe] variant=%s surface=%s%s\n", v.name, s.name, line.c_str());
            if (csv) {
                for (int x = 0; x < W; ++x) {
                    const unsigned char* p = &px[(static_cast<size_t>(row) * W + x) * 4];
                    fprintf(csv, "%s,%s,%d,%.3f,%d,%d,%d\n", v.name, s.name, x, (x + 0.5f) * kMaxDist / W, p[0],
                            p[1], p[2]);
                }
            }
            fflush(stdout);
        }
    }
    if (csv) fclose(csv);
    device->drop();
    return 0;
}
