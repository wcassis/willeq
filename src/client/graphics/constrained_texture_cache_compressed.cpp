// constrained_texture_cache_compressed.cpp — GPU compressed upload and EQE1.
// Compressed-format probing and tryCompressedUpload(), the EQE1 flat-pack
// loader registry, raw-data/entry lookup and request logging, and the
// uploadCompressedETC1()/uploadCompressedV2() upload paths.
// Split from the 2,211-line original by refactor/J10_constrained_texture_cache_split.md.
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/zone_geometry.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include "client/core/gem_owner_census.h"  // B962
#include "client/graphics/gpu_buffers_legacy.h"  // B962
#include <algorithm>
#include <optional>
#include <cstring>
#include <functional>
#include <fstream>
#include <cstdlib>

// P03: the EQE1 upload path (ETC1 + v2 payloads) is available on any GLES2
// driver build, not just DRM/ARM — EQT_HAS_GLES2 is defined for the x86
// llvmpipe dev build (EQT_DRM=OFF, EQT_GLES2=ON), where gles2WrapTexture and
// the GL entry points below are equally present. The ARM/Pi build defines
// BOTH macros, so its preprocessed output is unchanged by this widening.
#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)
#define EQT_EQE1_UPLOAD_AVAILABLE 1
#include <GL/gl.h>
#include <GL/glext.h>
// glGenerateMipmap and glCompressedTexImage2D may not be directly available
// in all GL headers; get function pointers via EGL at runtime
#include <EGL/egl.h>
typedef void (*PFNGLGENERATEMIPMAPPROC_)(GLenum target);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC_)(GLenum target, GLint level,
    GLenum internalformat, GLsizei width, GLsizei height, GLint border,
    GLsizei imageSize, const void* data);

// Defined in COpenGLES2Driver.cpp — wraps a raw GL texture handle in an ITexture*
extern void* gles2WrapTexture(void* driver, const char* name, unsigned int glTexName,
                               unsigned int width, unsigned int height,
                               unsigned int gpuBytes);
// T411 (board B261): records the ETC1 `_alpha` companion ON the main texture,
// so every draw path (3D material, draw2DImage, draw2DImageBatch) can bind
// unit 1 from unit 0 alone instead of each consumer calling setTexture(1,...).
extern void gles2SetAlphaCompanion(void* mainTexture, void* companionTexture);
// B962: the bytes a texture's storage really holds (COGLES2Texture::StorageBytes).
extern void gles2TextureSetStorageBytes(void* texture, unsigned int bytes);
#endif

namespace EQT {
namespace Graphics {

void ConstrainedTextureCache::probeCompressedTextureSupport() {
    // B338: the probe result is diagnostic only — it answers "does this GPU
    // have S3TC" for the log. It is the sole consumer of
    // config_.enableCompressedTextures, so it stays, but it no longer writes
    // a state flag nothing consulted (the old
    // compressedTexturesAvailable_ member).
    if (!config_.enableCompressedTextures) {
        return;
    }

#ifdef EQT_HAS_DRM
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && strstr(extensions, "GL_EXT_texture_compression_s3tc")) {
        LOG_INFO(MOD_GRAPHICS, "Compressed textures: S3TC support detected, enabled");
    } else {
        LOG_WARN(MOD_GRAPHICS, "Compressed textures: S3TC not available, falling back to software decode");
    }
#endif
}

irr::video::ITexture* ConstrainedTextureCache::tryCompressedUpload(
        const std::string& name, const std::vector<char>& data) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDecoded);  // T491
#ifdef EQT_HAS_DRM
    // Extract compressed DXT data without decompressing
    CompressedTextureData compressed = DDSDecoder::extractCompressed(data);
    if (!compressed.isValid()) {
        return nullptr;  // Not DDS or unsupported format
    }

    // Cannot downsample compressed data — skip if oversized
    if (static_cast<int>(compressed.width) > config_.maxTextureDimension ||
        static_cast<int>(compressed.height) > config_.maxTextureDimension) {
        LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: '{}' {}x{} exceeds max dimension {}, falling back",
            name, compressed.width, compressed.height, config_.maxTextureDimension);
        return nullptr;
    }

    // Track uncompressed size for memory budget.  On Lima/Mesa the driver
    // software-decodes S3TC and stores textures uncompressed in GPU memory,
    // so the budget must reflect the real GPU footprint (including mipmaps).
    size_t textureSize = calculateTextureSize(compressed.width, compressed.height);

    // Evict textures if needed to make room
    if (!evictUntilAvailable(textureSize)) {
        LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: eviction failed for '{}' (need {} bytes)", name, textureSize);
        return nullptr;
    }

    // Create a correctly-sized Irrlicht texture so its internal state (dimensions,
    // format tracking) matches the compressed data we'll upload.  Using a zeroed
    // buffer is a temporary allocation that is freed after addTexture.
    size_t dummyBytes = static_cast<size_t>(compressed.width) * compressed.height * 4;
    std::vector<uint8_t> dummyPixels(dummyBytes, 0);

    // T491: driver-side from here (IImage, COGLES2Texture, the ETC1 upload).
    eqt::memory::MemoryScope driverScope(eqt::memory::Category::TexturesDriver);
    eqt::memory::GemTag gemTag("tex:dxt", name.c_str(), compressed.width, compressed.height, 1);
    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(compressed.width, compressed.height),
        dummyPixels.data(), false);

    if (!image) {
        return nullptr;
    }

    irr::video::ITexture* texture = driver_->addTexture(name.c_str(), image);
    image->drop();
    // Release the temporary dummy buffer immediately
    { std::vector<uint8_t>().swap(dummyPixels); }

    if (!texture) {
        return nullptr;
    }

    // Get the underlying OpenGL texture name
    irr::u32 glName = texture->getDriverTextureHandle();
    if (glName == 0) {
        driver_->removeTexture(texture);
        return nullptr;
    }

    // Copy compressed data to aligned buffer (ARM bus error safety)
    std::vector<uint8_t> alignedData(compressed.data, compressed.data + compressed.dataSize);

    // Resolve GL function pointers via EGL
    static auto pfnCompressedTexImage2D = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC_>(
        eglGetProcAddress("glCompressedTexImage2D"));
    static auto pfnGenerateMipmap = reinterpret_cast<PFNGLGENERATEMIPMAPPROC_>(
        eglGetProcAddress("glGenerateMipmap"));

    if (!pfnCompressedTexImage2D) {
        LOG_WARN(MOD_GRAPHICS, "Compressed upload: glCompressedTexImage2D not available");
        driver_->removeTexture(texture);
        return nullptr;
    }

    // Drain any stale GL errors left by Irrlicht's addTexture
    while (glGetError() != GL_NO_ERROR) {}

    // Upload compressed data directly to the GPU
    glBindTexture(GL_TEXTURE_2D, glName);
    pfnCompressedTexImage2D(GL_TEXTURE_2D, 0, compressed.glFormat,
                            compressed.width, compressed.height, 0,
                            static_cast<GLsizei>(alignedData.size()),
                            alignedData.data());

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_WARN(MOD_GRAPHICS, "Compressed upload: glCompressedTexImage2D failed for '{}' (GL error 0x{:X})",
            name, static_cast<unsigned>(err));
        driver_->removeTexture(texture);
        return nullptr;
    }

    // Generate mipmaps if enabled
    if (config_.enableMipmaps && pfnGenerateMipmap) {
        pfnGenerateMipmap(GL_TEXTURE_2D);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            // Mipmap generation failed — not fatal, texture still usable
            LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: glGenerateMipmap failed for '{}' (GL error 0x{:X})", name, static_cast<unsigned>(err));
        }
    }

    // Set bilinear filtering directly on GL texture (see getOrLoad comment).
    // P42 (P38 finding): same dead-branch simplification as getOrLoad() --
    // this MIN_FILTER value is stomped by applyMaterialRenderState() before
    // first sample regardless of texture format, so the enableMipmaps
    // branch here was equally a no-op. No behavior change. (The
    // glGenerateMipmap() call just above is left as-is -- this whole
    // function is unreachable in the shipped orangepi preset,
    // enableCompressedTextures=false, so it does not affect production
    // either way; out of scope for this hygiene pass.)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    LOG_DEBUG(MOD_GRAPHICS, "Compressed upload: '{}' {}x{} {} (DXT {} bytes, GPU {} bytes)",
        name, compressed.width, compressed.height,
        (compressed.glFormat == 0x83F0 ? "DXT1" :
         compressed.glFormat == 0x83F2 ? "DXT3" : "DXT5"),
        compressed.dataSize, textureSize);

    // DXT3 and DXT5 always have alpha; DXT1 RGBA has 1-bit alpha
    bool compressedHasAlpha = (compressed.glFormat != 0x83F0);  // Not GL_COMPRESSED_RGB_S3TC_DXT1

    // Add to cache
    lruOrder_.push_back(name);
    CachedTexture entry;
    entry.texture = texture;
    entry.sizeBytes = textureSize;
    entry.hasAlpha = compressedHasAlpha;
    entry.lruIterator = std::prev(lruOrder_.end());
    cache_[name] = entry;
    currentUsage_ += textureSize;
    ++compressedUploadCount_;

    return texture;
#else
    (void)name;
    (void)data;
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// EQE1 flat pack support
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::addEQE1Loader(const EQE1Loader* loader) {
    if (loader) {
        std::lock_guard<std::mutex> lock(eqe1LoadersMutex_);
        eqe1Loaders_.push_back(loader);
    }
}

void ConstrainedTextureCache::clearEQE1Loaders() {
    {
        std::lock_guard<std::mutex> lock(eqe1LoadersMutex_);
        eqe1Loaders_.clear();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    compressedEntries_.clear();
    // B944: an entry's restore source points INTO a loader's mapping, and the
    // caller (unloadZone) destroys every non-persistent loader right after
    // this. Only protected entries -- process-lifetime textures from the
    // persistent willeq.eqe1 -- keep theirs; the rest are destroyed by the
    // clear() that follows, and until then an evicted one simply cannot be
    // restored (it binds GL name 0), rather than reading unmapped memory.
    for (auto& [key, entry] : cache_) {
        if (entry.source == CachedTexture::Source::Compressed &&
            protectedTextures_.find(key) == protectedTextures_.end()) {
            entry.source = CachedTexture::Source::None;
            entry.compressed = CompressedEntry{};
        }
    }
}

bool ConstrainedTextureCache::hasEQE1Loaders() const {
    return !snapshotEQE1Loaders().empty();
}

std::vector<const EQE1Loader*> ConstrainedTextureCache::snapshotEQE1Loaders() const {
    std::lock_guard<std::mutex> lock(eqe1LoadersMutex_);
    return eqe1Loaders_;
}

bool ConstrainedTextureCache::getEQE1RawData(const std::string& name,
                                               const uint8_t*& data, uint32_t& size) const {
    // P04/T207: search already-open archives first -- that keeps the current
    // zone's archives preferred, since they are opened up front. On a miss ask
    // the resolver for the ONE archive that holds this name; if it opens one,
    // retry against it. At most one open per lookup, and zero when the name is
    // in no archive at all. The loader-pointer vector is copied under
    // eqe1LoadersMutex_ so the resolver (which appends via addEQE1Loader) never
    // mutates a vector we are iterating; loaders live until zone teardown.
    size_t searched = 0;
    auto loaders = snapshotEQE1Loaders();
    for (; searched < loaders.size(); ++searched) {
        if (loaders[searched]->getRawData(name, data, size)) {
            recordEQE1Request(name, loaders[searched], true);
            return true;
        }
    }
    if (!eqe1ArchiveResolver_) { recordEQE1Request(name, nullptr, true); return false; }
    const EQE1Loader* opened = eqe1ArchiveResolver_(name);
    if (!opened) { recordEQE1Request(name, nullptr, true); return false; }
    const bool got = opened->getRawData(name, data, size);
    recordEQE1Request(name, got ? opened : nullptr, true);
    return got;
}

const ConstrainedTextureCache::CompressedEntry*
ConstrainedTextureCache::findCompressedEntry(const std::string& name) {
    if (snapshotEQE1Loaders().empty() && !eqe1ArchiveResolver_) return nullptr;

    // Strip .bmp extension — EQE1 stores names without extension
    std::string lookupName = name;
    if (lookupName.size() > 4) {
        auto ext = lookupName.substr(lookupName.size() - 4);
        if (ext == ".bmp" || ext == ".dds") {
            lookupName = lookupName.substr(0, lookupName.size() - 4);
        }
    }

    // Lowercase
    std::transform(lookupName.begin(), lookupName.end(), lookupName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // P04/T207: as above — search open archives, then resolve the miss to at
    // most one archive via the index. Vector snapshotted under
    // eqe1LoadersMutex_ (see getEQE1RawData).
    auto emit = [](const EQE1Loader::TextureEntry* entry) -> const CompressedEntry* {
        // Build a static CompressedEntry to return pointer to
        static thread_local CompressedEntry result;
        result.etc1Data = entry->data;
        result.dataSize = entry->dataSize;
        result.width = entry->width;
        result.height = entry->height;
        result.mipLevels = entry->mipLevels;
        result.hasAlpha = entry->hasAlpha;
        result.alphaData = entry->alphaData;
        result.alphaDataSize = entry->alphaDataSize;
        result.payloadFormat = entry->payloadFormat;
        return &result;
    };

    auto loaders = snapshotEQE1Loaders();
    for (const auto* loader : loaders) {
        if (const auto* entry = loader->lookup(lookupName)) {
            recordEQE1Request(lookupName, loader, false);
            return emit(entry);
        }
    }
    if (!eqe1ArchiveResolver_) { recordEQE1Request(lookupName, nullptr, false); return nullptr; }
    const EQE1Loader* opened = eqe1ArchiveResolver_(lookupName);
    if (!opened) { recordEQE1Request(lookupName, nullptr, false); return nullptr; }
    if (const auto* entry = opened->lookup(lookupName)) {
        recordEQE1Request(lookupName, opened, false);
        return emit(entry);
    }
    recordEQE1Request(lookupName, nullptr, false);
    return nullptr;
}

// T214 AUDIT INSTRUMENT ------------------------------------------------------
// Records every DISTINCT name looked up against the EQE1 set, with the archive
// that served it. Exists to answer one question with data instead of
// inference: which open archives does a zone load actually need?
static std::string eqe1ArchiveStem(const std::string& filepath) {
    size_t slash = filepath.find_last_of('/');
    std::string base = (slash == std::string::npos) ? filepath : filepath.substr(slash + 1);
    size_t dot = base.rfind(".eqe1");
    if (dot != std::string::npos) base.resize(dot);
    return base;
}

void ConstrainedTextureCache::recordEQE1Request(const std::string& name,
                                                const EQE1Loader* servedBy,
                                                bool rawData) const {
    std::lock_guard<std::mutex> lock(eqe1RequestLogMutex_);
    auto& slot = eqe1RequestLog_[name];
    ++slot.count;
    slot.rawData = rawData;
    // First resolution wins; a later miss must not erase a real owner.
    if (slot.servedBy.empty() || slot.servedBy == "<NOT-FOUND>") {
        slot.servedBy = servedBy ? eqe1ArchiveStem(servedBy->getFilePath()) : "<NOT-FOUND>";
    }
}

void ConstrainedTextureCache::noteEQE1LateOpen(const std::string& archiveStem) {
    std::lock_guard<std::mutex> lock(eqe1RequestLogMutex_);
    eqe1LateOpened_.insert(archiveStem);
}

void ConstrainedTextureCache::dumpEQE1RequestLog(const std::string& path) const {
    std::lock_guard<std::mutex> lock(eqe1RequestLogMutex_);
    std::ofstream out(path);
    if (!out.is_open()) {
        LOG_WARN(MOD_GRAPHICS, "T214: cannot write EQE1 request log to '{}'", path);
        return;
    }
    out << "name\tserved_by\tlookups\tkind\thow\n";
    for (const auto& [name, req] : eqe1RequestLog_) {
        const bool late = eqe1LateOpened_.count(req.servedBy) != 0;
        out << name << '\t' << req.servedBy << '\t' << req.count << '\t'
            << (req.rawData ? "raw" : "texture") << '\t'
            << (late ? "late-open" : "-") << '\n';
    }
    LOG_INFO(MOD_GRAPHICS, "T214: wrote {} distinct EQE1 request(s) to {}",
             eqe1RequestLog_.size(), path);
}

irr::video::ITexture* ConstrainedTextureCache::uploadCompressedETC1(
    const std::string& name, const std::string& cacheEntryKey, const CompressedEntry& entry)
{
    // P46d: `name` is the real texture name (GL debug label, ledger row,
    // ZoneMeshBuilder registration — see uploadTexture()'s comment on why
    // that registration deliberately stays name-keyed); `cacheEntryKey` is
    // this class's own internal (possibly mask-mangled) bookkeeping key.
    // mutex_ is already held by caller (uploadTexture)
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    // Budget check — use ETC1 compressed size, not RGBA size
    size_t gpuSize = EQT::Graphics::etc1MipChainSize(entry.width, entry.height, entry.mipLevels);
    if (entry.hasAlpha) {
        gpuSize += EQT::Graphics::etc1MipChainSize(entry.width, entry.height, entry.mipLevels);
    }

    if (!evictUntilAvailable(gpuSize)) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: budget exhausted uploading ETC1 '{}' (need {} bytes)",
                 name, gpuSize);
        return nullptr;
    }

    unsigned int texId = 0;
    unsigned int alphaTexId = 0;
    if (!createETC1Storage(name, entry, texId, alphaTexId)) {
        return nullptr;
    }

    // Wrap GL handle in ITexture* via driver (gles2WrapTexture declared at file scope above).
    // P46d: use cacheEntryKey, not the plain `name` -- COpenGLES2Driver::
    // wrapExternalTexture() does the same name-based dedup as addTexture()
    // (findTexture(name); if found, returns the EXISTING texture, silently
    // discarding this upload). See uploadTexture()'s identical fix/comment.
    irr::video::ITexture* texture = static_cast<irr::video::ITexture*>(
        gles2WrapTexture(driver_, cacheEntryKey.c_str(), texId,
                         entry.width, entry.height,
                         static_cast<unsigned int>(gpuSize)));

    if (!texture) {
        glDeleteTextures(1, &texId);
        if (alphaTexId) glDeleteTextures(1, &alphaTexId);
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: wrapTexture failed for ETC1 '{}'", name);
        return nullptr;
    }

    // Wrap alpha companion texture if present
    irr::video::ITexture* alphaTexture = nullptr;
    if (alphaTexId) {
        std::string alphaName = cacheEntryKey + "_alpha";
        alphaTexture = static_cast<irr::video::ITexture*>(
            gles2WrapTexture(driver_, alphaName.c_str(), alphaTexId,
                             entry.width, entry.height,
                             static_cast<unsigned int>(
                                 EQT::Graphics::etc1MipChainSize(entry.width, entry.height, entry.mipLevels))));
        if (!alphaTexture) {
            glDeleteTextures(1, &alphaTexId);
            LOG_WARN(MOD_GRAPHICS, "Constrained cache: wrapTexture failed for ETC1 alpha '{}'", alphaName);
        }
    }

    // T411 (board B261): the ONE place the pair is known. Recording it on the
    // texture is what makes an ETC1 alpha visible to the alpha-BLEND shaders
    // (SOLID3D_FS, UI2D_FS, the particle point sprite); before this only the
    // alpha-TEST variants saw it, and only where a consumer had remembered to
    // call setTexture(1, getAlphaTexture(...)).
    gles2SetAlphaCompanion(texture, alphaTexture);
    // B962: what the two storages really hold (the wrap above carries the
    // pair's full-chain budget figure on the main texture -- B948).
    gles2TextureSetStorageBytes(texture, etc1StorageBytes(entry, false));
    if (alphaTexture) gles2TextureSetStorageBytes(alphaTexture, etc1StorageBytes(entry, true));

    // Register in LRU cache (P46d: under cacheEntryKey — see cacheKey())
    lruOrder_.push_back(cacheEntryKey);
    CachedTexture cached;
    cached.texture = texture;
    cached.alphaTexture = alphaTexture;
    cached.sizeBytes = gpuSize;
    cached.hasAlpha = entry.hasAlpha;
    cached.isETC1 = true;  // H02d: companion-based alpha pipeline
    cached.lruIterator = std::prev(lruOrder_.end());
    // B944: the pack entry is where an eviction restores from (the pack is
    // mapped, so this costs a struct, not the pixels).
    cached.source = CachedTexture::Source::Compressed;
    cached.compressed = entry;

    if (ledgerEnabled_) {
        std::string ledgerCategory = "uncategorized";
        std::string ledgerCallsite;
        auto tagIt = pendingLedgerTag_.find(cacheEntryKey);
        if (tagIt != pendingLedgerTag_.end()) {
            ledgerCategory = tagIt->second.first;
            ledgerCallsite = tagIt->second.second;
            pendingLedgerTag_.erase(tagIt);
        }
        cached.category = ledgerCategory;
        ledger_.recordUpload(name, entry.width, entry.height, gpuSize,
                              entry.hasAlpha ? "ETC1+alpha" : "ETC1",
                              ledgerCategory, ledgerCallsite);
    }

    cache_[cacheEntryKey] = cached;
    currentUsage_ += gpuSize;
    ++compressedUploadCount_;

    // Register in mesh builder for entity texture lookups
    if (meshBuilder_) {
        meshBuilder_->registerUploadedTexture(name, texture, entry.hasAlpha);
    }

    LOG_DEBUG(MOD_GRAPHICS, "  [CONSTRAINED] ETC1 texture '{}' uploaded ({}x{}, {} mips, {} bytes{})",
              name, entry.width, entry.height, entry.mipLevels, gpuSize,
              entry.hasAlpha ? ", +alpha" : "");
    return texture;
#else
    // Non-GLES2 builds: ETC1 not supported, fall through to BMP decode
    (void)name;
    (void)cacheEntryKey;
    (void)entry;
    LOG_WARN(MOD_GRAPHICS, "Constrained cache: ETC1 upload not available (non-GLES2 build) for '{}'", name);
    return nullptr;
#endif
}

// B962: mirrors createETC1Storage()'s level choice exactly.
uint32_t ConstrainedTextureCache::etc1StorageBytes(const CompressedEntry& entry, bool companion) {
    uint8_t levels = 1;
    if (gpuBuffersLegacy()) {
        levels = entry.mipLevels;
    } else if (companion) {
        const bool mipmapped = entry.mipLevels > 1 &&
            entry.mipLevels == EQT::Graphics::computeMipLevels(entry.width, entry.height);
        levels = mipmapped ? entry.mipLevels : 1;
    }
    uint32_t bytes = 0;
    uint16_t w = entry.width, h = entry.height;
    for (uint8_t level = 0; level < levels && level < entry.mipLevels; ++level) {
        bytes += EQT::Graphics::etc1DataSize(w, h);
        w = std::max<uint16_t>(1, w / 2);
        h = std::max<uint16_t>(1, h / 2);
    }
    return bytes;
}

uint32_t ConstrainedTextureCache::v2StorageBytes(const CompressedEntry& entry) {
    const auto fmt = static_cast<EQT::Graphics::EQE1PayloadFormat>(entry.payloadFormat);
    if (gpuBuffersLegacy()) {
        return static_cast<uint32_t>(EQT::Graphics::payloadMipChainSize(entry.width, entry.height,
                                                                        entry.mipLevels, fmt));
    }
    return static_cast<uint32_t>(EQT::Graphics::payloadDataSize(entry.width, entry.height, fmt));
}

// B944: the GL half of uploadCompressedETC1(), shared with the eviction
// restore (constrained_texture_cache_residency.cpp) so a restored texture is
// built by the SAME code as the first upload -- same levels, same filters.
// Leaves GL_TEXTURE_2D on the active unit bound to 0, as it always did.
bool ConstrainedTextureCache::createETC1Storage(const std::string& name, const CompressedEntry& entry,
                                                unsigned int& texIdOut, unsigned int& alphaTexIdOut) {
    texIdOut = 0;
    alphaTexIdOut = 0;
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    // Generate GL texture and upload ETC1 mip chain
    static constexpr uint32_t GL_ETC1_RGB8 = 0x8D64;  // GL_ETC1_RGB8_OES

    GLuint texId = 0;
    glGenTextures(1, &texId);
    if (texId == 0) {
        LOG_ERROR(MOD_GRAPHICS, "Constrained cache: glGenTextures failed for '{}'", name);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texId);

    // B962: GEM owner tag for the census shim (no-op without it).
    std::optional<eqt::memory::GemTag> gemTag;
    gemTag.emplace("tex:etc1", name.c_str(), entry.width, entry.height, entry.mipLevels);

    // B962: unit 0 is NEVER sampled through a mipmap filter (T275 below, and
    // COpenGLES2Driver::applyMaterialRenderState() rewrites it to plain
    // GL_LINEAR/GL_NEAREST on every bind), so only level 0 is uploaded, and the
    // filter is set FIRST. Uploading the chain with the default
    // GL_NEAREST_MIPMAP_LINEAR in place made Mesa allocate level 0 alone and
    // park every further level in a GEM buffer of its own (4 KiB minimum each),
    // none of which was ever read -- refactor/B962/README.md.
    const bool legacyLevels = gpuBuffersLegacy();
    if (!legacyLevels) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    const uint8_t mainLevels = legacyLevels ? entry.mipLevels : std::min<uint8_t>(entry.mipLevels, 1);

    // Upload RGB mip chain
    uint32_t offset = 0;
    uint16_t w = entry.width, h = entry.height;
    for (uint8_t level = 0; level < mainLevels; ++level) {
        uint32_t levelSize = EQT::Graphics::etc1DataSize(w, h);

        // Get function pointer for glCompressedTexImage2D
        static auto pfnCompressedTexImage2D =
            reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC_>(
                eglGetProcAddress("glCompressedTexImage2D"));

        if (pfnCompressedTexImage2D) {
            pfnCompressedTexImage2D(GL_TEXTURE_2D, level, GL_ETC1_RGB8,
                                     w, h, 0, levelSize, entry.etc1Data + offset);
        }

        offset += levelSize;
        w = std::max<uint16_t>(1, w / 2);
        h = std::max<uint16_t>(1, h / 2);
    }

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // T275: NOT a mipmap min filter. An eqe1_builder ETC1 mip chain stops at
    // 4x4 (ETC1's block size) -- for a 256x256 texture that is 7 levels where
    // GLES2 demands 9 (down to 1x1) for MIPMAP COMPLETENESS. Asking for
    // GL_LINEAR_MIPMAP_LINEAR against a chain that short leaves the texture
    // INCOMPLETE, and sampling an incomplete texture returns opaque black
    // (0,0,0,1). Nothing here ever called glGenerateMipmap to fill the gap.
    //
    // The 3D path never showed the symptom because
    // COpenGLES2Driver::applyMaterialRenderState() overwrites texture unit
    // 0's MIN_FILTER with plain GL_LINEAR/GL_NEAREST on every material bind,
    // so the incomplete-mipmap filter was stomped before any 3D sampler saw
    // it. The 2D path (draw2DImage, which the whole static UI goes through)
    // binds the texture WITHOUT touching filters -- so `ui_atlas` reached the
    // sampler still incomplete and every UI sprite, bar and drawRect painted
    // opaque black. That is the T272 "drawRect draws nothing visible"
    // finding: it was drawing, in black, on a panel that was itself black for
    // the same reason.
    //
    // P42 already reached this conclusion for the RGBA upload path above
    // ("requested filtering against an incomplete mip chain") and simplified
    // it to the filter that actually took effect; the two EQE1 compressed
    // paths were missed. Same reasoning, same resolution, and identically a
    // no-op for 3D rendering.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Upload alpha texture if present
    GLuint alphaTexId = 0;
    gemTag.reset();
    if (entry.hasAlpha && entry.alphaData) {
        gemTag.emplace("tex:etc1a", name.c_str(), entry.width, entry.height, entry.mipLevels);
        glGenTextures(1, &alphaTexId);
        glBindTexture(GL_TEXTURE_2D, alphaTexId);

        // B962: the companion's minification filter (T279 below) decides what
        // is sampled, so it is set BEFORE the upload: with the mipmap filter in
        // place Mesa allocates the whole chain at level 0 and every level lands
        // in that one buffer (no per-level buffers, no reallocation at first
        // draw); a short chain is sampled at level 0 only, so only level 0 goes.
        const bool alphaMipComplete =
            entry.mipLevels == EQT::Graphics::computeMipLevels(entry.width, entry.height);
        const bool alphaMipmapped = alphaMipComplete && entry.mipLevels > 1;
        if (!legacyLevels) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            alphaMipmapped ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        }
        const uint8_t alphaLevels =
            (legacyLevels || alphaMipmapped) ? entry.mipLevels : std::min<uint8_t>(entry.mipLevels, 1);

        offset = 0;
        w = entry.width;
        h = entry.height;
        for (uint8_t level = 0; level < alphaLevels; ++level) {
            uint32_t levelSize = EQT::Graphics::etc1DataSize(w, h);

            static auto pfnCompressedTexImage2D2 =
                reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC_>(
                    eglGetProcAddress("glCompressedTexImage2D"));

            if (pfnCompressedTexImage2D2) {
                pfnCompressedTexImage2D2(GL_TEXTURE_2D, level, GL_ETC1_RGB8,
                                          w, h, 0, levelSize, entry.alphaData + offset);
            }

            offset += levelSize;
            w = std::max<uint16_t>(1, w / 2);
            h = std::max<uint16_t>(1, h / 2);
        }

        // Alpha companion textures use NEAREST_MIPMAP_NEAREST filtering.
        // Binary alpha masks must not be linearly interpolated — trilinear
        // blending pushes binary values below the 0.5 discard threshold.
        // NEAREST_MIPMAP_NEAREST selects the closest mip level without
        // interpolation, preserving the binary alpha mask. That intent is
        // unchanged; what changed (T279) is the completeness precondition.
        //
        // T279: `mipLevels > 1` was the WRONG guard. A mipmap MIN_FILTER is
        // only legal against a MIPMAP-COMPLETE texture — every level present
        // down to 1x1 — and packs built before `9189ad69` stopped the chain
        // at 4x4 (7 levels for 256x256 where GLES2 wants 9). Such a texture
        // has mipLevels > 1 and is still incomplete, and sampling it returns
        // opaque black (T275): for an alpha companion that means alpha==0
        // everywhere, i.e. every alpha-tested fragment discarded — invisible
        // foliage/hair/capes. Unlike unit 0, the driver never rewrites unit
        // 1's filters per material bind, so nothing masks it here.
        //
        // The guard is now the actual completeness predicate, checked against
        // the entry's own dimensions rather than trusting the asset tree to
        // be freshly built. A short chain degrades to plain GL_NEAREST (the
        // T275 resolution) instead of sampling black.
        const bool mipComplete =
            entry.mipLevels == EQT::Graphics::computeMipLevels(entry.width, entry.height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (mipComplete && entry.mipLevels > 1) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    texIdOut = texId;
    alphaTexIdOut = alphaTexId;
    return true;
#else
    (void)name;
    (void)entry;
    return false;
#endif
}

irr::video::ITexture* ConstrainedTextureCache::uploadCompressedV2(
    const std::string& name, const std::string& cacheEntryKey, const CompressedEntry& entry)
{
    // P46d: see uploadCompressedETC1()'s comment — `name` stays the real
    // texture name, `cacheEntryKey` is this class's own internal key.
    // mutex_ is already held by caller (uploadTexture)
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    auto payloadFmt = static_cast<EQT::Graphics::EQE1PayloadFormat>(entry.payloadFormat);

    // Budget check — use the uncompressed payload's actual byte size, not ETC1's.
    size_t gpuSize = EQT::Graphics::payloadMipChainSize(entry.width, entry.height,
                                                         entry.mipLevels, payloadFmt);
    if (!evictUntilAvailable(gpuSize)) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: budget exhausted uploading v2 payload '{}' (need {} bytes)",
                 name, gpuSize);
        return nullptr;
    }

    unsigned int texId = 0;
    if (!createV2Storage(name, entry, texId)) {
        return nullptr;
    }

    // P46d: cacheEntryKey, not `name` — see uploadCompressedETC1()'s identical fix.
    irr::video::ITexture* texture = static_cast<irr::video::ITexture*>(
        gles2WrapTexture(driver_, cacheEntryKey.c_str(), texId,
                         entry.width, entry.height,
                         static_cast<unsigned int>(gpuSize)));

    if (!texture) {
        glDeleteTextures(1, &texId);
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: wrapTexture failed for v2 payload '{}'", name);
        return nullptr;
    }

    gles2TextureSetStorageBytes(texture, v2StorageBytes(entry));  // B962

    // Register in LRU cache (P46d: under cacheEntryKey). No alpha companion
    // (isETC1=false → H02d routes this texture's alpha-test material, if
    // any, to the main-alpha RGBA shader variant which reads uTexture.a
    // directly).
    lruOrder_.push_back(cacheEntryKey);
    CachedTexture cached;
    cached.texture = texture;
    cached.alphaTexture = nullptr;
    cached.sizeBytes = gpuSize;
    cached.hasAlpha = entry.hasAlpha;
    cached.isETC1 = false;
    cached.lruIterator = std::prev(lruOrder_.end());
    cached.source = CachedTexture::Source::Compressed;  // B944: restore source
    cached.compressed = entry;

    static const char* kPayloadNames[] = {"etc1", "rgb565", "rgba4444", "rgba8888"};
    if (ledgerEnabled_) {
        std::string ledgerCategory = "uncategorized";
        std::string ledgerCallsite;
        auto tagIt = pendingLedgerTag_.find(cacheEntryKey);
        if (tagIt != pendingLedgerTag_.end()) {
            ledgerCategory = tagIt->second.first;
            ledgerCallsite = tagIt->second.second;
            pendingLedgerTag_.erase(tagIt);
        }
        cached.category = ledgerCategory;
        const char* fmtName = (entry.payloadFormat < 4) ? kPayloadNames[entry.payloadFormat] : "v2?";
        ledger_.recordUpload(name, entry.width, entry.height, gpuSize, fmtName,
                              ledgerCategory, ledgerCallsite);
    }

    cache_[cacheEntryKey] = cached;
    currentUsage_ += gpuSize;
    ++compressedUploadCount_;

    if (meshBuilder_) {
        meshBuilder_->registerUploadedTexture(name, texture, entry.hasAlpha);
    }

    const char* payloadName = (entry.payloadFormat < 4) ? kPayloadNames[entry.payloadFormat] : "?";
    LOG_DEBUG(MOD_GRAPHICS, "  [CONSTRAINED] v2 texture '{}' uploaded ({}x{}, {} mips, {} bytes, {}{})",
              name, entry.width, entry.height, entry.mipLevels, gpuSize, payloadName,
              entry.hasAlpha ? ", +alpha(in-band)" : "");
    return texture;
#else
    (void)name;
    (void)cacheEntryKey;
    (void)entry;
    LOG_WARN(MOD_GRAPHICS, "Constrained cache: v2 payload upload not available (non-GLES2 build) for '{}'", name);
    return nullptr;
#endif
}

// B944: the GL half of uploadCompressedV2(), shared with the eviction restore
// (see createETC1Storage()). Leaves GL_TEXTURE_2D on the active unit bound to 0.
bool ConstrainedTextureCache::createV2Storage(const std::string& name, const CompressedEntry& entry,
                                              unsigned int& texIdOut) {
    texIdOut = 0;
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    auto payloadFmt = static_cast<EQT::Graphics::EQE1PayloadFormat>(entry.payloadFormat);

    GLenum glFormat = GL_RGBA;
    GLenum glType = GL_UNSIGNED_BYTE;
    switch (payloadFmt) {
        case EQT::Graphics::EQE1_PAYLOAD_RGB565:
            glFormat = GL_RGB;
            glType = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case EQT::Graphics::EQE1_PAYLOAD_RGBA4444:
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_SHORT_4_4_4_4;
            break;
        case EQT::Graphics::EQE1_PAYLOAD_RGBA8888:
        default:
            glFormat = GL_RGBA;
            glType = GL_UNSIGNED_BYTE;
            break;
    }

    GLuint texId = 0;
    glGenTextures(1, &texId);
    if (texId == 0) {
        LOG_ERROR(MOD_GRAPHICS, "Constrained cache: glGenTextures failed for '{}'", name);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texId);
    eqt::memory::GemTag gemTag("tex:v2", name.c_str(), entry.width, entry.height, entry.mipLevels);

    // B962: sampled through GL_LINEAR only (see createETC1Storage()), so the
    // filter goes first and only level 0 is uploaded.
    const bool legacyLevels = gpuBuffersLegacy();
    if (!legacyLevels) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    const uint8_t levels = legacyLevels ? entry.mipLevels : std::min<uint8_t>(entry.mipLevels, 1);

    // Upload uncompressed mip chain via glTexImage2D (not compressed — no
    // glCompressedTexImage2D indirection or function-pointer lookup needed).
    uint32_t offset = 0;
    uint16_t w = entry.width, h = entry.height;
    for (uint8_t level = 0; level < levels; ++level) {
        uint32_t levelSize = EQT::Graphics::payloadDataSize(w, h, payloadFmt);
        glTexImage2D(GL_TEXTURE_2D, level, glFormat, w, h, 0, glFormat, glType,
                     entry.etc1Data + offset);
        offset += levelSize;
        w = std::max<uint16_t>(1, w / 2);
        h = std::max<uint16_t>(1, h / 2);
    }

    // Same filtering policy as the main ETC1 texture unit (unit 0) — v2
    // payloads have no companion, so there is only ever a "main" unit here.
    // T275: plain GL_LINEAR, for the mipmap-completeness reason spelled out
    // in uploadCompressedETC1() above — these chains are generated by the
    // same builder and stop just as short of 1x1.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);

    texIdOut = texId;
    return true;
#else
    (void)name;
    (void)entry;
    return false;
#endif
}

} // namespace Graphics
} // namespace EQT
