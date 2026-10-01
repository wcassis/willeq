#ifdef EQT_HAS_GLES2

#include "client/graphics/environment/unified_particle_renderer.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "common/logging.h"
#include "client/core/gem_owner_census.h"  // B962
#include <cstring>
#include <chrono>

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

uniform mat4 uProj;
uniform mat4 uView;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uScreenHeight;

varying vec4 vColor;
varying float vFogFactor;
varying vec2 vAtlasOffset;
varying float vRotation;

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

void main() {
    // Rotate gl_PointCoord (identity when vRotation=0: sin(0)=0, cos(0)=1)
    // Always compute — Mali 400 FS branches cost more than the sin/cos they skip
    vec2 pc = gl_PointCoord - 0.5;
    float s = sin(vRotation);
    float c = cos(vRotation);
    pc = vec2(pc.x * c - pc.y * s, pc.x * s + pc.y * c) + 0.5;

    // Map rotated point coord [0,1] to atlas sub-region UV
    vec2 uv = vAtlasOffset + pc * uAtlasRegionSize;

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

// === Implementation ===

UnifiedParticleRenderer::UnifiedParticleRenderer() = default;

UnifiedParticleRenderer::~UnifiedParticleRenderer() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (opaqueAlphaFallback_ != 0) {   // T411
        glDeleteTextures(1, &opaqueAlphaFallback_);
        opaqueAlphaFallback_ = 0;
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

bool UnifiedParticleRenderer::init() {
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
    additiveVerts_.reserve(1024);
    alphaVerts_.reserve(256);
    spriteAdditiveVerts_.reserve(1024);
    spriteAlphaVerts_.reserve(256);

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

    auto t0 = std::chrono::steady_clock::now();

    // Separate particles by SHEET and blend mode, and fill vertex arrays.
    // Both are per-draw state -- the bound texture and uAtlasRegionSize for
    // the sheet, glBlendFunc for the mode -- so each combination is a batch.
    additiveVerts_.clear();
    alphaVerts_.clear();
    spriteAdditiveVerts_.clear();
    spriteAlphaVerts_.clear();

    for (int i = 0; i < count; ++i) {
        const UnifiedParticle& p = particles[i];
        if (!p.isAlive()) continue;

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
            const int sprite = p.textureIndex - SpellSpriteAtlas::kBase;
            const int frame =
                SpellSpriteAtlas::frameForNormalizedAge(p.getNormalizedAge());
            const int cell = SpellSpriteAtlas::cellFor(sprite, frame);
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

        PointSpriteVertex v;
        v.x = p.position.x;
        v.y = p.position.y;
        v.z = p.position.z;
        v.r = p.color.r;
        v.g = p.color.g;
        v.b = p.color.b;
        v.a = p.color.a;
        v.pointSize = p.size;
        v.rotation = p.rotation;
        v.atlasU = atlasU;
        v.atlasV = atlasV;

        const bool additive = p.getBlendMode() == UnifiedBlendMode::ADDITIVE;
        if (onSpriteSheet) {
            (additive ? spriteAdditiveVerts_ : spriteAlphaVerts_).push_back(v);
        } else {
            (additive ? additiveVerts_ : alphaVerts_).push_back(v);
        }
    }

    if (additiveVerts_.empty() && alphaVerts_.empty() &&
        spriteAdditiveVerts_.empty() && spriteAlphaVerts_.empty()) {
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
                 additiveVerts_.size(), alphaVerts_.size(), atlasTexture,
                 uProj_, uView_);
        if (!additiveVerts_.empty()) {
            const auto& v = additiveVerts_[0];
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
    // Depth test ON (cull particles behind walls) but depth write OFF
    // (particles don't occlude each other or later transparent geometry)
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

    auto t3 = std::chrono::steady_clock::now();

    // ── Sheet 0: the 4x4 procedural particle atlas ──────────────────────
    if (!additiveVerts_.empty() || !alphaVerts_.empty()) {
        if (uAtlasRegionSize_ >= 0) glUniform2f(uAtlasRegionSize_, 0.25f, 0.25f);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D,
                      alphaCompanion ? alphaCompanion : opaqueAlphaFallback_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTexture);

        if (!additiveVerts_.empty()) {
            renderBatch(additiveVerts_, UnifiedBlendMode::ADDITIVE);
        }
        if (!alphaVerts_.empty()) {
            renderBatch(alphaVerts_, UnifiedBlendMode::ALPHA);
        }
    }

    auto t4 = std::chrono::steady_clock::now();

    // ── Sheet 1: the spell sprite sheet (T417, board B300) ──────────────
    // The binning loop already dropped these when spriteTexture is 0, so
    // reaching here with a non-empty batch means the sheet is loaded.
    if (!spriteAdditiveVerts_.empty() || !spriteAlphaVerts_.empty()) {
        if (uAtlasRegionSize_ >= 0) {
            glUniform2f(uAtlasRegionSize_, 1.0f / SpellSpriteAtlas::kColumns,
                        1.0f / SpellSpriteAtlas::kRows);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, spriteAlphaCompanion ? spriteAlphaCompanion
                                                          : opaqueAlphaFallback_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, spriteTexture);

        if (!spriteAdditiveVerts_.empty()) {
            renderBatch(spriteAdditiveVerts_, UnifiedBlendMode::ADDITIVE);
        }
        if (!spriteAlphaVerts_.empty()) {
            renderBatch(spriteAlphaVerts_, UnifiedBlendMode::ALPHA);
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
                 us(t2, t3) / 1000.0f, us(t3, t4) / 1000.0f, additiveVerts_.size(),
                 alphaVerts_.size(), us(t4, t5) / 1000.0f,
                 spriteAdditiveVerts_.size(), spriteAlphaVerts_.size(),
                 us(t5, t6) / 1000.0f);
    }
}

void UnifiedParticleRenderer::renderBatch(const std::vector<PointSpriteVertex>& verts,
                                           UnifiedBlendMode blendMode) {
    if (verts.empty()) return;

    if (blendMode == UnifiedBlendMode::ADDITIVE) {
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    const int stride = sizeof(PointSpriteVertex);
    const char* base = reinterpret_cast<const char*>(verts.data());

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, base + 0);

    glDisableVertexAttribArray(1);

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, base + 12);

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, base + 28);

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, base + 36);

    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(verts.size()));
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GLES2
