// COpenGLES2Driver.cpp — OpenGL ES 2.0 video driver for Irrlicht 1.8.5
// Core implementation: state tracking, transforms, draw calls, material setup

#include "COpenGLES2Driver.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "CIrrDeviceFB.h"
#include "COGLES2Texture.h"
#include "os.h"
#include "CImage.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstring>

namespace irr
{
namespace video
{

// ============================================================================
// Constructor / Destructor
// ============================================================================

COpenGLES2Driver::COpenGLES2Driver(const SIrrlichtCreationParameters& params,
                                   io::IFileSystem* io, CIrrDeviceFB* device)
    : CNullDriver(io, params.WindowSize),
      Device(device),
      TransformDirty(true),
      FogColor(0,0,0,0), FogStart(50.0f), FogEnd(100.0f),
      ResetRenderStates(true),
      CurrentRenderTarget(nullptr), DefaultFBO(0),
      Is2DMode(false),
      currentCustomProgram_(0)
{
    ScreenSize = params.WindowSize;
    CurrentRenderTargetSize = ScreenSize;

    state_.reset();
    extensions_.reset();

    for (u32 i = 0; i < ETS_COUNT; i++)
        Matrices[i].makeIdentity();
}

COpenGLES2Driver::~COpenGLES2Driver()
{
    // Clean up hardware buffers (VBOs/EBOs)
    deleteAllHardwareBuffers();

    // Clean up custom shader callbacks
    for (u32 i = 0; i < CustomShaders.size(); i++) {
        if (CustomShaders[i].callback)
            CustomShaders[i].callback->drop();
        if (CustomShaders[i].program)
            glDeleteProgram(CustomShaders[i].program);
    }

    removeAllTextures();
}

bool COpenGLES2Driver::initDriver()
{
    // EGL context is already current (set up by CIrrDeviceFB)
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    const char* vendor = (const char*)glGetString(GL_VENDOR);

    char msg[512];
    snprintf(msg, sizeof(msg), "GLES2 Driver: %s (%s) [%s]",
             renderer ? renderer : "?", vendor ? vendor : "?", version ? version : "?");
    os::Printer::log(msg, ELL_INFORMATION);

    // ---- Extension detection ----
    const char* extStr = (const char*)glGetString(GL_EXTENSIONS);
    if (extStr) {
        // Word-boundary-safe extension check: match " EXT " or start/end of string
        auto hasExtension = [&](const char* name) -> bool {
            size_t nameLen = strlen(name);
            const char* pos = extStr;
            while ((pos = strstr(pos, name)) != nullptr) {
                // Check word boundary before
                if (pos != extStr && pos[-1] != ' ') {
                    pos += nameLen;
                    continue;
                }
                // Check word boundary after
                char after = pos[nameLen];
                if (after == '\0' || after == ' ')
                    return true;
                pos += nameLen;
            }
            return false;
        };

        // GL_EXT_multisampled_render_to_texture
        if (hasExtension("GL_EXT_multisampled_render_to_texture")) {
            extensions_.glRenderbufferStorageMultisampleEXT =
                (SOGLES2Extensions::PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXT)
                eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
            extensions_.glFramebufferTexture2DMultisampleEXT =
                (SOGLES2Extensions::PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXT)
                eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
            if (extensions_.glRenderbufferStorageMultisampleEXT &&
                extensions_.glFramebufferTexture2DMultisampleEXT) {
                glGetIntegerv(GL_MAX_SAMPLES_EXT, &extensions_.maxSamples);
                extensions_.hasMultisampledRenderToTexture = true;
                snprintf(msg, sizeof(msg), "GLES2: GL_EXT_multisampled_render_to_texture (max %d samples)",
                         extensions_.maxSamples);
                os::Printer::log(msg, ELL_INFORMATION);
            }
        }

        // GL_EXT_discard_framebuffer
        if (hasExtension("GL_EXT_discard_framebuffer")) {
            extensions_.glDiscardFramebufferEXT =
                (SOGLES2Extensions::PFNGLDISCARDFRAMEBUFFEREXT)
                eglGetProcAddress("glDiscardFramebufferEXT");
            if (extensions_.glDiscardFramebufferEXT) {
                extensions_.hasDiscardFramebuffer = true;
                os::Printer::log("GLES2: GL_EXT_discard_framebuffer", ELL_INFORMATION);
            }
        }

        // GL_OES_standard_derivatives
        if (hasExtension("GL_OES_standard_derivatives")) {
            extensions_.hasStandardDerivatives = true;
            os::Printer::log("GLES2: GL_OES_standard_derivatives", ELL_INFORMATION);
        }

        // GL_OES_get_program_binary
        if (hasExtension("GL_OES_get_program_binary")) {
            extensions_.glGetProgramBinaryOES =
                (SOGLES2Extensions::PFNGLGETPROGRAMBINARYOES)
                eglGetProcAddress("glGetProgramBinaryOES");
            extensions_.glProgramBinaryOES =
                (SOGLES2Extensions::PFNGLPROGRAMBINARYOES)
                eglGetProcAddress("glProgramBinaryOES");
            if (extensions_.glGetProgramBinaryOES && extensions_.glProgramBinaryOES) {
                glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &extensions_.numBinaryFormats);
                if (extensions_.numBinaryFormats > 0) {
                    extensions_.hasProgramBinary = true;
                    snprintf(msg, sizeof(msg), "GLES2: GL_OES_get_program_binary (%d formats)",
                             extensions_.numBinaryFormats);
                    os::Printer::log(msg, ELL_INFORMATION);
                }
            }
        }
    }

    // Get default FBO
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    DefaultFBO = (GLuint)fbo;

    // Enable shader binary cache if extension available
    if (extensions_.hasProgramBinary) {
        char gpuId[512];
        snprintf(gpuId, sizeof(gpuId), "%s|%s",
                 version ? version : "?", renderer ? renderer : "?");
        shaderManager_.enableBinaryCache("config/shader_cache", gpuId,
                                          extensions_.glGetProgramBinaryOES,
                                          extensions_.glProgramBinaryOES);
    }

    // Initialize shaders
    // Note: GL_OES_standard_derivatives detected but NOT used for alpha threshold.
    // Lima driver's fwidth() returns degenerate values (likely NaN) that propagate
    // through clamp() and break alpha-test discard entirely.
    if (!shaderManager_.init(false)) {
        os::Printer::log("GLES2: Shader initialization failed", ELL_ERROR);
        return false;
    }

    // Initial GL state
    glViewport(0, 0, ScreenSize.Width, ScreenSize.Height);
    state_.viewportX = 0;
    state_.viewportY = 0;
    state_.viewportW = ScreenSize.Width;
    state_.viewportH = ScreenSize.Height;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);

    glEnable(GL_DEPTH_TEST);
    state_.depthTestEnabled = true;
    glDepthFunc(GL_LEQUAL);
    state_.depthFunc = GL_LEQUAL;
    glDepthMask(GL_TRUE);
    state_.depthWriteEnabled = true;

    glDisable(GL_BLEND);
    state_.blendEnabled = false;

    glDisable(GL_CULL_FACE);
    state_.cullEnabled = false;

    glDisable(GL_SCISSOR_TEST);
    state_.scissorEnabled = false;

    // Use Color2D as the default program
    shaderManager_.useProgram(EOGLES2SP_COLOR2D);

    os::Printer::log("GLES2 Driver initialized", ELL_INFORMATION);

    // Call genericDriverInit from CNullDriver to set up material renderers
    // This is done by calling the virtual init from parent
    // CNullDriver::genericDriverInit is protected, but we can set up
    // basic material renderers here

    return true;
}

// ============================================================================
// beginScene / endScene
// ============================================================================

bool COpenGLES2Driver::beginScene(bool backBuffer, bool zBuffer,
                                   SColor color,
                                   const SExposedVideoData& videoData,
                                   core::rect<s32>* sourceRect)
{
    CNullDriver::beginScene(backBuffer, zBuffer, color, videoData, sourceRect);

    GLbitfield clearMask = 0;
    if (backBuffer) {
        glClearColor(color.getRed() / 255.0f,
                     color.getGreen() / 255.0f,
                     color.getBlue() / 255.0f,
                     color.getAlpha() / 255.0f);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    if (zBuffer) {
        setDepthWrite(true);
        clearMask |= GL_DEPTH_BUFFER_BIT;
        // Always clear stencil when clearing depth (D24S8 is a single buffer)
        glStencilMask(0xFF);
        clearMask |= GL_STENCIL_BUFFER_BIT;
    }

    if (clearMask)
        glClear(clearMask);

    ResetRenderStates = true;
    Is2DMode = false;

    return true;
}

bool COpenGLES2Driver::endScene()
{
    CNullDriver::endScene();

    // Discard depth/stencil tiles before present (bandwidth saving on tile-based GPUs)
    if (extensions_.hasDiscardFramebuffer) {
        const GLenum attachments[] = { GL_DEPTH_EXT, GL_STENCIL_EXT };
        extensions_.glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, attachments);
    }

    // Present via device (eglSwapBuffers + DRM page flip)
    Device->present(0, 0, 0);

    return true;
}

// ============================================================================
// Transforms
// ============================================================================

void COpenGLES2Driver::setTransform(E_TRANSFORMATION_STATE state, const core::matrix4& mat)
{
    if (state < ETS_COUNT)
        Matrices[state] = mat;
    TransformDirty = true;
}

const core::matrix4& COpenGLES2Driver::getTransform(E_TRANSFORMATION_STATE state) const
{
    if (state < ETS_COUNT)
        return Matrices[state];
    return Matrices[ETS_WORLD];
}

// ============================================================================
// State tracking helpers
// ============================================================================

void COpenGLES2Driver::setBlend(bool enable, GLenum src, GLenum dst)
{
    if (enable != state_.blendEnabled) {
        if (enable)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        state_.blendEnabled = enable;
    }
    if (enable && (src != state_.blendSrc || dst != state_.blendDst)) {
        glBlendFunc(src, dst);
        state_.blendSrc = src;
        state_.blendDst = dst;
    }
}

void COpenGLES2Driver::setDepthTest(bool enable)
{
    if (enable != state_.depthTestEnabled) {
        if (enable)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        state_.depthTestEnabled = enable;
    }
}

void COpenGLES2Driver::setDepthWrite(bool enable)
{
    if (enable != state_.depthWriteEnabled) {
        glDepthMask(enable ? GL_TRUE : GL_FALSE);
        state_.depthWriteEnabled = enable;
    }
}

void COpenGLES2Driver::setCull(bool enable, GLenum face)
{
    if (enable != state_.cullEnabled) {
        if (enable)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
        state_.cullEnabled = enable;
    }
    if (enable && face != state_.cullFace) {
        glCullFace(face);
        state_.cullFace = face;
    }
}

void COpenGLES2Driver::bindTexture(GLuint unit, GLuint tex)
{
    GLuint idx = unit - GL_TEXTURE0;
    if (idx > 1) idx = 0;

    if (state_.activeTextureUnit != unit) {
        glActiveTexture(unit);
        state_.activeTextureUnit = unit;
    }
    // Always bind — external code (TextureAtlas, ShaderCallback) may change
    // bound textures via raw GL calls, making our tracked state stale.
    glBindTexture(GL_TEXTURE_2D, tex);
    state_.boundTexture[idx] = tex;
}

void COpenGLES2Driver::setStencilTest(bool enable)
{
    if (enable != state_.stencilTestEnabled) {
        if (enable)
            glEnable(GL_STENCIL_TEST);
        else
            glDisable(GL_STENCIL_TEST);
        state_.stencilTestEnabled = enable;
    }
}

void COpenGLES2Driver::setStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    if (func != state_.stencilFunc || ref != state_.stencilRef || mask != state_.stencilMask) {
        glStencilFunc(func, ref, mask);
        state_.stencilFunc = func;
        state_.stencilRef = ref;
        state_.stencilMask = mask;
    }
}

void COpenGLES2Driver::setStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    if (sfail != state_.stencilSFail || dpfail != state_.stencilDPFail || dppass != state_.stencilDPPass) {
        glStencilOp(sfail, dpfail, dppass);
        state_.stencilSFail = sfail;
        state_.stencilDPFail = dpfail;
        state_.stencilDPPass = dppass;
    }
}

void COpenGLES2Driver::setColorMask(bool r, bool g, bool b, bool a)
{
    if (r != state_.colorMaskR || g != state_.colorMaskG ||
        b != state_.colorMaskB || a != state_.colorMaskA) {
        glColorMask(r ? GL_TRUE : GL_FALSE, g ? GL_TRUE : GL_FALSE,
                    b ? GL_TRUE : GL_FALSE, a ? GL_TRUE : GL_FALSE);
        state_.colorMaskR = r;
        state_.colorMaskG = g;
        state_.colorMaskB = b;
        state_.colorMaskA = a;
    }
}

void COpenGLES2Driver::invalidateTextureState()
{
    // Force the state tracker to re-bind textures on next use.
    // Call this after external code (e.g. TextureAtlas) does raw GL texture ops.
    state_.boundTexture[0] = 0;
    state_.boundTexture[1] = 0;
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================================
// 2D Drawing
// ============================================================================

void COpenGLES2Driver::setOrthoProjection()
{
    if (Is2DMode)
        return;

    // Unbind VBO/EBO — 2D paths use client-side CPU pointers
    if (state_.boundVBO != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        state_.boundVBO = 0;
    }
    if (state_.boundEBO != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        state_.boundEBO = 0;
    }

    // Set up ortho projection for 2D rendering
    const core::dimension2d<u32>& renderSize = getCurrentRenderTargetSize();
    core::matrix4 ortho;

    if (CurrentRenderTarget) {
        // FBO: don't flip Y — content stored right-side-up so texture UV (0,0) = visual top-left
        ortho.buildProjectionMatrixOrthoLH(
            (f32)renderSize.Width, (f32)renderSize.Height, -1.0f, 1.0f);
        ortho.setTranslation(core::vector3df(-1.0f, -1.0f, 0.0f));
    } else {
        // Screen: flip Y so pixel (0,0) = top-left (standard 2D convention)
        ortho.buildProjectionMatrixOrthoLH(
            (f32)renderSize.Width, -(f32)renderSize.Height, -1.0f, 1.0f);
        ortho.setTranslation(core::vector3df(-1.0f, 1.0f, 0.0f));
    }

    Matrices[ETS_PROJECTION] = ortho;
    Matrices[ETS_VIEW].makeIdentity();
    Matrices[ETS_WORLD].makeIdentity();
    TransformDirty = true;
    Is2DMode = true;

    // Disable depth test for 2D
    setDepthTest(false);
    setDepthWrite(false);
    setCull(false);
}

void COpenGLES2Driver::restore3DProjection()
{
    if (!Is2DMode)
        return;
    Is2DMode = false;
}

void COpenGLES2Driver::setScissorFromClip(const core::rect<s32>* clip)
{
    if (clip) {
        const core::dimension2d<u32>& renderSize = getCurrentRenderTargetSize();
        glEnable(GL_SCISSOR_TEST);
        state_.scissorEnabled = true;
        // GL scissor is bottom-left origin
        glScissor(clip->UpperLeftCorner.X,
                  renderSize.Height - clip->LowerRightCorner.Y,
                  clip->getWidth(), clip->getHeight());
    } else if (state_.scissorEnabled) {
        glDisable(GL_SCISSOR_TEST);
        state_.scissorEnabled = false;
    }
}

void COpenGLES2Driver::draw2DRectangle(SColor color, const core::rect<s32>& pos,
                                         const core::rect<s32>* clip)
{
    draw2DRectangle(pos, color, color, color, color, clip);
}

void COpenGLES2Driver::draw2DRectangle(const core::rect<s32>& pos,
                                         SColor colorLeftUp, SColor colorRightUp,
                                         SColor colorLeftDown, SColor colorRightDown,
                                         const core::rect<s32>* clip)
{
    setOrthoProjection();
    setScissorFromClip(clip);
    setBlend(colorLeftUp.getAlpha() < 255 || colorRightUp.getAlpha() < 255 ||
             colorLeftDown.getAlpha() < 255 || colorRightDown.getAlpha() < 255,
             GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shaderManager_.useProgram(EOGLES2SP_COLOR2D);

    // Set WVP matrix
    core::matrix4 wvp = Matrices[ETS_PROJECTION];
    wvp *= Matrices[ETS_VIEW];
    wvp *= Matrices[ETS_WORLD];
    const SOGLES2ProgramUniforms& u = shaderManager_.getUniforms();
    if (u.mWorldViewProj >= 0)
        glUniformMatrix4fv(u.mWorldViewProj, 1, GL_FALSE, wvp.pointer());

    // Build quad vertices: position (3) + color (4 bytes as float)
    f32 left = (f32)pos.UpperLeftCorner.X;
    f32 right = (f32)pos.LowerRightCorner.X;
    f32 top = (f32)pos.UpperLeftCorner.Y;
    f32 bottom = (f32)pos.LowerRightCorner.Y;

    // Vertex layout: x, y, z, r, g, b, a (7 floats per vertex)
    f32 vertices[4 * 7];

    auto setVertex = [&](int idx, f32 x, f32 y, SColor c) {
        vertices[idx * 7 + 0] = x;
        vertices[idx * 7 + 1] = y;
        vertices[idx * 7 + 2] = 0.0f;
        vertices[idx * 7 + 3] = c.getRed() / 255.0f;
        vertices[idx * 7 + 4] = c.getGreen() / 255.0f;
        vertices[idx * 7 + 5] = c.getBlue() / 255.0f;
        vertices[idx * 7 + 6] = c.getAlpha() / 255.0f;
    };

    setVertex(0, left,  top,    colorLeftUp);
    setVertex(1, right, top,    colorRightUp);
    setVertex(2, right, bottom, colorRightDown);
    setVertex(3, left,  bottom, colorLeftDown);

    u16 indices[] = { 0, 1, 2, 0, 2, 3 };

    // Set vertex attributes
    glVertexAttribPointer(EOGLES2VA_POSITION, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(f32), vertices);
    glEnableVertexAttribArray(EOGLES2VA_POSITION);
    glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(f32), vertices + 3);
    glEnableVertexAttribArray(EOGLES2VA_COLOR);

    glDisableVertexAttribArray(EOGLES2VA_NORMAL);
    glDisableVertexAttribArray(EOGLES2VA_TEXCOORD0);
    glDisableVertexAttribArray(EOGLES2VA_TEXCOORD1);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    setScissorFromClip(nullptr);
}

void COpenGLES2Driver::draw2DLine(const core::position2d<s32>& start,
                                   const core::position2d<s32>& end,
                                   SColor color)
{
    setOrthoProjection();
    setBlend(color.getAlpha() < 255, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shaderManager_.useProgram(EOGLES2SP_COLOR2D);

    core::matrix4 wvp = Matrices[ETS_PROJECTION];
    wvp *= Matrices[ETS_VIEW];
    wvp *= Matrices[ETS_WORLD];
    const SOGLES2ProgramUniforms& u = shaderManager_.getUniforms();
    if (u.mWorldViewProj >= 0)
        glUniformMatrix4fv(u.mWorldViewProj, 1, GL_FALSE, wvp.pointer());

    f32 r = color.getRed() / 255.0f;
    f32 g = color.getGreen() / 255.0f;
    f32 b = color.getBlue() / 255.0f;
    f32 a = color.getAlpha() / 255.0f;

    // 2 vertices: x, y, z, r, g, b, a
    f32 vertices[2 * 7] = {
        (f32)start.X, (f32)start.Y, 0.0f, r, g, b, a,
        (f32)end.X,   (f32)end.Y,   0.0f, r, g, b, a
    };

    glVertexAttribPointer(EOGLES2VA_POSITION, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(f32), vertices);
    glEnableVertexAttribArray(EOGLES2VA_POSITION);
    glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(f32), vertices + 3);
    glEnableVertexAttribArray(EOGLES2VA_COLOR);
    glDisableVertexAttribArray(EOGLES2VA_NORMAL);
    glDisableVertexAttribArray(EOGLES2VA_TEXCOORD0);
    glDisableVertexAttribArray(EOGLES2VA_TEXCOORD1);

    glDrawArrays(GL_LINES, 0, 2);
}

// ============================================================================
// 2D Textured Drawing (Phase 2)
// ============================================================================

void COpenGLES2Driver::draw2DImage(const video::ITexture* texture,
                                    const core::position2d<s32>& destPos)
{
    if (!texture)
        return;

    core::rect<s32> sourceRect(core::position2d<s32>(0,0),
                                core::dimension2d<s32>(texture->getOriginalSize()));
    draw2DImage(texture, destPos, sourceRect, 0, SColor(255,255,255,255), true);
}

void COpenGLES2Driver::draw2DImage(const video::ITexture* texture,
                                    const core::position2d<s32>& destPos,
                                    const core::rect<s32>& sourceRect,
                                    const core::rect<s32>* clipRect,
                                    SColor color,
                                    bool useAlphaChannelOfTexture)
{
    if (!texture)
        return;

    core::rect<s32> destRect(destPos,
        core::dimension2d<s32>(sourceRect.getWidth(), sourceRect.getHeight()));

    draw2DImage(texture, destRect, sourceRect, clipRect, &color, useAlphaChannelOfTexture);
}

void COpenGLES2Driver::draw2DImage(const video::ITexture* texture,
                                    const core::rect<s32>& destRect,
                                    const core::rect<s32>& sourceRect,
                                    const core::rect<s32>* clipRect,
                                    const video::SColor* const colors,
                                    bool useAlphaChannelOfTexture)
{
    if (!texture)
        return;

    setOrthoProjection();
    setScissorFromClip(clipRect);

    if (useAlphaChannelOfTexture)
        setBlend(true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    else
        setBlend(false);

    // Bind texture
    const COGLES2Texture* gles2Tex = static_cast<const COGLES2Texture*>(texture);
    bindTexture(GL_TEXTURE0, gles2Tex->getOpenGLTextureName());

    shaderManager_.useProgram(EOGLES2SP_UI2D);

    core::matrix4 wvp = Matrices[ETS_PROJECTION];
    wvp *= Matrices[ETS_VIEW];
    wvp *= Matrices[ETS_WORLD];
    const SOGLES2ProgramUniforms& u = shaderManager_.getUniforms();
    if (u.mWorldViewProj >= 0)
        glUniformMatrix4fv(u.mWorldViewProj, 1, GL_FALSE, wvp.pointer());

    // Set texture sampler
    if (u.uTexture >= 0)
        glUniform1i(u.uTexture, 0);

    SColor tint = colors ? colors[0] : SColor(255, 255, 255, 255);

    // Compute UV coordinates from source rect and texture size
    const core::dimension2d<u32>& texSize = texture->getOriginalSize();
    f32 invW = 1.0f / (f32)texSize.Width;
    f32 invH = 1.0f / (f32)texSize.Height;

    f32 srcLeft = sourceRect.UpperLeftCorner.X * invW;
    f32 srcTop = sourceRect.UpperLeftCorner.Y * invH;
    f32 srcRight = sourceRect.LowerRightCorner.X * invW;
    f32 srcBottom = sourceRect.LowerRightCorner.Y * invH;

    f32 dstLeft = (f32)destRect.UpperLeftCorner.X;
    f32 dstTop = (f32)destRect.UpperLeftCorner.Y;
    f32 dstRight = (f32)destRect.LowerRightCorner.X;
    f32 dstBottom = (f32)destRect.LowerRightCorner.Y;

    f32 r = tint.getRed() / 255.0f;
    f32 g = tint.getGreen() / 255.0f;
    f32 b = tint.getBlue() / 255.0f;
    f32 a = tint.getAlpha() / 255.0f;

    // Vertex layout: x, y, z, r, g, b, a, u, v (9 floats per vertex)
    f32 vertices[4 * 9] = {
        dstLeft,  dstTop,    0.0f, r, g, b, a, srcLeft,  srcTop,
        dstRight, dstTop,    0.0f, r, g, b, a, srcRight, srcTop,
        dstRight, dstBottom, 0.0f, r, g, b, a, srcRight, srcBottom,
        dstLeft,  dstBottom, 0.0f, r, g, b, a, srcLeft,  srcBottom
    };

    u16 indices[] = { 0, 1, 2, 0, 2, 3 };

    glVertexAttribPointer(EOGLES2VA_POSITION, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(f32), vertices);
    glEnableVertexAttribArray(EOGLES2VA_POSITION);
    glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(f32), vertices + 3);
    glEnableVertexAttribArray(EOGLES2VA_COLOR);
    glVertexAttribPointer(EOGLES2VA_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(f32), vertices + 7);
    glEnableVertexAttribArray(EOGLES2VA_TEXCOORD0);
    glDisableVertexAttribArray(EOGLES2VA_NORMAL);
    glDisableVertexAttribArray(EOGLES2VA_TEXCOORD1);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    setScissorFromClip(nullptr);
}

void COpenGLES2Driver::draw2DImageBatch(const video::ITexture* texture,
                                          const core::array<core::position2d<s32>>& positions,
                                          const core::array<core::rect<s32>>& sourceRects,
                                          const core::rect<s32>* clipRect,
                                          SColor color,
                                          bool useAlphaChannelOfTexture)
{
    if (!texture || positions.size() == 0)
        return;

    u32 count = positions.size();
    if (count > sourceRects.size())
        count = sourceRects.size();

    // Batched rendering: build all quads into a single vertex array, one draw call
    setOrthoProjection();
    setScissorFromClip(clipRect);

    if (useAlphaChannelOfTexture)
        setBlend(true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    else
        setBlend(false);

    const COGLES2Texture* gles2Tex = static_cast<const COGLES2Texture*>(texture);
    bindTexture(GL_TEXTURE0, gles2Tex->getOpenGLTextureName());

    shaderManager_.useProgram(EOGLES2SP_UI2D);

    core::matrix4 wvp = Matrices[ETS_PROJECTION];
    wvp *= Matrices[ETS_VIEW];
    wvp *= Matrices[ETS_WORLD];
    const SOGLES2ProgramUniforms& u = shaderManager_.getUniforms();
    if (u.mWorldViewProj >= 0)
        glUniformMatrix4fv(u.mWorldViewProj, 1, GL_FALSE, wvp.pointer());
    if (u.uTexture >= 0)
        glUniform1i(u.uTexture, 0);

    const core::dimension2d<u32>& texSize = texture->getOriginalSize();
    f32 invW = 1.0f / (f32)texSize.Width;
    f32 invH = 1.0f / (f32)texSize.Height;

    f32 r = color.getRed() / 255.0f;
    f32 g = color.getGreen() / 255.0f;
    f32 b = color.getBlue() / 255.0f;
    f32 a = color.getAlpha() / 255.0f;

    // 4 vertices * 9 floats per quad, 6 indices per quad
    core::array<f32> vertices(count * 4 * 9);
    core::array<u16> indices(count * 6);

    for (u32 i = 0; i < count; i++) {
        const core::rect<s32>& srcRect = sourceRects[i];
        const core::position2d<s32>& pos = positions[i];

        f32 srcLeft = srcRect.UpperLeftCorner.X * invW;
        f32 srcTop = srcRect.UpperLeftCorner.Y * invH;
        f32 srcRight = srcRect.LowerRightCorner.X * invW;
        f32 srcBottom = srcRect.LowerRightCorner.Y * invH;

        f32 dstLeft = (f32)pos.X;
        f32 dstTop = (f32)pos.Y;
        f32 dstRight = dstLeft + (f32)srcRect.getWidth();
        f32 dstBottom = dstTop + (f32)srcRect.getHeight();

        u16 base = (u16)(i * 4);
        // Vertex layout: x, y, z, r, g, b, a, u, v
        f32 quad[4 * 9] = {
            dstLeft,  dstTop,    0.0f, r, g, b, a, srcLeft,  srcTop,
            dstRight, dstTop,    0.0f, r, g, b, a, srcRight, srcTop,
            dstRight, dstBottom, 0.0f, r, g, b, a, srcRight, srcBottom,
            dstLeft,  dstBottom, 0.0f, r, g, b, a, srcLeft,  srcBottom
        };
        for (int j = 0; j < 4 * 9; j++)
            vertices.push_back(quad[j]);

        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    glVertexAttribPointer(EOGLES2VA_POSITION, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(f32), vertices.pointer());
    glEnableVertexAttribArray(EOGLES2VA_POSITION);
    glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(f32), vertices.pointer() + 3);
    glEnableVertexAttribArray(EOGLES2VA_COLOR);
    glVertexAttribPointer(EOGLES2VA_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(f32), vertices.pointer() + 7);
    glEnableVertexAttribArray(EOGLES2VA_TEXCOORD0);
    glDisableVertexAttribArray(EOGLES2VA_NORMAL);
    glDisableVertexAttribArray(EOGLES2VA_TEXCOORD1);

    glDrawElements(GL_TRIANGLES, count * 6, GL_UNSIGNED_SHORT, indices.pointer());

    setScissorFromClip(nullptr);
}

// ============================================================================
// 3D Drawing (Phase 3)
// ============================================================================

void COpenGLES2Driver::drawVertexPrimitiveList(const void* vertices, u32 vertexCount,
                                                const void* indexList, u32 primitiveCount,
                                                E_VERTEX_TYPE vType,
                                                scene::E_PRIMITIVE_TYPE pType,
                                                E_INDEX_TYPE iType)
{
    if (!vertices || !indexList || vertexCount == 0 || primitiveCount == 0)
        return;

    restore3DProjection();

    // Unbind VBO/EBO — this path uses client-side CPU pointers
    if (state_.boundVBO != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        state_.boundVBO = 0;
    }
    if (state_.boundEBO != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        state_.boundEBO = 0;
    }

    // Determine stride and attribute offsets based on vertex type
    GLsizei stride;
    bool hasTCoords2 = false;

    switch (vType) {
        case EVT_STANDARD:
            stride = sizeof(S3DVertex);  // 36 bytes
            break;
        case EVT_2TCOORDS:
            stride = sizeof(S3DVertex2TCoords);  // 44 bytes
            hasTCoords2 = true;
            break;
        case EVT_TANGENTS:
            stride = sizeof(S3DVertexTangents);  // 60 bytes
            break;
        default:
            return;
    }

    const u8* base = (const u8*)vertices;

    // S3DVertex layout: Pos(12) + Normal(12) + Color(4) + TCoords(8) = 36
    // Position: offset 0 (3 floats)
    glVertexAttribPointer(EOGLES2VA_POSITION, 3, GL_FLOAT, GL_FALSE, stride, base + 0);
    glEnableVertexAttribArray(EOGLES2VA_POSITION);

    // Normal: offset 12 (3 floats)
    glVertexAttribPointer(EOGLES2VA_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, base + 12);
    glEnableVertexAttribArray(EOGLES2VA_NORMAL);

    // Color: offset 24 (4 unsigned bytes, normalized)
    glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, base + 24);
    glEnableVertexAttribArray(EOGLES2VA_COLOR);

    // TexCoord0: offset 28 (2 floats)
    glVertexAttribPointer(EOGLES2VA_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, stride, base + 28);
    glEnableVertexAttribArray(EOGLES2VA_TEXCOORD0);

    // TexCoord1: offset 36 (2 floats) — only for S3DVertex2TCoords
    if (hasTCoords2) {
        glVertexAttribPointer(EOGLES2VA_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, stride, base + 36);
        glEnableVertexAttribArray(EOGLES2VA_TEXCOORD1);
    } else {
        glDisableVertexAttribArray(EOGLES2VA_TEXCOORD1);
    }

    // Determine GL primitive type and index count
    GLenum glPrimitiveType;
    u32 indexCount;
    switch (pType) {
        case scene::EPT_TRIANGLES:
            glPrimitiveType = GL_TRIANGLES;
            indexCount = primitiveCount * 3;
            break;
        case scene::EPT_TRIANGLE_STRIP:
            glPrimitiveType = GL_TRIANGLE_STRIP;
            indexCount = primitiveCount + 2;
            break;
        case scene::EPT_TRIANGLE_FAN:
            glPrimitiveType = GL_TRIANGLE_FAN;
            indexCount = primitiveCount + 2;
            break;
        case scene::EPT_LINES:
            glPrimitiveType = GL_LINES;
            indexCount = primitiveCount * 2;
            break;
        case scene::EPT_LINE_STRIP:
            glPrimitiveType = GL_LINE_STRIP;
            indexCount = primitiveCount + 1;
            break;
        case scene::EPT_POINTS:
            glPrimitiveType = GL_POINTS;
            indexCount = primitiveCount;
            break;
        default:
            return;
    }

    // Draw
    GLenum glIndexType = (iType == EIT_16BIT) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

    // Note: GLES2 doesn't support 32-bit indices on all hardware.
    // Mali 400 supports OES_element_index_uint, but fallback to 16-bit if needed.
    if (glIndexType == GL_UNSIGNED_INT) {
        // Check if extension available, otherwise this may fail
        glIndexType = GL_UNSIGNED_SHORT;
    }

    glDrawElements(glPrimitiveType, indexCount, glIndexType, indexList);
    PrimitivesDrawn += primitiveCount;
}

void COpenGLES2Driver::drawMeshBuffer(const scene::IMeshBuffer* mb)
{
    if (!mb)
        return;

    // Check for hardware buffer (VBO/EBO) — fast path for static geometry
    auto it = HWBufferMap.find(mb);
    if (it != HWBufferMap.end()) {
        const SHWBuffer& hwb = it->second;

        restore3DProjection();

        // Determine stride and whether we have 2 texture coords
        GLsizei stride;
        bool hasTCoords2 = false;
        switch (hwb.vType) {
            case EVT_STANDARD:  stride = sizeof(S3DVertex); break;
            case EVT_2TCOORDS:  stride = sizeof(S3DVertex2TCoords); hasTCoords2 = true; break;
            case EVT_TANGENTS:  stride = sizeof(S3DVertexTangents); break;
            default: return;
        }

        // Bind VBO
        if (state_.boundVBO != hwb.vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, hwb.vbo);
            state_.boundVBO = hwb.vbo;
        }

        // Set vertex attribute pointers with byte offsets into VBO (not CPU pointers)
        // S3DVertex layout: Pos(12) + Normal(12) + Color(4) + TCoords(8) = 36
        glVertexAttribPointer(EOGLES2VA_POSITION, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
        glEnableVertexAttribArray(EOGLES2VA_POSITION);

        glVertexAttribPointer(EOGLES2VA_NORMAL, 3, GL_FLOAT, GL_FALSE, stride, (const void*)12);
        glEnableVertexAttribArray(EOGLES2VA_NORMAL);

        glVertexAttribPointer(EOGLES2VA_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)24);
        glEnableVertexAttribArray(EOGLES2VA_COLOR);

        glVertexAttribPointer(EOGLES2VA_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)28);
        glEnableVertexAttribArray(EOGLES2VA_TEXCOORD0);

        if (hasTCoords2) {
            glVertexAttribPointer(EOGLES2VA_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)36);
            glEnableVertexAttribArray(EOGLES2VA_TEXCOORD1);
        } else {
            glDisableVertexAttribArray(EOGLES2VA_TEXCOORD1);
        }

        // Bind EBO and draw
        if (state_.boundEBO != hwb.ebo) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hwb.ebo);
            state_.boundEBO = hwb.ebo;
        }

        glDrawElements(GL_TRIANGLES, hwb.indexCount, GL_UNSIGNED_SHORT, (const void*)0);
        PrimitivesDrawn += hwb.indexCount / 3;
        return;
    }

    // Fallback: client-side vertex arrays (for entities, doors, UI, etc.)
    // Unbind VBO/EBO so glVertexAttribPointer uses CPU pointers
    if (state_.boundVBO != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        state_.boundVBO = 0;
    }
    if (state_.boundEBO != 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        state_.boundEBO = 0;
    }

    drawVertexPrimitiveList(mb->getVertices(), mb->getVertexCount(),
                            mb->getIndices(), mb->getIndexCount() / 3,
                            mb->getVertexType(), scene::EPT_TRIANGLES,
                            mb->getIndexType());
}

// ============================================================================
// Material
// ============================================================================

void COpenGLES2Driver::setMaterial(const SMaterial& material)
{
    LastMaterial = Material;
    Material = material;
    applyMaterialState(material);
}

void COpenGLES2Driver::applyMaterialState(const SMaterial& material)
{
    // Check if this is a custom shader material
    s32 matType = material.MaterialType;

    if (matType >= (s32)EMT_CUSTOM_BASE && (matType - EMT_CUSTOM_BASE) < (s32)CustomShaders.size()) {
        applyCustomShaderState(matType, material);
        return;
    }

    // Built-in material types
    switch (material.MaterialType) {
        case EMT_TRANSPARENT_ALPHA_CHANNEL:
            setBlend(true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            setDepthWrite(false);
            break;
        case EMT_TRANSPARENT_ALPHA_CHANNEL_REF:
            setBlend(false);
            setDepthWrite(true);
            // Alpha test handled in shader via discard
            break;
        case EMT_TRANSPARENT_ADD_COLOR:
            setBlend(true, GL_ONE, GL_ONE);
            setDepthWrite(false);
            break;
        case EMT_TRANSPARENT_VERTEX_ALPHA:
            setBlend(true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            setDepthWrite(false);
            break;
        default:
            // EMT_SOLID and others
            setBlend(false);
            setDepthWrite(true);
            break;
    }

    // Depth test
    setDepthTest(material.ZBuffer != ECFN_NEVER);

    // Backface culling
    setCull(material.BackfaceCulling, GL_BACK);

    // Texture binding
    if (material.getTexture(0)) {
        const COGLES2Texture* tex = static_cast<const COGLES2Texture*>(material.getTexture(0));
        bindTexture(GL_TEXTURE0, tex->getOpenGLTextureName());

        // Set texture filtering
        GLint magFilter = material.TextureLayer[0].BilinearFilter ? GL_LINEAR : GL_NEAREST;
        GLint minFilter = material.TextureLayer[0].BilinearFilter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

        // Clamp
        bool clamp = material.TextureLayer[0].TextureWrapU == ETC_CLAMP ||
                     material.TextureLayer[0].TextureWrapU == ETC_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    }

    // Second texture unit (for atlas alpha)
    if (material.getTexture(1)) {
        const COGLES2Texture* tex = static_cast<const COGLES2Texture*>(material.getTexture(1));
        bindTexture(GL_TEXTURE1, tex->getOpenGLTextureName());
    }

    // Select appropriate built-in shader
    // (Custom shaders set their own program in applyCustomShaderState)
    bool hasTexture = material.getTexture(0) != nullptr;
    if (hasTexture) {
        // Use AlphaTest3D for alpha-ref materials (discard in fragment shader)
        if (material.MaterialType == EMT_TRANSPARENT_ALPHA_CHANNEL_REF)
            shaderManager_.useProgram(EOGLES2SP_ALPHA3D);
        else
            shaderManager_.useProgram(EOGLES2SP_SOLID3D);
    } else {
        shaderManager_.useProgram(EOGLES2SP_COLOR2D);
    }

    // Upload matrices and fog uniforms for 3D shaders
    const SOGLES2ProgramUniforms& u = shaderManager_.getUniforms();
    core::matrix4 wvp = Matrices[ETS_PROJECTION];
    wvp *= Matrices[ETS_VIEW];
    wvp *= Matrices[ETS_WORLD];
    if (u.mWorldViewProj >= 0)
        glUniformMatrix4fv(u.mWorldViewProj, 1, GL_FALSE, wvp.pointer());
    if (u.mWorld >= 0)
        glUniformMatrix4fv(u.mWorld, 1, GL_FALSE, Matrices[ETS_WORLD].pointer());

    // Default lighting for built-in materials: pass through vertex color.
    // Entity/door meshes have lighting pre-baked into vertex colors, so
    // ambient=1 + tint=1 makes the shader compute: vColor = (1*1) * aColor = aColor.
    if (u.uAmbientColor >= 0) {
        f32 white[3] = {1.0f, 1.0f, 1.0f};
        glUniform3fv(u.uAmbientColor, 1, white);
    }
    if (u.uTintColor >= 0) {
        f32 white[3] = {1.0f, 1.0f, 1.0f};
        glUniform3fv(u.uTintColor, 1, white);
    }
    // Sun and point lights at zero — only ambient matters for built-in path
    if (u.uSunColor >= 0) {
        f32 zero[3] = {0.0f, 0.0f, 0.0f};
        glUniform3fv(u.uSunColor, 1, zero);
    }

    // Fog — respect material's FogEnable flag.
    // When disabled (e.g. sky dome), set huge fog range so vFogFactor = 1.0 (no fog).
    if (material.FogEnable) {
        if (u.uFogColor >= 0) {
            f32 fogColor[4] = {
                FogColor.getRed() / 255.0f,
                FogColor.getGreen() / 255.0f,
                FogColor.getBlue() / 255.0f,
                FogColor.getAlpha() / 255.0f
            };
            glUniform4fv(u.uFogColor, 1, fogColor);
        }
        if (u.uFogStart >= 0)
            glUniform1f(u.uFogStart, FogStart);
        if (u.uFogEnd >= 0)
            glUniform1f(u.uFogEnd, FogEnd);
    } else {
        // Disable fog by pushing fog range to infinity
        if (u.uFogStart >= 0)
            glUniform1f(u.uFogStart, 999999.0f);
        if (u.uFogEnd >= 0)
            glUniform1f(u.uFogEnd, 999999.0f);
    }

    // Texture sampler
    if (u.uTexture >= 0)
        glUniform1i(u.uTexture, 0);
    if (u.uAlphaTexture >= 0)
        glUniform1i(u.uAlphaTexture, 1);
}

void COpenGLES2Driver::applyCustomShaderState(s32 materialType, const SMaterial& material)
{
    u32 idx = materialType - EMT_CUSTOM_BASE;
    if (idx >= CustomShaders.size())
        return;

    const SOGLES2CustomShader& cs = CustomShaders[idx];

    // Apply base material state (blend, depth, cull)
    SMaterial baseMat = material;
    baseMat.MaterialType = cs.baseMaterial;
    applyMaterialState(baseMat);

    // Use custom shader program
    glUseProgram(cs.program);
    currentCustomProgram_ = cs.program;

    // Call the callback to set uniforms
    if (cs.callback) {
        cs.callback->OnSetMaterial(material);
        cs.callback->OnSetConstants(this, 0);
    }
}

// ============================================================================
// Fog
// ============================================================================

void COpenGLES2Driver::setFog(SColor color, E_FOG_TYPE fogType,
                               f32 start, f32 end, f32 density,
                               bool pixelFog, bool rangeFog)
{
    FogColor = color;
    FogStart = start;
    FogEnd = end;
    // Keep CNullDriver's members in sync so getFog() returns correct values.
    // The renderer reads fog back via getFog() and passes it to shader callbacks.
    CNullDriver::setFog(color, fogType, start, end, density, pixelFog, rangeFog);
}

// ============================================================================
// Textures (Phase 2)
// ============================================================================

ITexture* COpenGLES2Driver::addTexture(const core::dimension2d<u32>& size,
                                        const io::path& name,
                                        ECOLOR_FORMAT format)
{
    // Create a blank texture
    IImage* image = createImage(format, size);
    if (!image)
        return nullptr;

    ITexture* tex = addTexture(name, image);
    image->drop();
    return tex;
}

ITexture* COpenGLES2Driver::addTexture(const io::path& name, IImage* image, void* mipmapData)
{
    if (!image)
        return nullptr;

    // Check if texture already exists
    ITexture* existing = findTexture(name);
    if (existing)
        return existing;

    COGLES2Texture* tex = new COGLES2Texture(image, name, this);
    CNullDriver::addTexture(tex);
    tex->drop();
    return tex;
}

ITexture* COpenGLES2Driver::createDeviceDependentTexture(IImage* surface, const io::path& name, void* mipmapData)
{
    // This is called by CNullDriver::loadTextureFromFile() and CNullDriver::addTexture(name, image)
    // to create a device-dependent texture. Without this override, CNullDriver returns SDummyTexture
    // which has zero size and no GL texture — breaking getTexture(filename), getTexture(IReadFile*),
    // and any other path that goes through the CNullDriver texture loading chain.
    return new COGLES2Texture(surface, name, this);
}

void COpenGLES2Driver::removeTexture(ITexture* texture)
{
    // Unbind if currently bound
    COGLES2Texture* gles2Tex = static_cast<COGLES2Texture*>(texture);
    if (gles2Tex) {
        GLuint name = gles2Tex->getOpenGLTextureName();
        for (int i = 0; i < 2; i++) {
            if (state_.boundTexture[i] == name)
                state_.boundTexture[i] = 0;
        }
    }
    CNullDriver::removeTexture(texture);
}

void COpenGLES2Driver::removeAllTextures()
{
    state_.boundTexture[0] = state_.boundTexture[1] = 0;
    CNullDriver::removeAllTextures();
}

ITexture* COpenGLES2Driver::addRenderTargetTexture(const core::dimension2d<u32>& size,
                                                    const io::path& name,
                                                    const ECOLOR_FORMAT format)
{
    COGLES2Texture* tex = new COGLES2Texture(size, name, this);
    CNullDriver::addTexture(tex);
    tex->drop();
    return tex;
}

bool COpenGLES2Driver::setRenderTarget(video::ITexture* texture,
                                        bool clearBackBuffer, bool clearZBuffer,
                                        SColor color)
{
    if (texture) {
        COGLES2Texture* gles2Tex = static_cast<COGLES2Texture*>(texture);
        GLuint fbo = gles2Tex->getFBO();
        if (fbo == 0)
            return false;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        CurrentRenderTarget = gles2Tex;
        CurrentRenderTargetSize = texture->getOriginalSize();
        setViewPort(core::rect<s32>(0, 0, CurrentRenderTargetSize.Width, CurrentRenderTargetSize.Height));
    } else {
        // Discard FBO depth before unbinding (skip tile writeback for depth we no longer need)
        if (CurrentRenderTarget && extensions_.hasDiscardFramebuffer) {
            const GLenum attachments[] = { GL_DEPTH_ATTACHMENT };
            extensions_.glDiscardFramebufferEXT(GL_FRAMEBUFFER, 1, attachments);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, DefaultFBO);
        CurrentRenderTarget = nullptr;
        CurrentRenderTargetSize = ScreenSize;
        setViewPort(core::rect<s32>(0, 0, ScreenSize.Width, ScreenSize.Height));
    }

    // Invalidate 2D mode so ortho projection recalculates for new render target size
    Is2DMode = false;

    GLbitfield clearMask = 0;
    if (clearBackBuffer) {
        glClearColor(color.getRed() / 255.0f,
                     color.getGreen() / 255.0f,
                     color.getBlue() / 255.0f,
                     color.getAlpha() / 255.0f);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    if (clearZBuffer) {
        setDepthWrite(true);
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }
    if (clearMask)
        glClear(clearMask);

    return true;
}

// ============================================================================
// Screenshots
// ============================================================================

IImage* COpenGLES2Driver::createScreenShot(video::ECOLOR_FORMAT format,
                                            video::E_RENDER_TARGET target)
{
    const core::dimension2d<u32>& size = getCurrentRenderTargetSize();
    u32 w = size.Width;
    u32 h = size.Height;

    // Read pixels (GLES2 only guarantees GL_RGBA + GL_UNSIGNED_BYTE)
    u8* pixels = new u8[w * h * 4];
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // OpenGL reads bottom-to-top, flip vertically
    u32 rowSize = w * 4;
    u8* temp = new u8[rowSize];
    for (u32 y = 0; y < h / 2; y++) {
        u8* row1 = pixels + y * rowSize;
        u8* row2 = pixels + (h - 1 - y) * rowSize;
        memcpy(temp, row1, rowSize);
        memcpy(row1, row2, rowSize);
        memcpy(row2, temp, rowSize);
    }
    delete[] temp;

    // Convert RGBA to A8R8G8B8 (BGRA)
    for (u32 i = 0; i < w * h; i++) {
        u8 r = pixels[i * 4 + 0];
        u8 g = pixels[i * 4 + 1];
        u8 b = pixels[i * 4 + 2];
        u8 a = pixels[i * 4 + 3];
        pixels[i * 4 + 0] = b;  // B
        pixels[i * 4 + 1] = g;  // G
        pixels[i * 4 + 2] = r;  // R
        pixels[i * 4 + 3] = a;  // A
    }

    IImage* image = createImageFromData(ECF_A8R8G8B8, core::dimension2d<u32>(w, h),
                                         pixels, true, false);
    // pixels ownership transferred to image (or deleted if image creation failed)
    return image;
}

// ============================================================================
// Feature queries
// ============================================================================

bool COpenGLES2Driver::queryFeature(E_VIDEO_DRIVER_FEATURE feature) const
{
    switch (feature) {
        case EVDF_RENDER_TO_TARGET:
        case EVDF_FRAMEBUFFER_OBJECT:
            return true;  // GLES2 has FBOs as core
        case EVDF_HARDWARE_TL:
            return true;  // GLES2 is fully GPU-accelerated
        case EVDF_MULTITEXTURE:
            return true;
        case EVDF_BILINEAR_FILTER:
            return true;
        case EVDF_MIP_MAP:
            return true;
        case EVDF_STENCIL_BUFFER:
            return true;  // Available if EGL config has stencil bits
        case EVDF_TEXTURE_NPOT:
            return true;  // GLES2 supports NPOT (with restrictions)
        case EVDF_VERTEX_SHADER_1_1:
        case EVDF_VERTEX_SHADER_2_0:
            return true;
        case EVDF_PIXEL_SHADER_1_1:
        case EVDF_PIXEL_SHADER_2_0:
            return true;
        case EVDF_ARB_VERTEX_PROGRAM_1:
        case EVDF_ARB_FRAGMENT_PROGRAM_1:
            return true;
        case EVDF_ALPHA_TO_COVERAGE:
            return false;
        default:
            return false;
    }
}

const core::dimension2d<u32>& COpenGLES2Driver::getCurrentRenderTargetSize() const
{
    if (CurrentRenderTarget)
        return CurrentRenderTargetSize;
    return ScreenSize;
}

core::dimension2du COpenGLES2Driver::getMaxTextureSize() const
{
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    return core::dimension2du(maxSize, maxSize);
}

// ============================================================================
// Viewport
// ============================================================================

void COpenGLES2Driver::setViewPort(const core::rect<s32>& area)
{
    core::rect<s32> vp = area;
    core::rect<s32> rendertarget(0, 0,
        getCurrentRenderTargetSize().Width, getCurrentRenderTargetSize().Height);
    vp.clipAgainst(rendertarget);

    if (vp.getHeight() > 0 && vp.getWidth() > 0) {
        glViewport(vp.UpperLeftCorner.X,
                   getCurrentRenderTargetSize().Height - vp.LowerRightCorner.Y,
                   vp.getWidth(), vp.getHeight());
        state_.viewportX = vp.UpperLeftCorner.X;
        state_.viewportY = getCurrentRenderTargetSize().Height - vp.LowerRightCorner.Y;
        state_.viewportW = vp.getWidth();
        state_.viewportH = vp.getHeight();
    }

    ViewPort = vp;
}

// ============================================================================
// Shader Programming Services (IGPUProgrammingServices)
// ============================================================================

IGPUProgrammingServices* COpenGLES2Driver::getGPUProgrammingServices()
{
    return this;
}

s32 COpenGLES2Driver::addHighLevelShaderMaterial(
    const c8* vertexShaderProgram,
    const c8* vertexShaderEntryPointName,
    E_VERTEX_SHADER_TYPE vsCompileTarget,
    const c8* pixelShaderProgram,
    const c8* pixelShaderEntryPointName,
    E_PIXEL_SHADER_TYPE psCompileTarget,
    const c8* geometryShaderProgram,
    const c8* geometryShaderEntryPointName,
    E_GEOMETRY_SHADER_TYPE gsCompileTarget,
    scene::E_PRIMITIVE_TYPE inType,
    scene::E_PRIMITIVE_TYPE outType,
    u32 verticesOut,
    IShaderConstantSetCallBack* callback,
    E_MATERIAL_TYPE baseMaterialType,
    s32 userData,
    E_GPU_SHADING_LANGUAGE shadingLanguage)
{
    if (!vertexShaderProgram || !pixelShaderProgram)
        return -1;

    // Compile and link the shader program
    GLuint prog = shaderManager_.buildProgram(vertexShaderProgram, pixelShaderProgram);
    if (!prog) {
        os::Printer::log("GLES2: Custom shader compilation failed", ELL_ERROR);
        return -1;
    }

    SOGLES2CustomShader cs;
    cs.program = prog;
    cs.callback = callback;
    cs.baseMaterial = baseMaterialType;

    if (callback)
        callback->grab();

    CustomShaders.push_back(cs);

    s32 materialType = (s32)EMT_CUSTOM_BASE + (s32)CustomShaders.size() - 1;
    return materialType;
}

s32 COpenGLES2Driver::addHighLevelShaderMaterialFromFiles(
    const io::path& vertexShaderProgramFileName,
    const c8* vertexShaderEntryPointName,
    E_VERTEX_SHADER_TYPE vsCompileTarget,
    const io::path& pixelShaderProgramFileName,
    const c8* pixelShaderEntryPointName,
    E_PIXEL_SHADER_TYPE psCompileTarget,
    const io::path& geometryShaderProgramFileName,
    const c8* geometryShaderEntryPointName,
    E_GEOMETRY_SHADER_TYPE gsCompileTarget,
    scene::E_PRIMITIVE_TYPE inType,
    scene::E_PRIMITIVE_TYPE outType,
    u32 verticesOut,
    IShaderConstantSetCallBack* callback,
    E_MATERIAL_TYPE baseMaterialType,
    s32 userData,
    E_GPU_SHADING_LANGUAGE shadingLanguage)
{
    // Not implemented — WillEQ uses inline shader strings
    return -1;
}

// ============================================================================
// IMaterialRendererServices implementation
// ============================================================================

void COpenGLES2Driver::setBasicRenderStates(const SMaterial& material,
                                              const SMaterial& lastMaterial,
                                              bool resetAllRenderstates)
{
    // This is called by material renderers, but our applyMaterialState handles it
}

bool COpenGLES2Driver::setVertexShaderConstant(const c8* name, const f32* floats, int count)
{
    GLuint prog = currentCustomProgram_ ? currentCustomProgram_ :
                  shaderManager_.getGLProgram(shaderManager_.getActiveProgram());
    GLint loc = glGetUniformLocation(prog, name);
    if (loc < 0)
        return false;

    switch (count) {
        case 1: glUniform1fv(loc, 1, floats); break;
        case 2: glUniform2fv(loc, 1, floats); break;
        case 3: glUniform3fv(loc, 1, floats); break;
        case 4: glUniform4fv(loc, 1, floats); break;
        case 16: glUniformMatrix4fv(loc, 1, GL_FALSE, floats); break;
        default:
            // Array of vec3 (e.g., 24 = 8 * 3 for point lights)
            if (count % 3 == 0)
                glUniform3fv(loc, count / 3, floats);
            else if (count % 4 == 0)
                glUniform4fv(loc, count / 4, floats);
            else
                glUniform1fv(loc, count, floats);
            break;
    }
    return true;
}

bool COpenGLES2Driver::setVertexShaderConstant(const c8* name, const bool* bools, int count)
{
    // Convert bools to ints
    s32 ints[16];
    int n = count < 16 ? count : 16;
    for (int i = 0; i < n; i++)
        ints[i] = bools[i] ? 1 : 0;
    return setVertexShaderConstant(name, (const s32*)ints, n);
}

bool COpenGLES2Driver::setVertexShaderConstant(const c8* name, const s32* ints, int count)
{
    GLuint prog = currentCustomProgram_ ? currentCustomProgram_ :
                  shaderManager_.getGLProgram(shaderManager_.getActiveProgram());
    GLint loc = glGetUniformLocation(prog, name);
    if (loc < 0)
        return false;

    switch (count) {
        case 1: glUniform1iv(loc, 1, ints); break;
        case 2: glUniform2iv(loc, 1, ints); break;
        case 3: glUniform3iv(loc, 1, ints); break;
        case 4: glUniform4iv(loc, 1, ints); break;
        default: glUniform1iv(loc, count, ints); break;
    }
    return true;
}

bool COpenGLES2Driver::setPixelShaderConstant(const c8* name, const f32* floats, int count)
{
    // In GLES2, vertex and fragment uniforms share the same namespace
    return setVertexShaderConstant(name, floats, count);
}

bool COpenGLES2Driver::setPixelShaderConstant(const c8* name, const bool* bools, int count)
{
    return setVertexShaderConstant(name, bools, count);
}

bool COpenGLES2Driver::setPixelShaderConstant(const c8* name, const s32* ints, int count)
{
    return setVertexShaderConstant(name, ints, count);
}

IVideoDriver* COpenGLES2Driver::getVideoDriver()
{
    return this;
}

void COpenGLES2Driver::enableMaterial2D(bool enable)
{
    // No special handling needed — 2D rendering uses its own shader programs
}

// ============================================================================
// Hardware Buffer (VBO/EBO) Management
// ============================================================================

bool COpenGLES2Driver::createStaticHardwareBuffer(const scene::IMeshBuffer* mb)
{
    if (!mb || mb->getVertexCount() == 0 || mb->getIndexCount() == 0)
        return false;

    // Already exists?
    auto it = HWBufferMap.find(mb);
    if (it != HWBufferMap.end())
        return true;

    // Determine vertex stride
    GLsizei stride;
    switch (mb->getVertexType()) {
        case EVT_STANDARD:    stride = sizeof(S3DVertex); break;         // 36 bytes
        case EVT_2TCOORDS:    stride = sizeof(S3DVertex2TCoords); break; // 44 bytes
        case EVT_TANGENTS:    stride = sizeof(S3DVertexTangents); break; // 60 bytes
        default: return false;
    }

    SHWBuffer hwb;
    hwb.vertexCount = mb->getVertexCount();
    hwb.indexCount = mb->getIndexCount();
    hwb.vType = mb->getVertexType();
    hwb.mappedVertexCount = hwb.vertexCount;

    // Create and upload VBO
    glGenBuffers(1, &hwb.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, hwb.vbo);
    glBufferData(GL_ARRAY_BUFFER, hwb.vertexCount * stride,
                 mb->getVertices(), GL_STATIC_DRAW);
    state_.boundVBO = hwb.vbo;

    // Create and upload EBO (16-bit indices only — Mali-400 constraint)
    glGenBuffers(1, &hwb.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hwb.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, hwb.indexCount * sizeof(u16),
                 mb->getIndices(), GL_STATIC_DRAW);
    state_.boundEBO = hwb.ebo;

    HWBufferMap[mb] = hwb;
    return true;
}

void COpenGLES2Driver::deleteStaticHardwareBuffer(const scene::IMeshBuffer* mb)
{
    auto it = HWBufferMap.find(mb);
    if (it == HWBufferMap.end())
        return;

    SHWBuffer& hwb = it->second;

    // Unbind if currently bound
    if (state_.boundVBO == hwb.vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        state_.boundVBO = 0;
    }
    if (state_.boundEBO == hwb.ebo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        state_.boundEBO = 0;
    }

    if (hwb.vbo) glDeleteBuffers(1, &hwb.vbo);
    if (hwb.ebo) glDeleteBuffers(1, &hwb.ebo);

    HWBufferMap.erase(it);
}

void COpenGLES2Driver::deleteAllHardwareBuffers()
{
    for (auto& [mb, hwb] : HWBufferMap) {
        if (hwb.vbo) glDeleteBuffers(1, &hwb.vbo);
        if (hwb.ebo) glDeleteBuffers(1, &hwb.ebo);
    }
    HWBufferMap.clear();

    // Reset bound buffer state
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    state_.boundVBO = 0;
    state_.boundEBO = 0;
}

// ============================================================================
// Factory function
// ============================================================================

IVideoDriver* createOpenGLES2Driver(const SIrrlichtCreationParameters& params,
                                     io::IFileSystem* io, CIrrDeviceFB* device)
{
    COpenGLES2Driver* driver = new COpenGLES2Driver(params, io, device);
    if (!driver->initDriver()) {
        driver->drop();
        return nullptr;
    }
    return driver;
}

} // end namespace video
} // end namespace irr

// ============================================================================
// Bridge functions for external VBO management (called from WillEQ renderer)
// These avoid the need for external code to include COpenGLES2Driver.h
// (which depends on Irrlicht-internal headers like CNullDriver.h).
// ============================================================================

bool gles2CreateStaticHWBuffer(void* driver, const void* meshBuffer)
{
    auto* d = static_cast<irr::video::COpenGLES2Driver*>(driver);
    auto* mb = static_cast<const irr::scene::IMeshBuffer*>(meshBuffer);
    return d->createStaticHardwareBuffer(mb);
}

void gles2DeleteStaticHWBuffer(void* driver, const void* meshBuffer)
{
    auto* d = static_cast<irr::video::COpenGLES2Driver*>(driver);
    auto* mb = static_cast<const irr::scene::IMeshBuffer*>(meshBuffer);
    d->deleteStaticHardwareBuffer(mb);
}

void gles2DeleteAllStaticHWBuffers(void* driver)
{
    auto* d = static_cast<irr::video::COpenGLES2Driver*>(driver);
    d->deleteAllHardwareBuffers();
}

#endif // _IRR_COMPILE_WITH_OGLES2_
