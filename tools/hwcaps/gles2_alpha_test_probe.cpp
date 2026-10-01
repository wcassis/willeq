// gles2_alpha_test_probe -- B958 (GitLab #276): why an alpha-tested RGBA quad
// renders INVERTED on Lima (the transparent texels drawn black, the opaque
// texels discarded) while the same code is right on llvmpipe.
//
// One process, one texture, many ways of drawing it; every result is a pixel
// count, never a verdict. The texture is 64x64 RGBA: the LEFT half opaque
// (200,120,40,255), the RIGHT half (0,0,0,0) -- exactly the two texel kinds of
// an impostor capture. It is drawn as a centred quad over a (40,40,60) clear,
// and the frame is read back with glReadPixels into this tool's own buffer.
// For each draw the two halves of the quad are classified:
//   model = within 8 of (200,120,40)    black = all channels < 8
//   bg    = within 8 of (40,40,60)      other = anything else
// A correct alpha test is left: all model, right: all bg. The Lima defect is
// left: all bg, right: all black.
//
// Draws:
//   driver_*  -- through COpenGLES2Driver (setMaterial + drawVertexPrimitiveList,
//                the billboard's exact call shape), with the texture uploaded by
//                driver->addTexture(IImage) ("upload") or by raw glTexImage2D +
//                gles2WrapTexture ("wrapped", the impostor's shape), or rendered
//                into an addRenderTargetTexture() ("rtt", the capture's kind).
//   raw_*     -- this tool's own GL programs: the driver's two ALPHA3D fragment
//                shaders (copied verbatim), and no-discard diagnostics that
//                write the sampled alpha / the comparison / the threshold as a
//                colour. Every raw program is drawn twice: freshly compiled
//                ("fresh") and after a glGetProgramBinaryOES ->
//                glProgramBinaryOES round trip ("binary") -- the driver's
//                shader cache (config/shader_cache) loads its built-in
//                programs that way on every run after the first.
//
// Usage: gles2_alpha_test_probe [--drm] [--res W H]
// Run it twice from the same empty directory to see the driver's built-in
// program both compiled (first run) and loaded from its binary cache (second).

#include <irrlicht.h>
#include "client/graphics/gles2_shader_cache.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                              unsigned int width, unsigned int height, unsigned int gpuBytes);

namespace {

constexpr int kTex = 64;
constexpr unsigned char kModel[3] = {200, 120, 40};
constexpr unsigned char kBg[3] = {40, 40, 60};

// ---- the driver's ALPHA3D fragment shaders, verbatim (COGLES2Shaders.cpp) ----
const char* kAlpha3dBase = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < 0.5) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

const char* kAlpha3dDerivatives = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float threshold = clamp(0.5 - fwidth(texColor.a), 0.1, 0.5);
    if (texColor.a < threshold) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ---- no-discard diagnostics ----
const char* kAlphaAsColour = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 t = texture2D(uTexture, vTexCoord);
    gl_FragColor = vec4(t.a, t.a, t.a, 1.0);
}
)";

// Red where the base test would discard, green where it would keep.
const char* kCompareBase = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 t = texture2D(uTexture, vTexCoord);
    if (t.a < 0.5) gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    else gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
)";

// Red where the derivatives test would discard, green where it would keep.
const char* kCompareDerivatives = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 t = texture2D(uTexture, vTexCoord);
    float threshold = clamp(0.5 - fwidth(t.a), 0.1, 0.5);
    if (t.a < threshold) gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
    else gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
)";

// R = threshold, G = fwidth(a) -- read back as bytes.
const char* kThresholdAsColour = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 t = texture2D(uTexture, vTexCoord);
    float fw = fwidth(t.a);
    float threshold = clamp(0.5 - fw, 0.1, 0.5);
    gl_FragColor = vec4(threshold, fw, 0.0, 1.0);
}
)";

// ---- isolating the Lima defect (B958): which part of ALPHA3D_FS_DERIVATIVES
// turns the discard around? Each keeps ONE ingredient of it. ----

// Derivatives computed, but the discard compares with the constant 0.5.
const char* kFwidthUnusedInDiscard = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float fw = fwidth(texColor.a);
    if (texColor.a < 0.5) discard;
    gl_FragColor = texColor * vColor;
    gl_FragColor.a = min(1.0, gl_FragColor.a + fw);  // keeps fw alive; alpha is not blended
}
)";

// A non-constant threshold with no derivative in it (uThreshold = 0.5).
const char* kUniformThreshold = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
uniform float uThreshold;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < uThreshold) discard;
    gl_FragColor = texColor * vColor;
}
)";

// The derivative threshold, tested through step() instead of `<`.
const char* kDerivativesStep = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float threshold = clamp(0.5 - fwidth(texColor.a), 0.1, 0.5);
    if (step(threshold, texColor.a) < 0.5) discard;
    gl_FragColor = texColor * vColor;
}
)";

// The derivative threshold, with the discard as the LAST statement.
const char* kDerivativesDiscardLast = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float threshold = clamp(0.5 - fwidth(texColor.a), 0.1, 0.5);
    gl_FragColor = texColor * vColor;
    if (texColor.a < threshold) discard;
}
)";

// fwidth of a quantity that is not the one tested (the texture coordinate).
const char* kDerivativeOfOtherValue = R"(
#extension GL_OES_standard_derivatives : enable
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    float threshold = clamp(0.5 - fwidth(vTexCoord.x) * 0.001, 0.1, 0.5);
    if (texColor.a < threshold) discard;
    gl_FragColor = texColor * vColor;
}
)";

const char* kVs = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec2 aTexCoord0;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    gl_Position = vec4(aPosition, 1.0);
    vTexCoord = aTexCoord0;
    vColor = vec4(1.0);
    vFogFactor = 1.0;
}
)";

struct Counts {
    long model = 0, black = 0, bg = 0, other = 0;
    double sumR = 0, sumG = 0, sumB = 0;
    long n = 0;
};

bool near3(const unsigned char* p, const unsigned char* c) {
    for (int i = 0; i < 3; ++i)
        if (std::abs(static_cast<int>(p[i]) - static_cast<int>(c[i])) > 8) return false;
    return true;
}

Counts classify(const std::vector<unsigned char>& px, int W, int x0, int x1, int y0, int y1) {
    Counts c;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const unsigned char* p = &px[(static_cast<size_t>(y) * W + x) * 4];
            if (near3(p, kModel)) c.model++;
            else if (p[0] < 8 && p[1] < 8 && p[2] < 8) c.black++;
            else if (near3(p, kBg)) c.bg++;
            else c.other++;
            c.sumR += p[0];
            c.sumG += p[1];
            c.sumB += p[2];
            c.n++;
        }
    }
    return c;
}

struct Probe {
    irr::video::IVideoDriver* driver = nullptr;
    int W = 0, H = 0;

    void report(const char* draw, const char* tex, const char* prog) {
        glFinish();
        std::vector<unsigned char> px(static_cast<size_t>(W) * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        // The quad spans clip -0.5..0.5 = pixels W/4..3W/4, H/4..3H/4 (row 0 is
        // the bottom in both GL and this buffer, so no flip is needed). Keep 8
        // px off every edge and off the seam, where bilinear filtering mixes
        // the halves.
        const int m = 8;
        const int yl = H / 4 + m, yh = 3 * H / 4 - m;
        Counts L = classify(px, W, W / 4 + m, W / 2 - m, yl, yh);
        Counts R = classify(px, W, W / 2 + m, 3 * W / 4 - m, yl, yh);
        printf("[alpha-probe] draw=%s tex=%s prog=%s "
               "left_opaque: model=%ld black=%ld bg=%ld other=%ld mean=(%.1f,%.1f,%.1f) | "
               "right_transparent: model=%ld black=%ld bg=%ld other=%ld mean=(%.1f,%.1f,%.1f)\n",
               draw, tex, prog, L.model, L.black, L.bg, L.other, L.sumR / L.n, L.sumG / L.n,
               L.sumB / L.n, R.model, R.black, R.bg, R.other, R.sumR / R.n, R.sumG / R.n,
               R.sumB / R.n);
        fflush(stdout);
    }

    void begin() { driver->beginScene(true, true, irr::video::SColor(255, kBg[0], kBg[1], kBg[2])); }
    void end() { driver->endScene(); }

    // The billboard's call shape (zone_bench_impostor.cpp drawImpostorBillboard):
    // EVT_STANDARD vertices, 16-bit indices, drawVertexPrimitiveList.
    void driverDraw(irr::video::ITexture* tex, irr::video::E_MATERIAL_TYPE type, float e = 0.5f) {
        const irr::video::SColor white(255, 255, 255, 255);
        irr::video::S3DVertex q[4] = {
            irr::video::S3DVertex(-e, -e, 0.5f, 0, 0, 1, white, 0.0f, 0.0f),
            irr::video::S3DVertex(e, -e, 0.5f, 0, 0, 1, white, 1.0f, 0.0f),
            irr::video::S3DVertex(e, e, 0.5f, 0, 0, 1, white, 1.0f, 1.0f),
            irr::video::S3DVertex(-e, e, 0.5f, 0, 0, 1, white, 0.0f, 1.0f),
        };
        const irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};
        irr::video::SMaterial m;
        m.MaterialType = type;
        m.MaterialTypeParam = 0.1f;
        m.Lighting = false;
        m.BackfaceCulling = false;
        m.FogEnable = false;
        m.setTexture(0, tex);
        const irr::core::matrix4 ident;
        driver->setTransform(irr::video::ETS_PROJECTION, ident);
        driver->setTransform(irr::video::ETS_VIEW, ident);
        driver->setTransform(irr::video::ETS_WORLD, ident);
        driver->setMaterial(m);
        driver->drawVertexPrimitiveList(q, 4, idx, 2, irr::video::EVT_STANDARD,
                                        irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
    }
};

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        printf("[alpha-probe] compile FAILED: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* fsSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, kVs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPosition");
    glBindAttribLocation(p, 1, "aTexCoord0");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        printf("[alpha-probe] link FAILED\n");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// glGetProgramBinaryOES -> a new program via glProgramBinaryOES; 0 when the
// extension is absent (llvmpipe exposes no binary format) or the load fails.
GLuint roundTrip(GLuint prog) {
    auto getBin = reinterpret_cast<PFNGLGETPROGRAMBINARYOESPROC>(eglGetProcAddress("glGetProgramBinaryOES"));
    auto putBin = reinterpret_cast<PFNGLPROGRAMBINARYOESPROC>(eglGetProcAddress("glProgramBinaryOES"));
    GLint formats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &formats);
    while (glGetError() != GL_NO_ERROR) {}
    if (!getBin || !putBin || formats <= 0) return 0;
    GLint len = 0;
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH_OES, &len);
    if (len <= 0) return 0;
    std::vector<unsigned char> bin(static_cast<size_t>(len));
    GLenum fmt = 0;
    GLsizei got = 0;
    getBin(prog, len, &got, &fmt, bin.data());
    if (got <= 0) return 0;
    GLuint p = glCreateProgram();
    putBin(p, fmt, bin.data(), got);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void rawDraw(GLuint prog, GLuint tex) {
    static const float pos[] = {-0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f};
    static const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
    static const unsigned short idx[] = {0, 1, 2, 0, 2, 3};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    for (GLuint a = 0; a < 8; ++a) glDisableVertexAttribArray(a);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint loc = glGetUniformLocation(prog, "uTexture");
    if (loc >= 0) glUniform1i(loc, 0);
    loc = glGetUniformLocation(prog, "uFogColor");
    if (loc >= 0) glUniform4f(loc, 0.0f, 0.0f, 0.0f, 1.0f);
    loc = glGetUniformLocation(prog, "uThreshold");
    if (loc >= 0) glUniform1f(loc, 0.5f);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, pos);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray(1);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

}  // namespace

int main(int argc, char** argv) {
    bool useDrm = false;
    int W = 800, H = 600;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--drm")) useDrm = true;
        else if (!strcmp(argv[i], "--res") && i + 2 < argc) {
            W = atoi(argv[++i]);
            H = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [--drm] [--res W H]\n", argv[0]);
            return 2;
        }
    }

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(W, H);
    params.Fullscreen = true;
    params.Stencilbuffer = true;
    params.Vsync = false;
#ifdef EQT_HAS_DRM
    if (useDrm) params.DeviceType = irr::EIDT_FRAMEBUFFER;
#else
    (void)useDrm;
#endif
    eqt::ConfigureGles2ShaderCache();  // B959: cache dir via ResolveCachePathForWrite
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: could not create the EDT_OGLES2 device\n");
        return 1;
    }
    Probe pr;
    pr.driver = device->getVideoDriver();
    const irr::core::dimension2d<irr::u32> screen = pr.driver->getScreenSize();
    pr.W = static_cast<int>(screen.Width);
    pr.H = static_cast<int>(screen.Height);
    printf("[alpha-probe] renderer=\"%s\" version=\"%s\" screen=%dx%d\n",
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
           reinterpret_cast<const char*>(glGetString(GL_VERSION)), pr.W, pr.H);
    // Which Mesa is mapped (the Pi has the system Mesa and the LLVM-less
    // prefix, both 25.2.8, so the version string cannot tell them apart).
    if (FILE* maps = fopen("/proc/self/maps", "r")) {
        char line[512];
        std::string gallium, llvm;
        while (fgets(line, sizeof(line), maps)) {
            const char* path = strchr(line, '/');
            if (!path) continue;
            if (gallium.empty() && strstr(path, "libgallium")) gallium = path;
            if (llvm.empty() && strstr(path, "libLLVM")) llvm = path;
        }
        fclose(maps);
        if (!gallium.empty() && gallium.back() == '\n') gallium.pop_back();
        if (!llvm.empty() && llvm.back() == '\n') llvm.pop_back();
        printf("[alpha-probe] mapped gallium=%s llvm=%s\n", gallium.empty() ? "-" : gallium.c_str(),
               llvm.empty() ? "-" : llvm.c_str());
    }

    // The texture, as RGBA bytes (left half opaque model colour, right half 0).
    std::vector<unsigned char> rgba(kTex * kTex * 4, 0);
    for (int y = 0; y < kTex; ++y)
        for (int x = 0; x < kTex / 2; ++x) {
            unsigned char* p = &rgba[(y * kTex + x) * 4];
            p[0] = kModel[0];
            p[1] = kModel[1];
            p[2] = kModel[2];
            p[3] = 255;
        }

    // (1) the driver's own upload: an A8R8G8B8 IImage (BGRA bytes).
    irr::video::IImage* img = pr.driver->createImage(irr::video::ECF_A8R8G8B8,
                                                     irr::core::dimension2d<irr::u32>(kTex, kTex));
    {
        unsigned char* d = static_cast<unsigned char*>(img->lock());
        for (int i = 0; i < kTex * kTex; ++i) {
            d[i * 4 + 0] = rgba[i * 4 + 2];
            d[i * 4 + 1] = rgba[i * 4 + 1];
            d[i * 4 + 2] = rgba[i * 4 + 0];
            d[i * 4 + 3] = rgba[i * 4 + 3];
        }
        img->unlock();
    }
    irr::video::ITexture* uploaded = pr.driver->addTexture("b958_probe_upload", img);
    img->drop();

    // (2) the impostor's shape: raw glTexImage2D, wrapped by the driver.
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    GLuint rawTex = 0;
    glGenTextures(1, &rawTex);
    glBindTexture(GL_TEXTURE_2D, rawTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTex, kTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    irr::video::ITexture* wrapped = static_cast<irr::video::ITexture*>(
        gles2WrapTexture(pr.driver, "b958_probe_wrapped", rawTex, kTex, kTex, kTex * kTex * 4));

    // (3) a render target (the impostor capture's kind of texture): the
    // uploaded texture copied into it full-frame with EMT_SOLID, which writes
    // the texel alpha through unchanged (blending is off).
    irr::video::ITexture* rtt =
        pr.driver->addRenderTargetTexture(irr::core::dimension2d<irr::u32>(kTex, kTex), "b958_probe_rtt");
    if (rtt && pr.driver->setRenderTarget(rtt, true, true, irr::video::SColor(0, 0, 0, 0))) {
        pr.driverDraw(uploaded, irr::video::EMT_SOLID, 1.0f);
        pr.driver->setRenderTarget(nullptr, false, false);
    } else {
        rtt = nullptr;
    }

    struct { const char* name; irr::video::E_MATERIAL_TYPE type; } kDriverDraws[] = {
        {"driver_alpha_ref", irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF},
        {"driver_alpha_blend", irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL},
        {"driver_solid", irr::video::EMT_SOLID},
    };
    for (const auto& d : kDriverDraws) {
        for (int t = 0; t < 3; ++t) {
            irr::video::ITexture* tex = t == 0 ? uploaded : (t == 1 ? wrapped : rtt);
            if (!tex) continue;
            pr.begin();
            pr.driverDraw(tex, d.type);
            pr.report(d.name, t == 0 ? "upload" : (t == 1 ? "wrapped" : "rtt"), "builtin");
            pr.end();
        }
    }

    // Raw programs last: they leave the driver's cached GL state stale.
    struct { const char* name; const char* fs; } kRaw[] = {
        {"raw_alpha3d_base", kAlpha3dBase},
        {"raw_alpha3d_derivatives", kAlpha3dDerivatives},
        {"raw_alpha_as_colour", kAlphaAsColour},
        {"raw_compare_base", kCompareBase},
        {"raw_compare_derivatives", kCompareDerivatives},
        {"raw_threshold_as_colour", kThresholdAsColour},
        {"raw_fwidth_unused_in_discard", kFwidthUnusedInDiscard},
        {"raw_uniform_threshold", kUniformThreshold},
        {"raw_derivatives_step", kDerivativesStep},
        {"raw_derivatives_discard_last", kDerivativesDiscardLast},
        {"raw_derivative_of_other_value", kDerivativeOfOtherValue},
    };
    for (const auto& r : kRaw) {
        GLuint fresh = link(r.fs);
        if (!fresh) {
            printf("[alpha-probe] draw=%s prog=fresh UNAVAILABLE\n", r.name);
            continue;
        }
        GLuint bin = roundTrip(fresh);
        pr.begin();
        rawDraw(fresh, rawTex);
        pr.report(r.name, "raw", "fresh");
        pr.end();
        if (bin) {
            pr.begin();
            rawDraw(bin, rawTex);
            pr.report(r.name, "raw", "binary");
            pr.end();
            glDeleteProgram(bin);
        } else {
            printf("[alpha-probe] draw=%s prog=binary UNAVAILABLE (no program binary format or load failed)\n",
                   r.name);
        }
        glDeleteProgram(fresh);
    }

    device->drop();
    return 0;
}
