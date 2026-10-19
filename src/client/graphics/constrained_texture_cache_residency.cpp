// constrained_texture_cache_residency.cpp — B944 (GitLab #262): eviction that
// keeps the texture object.
//
// Before B944, ConstrainedTextureCache::evictTexture() DESTROYED the evicted
// ITexture (driver_->removeTexture()), and every holder of the raw pointer --
// SMaterial texture slots, ZoneMeshBuilder's name map, RaceModelLoader's and
// EquipmentModelLoader's cached meshes, ZoneMergeBuffers' pooled materials --
// was left dangling unless one of the eviction-listener guards happened to
// reach it and swap in the placeholder. gles2_zone_benchmark registered no
// guard at all, so with textureMemoryBytes = 20 MiB its qeynos2 golden run
// read a freed texture's name in ZoneBench::textureGroupId (SIGSEGV). In the
// client the guards turned the same eviction into a permanent magenta/grey
// placeholder for the rest of the zone, and every holder they did not reach
// kept a freed pointer.
//
// Now an eviction releases only the GL storage (the main texture's and its
// ETC1 alpha companion's). The object stays in the driver and in cache_ as a
// non-resident entry; COGLES2Texture::getOpenGLTextureName() -- the accessor
// every bind path goes through -- calls restoreHook() on first use, which
// rebuilds the storage from the entry's source with the same code the first
// upload used and adopts it into the same object.
//
// Sources: an EQE1 entry keeps its CompressedEntry (pointers into the mapped
// pack -- a struct, not pixels). A decode-path entry is re-read from the
// TextureOrigin its caller named (B947: the character/equipment archive
// member, or the data/textures/ file), else by name from a
// TextureSourceProvider (ArchiveTextureSource over the zone's archives), and
// only if the bytes match the first decode's digest. A GLES2 entry with no
// source is PINNED (B947): kept resident and never evicted again. Only a
// texture that is not a GLES2 texture at all -- the EDT_NULL unit tests, which
// pin the eviction-listener guards -- still takes the pre-B944 destroy path.
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/archive_texture_source.h"
#include "client/core/memory_ledger.h"
#include "common/logging.h"
#include <algorithm>

#if defined(EQT_HAS_DRM) || defined(EQT_HAS_GLES2)
#define EQT_EQE1_UPLOAD_AVAILABLE 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
// Defined in COpenGLES2Driver.cpp (docker/irrlicht-drm) -- see
// COGLES2Texture's "Residency" block.
extern void gles2TextureReleaseStorage(void* texture);
extern void gles2TextureAdoptStorage(void* texture, unsigned int glTexName, unsigned int gpuBytes);
extern void gles2TextureSetStorageBytes(void* texture, unsigned int bytes);  // B962
extern bool gles2TextureTransferStorage(void* texture, void* donor);
extern void gles2TextureSetResidencyOwner(void* texture, bool (*restoreFn)(void* ctx, void* texture),
                                          void* ctx);
#endif

namespace EQT {
namespace Graphics {

namespace {

bool isGles2Texture(const irr::video::ITexture* texture) {
    return texture && texture->getDriverType() == irr::video::EDT_OGLES2;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::addSourceProvider(TextureSourceProvider* provider) {
    if (!provider) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(sourceProviders_.begin(), sourceProviders_.end(), provider) == sourceProviders_.end()) {
        sourceProviders_.push_back(provider);
    }
}

void ConstrainedTextureCache::removeSourceProvider(TextureSourceProvider* provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    sourceProviders_.erase(std::remove(sourceProviders_.begin(), sourceProviders_.end(), provider),
                           sourceProviders_.end());
}

size_t ConstrainedTextureCache::getResidentTextureCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& [key, entry] : cache_) {
        if (entry.resident) ++n;
    }
    return n;
}

size_t ConstrainedTextureCache::getRestoreCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return restoreCount_;
}

size_t ConstrainedTextureCache::getRestoreFailureCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return restoreFailureCount_;
}

bool ConstrainedTextureCache::isResident(const std::string& name, bool applyPaletteMask) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // B1083: the callers (tests) query unscoped entries; "" is the identity
    // for the archive axis, exactly as before.
    auto it = cache_.find(cacheKey(name, "", applyPaletteMask));
    return it != cache_.end() && it->second.resident;
}

// ---------------------------------------------------------------------------
// Eviction: release, or destroy (caller holds mutex_)
// ---------------------------------------------------------------------------

bool ConstrainedTextureCache::canReleaseStorage(const std::string& key, CachedTexture& entry) {
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    if (!isGles2Texture(entry.texture) || entry.texture->isRenderTarget()) return false;
    if (entry.alphaTexture && !isGles2Texture(entry.alphaTexture)) return false;
    switch (entry.source) {
        case CachedTexture::Source::Compressed:
            return entry.compressed.etc1Data != nullptr;
        case CachedTexture::Source::Decoded:
            return decodedSourceAvailable(key, entry);
        case CachedTexture::Source::None:
            break;
    }
#else
    (void)key;
    (void)entry;
#endif
    return false;
}

// B947 (GitLab #265): a decode-path entry is restorable only if its exact raw
// bytes can be read again -- from the origin its caller named (an archive
// member or a file: the character/equipment archives, data/textures/), else
// from a name-keyed TextureSourceProvider (the zone archives, B944). The read
// is checked against the size + digest recorded at the first decode, so a
// name that two archives both hold can never restore the other one's pixels.
// Checked on the entry's FIRST eviction and remembered: one extra read per
// evicted texture, and the answer decides between release and pin.
bool ConstrainedTextureCache::decodedSourceAvailable(const std::string& key, CachedTexture& entry) {
    if (!entry.sourceChecked) {
        std::vector<char> bytes;
        {
            eqt::memory::MemoryScope scope(eqt::memory::Category::TexturesDecoded);  // T491
            entry.sourceOk = readDecodedSource(key, entry, bytes);
        }
        entry.sourceChecked = true;
    }
    return entry.sourceOk;
}

bool ConstrainedTextureCache::readDecodedSource(const std::string& key, const CachedTexture& entry,
                                                std::vector<char>& out) {
    auto matches = [&](const std::vector<char>& bytes) {
        return bytes.size() == entry.rawSize && rawBytesDigest(bytes) == entry.rawDigest;
    };
    if (entry.origin.known()) {
        bool read = false;
        if (entry.origin.isArchiveMember()) {
            if (!originArchives_) originArchives_ = std::make_unique<ArchiveTextureSource>();
            read = originArchives_->readMember(*entry.origin.file, entry.origin.member, out);
        } else {
            read = ArchiveTextureSource::readFile(*entry.origin.file, out);
        }
        if (read && matches(out)) return true;
        out.clear();
    }
    std::string realName;
    std::string sourceArchiveStem;
    bool masked = false;
    splitCacheKey(key, realName, sourceArchiveStem, masked);
    for (auto* provider : sourceProviders_) {
        if (provider->hasSource(realName) && provider->readSource(realName, out) && matches(out)) return true;
        out.clear();
    }
    return false;
}

// B947: the fallback for an entry nothing can restore. Before this unit it
// was destroyed (the pre-B944 path) and every holder the eviction guards did
// not reach -- RaceModelLoader's cached meshes above all -- kept a freed
// pointer. Now it stays: resident, counted in the usage, skipped by
// evictUntilAvailable() for the rest of the zone. A texture that can dangle
// does not exist; one that can't be given back is honest about costing.
void ConstrainedTextureCache::pinEntry(const std::string& key, CachedTexture& entry) {
    entry.pinned = true;
    if (pinLogged_.insert(key).second) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: evicted texture '{}' ({} bytes) has no restore source -- "
                 "PINNED (stays resident, never evicted again)", key, entry.sizeBytes);
    }
}

size_t ConstrainedTextureCache::getPinnedTextureCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const auto& [key, entry] : cache_) {
        if (entry.pinned) ++n;
    }
    return n;
}

bool ConstrainedTextureCache::isPinned(const std::string& name, bool applyPaletteMask) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // B1083: "" is the identity for the archive axis (see isResident).
    auto it = cache_.find(cacheKey(name, "", applyPaletteMask));
    return it != cache_.end() && it->second.pinned;
}

void ConstrainedTextureCache::releaseStorage(const std::string& key, CachedTexture& entry) {
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    gles2TextureReleaseStorage(entry.texture);
    gles2TextureSetResidencyOwner(entry.texture, &ConstrainedTextureCache::restoreHook, this);
    evictedOwner_[entry.texture] = key;
    if (entry.alphaTexture) {
        gles2TextureReleaseStorage(entry.alphaTexture);
        gles2TextureSetResidencyOwner(entry.alphaTexture, &ConstrainedTextureCache::restoreHook, this);
        evictedOwner_[entry.alphaTexture] = key;
    }
#endif
    currentUsage_ -= entry.sizeBytes;
    lruOrder_.erase(entry.lruIterator);
    entry.resident = false;
}

void ConstrainedTextureCache::destroyEntry(const std::string& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) return;
    // B1083 dedupe: this entry is the target of every alias pointing at it --
    // they re-fold, or resolve on their own, when they next promote/query.
    dropAliasesTo(key);
    destroyTextures(key, it->second, /*scrubMeshBuilder=*/true);
    if (it->second.resident) {
        currentUsage_ -= it->second.sizeBytes;
        lruOrder_.erase(it->second.lruIterator);
    }
    cache_.erase(it);
}

void ConstrainedTextureCache::destroyTextures(const std::string& key, CachedTexture& entry,
                                              bool scrubMeshBuilder) {
    // Notify listeners before clearing references and removing the textures.
    // The companion gets its own notification: a guard swaps by POINTER, and
    // a material that bound the companion explicitly (setTexture(1, ...)) holds
    // that pointer, not the main one.
    for (irr::video::ITexture* tex : {entry.texture, entry.alphaTexture}) {
        if (!tex) continue;
        const std::string notifiedName = (tex == entry.texture) ? key : key + "_alpha";
        for (auto* listener : evictionListeners_) {
            listener->onTextureEvicted(notifiedName, tex);
        }
    }
    for (irr::video::ITexture* tex : {entry.texture, entry.alphaTexture}) {
        if (!tex) continue;
        // CRITICAL: Clear all references to this texture from mesh materials
        // BEFORE removing it -- prevents a dangling pointer during rendering.
        clearTextureReferences(tex);
        // B944: ZoneMeshBuilder's name -> ITexture* map is filled by this
        // cache (registerUploadedTexture) and was never scrubbed on an
        // eviction, so a later entity build could pick a destroyed texture up
        // from it. NOT from clear(): at zone unload RaceModelLoader::
        // clearMeshCaches() has already emptied the map, and when clear()
        // runs from this cache's destructor the builder (RaceModelLoader's)
        // may already be destroyed -- the benchmark destroys it first, and
        // scrubbing it there was a heap-use-after-free (ASan, entity viewer).
        if (scrubMeshBuilder && meshBuilder_) meshBuilder_->forgetTexture(tex);
        evictedOwner_.erase(tex);
        driver_->removeTexture(tex);
    }
    entry.texture = nullptr;
    entry.alphaTexture = nullptr;
}

// ---------------------------------------------------------------------------
// Restore (driver callback, GL thread)
// ---------------------------------------------------------------------------

bool ConstrainedTextureCache::restoreHook(void* ctx, void* texture) {
    auto* self = static_cast<ConstrainedTextureCache*>(ctx);
    return self && self->restoreEvicted(static_cast<irr::video::ITexture*>(texture));
}

bool ConstrainedTextureCache::restoreEvicted(irr::video::ITexture* texture) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDriver);  // T491
    std::lock_guard<std::mutex> lock(mutex_);
    auto ownerIt = evictedOwner_.find(texture);
    if (ownerIt == evictedOwner_.end()) return false;
    const std::string key = ownerIt->second;
    auto it = cache_.find(key);
    if (it == cache_.end() || it->second.resident) return false;
    CachedTexture& entry = it->second;

    bool ok = evictUntilAvailable(entry.sizeBytes);
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    if (ok) {
        // The restore uploads on the ACTIVE texture unit, in the middle of a
        // bind: put back whatever that unit had bound so the caller's state is
        // exactly what it was (a name an eviction just deleted goes back to 0
        // rather than being re-created by the bind).
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        ok = rebuildStorage(key, entry);
        const GLuint prev = static_cast<GLuint>(previous);
        glBindTexture(GL_TEXTURE_2D, (prev != 0 && glIsTexture(prev)) ? prev : 0);
    }
#else
    ok = false;
#endif
    if (!ok) {
        ++restoreFailureCount_;
        if (restoreFailureLogged_.insert(key).second) {
            LOG_WARN(MOD_GRAPHICS, "Constrained cache: could not restore evicted texture '{}' ({} bytes, "
                     "budget {} bytes, {} resident) -- it draws unbound until a restore succeeds",
                     key, entry.sizeBytes, config_.textureMemoryBytes, currentUsage_);
        }
        return false;
    }

    lruOrder_.push_back(key);
    entry.lruIterator = std::prev(lruOrder_.end());
    entry.resident = true;
    currentUsage_ += entry.sizeBytes;
    evictedOwner_.erase(entry.texture);
    if (entry.alphaTexture) evictedOwner_.erase(entry.alphaTexture);
    ++restoreCount_;
    if (ledgerEnabled_) {
        const auto size = entry.texture->getSize();
        std::string realName;
        std::string sourceArchiveStem;
        bool masked = false;
        splitCacheKey(key, realName, sourceArchiveStem, masked);
        ledger_.recordUpload(realName, static_cast<int>(size.Width), static_cast<int>(size.Height),
                              entry.sizeBytes, "restore", entry.category, "restoreEvicted");
    }
    LOG_DEBUG(MOD_GRAPHICS, "Constrained cache: restored evicted texture '{}' ({} bytes)",
              key, entry.sizeBytes);
    return true;
}

bool ConstrainedTextureCache::rebuildStorage(const std::string& key, CachedTexture& entry) {
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    std::string realName;
    std::string sourceArchiveStem;  // B1083: not consumed here (GL debug label only)
    bool masked = false;
    splitCacheKey(key, realName, sourceArchiveStem, masked);

    if (entry.source == CachedTexture::Source::Compressed) {
        const CompressedEntry& c = entry.compressed;
        if (c.payloadFormat == EQT::Graphics::EQE1_PAYLOAD_ETC1) {
            unsigned int texId = 0, alphaTexId = 0;
            if (!createETC1Storage(realName, c, texId, alphaTexId)) return false;
            // B948 (GitLab #266): same split the first upload now hands
            // gles2WrapTexture() -- the main texture adopts its OWN chain,
            // not the pair entry.sizeBytes carries.
            gles2TextureAdoptStorage(entry.texture, texId,
                static_cast<unsigned int>(
                    EQT::Graphics::etc1MipChainSize(c.width, c.height, c.mipLevels)));
            gles2TextureSetStorageBytes(entry.texture, etc1StorageBytes(c, false));  // B962
            if (entry.alphaTexture && alphaTexId) {
                gles2TextureAdoptStorage(entry.alphaTexture, alphaTexId,
                                         static_cast<unsigned int>(
                                             EQT::Graphics::etc1MipChainSize(
                                                 c.width, c.height, c.mipLevels)));
                gles2TextureSetStorageBytes(entry.alphaTexture, etc1StorageBytes(c, true));  // B962
            } else if (alphaTexId) {
                glDeleteTextures(1, &alphaTexId);
            }
            return true;
        }
        unsigned int texId = 0;
        if (!createV2Storage(realName, c, texId)) return false;
        gles2TextureAdoptStorage(entry.texture, texId, static_cast<unsigned int>(entry.sizeBytes));
        gles2TextureSetStorageBytes(entry.texture, v2StorageBytes(c));  // B962
        return true;
    }

    if (entry.source == CachedTexture::Source::Decoded) {
        std::vector<char> raw;
        {
            eqt::memory::MemoryScope decodeScope(eqt::memory::Category::TexturesDecoded);
            // B947: the same bytes the first decode had, or nothing.
            if (!readDecodedSource(key, entry, raw)) return false;
        }
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        bool hasAlphaDecoded = false;
        {
            eqt::memory::MemoryScope decodeScope(eqt::memory::Category::TexturesDecoded);
            if (!processTextureData(raw, rgba, width, height, hasAlphaDecoded, masked)) return false;
            std::vector<char>().swap(raw);
        }
        const auto size = entry.texture->getSize();
        if (static_cast<irr::u32>(width) != size.Width || static_cast<irr::u32>(height) != size.Height) {
            LOG_WARN(MOD_GRAPHICS, "Constrained cache: restore source for '{}' decodes to {}x{}, the "
                     "texture is {}x{} -- not restored", key, width, height, size.Width, size.Height);
            return false;
        }
        // Upload through the first upload's own path into a throwaway texture,
        // then move its storage into the evicted object.
        static const std::string kDonorName = "\x02" "b944_restore";
        irr::video::ITexture* donor = createDecodedTexture(kDonorName, rgba, width, height);
        if (!donor) return false;
        const bool moved = gles2TextureTransferStorage(entry.texture, donor);
        driver_->removeTexture(donor);
        return moved;
    }
#else
    (void)key;
    (void)entry;
#endif
    return false;
}

}  // namespace Graphics
}  // namespace EQT
