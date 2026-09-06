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

    // Load item ID to model ID mapping from JSON file
    // Returns number of mappings loaded, or -1 on error
    int loadItemModelMapping(const std::string& jsonPath);

    // Load equipment models from gequip.s3d and gequip2.s3d
    bool loadEquipmentArchives();

    // Get IT model number from database item ID
    // Returns -1 if item not found in mapping
    int getModelIdForItem(uint32_t databaseItemId) const;

    // Get equipment mesh by IT model ID
    // Returns nullptr if model not found
    irr::scene::IMesh* getEquipmentMeshByModelId(int modelId);

    // Get equipment mesh by equipment ID
    // First tries to look up as database item ID, then as direct IT model ID
    // NPC spawn packets use direct model IDs; player items use database IDs
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

    // Get number of item ID mappings
    size_t getMappingCount() const { return itemToModelMap_.size(); }

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
        size_t mappingCount = 0;         // Number of item-to-model mappings
        size_t geometryBytes = 0;        // ZoneGeometry data in loaded models
        size_t irrlichtMeshBytes = 0;    // Irrlicht IMesh vertex/index buffers
        size_t indexBytes = 0;           // Index data (model refs, texture refs, mappings)
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

    // Getters for index access (triggers lazy-init if needed)
    const EquipmentModelRef* getModelRef(int modelId);
    const EquipmentTextureRef* getTextureRef(const std::string& name);

    // Cache pre-extracted equipment model data (from background thread prep)
    // This makes it available for getEquipmentMeshByModelId to build Irrlicht mesh
    void cacheEquipmentModelData(int modelId, std::shared_ptr<EquipmentModelData> data);

    // Accept pre-built index data from background thread (avoids re-parsing archives)
    void adoptIndex(std::map<int, EquipmentModelRef>&& modelIndex,
                    std::map<std::string, EquipmentTextureRef>&& textureIndex,
                    std::map<uint32_t, int>&& itemToModelMap);

    // Build equipment index from archives (no GL, no Irrlicht — thread-safe)
    // clientPath must end with '/'
    // Returns true if any models were indexed
    static bool buildEquipmentIndex(
        const std::string& clientPath,
        std::map<int, EquipmentModelRef>& outModelIndex,
        std::map<std::string, EquipmentTextureRef>& outTextureIndex);

    // Load item-to-model mapping from JSON (thread-safe, no Irrlicht)
    // Returns number of mappings loaded, or -1 on error
    static int loadItemModelMappingStatic(const std::string& jsonPath,
                                          std::map<uint32_t, int>& outMap);

private:
    // Lazy-init: build equipment index on first use (if not already loaded via adoptIndex)
    void ensureIndexLoaded();

    // Index-only scan of a single S3D archive (no geometry or texture data loaded)
    bool loadEquipmentArchive(const std::string& archivePath);

    // Load a single equipment model on demand from its indexed archive
    bool loadEquipmentModelOnDemand(int modelId);

    // Load a single equipment texture on demand from its indexed archive
    std::shared_ptr<TextureInfo> getEquipmentTexture(const std::string& lowerName);

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

    // Database item ID -> IT model number mapping
    std::map<uint32_t, int> itemToModelMap_;

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

    // T126: resolved once in setConstrainedTextureCache(), before any
    // eviction can occur -- see class doc comment and onTextureEvicted().
    irr::video::ITexture* placeholder_ = nullptr;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H
