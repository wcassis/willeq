// COGLES2Texture.cpp — GLES2 texture implementation
// Handles RGBA upload, format conversion, native ETC1, and FBO render targets.

#include "COGLES2Texture.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "COpenGLES2Driver.h"
#include "IImage.h"
#include "CImage.h"
#include "os.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstring>
#include <cstdio>

namespace irr
{
namespace video
{

// ============================================================================
// Constructor: from IImage
// ============================================================================

COGLES2Texture::COGLES2Texture(IImage* image, const io::path& name,
                               COpenGLES2Driver* driver)
    : ITexture(name),
      Driver(driver),
      TextureName(0), FBO(0), DepthBuffer(0),
      ColorFormat(ECF_A8R8G8B8), Pitch(0),
      HasMipMaps(false), IsRenderTarget(false),
      LockBuffer(nullptr), GpuBytes(0)
{
    if (!image)
        return;

    OriginalSize = image->getDimension();
    TextureSize = OriginalSize;

    uploadImage(image);
}

// ============================================================================
// Constructor: render target
// ============================================================================

COGLES2Texture::COGLES2Texture(const core::dimension2d<u32>& size, const io::path& name,
                               COpenGLES2Driver* driver)
    : ITexture(name),
      Driver(driver),
      TextureName(0), FBO(0), DepthBuffer(0),
      ColorFormat(ECF_A8R8G8B8), Pitch(0),
      HasMipMaps(false), IsRenderTarget(true),
      LockBuffer(nullptr), GpuBytes(0)
{
    OriginalSize = size;
    TextureSize = size;

    createRenderTarget(size);
}

// ============================================================================
// Destructor
// ============================================================================

COGLES2Texture::~COGLES2Texture()
{
    if (FBO)
        glDeleteFramebuffers(1, &FBO);
    if (DepthBuffer)
        glDeleteRenderbuffers(1, &DepthBuffer);
    if (TextureName)
        glDeleteTextures(1, &TextureName);
    delete[] LockBuffer;
}

// ============================================================================
// Upload from IImage
// ============================================================================

void COGLES2Texture::uploadImage(IImage* image)
{
    if (!image)
        return;

    glGenTextures(1, &TextureName);
    glBindTexture(GL_TEXTURE_2D, TextureName);

    // Set default parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    u32 w = image->getDimension().Width;
    u32 h = image->getDimension().Height;
    ECOLOR_FORMAT srcFormat = image->getColorFormat();

    // GLES2 supports GL_RGBA + GL_UNSIGNED_BYTE reliably
    // Convert source image to RGBA if needed
    if (srcFormat == ECF_A8R8G8B8) {
        // Irrlicht uses A8R8G8B8 (BGRA in memory on little-endian)
        // Need to swizzle to RGBA for GLES2
        u32 pixelCount = w * h;
        u8* rgba = new u8[pixelCount * 4];
        const u8* src = (const u8*)image->lock();

        for (u32 i = 0; i < pixelCount; i++) {
            rgba[i * 4 + 0] = src[i * 4 + 2];  // R (was at offset 2 in BGRA)
            rgba[i * 4 + 1] = src[i * 4 + 1];  // G
            rgba[i * 4 + 2] = src[i * 4 + 0];  // B (was at offset 0 in BGRA)
            rgba[i * 4 + 3] = src[i * 4 + 3];  // A
        }

        image->unlock();

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        delete[] rgba;
        ColorFormat = ECF_A8R8G8B8;
        Pitch = w * 4;
    }
    else if (srcFormat == ECF_R8G8B8) {
        // RGB — need to convert to RGBA (no 3-byte format in GLES2 on all HW)
        u32 pixelCount = w * h;
        u8* rgba = new u8[pixelCount * 4];
        const u8* src = (const u8*)image->lock();

        for (u32 i = 0; i < pixelCount; i++) {
            rgba[i * 4 + 0] = src[i * 3 + 0];  // R
            rgba[i * 4 + 1] = src[i * 3 + 1];  // G
            rgba[i * 4 + 2] = src[i * 3 + 2];  // B
            rgba[i * 4 + 3] = 255;              // A
        }

        image->unlock();

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        delete[] rgba;
        ColorFormat = ECF_A8R8G8B8;
        Pitch = w * 4;
    }
    else if (srcFormat == ECF_A1R5G5B5 || srcFormat == ECF_R5G6B5) {
        // 16-bit formats — convert to RGBA
        u32 pixelCount = w * h;
        u8* rgba = new u8[pixelCount * 4];
        const u8* src = (const u8*)image->lock();

        for (u32 i = 0; i < pixelCount; i++) {
            u16 pixel = ((const u16*)src)[i];
            if (srcFormat == ECF_A1R5G5B5) {
                rgba[i * 4 + 0] = ((pixel >> 10) & 0x1F) * 255 / 31;  // R
                rgba[i * 4 + 1] = ((pixel >> 5) & 0x1F) * 255 / 31;   // G
                rgba[i * 4 + 2] = (pixel & 0x1F) * 255 / 31;          // B
                rgba[i * 4 + 3] = (pixel & 0x8000) ? 255 : 0;         // A
            } else {
                rgba[i * 4 + 0] = ((pixel >> 11) & 0x1F) * 255 / 31;  // R
                rgba[i * 4 + 1] = ((pixel >> 5) & 0x3F) * 255 / 63;   // G
                rgba[i * 4 + 2] = (pixel & 0x1F) * 255 / 31;          // B
                rgba[i * 4 + 3] = 255;                                  // A
            }
        }

        image->unlock();

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        delete[] rgba;
        ColorFormat = ECF_A8R8G8B8;
        Pitch = w * 4;
    }
    else {
        // Unsupported format — try direct RGBA upload from lock data
        const u8* src = (const u8*)image->lock();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, src);
        image->unlock();
        ColorFormat = ECF_A8R8G8B8;
        Pitch = w * 4;
    }

    TextureSize.Width = w;
    TextureSize.Height = h;
    GpuBytes = w * h * 4;  // All uploads are GL_RGBA
}

// ============================================================================
// Upload ETC1 compressed data (native)
// ============================================================================

void COGLES2Texture::uploadETC1(const void* data, u32 dataSize, u32 width, u32 height)
{
    if (!data || dataSize == 0)
        return;

    if (TextureName == 0) {
        glGenTextures(1, &TextureName);
    }

    glBindTexture(GL_TEXTURE_2D, TextureName);

    // Set parameters for compressed textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload ETC1 data directly — GL_ETC1_RGB8_OES is guaranteed in GLES2
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
                           width, height, 0, dataSize, data);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "GLES2 ETC1 upload failed: 0x%04x (%ux%u, %u bytes)",
                 err, width, height, dataSize);
        os::Printer::log(msg, ELL_ERROR);
    }

    TextureSize.Width = width;
    TextureSize.Height = height;
    OriginalSize = TextureSize;
    ColorFormat = ECF_R8G8B8;  // ETC1 is RGB (no alpha)
    Pitch = 0;  // Compressed — no meaningful pitch
    GpuBytes = dataSize;  // ETC1: actual compressed size
}

// ============================================================================
// Render target creation
// ============================================================================

void COGLES2Texture::createRenderTarget(const core::dimension2d<u32>& size)
{
    // Create texture
    glGenTextures(1, &TextureName);
    glBindTexture(GL_TEXTURE_2D, TextureName);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.Width, size.Height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    ColorFormat = ECF_A8R8G8B8;
    Pitch = size.Width * 4;
    GpuBytes = size.Width * size.Height * 4;  // RGBA render target

    // Create depth renderbuffer
    glGenRenderbuffers(1, &DepthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, DepthBuffer);

    // Try MSAA via GL_EXT_multisampled_render_to_texture (free on tile-based GPUs)
    const SOGLES2Extensions& ext = Driver->getExtensions();
    bool useMSAA = false;
    GLsizei samples = 0;

    if (ext.hasMultisampledRenderToTexture) {
        samples = ext.maxSamples < 4 ? ext.maxSamples : 4;
        if (samples > 0) {
            ext.glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, samples,
                GL_DEPTH_COMPONENT16, size.Width, size.Height);
            useMSAA = true;
        }
    }

    if (!useMSAA) {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                              size.Width, size.Height);
    }

    // Create FBO
    glGenFramebuffers(1, &FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);

    if (useMSAA) {
        ext.glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, TextureName, 0, samples);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, TextureName, 0);
    }

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, DepthBuffer);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (useMSAA) {
            // MSAA FBO failed — fall back to non-MSAA
            char msg[128];
            snprintf(msg, sizeof(msg), "GLES2: MSAA FBO incomplete (0x%04x), falling back", status);
            os::Printer::log(msg, ELL_WARNING);

            // Recreate depth buffer without MSAA
            glBindRenderbuffer(GL_RENDERBUFFER, DepthBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                                  size.Width, size.Height);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, TextureName, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, DepthBuffer);

            status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                char msg2[128];
                snprintf(msg2, sizeof(msg2), "GLES2: FBO incomplete (status=0x%04x)", status);
                os::Printer::log(msg2, ELL_ERROR);
            }
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "GLES2: FBO incomplete (status=0x%04x)", status);
            os::Printer::log(msg, ELL_ERROR);
        }
    }

    // Restore default FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
// Lock / Unlock (for CPU-side access)
// ============================================================================

void* COGLES2Texture::lock(E_TEXTURE_LOCK_MODE mode, u32 mipmapLevel)
{
    if (LockBuffer)
        return LockBuffer;

    u32 w = TextureSize.Width;
    u32 h = TextureSize.Height;
    u32 bufSize = w * h * 4;

    LockBuffer = new u8[bufSize];

    if (mode != ETLM_WRITE_ONLY) {
        // Read pixels from texture via FBO
        GLuint tempFBO = 0;
        if (!IsRenderTarget) {
            glGenFramebuffers(1, &tempFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, TextureName, 0);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, FBO);
        }

        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, LockBuffer);

        if (tempFBO) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &tempFBO);
        }

        // Convert RGBA to BGRA (Irrlicht's A8R8G8B8)
        for (u32 i = 0; i < w * h; i++) {
            u8 tmp = LockBuffer[i * 4 + 0];
            LockBuffer[i * 4 + 0] = LockBuffer[i * 4 + 2];
            LockBuffer[i * 4 + 2] = tmp;
        }
    }

    return LockBuffer;
}

void COGLES2Texture::unlock()
{
    if (!LockBuffer)
        return;

    // Re-upload modified data
    u32 w = TextureSize.Width;
    u32 h = TextureSize.Height;

    // Convert BGRA back to RGBA
    u8* rgba = new u8[w * h * 4];
    for (u32 i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = LockBuffer[i * 4 + 2];  // R
        rgba[i * 4 + 1] = LockBuffer[i * 4 + 1];  // G
        rgba[i * 4 + 2] = LockBuffer[i * 4 + 0];  // B
        rgba[i * 4 + 3] = LockBuffer[i * 4 + 3];  // A
    }

    glBindTexture(GL_TEXTURE_2D, TextureName);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    delete[] rgba;
    delete[] LockBuffer;
    LockBuffer = nullptr;
}

void COGLES2Texture::regenerateMipMapLevels(void* mipmapData)
{
    if (TextureName) {
        glBindTexture(GL_TEXTURE_2D, TextureName);
        glGenerateMipmap(GL_TEXTURE_2D);
        HasMipMaps = true;
    }
}

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_OGLES2_
