// COGLES2Texture.h — GLES2 texture class for COpenGLES2Driver
// Supports RGBA upload, native ETC1 compressed textures, and FBO render targets.

#ifndef __C_OGLES2_TEXTURE_H_INCLUDED__
#define __C_OGLES2_TEXTURE_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "ITexture.h"
#include "t122_texprobe.h"
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

    // Wrap an already-uploaded GL texture (no glGenTextures/glTexImage2D — caller owns the upload)
    // gpuBytes: actual GPU memory consumed (0 = assume RGBA8888 = w*h*4)
    COGLES2Texture(const io::path& name, GLuint glTexName,
                   const core::dimension2d<u32>& size, COpenGLES2Driver* driver,
                   u32 gpuBytes = 0);

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

    // P42 (P38 state-dedup prototype, productionized): per-texture-object
    // cache of the last-applied mag/min-filter + wrap-S/T combo, so
    // COpenGLES2Driver::applyMaterialRenderState() can skip the 4
    // glTexParameteri calls when nothing changed since the last draw that
    // used this texture. P22 proved (via GL-readback instrumentation) that
    // every texture requests exactly ONE filter/wrap combo for its whole
    // lifetime in the traced paths, so a simple last-value cache is safe —
    // no eviction/invalidation needed. Cache lives on the C++ texture
    // object (not a driver-side GLuint-keyed map) so a deleted+recreated
    // texture that happens to reuse a GL name never sees a stale hit: the
    // new COGLES2Texture instance starts with TexParamsCached=false.
    // Mutable because this is a cache, called through a `const
    // COGLES2Texture*` in applyMaterialRenderState().
    bool texParamsMatch(GLint magFilter, GLint minFilter, GLenum wrapS, GLenum wrapT) const
    {
        return TexParamsCached && CachedMagFilter == magFilter && CachedMinFilter == minFilter &&
               CachedWrapS == wrapS && CachedWrapT == wrapT;
    }
    void cacheTexParams(GLint magFilter, GLint minFilter, GLenum wrapS, GLenum wrapT) const
    {
        // T122: record the write BEFORE it happens, so a crash mid-write
        // (e.g. because `this` is not actually a live COGLES2Texture) still
        // leaves the TEXPARAMS entry in the ring. No-op when
        // WILLEQ_T122_TEXPROBE is not defined.
        T122_RECORD_TEXPARAMS(this, static_cast<uint32_t>(magFilter));
        TexParamsCached = true;
        CachedMagFilter = magFilter;
        CachedMinFilter = minFilter;
        CachedWrapS = wrapS;
        CachedWrapT = wrapT;
    }

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

    // P42 state-dedup cache (see texParamsMatch()/cacheTexParams() above).
    mutable bool TexParamsCached = false;
    mutable GLint CachedMagFilter = 0;
    mutable GLint CachedMinFilter = 0;
    mutable GLenum CachedWrapS = 0;
    mutable GLenum CachedWrapT = 0;

public:
    // Get GPU memory usage for this texture
    u32 getGpuMemoryBytes() const { return GpuBytes; }
};

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_OGLES2_
#endif // __C_OGLES2_TEXTURE_H_INCLUDED__
