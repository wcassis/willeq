// COGLES2Texture.h — GLES2 texture class for COpenGLES2Driver
// Supports RGBA upload, native ETC1 compressed textures, and FBO render targets.

#ifndef __C_OGLES2_TEXTURE_H_INCLUDED__
#define __C_OGLES2_TEXTURE_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "ITexture.h"
#include "t122_texprobe.h"
#include <GLES2/gl2.h>
#include <atomic>

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

    // Driver-specific texture handle. B944: restores an evicted texture's GL
    // storage first (see "Residency" below), so a raw-GL consumer that binds
    // this handle itself (ParticleManager) never gets a stale name.
    virtual u32 getDriverTextureHandle() const { return getOpenGLTextureName(); }

    // GLES2-specific. B944: this is THE accessor every bind path in
    // COpenGLES2Driver goes through, so it is where an evicted texture is made
    // resident again, on first use, before its name is handed out.
    // B949: and where a resident texture is stamped "used in frame N" — the
    // driver's frame counter (COpenGLES2Driver::FrameCounter, incremented in
    // beginScene()) — so ConstrainedTextureCache's LRU can evict the
    // least-recently-DRAWN entry instead of the oldest-LOADED one. One
    // atomic load/compare/store per bind, skipped when the frame was
    // already stamped (a texture bound five times in one frame writes once).
    GLuint getOpenGLTextureName() const
    {
        if (Evicted)
            ensureResident();
        markDrawn();
        return TextureName;
    }
    GLuint getFBO() const { return FBO; }

    // ---- Residency (B944, GitLab #262) ------------------------------------
    //
    // ConstrainedTextureCache evicts a texture under its memory ceiling by
    // releasing the GL STORAGE, not the object. This object stays alive, with
    // its name, size and alpha-companion link, for as long as the cache
    // entry does, so every raw ITexture* a material, a mesh cache or a
    // manager holds stays a valid pointer across an eviction (before B944 the
    // object itself was destroyed and every such holder dangled -- the
    // gles2_zone_benchmark SIGSEGV in ZoneBench::textureGroupId). The first
    // bind afterwards calls the owner's restore function, which re-uploads the
    // same pixels and hands the new GL name back through adoptStorage().
    //
    //   releaseStorage()   delete the GL texture; GpuBytes 0; marks Evicted
    //   adoptStorage()     take a freshly uploaded GL name; clears Evicted
    //   takeStorage()      give this texture's GL name away (the restore path
    //                      uploads into a throwaway texture and moves the
    //                      storage into the evicted one); the destructor then
    //                      deletes nothing
    //   getStoredTextureName()  the name WITHOUT restoring -- for teardown
    //                      paths (removeTexture) that must never upload
    // `texture` is this object as an ITexture* (converted to void*).
    typedef bool (*ResidencyRestoreFn)(void* ctx, void* texture);
    void setResidencyOwner(ResidencyRestoreFn fn, void* ctx)
    {
        RestoreFn = fn;
        RestoreCtx = ctx;
    }
    bool isEvicted() const { return Evicted; }
    GLuint getStoredTextureName() const { return TextureName; }
    void releaseStorage();
    void adoptStorage(GLuint glTexName, u32 gpuBytes);
    GLuint takeStorage(u32* gpuBytesOut);

    // B949 (GitLab #267): the driver frame in which this texture was last
    // BOUND (COpenGLES2Driver::FrameCounter, incremented in beginScene()).
    // getOpenGLTextureName() -- the one accessor every bind path goes
    // through -- refreshes it, so a texture drawn every frame always carries
    // the current frame number while a texture that has not been drawn since
    // frame N keeps N. ConstrainedTextureCache::endFrame() reads it to put
    // the LRU list in least-recently-DRAWN order (B944's list was load
    // order: a floor texture loaded first was evicted first even though it
    // was on screen every frame). Relaxed: only THIS thread (the GL thread)
    // ever writes it, and the cache reads it from the same thread at frame
    // end.
    void markDrawn() const;
    u32 getDrawFrameStamp() const { return DrawFrameStamp_.load(std::memory_order_relaxed); }

    // Upload raw ETC1 compressed data (native, no conversion)
    void uploadETC1(const void* data, u32 dataSize, u32 width, u32 height);

    // T411 (board B261/B287): the ETC1 `_alpha` companion that carries this
    // texture's alpha channel, or nullptr when the texture carries its alpha
    // in band (RGBA8888 / raw BMP / DDS decode) or has none.
    //
    // The association is set ONCE by the app's ConstrainedTextureCache the
    // moment it wraps both GL handles (gles2SetAlphaCompanion(), the sibling
    // of gles2WrapTexture()), so every draw path -- 3D material, draw2DImage,
    // draw2DImageBatch -- can find the companion from texture unit 0 alone
    // instead of each consumer remembering to call setTexture(1, ...). That
    // is what made an ETC1 alpha invisible to every alpha-BLEND draw (B261).
    //
    // Raw, non-grabbed pointer. Safe because the cache creates the pair
    // together and destroys them together (B944: ConstrainedTextureCache's
    // destroyEntry() removes the companion right after the main texture; an
    // eviction releases both STORAGES and keeps both objects), so the
    // companion never dies while the object holding this pointer lives.
    void setAlphaCompanion(ITexture* companion) { AlphaCompanion = companion; }
    ITexture* getAlphaCompanion() const { return AlphaCompanion; }

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
    void ensureResident() const;

    // B944 residency state (see "Residency" above).
    bool Evicted = false;
    mutable bool Restoring = false;
    ResidencyRestoreFn RestoreFn = nullptr;
    void* RestoreCtx = nullptr;

    // B949: last frame this texture was bound in (see markDrawn()).
    mutable std::atomic<u32> DrawFrameStamp_{0};

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

    // T411: ETC1 alpha companion (unit 1). See setAlphaCompanion().
    ITexture* AlphaCompanion = nullptr;

    // P42 state-dedup cache (see texParamsMatch()/cacheTexParams() above).
    mutable bool TexParamsCached = false;
    mutable GLint CachedMagFilter = 0;
    mutable GLint CachedMinFilter = 0;
    mutable GLenum CachedWrapS = 0;
    mutable GLenum CachedWrapT = 0;

    // B962: the bytes this texture's storage really holds, when the uploader
    // knows them to differ from GpuBytes (an ETC1 texture whose upload sends
    // level 0 only, a main texture wrapped with its companion's bytes too --
    // B948). GpuBytes stays the budget/gate figure; the gpu_buffers tally
    // reads this. Unset = GpuBytes.
    u32 StorageBytes = 0;
    bool HasStorageBytes = false;

public:
    // Get GPU memory usage for this texture
    u32 getGpuMemoryBytes() const { return GpuBytes; }
    // B962 (see StorageBytes)
    u32 getStorageBytes() const { return HasStorageBytes ? StorageBytes : GpuBytes; }
    void setStorageBytes(u32 bytes) { StorageBytes = bytes; HasStorageBytes = true; }
};

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_OGLES2_
#endif // __C_OGLES2_TEXTURE_H_INCLUDED__
