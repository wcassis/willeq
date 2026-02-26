// COGLES2Shaders.h — Built-in GLSL ES 1.0 shader programs for COpenGLES2Driver
// Part of WillEQ's GLES2 rendering backend for Irrlicht 1.8.5
//
// Manages 6 built-in shader programs:
//   0: Solid3D     — Zone geometry, entities, doors (per-vertex lighting + fog)
//   1: AlphaTest3D — Vegetation, transparent objects (same + discard)
//   2: AtlasSolid3D — Atlas zone geometry (precomputed UV from texcoord1)
//   3: AtlasAlpha3D — Atlas alpha (dual sample RGB + alpha pages)
//   4: UI2D        — All 2D UI (textured quads with color modulation)
//   5: Color2D     — 2D rectangles, lines, debug overlays (solid color)

#ifndef __C_OGLES2_SHADERS_H_INCLUDED__
#define __C_OGLES2_SHADERS_H_INCLUDED__

#include <GLES2/gl2.h>

namespace irr
{
namespace video
{

// Shader program IDs
enum EOGLES2ShaderProgram
{
    EOGLES2SP_SOLID3D = 0,
    EOGLES2SP_ALPHA3D,
    EOGLES2SP_ATLAS_SOLID3D,
    EOGLES2SP_ATLAS_ALPHA3D,
    EOGLES2SP_UI2D,
    EOGLES2SP_COLOR2D,
    EOGLES2SP_COUNT
};

// Fixed vertex attribute indices (bound via glBindAttribLocation before linking)
enum EOGLES2VertexAttribute
{
    EOGLES2VA_POSITION = 0,
    EOGLES2VA_NORMAL = 1,
    EOGLES2VA_COLOR = 2,
    EOGLES2VA_TEXCOORD0 = 3,
    EOGLES2VA_TEXCOORD1 = 4
};

// Uniform locations cached per program
struct SOGLES2ProgramUniforms
{
    // Vertex shader
    GLint mWorldViewProj;
    GLint mWorld;
    GLint uSunDir;
    GLint uSunColor;
    GLint uAmbientColor;
    GLint uTintColor;
    GLint uFogStart;
    GLint uFogEnd;
    GLint uLightPos;       // uLightPos[0]
    GLint uLightColor;     // uLightColor[0]
    GLint uLightAtten;     // uLightAtten[0]

    // Fragment shader
    GLint uFogColor;
    GLint uTexture;
    GLint uAlphaTexture;
    GLint uColor;          // For Color2D / UI2D tint

    void invalidate()
    {
        mWorldViewProj = mWorld = -1;
        uSunDir = uSunColor = uAmbientColor = uTintColor = -1;
        uFogStart = uFogEnd = -1;
        uLightPos = uLightColor = uLightAtten = -1;
        uFogColor = uTexture = uAlphaTexture = uColor = -1;
    }
};

// Manages compilation, linking, and caching of built-in GLSL ES shader programs
class COGLES2ShaderManager
{
public:
    COGLES2ShaderManager();
    ~COGLES2ShaderManager();

    // Compile and link all built-in programs. Returns true if at least Color2D succeeded.
    // If hasStandardDerivatives is true, alpha-test shaders use fwidth() for smoother edges.
    bool init(bool hasStandardDerivatives = false);

    // Activate a program. Returns false if program unavailable.
    bool useProgram(EOGLES2ShaderProgram prog);

    // Get the currently active program
    EOGLES2ShaderProgram getActiveProgram() const { return activeProgram_; }

    // Invalidate active program cache (call after external glUseProgram bypasses this manager)
    void invalidateActiveProgram() { activeProgram_ = EOGLES2SP_COUNT; }

    // Get uniform locations for the currently active program
    const SOGLES2ProgramUniforms& getUniforms() const { return uniforms_[activeProgram_]; }

    // Get uniform locations for a specific program
    const SOGLES2ProgramUniforms& getUniforms(EOGLES2ShaderProgram prog) const { return uniforms_[prog]; }

    // Get GL program handle (for custom uniform setting)
    GLuint getGLProgram(EOGLES2ShaderProgram prog) const { return programs_[prog]; }

    // Check if a specific program compiled successfully
    bool isProgramAvailable(EOGLES2ShaderProgram prog) const { return programs_[prog] != 0; }

    // Build one complete program from VS + FS source (public for custom shader compilation).
    // Returns 0 on failure, GL program handle on success.
    GLuint buildProgram(const char* vsSrc, const char* fsSrc);

    // Enable shader binary caching (call before init)
    typedef void (GL_APIENTRY *PFNglGetProgramBinaryOES)(GLuint, GLsizei, GLsizei*, GLenum*, void*);
    typedef void (GL_APIENTRY *PFNglProgramBinaryOES)(GLuint, GLenum, const void*, GLsizei);
    void enableBinaryCache(const char* cacheDir, const char* gpuId,
                           PFNglGetProgramBinaryOES getProgramBinary,
                           PFNglProgramBinaryOES programBinary);

private:
    // Compile a shader from source. Returns 0 on failure.
    GLuint compileShader(GLenum type, const char* source);

    // Link a program from VS and FS, binding standard vertex attributes.
    // Returns 0 on failure.
    GLuint linkProgram(GLuint vs, GLuint fs);

    // Cache uniform locations for a program
    void cacheUniforms(EOGLES2ShaderProgram prog);

    // Shader binary cache helpers
    GLuint loadCachedBinary(const char* vsSrc, const char* fsSrc);
    void saveBinaryToCache(GLuint program, const char* vsSrc, const char* fsSrc);
    uint32_t hashSources(const char* vsSrc, const char* fsSrc);

    GLuint programs_[EOGLES2SP_COUNT];
    SOGLES2ProgramUniforms uniforms_[EOGLES2SP_COUNT];
    EOGLES2ShaderProgram activeProgram_;

    // Binary cache state
    bool cacheEnabled_;
    char cacheDir_[256];
    char gpuId_[256];
    PFNglGetProgramBinaryOES glGetProgramBinaryOES_;
    PFNglProgramBinaryOES glProgramBinaryOES_;
};

} // end namespace video
} // end namespace irr

#endif // __C_OGLES2_SHADERS_H_INCLUDED__
