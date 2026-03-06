#ifdef EQT_HAS_GLES2

#include "client/graphics/environment/unified_particle_renderer.h"
#include "common/logging.h"
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
uniform vec4 uFogColor;
uniform vec2 uAtlasRegionSize;

varying vec4 vColor;
varying float vFogFactor;
varying vec2 vAtlasOffset;
varying float vRotation;

void main() {
    // Optionally rotate gl_PointCoord for rain streaks
    vec2 pc = gl_PointCoord;
    if (vRotation != 0.0) {
        pc -= 0.5;
        float s = sin(vRotation);
        float c = cos(vRotation);
        pc = vec2(pc.x * c - pc.y * s, pc.x * s + pc.y * c);
        pc += 0.5;
    }

    // Map rotated point coord [0,1] to atlas sub-region UV
    vec2 uv = vAtlasOffset + pc * uAtlasRegionSize;

    // Sample atlas texture
    vec4 texColor = texture2D(uTexture, uv);

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
    uAtlasRegionSize_ = glGetUniformLocation(program_, "uAtlasRegionSize");
    uScreenHeight_ = glGetUniformLocation(program_, "uScreenHeight");

    // Query max point size
    float pointSizeRange[2] = {1.0f, 64.0f};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, pointSizeRange);
    maxPointSize_ = pointSizeRange[1];

    // Reserve vertex array space
    additiveVerts_.reserve(1024);
    alphaVerts_.reserve(256);

    initialized_ = true;
    LOG_INFO(MOD_GRAPHICS, "UnifiedParticleRenderer: Initialized (max point size: {:.0f}, "
             "uProj={}, uView={}, uFogStart={}, uScreenHeight={})",
             maxPointSize_, uProj_, uView_, uFogStart_, uScreenHeight_);
    return true;
}

void UnifiedParticleRenderer::render(const UnifiedParticle* particles, int count,
                                      const float* viewMatrix, const float* projMatrix,
                                      float fogStart, float fogEnd, const float* fogColor,
                                      float screenHeight, GLuint atlasTexture) {
    if (!initialized_ || count <= 0 || !particles) return;

    auto t0 = std::chrono::steady_clock::now();

    // Separate particles by blend mode and fill vertex arrays
    additiveVerts_.clear();
    alphaVerts_.clear();

    for (int i = 0; i < count; ++i) {
        const UnifiedParticle& p = particles[i];
        if (!p.isAlive()) continue;

        // Compute atlas UV offset from texture index (4x4 grid)
        int col = p.textureIndex % 4;
        int row = p.textureIndex / 4;
        float atlasU = col * 0.25f;
        float atlasV = row * 0.25f;

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

        if (p.getBlendMode() == UnifiedBlendMode::ADDITIVE) {
            additiveVerts_.push_back(v);
        } else {
            alphaVerts_.push_back(v);
        }
    }

    if (additiveVerts_.empty() && alphaVerts_.empty()) return;

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

    if (uAtlasRegionSize_ >= 0) glUniform2f(uAtlasRegionSize_, 0.25f, 0.25f);

    glBindTexture(GL_TEXTURE_2D, atlasTexture);
    if (uTexture_ >= 0) glUniform1i(uTexture_, 0);

    auto t3 = std::chrono::steady_clock::now();

    // Render additive particles (1 draw call)
    if (!additiveVerts_.empty()) {
        renderBatch(additiveVerts_, UnifiedBlendMode::ADDITIVE);
    }

    auto t4 = std::chrono::steady_clock::now();

    // Render alpha-blended particles (1 draw call)
    if (!alphaVerts_.empty()) {
        renderBatch(alphaVerts_, UnifiedBlendMode::ALPHA);
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
                 "setup={:.1f}ms drawAdd={:.1f}ms({}) drawAlpha={:.1f}ms({}) restore={:.1f}ms",
                 totalUs / 1000.0f,
                 us(t0, t1) / 1000.0f, us(t1, t2) / 1000.0f,
                 us(t2, t3) / 1000.0f, us(t3, t4) / 1000.0f, additiveVerts_.size(),
                 us(t4, t5) / 1000.0f, alphaVerts_.size(),
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
