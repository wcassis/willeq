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

namespace irr {
namespace video {
class IVideoDriver;
}  // namespace video
}  // namespace irr

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
    // driver: the IVideoDriver that owns the GL context (null in the no-GLES2
    // build's test doubles); the bounded VBO's bytes enter its logical tally
    // through gles2AdjustRawBufferBytes, the same accounting the name-tag
    // batch uses.
    // Returns true on success.
    bool init(irr::video::IVideoDriver* driver = nullptr);

    // Render a batch of alive particles.
    // particles: pointer to array of alive particles
    // count: number of particles
    // viewMatrix: camera view matrix (16 floats, Irrlicht layout)
    // projMatrix: projection matrix (16 floats, Irrlicht layout)
    // fogStart/fogEnd: linear fog distances
    // fogColor: RGBA float[4] (may be nullptr if no fog)
    // screenHeight: viewport height for perspective point size scaling
    // atlasTexture: GL texture handle for the particle atlas
    // alphaCompanion: GL handle of the atlas's ETC1 `_alpha` companion, or 0
    //   when the atlas carries its alpha in band (T411, board B261). Bound on
    //   texture unit 1; 0 binds a 1x1 opaque-white texture so the shader's
    //   multiply is an exact no-op.
    // spriteTexture / spriteAlphaCompanion: T417, board B300. The SECOND
    //   particle sheet, data/textures/spell_sprite_atlas.bmp -- the official
    //   client's own spel*.tga art, 32x32 cells of 32x32 (T420, board B355:
    //   all sixteen animation frames of each of the 33 sprites). A particle
    //   whose textureIndex is >= SpellSpriteAtlas::kBase addresses this sheet
    //   instead of the 4x4 procedural atlas -- the ordinal names the SPRITE
    //   and the CELL is derived from the particle's normalized age, so the
    //   sprite animates -- and it is drawn in its own batch because the sheet
    //   and its region size are per-draw state. 0 means the sheet is
    //   unavailable, and such particles are skipped rather than drawn with
    //   the wrong sheet's UVs.
    void render(const UnifiedParticle* particles, int count,
                const float* viewMatrix, const float* projMatrix,
                float fogStart, float fogEnd, const float* fogColor,
                float screenHeight, GLuint atlasTexture,
                GLuint alphaCompanion = 0, GLuint spriteTexture = 0,
                GLuint spriteAlphaCompanion = 0);

    // Max supported point size on this GPU
    float getMaxPointSize() const { return maxPointSize_; }

    bool isInitialized() const { return initialized_; }

    // B972 (#290): the particle point draw is the one untagged raw-GL client
    // array draw in the client -- the census names it only because the draw
    // has no driver tag. This seam turns the bounded VBO that now backs the
    // point draw on or off so a unit test can pin pixel identity against the
    // old client-array path without a full scene. When on (the default) the
    // point vertices are streamed through a reused VBO (B975's pattern: one
    // whole-batch glBufferSubData that stays strictly smaller than the
    // allocation), so the 1 MiB Mesa stream-upload slot the client-array path
    // warms up is freed and the draw's bytes enter the driver's logical
    // tally via gles2AdjustRawBufferBytes.
    void setVboPathEnabled(bool enabled) { useVbo_ = enabled; }
    bool isVboPathEnabled() const { return useVbo_; }
    // B972-FT (alternative shape): slot i of the 3-slot RING (one bounded
    // VBO per FRAME: the frame's non-empty point batches packed into it).
    GLuint vertexBuffer(size_t i) const { return vertexVbo_[i]; }
    // B972-FT: the RING's total bytes (every slot is named in the driver's
    // logical tally).
    size_t vertexBufferBytes() const {
        size_t sum = 0;
        for (size_t i = 0; i < kVboRingFrames; ++i) sum += vertexVboBytes_[i];
        return sum;
    }

    // B995: the Options pages' opacity and near clip, applied per particle by
    // Environment::applyParticleKnobs() in render(). The camera is the one
    // the near clip is measured from. Identity until set (the visual gate).
    void setParticleKnobs(const ParticleKnobs& knobs, const glm::vec3& cameraPos) {
        knobs_ = knobs;
        cameraPos_ = cameraPos;
    }


private:
    ParticleKnobs knobs_ = identityParticleKnobs();  // B995
    glm::vec3 cameraPos_{0.0f};
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

    // B972-FT (alternative shape): draw ONE batch's vertex WINDOW of the
    // packed frame VBO (start vertex + count, the blend mode set per call).
    // batchIdx is the batch identity (0..3: the two procedural-atlas
    // batches, then the two spell-sprite batches) for the timing log.
    // bindAndPointers: true for the first packed batch of the frame -- the
    // upload's trailing unbind must be re-established (bind + attribute
    // pointers capture the VBO, the run-381 pattern) before any draw reads
    // it.
    void renderBatch(const PointSpriteVertex* verts, size_t start, size_t count,
                     UnifiedBlendMode blendMode, int batchIdx, bool bindAndPointers);

    // B972-FT (alternative shape): grow the ring row's VBO and write the
    // frame's PACKED vertices (packedVerts_: the non-empty batches
    // concatenated) with ONE glBufferSubData, then set the point-vertex
    // attribute pointers once (they capture this VBO). The write-wait
    // (issue #326) is measured here: the row was last written two frames
    // ago, so no in-flight job references it. Returns the row's VBO (0 when
    // the pack is empty).
    GLuint uploadPackedVerts();

    // B972-FT (alternative shape): the RING's rows (frames). render() packs
    // the frame's non-empty batches into ONE bounded VBO (the row's slot)
    // with one glBufferSubData; each batch then draws its vertex WINDOW with
    // a glDrawArrays offset. ONE VBO per frame -- the pre-ring cut's shape --
    // is what keeps Mesa/Lima's driver-retained per-BO scratch at the cut's
    // level (the per-batch 12-slot ring, the first measured-and-rejected
    // shape, gave back 4-5 MiB of gpu_unnamed: the scratch scales with the
    // number of distinct VBOs the frame's job names). 3 rows cover the two
    // in-flight frames N-1/N-2 a tile-based driver keeps queued.
    static constexpr size_t kVboRingFrames = 3;
    unsigned frameCount_ = 0;  // render() count, incremented once per frame

    GLuint program_ = 0;
    // T411: 1x1 opaque-white texture bound on unit 1 when the atlas has no
    // ETC1 companion, so `texColor.a *= texture2D(uAlphaTexture, uv).r` is an
    // exact no-op for an in-band-alpha atlas. Mirrors the driver's
    // COpenGLES2Driver::getOpaqueAlphaFallback(); this renderer bypasses the
    // driver entirely (raw GL), so it needs its own.
    GLuint opaqueAlphaFallback_ = 0;
    bool initialized_ = false;
    float maxPointSize_ = 64.0f;

    // B972 (#290) / B972-FT (#326), the ALTERNATIVE shape: the RING of
    // bounded VBOs, ONE per FRAME (kVboRingFrames rows). render() packs the
    // frame's non-empty point batches into the row's VBO with a single
    // glBufferSubData, then each batch draws its vertex WINDOW with a
    // glDrawArrays offset -- the frame's job references exactly ONE VBO,
    // the pre-ring cut's shape (the per-batch 12-slot ring, first rejected
    // by measurement, re-added 4-5 MiB of gpu_unnamed: Mesa/Lima's
    // driver-retained per-BO scratch scales with the number of distinct
    // VBOs the frame's job names). The row was last written two frames ago,
    // so no in-flight job references it and Lima's
    // lima_flush_job_accessing_bo + lima_bo_wait return immediately (issue
    // #326's write-wait stays gone). The VBO grows at high-water + one
    // slack vertex, so every glBufferSubData stays strictly smaller than
    // the allocation (B975's pattern).
    GLuint vertexVbo_[kVboRingFrames] = {0};
    size_t vertexVboBytes_[kVboRingFrames] = {0};
    // B972-FT (alternative shape): the frame's packed point vertices (the
    // non-empty batches concatenated) that uploadPackedVerts() streams into
    // the ring row. Reused each frame.
    std::vector<PointSpriteVertex> packedVerts_;
    bool useVbo_ = true;
    // The IVideoDriver that owns the GL context (init()); null in the
    // no-GLES2 build's doubles. After a raw VBO draw the driver's state_
    // cache still names the particle VBO as bound, so render() re-reads the
    // real bindings through gles2SyncBufferBindings().
    irr::video::IVideoDriver* driver_ = nullptr;

    // Uniform locations
    GLint uProj_ = -1;
    GLint uView_ = -1;
    GLint uFogStart_ = -1;
    GLint uFogEnd_ = -1;
    GLint uFogColor_ = -1;
    GLint uTexture_ = -1;
    GLint uAlphaTexture_ = -1;   // T411: ETC1 alpha companion sampler (unit 1)
    GLint uAtlasRegionSize_ = -1;
    GLint uScreenHeight_ = -1;

    // CPU-side vertex arrays (reused each frame). Four, not two: blend mode
    // splits the draw, and so does the sheet (T417).
    std::vector<PointSpriteVertex> additiveVerts_;
    std::vector<PointSpriteVertex> alphaVerts_;
    std::vector<PointSpriteVertex> spriteAdditiveVerts_;
    std::vector<PointSpriteVertex> spriteAlphaVerts_;
    // B1146: additive particles with spellsnew.edd colour words, drawn
    // GL_SRC_ALPHA, GL_ONE (UnifiedBlendMode::ADDITIVE_SRC_ALPHA).
    std::vector<PointSpriteVertex> additiveSrcAlphaVerts_;
    std::vector<PointSpriteVertex> spriteAdditiveSrcAlphaVerts_;
};

} // namespace Environment
} // namespace Graphics
} // namespace EQT

#endif // EQT_HAS_GLES2
