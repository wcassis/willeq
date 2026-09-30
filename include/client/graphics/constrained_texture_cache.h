#ifndef EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
#define EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H

#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_format.h"
#include "client/graphics/texture_upload_ledger.h"
#include <irrlicht.h>
#include <map>
#include <set>
#include <list>
#include <mutex>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace EQT {
namespace Graphics {

class ZoneMeshBuilder;
class EQE1Loader;

// Listener interface for texture DESTRUCTION notifications.
// Subsystems that register textures with the cache can implement this
// to null out local pointers when their textures are destroyed.
//
// B944: since GitLab #262 an LRU eviction of a restorable texture does NOT
// fire this -- the object survives (its storage is released and restored on
// the next bind), so holders keep a valid pointer and must NOT swap it for the
// placeholder. The callback fires when the object really goes: clear() (zone
// unload), a registerTexture() replacement, and an eviction of an entry the
// cache cannot restore (see ConstrainedTextureCache::evictTexture()). For an
// ETC1 texture it fires twice, once for the main texture and once (name +
// "_alpha") for its alpha companion.
//
// P10b: the texture pointer is passed alongside the name (in addition to
// the pre-existing name-only contract) because the pointer is still valid
// and resolvable at notification time (evictTexture() notifies listeners
// BEFORE erasing the cache entry / calling driver_->removeTexture()), and
// at least one listener (RegionMeshTextureGuard, renderer_zone_merge.h)
// needs to scan raw SMaterial texture slots by pointer identity -- calling
// back into the cache (e.g. getTexture(name)) from inside this callback
// would deadlock (evictTexture() runs under a non-recursive mutex_ held by
// the calling uploadTexture()/evictUntilAvailable() frame). Existing
// listeners that only need the name may ignore the second parameter.
class TextureEvictionListener {
public:
    virtual ~TextureEvictionListener() = default;
    virtual void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) = 0;
};

// B944 (GitLab #262): where an evicted DECODE-path texture's bytes come back
// from. An EQE1 entry restores from its (mapped) flat pack and needs none of
// this; a texture the cache decoded from raw BMP/DDS bytes (the S3D path) was
// handed those bytes by a caller that has since released them, so an eviction
// can only give its storage back if something can produce the same bytes
// again by name. ArchiveTextureSource (archive_texture_source.h) re-reads them
// from the mapped S3D archives. A decode-path entry no provider can serve is
// evicted the pre-B944 way (destroyed, holders swapped to the placeholder by
// the guards) -- see ConstrainedTextureCache::evictTexture().
//
// Both calls happen under the cache's mutex, on whichever thread evicts or
// restores; an implementation must not call back into the cache.
class TextureSourceProvider {
public:
    virtual ~TextureSourceProvider() = default;
    virtual bool hasSource(const std::string& name) = 0;
    virtual bool readSource(const std::string& name, std::vector<char>& out) = 0;
};

// Decoded texture pixels waiting for GPU upload
struct DecodedUpload {
    std::string name;
    std::vector<uint8_t> rgbaPixels;  // Decoded RGBA, ready for GPU
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

// Decoded texture entry — decoded but not yet uploaded to GPU.
// Created by getOrLoad(), promoted to CachedTexture by uploadTexture().
struct DecodedEntry {
    std::vector<uint8_t> rgbaPixels;  // Decoded RGBA pixels
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

// LRU texture cache with memory budget enforcement
// Used for resource-constrained rendering modes (Voodoo1, etc.)
//
// B944 (GitLab #262) -- EVICTION KEEPS THE OBJECT. When the ceiling
// (config.textureMemoryBytes) forces a texture out, the cache releases its GL
// storage (and its ETC1 alpha companion's) and keeps the ITexture itself: the
// entry stays in the map as a non-resident "shell" with its name, size and
// restore source. Every raw ITexture* a material, mesh cache or manager holds
// therefore stays valid across an eviction, and the driver's first bind of the
// shell (COGLES2Texture::getOpenGLTextureName()) calls back into this cache,
// which re-uploads the same pixels -- evicting something older if it must --
// and hands the new GL name back. Only resident entries count toward
// getCurrentUsage() and sit in the LRU list. An object is destroyed only by
// clear() (zone unload), by a registerTexture() replacement, or by the
// pre-B944 fallback for an entry that cannot be restored (no GLES2 storage to
// release, or no source to restore from) -- and those three still notify the
// TextureEvictionListeners, whose guards swap holders to the placeholder.
class ConstrainedTextureCache {
public:
    // Construct with config only (no driver needed). Call setDriver() later
    // when the graphics context is available for GPU uploads.
    explicit ConstrainedTextureCache(const ConstrainedRendererConfig& config);
    ~ConstrainedTextureCache();

    // Set video driver for GPU texture uploads. Must be called before
    // uploadTexture() or uploadAllPending(). Probes compressed texture support.
    void setDriver(irr::video::IVideoDriver* driver);

    // Decode and cache a texture (no GL calls — safe to call from any thread).
    // If already uploaded (CachedTexture), returns the ITexture*.
    // If already decoded (DecodedEntry), returns nullptr (call uploadTexture to promote).
    // If not in cache, decodes raw bytes to RGBA and stores as DecodedEntry, returns nullptr.
    // Returns nullptr on decode failure.
    //
    // P13: category/callsite are optional ledger tags (e.g. "zone_region",
    // "object", "entity_perpart", "composite_sheet", "sky") recorded against
    // this name for the upload-ledger row emitted when this actually gets
    // uploaded (see uploadTexture()/uploadCompressed*()). Only stored when
    // the ledger is enabled (textureUploadLedgerPath non-empty) — no-op
    // otherwise, so passing these costs nothing when the audit is off.
    //
    // P46: applyPaletteMask, default false, is the gate for the runtime
    // palette-index-0 masking fix in decodeBMP() — pass true ONLY when the
    // caller has already confirmed (via the WLD-derived textureMasked()
    // flag) that this specific texture is WLD-masked. Leaving it false
    // preserves the pre-P46 behavior byte-for-byte (8bpp palette index 0
    // decodes opaque, as it always has) — this is load-bearing for zone
    // geometry, where palette slot 0 is frequently a legitimate opaque
    // color and must never be punched transparent. See decodeBMP().
    irr::video::ITexture* getOrLoad(const std::string& name,
                                     const std::vector<char>& data,
                                     const std::string& category = "uncategorized",
                                     const std::string& callsite = "",
                                     bool applyPaletteMask = false);

    // Upload a decoded texture to the GPU.
    // Promotes DecodedEntry → CachedTexture via driver_->addTexture().
    // Must be called on a thread that owns the rendering context.
    // Returns the uploaded ITexture*, or nullptr if not found/failed.
    //
    // P46d: applyPaletteMask must match whatever was passed to the getOrLoad()
    // call that decoded this name — it selects which internal variant entry
    // (see cacheKey()) to promote. Defaults to false, matching every call site
    // that never requests the masked variant (the vast majority). Callers that
    // pass applyPaletteMask=true to getOrLoad() and then explicitly call
    // uploadTexture() themselves (rather than relying on uploadAllPending(),
    // which re-derives the correct internal key on its own — see .cpp) MUST
    // pass the same flag here or the lookup will miss.
    irr::video::ITexture* uploadTexture(const std::string& name, bool applyPaletteMask = false);

    // Upload all pending decoded textures to the GPU.
    // Calls uploadTexture() for each DecodedEntry in the cache.
    // Used by the sequential loader after populating the cache with getOrLoad().
    // Returns the number of textures uploaded.
    int uploadAllPending();

    // Mark a texture as recently used (moves to back of LRU list)
    void touch(const std::string& name);

    // Check if texture is in cache
    bool hasTexture(const std::string& name) const;

    // Get a cached texture without loading (returns nullptr if not cached).
    // P50: applyPaletteMask must match whatever was passed to the
    // getOrLoad()/uploadTexture() call that stored this entry (see
    // cacheKey()) — a plain-name query for a masked-variant entry always
    // missed before this parameter existed. Defaults to false, matching
    // every call site that never requests the masked variant.
    irr::video::ITexture* getTexture(const std::string& name, bool applyPaletteMask = false);

    // Register an externally-created texture into the LRU cache.
    // The texture must already be created via driver_->addTexture().
    // Performs LRU bookkeeping and eviction but skips decode/upload.
    // Returns true if registered, false if texture is too large for budget.
    bool registerTexture(const std::string& name, irr::video::ITexture* texture,
                         size_t sizeBytes, bool hasAlpha = false,
                         const std::string& category = "uncategorized");

    // Check if a cached texture has alpha transparency.
    // P50: see getTexture() above — applyPaletteMask must match the
    // variant's original getOrLoad()/uploadTexture() call.
    bool hasAlpha(const std::string& name, bool applyPaletteMask = false) const;

    // Get ETC1 alpha companion texture (for binding to texture unit 1)
    // Returns nullptr if texture has no alpha companion.
    // P50: see getTexture() above — applyPaletteMask must match the
    // variant's original getOrLoad()/uploadTexture() call.
    irr::video::ITexture* getAlphaTexture(const std::string& name, bool applyPaletteMask = false) const;

    // Reverse-lookup overload for callers that only have the GPU texture
    // pointer (e.g. ZoneMeshBuilder::buildCompositeMesh, which builds from a
    // single merged texture with no single originating cache name). Linear
    // scan over the cache — fine given the current (small, mesh-build-time)
    // call volume.
    irr::video::ITexture* getAlphaTexture(irr::video::ITexture* texture) const;

    // H02d (Option A alpha-source-aware shader selection): does this cached
    // texture come from the ETC1/EQE1 pipeline (uploadCompressedETC1, which
    // builds a companion alpha texture on unit 1) or the direct RGBA/DDS
    // decode path (uploadTexture, which has real alpha in the main texture's
    // own alpha channel and never needs/builds a companion)? Callers use this
    // to pick the matching alpha-test shader variant. Returns false for
    // unknown/not-yet-uploaded names.
    // P50: see getTexture() above — applyPaletteMask must match the
    // variant's original getOrLoad()/uploadTexture() call.
    bool isETC1Texture(const std::string& name, bool applyPaletteMask = false) const;

    // Reverse-lookup overload by GPU texture pointer (see getAlphaTexture(ITexture*)).
    bool isETC1Texture(irr::video::ITexture* texture) const;

    // Clear all cached textures
    void clear();

    // Memory statistics
    size_t getCurrentUsage() const;

    // T504: true when at least one EQE1 flat pack is registered -- the exact
    // condition under which getOrLoad() serves every name from a pack and treats
    // a miss as fatal, i.e. under which a caller's raw BMP/DDS bytes are never
    // decoded. RaceModelLoader uses it to release those bytes at once.
    bool hasEQE1Loaders() const;
    size_t getMemoryLimit() const;
    size_t getAvailableMemory() const;
    // Entries, resident or evicted (an evicted entry is still a live object).
    size_t getTextureCount() const;
    // B944: entries whose GL storage is resident, and the restore statistics.
    size_t getResidentTextureCount() const;
    size_t getRestoreCount() const;
    size_t getRestoreFailureCount() const;
    // B944: true when `name` is cached AND its storage is resident; false for
    // an evicted entry or an unknown name.
    bool isResident(const std::string& name, bool applyPaletteMask = false) const;

    // B944: register/unregister a source for evicted decode-path textures
    // (see TextureSourceProvider). Non-owning; unregister before destroying.
    void addSourceProvider(TextureSourceProvider* provider);
    void removeSourceProvider(TextureSourceProvider* provider);

    // Cache statistics
    size_t getCacheHits() const;
    size_t getCacheMisses() const;
    size_t getEvictionCount() const;
    size_t getCompressedUploadCount() const;

    // Get hit rate as percentage (0-100)
    float getHitRate() const;

    // Reset statistics
    void resetStatistics();

    // Freeze/unfreeze the cache.
    // B944: this flag is consulted by NOTHING -- evictUntilAvailable() evicts
    // whether or not the cache is frozen, and nothing calls freeze(). The old
    // comment here ("when frozen, no evictions occur (prevents crashes from
    // dangling texture pointers)") was false; eviction is made safe by keeping
    // the object (see the class comment), not by freezing.
    void freeze();
    void unfreeze();
    bool isFrozen() const;

    // Get config for debug display
    const ConstrainedRendererConfig& getConfig() const { return config_; }

    // T475 (board B670): setGPUUploadThread() and clearPendingAsync() are
    // deleted. The pointer was never read, and the pending-async set was never
    // written after T04 made uploads synchronous; its one clearer was the dead
    // texture-arrival arm in renderer_gpu_upload.cpp.

    // Set scene manager for safe eviction (scans meshes to remove texture references)
    void setSceneManager(irr::scene::ISceneManager* smgr);

    // Get placeholder texture (used when a texture is evicted from a mesh material)
    irr::video::ITexture* getPlaceholderTexture();

    // P10b: reverse-lookup liveness check by GPU texture pointer -- true if
    // this pointer is currently a live cache entry OR the placeholder itself
    // (both safe to dereference); false for anything else, including a
    // pointer that WAS a cache entry and has since been evicted/freed via
    // driver_->removeTexture(). Linear scan over the cache, same precedent
    // as getAlphaTexture(ITexture*)/isETC1Texture(ITexture*) above — fine
    // given the current (small, mesh-build-time) call volume. Defense in
    // depth for ZoneMergeBuffers::build() (see renderer_zone_merge.h); the
    // actual fix for the dangling-pointer root cause is
    // RegionMeshTextureGuard, not this check.
    bool isLiveTexture(irr::video::ITexture* texture) const;

    // T04: queueDecoded, queueDecodedARGB, processUploadQueue, drainDecodedQueue,
    // processOneUpload, getStagedUploadCount removed. Use getOrLoad() + uploadTexture() instead.

    // Check if a texture name is decoded but not yet uploaded (not yet in cache)
    bool isPending(const std::string& name) const;

    // Check if any decoded textures are waiting for their upload
    bool hasPendingWork() const;

    // Set mesh builder for entity texture registration (non-owning)
    void setMeshBuilder(ZoneMeshBuilder* meshBuilder);

    // Eviction listener management
    void addEvictionListener(TextureEvictionListener* listener);
    void removeEvictionListener(TextureEvictionListener* listener);

    // EQE1 flat pack loader management (GLES2 ETC1 compressed textures)
    // T202: mark a texture as never-evictable. Needed for anything the
    // renderer holds a RAW pointer to across frames -- the UI atlas is loaded
    // from willeq.eqe1 into this cache but is drawn every frame forever, so
    // LRU eviction left UIRenderer::endFrame drawing a freed texture (T201).
    void protectTexture(const std::string& name) { protectedTextures_.insert(name); }

    void addEQE1Loader(const EQE1Loader* loader);
    void clearEQE1Loaders();

    // P04: global EQE1 archives are no longer opened eagerly at zone load.
    // This callback (set once by the renderer) opens the next not-yet-tried
    // global archive on a lookup miss and registers it via addEQE1Loader()
    // internally; returns nullptr once the list is exhausted. Only archives a
    // zone actually references end up mmap'd.
    // T207: resolve a lookup name to the ONE archive that holds it and open
    // that archive (registering it via addEQE1Loader), or return nullptr having
    // opened nothing. Replaces the old argless lazy opener, which walked the
    // configured global list one archive at a time on every miss -- so a name
    // present in no archive opened all of them.
    void setEQE1ArchiveResolver(std::function<const EQE1Loader*(const std::string&)> resolver) {
        eqe1ArchiveResolver_ = std::move(resolver);
    }

    // T214 AUDIT INSTRUMENT: complete per-name request log for one zone load.
    // Every EQE1 lookup (texture and raw-data) records the name and which
    // archive actually served it, so the open-archive set can be audited
    // against the set the zone genuinely needs. Recording is unconditional and
    // cheap (one map insert per DISTINCT name); dumpEQE1RequestLog() writes it.
    struct EQE1Request {
        std::string servedBy;   // archive stem, or "<NOT-FOUND>"
        uint32_t    count = 0;  // times this name was looked up
        bool        rawData = false;
    };
    void dumpEQE1RequestLog(const std::string& path) const;
    // T266: mark an archive as opened OUTSIDE the zone's load-time candidate
    // set (rule R2 -- a lookup needed it and nothing open could serve it).
    // Every name that archive serves is written with `late-open` in the
    // request log's `how` column, so a post-load spawn's archive is
    // distinguishable from the load-time set in the audit.
    void noteEQE1LateOpen(const std::string& archiveStem);

    // Look up raw (non-texture) data from registered EQE1 loaders.
    // Used for JSON metadata, WLD files, etc. packed as EQE1_TYPE_RAW_DATA entries.
    // Returns true if found, with data pointer and size. Data is owned by the EQE1 loader.
    bool getEQE1RawData(const std::string& name, const uint8_t*& data, uint32_t& size) const;

    // P13: enable the upload/eviction ledger (off by default — see
    // ConstrainedRendererConfig::textureUploadLedgerPath). When disabled,
    // getOrLoad's category/callsite arguments are discarded immediately and
    // no ledger bookkeeping happens anywhere in this class — behavior and
    // performance are unchanged from pre-P13 code.
    void setLedgerEnabled(bool enabled) { ledgerEnabled_ = enabled; }
    bool isLedgerEnabled() const { return ledgerEnabled_; }

    // Dump the ledger collected so far to `path` (see TextureUploadLedger::
    // dumpToFile). No-op (returns false) if the ledger isn't enabled.
    bool dumpLedger(const std::string& path, const std::string& runLabel);

private:
    // Look up a texture name in registered EQE1 loaders.
    // Strips .bmp extension and lowercases before lookup.
    // Returns nullptr if not found in any loader.
    struct CompressedEntry {
        const uint8_t* etc1Data;  // v2: raw uncompressed payload bytes (name kept for churn's sake)
        uint32_t dataSize;
        uint16_t width;
        uint16_t height;
        uint8_t  mipLevels;
        bool hasAlpha;
        const uint8_t* alphaData;
        uint32_t alphaDataSize;
        uint8_t  payloadFormat = 0;  // EQE1PayloadFormat; 0 == EQE1_PAYLOAD_ETC1
    };
    const CompressedEntry* findCompressedEntry(const std::string& name);

    // P04: copy of the loader-pointer vector taken under eqe1LoadersMutex_.
    std::vector<const EQE1Loader*> snapshotEQE1Loaders() const;

    // Upload pre-compressed ETC1 data to GPU via glCompressedTexImage2D.
    // Creates GL texture, wraps in ITexture*, registers in LRU cache.
    // Must be called on a thread with GL context. Caller holds mutex_.
    // ETC1/v1 path — byte-for-byte untouched by the v2 payload-format work.
    // P46d: `name` is the real texture name (GL debug label, ledger row,
    // ZoneMeshBuilder registration); `cacheEntryKey` is this class's own
    // internal (possibly mask-mangled, see cacheKey()) bookkeeping key.
    irr::video::ITexture* uploadCompressedETC1(const std::string& name,
                                                const std::string& cacheEntryKey,
                                                const CompressedEntry& entry);

    // Upload an uncompressed v2 payload (RGB565/RGBA4444/RGBA8888) to GPU via
    // glTexImage2D with the format/type matching entry.payloadFormat. No
    // alpha companion is ever built (v2 alpha lives in the main texture's own
    // alpha channel) — CachedTexture::isETC1 stays false, which routes these
    // textures to H02d's main-alpha shader variant. Caller holds mutex_.
    // P46d: see uploadCompressedETC1()'s name/cacheEntryKey split above.
    irr::video::ITexture* uploadCompressedV2(const std::string& name,
                                              const std::string& cacheEntryKey,
                                              const CompressedEntry& entry);

    // B944: the GL halves of the three upload paths, shared with the eviction
    // restore so a restored texture is built by the same code as the first
    // upload. Caller holds mutex_ and owns a GL context.
    bool createETC1Storage(const std::string& name, const CompressedEntry& entry,
                           unsigned int& texIdOut, unsigned int& alphaTexIdOut);
    bool createV2Storage(const std::string& name, const CompressedEntry& entry,
                         unsigned int& texIdOut);
    irr::video::ITexture* createDecodedTexture(const std::string& glName,
                                               const std::vector<uint8_t>& rgbaPixels,
                                               int width, int height);

    // P46d: internal cache/decodedEntries_/compressedEntries_ map key for
    // (name, applyPaletteMask). Identity when applyPaletteMask is false (the
    // overwhelming majority of calls) — cacheKey(x, false) == x always, which
    // is why callers that only ever pass plain names (getTexture/hasTexture/
    // touch/isPending/evictTexture, every eviction listener, P13b's
    // deferredPerPartTextureNames_ skip-list, uploadAllPending()'s own
    // re-derivation of the key it already collected) need NO changes: they
    // never request the masked variant, so they always resolve to the
    // unmasked entry, exactly as before this fix. Only getOrLoad() and
    // uploadTexture() (the two call sites that take applyPaletteMask
    // directly) ever produce or consume a masked key. A control character
    // prefix (never legal in an EQ texture/EQE1 filename) makes collision
    // with a real name impossible. See constrained_texture_cache.cpp's
    // top-of-file comment for the full rationale (root cause:
    // refactor/P46c_neutral_pair_stop.md; fix: refactor/P46d_cache_key_fix.md).
    static std::string cacheKey(const std::string& name, bool applyPaletteMask);

    // P46d: inverse of cacheKey() — recovers (realName, applyPaletteMask)
    // from an internal key. Needed by uploadAllPending(), which only has the
    // (possibly mangled) keys it harvested from decodedEntries_/
    // compressedEntries_, not the original plain names — calling
    // uploadTexture(key) directly (relying on cacheKey(key,false)==key) is
    // correct for cache_/decodedEntries_/compressedEntries_ lookups, but
    // would leak the mangled key into uploadTexture's `name` parameter,
    // which (unlike internalKey) is supposed to be the real display name
    // (GL debug label, ledger row, ZoneMeshBuilder::registerUploadedTexture
    // — see uploadTexture()'s comment). Without this split, masked entries
    // uploaded via uploadAllPending() register into ZoneMeshBuilder's
    // textureCache_ under the MANGLED name, which
    // mesh_building.cpp::getOrLoadTexture()'s plain-name lookup then never
    // finds — silently falling back to the unmasked loadTextureFromBMP()
    // path and defeating this whole fix. Caught by this unit's own
    // discard-fire re-verification (see Review).
    static void splitCacheKey(const std::string& key, std::string& realName, bool& applyPaletteMask);

    // Move name to back of LRU list — call only while holding mutex_
    void touchInternal(const std::string& name);

    // Get or create placeholder — call only while holding mutex_
    irr::video::ITexture* getPlaceholderTextureInternal();

    // Remove all references to a texture from mesh materials in the scene
    // This must be called before driver_->removeTexture() to prevent dangling pointers
    void clearTextureReferences(irr::video::ITexture* texture);

    // B338: S3TC capability probe, called once from setDriver(). Diagnostic
    // log only — its old result flag (compressedTexturesAvailable_) was
    // written four ways and consulted nowhere. It is the sole consumer of
    // config_.enableCompressedTextures, which is why it stays.
    void probeCompressedTextureSupport();

    // Try uploading DDS data as compressed texture directly to GPU
    // Returns nullptr if compressed upload not available or data not suitable
    irr::video::ITexture* tryCompressedUpload(const std::string& name,
                                               const std::vector<char>& data);

    // Evict least recently used texture(s) until we have at least 'bytesNeeded' available
    // Returns true if successful, false if cannot free enough space
    bool evictUntilAvailable(size_t bytesNeeded);

    // Evict a single texture by name. B944: releases the storage and keeps
    // the object when canReleaseStorage(); otherwise destroyEntry().
    void evictTexture(const std::string& name);

    // ---- B944 residency (constrained_texture_cache_residency.cpp) ----------
    struct CachedTexture;
    bool canReleaseStorage(const std::string& key, const CachedTexture& entry);
    void releaseStorage(const std::string& key, CachedTexture& entry);
    // Destroy an entry's objects and drop it from the map (clear(), a
    // registerTexture() replacement, an eviction that cannot restore).
    void destroyEntry(const std::string& key);
    // The destruction itself: listeners, scene scrub, ZoneMeshBuilder's map
    // (scrubMeshBuilder; not from clear(), see the .cpp), driver removal --
    // for the main texture AND its alpha companion.
    void destroyTextures(const std::string& key, CachedTexture& entry, bool scrubMeshBuilder);
    // The driver's restore callback (COGLES2Texture::ensureResident()).
    static bool restoreHook(void* ctx, void* texture);
    bool restoreEvicted(irr::video::ITexture* texture);
    bool rebuildStorage(const std::string& key, CachedTexture& entry);

    // Calculate texture size in bytes (after processing)
    size_t calculateTextureSize(int width, int height) const;

    // Process raw texture data: decode, downsample if needed, convert to 16-bit
    // Returns processed RGBA data with final dimensions
    // Sets hasAlpha to true if texture has meaningful alpha channel
    bool processTextureData(const std::vector<char>& rawData,
                            std::vector<uint8_t>& processedData,
                            int& width, int& height,
                            bool& hasAlpha,
                            bool applyPaletteMask = false);

    // Downsample texture data using box filter
    // Halves dimensions until both are <= maxDimension
    void downsampleToMaxSize(std::vector<uint8_t>& data,
                             int& width, int& height,
                             int channels);

    // Convert 32-bit RGBA to 16-bit format
    // Returns RGB565 for opaque, RGBA1555 for transparent
    std::vector<uint16_t> convertTo16Bit(const std::vector<uint8_t>& rgba,
                                          int width, int height,
                                          bool hasAlpha);

    // Detect if texture has meaningful alpha (not all 255)
    bool detectAlpha(const std::vector<uint8_t>& rgba, int width, int height);

    // Decode BMP data to RGBA.
    //
    // P46: applyPaletteMask (default false) gates the EQ palette-index-0
    // transparency convention for 8bpp paletted BMPs — mirrors
    // tools/eqe1_builder.cpp's decodeBMPToRGBA() (the only other place this
    // convention is implemented, offline-only, until this fix). When false
    // (the pre-P46 and all-non-entity-texture default), palette index 0
    // decodes as an ordinary opaque color, exactly as before — required
    // because index 0 is a legitimate color in unmasked 8bpp textures and
    // in zone textures generally, not a universal transparency marker.
    bool decodeBMP(const std::vector<char>& data,
                   std::vector<uint8_t>& rgba,
                   int& width, int& height,
                   bool applyPaletteMask = false);

    // Cached texture entry
    struct CachedTexture {
        irr::video::ITexture* texture;
        irr::video::ITexture* alphaTexture = nullptr;  // ETC1 alpha companion (unit 1)
        size_t sizeBytes;
        bool hasAlpha = false;
        // H02d: true if uploaded via uploadCompressedETC1 (companion-based
        // alpha), false if uploaded via uploadTexture (direct RGBA/DDS decode,
        // alpha lives in the main texture's own alpha channel).
        bool isETC1 = false;
        // P13: source category recorded at upload time (only populated when
        // ledgerEnabled_; "" otherwise), consulted by evictTexture() so the
        // eviction-ledger row can carry the same category as the original
        // upload row for that name.
        std::string category;
        // Valid only while `resident` (an evicted entry is not in lruOrder_).
        std::list<std::string>::iterator lruIterator;
        // B944: false once evict released the GL storage; the object lives on.
        bool resident = true;
        // B944: what a restore re-uploads from. Compressed = `compressed`
        // (pointers into a mapped EQE1 pack); Decoded = a TextureSourceProvider
        // by name; None = nothing (external registerTexture(), DDS direct).
        enum class Source : uint8_t { None, Compressed, Decoded };
        Source source = Source::None;
        CompressedEntry compressed{};
    };

    // Protects all state except decodedQueue_ (which has its own decodedQueueMutex_)
    mutable std::mutex mutex_;

    ConstrainedRendererConfig config_;
    irr::video::IVideoDriver* driver_;
    irr::scene::ISceneManager* smgr_ = nullptr;

    // Placeholder texture for evicted textures (created on demand)
    irr::video::ITexture* placeholderTexture_ = nullptr;

    // Texture cache: name -> uploaded texture entry
    std::map<std::string, CachedTexture> cache_;

    // Decoded but not yet uploaded: name -> decoded RGBA pixels
    std::map<std::string, DecodedEntry> decodedEntries_;

    // LRU order: front = oldest (evict first), back = newest
    std::list<std::string> lruOrder_;

    // Current memory usage
    size_t currentUsage_ = 0;

    // Statistics
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;
    size_t evictionCount_ = 0;
    size_t compressedUploadCount_ = 0;

    // Frozen flag - when true, no evictions occur
    bool frozen_ = false;

    // Mesh builder for entity texture registration (non-owning)
    ZoneMeshBuilder* meshBuilder_ = nullptr;

    // T04: decodedQueueMutex_, decodedQueue_, stagedUploads_, pendingDecodes_ removed.
    // Replaced by decodedEntries_ (decode) + uploadTexture() (upload).

    // Eviction listeners (non-owning)
    std::vector<TextureEvictionListener*> evictionListeners_;

    // B944: evicted texture object (main or alpha companion) -> its cache
    // key, for the driver's restore callback. Guarded by mutex_.
    std::map<const irr::video::ITexture*, std::string> evictedOwner_;
    std::vector<TextureSourceProvider*> sourceProviders_;  // non-owning
    size_t restoreCount_ = 0;
    size_t restoreFailureCount_ = 0;
    std::set<std::string> restoreFailureLogged_;  // one WARN per key

    // EQE1 flat pack loaders (non-owning, populated per zone).
    // P04: guarded by eqe1LoadersMutex_ (NOT mutex_) — addEQE1Loader is called
    // from prep-worker/equipment threads via getOrOpenEQE1 while the render
    // thread iterates the vector inside findCompressedEntry/getEQE1RawData;
    // with lazy global-archive opening those two now overlap routinely. A
    // separate mutex (not mutex_) because the lazy opener runs while mutex_ is
    // held and re-enters addEQE1Loader on the same thread.
    std::vector<const EQE1Loader*> eqe1Loaders_;
    mutable std::mutex eqe1LoadersMutex_;

    // Compressed entries pending GPU upload (from EQE1 lookup)
    std::map<std::string, CompressedEntry> compressedEntries_;

    // P04/T207: archive resolver for EQE1 lookups (see setEQE1ArchiveResolver).
    std::function<const EQE1Loader*(const std::string&)> eqe1ArchiveResolver_;

    // T214: see dumpEQE1RequestLog(). Guarded by eqe1RequestLogMutex_ because
    // findCompressedEntry/getEQE1RawData run on loader threads.
    mutable std::mutex eqe1RequestLogMutex_;
    mutable std::map<std::string, EQE1Request> eqe1RequestLog_;
    std::set<std::string> eqe1LateOpened_;  // T266; guarded by eqe1RequestLogMutex_
    void recordEQE1Request(const std::string& name, const EQE1Loader* servedBy,
                           bool rawData) const;

    // T202: names exempt from LRU eviction. See protectTexture().
    std::set<std::string> protectedTextures_;

    // P13: texture-upload audit ledger (see setLedgerEnabled/dumpLedger).
    // Disabled by default; when disabled, none of the record*() calls below
    // are ever made (checked at each call site) so this is a zero-cost,
    // zero-behavior-change addition when off.
    bool ledgerEnabled_ = false;
    TextureUploadLedger ledger_;
    // name -> (category, callsite) recorded by getOrLoad(), consumed by
    // whichever upload path (uploadTexture/uploadCompressedETC1/
    // uploadCompressedV2) actually promotes this name to a GPU texture.
    // Only populated/consulted when ledgerEnabled_.
    std::map<std::string, std::pair<std::string, std::string>> pendingLedgerTag_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_TEXTURE_CACHE_H
