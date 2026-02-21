#pragma once

// Unified Particle Renderer — GLES2 point sprite backend
// Self-contained: compiles shaders via raw GL calls (no Irrlicht driver header deps).
// Renders additive/alpha particles in 1-2 draw calls using GL_POINTS.
// GLES2-only: guarded by EQT_HAS_GLES2.

#ifdef EQT_HAS_GLES2

#include "unified_particle.h"
#include <GLES2/gl2.h>
#include <vector>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Environment {

class UnifiedParticleRenderer {
public:
    UnifiedParticleRenderer();
    ~UnifiedParticleRenderer();

    // Non-copyable
    UnifiedParticleRenderer(const UnifiedParticleRenderer&) = delete;
    UnifiedParticleRenderer& operator=(const UnifiedParticleRenderer&) = delete;

    // Compile the point sprite shader program and query GL limits.
    // Returns true on success.
    bool init();

    // Render a batch of alive particles.
    // particles: pointer to array of alive particles
    // count: number of particles
    // viewMatrix: camera view matrix (16 floats, Irrlicht layout)
    // projMatrix: projection matrix (16 floats, Irrlicht layout)
    // fogStart/fogEnd: linear fog distances
    // fogColor: RGBA float[4] (may be nullptr if no fog)
    // screenHeight: viewport height for perspective point size scaling
    // atlasTexture: GL texture handle for the particle atlas
    void render(const UnifiedParticle* particles, int count,
                const float* viewMatrix, const float* projMatrix,
                float fogStart, float fogEnd, const float* fogColor,
                float screenHeight, GLuint atlasTexture);

    // Max supported point size on this GPU
    float getMaxPointSize() const { return maxPointSize_; }

    bool isInitialized() const { return initialized_; }

private:
    // Point sprite vertex layout: position(3f) + color(4f) + texcoord0(2f) + texcoord1(2f) = 11 floats = 44 bytes
    struct PointSpriteVertex {
        float x, y, z;          // World position
        float r, g, b, a;       // Color RGBA
        float pointSize, rotation;  // Point size in aTexCoord0.x, UV rotation angle in .y
        float atlasU, atlasV;   // Atlas UV offset in aTexCoord1
    };
    static_assert(sizeof(PointSpriteVertex) == 44, "Unexpected vertex size");

    // Compile a single shader (GL_VERTEX_SHADER or GL_FRAGMENT_SHADER)
    GLuint compileShader(GLenum type, const char* source);

    // Link VS+FS into a program with standard attribute bindings
    GLuint linkProgram(GLuint vs, GLuint fs);

    void renderBatch(const std::vector<PointSpriteVertex>& verts, UnifiedBlendMode blendMode);

    GLuint program_ = 0;
    bool initialized_ = false;
    float maxPointSize_ = 64.0f;

    // Uniform locations
    GLint uProj_ = -1;
    GLint uView_ = -1;
    GLint uFogStart_ = -1;
    GLint uFogEnd_ = -1;
    GLint uFogColor_ = -1;
    GLint uTexture_ = -1;
    GLint uAtlasRegionSize_ = -1;
    GLint uScreenHeight_ = -1;

    // CPU-side vertex arrays (reused each frame)
    std::vector<PointSpriteVertex> additiveVerts_;
    std::vector<PointSpriteVertex> alphaVerts_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GLES2
