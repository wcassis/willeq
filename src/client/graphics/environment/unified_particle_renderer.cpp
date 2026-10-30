#ifdef EQT_HAS_GLES2

#include "client/graphics/environment/unified_particle_renderer.h"
#include "client/graphics/environment/spell_spawn_shape.h"  // B1151: eddAlignAxis / eddAlignAngle512
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "client/core/gem_owner_census.h"  // B972: GemTag for the VBO's census row
#include "common/logging.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cstddef>

// The driver's raw-GL accounting seams (docker/irrlicht-drm/COpenGLES2Driver.cpp).
extern void gles2AdjustRawBufferBytes(void* driver, ptrdiff_t delta);
extern void gles2SyncBufferBindings(void* driver);

// GLES2 spec constants — fallback defines for older headers
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif
#ifndef GL_DEPTH_WRITEMASK
#define GL_DEPTH_WRITEMASK 0x0B72
#endif

namespace EQT {
namespace Graphics {
namespace Environment {

// === Point sprite shader sources ===

// Vertex shader: uses separate View and Projection matrices (same convention
// as built-in COGLES2 shaders — guaranteed to work with Irrlicht's matrix layout)
static const char* POINT_SPRITE_VS = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec4 aColor;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;
attribute float aSpan;   // B1188: the sprite's stored frame, in CELLS (1/2/4)

uniform mat4 uProj;
uniform mat4 uView;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uScreenHeight;

varying vec4 vColor;
varying float vFogFactor;
varying vec2 vAtlasOffset;
varying float vRotation;
varying float vSpan;

void main() {
    vec4 eyePos = uView * vec4(aPosition, 1.0);
    vec4 clipPos = uProj * eyePos;
    gl_Position = clipPos;

    // Perspective-scaled point size: larger when close, smaller when far
    // aTexCoord0.x contains the desired world-space size
    float pointSize = aTexCoord0.x;
    float dist = clipPos.w;
    if (dist > 0.0) {
        gl_PointSize = pointSize * uScreenHeight / dist;
    } else {
        gl_PointSize = 0.0;
    }

    // Clamp point size to reasonable range
    gl_PointSize = clamp(gl_PointSize, 1.0, 100.0);

    // Linear fog based on eye-space distance (matches zone geometry)
    float fogDist = length(eyePos.xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);

    vColor = aColor;
    vAtlasOffset = aTexCoord1;
    vRotation = aTexCoord0.y;
    vSpan = aSpan;
}
)";

// Fragment shader: atlas-textured point sprite with vertex color and fog
static const char* POINT_SPRITE_FS = R"(
precision mediump float;

uniform sampler2D uTexture;
// T411 (board B261): the atlas's ETC1 `_alpha` companion on unit 1, or a 1x1
// opaque-white texture when the atlas carries its alpha in band. Multiplying
// unconditionally is exact for both: an ETC1 payload reads texColor.a == 1.0
// so alpha becomes the companion, and an in-band payload multiplies by 1.0.
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;
uniform vec2 uAtlasRegionSize;

varying vec4 vColor;
varying float vFogFactor;
varying vec2 vAtlasOffset;
varying float vRotation;
varying float vSpan;

void main() {
    // Rotate gl_PointCoord (identity when vRotation=0: sin(0)=0, cos(0)=1)
    // Always compute — Mali 400 FS branches cost more than the sin/cos they skip
    vec2 pc = gl_PointCoord - 0.5;
    float s = sin(vRotation);
    float c = cos(vRotation);
    pc = vec2(pc.x * c - pc.y * s, pc.x * s + pc.y * c) + 0.5;

    // Map rotated point coord [0,1] to atlas sub-region UV. B1188: the
    // region is the sprite's STORED frame -- span cells (1/2/4) of the
    // per-draw cell size -- so a 64x64 picture drawn at the client's own
    // size samples its native pixels, not a 2:1 box.
    vec2 uv = vAtlasOffset + pc * (vSpan * uAtlasRegionSize);

    // Sample atlas texture
    vec4 texColor = texture2D(uTexture, uv);
    texColor.a *= texture2D(uAlphaTexture, uv).r;

    // Multiply texture by vertex color (particle color with alpha fade)
    vec4 color = texColor * vColor;

    // Fog: fade particle brightness toward zero rather than mixing toward
    // fog color. With additive blend (GL_ONE, GL_ONE), mix() injects fog
    // color into every particle — even zero-RGB ones — producing visible
    // dots on dark backgrounds. Multiplicative fade is physically correct
    // for light-emitting particles (light dims in fog, doesn't become fog).
    color.rgb *= vFogFactor;

    // Discard fully transparent fragments for performance
    if (color.a < 0.004) discard;

    gl_FragColor = color;
}
)";

// B1149 (#469): the spellsnew.edd sprite -- a screen-facing QUAD w wide and h
// tall in world units at the particle (EQGraphicsDX9 0x1006335a..: half-width
// w * 0.5 / depth times the projection scale), as the official client draws a
// mode-0 sprite. A point sprite cannot: its size is one number, square, and
// clamped (100 px here, the GPU's range beyond that). The corners are built on
// the CPU from the view's right/up axes; aTexCoord0 carries the corner's
// texture coordinate (0..1, top-left origin like gl_PointCoord).
static const char* QUAD_VS = R"(
precision highp float;

attribute vec3 aPosition;
attribute vec4 aColor;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;
attribute float aSpan;   // B1188: the sprite's stored frame, in CELLS (1/2/4)

uniform mat4 uProj;
uniform mat4 uView;
uniform float uFogStart;
uniform float uFogEnd;

varying vec4 vColor;
varying float vFogFactor;
varying vec2 vAtlasOffset;
varying vec2 vCorner;
varying float vSpan;

void main() {
    vec4 eyePos = uView * vec4(aPosition, 1.0);
    gl_Position = uProj * eyePos;
    float fogDist = length(eyePos.xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
    vColor = aColor;
    vAtlasOffset = aTexCoord1;
    vCorner = aTexCoord0;
    vSpan = aSpan;
}
)";

static const char* QUAD_FS = R"(
precision mediump float;

uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;
uniform vec2 uAtlasRegionSize;

varying vec4 vColor;
varying float vFogFactor;
varying vec2 vAtlasOffset;
varying vec2 vCorner;
varying float vSpan;

void main() {
    // B1188: the corner spans the sprite's stored frame (span cells), not
    // one cell -- a 64x64 picture drawn at the client's size samples its
    // native pixels.
    vec2 uv = vAtlasOffset + clamp(vCorner, 0.0, 1.0) * (vSpan * uAtlasRegionSize);
    vec4 texColor = texture2D(uTexture, uv);
    texColor.a *= texture2D(uAlphaTexture, uv).r;
    vec4 color = texColor * vColor;
    color.rgb *= vFogFactor;
    if (color.a < 0.004) discard;
    gl_FragColor = color;
}
)";

// === Implementation ===

UnifiedParticleRenderer::UnifiedParticleRenderer() = default;

UnifiedParticleRenderer::~UnifiedParticleRenderer() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (quad_.program != 0) {  // B1149
        glDeleteProgram(quad_.program);
        quad_.program = 0;
    }
    if (opaqueAlphaFallback_ != 0) {   // T411
        glDeleteTextures(1, &opaqueAlphaFallback_);
        opaqueAlphaFallback_ = 0;
    }
    if (vertexVbo_[0] != 0 || vertexVboBytes_[0] != 0) {  // B972
        size_t total = 0;
        for (size_t i = 0; i < kVboRingFrames; ++i) {
            if (vertexVbo_[i] != 0) glDeleteBuffers(1, &vertexVbo_[i]);
            total += vertexVboBytes_[i];
        }
        if (driver_ && total != 0)
            gles2AdjustRawBufferBytes(driver_, -static_cast<ptrdiff_t>(total));
        for (size_t i = 0; i < kVboRingFrames; ++i) {
            vertexVbo_[i] = 0;
            vertexVboBytes_[i] = 0;
        }
    }
}

GLuint UnifiedParticleRenderer::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            std::vector<char> log(logLen);
            glGetShaderInfoLog(shader, logLen, nullptr, log.data());
            LOG_ERROR(MOD_GRAPHICS, "UnifiedParticleRenderer: Shader compile error: {}",
                      log.data());
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint UnifiedParticleRenderer::linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    if (prog == 0) return 0;

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    // Bind standard attribute locations (same as COGLES2ShaderManager::linkProgram)
    glBindAttribLocation(prog, 0, "aPosition");
    glBindAttribLocation(prog, 1, "aNormal");
    glBindAttribLocation(prog, 2, "aColor");
    glBindAttribLocation(prog, 3, "aTexCoord0");
    glBindAttribLocation(prog, 4, "aTexCoord1");
    glBindAttribLocation(prog, 5, "aSpan");  // B1188: stored frame in cells

    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            std::vector<char> log(logLen);
            glGetProgramInfoLog(prog, logLen, nullptr, log.data());
            LOG_ERROR(MOD_GRAPHICS, "UnifiedParticleRenderer: Program link error: {}",
                      log.data());
        }
        glDeleteProgram(prog);
        return 0;
    }

    glDetachShader(prog, vs);
    glDetachShader(prog, fs);
    return prog;
}

bool UnifiedParticleRenderer::init(irr::video::IVideoDriver* driver) {
    driver_ = driver;
    // Compile vertex shader
    GLuint vs = compileShader(GL_VERTEX_SHADER, POINT_SPRITE_VS);
    if (vs == 0) {
        LOG_ERROR(MOD_GRAPHICS, "UnifiedParticleRenderer: Failed to compile VS");
        return false;
    }

    // Compile fragment shader
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, POINT_SPRITE_FS);
    if (fs == 0) {
        glDeleteShader(vs);
        LOG_ERROR(MOD_GRAPHICS, "UnifiedParticleRenderer: Failed to compile FS");
        return false;
    }

    // Link program
    program_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (program_ == 0) {
        LOG_ERROR(MOD_GRAPHICS, "UnifiedParticleRenderer: Failed to link program");
        return false;
    }

    // Cache uniform locations
    uProj_ = glGetUniformLocation(program_, "uProj");
    uView_ = glGetUniformLocation(program_, "uView");
    uFogStart_ = glGetUniformLocation(program_, "uFogStart");
    uFogEnd_ = glGetUniformLocation(program_, "uFogEnd");
    uFogColor_ = glGetUniformLocation(program_, "uFogColor");
    uTexture_ = glGetUniformLocation(program_, "uTexture");
    uAlphaTexture_ = glGetUniformLocation(program_, "uAlphaTexture");
    uAtlasRegionSize_ = glGetUniformLocation(program_, "uAtlasRegionSize");
    uScreenHeight_ = glGetUniformLocation(program_, "uScreenHeight");

    // B1149: the spellsnew.edd sprite quads' program (same attribute slots).
    {
        GLuint qvs = compileShader(GL_VERTEX_SHADER, QUAD_VS);
        GLuint qfs = qvs ? compileShader(GL_FRAGMENT_SHADER, QUAD_FS) : 0;
        if (qvs && qfs) quad_.program = linkProgram(qvs, qfs);
        if (qvs) glDeleteShader(qvs);
        if (qfs) glDeleteShader(qfs);
        if (quad_.program == 0) {
            LOG_ERROR(MOD_GRAPHICS, "UnifiedParticleRenderer: Failed to build the sprite-quad program");
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }
        quad_.uProj = glGetUniformLocation(quad_.program, "uProj");
        quad_.uView = glGetUniformLocation(quad_.program, "uView");
        quad_.uFogStart = glGetUniformLocation(quad_.program, "uFogStart");
        quad_.uFogEnd = glGetUniformLocation(quad_.program, "uFogEnd");
        quad_.uFogColor = glGetUniformLocation(quad_.program, "uFogColor");
        quad_.uTexture = glGetUniformLocation(quad_.program, "uTexture");
        quad_.uAlphaTexture = glGetUniformLocation(quad_.program, "uAlphaTexture");
        quad_.uAtlasRegionSize = glGetUniformLocation(quad_.program, "uAtlasRegionSize");
    }

    // T411: 1x1 opaque-white companion fallback (see the header). Created here
    // rather than lazily in render() because render() saves/restores unit 0's
    // binding around itself and a lazy glGenTextures there would have to be
    // careful about which unit is active -- exactly the P09b footgun.
    {
        GLint savedActive = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActive);
        GLint savedTex = 0;
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTex);
        glGenTextures(1, &opaqueAlphaFallback_);
        glBindTexture(GL_TEXTURE_2D, opaqueAlphaFallback_);
        const unsigned char white[4] = {255, 255, 255, 255};
        {
            eqt::memory::GemTag gemTag("tex:particle-alpha-fallback 1x1");
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(savedTex));
        glActiveTexture(static_cast<GLenum>(savedActive));
    }

    // Query max point size
    float pointSizeRange[2] = {1.0f, 64.0f};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, pointSizeRange);
    maxPointSize_ = pointSizeRange[1];

    // Reserve vertex array space
    winAdditive_.on.reserve(1024);
    winAlpha_.on.reserve(256);
    winSpriteAdditive_.on.reserve(1024);
    winSpriteAlpha_.on.reserve(256);
    winAdditiveSrcAlpha_.on.reserve(64);        // B1146
    winSpriteAdditiveSrcAlpha_.on.reserve(256);

    // B972 (#290) / B972-FT (#326), the ALTERNATIVE shape: the ring's VBOs
    // are created EMPTY and grown on first use in uploadPackedVerts(); the
    // byte deltas go through the driver's logical tally so the draws'
    // storage stops being UNNAMED GEM (the 1 MiB stream-upload slots the
    // old client-array paths warmed up stay freed with them).

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "UnifiedParticleRenderer: Initialized (max point size: {:.0f}, "
             "uProj={}, uView={}, uFogStart={}, uScreenHeight={})",
             maxPointSize_, uProj_, uView_, uFogStart_, uScreenHeight_);
    return true;
}

void UnifiedParticleRenderer::render(const UnifiedParticle* particles, int count,
                                      const float* viewMatrix, const float* projMatrix,
                                      float fogStart, float fogEnd, const float* fogColor,
                                      float screenHeight, GLuint atlasTexture,
                                      GLuint alphaCompanion, GLuint spriteTexture,
                                      GLuint spriteAlphaCompanion) {
    if (!initialized_ || count <= 0 || !particles) return;
    // B972-FT: the ring's row advances once per render(), not per batch.
    ++frameCount_;

    auto t0 = std::chrono::steady_clock::now();

    // Separate particles by SHEET and blend mode, and fill vertex arrays.
    // Both are per-draw state -- the bound texture and uAtlasRegionSize for
    // the sheet, glBlendFunc for the mode -- so each combination is a batch.
    winAdditive_.clear();
    winAlpha_.clear();
    winSpriteAdditive_.clear();
    winSpriteAlpha_.clear();
    winAdditiveSrcAlpha_.clear();
    winSpriteAdditiveSrcAlpha_.clear();
    for (auto& q : winQuad_) q.clear();  // B1149

    // B1149: the view's right and up axes in world space (Irrlicht's view
    // matrix holds them in its first two columns: M[0],M[4],M[8] / M[1],M[5],M[9]).
    const glm::vec3 viewRight(viewMatrix[0], viewMatrix[4], viewMatrix[8]);
    const glm::vec3 viewUp(viewMatrix[1], viewMatrix[5], viewMatrix[9]);

    for (int i = 0; i < count; ++i) {
        const UnifiedParticle& p = particles[i];
        if (!p.isAlive()) continue;
        // B1151: +0x198 (draw site 0x10062e5c): inward radial motion past the
        // axis (r < 0) is not drawn unless the word == 1.
        if (!p.eddDraws()) continue;

        // T417 (board B300): an index at or above kBase addresses the spell
        // sprite sheet, anything below it the procedural atlas (4x4 tiles).
        const bool onSpriteSheet = p.textureIndex >= SpellSpriteAtlas::kBase;

        // A sprite particle with no sheet loaded is SKIPPED, never drawn
        // with the procedural atlas's UVs -- that would paint an unrelated
        // tile with a confident face on it.
        if (onSpriteSheet && spriteTexture == 0) continue;

        float atlasU, atlasV;
        if (onSpriteSheet) {
            // T420 (board B355): THE SPRITE ANIMATES. The ordinal names a
            // SPRITE, not a cell -- 528 cells do not fit in the uint8_t
            // `EmitterConfig::textureRegions[]` -- and the cell is derived
            // here, per particle, per frame, from how far through its life
            // the particle is. That is the whole animation: the sheets are
            // fade-in/peak/fade-out cycles, so one playthrough per lifetime
            // makes a particle bloom and vanish the way the official
            // client's do. No shader change was needed; the offset was
            // already computed on the CPU and handed over as a vertex
            // attribute.
            //
            // B1124 (#444): only a GRID sheet animates. A single-image sheet
            // (spelc3, spelc4, ... -- `SpellSpriteAtlas::kSheetFrames`) is one
            // picture packed whole into its first cell and held for the
            // particle's whole life; stepping it would walk fifteen
            // transparent cells.
            const int sprite = p.textureIndex - SpellSpriteAtlas::kBase;
            int cell;
            if (p.eddFrameCount > 1) {
                // B1151 (#471): the record's frame rule (+0x130/+0x134,
                // TRACE.md (c), 0x10063b7a..0x10063d03): N frames,
                // frame = ftol(age * fps) % N, row-major in the N-frame grid
                // (2x2 / 4x2 / 4x4); N > 16 the whole texture (N == 1). The
                // builder packs a sprite's frames into the first N of its 16
                // cells, so the frame is the cell offset.
                cell = SpellSpriteAtlas::cellFor(
                    sprite,
                    std::min(p.eddFrame(), SpellSpriteAtlas::framesFor(sprite) - 1));
            } else {
                // The record-less path is unchanged (T420/B1124): the
                // single-image sheets hold their cell all life; only a true
                // grid sheet animates, once per particle life, by normalized
                // age.
                cell = SpellSpriteAtlas::cellForAge(sprite, p.getNormalizedAge());
            }
            atlasU = (cell % SpellSpriteAtlas::kColumns) *
                     (1.0f / SpellSpriteAtlas::kColumns);
            atlasV = (cell / SpellSpriteAtlas::kColumns) *
                     (1.0f / SpellSpriteAtlas::kRows);
        } else {
            const int col = p.textureIndex % 4;
            const int row = p.textureIndex / 4;
            atlasU = col * 0.25f;
            atlasV = row * 0.25f;
        }

        // B1146 (#466): a particle on the client's alpha (hasEddColour())
        // blends the client's way -- additive is SRC_ALPHA, ONE, so its alpha
        // (fade x peak) scales what it adds; every other additive particle
        // keeps ONE, ONE exactly as before.
        const bool additive = p.getBlendMode() == UnifiedBlendMode::ADDITIVE;
        const bool srcAlphaAdditive = additive && p.hasEddColour();

        // B1151 (#471): the +0x64 depth-write word (EQGraphicsDX9
        // 0x1005f7a8/0x1005f82c): the official client's EDD emitter draw
        // runs with ZWRITEENABLE=1 (depth write ON) by default and turns it
        // to 0 before the draw -- and back to 1 after -- for a record whose
        // +0x64 == 1. A record particle therefore follows its record
        // (depthOn = hasEddMotion() && +0x64 == 0); every non-record
        // particle (config presets, weather, ...) keeps fable's
        // long-standing OFF state (the .off sub-windows, drawn with the
        // mask FALSE as before).
        const bool depthOn = p.hasEddMotion() && p.eddNoDepthWrite == 0;

        // B1150 (#470): the +0xD0 view-depth bias (EQGraphicsDX9 0x10063069 /
        // 0x10063198: the view depth has D_D0 subtracted BEFORE the near/far
        // cull and the 1.0/depth perspective divide). This renderer projects
        // in the vertex shader (clipPos = uProj * uView * pos), so the bias
        // is applied by translating the world position along r -- the view
        // matrix's rotated row 3, the unit vector from scene toward camera
        // (r . viewDir = -1) -- by D: that changes view.z by exactly D, i.e.
        // the depth (front-of-camera) by -D, and the shader's cull, divide
        // and point-size scale all see the biased depth.
        glm::vec3 pos(p.position);
        if (p.eddDepthBias != 0.0f) {
            const glm::vec3 r(viewMatrix[8], viewMatrix[9], viewMatrix[10]);
            pos += r * p.eddDepthBias;
        }

        // B995: the family's Options opacity / near clip (identity by default).
        // The Options opacity is the client's SpellParticleOpacity, which
        // multiplies the alpha (0x10063a85); a ONE, ONE particle has no alpha
        // in its blend, so it takes the opacity in its RGB instead.
        // B1150: the near clip sees the biased position (the client culls on
        // the biased depth).
        glm::vec4 color = p.color;
        if (!applyParticleKnobs(knobs_, p.getFamily(), additive && !srcAlphaAdditive,
                                pos, cameraPos_, color)) {
            continue;
        }

        PointSpriteVertex v;
        v.x = pos.x;
        v.y = pos.y;
        v.z = pos.z;
        v.r = color.r;
        v.g = color.g;
        v.b = color.b;
        v.a = color.a;
        v.pointSize = p.size;
        v.rotation = p.rotation;
        v.atlasU = atlasU;
        v.atlasV = atlasV;
        // B1188: the draw's UV region is the sprite's stored frame -- span
        // cells wide/tall (1 grid cell, 2x2 for a native 64x64 picture, the
        // whole block for glowflare02). Procedural-atlas tiles are one
        // 0.25 region, so span 1 in that per-draw unit too.
        v.span = onSpriteSheet
                     ? static_cast<float>(SpellSpriteAtlas::storedSpan(
                           p.textureIndex - SpellSpriteAtlas::kBase))
                     : 1.0f;

        if (p.hasEddSprite()) {
            // B1149: six vertices of the world-unit quad, corners rotated by
            // the particle's spin in the screen plane; batch index as below.
            const int b = onSpriteSheet ? (srcAlphaAdditive ? 5 : additive ? 2 : 3)
                                        : (srcAlphaAdditive ? 4 : additive ? 0 : 1);
            const float hw = 0.5f * p.size, hh = 0.5f * p.height;
            // B1151 (#471): the record's spin (age*spin + start + align; draws
            // 0x100634c1/0x100638e8) replaces the config rotation for a
            // record particle; the +0x178 alignment is a SCREEN angle, so it
            // is measured here, where the camera is (the axis dispatch and
            // the angle are the pure helpers in spell_spawn_shape.h).
            float spinAngle = p.rotation;
            if (p.hasEddMotion()) {
                float align512 = 0.0f;
                if (p.eddAlignWord != 0) {
                    // PARTIAL as the TSV row says: the attach-matrix column
                    // letters go through FUN_100399f0 (untraced); fable uses
                    // A = the facing, B = up, C = the perpendicular of the
                    // particle's own frame (spell_spawn_shape.h eddAlignAxis).
                    const glm::vec3 axis =
                        eddAlignAxis(p.eddAlignWord, p.eddBasis2, p.eddBasis1, p.eddBasis3);
                    align512 = eddAlignAngle512(
                        axis, p.position - p.eddBase, viewRight, viewUp);
                }
                spinAngle = (p.eddSpinAngle512() + align512) * (6.283185307179586f / 512.0f);
            }
            const float cs = std::cos(spinAngle), sn = std::sin(spinAngle);
            const glm::vec3 c(pos.x, pos.y, pos.z);
            auto pushCorner = [&](const glm::vec3& w, float u, float t) {
                PointSpriteVertex q = v;
                q.x = w.x;
                q.y = w.y;
                q.z = w.z;
                q.pointSize = u;  // aTexCoord0 = the corner's texture coordinate
                q.rotation = t;
                (depthOn ? winQuad_[b].on : winQuad_[b].off).push_back(q);  // B1151: +0x64
            };
            if (p.hasEddMotion() && p.eddSpriteMode == 2) {
                // B1151 mode 2 (TSV +0x70 row; listing 0x100634b0..0x10063680):
                // the WORLD-ORIENTED quad -- NOT screen-facing. axisA/axisB
                // are the emitter's post-tilt basis 2/3 rotated about basis 1
                // by the spin angle (the 0x1005dc80 matrix-vector), and the
                // corners are pos +- (w*s*0.5)*axisA +- (h*s*0.5)*axisB
                // (0x100635b3..0x100635ba, 0x100635f2..0x10063600, 0.5 at
                // 0x101c594c). p.size/p.height carry the w*s / h*s products
                // (B1150's +0x19C scale where the record says so).
                const glm::vec3 axisA = p.eddBasis2 * cs + p.eddBasis3 * sn;
                const glm::vec3 axisB = p.eddBasis2 * -sn + p.eddBasis3 * cs;
                pushCorner(c + axisA * hw + axisB * hh, 0.0f, 0.0f);
                pushCorner(c - axisA * hw + axisB * hh, 1.0f, 0.0f);
                pushCorner(c - axisA * hw - axisB * hh, 1.0f, 1.0f);
                pushCorner(c + axisA * hw + axisB * hh, 0.0f, 0.0f);
                pushCorner(c - axisA * hw - axisB * hh, 1.0f, 1.0f);
                pushCorner(c + axisA * hw - axisB * hh, 0.0f, 1.0f);
                continue;
            }
            if (p.hasEddMotion() && p.eddSpriteMode == 1) {
                // B1151 mode 1 (TSV +0x70 row): the STREAK -- a second point
                // pos + emitter-axis*(s*h) and a quad stretched between the
                // two PROJECTED points. The emitter axis is the post-tilt
                // basis 1 (PARTIAL: the client's axis word is not traced past
                // "the emitter's axis"); the length is the drawn height
                // (h*s, the same product the billboard's hh uses). The corners
                // are built in view space (world-unit X/Y/Z) and mapped back
                // through the view basis, so the quad projects to the streak.
                const glm::vec3 r(viewMatrix[8], viewMatrix[9], viewMatrix[10]);
                auto toView = [&](const glm::vec3& w) {
                    const glm::vec3 d = w - cameraPos_;
                    return glm::vec3(glm::dot(viewRight, d), glm::dot(viewUp, d),
                                     glm::dot(r, d));
                };
                auto fromView = [&](const glm::vec3& vv) {
                    return cameraPos_ + viewRight * vv.x + viewUp * vv.y + r * vv.z;
                };
                const glm::vec3 v1 = toView(c);
                const glm::vec3 v2 = toView(c + p.eddBasis1 * p.height);
                glm::vec3 longAxis = v2 - v1;
                if (glm::dot(longAxis, longAxis) < 1e-12f) longAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                longAxis = glm::normalize(longAxis);
                glm::vec3 perp(longAxis.y, -longAxis.x, 0.0f);
                if (glm::dot(perp, perp) < 1e-12f) perp = glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec3 ca = fromView(v1 + perp * hw);
                const glm::vec3 cb = fromView(v1 - perp * hw);
                const glm::vec3 cc = fromView(v2 - perp * hw);
                const glm::vec3 cd = fromView(v2 + perp * hw);
                // the width axis is the texture v; the streak length the u
                pushCorner(ca, 0.0f, 0.0f);
                pushCorner(cb, 0.0f, 1.0f);
                pushCorner(cc, 1.0f, 1.0f);
                pushCorner(ca, 0.0f, 0.0f);
                pushCorner(cc, 1.0f, 1.0f);
                pushCorner(cd, 1.0f, 0.0f);
                continue;
            }
            auto corner = [&](float sx, float sy, float u, float t) {
                const float dx = sx * hw, dy = sy * hh;
                const glm::vec3 w = c + viewRight * (dx * cs - dy * sn) + viewUp * (dx * sn + dy * cs);
                pushCorner(w, u, t);
            };
            corner(-1.0f, 1.0f, 0.0f, 0.0f);
            corner(1.0f, 1.0f, 1.0f, 0.0f);
            corner(1.0f, -1.0f, 1.0f, 1.0f);
            corner(-1.0f, 1.0f, 0.0f, 0.0f);
            corner(1.0f, -1.0f, 1.0f, 1.0f);
            corner(-1.0f, -1.0f, 0.0f, 1.0f);
            continue;
        }

        if (onSpriteSheet) {
            DepthWindow& w = srcAlphaAdditive ? winSpriteAdditiveSrcAlpha_
                              : additive ? winSpriteAdditive_ : winSpriteAlpha_;
            (depthOn ? w.on : w.off).push_back(v);  // B1151: +0x64
        } else {
            DepthWindow& w = srcAlphaAdditive ? winAdditiveSrcAlpha_
                              : additive ? winAdditive_ : winAlpha_;
            (depthOn ? w.on : w.off).push_back(v);  // B1151: +0x64
        }
    }

    bool anyQuad = false;
    for (const auto& q : winQuad_) anyQuad = anyQuad || q.any();
    if (!winAdditive_.any() && !winAlpha_.any() &&
        !winSpriteAdditive_.any() && !winSpriteAlpha_.any() &&
        !winAdditiveSrcAlpha_.any() && !winSpriteAdditiveSrcAlpha_.any() && !anyQuad) {
        return;
    }

    auto t1 = std::chrono::steady_clock::now();

    // ===== One-shot diagnostic =====
    static bool diagLogged = false;
    if (!diagLogged) {
        diagLogged = true;
        LOG_INFO(MOD_GRAPHICS,
                 "UnifiedParticleRenderer DIAG: additive={} alpha={} atlasGL={} "
                 "uProj={} uView={}",
                 winAdditive_.total(), winAlpha_.total(), atlasTexture,
                 uProj_, uView_);
        if (winAdditive_.any()) {
            const auto& v = winAdditive_.on.empty() ? winAdditive_.off[0] : winAdditive_.on[0];
            LOG_INFO(MOD_GRAPHICS,
                     "UnifiedParticleRenderer DIAG: first particle world=({:.1f},{:.1f},{:.1f}) "
                     "size={:.2f} color=({:.2f},{:.2f},{:.2f},{:.2f})",
                     v.x, v.y, v.z, v.pointSize, v.r, v.g, v.b, v.a);
        }
    }

    // ===== Save ALL GL state we will modify =====
    GLint savedProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);

    GLboolean savedBlendEnabled = glIsEnabled(GL_BLEND);
    GLint savedBlendSrc = 0, savedBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &savedBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &savedBlendDst);

    GLboolean savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean savedDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &savedDepthMask);

    GLboolean savedCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean savedScissorTest = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean savedStencilTest = glIsEnabled(GL_STENCIL_TEST);

    GLint savedActiveTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTexture);

    glActiveTexture(GL_TEXTURE0);
    GLint savedTexture0 = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture0);

    // T411: unit 1 now carries the atlas's alpha companion, so it has to be
    // saved and restored exactly like unit 0 -- this renderer runs inside an
    // Irrlicht frame and must leave every unit as it found it.
    glActiveTexture(GL_TEXTURE1);
    GLint savedTexture1 = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture1);
    glActiveTexture(GL_TEXTURE0);

    GLint savedVBO = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &savedVBO);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLint savedEBO = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &savedEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    auto t2 = std::chrono::steady_clock::now();

    // ===== Set particle rendering state =====
    // Depth test ON (cull particles behind walls). Depth write: OFF is
    // fable's long-standing default for FX (the .off sub-windows), and the
    // B1151 +0x64 == 0 record sub-windows (.on) toggle the mask to ON for
    // their own draws (EQGraphicsDX9 0x1005f7a8/0x1005f82c) -- see
    // drawDepthWindow below.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);

    // ===== Bind our program and set uniforms =====
    glUseProgram(program_);

    // Pass View and Projection as separate uniforms — same convention as
    // the built-in COGLES2 shaders. GLSL multiplies: clipPos = uProj * uView * pos.
    if (uProj_ >= 0) glUniformMatrix4fv(uProj_, 1, GL_FALSE, projMatrix);
    if (uView_ >= 0) glUniformMatrix4fv(uView_, 1, GL_FALSE, viewMatrix);
    if (uFogStart_ >= 0) glUniform1f(uFogStart_, fogStart);
    if (uFogEnd_ >= 0) glUniform1f(uFogEnd_, fogEnd);
    if (uScreenHeight_ >= 0) glUniform1f(uScreenHeight_, screenHeight);

    if (uFogColor_ >= 0) {
        if (fogColor) {
            glUniform4fv(uFogColor_, 1, fogColor);
        } else {
            float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            glUniform4fv(uFogColor_, 1, black);
        }
    }

    if (uTexture_ >= 0) glUniform1i(uTexture_, 0);
    if (uAlphaTexture_ >= 0) glUniform1i(uAlphaTexture_, 1);

    // B1149: the same per-frame uniforms on the sprite-quad program.
    if (anyQuad) {
        glUseProgram(quad_.program);
        if (quad_.uProj >= 0) glUniformMatrix4fv(quad_.uProj, 1, GL_FALSE, projMatrix);
        if (quad_.uView >= 0) glUniformMatrix4fv(quad_.uView, 1, GL_FALSE, viewMatrix);
        if (quad_.uFogStart >= 0) glUniform1f(quad_.uFogStart, fogStart);
        if (quad_.uFogEnd >= 0) glUniform1f(quad_.uFogEnd, fogEnd);
        if (quad_.uFogColor >= 0) {
            const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            glUniform4fv(quad_.uFogColor, 1, fogColor ? fogColor : black);
        }
        if (quad_.uTexture >= 0) glUniform1i(quad_.uTexture, 0);
        if (quad_.uAlphaTexture >= 0) glUniform1i(quad_.uAlphaTexture, 1);
        glUseProgram(program_);
    }

    auto t3 = std::chrono::steady_clock::now();

    // B972-FT (alternative shape): pack the frame's non-empty batches into
    // ONE bounded VBO (the ring row) with a single upload, then draw each
    // batch's vertex window. The job references one VBO -- the pre-ring
    // cut's shape -- so Mesa's driver-retained per-BO scratch stays at the
    // cut's level, while the ring keeps issue #326's write-wait gone.
    GLuint packVbo = 0;
    {
        packedVerts_.clear();
        // B1151: each window packs its .on sub-window first, then .off, in
        // window order -- the draw side walks the pack in the same order and
        // sets the depth mask per sub-window.
        auto packWindow = [](std::vector<PointSpriteVertex>& v,
                             const DepthWindow& w) {
            v.insert(v.end(), w.on.begin(), w.on.end());
            v.insert(v.end(), w.off.begin(), w.off.end());
        };
        packWindow(packedVerts_, winAdditive_);
        packWindow(packedVerts_, winAdditiveSrcAlpha_);
        packWindow(packedVerts_, winAlpha_);
        packWindow(packedVerts_, winSpriteAdditive_);
        packWindow(packedVerts_, winSpriteAdditiveSrcAlpha_);
        packWindow(packedVerts_, winSpriteAlpha_);
        for (const auto& q : winQuad_) packWindow(packedVerts_, q);  // B1149: batches 0..5 as quads
        if (useVbo_ && !packedVerts_.empty()) packVbo = uploadPackedVerts();
    }
    const PointSpriteVertex* pv = packVbo ? packedVerts_.data() : nullptr;
    size_t cursor = 0;
    bool packedBound = false;
    auto drawWindow = [&](const std::vector<PointSpriteVertex>& src, UnifiedBlendMode mode,
                          int batchIdx, GLenum primitive = GL_POINTS) -> size_t {
        (void)batchIdx;  // the batch identity, kept for the draw order and log
        const size_t start = cursor;
        cursor += src.size();
        if (!src.empty()) {
            // VBO path: start is the batch's window into the PACK (one
            // buffer). Client-array path: start is 0 into the batch's own
            // array (there is no pack) -- mixing the two is an OOB read.
            // The bind + attribute pointers go to the first batch that
            // ACTUALLY draws (an earlier empty batch must not consume the
            // flag, or the real batch would draw with stale pointers).
            const bool bindAndPointers = packVbo && !packedBound;
            renderBatch(packVbo ? pv : src.data(), packVbo ? start : 0,
                        src.size(), mode, batchIdx, bindAndPointers, primitive);
            if (bindAndPointers) packedBound = true;
        }
        return start;
    };

    // B1151 (#471): draw a window as up to TWO sub-passes with their own
    // depth-write state: the .on sub-window (record particles whose +0x64
    // == 0, the official client's ZWRITEENABLE=1 default) first with the
    // mask ON, then the .off sub-window (everything else -- the official
    // client's +0x64 == 1 draws, plus all non-record FX) with it OFF, as
    // before. The official client interleaves the two classes per emitter;
    // the per-sub-window order is preserved here (additive windows are
    // order-invariant; alpha windows differ only in the blend interleaving
    // across the two sub-populations).
    auto drawDepthWindow = [&](const DepthWindow& w, UnifiedBlendMode mode,
                               int batchIdx, GLenum primitive = GL_POINTS) {
        if (!w.on.empty()) {
            glDepthMask(GL_TRUE);
            drawWindow(w.on, mode, batchIdx, primitive);
        }
        if (!w.off.empty()) {
            glDepthMask(GL_FALSE);
            drawWindow(w.off, mode, batchIdx, primitive);
        }
    };

    // ── Sheet 0: the 4x4 procedural particle atlas ──────────────────────
    if (winAdditive_.any() || winAlpha_.any() || winAdditiveSrcAlpha_.any()) {
        if (uAtlasRegionSize_ >= 0) glUniform2f(uAtlasRegionSize_, 0.25f, 0.25f);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,
                      alphaCompanion ? alphaCompanion : opaqueAlphaFallback_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);

        drawDepthWindow(winAdditive_, UnifiedBlendMode::ADDITIVE, 0);
        drawDepthWindow(winAdditiveSrcAlpha_, UnifiedBlendMode::ADDITIVE_SRC_ALPHA, 4);
        drawDepthWindow(winAlpha_, UnifiedBlendMode::ALPHA, 1);
    }

    auto t4 = std::chrono::steady_clock::now();

    // ── Sheet 1: the spell sprite sheet (T417, board B300) ──────────────
    // The binning loop already dropped these when spriteTexture is 0, so
    // reaching here with a non-empty batch means the sheet is loaded.
    if (winSpriteAdditive_.any() || winSpriteAlpha_.any() ||
        winSpriteAdditiveSrcAlpha_.any()) {
        if (uAtlasRegionSize_ >= 0) {
            glUniform2f(uAtlasRegionSize_, 1.0f / SpellSpriteAtlas::kColumns,
                        1.0f / SpellSpriteAtlas::kRows);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, spriteAlphaCompanion ? spriteAlphaCompanion
                                                          : opaqueAlphaFallback_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, spriteTexture);

        drawDepthWindow(winSpriteAdditive_, UnifiedBlendMode::ADDITIVE, 2);
        drawDepthWindow(winSpriteAdditiveSrcAlpha_, UnifiedBlendMode::ADDITIVE_SRC_ALPHA, 5);
        drawDepthWindow(winSpriteAlpha_, UnifiedBlendMode::ALPHA, 3);
    }

    // ── B1149: the spellsnew.edd sprite quads, both sheets ──────────────
    // Packed after every point batch, in batch order 0..5 (the cursor walks
    // the pack in that order).
    if (anyQuad) {
        glUseProgram(quad_.program);
        static const UnifiedBlendMode kMode[6] = {
            UnifiedBlendMode::ADDITIVE, UnifiedBlendMode::ALPHA, UnifiedBlendMode::ADDITIVE,
            UnifiedBlendMode::ALPHA, UnifiedBlendMode::ADDITIVE_SRC_ALPHA, UnifiedBlendMode::ADDITIVE_SRC_ALPHA};
        for (int b = 0; b < 6; ++b) {
            const bool sprite = (b == 2 || b == 3 || b == 5);
            if (winQuad_[b].any()) {
                if (quad_.uAtlasRegionSize >= 0) {
                    if (sprite)
                        glUniform2f(quad_.uAtlasRegionSize, 1.0f / SpellSpriteAtlas::kColumns,
                                    1.0f / SpellSpriteAtlas::kRows);
                    else
                        glUniform2f(quad_.uAtlasRegionSize, 0.25f, 0.25f);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, sprite ? (spriteAlphaCompanion ? spriteAlphaCompanion
                                                                            : opaqueAlphaFallback_)
                                                    : (alphaCompanion ? alphaCompanion : opaqueAlphaFallback_));
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sprite ? spriteTexture : atlasTexture);
            }
            drawDepthWindow(winQuad_[b], kMode[b], 6 + b, GL_TRIANGLES);
        }
    }

    auto t5 = std::chrono::steady_clock::now();

    // ===== Restore ALL GL state =====
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    if (savedDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(savedDepthMask);

    if (savedCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (savedScissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (savedStencilTest) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    glBlendFunc(savedBlendSrc, savedBlendDst);
    if (savedBlendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, savedVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, savedEBO);
    // B972: when the batches streamed through the VBO the GL bindings are
    // correct again, but the driver's state_ cache still named the VBO it
    // last drew with; a client-array fallback that skips its bind on a
    // 0-cache would then sample it. Re-read the real bindings from GL.
    if (driver_ && useVbo_) gles2SyncBufferBindings(driver_);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(savedTexture1));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, savedTexture0);
    if (savedActiveTexture != GL_TEXTURE0) glActiveTexture(savedActiveTexture);

    glUseProgram(savedProgram);

    auto t6 = std::chrono::steady_clock::now();

    // Log breakdown only on spike frames (>10ms total)
    auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t6 - t0).count();
    if (totalUs > 10000) {
        auto us = [](auto a, auto b) {
            return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
        };
        LOG_WARN(MOD_GRAPHICS,
                 "UnifiedParticle SPIKE: {:.1f}ms total | fillVerts={:.1f}ms stateSave={:.1f}ms "
                 "setup={:.1f}ms atlas={:.1f}ms(add {} alpha {}) sprites={:.1f}ms"
                 "(add {} alpha {}) restore={:.1f}ms",
                 totalUs / 1000.0f,
                 us(t0, t1) / 1000.0f, us(t1, t2) / 1000.0f,
                 us(t2, t3) / 1000.0f, us(t3, t4) / 1000.0f, winAdditive_.total(),
                 winAlpha_.total(), us(t4, t5) / 1000.0f,
                 winSpriteAdditive_.total(), winSpriteAlpha_.total(),
                 us(t5, t6) / 1000.0f);
    }
}

void UnifiedParticleRenderer::renderBatch(const PointSpriteVertex* verts, size_t start,
                                          size_t count, UnifiedBlendMode blendMode,
                                          int batchIdx, bool bindAndPointers, GLenum primitive) {
    if (count == 0) return;
    const PointSpriteVertex* v = verts + start;
    (void)batchIdx;  // the batch identity for the timing log (uploadPackedVerts)

    if (blendMode == UnifiedBlendMode::ADDITIVE) {
        glBlendFunc(GL_ONE, GL_ONE);
    } else if (blendMode == UnifiedBlendMode::ADDITIVE_SRC_ALPHA) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // B1146: the client's DESTBLEND ONE
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    const int stride = sizeof(PointSpriteVertex);

    if (useVbo_) {
        // B972-FT (alternative shape): re-bind the row's VBO and set the
        // attribute pointers HERE, after the bind (they capture the buffer
        // bound AT CALL TIME, no VAO -- the run-381 pattern: bind first,
        // pointers second), then draw this batch's vertex WINDOW. The
        // pointers name the row's VBO set up by uploadPackedVerts().
        if (bindAndPointers) {
            // The first packed batch of the frame re-establishes the bind the
            // upload's trailing unbind removed; later batches leave it in
            // place (and the pointers already name this VBO).
            glBindBuffer(GL_ARRAY_BUFFER, vertexVbo_[(frameCount_ - 1) % kVboRingFrames]);
            const int stride = sizeof(PointSpriteVertex);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
            glDisableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(12));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(28));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(36));
            glEnableVertexAttribArray(5);
            glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const GLvoid*>(44));
        }
        glDrawArrays(primitive, static_cast<GLsizei>(start),
                     static_cast<GLsizei>(count));
        (void)v;
    } else {
        // The pre-B972 client-array path, kept for the pixel-identity seam
        // (test_b972_particle_vbo renders both and demands a byte-identical
        // framebuffer).
        const char* base = reinterpret_cast<const char*>(v);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, base + 0);

        glDisableVertexAttribArray(1);

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, base + 12);

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, base + 28);

        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, base + 36);

        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, base + 44);

        glDrawArrays(primitive, static_cast<GLsizei>(start),
                     static_cast<GLsizei>(count));
    }
}

// B972 (#290) / B972-FT (#326), the ALTERNATIVE shape: grow the ring row's
// ONE VBO and write the frame's packed point vertices (packedVerts_) into
// it with a single glBufferSubData, then set the point-vertex attribute
// pointers once. The tag spans the whole run: the glBufferData growth AND
// the subdata both allocate GEM storage, and the attribution must name them
// vbo:particles, not leave them for the 1 MiB slot.
//
// The write-wait (issue #326) is measured here: on Lima the subdata's wall
// clock carries lima_transfer_map's lima_flush_job_accessing_bo +
// lima_bo_wait when the written BO is still referenced by an in-flight job.
// The row was last written two frames ago, so no live job references it and
// the wait returns immediately. WILLEQ_B972FT_TIMING wall-clocks it.
GLuint UnifiedParticleRenderer::uploadPackedVerts() {
    if (packedVerts_.empty()) return 0;
    eqt::memory::GemTag gemTag("vbo:particles");
    const size_t needed =
        packedVerts_.size() * static_cast<size_t>(sizeof(PointSpriteVertex)) +
        sizeof(PointSpriteVertex);
    // The row advances once per render() (frameCount_ incremented at the top),
    // and the row was last written two frames ago.
    const size_t row = (frameCount_ - 1) % kVboRingFrames;
    // One slack vertex keeps every subdata write strictly smaller than the
    // allocation (B975): a whole-resource write is what renames a fresh BO;
    // a partial write reuses the bounded storage.
    if (!vertexVbo_[row]) glGenBuffers(1, &vertexVbo_[row]);
    glBindBuffer(GL_ARRAY_BUFFER, vertexVbo_[row]);
    if (needed > vertexVboBytes_[row]) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(needed), nullptr,
                     GL_DYNAMIC_DRAW);
        if (driver_)
            gles2AdjustRawBufferBytes(driver_,
                                      static_cast<ptrdiff_t>(needed) - vertexVboBytes_[row]);
        vertexVboBytes_[row] = needed;
    }
    if (getenv("WILLEQ_B972FT_TIMING")) {
        const auto t0 = std::chrono::steady_clock::now();
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(packedVerts_.size() *
                                                static_cast<size_t>(sizeof(PointSpriteVertex))),
                        packedVerts_.data());
        const auto t1 = std::chrono::steady_clock::now();
        const int64_t subUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        fprintf(stderr, "[B972FT] frame=%u row=%zu subdata=1 verts=%zu sub=%lld us\n",
                (unsigned)(frameCount_ - 1), row, packedVerts_.size(), (long long)subUs);
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(packedVerts_.size() *
                                                static_cast<size_t>(sizeof(PointSpriteVertex))),
                        packedVerts_.data());
    }

    // NO attribute-pointer setup here: pointers capture the buffer bound AT
    // CALL TIME (no VAO), and the unbind below would make them capture
    // client null. renderBatch() re-binds the row (first batch) and THEN
    // sets them.
    //
    // Unbind before returning: the driver's state_ cache is NOT updated by
    // the raw glBindBuffer above (it stays at the driver's last tracked
    // bind, usually 0), so a later client-array draw -- which skips its own
    // unbind when the cache says 0 -- would leave this VBO bound and draw
    // through it. The batch draws re-bind the row (first batch) and
    // render() restores the saved binds at its end; this unbind only matters
    // for a client-array frame in between. gles2SyncBufferBindings re-reads
    // the real GL bindings into the cache (the B972 pattern) -- but only
    // when a driver is attached: test doubles init() without one and draw
    // in raw GL, where there is no cache to re-read.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (driver_) gles2SyncBufferBindings(driver_);
    return vertexVbo_[row];
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GLES2
