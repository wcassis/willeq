// COGLES2Texture.h — GLES2 texture class for COpenGLES2Driver
// Supports RGBA upload, native ETC1 compressed textures, and FBO render targets.

#ifndef __C_OGLES2_TEXTURE_H_INCLUDED__
#define __C_OGLES2_TEXTURE_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "ITexture.h"
#include <GLES2/gl2.h>

namespace irr
{
namespace video
{

class COpenGLES2Driver;

class COGLES2Texture : public ITexture
{
public:
    // Create texture from an IImage
    COGLES2Texture(IImage* image, const io::path& name, COpenGLES2Driver* driver);

    // Create render target texture (blank, with FBO)
    COGLES2Texture(const core::dimension2d<u32>& size, const io::path& name,
                   COpenGLES2Driver* driver);

    virtual ~COGLES2Texture();

    // ITexture interface
    virtual void* lock(E_TEXTURE_LOCK_MODE mode=ETLM_READ_WRITE, u32 mipmapLevel=0);
    virtual void unlock();
    virtual const core::dimension2d<u32>& getOriginalSize() const { return OriginalSize; }
    virtual const core::dimension2d<u32>& getSize() const { return TextureSize; }
    virtual E_DRIVER_TYPE getDriverType() const { return EDT_OGLES2; }
    virtual ECOLOR_FORMAT getColorFormat() const { return ColorFormat; }
    virtual u32 getPitch() const { return Pitch; }
    virtual bool hasMipMaps() const { return HasMipMaps; }
    virtual void regenerateMipMapLevels(void* mipmapData=0);
    virtual bool isRenderTarget() const { return IsRenderTarget; }

    // Driver-specific texture handle
    virtual u32 getDriverTextureHandle() const { return TextureName; }

    // GLES2-specific
    GLuint getOpenGLTextureName() const { return TextureName; }
    GLuint getFBO() const { return FBO; }

    // Upload raw ETC1 compressed data (native, no conversion)
    void uploadETC1(const void* data, u32 dataSize, u32 width, u32 height);

private:
    void uploadImage(IImage* image);
    void createRenderTarget(const core::dimension2d<u32>& size);

    COpenGLES2Driver* Driver;
    GLuint TextureName;
    GLuint FBO;           // Framebuffer object (0 if not a render target)
    GLuint DepthBuffer;   // Depth renderbuffer (for render targets)

    core::dimension2d<u32> OriginalSize;
    core::dimension2d<u32> TextureSize;
    ECOLOR_FORMAT ColorFormat;
    u32 Pitch;
    bool HasMipMaps;
    bool IsRenderTarget;

    // Lock buffer (for lock/unlock support)
    u8* LockBuffer;

    // GPU memory usage tracking (set during upload)
    u32 GpuBytes;

public:
    // Get GPU memory usage for this texture
    u32 getGpuMemoryBytes() const { return GpuBytes; }
};

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_OGLES2_
#endif // __C_OGLES2_TEXTURE_H_INCLUDED__
