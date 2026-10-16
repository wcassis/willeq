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
#ifdef __APPLE__
// B1043 (#363): macOS has no desktop GL/gl.h on the GLES2 path; ANGLE ships
// the GLES2 headers. (Linux keeps Mesa's GL/gl.h: the FIX card moves every
// GLES2 build to <GLES2/gl2.h>.)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
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
//
// B1083 (GitLab #403): the key gains one more axis, the SOURCE ARCHIVE. Two
// packs of the same zone family ship the same name with different content
// (qeytoqrg.eqe1 tree70 opaque ETC1, qeytoqrg_obj.eqe1 tree70 with an alpha
// companion -- the willow's foliage drew as a solid rectangle because the
// zone pack, opened first in configured order, won the name-keyed lookup for
// the OBJECT's material too). A request that names its source archive gets a
// (stem, name) key, so the two copies coexist side by side: the object
// material keeps its alpha copy, the zone material keeps the opaque one. An
// empty stem (every call site that does not name a source) is the identity,
// so all of P46d and everything that never knew an archive is untouched.
//
// The stem is joined with \x01@ ... \x01 -- control characters never appear
// in an EQ texture name, an archive stem or the masked suffix -- so
// ("tree70", "qeytoqrg_obj") cannot collide with any other (name, stem,
// mask) combination, and splitCacheKey() recovers all three axes.
std::string ConstrainedTextureCache::cacheKey(const std::string& name,
                                               const std::string& sourceArchiveStem,
                                               bool applyPaletteMask) {
    std::string key = name;
    if (!sourceArchiveStem.empty()) {
        key += "\x01@" + sourceArchiveStem + "\x01";
    }
    if (applyPaletteMask) {
        // \x01 cannot appear in an EQ texture filename or an EQE1 flat-pack
        // entry name (both are plain ASCII filenames), so this can never
        // collide with a real name, masked or not.
        key += "\x01masked";
    }
    return key;
}

void ConstrainedTextureCache::splitCacheKey(const std::string& key, std::string& realName,
                                             std::string& sourceArchiveStem,
                                             bool& applyPaletteMask) {
    static const std::string kSuffix = "\x01masked";
    static const std::string kStemOpen = "\x01@";
    std::string work = key;
    if (work.size() > kSuffix.size() &&
        work.compare(work.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
        work = work.substr(0, work.size() - kSuffix.size());
        applyPaletteMask = true;
    } else {
        applyPaletteMask = false;
    }
    const size_t open = work.rfind(kStemOpen);
    if (open != std::string::npos) {
        // The stem cannot contain \x01, so the LAST \x01 of the stem field
        // is its terminator even for a name that itself would never match.
        const size_t close = work.find("\x01", open + kStemOpen.size());
        if (close != std::string::npos && close == work.size() - 1) {
            sourceArchiveStem = work.substr(open + kStemOpen.size(),
                                             close - (open + kStemOpen.size()));
            work = work.substr(0, open);
        }
    }
    realName = work;
}

const ConstrainedTextureCache::CachedTexture* ConstrainedTextureCache::findEntryForKey(
    const std::string& name, const std::string& sourceArchiveStem, bool applyPaletteMask,
    const std::string** foundKey) const {
    const std::string internalKey = cacheKey(name, sourceArchiveStem, applyPaletteMask);
    auto it = cache_.find(internalKey);
    if (it != cache_.end()) {
        if (foundKey) *foundKey = &it->first;
        return &it->second;
    }
    // B1083 dedupe: this key may be an alias recorded by the promote-time
    // gate -- its content is served by the TWIN's single GPU entry, so the
    // alias answers like its target (texture, hasAlpha, companion,
    // ETC1-ness; an evicted shell answers too -- its driver-side bind
    // restores the same pixels, exactly as a direct evicted hit does; the
    // foundKey the caller touches/reports is the target's). A stale alias
    // (target destroyed) does NOT answer here: the request falls through
    // exactly as if the alias never existed (its own pending promotion, or
    // the scoped-miss null).
    {
        auto alias = deduped_.find(internalKey);
        if (alias != deduped_.end()) {
            auto twin = cache_.find(alias->second);
            if (twin != cache_.end()) {
                if (foundKey) *foundKey = &twin->first;
                return &twin->second;
            }
        }
    }
    // Scoped query that missed: a SIBLING archive's entry is the wrong
    // answer — return null (the caller treats a miss as "not this copy").
    if (!sourceArchiveStem.empty()) {
        return nullptr;
    }
    // Unscoped miss: iterate the prefix range [name, name\x00) and look for
    // the entry whose realName IS name with this mask (any archive). A
    // lower_bound(name) alone is not enough: the unscoped key `name` sorts
    // BELOW any suffixed key of a LONGER name (name < "nameA" < "name\x01.."),
    // so the range can contain unrelated longer-named entries that must be
    // skipped, and a prefix-matching-but-different-name key must not satisfy
    // the realName check.
    for (auto it2 = cache_.lower_bound(name);
         it2 != cache_.end() && it2->first.compare(0, name.size(), name) == 0;
         ++it2) {
        if (it2->first.size() == name.size()) break;  // the plain key itself: miss
        std::string realName;
        std::string stem;
        bool masked = false;
        splitCacheKey(it2->first, realName, stem, masked);
        if (realName == name && masked == applyPaletteMask) {
            if (foundKey) *foundKey = &it2->first;
            return &it2->second;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// B1083 (GitLab #403, ruling 2026-10-16): dedupe by content
// ---------------------------------------------------------------------------

// FNV-1a 64 (the same family as rawBytesDigest in texture_origin.h) over a
// uint32.
namespace {
inline uint64_t fnvU32(uint64_t h, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 1099511628211ULL;
    }
    return h;
}
}  // namespace

uint64_t ConstrainedTextureCache::compressedContentDigest(const CompressedEntry& e) {
    uint64_t h = 1469598103934665603ULL;
    h = fnvU32(h, e.width);
    h = fnvU32(h, e.height);
    h = fnvU32(h, e.mipLevels);
    h = fnvU32(h, e.payloadFormat);
    h ^= e.hasAlpha ? 1u : 0u;
    h *= 1099511628211ULL;
    if (e.etc1Data && e.dataSize) {
        for (uint32_t i = 0; i < e.dataSize; ++i) {
            h ^= e.etc1Data[i];
            h *= 1099511628211ULL;
        }
    }
    if (e.hasAlpha && e.alphaData && e.alphaDataSize) {
        h ^= 0xA5A5u;  // boundary: companion bytes are a distinct run
        h *= 1099511628211ULL;
        for (uint32_t i = 0; i < e.alphaDataSize; ++i) {
            h ^= e.alphaData[i];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

uint64_t ConstrainedTextureCache::decodedContentDigest(const std::vector<uint8_t>& rgba, int width,
                                                        int height, bool hasAlpha) {
    uint64_t h = 1469598103934665603ULL;
    h = fnvU32(h, static_cast<uint32_t>(width));
    h = fnvU32(h, static_cast<uint32_t>(height));
    h ^= hasAlpha ? 1u : 0u;
    h *= 1099511628211ULL;
    for (uint8_t b : rgba) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

size_t ConstrainedTextureCache::getDedupedEntryCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return deduped_.size();
}

// B1083 dedupe: the content identity of a PENDING payload (0 when the key
// holds none -- a same-key re-arrival after a promotion, or a key nothing
// decoded yet; the gate then dedupes nothing, which is correct: nothing is
// about to be promoted). Call while holding mutex_.
uint64_t ConstrainedTextureCache::pendingContentDigest(const std::string& internalKey) const {
    auto dec = decodedEntries_.find(internalKey);
    if (dec != decodedEntries_.end()) return dec->second.contentDigest;
    auto cmp = compressedEntries_.find(internalKey);
    if (cmp != compressedEntries_.end()) return compressedContentDigest(cmp->second);
    return 0;
}

// B1083 dedupe: the first entry of this (name, mask) family (in cache_ key
// order -- the plain key first, then the \x01-suffixed archive variants, the
// same range argument as findEntryForKey) under a DIFFERENT key whose
// content digest matches, or not found. Scoped requests only: an unscoped
// request must create/answer the plain (name, mask) key itself (the
// configured-order answer), never an archive-scoped twin. An EVICTED shell
// counts: its driver-side bind restores the same pixels, so aliasing to it
// is safe (and consistent: findEntryForKey answers an evicted direct hit
// with its shell too). Call while holding mutex_.
ConstrainedTextureCache::ContentTwin ConstrainedTextureCache::findContentTwin(
    const std::string& name, const std::string& sourceArchiveStem, bool applyPaletteMask,
    uint64_t contentDigest) const {
    ContentTwin out;
    if (sourceArchiveStem.empty() || contentDigest == 0) return out;
    const std::string requestKey = cacheKey(name, sourceArchiveStem, applyPaletteMask);
    for (auto it = cache_.lower_bound(name);
         it != cache_.end() && it->first.compare(0, name.size(), name) == 0;
         ++it) {
        const auto& key = it->first;
        if (key == requestKey) continue;
        if (key.size() == name.size()) {
            // The plain (name) key: the unmasked family member.
            if (key != cacheKey(name, "", applyPaletteMask)) continue;
        } else {
            std::string realName;
            std::string stem;
            bool masked = false;
            splitCacheKey(key, realName, stem, masked);
            if (realName != name || masked != applyPaletteMask) continue;
        }
        const auto& entry = it->second;
        if (entry.contentDigest == contentDigest) {
            out.entry = &entry;
            out.key = &key;
            out.digest = entry.contentDigest;
            return out;
        }
    }
    return out;
}

// B1083 dedupe (ruling 2026-10-16): the promote-time gate -- the ONLY
// dedupe decision point. A scoped request whose payload is about to become
// a GPU entry asks whether an entry for this (name, mask) already exists
// with the same content digest: Deduped -- the request key becomes an
// alias (deduped_) of that entry, the caller answers the entry's texture
// (a resident one, or an evicted shell whose bind restores the identical
// pixels) and drops the request's payload; one GPU entry serves both keys.
// None -- no content-identical entry (a different-content same-name family,
// e.g. qeytoqrg tree70's DXT1-vs-DXT5, keeps two entries by design): the
// caller promotes its own payload, which then becomes the target for any
// later content-identical sibling request. First entry promoted in time
// wins -- exactly the pre-B1083 name-keyed cache's first-upload-wins
// answer, since the zone load promotes each texture right after decoding
// it. Call while holding mutex_.
ConstrainedTextureCache::DedupeGateOutcome ConstrainedTextureCache::dedupeBeforePromote(
    const std::string& name, const std::string& sourceArchiveStem, bool applyPaletteMask,
    const std::string& internalKey, uint64_t contentDigest) {
    DedupeGateOutcome out;
    if (sourceArchiveStem.empty() || contentDigest == 0) return out;  // unscoped, or nothing to promote
    auto existing = deduped_.find(internalKey);
    if (existing != deduped_.end()) {
        // Already an alias: the recorded target, if still in the map
        // (resident or an evicted shell -- both restore the same pixels on
        // their next bind), is the answer. Gone (destroyed) -- decide fresh.
        auto target = cache_.find(existing->second);
        if (target != cache_.end()) {
            out.result = DedupeGateResult::Deduped;
            out.twinEntry = &target->second;
            return out;
        }
        deduped_.erase(existing);
    }
    // An entry under the request's own key (an evicted shell: uploadTexture's
    // early return answered a resident one already) restores its OWN pixels
    // on first bind -- it is never folded into another key's entry.
    if (cache_.find(internalKey) != cache_.end()) return out;
    const ContentTwin twin = findContentTwin(name, sourceArchiveStem, applyPaletteMask, contentDigest);
    if (!twin.found()) return out;
    deduped_[internalKey] = *twin.key;
    out.result = DedupeGateResult::Deduped;
    out.twinEntry = twin.entry;
    LOG_TRACE(MOD_GRAPHICS, "  [CONSTRAINED] texture '{}' deduped: archive '{}'{} reuses entry (one GPU entry serves both keys)",
              name, sourceArchiveStem, applyPaletteMask ? " [masked]" : "");
    return out;
}

// B1083 dedupe: every alias recording `key` as its target is dropped -- the
// target is going (destroyEntry / clear). The aliasers re-fold, or resolve
// on their own, when they next promote or query. Call while holding mutex_.
void ConstrainedTextureCache::dropAliasesTo(const std::string& key) {
    for (auto it = deduped_.begin(); it != deduped_.end();) {
        if (it->second == key) it = deduped_.erase(it);
        else ++it;
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
                                                          const TextureOrigin* origin,
                                                          const std::string& sourceArchiveStem) {
    eqt::memory::MemoryScope memScope(eqt::memory::Category::TexturesDecoded);  // T491
    std::lock_guard<std::mutex> lock(mutex_);

    // P46d: masked and unmasked variants of the same name are distinct cache
    // entries (see cacheKey()). B1083 (GitLab #403): the entry is additionally
    // scoped to the SOURCE ARCHIVE -- internalKey is identity only when both
    // sourceArchiveStem and applyPaletteMask are empty/false (the vast
    // majority), so every unscoped call site is completely unaffected. Two
    // packs of one zone family that hold a name with different content
    // (qeytoqrg tree70 opaque vs qeytoqrg_obj tree70 alpha) now coexist as
    // two entries instead of collapsing into one name-keyed slot.
    const std::string internalKey = cacheKey(name, sourceArchiveStem, applyPaletteMask);

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
    //
    // B1083 (GitLab #403): findCompressedEntry takes the SOURCE ARCHIVE too.
    // When a request names the archive its bytes came from (the S3D stem a
    // zone/object WLD was loaded from), that pack is tried FIRST, before the
    // configured-order scan of the other open packs. qeytoqrg.s3d and
    // qeytoqrg_obj.s3d both hold tree70 (opaque DXT1 vs alpha DXT5) and the
    // packs mirror that; the configured order ({zone}, {zone}_obj,
    // {zone}_chr) handed the OBJECT's material the zone pack's opaque copy,
    // so the willow drew as a solid rectangle. With the source tried first
    // the object keeps its own copy and the zone keeps its own — the two
    // coexist as (archive, name) entries instead of one name-keyed slot.
    // An empty stem (every call site that does not name a source: character
    // models, equipment, composite sheets, UI) is exactly today's scan +
    // resolver fallback, so T207/T266's late-open rules and the
    // global/_chr lookups are untouched.
    auto* compressed = findCompressedEntry(name, sourceArchiveStem);
    if (compressed) {
        // B1083 dedupe (ruling 2026-10-16): the (archive, name) key change
        // splits EVERY name a sibling pack also holds -- byte-identical or
        // not -- into two entries, and those extra bytes bust the texture
        // budget on zones the name-keyed run stayed under (the x86 gate:
        // qeynos2 at 99.6% of the 20 MiB ceiling, evicted textures
        // drawing black). This payload is stored with its content identity
        // (the promote-time gate's input, see pendingContentDigest); the
        // gate in uploadTexture() folds a content-identical sibling request
        // into the entry this payload becomes -- one GPU entry serves both
        // keys. Only genuinely different same-name textures (qeytoqrg
        // tree70, DXT1-vs-DXT5) keep two entries -- the willow fix itself.
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
                      "name misses every open pack -- this is not a plain "
                      "asset gap. Every name in a rebuilt pack either fits "
                      "the field or goes to the v3 name table.",
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

    // B1083 dedupe (ruling 2026-10-16): the decode path (S3D, no EQE1) has
    // the same split -- a name held byte-identically by two of the zone's
    // open .s3d archives becomes two (archive, name) entries. The decoded
    // image is stored with its content digest (FNV-1a 64 over pixels +
    // dimensions + alpha); the promote-time gate in uploadTexture() folds a
    // content-identical scoped request into the entry this image becomes
    // (one GPU entry serves both keys).
    decoded.contentDigest = decodedContentDigest(decoded.rgbaPixels, width, height, hasAlpha);
    decodedEntries_[internalKey] = std::move(decoded);

    LOG_DEBUG(MOD_GRAPHICS, "  [CONSTRAINED] texture '{}' decoded ({}x{}, alpha={}{})",
              name, width, height, hasAlpha, applyPaletteMask ? ", masked variant" : "");
    return nullptr;  // Decoded but not uploaded — call uploadTexture() to promote
}

irr::video::ITexture* ConstrainedTextureCache::uploadTexture(const std::string& name, bool applyPaletteMask,
                                                              const std::string& sourceArchiveStem) {
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
    // B1083: the key also carries the source archive stem, so a masked or
    // archive-scoped variant promotes its OWN entry rather than the plain
    // name's.
    const std::string internalKey = cacheKey(name, sourceArchiveStem, applyPaletteMask);

    // Already uploaded?
    auto cacheIt = cache_.find(internalKey);
    if (cacheIt != cache_.end()) {
        touchInternal(internalKey);
        return cacheIt->second.texture;
    }

    // B1083 dedupe (ruling 2026-10-16): the promote-time gate -- the ONLY
    // dedupe decision point. A scoped request that is about to create a GPU
    // entry asks whether an entry for this (name, mask) already exists with
    // the same content digest: if so it becomes an ALIAS of that entry (one
    // GPU entry serves both keys) and the entry's texture is the answer,
    // and the pending payload is dropped so nothing promotes a second
    // entry. Genuinely different same-name textures (qeytoqrg tree70,
    // DXT1-vs-DXT5) keep two entries -- the willow fix itself. First
    // promoted in time wins (the pre-B1083 name-keyed cache's
    // first-upload-wins answer). A re-arrival for an already-recorded
    // alias answers the same way; a stale alias (its target was destroyed)
    // is dropped and the normal promotion below runs, so the dedupe is a
    // memory optimization and never makes a name unresolvable.
    {
        const DedupeGateOutcome gate = dedupeBeforePromote(name, sourceArchiveStem,
                                                           applyPaletteMask, internalKey,
                                                           pendingContentDigest(internalKey));
        if (gate.result == DedupeGateResult::Deduped) {
            const auto targetKey = deduped_.find(internalKey);
            if (targetKey != deduped_.end()) touchInternal(targetKey->second);
            compressedEntries_.erase(internalKey);
            decodedEntries_.erase(internalKey);
            if (ledgerEnabled_) pendingLedgerTag_.erase(internalKey);
            return gate.twinEntry->texture;
        }
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
    // B1083 dedupe: the content identity a later sibling-archive request
    // compares against (recorded in getOrLoad() from the same decoded image).
    cached.contentDigest = entry.contentDigest;

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
        std::string sourceArchiveStem;
        bool applyPaletteMask = false;
        splitCacheKey(key, realName, sourceArchiveStem, applyPaletteMask);
        if (uploadTexture(realName, applyPaletteMask, sourceArchiveStem)) {
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

irr::video::ITexture* ConstrainedTextureCache::getTexture(const std::string& name, bool applyPaletteMask,
                                                           const std::string& sourceArchiveStem) {
    std::lock_guard<std::mutex> lock(mutex_);
    // P50: internalKey mirrors getOrLoad()/uploadTexture() — identity when
    // applyPaletteMask is false, so every existing (unmasked) call site is
    // byte-identical to before this parameter existed.
    // B1083: the archive axis too — a zone and the _obj pack of one family
    // can both cache the same name, and only the scoped query finds the
    // entry the caller decoded (see cacheKey()). findEntryForKey() keeps the
    // scoped-miss-returns-null rule (no sibling-archive leak) and, for an
    // unscoped query, finds the entry stored under (name, mask) in any
    // archive — including an S3D-stemmed one the on-demand fallback stored.
    const std::string* foundKey = nullptr;
    const CachedTexture* entry = findEntryForKey(name, sourceArchiveStem, applyPaletteMask, &foundKey);
    if (entry != nullptr) {
        touchInternal(*foundKey);
        return entry->texture;
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

bool ConstrainedTextureCache::hasAlpha(const std::string& name, bool applyPaletteMask,
                                       const std::string& sourceArchiveStem) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // B1083: the (archive, name) entry — the zone and _obj copies of one name
    // carry different hasAlpha, so an unscoped query would report whichever
    // pack the configured order happened to cache first (see cacheKey()).
    // findEntryForKey() scopes the answer: a SCOPED query misses cleanly (no
    // sibling-archive leak), an UNSCOPED query falls back to the entry stored
    // for that (name, mask) under any archive.
    const CachedTexture* entry = findEntryForKey(name, sourceArchiveStem, applyPaletteMask);
    return entry != nullptr && entry->hasAlpha;
}

irr::video::ITexture* ConstrainedTextureCache::getAlphaTexture(const std::string& name, bool applyPaletteMask,
                                                               const std::string& sourceArchiveStem) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // B1083: the companion belongs to the (archive, name) entry — the object
    // material binds the _obj pack's companion, not the zone pack's. The
    // scoped/unscoped semantics mirror hasAlpha() via findEntryForKey().
    const CachedTexture* entry = findEntryForKey(name, sourceArchiveStem, applyPaletteMask);
    if (entry != nullptr) return entry->alphaTexture;
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

bool ConstrainedTextureCache::isETC1Texture(const std::string& name, bool applyPaletteMask,
                                            const std::string& sourceArchiveStem) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // B1083: the (archive, name) entry (see cacheKey()); scoped/unscoped
    // semantics mirror hasAlpha() via findEntryForKey().
    const CachedTexture* entry = findEntryForKey(name, sourceArchiveStem, applyPaletteMask);
    return entry != nullptr && entry->isETC1;
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
    // B1083 dedupe: every destroyed entry was an alias target; drop them all
    // (a target surviving the clear re-registers itself and re-earns its
    // aliasers on their next promotion).
    deduped_.clear();
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
