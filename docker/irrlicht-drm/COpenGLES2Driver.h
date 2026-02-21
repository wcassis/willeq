// COpenGLES2Driver.h — OpenGL ES 2.0 video driver for Irrlicht 1.8.5
// Extends CNullDriver, implementing only the ~25 methods WillEQ uses.
// All rendering through built-in GLSL ES 1.0 shader programs (no fixed-function).

#ifndef __C_OPEN_GLES2_DRIVER_H_INCLUDED__
#define __C_OPEN_GLES2_DRIVER_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "CNullDriver.h"
#include "IMaterialRendererServices.h"
#include "IShaderConstantSetCallBack.h"
#include "SIrrCreationParameters.h"
#include "COGLES2Shaders.h"

// Custom material types start at this offset (beyond all built-in E_MATERIAL_TYPE values)
#define EMT_CUSTOM_BASE 256

#include <GLES2/gl2.h>

namespace irr
{

class CIrrDeviceFB;

namespace video
{

class COGLES2Texture;

// State tracking to minimize redundant GL calls (critical for Mali 400 tile-based arch)
struct SOGLES2State
{
    // Blend
    bool blendEnabled;
    GLenum blendSrc;
    GLenum blendDst;

    // Depth
    bool depthTestEnabled;
    bool depthWriteEnabled;
    GLenum depthFunc;

    // Cull
    bool cullEnabled;
    GLenum cullFace;

    // Scissor
    bool scissorEnabled;

    // Bound textures
    GLuint boundTexture[2];  // Unit 0 and 1
    GLenum activeTextureUnit;

    // Current viewport
    GLint viewportX, viewportY;
    GLsizei viewportW, viewportH;

    // Current program
    GLuint currentProgram;

    void reset()
    {
        blendEnabled = false;
        blendSrc = GL_ONE;
        blendDst = GL_ZERO;
        depthTestEnabled = false;
        depthWriteEnabled = true;
        depthFunc = GL_LEQUAL;
        cullEnabled = false;
        cullFace = GL_BACK;
        scissorEnabled = false;
        boundTexture[0] = boundTexture[1] = 0;
        activeTextureUnit = GL_TEXTURE0;
        viewportX = viewportY = 0;
        viewportW = viewportH = 0;
        currentProgram = 0;
    }
};

// GLES2 extension support (Mali 400 tile-based architecture optimizations)
struct SOGLES2Extensions
{
    // GL_EXT_multisampled_render_to_texture — free MSAA at tile level
    bool hasMultisampledRenderToTexture;
    typedef void (GL_APIENTRY *PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXT)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXT glRenderbufferStorageMultisampleEXT;
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXT glFramebufferTexture2DMultisampleEXT;
    GLint maxSamples;

    // GL_EXT_discard_framebuffer — skip depth/stencil tile writeback
    bool hasDiscardFramebuffer;
    typedef void (GL_APIENTRY *PFNGLDISCARDFRAMEBUFFEREXT)(GLenum, GLsizei, const GLenum*);
    PFNGLDISCARDFRAMEBUFFEREXT glDiscardFramebufferEXT;

    // GL_OES_standard_derivatives — fwidth() in fragment shaders
    bool hasStandardDerivatives;

    // GL_OES_get_program_binary — shader binary caching
    bool hasProgramBinary;
    typedef void (GL_APIENTRY *PFNGLGETPROGRAMBINARYOES)(GLuint, GLsizei, GLsizei*, GLenum*, void*);
    typedef void (GL_APIENTRY *PFNGLPROGRAMBINARYOES)(GLuint, GLenum, const void*, GLsizei);
    PFNGLGETPROGRAMBINARYOES glGetProgramBinaryOES;
    PFNGLPROGRAMBINARYOES glProgramBinaryOES;
    GLint numBinaryFormats;

    void reset()
    {
        hasMultisampledRenderToTexture = false;
        glRenderbufferStorageMultisampleEXT = nullptr;
        glFramebufferTexture2DMultisampleEXT = nullptr;
        maxSamples = 0;

        hasDiscardFramebuffer = false;
        glDiscardFramebufferEXT = nullptr;

        hasStandardDerivatives = false;

        hasProgramBinary = false;
        glGetProgramBinaryOES = nullptr;
        glProgramBinaryOES = nullptr;
        numBinaryFormats = 0;
    }
};

// Custom shader material renderer info (for IGPUProgrammingServices)
struct SOGLES2CustomShader
{
    GLuint program;
    IShaderConstantSetCallBack* callback;
    E_MATERIAL_TYPE baseMaterial;
};

class COpenGLES2Driver : public CNullDriver, public IMaterialRendererServices
{
public:
    COpenGLES2Driver(const SIrrlichtCreationParameters& params,
                     io::IFileSystem* io, CIrrDeviceFB* device);
    virtual ~COpenGLES2Driver();

    bool initDriver();

    // --- CNullDriver overrides (IVideoDriver) ---

    virtual bool beginScene(bool backBuffer=true, bool zBuffer=true,
                            SColor color=SColor(255,0,0,0),
                            const SExposedVideoData& videoData=SExposedVideoData(),
                            core::rect<s32>* sourceRect=0);
    virtual bool endScene();

    // Transforms
    virtual void setTransform(E_TRANSFORMATION_STATE state, const core::matrix4& mat);
    virtual const core::matrix4& getTransform(E_TRANSFORMATION_STATE state) const;

    // 2D drawing
    virtual void draw2DRectangle(SColor color, const core::rect<s32>& pos,
                                 const core::rect<s32>* clip=0);
    virtual void draw2DRectangle(const core::rect<s32>& pos,
                                 SColor colorLeftUp, SColor colorRightUp,
                                 SColor colorLeftDown, SColor colorRightDown,
                                 const core::rect<s32>* clip=0);
    virtual void draw2DLine(const core::position2d<s32>& start,
                            const core::position2d<s32>& end,
                            SColor color=SColor(255,255,255,255));
    virtual void draw2DImage(const video::ITexture* texture,
                             const core::position2d<s32>& destPos);
    virtual void draw2DImage(const video::ITexture* texture,
                             const core::position2d<s32>& destPos,
                             const core::rect<s32>& sourceRect,
                             const core::rect<s32>* clipRect=0,
                             SColor color=SColor(255,255,255,255),
                             bool useAlphaChannelOfTexture=false);
    virtual void draw2DImage(const video::ITexture* texture,
                             const core::rect<s32>& destRect,
                             const core::rect<s32>& sourceRect,
                             const core::rect<s32>* clipRect=0,
                             const video::SColor* const colors=0,
                             bool useAlphaChannelOfTexture=false);
    virtual void draw2DImageBatch(const video::ITexture* texture,
                                  const core::array<core::position2d<s32>>& positions,
                                  const core::array<core::rect<s32>>& sourceRects,
                                  const core::rect<s32>* clipRect=0,
                                  SColor color=SColor(255,255,255,255),
                                  bool useAlphaChannelOfTexture=false);

    // 3D drawing
    virtual void drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
                                         const void* indexList, u32 primitiveCount,
                                         E_VERTEX_TYPE vType=EVT_STANDARD,
                                         scene::E_PRIMITIVE_TYPE pType=scene::EPT_TRIANGLES,
                                         E_INDEX_TYPE iType=EIT_16BIT);
    virtual void drawMeshBuffer(const scene::IMeshBuffer* mb);

    // Material
    virtual void setMaterial(const SMaterial& material);

    // Textures
    virtual ITexture* addTexture(const core::dimension2d<u32>& size,
                                 const io::path& name,
                                 ECOLOR_FORMAT format=ECF_A8R8G8B8);
    virtual ITexture* addTexture(const io::path& name, IImage* image, void* mipmapData=0);
    virtual void removeTexture(ITexture* texture);
    virtual ITexture* createDeviceDependentTexture(IImage* surface, const io::path& name, void* mipmapData=0);
    virtual ITexture* addRenderTargetTexture(const core::dimension2d<u32>& size,
                                              const io::path& name="rt",
                                              const ECOLOR_FORMAT format=ECF_UNKNOWN);
    virtual bool setRenderTarget(video::ITexture* texture,
                                 bool clearBackBuffer=true,
                                 bool clearZBuffer=true,
                                 SColor color=video::SColor(0,0,0,0));

    // Fog
    virtual void setFog(SColor color=SColor(0,255,255,255),
                        E_FOG_TYPE fogType=EFT_FOG_LINEAR,
                        f32 start=50.0f, f32 end=100.0f, f32 density=0.01f,
                        bool pixelFog=false, bool rangeFog=false);

    // Screenshots
    virtual IImage* createScreenShot(video::ECOLOR_FORMAT format=video::ECF_UNKNOWN,
                                     video::E_RENDER_TARGET target=video::ERT_FRAME_BUFFER);

    // Feature queries
    virtual bool queryFeature(E_VIDEO_DRIVER_FEATURE feature) const;
    virtual const core::dimension2d<u32>& getScreenSize() const { return ScreenSize; }
    virtual const core::dimension2d<u32>& getCurrentRenderTargetSize() const;
    virtual E_DRIVER_TYPE getDriverType() const { return EDT_OGLES2; }
    virtual const wchar_t* getName() const { return L"OpenGL ES 2.0"; }

    // Shader programming services
    virtual IGPUProgrammingServices* getGPUProgrammingServices();

    // IMaterialRendererServices interface
    virtual void setBasicRenderStates(const SMaterial& material,
                                       const SMaterial& lastMaterial,
                                       bool resetAllRenderstates);
    virtual bool setVertexShaderConstant(const c8* name, const f32* floats, int count);
    virtual bool setVertexShaderConstant(const c8* name, const bool* bools, int count);
    virtual bool setVertexShaderConstant(const c8* name, const s32* ints, int count);
    virtual bool setPixelShaderConstant(const c8* name, const f32* floats, int count);
    virtual bool setPixelShaderConstant(const c8* name, const bool* bools, int count);
    virtual bool setPixelShaderConstant(const c8* name, const s32* ints, int count);
    virtual IVideoDriver* getVideoDriver();

    // IMaterialRendererServices index-based (unused, required by interface)
    virtual void setVertexShaderConstant(const f32* data, s32 startRegister, s32 constantAmount=1) {}
    virtual void setPixelShaderConstant(const f32* data, s32 startRegister, s32 constantAmount=1) {}

    // IGPUProgrammingServices interface (must match full virtual signature with geometry shader params)
    virtual s32 addHighLevelShaderMaterial(
        const c8* vertexShaderProgram,
        const c8* vertexShaderEntryPointName,
        E_VERTEX_SHADER_TYPE vsCompileTarget,
        const c8* pixelShaderProgram,
        const c8* pixelShaderEntryPointName,
        E_PIXEL_SHADER_TYPE psCompileTarget,
        const c8* geometryShaderProgram,
        const c8* geometryShaderEntryPointName = "main",
        E_GEOMETRY_SHADER_TYPE gsCompileTarget = EGST_GS_4_0,
        scene::E_PRIMITIVE_TYPE inType = scene::EPT_TRIANGLES,
        scene::E_PRIMITIVE_TYPE outType = scene::EPT_TRIANGLE_STRIP,
        u32 verticesOut = 0,
        IShaderConstantSetCallBack* callback=0,
        E_MATERIAL_TYPE baseMaterialType=EMT_SOLID,
        s32 userData=0,
        E_GPU_SHADING_LANGUAGE shadingLanguage=EGSL_DEFAULT);

    virtual s32 addHighLevelShaderMaterialFromFiles(
        const io::path& vertexShaderProgramFileName,
        const c8* vertexShaderEntryPointName,
        E_VERTEX_SHADER_TYPE vsCompileTarget,
        const io::path& pixelShaderProgramFileName,
        const c8* pixelShaderEntryPointName,
        E_PIXEL_SHADER_TYPE psCompileTarget,
        const io::path& geometryShaderProgramFileName,
        const c8* geometryShaderEntryPointName = "main",
        E_GEOMETRY_SHADER_TYPE gsCompileTarget = EGST_GS_4_0,
        scene::E_PRIMITIVE_TYPE inType = scene::EPT_TRIANGLES,
        scene::E_PRIMITIVE_TYPE outType = scene::EPT_TRIANGLE_STRIP,
        u32 verticesOut = 0,
        IShaderConstantSetCallBack* callback=0,
        E_MATERIAL_TYPE baseMaterialType=EMT_SOLID,
        s32 userData=0,
        E_GPU_SHADING_LANGUAGE shadingLanguage=EGSL_DEFAULT);

    // Invalidate texture state tracking (call after raw GL texture ops)
    void invalidateTextureState();

    // Access to the shader manager (for COGLES2Texture FBO operations, etc.)
    COGLES2ShaderManager& getShaderManager() { return shaderManager_; }

    // Access to detected GLES2 extensions
    const SOGLES2Extensions& getExtensions() const { return extensions_; }

    // Viewport
    virtual void setViewPort(const core::rect<s32>& area);

    // Deletion callbacks
    virtual void removeAllTextures();

    // Enable/disable material flags
    virtual void enableMaterial2D(bool enable=true);

    // MaxTextureSize query
    virtual core::dimension2du getMaxTextureSize() const;

private:
    // Setup ortho projection for 2D drawing
    void setOrthoProjection();

    // Restore 3D projection after 2D drawing
    void restore3DProjection();

    // Apply material state to GL (blend, depth, cull, texture)
    void applyMaterialState(const SMaterial& material);

    // Apply a custom shader's state
    void applyCustomShaderState(s32 materialType, const SMaterial& material);

    // Set scissor rect from clip rect
    void setScissorFromClip(const core::rect<s32>* clip);

    // State tracking
    void setBlend(bool enable, GLenum src=GL_ONE, GLenum dst=GL_ZERO);
    void setDepthTest(bool enable);
    void setDepthWrite(bool enable);
    void setCull(bool enable, GLenum face=GL_BACK);
    void bindTexture(GLuint unit, GLuint tex);

    CIrrDeviceFB* Device;
    COGLES2ShaderManager shaderManager_;
    SOGLES2State state_;
    SOGLES2Extensions extensions_;

    // Transform matrices
    core::matrix4 Matrices[ETS_COUNT];
    bool TransformDirty;

    // Fog state
    SColor FogColor;
    f32 FogStart;
    f32 FogEnd;

    // Material state
    SMaterial Material;
    SMaterial LastMaterial;
    bool ResetRenderStates;

    // Render target state
    core::dimension2d<u32> ScreenSize;
    core::dimension2d<u32> CurrentRenderTargetSize;
    COGLES2Texture* CurrentRenderTarget;
    GLuint DefaultFBO;

    // 2D rendering state
    bool Is2DMode;

    // Custom shader materials (from addHighLevelShaderMaterial)
    core::array<SOGLES2CustomShader> CustomShaders;

    // Currently bound custom shader program (0 = built-in)
    GLuint currentCustomProgram_;
};

// Factory function (called from CIrrDeviceFB::createDriver)
IVideoDriver* createOpenGLES2Driver(const SIrrlichtCreationParameters& params,
                                     io::IFileSystem* io, CIrrDeviceFB* device);

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_OGLES2_
#endif // __C_OPEN_GLES2_DRIVER_H_INCLUDED__
