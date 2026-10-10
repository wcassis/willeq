// constrained_texture_cache.cpp — ConstrainedTextureCache: the live load/upload
// path and the public surface. Cache keys, construction/driver wiring,
// getOrLoad()/uploadTexture()/uploadAllPending(), registration, queries,
// statistics, the ledger dump, freeze/unfreeze and the setters.
// Split from the 2,211-line original by refactor/J10_constrained_texture_cache_split.md.
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/archive_texture_source.h"
#include "common/logging.h"
#include "client/core/memory_ledger.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <fstream>
#include <cstdlib>
#include <limits>

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
// B949 (GitLab #267): the driver frame in which `texture` was last bound
// (0 = never drawn). endFrame() reorders the LRU list by these stamps.
extern unsigned int gles2TextureGetDrawFrameStamp(void* texture);
#endif

namespace EQT {
namespace Graphics {

// P46d: fixes the cache-key collision root-caused in
// refactor/P46c_neutral_pair_stop.md — getOrLoad()/uploadTexture() used to key
// cache_/decodedEntries_/compressedEntries_ on texture name ALONE, so
// whichever caller decoded a name FIRST (masked or not) won that name's slot
// for the process lifetime; any later request for the other variant silently
// got the wrong (already-cached) pixels. cacheKey() gives the masked variant
// a distinct internal key so both can coexist. applyPaletteMask=false (every
// call site except race_model_loader.cpp's T05 loop and
// equipment_model_loader.cpp's per-triangle-group load, the only two that
// compute a WLD-derived masked flag) maps to the identity function, so this
// is a no-op for every other caller/texture class — in particular zone
// geometry (zone_geometry.cpp's own getOrLoad call site never passes true)
// stays byte-identical.
std::string ConstrainedTextureCache::cacheKey(const std::string& name, bool applyPaletteMask) {
    if (!applyPaletteMask) {
        return name;
    }
    // \x01 cannot appear in an EQ texture filename or an EQE1 flat-pack entry
    // name (both are plain ASCII filenames), so this can never collide with a
    // real name, masked or not.
    return name + "\x01masked";
}

void ConstrainedTextureCache::splitCacheKey(const std::string& key, std::string& realName,
                                             bool& applyPaletteMask) {
    static const std::string kSuffix = "\x01masked";
    if (key.size() > kSuffix.size() &&
        key.compare(key.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
        realName = key.substr(0, key.size() - kSuffix.size());
        applyPaletteMask = true;
    } else {
        realName = key;
        applyPaletteMask = false;
    }
}

ConstrainedTextureCache::ConstrainedTextureCache(const ConstrainedRendererConfig& config)
    : config_(config)
{
    // B944 (GitLab #262): a TEST knob, never set by any shipped path -- force
    // the eviction ceiling low under the real preset so eviction can be
    // exercised live (refactor/B944/evict_client_capture.sh). Loud on purpose.
    if (const char* forced = std::getenv("WILLEQ_TEXTURE_MEMORY_BYTES")) {
        const unsigned long long bytes = std::strtoull(forced, nullptr, 10);
        if (bytes > 0) {
            LOG_WARN(MOD_GRAPHICS, "Constrained cache: WILLEQ_TEXTURE_MEMORY_BYTES={} overrides the "
                     "preset's textureMemoryBytes {} (test knob)", bytes, config_.textureMemoryBytes);
            config_.textureMemoryBytes = static_cast<size_t>(bytes);
        }
    }
}

void ConstrainedTextureCache::setDriver(irr::video::IVideoDriver* driver) {
    driver_ = driver;
    probeCompressedTextureSupport();
    // P13: ledger is enabled purely by config (non-empty output path) — see
    // ConstrainedRendererConfig::textureUploadLedgerPath.
    if (!config_.textureUploadLedgerPath.empty()) {
        ledgerEnabled_ = true;
    }
}

ConstrainedTextureCache::~ConstrainedTextureCache() {
    // T214: final dump, so the audit covers requests made AFTER zone-load
    // completes -- the distance gate defers entities (T210), and a
    // zone-load-complete snapshot cannot see what they ask for on approach.
    if (const char* dir = std::getenv("EQT_EQE1_REQUEST_LOG_DIR")) {
        dumpEQE1RequestLog(std::string(dir) + "/eqe1_requests_FINAL.tsv");
    }
    clear();
    // B944: deliberately NOTHING here touches what clear() preserved (the
    // protected entries). At client shutdown the renderer has already dropped
    // the Irrlicht device, and with it every texture object -- the first
    // version of this unit cleared their residency back-pointers here and the
    // ASan client read freed memory doing it. None of them needs it: a
    // protected entry is never evicted (evictUntilAvailable() skips it), so it
    // never carries a residency owner.
}

// ---------------------------------------------------------------------------
// Internal helpers (called while holding mutex_)
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::touchInternal(const std::string& name) {
    auto it = cache_.find(name);
    // B944: an evicted entry is not in lruOrder_ (its iterator is stale); it
    // re-enters the list when its storage is restored on first bind.
    if (it != cache_.end() && it->second.resident) {
        lruOrder_.erase(it->second.lruIterator);
        lruOrder_.push_back(name);
        it->second.lruIterator = std::prev(lruOrder_.end());
    }
}

irr::video::ITexture* ConstrainedTextureCache::getPlaceholderTextureInternal() {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDriver);  // T491
    if (placeholderTexture_) {
        return placeholderTexture_;
    }

    // Create a small checkerboard placeholder texture (8x8 magenta/black)
    const int size = 8;
    std::vector<uint32_t> pixels(size * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Checkerboard pattern: magenta and dark gray
            bool checker = ((x / 2) + (y / 2)) % 2 == 0;
            pixels[y * size + x] = checker ? 0xFFFF00FF : 0xFF202020;  // ARGB
        }
    }

    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8,
        irr::core::dimension2d<irr::u32>(size, size),
        pixels.data(), false);

    if (image) {
        placeholderTexture_ = driver_->addTexture("_placeholder_", image);
        image->drop();
        if (ledgerEnabled_ && placeholderTexture_) {
            ledger_.recordUpload("_placeholder_", size, size, calculateTextureSize(size, size),
                                  "RGBA8888", "placeholder", "getPlaceholderTextureInternal");
        }
    }

    return placeholderTexture_;
}

// ---------------------------------------------------------------------------
// Public methods — all acquire mutex_ (except queueDecoded/queueDecodedARGB
// which only use decodedQueueMutex_)
// ---------------------------------------------------------------------------

irr::video::ITexture* ConstrainedTextureCache::getOrLoad(const std::string& name,
                                                          const std::vector<char>& data,
                                                          const std::string& category,
                                                          const std::string& callsite,
                                                          bool applyPaletteMask,
                                                          const TextureOrigin* origin) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDecoded);  // T491
    std::lock_guard<std::mutex> lock(mutex_);

    // P46d: masked and unmasked variants of the same name are distinct cache
    // entries (see cacheKey()) — internalKey is identity when
    // applyPaletteMask is false, so every non-masked call site (the vast
    // majority) is completely unaffected.
    const std::string internalKey = cacheKey(name, applyPaletteMask);

    // Check if already uploaded
    auto cacheIt = cache_.find(internalKey);
    if (cacheIt != cache_.end()) {
        touchInternal(internalKey);
        ++cacheHits_;
        return cacheIt->second.texture;
    }

    // Check if already in compressed entries (from EQE1 lookup)
    if (compressedEntries_.find(internalKey) != compressedEntries_.end()) {
        ++cacheHits_;
        return nullptr;  // Already found in EQE1, call uploadTexture() to promote
    }

    // Check if already decoded (waiting for upload)
    if (decodedEntries_.find(internalKey) != decodedEntries_.end()) {
        ++cacheHits_;
        return nullptr;  // Already decoded, call uploadTexture() to promote
    }

    ++cacheMisses_;

    // P13: record the caller's category/callsite tag for this name so the
    // ledger row emitted at actual upload time (uploadTexture()/
    // uploadCompressed*()) can carry it. This covers all three upload
    // sub-paths (RGBA decode, ETC1, v2) uniformly since it's set before any
    // of the branches below. No-op when the ledger is disabled.
    if (ledgerEnabled_) {
        pendingLedgerTag_[internalKey] = {category, callsite};
    }

    // Debug: skip all texture uploads to isolate endScene regression
    if (config_.skipConstrainedTextureUpload) {
        return nullptr;
    }

    // Check EQE1 flat packs for pre-compressed ETC1 data (no decode needed).
    // P46d: looked up by the REAL name (findCompressedEntry strips/lowercases
    // it to match the flat pack's own entry names) — applyPaletteMask is
    // irrelevant to the ETC1/EQE1 path (its alpha comes from a companion
    // texture built offline by eqe1_builder, never from this class's
    // decodeBMP palette-index-0 punch), but the result is still stored under
    // internalKey so a masked-requested name doesn't collide with an
    // unmasked-requested one in compressedEntries_/cache_ bookkeeping. Any
    // resulting duplication (both variants independently finding + uploading
    // the same EQE1 payload) is a bounded, expected cost — see this unit's
    // Review (only WLD-masked textures, dozens not thousands, ever request
    // the masked variant).
    auto* compressed = findCompressedEntry(name);
    if (compressed) {
        compressedEntries_[internalKey] = *compressed;
        LOG_TRACE(MOD_GRAPHICS, "  [CONSTRAINED] texture '{}' EQE1 hit ({}x{}, {} mips{}){}",
                  name, compressed->width, compressed->height, compressed->mipLevels,
                  compressed->hasAlpha ? ", alpha" : "",
                  applyPaletteMask ? " [masked variant]" : "");
        return nullptr;  // Compressed, call uploadTexture() to promote
    }

    // EQE1 is configured but texture not found — fatal error.
    // All textures should be in the EQE1 files. A miss means the EQE1 directory
    // is incomplete or the wrong path was specified.
    if (!snapshotEQE1Loaders().empty()) {
        // B930: before the generic asset-gap FATAL, check for the STALE-PACK
        // signature — a name longer than the 31-char inline field that a v1/v2
        // pack holds truncated (its 31-char prefix as a genuine entry key).
        // That is a different defect than "the directory is missing this
        // texture": the pack IS present and IS open, it was just built by the
        // pre-B910 builder that silently strncpy'd the name to 31. soldunga
        // died here with the generic message (which names a texture, not the
        // cause), #248. The diagnosis is pure (diagnoseStaleEQE1Pack) so the
        // RED/GREEN test exercises it without a GL context.
        const auto diag = diagnoseStaleEQE1Pack(snapshotEQE1Loaders(), name);
        if (diag.stalePack) {
            LOG_FATAL(MOD_GRAPHICS,
                      "EQE1 pack '{}' is format v{}; this client needs v3 "
                      "(rebuild with tools/convert_all_to_eqe1.sh). "
                      "The pack's inline name field caps at {} chars, so it "
                      "holds '{}' but not '{}'; that truncation is why the "
                      "texture miss above is not a plain asset gap. Every "
                      "name in a rebuilt pack either fits the field or goes "
                      "to the v3 name table.",
                      diag.packPath, diag.packVersion,
                      static_cast<unsigned>(EQE1_INLINE_NAME_LIMIT),
                      diag.truncatedName, name);
        } else {
            LOG_FATAL(MOD_GRAPHICS, "Texture '{}' not found in any EQE1 flat pack. "
                      "The --eqe1-path directory is missing this texture. "
                      "Re-run eqe1_builder to regenerate, or remove --eqe1-path to use S3D textures.",
                      name);
        }
        std::exit(1);
    }

    // Decode raw bytes to RGBA (no GL calls) — BMP/DDS path (no EQE1 configured)
    std::vector<uint8_t> rgbaPixels;
    int width = 0, height = 0;
    bool hasAlpha = false;

    // T92: distinguish "the caller never supplied any bytes" from "we were
    // given real bytes and the decoder rejected them" — these are different
    // defects with different fixes, and the old single "decode failed"
    // message conflated them, sending a prior investigation upstream to
    // decodeBMP/DDSDecoder when the actual bug was a caller (see
    // race_model_loader.cpp's composite-sheet lookup, this unit's Review)
    // passing an intentionally empty buffer expecting an EQE1 flat-pack hit
    // that never came because no --eqe1-path was configured for the run.
    if (data.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: empty input data for '{}' — no bytes were "
                  "supplied to decode (not a decoder failure; check the caller: an EQE1-only "
                  "lookup with no EQE1 archive configured, a missing/mis-keyed S3D archive "
                  "entry, or a wrongly-constructed texture name)", name);
        return nullptr;
    }
    if (!processTextureData(data, rgbaPixels, width, height, hasAlpha, applyPaletteMask)) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: decode failed for '{}' ({} bytes supplied)", name, data.size());
        return nullptr;
    }

    // Store as decoded entry
    DecodedEntry decoded;
    decoded.rgbaPixels = std::move(rgbaPixels);
    decoded.width = width;
    decoded.height = height;
    decoded.hasAlpha = hasAlpha;
    // B947: the restore identity of these bytes (see TextureOrigin).
    if (origin && origin->known()) decoded.origin = *origin;
    decoded.rawDigest = rawBytesDigest(data);
    decoded.rawSize = data.size();
    decodedEntries_[internalKey] = std::move(decoded);

    LOG_DEBUG(MOD_GRAPHICS, "  [CONSTRAINED] texture '{}' decoded ({}x{}, alpha={}{})",
              name, width, height, hasAlpha, applyPaletteMask ? ", masked variant" : "");
    return nullptr;  // Decoded but not uploaded — call uploadTexture() to promote
}

irr::video::ITexture* ConstrainedTextureCache::uploadTexture(const std::string& name, bool applyPaletteMask) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDecoded);  // T491
    std::lock_guard<std::mutex> lock(mutex_);

    // P46d: internalKey is the same (name, applyPaletteMask) key getOrLoad()
    // used to store this entry (see cacheKey()) — identity when
    // applyPaletteMask is false. `name` itself is kept around, unmangled, for
    // everything that must stay name-keyed by a THIRD party outside this
    // class's own bookkeeping maps: the GPU texture's Irrlicht debug label,
    // the P13 ledger row's texture-name column, and
    // ZoneMeshBuilder::registerUploadedTexture() (its own textureCache_
    // fast-path map, keyed by plain name exactly as before this fix — see
    // this unit's Review for why that map does not also need mask-awareness).
    const std::string internalKey = cacheKey(name, applyPaletteMask);

    // Already uploaded?
    auto cacheIt = cache_.find(internalKey);
    if (cacheIt != cache_.end()) {
        touchInternal(internalKey);
        return cacheIt->second.texture;
    }

    // Check for compressed/v2 entry (from EQE1 flat pack)
    auto compressedIt = compressedEntries_.find(internalKey);
    if (compressedIt != compressedEntries_.end()) {
        const auto& entry = compressedIt->second;
        irr::video::ITexture* result = (entry.payloadFormat == EQT::Graphics::EQE1_PAYLOAD_ETC1)
            ? uploadCompressedETC1(name, internalKey, entry)
            : uploadCompressedV2(name, internalKey, entry);
        compressedEntries_.erase(compressedIt);
        return result;
    }

    // Find decoded entry
    auto decodedIt = decodedEntries_.find(internalKey);
    if (decodedIt == decodedEntries_.end()) {
        return nullptr;  // Not decoded
    }

    auto& entry = decodedIt->second;

    // Budget check
    size_t textureSize = calculateTextureSize(entry.width, entry.height);
    if (!evictUntilAvailable(textureSize)) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: budget exhausted uploading '{}' (need {} bytes, budget {} bytes, used {} bytes)",
            name, textureSize, config_.textureMemoryBytes, currentUsage_);
        decodedEntries_.erase(decodedIt);
        return nullptr;
    }

    // P46d: MUST use internalKey (not the plain `name`), not just for this
    // class's own bookkeeping: COpenGLES2Driver::addTexture() itself
    // deduplicates by name (`findTexture(name)`; if a texture with that name
    // already exists, it returns the EXISTING texture UNCHANGED, silently
    // discarding the new image data). Passing the plain name for both the
    // unmasked and masked variant made the masked upload a no-op that
    // returned the SAME (unmasked) GPU texture — caught by this unit's own
    // discard-fire re-verification, which found identical pointers for both
    // uploads until this fix.
    irr::video::ITexture* texture =
        createDecodedTexture(internalKey, entry.rgbaPixels, entry.width, entry.height);

    bool hasAlpha = entry.hasAlpha;
    int width = entry.width;
    int height = entry.height;
    TextureOrigin origin = std::move(entry.origin);  // B947
    const uint64_t rawDigest = entry.rawDigest;
    const size_t rawSize = entry.rawSize;

    // Remove decoded entry regardless of upload success (P46d: keyed by internalKey)
    decodedEntries_.erase(decodedIt);

    if (!texture) {
        LOG_WARN(MOD_GRAPHICS, "Constrained cache: addTexture failed for '{}' ({}x{})", name, width, height);
        return nullptr;
    }

    // Register in LRU cache (P46d: under internalKey — see cacheKey())
    lruOrder_.push_back(internalKey);
    CachedTexture cached;
    cached.texture = texture;
    cached.sizeBytes = textureSize;
    cached.hasAlpha = hasAlpha;
    cached.lruIterator = std::prev(lruOrder_.end());
    // B944: an eviction can restore this from a registered
    // TextureSourceProvider (the raw bytes are the caller's and are gone).
    // B947: or from the origin the caller named, checked against the digest.
    cached.source = CachedTexture::Source::Decoded;
    cached.origin = std::move(origin);
    cached.rawDigest = rawDigest;
    cached.rawSize = rawSize;

    // P13: consume this name's pending ledger tag (set by getOrLoad) and
    // record the upload row. No-op when disabled.
    if (ledgerEnabled_) {
        std::string ledgerCategory = "uncategorized";
        std::string ledgerCallsite;
        auto tagIt = pendingLedgerTag_.find(internalKey);
        if (tagIt != pendingLedgerTag_.end()) {
            ledgerCategory = tagIt->second.first;
            ledgerCallsite = tagIt->second.second;
            pendingLedgerTag_.erase(tagIt);
        }
        cached.category = ledgerCategory;
        ledger_.recordUpload(name, width, height, textureSize, "RGBA8888",
                              ledgerCategory, ledgerCallsite);
    }

    cache_[internalKey] = cached;
    currentUsage_ += textureSize;

    // Also register in mesh builder for entity texture lookups. P46d:
    // deliberately keyed by the REAL `name` (not internalKey) — this is
    // ZoneMeshBuilder's OWN fast-path cache (textureCache_, zone_geometry.h),
    // consulted by name only (mesh_building.cpp's getOrLoadTexture()) with no
    // mask parameter of its own. Registering under a mangled key here would
    // make that fast path always miss for masked entries, falling through to
    // ZoneMeshBuilder::loadTextureFromBMP() — which calls getOrLoad() with NO
    // applyPaletteMask argument (defaults false) — silently re-decoding the
    // UNMASKED variant and defeating this whole fix for the exact path P46c
    // demonstrated the bug on. Registering under the plain name instead
    // relies on production's existing, verified-safe ordering invariant (see
    // this unit's Review, part B): the masked T05 decode always runs before
    // buildMeshFromGeometry's lookup for the SAME race, and EQ's texture
    // naming (race-code-prefixed) means no static-only race's texture name
    // ever collides with an animated masked race's — so last-write-wins here
    // is safe in practice, exactly as it was (silently) before this fix.
    if (meshBuilder_) {
        meshBuilder_->registerUploadedTexture(name, texture, hasAlpha);
    }

    LOG_DEBUG(MOD_GRAPHICS, "  [CONSTRAINED] texture '{}' uploaded ({}x{}, {} bytes, alpha={}{})",
              name, width, height, textureSize, hasAlpha, applyPaletteMask ? ", masked variant" : "");
    return texture;
}

// B944: the driver half of uploadTexture()'s decode path, shared with the
// eviction restore so a restored texture goes through the SAME conversion,
// upload and filter setup as the first one. `glName` is the driver-side name
// (addTexture dedups by it, see the P46d note in uploadTexture()).
irr::video::ITexture* ConstrainedTextureCache::createDecodedTexture(const std::string& glName,
                                                                    const std::vector<uint8_t>& rgbaPixels,
                                                                    int width, int height) {
    // Convert RGBA → ARGB (Irrlicht's native format)
    std::vector<uint8_t> argbData(rgbaPixels.size());
    for (size_t i = 0; i < rgbaPixels.size(); i += 4) {
        argbData[i + 0] = rgbaPixels[i + 2];  // B
        argbData[i + 1] = rgbaPixels[i + 1];  // G
        argbData[i + 2] = rgbaPixels[i + 0];  // R
        argbData[i + 3] = rgbaPixels[i + 3];  // A
    }

    irr::core::dimension2d<irr::u32> size(width, height);
    // T491: from here the bytes are the driver's (IImage copy, COGLES2Texture
    // and its mip/lock buffers), not the cache's decoded entry.
    eqt::memory::MemoryScope driverScope(eqt::memory::Category::TexturesDriver);
    irr::video::IImage* image = driver_->createImageFromData(
        irr::video::ECF_A8R8G8B8, size, argbData.data(), false);

    irr::video::ITexture* texture = nullptr;
    if (image) {
        texture = driver_->addTexture(glName.c_str(), image);
        image->drop();
    }
    if (!texture) {
        return nullptr;
    }

#ifdef EQT_HAS_DRM
    // Set bilinear filtering directly on GL texture (Lima/Mali400 workaround).
    // P42 (P38 finding, refactor/perf_truth_audit.md): this used to branch on
    // config_.enableMipmaps to request GL_LINEAR_MIPMAP_LINEAR here, but
    // COpenGLES2Driver::applyMaterialRenderState() unconditionally
    // overwrites MIN_FILTER to plain GL_LINEAR/GL_NEAREST on every single
    // draw before this texture is ever sampled -- the mipmap filter mode
    // set here was always stomped before use, a total no-op (and, while
    // live, requested filtering against an incomplete mip chain -- no code
    // path here ever called glGenerateMipmap). Simplified to the filter
    // that actually took effect; no behavior change, this branch never
    // affected rendering.
    {
        irr::u32 glTexName = texture->getDriverTextureHandle();
        if (glTexName != 0) {
            glBindTexture(GL_TEXTURE_2D, glTexName);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
#endif
    return texture;
}

int ConstrainedTextureCache::uploadAllPending() {
    return uploadPending(std::numeric_limits<int>::max());
}

size_t ConstrainedTextureCache::pendingUploadCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return decodedEntries_.size() + compressedEntries_.size();
}

int ConstrainedTextureCache::uploadPending(int maxCount) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDecoded);  // T491
    // Collect internal keys first (uploadTexture modifies decodedEntries_ and
    // compressedEntries_). P46d: these keys may already be mangled (see
    // cacheKey()) — splitCacheKey() recovers the real display name and
    // applyPaletteMask flag from each before calling uploadTexture(), so it
    // both (a) recomputes the identical internal key internally (cacheKey()
    // is the exact inverse) and (b) passes the REAL name through for
    // uploadTexture()'s display-name uses (GL debug label, ledger row,
    // ZoneMeshBuilder::registerUploadedTexture()). Passing the raw
    // (possibly-mangled) key straight through as `name` — this loop's
    // original approach — silently registered masked entries into
    // ZoneMeshBuilder's own name-keyed fast-path cache under the MANGLED
    // name, which mesh_building.cpp's plain-name lookup never finds; caught
    // by this unit's own discard-fire re-verification (see Review).
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        names.reserve(decodedEntries_.size() + compressedEntries_.size());
        for (const auto& [name, entry] : compressedEntries_) {
            names.push_back(name);
        }
        for (const auto& [name, entry] : decodedEntries_) {
            names.push_back(name);
        }
    }

    int uploaded = 0;
    int attempted = 0;
    for (const auto& key : names) {
        if (attempted >= maxCount) break;
        ++attempted;
        std::string realName;
        bool applyPaletteMask = false;
        splitCacheKey(key, realName, applyPaletteMask);
        if (uploadTexture(realName, applyPaletteMask)) {
            ++uploaded;
        }
    }
    if (maxCount == std::numeric_limits<int>::max()) {
        LOG_INFO(MOD_GRAPHICS, "Constrained cache: uploadAllPending uploaded {} textures ({} were pending)",
                 uploaded, names.size());
    }
    return uploaded;
}

void ConstrainedTextureCache::touch(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    touchInternal(name);
}

// ---------------------------------------------------------------------------
// B949 (GitLab #267): LRU by draw order
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::endFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef EQT_EQE1_UPLOAD_AVAILABLE
    // Reorder the resident list by "last drawn in frame N", ascending:
    // front = least recently drawn = the eviction candidate. The stamps the
    // driver records on every bind are frame numbers, so equal stamps (two
    // textures drawn in the same frame, or several never drawn) must keep
    // their old relative order: a texture that was only looked up (never
    // bound) or was just uploaded sits exactly where load/touch order put it
    // relative to its equal-stamped peers, and the first endFrame() of a
    // zone is the identity permutation (every stamp is 0). A freshly
    // RESTORED entry was pushed_back by restoreEvicted(), which is
    // consistent: it was just drawn (its bind restored it).
    //
    // Mechanism: compute the stable order over the names, then move the
    // EXISTING nodes into that order with splice(). A spliced node keeps its
    // address, so every entry's lruIterator into lruOrder_ stays valid.
    // (lruOrder_.assign() -- the first draft of this -- rebuilt the node
    // set and left every lruIterator dangling; evictTexture()/
    // releaseStorage() erase() through them, and the gate run spun forever
    // in evictUntilAvailable() on the corrupted list.)
    std::vector<std::string> order(lruOrder_.begin(), lruOrder_.end());
    std::stable_sort(order.begin(), order.end(), [this](const std::string& a,
                                                        const std::string& b) {
        const CachedTexture& ea = cache_.at(a);
        const CachedTexture& eb = cache_.at(b);
        const unsigned int stampA = gles2TextureGetDrawFrameStamp(ea.texture);
        const unsigned int stampB = gles2TextureGetDrawFrameStamp(eb.texture);
        return stampA < stampB;
    });
    std::list<std::string> sorted;
    std::map<std::string, std::list<std::string>::iterator> node;
    for (auto lit = lruOrder_.begin(); lit != lruOrder_.end(); ++lit) {
        node[*lit] = lit;
    }
    for (const std::string& name : order) {
        sorted.splice(sorted.end(), lruOrder_, node[name]);
    }
    lruOrder_.swap(sorted);
#else
    // EDT_NULL and other non-GLES2 drivers carry no draw stamps: the list
    // stays in load order, exactly the pre-B949 behaviour (the unit tests'
    // only driver).
    (void)lruOrder_;
#endif
}

bool ConstrainedTextureCache::hasTexture(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(name) != cache_.end();
}

irr::video::ITexture* ConstrainedTextureCache::getTexture(const std::string& name, bool applyPaletteMask) {
    std::lock_guard<std::mutex> lock(mutex_);
    // P50: internalKey mirrors getOrLoad()/uploadTexture() — identity when
    // applyPaletteMask is false, so every existing (unmasked) call site is
    // byte-identical to before this parameter existed.
    const std::string internalKey = cacheKey(name, applyPaletteMask);
    auto it = cache_.find(internalKey);
    if (it != cache_.end()) {
        touchInternal(internalKey);
        return it->second.texture;
    }
    return nullptr;
}

bool ConstrainedTextureCache::registerTexture(const std::string& name,
                                               irr::video::ITexture* texture,
                                               size_t sizeBytes, bool hasAlphaFlag,
                                               const std::string& category) {
    if (!texture) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    // Already in cache — check if same texture (just touch) or replacement
    auto it = cache_.find(name);
    if (it != cache_.end()) {
        if (it->second.texture == texture) {
            touchInternal(name);
            return true;
        }
        // Different texture under same name — destroy the old one first
        // (B944: a REPLACEMENT, so the old object really goes; evictTexture()
        // would only release its storage and keep it under this key).
        destroyEntry(name);
    }

    // Too large to ever fit
    if (sizeBytes > config_.textureMemoryBytes) {
        return false;
    }

    // Evict textures if needed to make room
    if (!evictUntilAvailable(sizeBytes)) {
        return false;
    }

    // Add to LRU cache
    lruOrder_.push_back(name);
    CachedTexture entry;
    entry.texture = texture;
    entry.sizeBytes = sizeBytes;
    entry.hasAlpha = hasAlphaFlag;
    entry.lruIterator = std::prev(lruOrder_.end());

    if (ledgerEnabled_) {
        entry.category = category;
        int w = static_cast<int>(texture->getSize().Width);
        int h = static_cast<int>(texture->getSize().Height);
        ledger_.recordUpload(name, w, h, sizeBytes, "external", category, "registerTexture");
    }

    cache_[name] = entry;
    currentUsage_ += sizeBytes;

    return true;
}

bool ConstrainedTextureCache::hasAlpha(const std::string& name, bool applyPaletteMask) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string internalKey = cacheKey(name, applyPaletteMask);
    auto it = cache_.find(internalKey);
    return it != cache_.end() && it->second.hasAlpha;
}

irr::video::ITexture* ConstrainedTextureCache::getAlphaTexture(const std::string& name, bool applyPaletteMask) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string internalKey = cacheKey(name, applyPaletteMask);
    auto it = cache_.find(internalKey);
    if (it != cache_.end()) return it->second.alphaTexture;
    return nullptr;
}

irr::video::ITexture* ConstrainedTextureCache::getAlphaTexture(irr::video::ITexture* texture) const {
    if (!texture) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [name, entry] : cache_) {
        if (entry.texture == texture) return entry.alphaTexture;
    }
    return nullptr;
}

bool ConstrainedTextureCache::isETC1Texture(const std::string& name, bool applyPaletteMask) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string internalKey = cacheKey(name, applyPaletteMask);
    auto it = cache_.find(internalKey);
    return it != cache_.end() && it->second.isETC1;
}

bool ConstrainedTextureCache::isETC1Texture(irr::video::ITexture* texture) const {
    if (!texture) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [name, entry] : cache_) {
        if (entry.texture == texture) return entry.isETC1;
    }
    return false;
}

bool ConstrainedTextureCache::isLiveTexture(irr::video::ITexture* texture) const {
    if (!texture) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (texture == placeholderTexture_) return true;
    for (const auto& [name, entry] : cache_) {
        if (entry.texture == texture) return true;
    }
    return false;
}

void ConstrainedTextureCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Notify listeners for each texture being cleared
    for (auto& pair : cache_) {
        // T205: a protected texture has an owner outside this cache holding a
        // raw pointer to it; destroying it here is exactly the use-after-free
        // this guard exists to prevent.
        if (protectedTextures_.find(pair.first) != protectedTextures_.end()) {
            continue;
        }
        // T119: clear() is the zone-unload hook and, until this fix, was the
        // one texture-destruction path in this class that did NOT call
        // clearTextureReferences() before driver_->removeTexture() --
        // evictTexture() (the LRU path) always has. SMaterial::setTexture()
        // (Irrlicht SMaterial.h) is a raw, non-owning pointer assignment (no
        // grab/refcount), and driver_->removeTexture() unconditionally
        // drops the texture's sole owning reference (CNullDriver::
        // removeTexture() -> texture->drop()), which deletes the
        // COGLES2Texture the instant its refcount hits zero. Any scene node
        // that survives clear() with a still-live material slot pointing at
        // one of these textures -- e.g. SkyRenderer's dome/sun/moon/glow
        // nodes, which unloadZone() only disables (setVisible(false)), not
        // removes -- is left holding a dangling ITexture* with no protection
        // at all. clearTextureReferences() (used by evictTexture() and,
        // via RegionMeshTextureGuard/its entity-mesh sibling, wired as an
        // eviction *listener* for buffers outside the scene graph) is the
        // exact mechanism that already solves this for eviction; mirroring
        // it here closes the same gap for zone unload without touching
        // ownership/refcounting semantics or GL teardown order.
        //
        // B944: the listeners, the scene scrub and the removal now live in
        // destroyTextures(), shared with destroyEntry(), and cover the ETC1
        // alpha COMPANION too -- clear() used to remove only the main texture,
        // so every zone unload leaked each companion (its GL storage and its
        // driver object) for the life of the process.
        destroyTextures(pair.first, pair.second, /*scrubMeshBuilder=*/false);
    }
    // T205: preserve protected entries across the zone-unload wipe.
    //
    // clear() is the zone-unload hook and destroyed EVERY cached texture,
    // including the UI atlas -- which is not a zone asset: UIAtlas::load()
    // fetches it once from willeq.eqe1 and UIRenderer keeps a RAW pointer to
    // it for the life of the process. On the next zone load, unloadZone() ->
    // clear() -> removeTexture() deleted it, and the following frame drew
    // freed memory: SIGSEGV in COpenGLES2Driver::draw2DImage, with the
    // texture's vtable reading 0x50. Found by watchpoint (T205); the
    // destroying stack was
    //   ITexture::~ITexture <- COGLES2Texture::~COGLES2Texture <- drop()
    //   <- CNullDriver::removeTexture <- ConstrainedTextureCache::clear
    //   <- IrrlichtRenderer::unloadZone <- loadZoneGraphicsOnThread.
    //
    // This is the same hazard T119 documents above for scene nodes that
    // survive clear(); the UI atlas is another owner that survives it, and
    // protectTexture() is how an owner says so. Note the LRU path
    // (evictUntilAvailable) consults protectedTextures_ too -- guarding only
    // that one was T202's mistake, and why pinning appeared to do nothing.
    std::map<std::string, CachedTexture> preserved;
    for (const auto& name : protectedTextures_) {
        auto it = cache_.find(name);
        if (it != cache_.end()) {
            preserved.emplace(it->first, it->second);
        }
    }

    cache_.clear();
    lruOrder_.clear();
    currentUsage_ = 0;
    // B944: every shell left in the map was destroyed above (protected ones
    // are re-registered below with their own entry, never through this map).
    evictedOwner_.clear();
    // B947: the zone's origin archives go with its textures (reopened on the
    // next zone's first restore that needs one).
    originArchives_.reset();
    pinLogged_.clear();

    for (auto& [name, entry] : preserved) {
        if (!entry.resident) {
            // B944: protected AFTER it was evicted -- still a valid object;
            // it re-enters the LRU and the usage when its first bind restores it.
            cache_.emplace(name, entry);
            evictedOwner_[entry.texture] = name;
            if (entry.alphaTexture) evictedOwner_[entry.alphaTexture] = name;
            continue;
        }
        currentUsage_ += entry.sizeBytes;
        lruOrder_.push_back(name);
        // T486 (2026-09-14 audit, item 1): the copied entry still holds its
        // iterator into the list cleared just above -- a dangling node. Every
        // later touch of a protected name (getOrLoad / uploadTexture hit,
        // registerTexture, getTexture, touch()) does lruOrder_.erase() on it.
        // Re-point it at the node just pushed; tests/test_constrained_
        // texture_cache.cpp ProtectedEntriesSurviveClearWithValidLruIterators.
        entry.lruIterator = std::prev(lruOrder_.end());
        cache_.emplace(name, entry);
    }
    if (!preserved.empty()) {
        LOG_INFO(MOD_GRAPHICS, "Constrained cache: preserved {} protected texture(s) "
                 "across zone unload", preserved.size());
    }

    // P17: clear() is the sole zone-unload hook (called once from
    // IrrlichtRenderer::unloadZone(), plus harmlessly from the destructor) --
    // reset the P13 ledger here too so each zone's dump
    // (loadZoneSequential's "zone-in complete" marker) reflects only that
    // zone's own uploads/evictions, not every zone visited earlier in the
    // same process. Segmenting the ledger this way is what actually makes
    // "near-zero evictions post-release" measurable per zone instead of
    // accumulating a session-wide count that would mask the fix. Safe to
    // call unconditionally: TextureUploadLedger::reset() is a no-op-shaped
    // clear of empty vectors when the ledger was never enabled.
    ledger_.reset();
    pendingLedgerTag_.clear();
}

// ---------------------------------------------------------------------------
// Statistics (lock mutex_ for consistent reads)
// ---------------------------------------------------------------------------

size_t ConstrainedTextureCache::getCurrentUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentUsage_;
}

size_t ConstrainedTextureCache::getMemoryLimit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.textureMemoryBytes;
}

size_t ConstrainedTextureCache::getAvailableMemory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.textureMemoryBytes - currentUsage_;
}

size_t ConstrainedTextureCache::getTextureCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

size_t ConstrainedTextureCache::getCacheHits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheHits_;
}

size_t ConstrainedTextureCache::getCacheMisses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheMisses_;
}

size_t ConstrainedTextureCache::getEvictionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return evictionCount_;
}

size_t ConstrainedTextureCache::getCompressedUploadCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return compressedUploadCount_;
}

float ConstrainedTextureCache::getHitRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = cacheHits_ + cacheMisses_;
    if (total == 0) return 0.0f;
    return (static_cast<float>(cacheHits_) / total) * 100.0f;
}

void ConstrainedTextureCache::resetStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    cacheHits_ = 0;
    cacheMisses_ = 0;
    evictionCount_ = 0;
    restoreCount_ = 0;         // B944
    restoreFailureCount_ = 0;
}

bool ConstrainedTextureCache::dumpLedger(const std::string& path, const std::string& runLabel) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ledgerEnabled_) {
        return false;
    }
    bool ok = ledger_.dumpToFile(path, runLabel);
    LOG_INFO(MOD_GRAPHICS, "P13 texture ledger: {} uploads, {} evictions, {} thrash re-uploads -> {} ({})",
             ledger_.uploadCount(), ledger_.evictionCount(), ledger_.thrashCount(), path,
             ok ? "ok" : "FAILED to write");
    return ok;
}

// ---------------------------------------------------------------------------
// Freeze / setters (lock mutex_)
// ---------------------------------------------------------------------------

void ConstrainedTextureCache::freeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = true;
}

void ConstrainedTextureCache::unfreeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = false;
}

bool ConstrainedTextureCache::isFrozen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frozen_;
}

void ConstrainedTextureCache::setSceneManager(irr::scene::ISceneManager* smgr) {
    std::lock_guard<std::mutex> lock(mutex_);
    smgr_ = smgr;
}

void ConstrainedTextureCache::setMeshBuilder(ZoneMeshBuilder* meshBuilder) {
    std::lock_guard<std::mutex> lock(mutex_);
    meshBuilder_ = meshBuilder;
}

} // namespace Graphics
} // namespace EQT
