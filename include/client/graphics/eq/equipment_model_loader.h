#ifndef EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H
#define EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H

#include "s3d_loader.h"
#include "zone_geometry.h"
#include "client/graphics/constrained_texture_cache.h"  // TextureEvictionListener
#include <irrlicht.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

// Equipment model data - geometry for a single equipment item
struct EquipmentModelData {
    std::shared_ptr<ZoneGeometry> geometry;
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    std::string modelName;  // e.g., "IT1", "IT156"
    int modelId = 0;        // The IT number (1, 156, etc.)
    std::string sourceArchive;   // e.g., "gequip5.s3d"
    std::string sourceWld;       // e.g., "gequip5.wld"
    std::string geometryName;    // e.g., "IT10653_DMSPRITEDEF"
    std::vector<std::string> textureNames;  // Textures used by this model
};

// Loads and caches equipment models from gequip S3D archives
//
// T126: self-registers as a TextureEvictionListener (mirrors
// AnimatedTextureManager/SkyRenderer's precedent) because meshCache_ is a
// PERSISTENT cache -- IMesh* objects survive zone unload/reload, kept alive
// by refcounting (addMeshRef/removeMeshRef), not rebuilt per zone the way
// object/region meshes are (see renderer_zone_load.cpp: those call
// buildTexturedMesh() fresh every zone load). If a cached mesh's own
// mesh-buffer material still points at a texture ConstrainedTextureCache
// evicts, EVERY future scene node built from that mesh
// (EntityRenderer::attachEquipment() -> smgr_->addMeshSceneNode(mesh, ...))
// inherits the dangling pointer the instant it's constructed -- Irrlicht's
// CMeshSceneNode constructor copies each mesh buffer's CURRENT material into
// its own per-node override array at that moment, so fixing only the
// already-attached nodes (EntityMeshTextureGuard's job) does not stop a
// brand new equip/re-equip from reintroducing the same stale pointer from
// this cache. onTextureEvicted() here fixes the root: the cached IMesh's own
// buffer materials, once, at eviction time -- see
// refactor/T126_uaf_attribution.md for the write-time evidence that named
// this specific gap (a live, normally-rendered IMeshSceneNode was the
// dominant unattributed UAF holder across the whole T122-T126 arc; equip
// nodes were the one entity-mesh scan target EntityMeshTextureGuard never
// actually covered despite T123's holder-inventory table claiming
// "equipment" was included).
class EquipmentModelLoader : public TextureEvictionListener {
public:
    EquipmentModelLoader(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                         irr::io::IFileSystem* fileSystem);
    ~EquipmentModelLoader();

    // Set constrained texture cache for LRU tracking of equipment textures.
    // Also (un)registers this loader as an eviction listener -- see class
    // doc comment above.
    void setConstrainedTextureCache(ConstrainedTextureCache* cache);

    // TextureEvictionListener. MUST NOT call back into
    // ConstrainedTextureCache (mutex_ is held by the calling frame) -- same
    // load-bearing constraint every other guard in this file family
    // documents; placeholder_ is resolved once, safely, in
    // setConstrainedTextureCache() before any eviction can occur.
    void onTextureEvicted(const std::string& name, irr::video::ITexture* texture) override;

    // Set the base path for EQ client files
    void setClientPath(const std::string& path);

    // Load equipment models from gequip.s3d and gequip2.s3d
    bool loadEquipmentArchives();

    // Get IT model number from database item ID
    // Returns -1 if item not found in the database
    // Database ITEM id -> IT model number, via willeq.db's item_models table
    // (data/item_models.json is its input; the JSON itself is never parsed at
    // runtime -- B824 deleted the in-memory map that used to hold it).
    // T498 (board B755/B823): this is for the OFFLINE TOOLS only --
    // tools/model_viewer and tools/hwcaps/ui_inventory_capture are the last
    // callers that genuinely hold an item id. No render site may use it:
    // EntityAppearance::equipment[] is a MATERIAL / IT model number for
    // every character, never a DB item id, and the table it reads disagrees
    // with the server on 2,128 of its 3,661 rows
    // (data/item_models_drift_baseline.txt).
    int getModelIdForItem(uint32_t databaseItemId) const;

    // Get equipment mesh by IT model ID
    // Returns nullptr if model not found
    irr::scene::IMesh* getEquipmentMeshByModelId(int modelId);

    // Get equipment mesh by equipment ID
    // T498 (B755/B823): equipmentId is an appearance MATERIAL, i.e. an IT
    // model number, for every character (spawn packets and wear changes, and
    // since T498 the local player's own slots). It is used directly; there is
    // no database-item-id branch.
    irr::scene::IMesh* getEquipmentMesh(uint32_t equipmentId);

    // Check if a model ID is a shield (IT200-IT299)
    static bool isShield(int modelId);

    // T126: test-only seam -- inject a mesh directly into meshCache_ so
    // tests can drive the REAL onTextureEvicted() path (via
    // ConstrainedTextureCache::clear()) against a real cached entry, the
    // same discipline test_object_node_texture_guard.cpp established
    // (exercise the real destruction path, not a synthetic call to the
    // listener interface). Takes ownership of the ONE reference the caller
    // hands in, exactly like getEquipmentMeshByModelId()'s own
    // `meshCache_[modelId] = mesh;` after buildMeshFromGeometry() (no extra
    // grab() -- the destructor's mesh->drop() per cached entry expects
    // exactly one owned reference). Not used by any non-test code path.
    void debugInjectCachedMeshForTest(int modelId, irr::scene::IMesh* mesh) {
        meshCache_[modelId] = mesh;
    }

    // Check if equipment archives have been loaded
    bool isLoaded() const { return archivesLoaded_; }

    // Get total number of indexed equipment models
    size_t getLoadedModelCount() const { return equipmentModelIndex_.size(); }

    // Get equipment model data by model ID (for debugging/inspection)
    // Triggers on-demand loading if model is indexed but not yet loaded
    const EquipmentModelData* getEquipmentModelData(int modelId);

    // Release raw texture data after GPU upload (frees CPU-side pixel data)
    // Returns the number of bytes freed
    size_t releaseRawTextureData();

    // Memory stats for /pmem reporting
    struct MemoryStats {
        size_t rawTextureBytes = 0;      // Raw texture data (CPU-side)
        size_t meshCacheCount = 0;       // Number of cached meshes
        size_t indexedModelCount = 0;    // Total models indexed from archives
        size_t loadedGeometryCount = 0;  // Models with geometry loaded on demand
        size_t geometryBytes = 0;        // ZoneGeometry data in loaded models
        size_t irrlichtMeshBytes = 0;    // Irrlicht IMesh vertex/index buffers
        size_t indexBytes = 0;           // Index data (model refs, texture refs)
    };
    MemoryStats getMemoryStats() const;

    // Reference counting for mesh cache eviction
    // Call addMeshRef when attaching equipment to an entity
    // Call removeMeshRef when detaching - mesh is evicted when refcount hits 0
    void addMeshRef(int modelId);
    void removeMeshRef(int modelId);

    // Index entry: where to find an equipment model (no geometry loaded)
    struct EquipmentModelRef {
        std::string archivePath;
        std::string wldName;
        std::string actorName;
        std::vector<std::string> textureNames;  // Textures used by this model
    };

    // Index entry: where to find an equipment texture
    struct EquipmentTextureRef {
        std::string archivePath;
        std::string entryName;
    };

    // Thread-safe: extract equipment model data without any Irrlicht calls.
    // Opens S3D archive, parses WLD, extracts geometry + raw textures.
    // Does NOT create Irrlicht meshes or upload textures.
    static std::shared_ptr<EquipmentModelData> extractEquipmentModelOffThread(
        const EquipmentModelRef& modelRef, int modelId);
    // T504: same result through this loader's parse cache (one open + one
    // parse per archive across consecutive items; see parseMutex_).
    std::shared_ptr<EquipmentModelData> extractEquipmentModelCached(
        const EquipmentModelRef& modelRef, int modelId);
    // The shared body: geometry + raw textures from an open archive and a
    // parsed WLD (no Irrlicht calls).
    static std::shared_ptr<EquipmentModelData> extractFromParsed(
        PfsArchive& archive, WldLoader& wldLoader, const EquipmentModelRef& modelRef, int modelId);

    // Getters for index access (triggers lazy-init if needed)
    const EquipmentModelRef* getModelRef(int modelId);
    const EquipmentTextureRef* getTextureRef(const std::string& name);

    // Cache pre-extracted equipment model data (from background thread prep)
    // This makes it available for getEquipmentMeshByModelId to build Irrlicht mesh
    void cacheEquipmentModelData(int modelId, std::shared_ptr<EquipmentModelData> data);

    // Accept pre-built index data from background thread (avoids re-parsing archives)
    // B824: the map argument went -- the item->model lookup is willeq.db-only
    // (WilleqDatabase::getItemModel()); the JSON map it used to adopt was
    // never read.
    void adoptIndex(std::map<int, EquipmentModelRef>&& modelIndex,
                    std::map<std::string, EquipmentTextureRef>&& textureIndex);

    // Build equipment index from archives (no GL, no Irrlicht — thread-safe)
    // clientPath must end with '/'
    // Returns true if any models were indexed
    static bool buildEquipmentIndex(
        const std::string& clientPath,
        std::map<int, EquipmentModelRef>& outModelIndex,
        std::map<std::string, EquipmentTextureRef>& outTextureIndex);
    // T504c Leg 2f: the archives the index reads, in priority order (first
    // holder of a model id wins). The original client's Resources/
    // GlobalLoad.txt names GEquip, GEquip2-5 and GEquip8 (Will, 2026-09-22:
    // load what GlobalLoad.txt loads, prune the post-Velious files); the
    // loy/ldon/gates/frog/omens/don/dod entries it also lists are later
    // expansions and dropped, and vequip/lgequip*/gequip6/obequip_lexit are
    // not in GlobalLoad.txt at all. The tests iterate this list.
    static const std::vector<std::string>& indexedArchiveNames();

private:
    // Lazy-init: build equipment index on first use (if not already loaded via adoptIndex)
    void ensureIndexLoaded();

    // Index-only scan of a single S3D archive (no geometry or texture data loaded)
    bool loadEquipmentArchive(const std::string& archivePath);

    // Load a single equipment model on demand from its indexed archive
    bool loadEquipmentModelOnDemand(int modelId);

    // Load a single equipment texture on demand from its indexed archive
    std::shared_ptr<TextureInfo> getEquipmentTexture(const std::string& lowerName);
    std::shared_ptr<TextureInfo> getEquipmentTextureLocked(const std::string& lowerName);  // parseMutex_ held

    // Build an Irrlicht mesh from equipment geometry
    irr::scene::IMesh* buildMeshFromGeometry(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures);

    // Parse IT model ID from actor name (e.g., "IT156_ACTORDEF" -> 156)
    static int parseModelIdFromActorName(const std::string& actorName);

    // Static helper: index a single archive (thread-safe, no Irrlicht)
    static bool indexEquipmentArchive(
        const std::string& archivePath,
        std::map<int, EquipmentModelRef>& outModelIndex,
        std::map<std::string, EquipmentTextureRef>& outTextureIndex);

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;
    std::unique_ptr<ZoneMeshBuilder> meshBuilder_;

    std::string clientPath_;

    // Model index: modelId -> location (populated at startup, no geometry)
    std::map<int, EquipmentModelRef> equipmentModelIndex_;

    // Texture index: lowercase name -> archive location (populated at startup, no data)
    std::map<std::string, EquipmentTextureRef> textureIndex_;

    // IT model ID -> equipment model data (populated on demand)
    std::map<int, std::shared_ptr<EquipmentModelData>> equipmentModels_;

    // IT model ID -> cached Irrlicht mesh
    std::map<int, irr::scene::IMesh*> meshCache_;

    // On-demand loaded textures (cache to avoid re-extracting from archives)
    std::map<std::string, std::shared_ptr<TextureInfo>> textures_;

    // Reference counts for cached meshes (model ID -> ref count)
    std::map<int, int> meshRefCounts_;

    // T114: plain bools read/written across the double-checked-locking fast
    // path in ensureIndexLoaded() (worker thread via EntityPrepWorker::
    // prepEquipmentModels(), main thread via loadEquipmentModelOnDemand()/
    // getEquipmentTexture()) -- the fast-path read races the locked write,
    // UB for a non-atomic bool even though it's unlikely to tear in
    // practice. atomic<bool> makes the double-checked-locking idiom actually
    // well-defined; initMutex_ still guards the one-time population of
    // equipmentModelIndex_/textureIndex_ itself.
    std::atomic<bool> archivesLoaded_{false};
    std::mutex initMutex_;  // Guards ensureIndexLoaded() for thread safety

    // Constrained texture cache for LRU tracking (non-owning, owned by IrrlichtRenderer)
    ConstrainedTextureCache* constrainedTextureCache_ = nullptr;

    // T504: the last archive opened / WLD parsed by the on-demand paths.
    // loadEquipmentModelOnDemand() used to open the whole gequip*.s3d and
    // parse its ENTIRE WLD for ONE item, per distinct item, and
    // getEquipmentTexture() re-opened the archive per texture (7 MiB block
    // read + ~10 MiB parse each time; equipment ledger peak 10 MiB in every
    // zone). Consecutive requests in a zone load mostly hit the same
    // archive, so one open handle and one parsed WLD are kept until
    // releaseParseCache() (called from releaseRawTextureData(), i.e. when
    // the progressive load is done) drops them. Guarded by parseMutex_,
    // which also serialises the two on-demand entry points (prep worker and
    // zone-load thread).
    std::mutex parseMutex_;
    std::string cachedArchivePath_;
    std::unique_ptr<PfsArchive> cachedArchive_;
    std::string cachedWldName_;
    std::unique_ptr<WldLoader> cachedWld_;
    // Cache statistics, reported (INFO) by releaseParseCache().
    size_t parseCacheRequests_ = 0, parseCacheOpens_ = 0, parseCacheParses_ = 0;
    PfsArchive* openArchiveLocked(const std::string& archivePath);   // parseMutex_ held
    WldLoader* parsedWldLocked(const std::string& archivePath, const std::string& wldName);  // parseMutex_ held
public:
    // Drop the cached archive handle and parsed WLD (see above). Safe to call
    // any time; the next on-demand request re-opens.
    void releaseParseCache();
private:

    // T126: resolved once in setConstrainedTextureCache(), before any
    // eviction can occur -- see class doc comment and onTextureEvicted().
    irr::video::ITexture* placeholder_ = nullptr;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H
