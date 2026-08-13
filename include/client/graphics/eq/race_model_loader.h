#ifndef EQT_GRAPHICS_RACE_MODEL_LOADER_H
#define EQT_GRAPHICS_RACE_MODEL_LOADER_H

#include "s3d_loader.h"
#include "zone_geometry.h"
#include "composite_texture_data.h"
#include "animated_mesh_scene_node.h"
#include "race_codes.h"
#include "equipment_textures.h"
#include <irrlicht.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <list>

// Forward declarations
namespace EQT { namespace Graphics { struct EntityAppearance; class GraphicsArchiveIndex; class ConstrainedTextureCache; } }

namespace EQT {
namespace Graphics {

// Pre-decoded texture data (CPU-side ARGB pixels, ready for GPU upload)
struct DecodedTexture {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> argbPixels;  // Pre-decoded ARGB for GPU upload
    bool hasAlpha = false;
};

// Race model data - combined geometry for a race/gender combo
struct RaceModelData {
    std::shared_ptr<ZoneGeometry> combinedGeometry;      // Skinned geometry (bone transforms applied)
    std::shared_ptr<ZoneGeometry> rawGeometry;           // Raw geometry (no bone transforms, for animation)
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    std::string raceName;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    float scale = 1.0f;

    // Animation data
    std::shared_ptr<CharacterSkeleton> skeleton;     // Skeleton with animation tracks
    std::vector<VertexPiece> vertexPieces;           // Vertex-to-bone mapping for skinning

    // Pre-decoded textures (background thread decodes DDS → ARGB, main thread uploads to GPU)
    std::vector<DecodedTexture> decodedTextures;

    // Composite texture data (if available — enables 1-draw-call entity rendering)
    std::shared_ptr<CompositeTextureData> compositeData;  // UV remap metadata
    std::string compositeBmpPath;  // EQE1 entry name for composite texture (e.g., "hum_hs_def_0_0")
};

// Loads and caches character models by race ID
class RaceModelLoader {
public:
    // Armor texture lazy-loading: index maps texture name -> archive location
    struct ArmorTextureRef {
        std::string archivePath;   // Full path to the S3D archive
        std::string entryName;     // Original filename inside the archive
    };

    RaceModelLoader(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    irr::io::IFileSystem* fileSystem);
    ~RaceModelLoader();

    // Set the base path for EQ client files
    void setClientPath(const std::string& path);

    // Set graphics archive index for on-demand model loading (deferred mode)
    void setGraphicsArchiveIndex(GraphicsArchiveIndex* index) { graphicsArchiveIndex_ = index; }

    // Load all global character models from global_chr.s3d
    bool loadGlobalModels();

    // Load additional numbered global#_chr.s3d files (global2-global7)
    bool loadNumberedGlobalModels();

    // Load armor textures from global17-23_amr.s3d
    bool loadArmorTextures();

    // Load zone-specific character models from zone_chr.s3d
    bool loadZoneModels(const std::string& zoneName);

    // Set the current zone name (for zone-specific model loading)
    void setCurrentZone(const std::string& zoneName);

    // Check if a race model is available
    bool hasRaceModel(uint16_t raceId, uint8_t gender = 0) const;

    // Get an Irrlicht mesh for a specific race/gender
    // Returns nullptr if not found (caller should use placeholder)
    irr::scene::IMesh* getMeshForRace(uint16_t raceId, uint8_t gender = 0);

    // Get an Irrlicht mesh for a specific race/gender with appearance variants
    // headVariant: which head mesh to use (0 = default, maps to HUMHE00, HUMHE01, etc.)
    // bodyVariant: which body mesh to use (0 = default, maps to HUM, HUM01, etc.)
    irr::scene::IMesh* getMeshForRaceWithAppearance(uint16_t raceId, uint8_t gender,
                                                     uint8_t headVariant, uint8_t bodyVariant);

    // Get an animated mesh for a specific race/gender
    // Returns nullptr if not found or no animation data available
    // compositeAllowed (P08d): per-entity eligibility gate for the composite-texture-sheet
    // mesh path. Defaults to true so every EXISTING caller (model_viewer, the composite
    // audit tool, the P08c benchmark tools) keeps its prior (dormant-unless-config-enabled)
    // behavior unchanged. The production entity path (entity_renderer.cpp) is the only
    // caller that explicitly passes false for ineligible entities (players, NPCs with
    // per-slot equipment overrides or tints) — see EntityRenderer::buildEntityMesh.
    // Composite-allowed and composite-disallowed builds for the SAME race/gender are
    // cached in SEPARATE maps (compositeAnimatedMeshCache_ vs animatedMeshCache_) so an
    // eligible NPC and an ineligible player of the same race never share a cached mesh.
    EQAnimatedMesh* getAnimatedMeshForRace(uint16_t raceId, uint8_t gender = 0, bool compositeAllowed = true);

    // Get an animated mesh with appearance-based variant selection
    // headVariant: which head mesh to use (0 = default)
    // bodyVariant: which body mesh to use (0 = default)
    // textureVariant: equipment texture (0=naked, 1=leather, 2=chain, 3=plate, 10+=robes)
    // compositeAllowed: see getAnimatedMeshForRace. Only consulted when headVariant==0 &&
    // bodyVariant==0 (entity_composite_builder only bakes the default body mesh geometry;
    // head/body mesh variants are structurally ineligible for a composite sheet).
    EQAnimatedMesh* getAnimatedMeshWithAppearance(uint16_t raceId, uint8_t gender,
                                                   uint8_t headVariant, uint8_t bodyVariant,
                                                   uint8_t textureVariant = 0,
                                                   bool compositeAllowed = true);

    // Create an animated mesh scene node for a race
    // The caller is responsible for adding the node to the scene
    EQAnimatedMeshSceneNode* createAnimatedNode(uint16_t raceId, uint8_t gender,
                                                  irr::scene::ISceneNode* parent = nullptr,
                                                  irr::s32 id = -1,
                                                  bool compositeAllowed = true);

    // Create an animated mesh scene node with appearance-based variants
    EQAnimatedMeshSceneNode* createAnimatedNodeWithAppearance(uint16_t raceId, uint8_t gender,
                                                               uint8_t headVariant, uint8_t bodyVariant,
                                                               irr::scene::ISceneNode* parent = nullptr,
                                                               irr::s32 id = -1,
                                                               bool compositeAllowed = true);

    // Create an animated mesh scene node with full equipment appearance
    // compositeAllowed (P08d): the production per-entity eligibility gate. Callers other
    // than entity_renderer.cpp should leave this at its default (true).
    EQAnimatedMeshSceneNode* createAnimatedNodeWithEquipment(uint16_t raceId, uint8_t gender,
                                                              const EntityAppearance& appearance,
                                                              irr::scene::ISceneNode* parent = nullptr,
                                                              irr::s32 id = -1,
                                                              bool compositeAllowed = true);

    // Get race model data
    std::shared_ptr<RaceModelData> getRaceModelData(uint16_t raceId, uint8_t gender = 0);

    // P13b: ensures loadedModels_[raceId,gender] is populated (running the same
    // S3D-archive search/load fallback chain getMeshForRace() used to run inline),
    // WITHOUT building a textured static mesh (buildTexturedMesh()'s eager per-part
    // texture decode/upload) as a side effect. getMeshForRace() calls this then
    // builds+caches its static mesh as before; getAnimatedMeshForRace() (which
    // previously called getMeshForRace() purely for this side effect, discarding
    // the returned mesh) now calls this directly so an eligible entity whose race
    // got a composite-sheet hit doesn't have its "deferred" per-part textures
    // eagerly decoded anyway by the static-mesh path. Returns nullptr if the race
    // model cannot be found by any search path (caller should use placeholder).
    std::shared_ptr<RaceModelData> ensureModelDataLoaded(uint16_t raceId, uint8_t gender = 0);

    // Get mesh builder (for registering pre-uploaded textures in multi-frame pipeline)
    ZoneMeshBuilder* getMeshBuilder() { return meshBuilder_.get(); }

    // Set constrained texture cache for direct texture loading via getOrLoad()
    void setConstrainedTextureCache(EQT::Graphics::ConstrainedTextureCache* cache) { constrainedTextureCache_ = cache; }

    // Set composite entity texture path (directory containing composite BMP + JSON from entity_composite_builder)
    void setCompositeEntityPath(const std::string& path) { compositeEntityPath_ = path; }

    // Background-safe: loads S3D model data + merges animations into staging cache.
    // Does NOT create textures, Irrlicht meshes, or scene nodes (no GL calls).
    // Reads from immutable archives (globalCharacters_, etc.) and writes to
    // preparedModelData_ (thread-safe staging map). Call promotePreparedModels()
    // on the main thread to move data into loadedModels_ before getMeshForRace().
    bool preloadModelData(uint16_t raceId, uint8_t gender, int textureVariant = 0);

    // Background-safe: loads variant model data (S3D load + character search +
    // part combining + animation merge) for zone-specific variants (e.g., QCM).
    // Stores result in variantModels_ (protected by variantModelsMutex_).
    // Does NOT create textures, Irrlicht meshes, or scene nodes (no GL calls).
    bool preloadVariantModel(uint16_t raceId, uint8_t gender,
                             uint8_t headVariant, uint8_t bodyVariant);

    // Get variant model data from variantModels_ cache (thread-safe).
    // Returns nullptr if not cached.
    std::shared_ptr<RaceModelData> getVariantModelData(uint16_t raceId, uint8_t gender,
                                                        uint8_t headVariant, uint8_t bodyVariant);

    // Check if model data is available in either the main cache (loadedModels_)
    // or the staging cache (preparedModelData_). Thread-safe.
    // cacheKey = (raceId << 8) | gender
    bool isModelDataCached(uint32_t cacheKey) const;

    // T94: Returns true if a prior preloadModelData() call for this
    // race/gender already walked the FULL search chain (zoneCharacters_,
    // otherChrCaches_, JSON-specified S3D, global_chr.s3d, global2-7_chr.s3d)
    // and found nothing. A permanent negative memo — the S3D archives are
    // static for the process lifetime, so a race with no model at startup
    // has none at shutdown; there is no invalidation path to build. Callers
    // (loadZoneStep8a_BaseModelPrep, EntityPrepWorker::processRequest) use
    // this to skip calling preloadModelData() entirely for every subsequent
    // entity of an already-proven-absent race/gender, instead of rediscovering
    // "no model" from scratch per entity (T94's measured cost: 130/452
    // entities in Nektulos, 93.8% of Step 8's wall time, before this cache).
    // cacheKey = (raceId << 8) | gender (variant/texture index do not
    // participate — every preloadModelData() caller passes textureVariant=0,
    // and the parameter only affects a post-success composite lookup, never
    // whether the base model search finds a match). Thread-safe (same
    // preparedDataMutex_ preloadModelData already holds for its other
    // staging-cache checks).
    bool isRaceModelKnownMissing(uint32_t cacheKey) const;

    // Move prepared model data from staging cache to main cache.
    // Call this on the main thread before getMeshForRace() to make
    // background-preloaded data available for mesh building.
    void promotePreparedModels();

    // Get race scale factor (some races are larger/smaller)
    // Delegates to the free function in race_codes.h
    float getRaceScale(uint16_t raceId) const { return EQT::Graphics::getRaceScale(raceId); }

    // Get the S3D filename for a race (for loading from zone archives)
    // Delegates to the free function in race_codes.h
    static std::string getRaceModelFilename(uint16_t raceId, uint8_t gender) {
        return EQT::Graphics::getRaceModelFilename(raceId, gender);
    }

    // Get a 3-letter race code (HUM, ELF, DWF, etc.)
    // Delegates to the free function in race_codes.h
    static std::string getRaceCode(uint16_t raceId) {
        return EQT::Graphics::getRaceCode(raceId);
    }

    // Get number of loaded race models
    size_t getLoadedModelCount() const { return loadedModels_.size(); }

    // Release raw texture data after GPU upload (frees CPU-side pixel data)
    // Armor texture cache is also released (reconstructible from index)
    // Returns the number of bytes freed
    size_t releaseRawTextureData();

    // Memory stats for /pmem reporting
    struct MemoryStats {
        size_t globalTextureBytes = 0;     // Raw bytes in globalTextures_
        size_t numberedTextureBytes = 0;   // Raw bytes in numberedGlobalTextures_
        size_t armorTextureBytes = 0;      // Raw bytes in armorTextureCache_
        size_t zoneTextureBytes = 0;       // Raw bytes in zoneTextures_
        size_t otherChrTextureBytes = 0;   // Raw bytes in otherChrCaches_ textures
        size_t loadedModelCount = 0;       // Number of loaded race models
        size_t meshCacheCount = 0;         // meshCache_ entries
        size_t variantMeshCacheCount = 0;  // variantMeshCache_ entries
        size_t animatedMeshCacheCount = 0; // animatedMeshCache_ + variantAnimatedMeshCache_
        size_t armorTextureCount = 0;      // Number of armor textures

        // Geometry and skeleton tracking (previously untracked)
        size_t modelGeometryBytes = 0;     // ZoneGeometry data in loadedModels_ + variantModels_
        size_t modelSkeletonBytes = 0;     // CharacterSkeleton + animation tracks in loadedModels_
        size_t characterModelBytes = 0;    // CharacterModel data (global + numbered + zone + other)
        size_t animatedMeshBytes = 0;      // EQAnimatedMesh working data (originalVertices_ etc.)
        size_t irrlichtMeshBytes = 0;      // Irrlicht IMesh vertex/index buffers in mesh caches
    };
    MemoryStats getMemoryStats() const;

    // P08d: composite sheet hit/miss counters, tracked across all composite-ALLOWED mesh
    // build attempts (getAnimatedMeshForRace / getAnimatedMeshWithAppearance) since the
    // last reset. "Hit" = a pre-baked sheet was found and successfully built into a
    // composite mesh; "miss" = compositeAllowed was true but no usable sheet existed
    // (missing JSON, texture not yet uploaded, or build failure) and the call fell back
    // to the multi-buffer path. Used for the per-zone-load adoption summary log.
    size_t getCompositeSheetHits() const { return compositeSheetHits_; }
    size_t getCompositeSheetMisses() const { return compositeSheetMisses_; }
    void resetCompositeAdoptionStats() { compositeSheetHits_ = 0; compositeSheetMisses_ = 0; }

    // Reference counting for mesh cache eviction
    // Call addMeshRef when creating an entity with this race/gender
    // Call removeMeshRef when removing that entity
    void addMeshRef(uint16_t raceId, uint8_t gender);
    void removeMeshRef(uint16_t raceId, uint8_t gender);

    // Set maximum cached _chr.s3d entries (0 = unlimited)
    void setMaxChrCacheEntries(size_t max) { maxChrCacheEntries_ = max; }

    // Old models mode - when true, only load from global_chr.s3d (classic models)
    // When false, prefer race-specific S3D files (Luclin+ models)
    void setUseOldModels(bool useOld);
    bool isUsingOldModels() const { return useOldModels_; }

    // Clear cached meshes (call after toggling old/new models)
    void clearCache();

    // Clear mesh caches for zone transition (keeps model data, forces fresh mesh/texture rebuild)
    void clearMeshCaches();

    // Build a composite mesh from geometry + pre-uploaded atlas texture + UV metadata.
    // Public interface for dynamic atlas pipeline (audit tool, runtime atlas builder).
    irr::scene::IMesh* buildCompositeEntityMesh(
        const std::shared_ptr<ZoneGeometry>& geometry,
        irr::video::ITexture* compositeTexture,
        const CompositeTextureData& compositeData) {
        return buildCompositeMeshFromGeometry(geometry, compositeTexture, compositeData);
    }

private:
    // Load model from a specific S3D file
    bool loadModelFromS3D(const std::string& s3dPath, uint16_t raceId, uint8_t gender);

    // Load model from global_chr.s3d by searching for race code
    bool loadModelFromGlobalChr(uint16_t raceId, uint8_t gender);

    // Load model from a numbered global#_chr.s3d file
    bool loadModelFromNumberedGlobal(int globalNum, uint16_t raceId, uint8_t gender);

    // Load model from zone-specific _chr.s3d file (clears cache, for current game zone)
    bool loadModelFromZoneChr(const std::string& zoneName, uint16_t raceId, uint8_t gender);

    // Load model from a chr file using otherChrCaches_ (preserves cache, for JSON-specified files)
    bool loadModelFromCachedChr(const std::string& chrFilename, uint16_t raceId, uint8_t gender);

    // Load model using GraphicsArchiveIndex for targeted per-archive loading
    bool loadModelFromArchiveIndex(uint16_t raceId, uint8_t gender);

    // Search all loaded global character archives for a model
    bool searchAllGlobalsForModel(uint16_t raceId, uint8_t gender);

    // Search all zone _chr.s3d files in the client directory for a model
    bool searchZoneChrFilesForModel(uint16_t raceId, uint8_t gender);

    // Build an Irrlicht mesh from geometry data
    // bodyTextureVariant: 0=naked, 1=leather, 2=chain, 3=plate, 10+=robes
    // raceCode: 3-letter code like "QCF", "HUM" for texture name transformation
    irr::scene::IMesh* buildMeshFromGeometry(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint8_t bodyTextureVariant = 0,
        const std::string& raceCode = "");

    // Build a single-buffer mesh using composite texture sheet (1 draw call per entity).
    // Remaps UVs to composite sheet coordinates and populates vertexMappingForAnimation_.
    // compositeTexture: pre-uploaded composite sheet ITexture*
    // compositeData: UV remap metadata from entity_composite_builder JSON
    irr::scene::IMesh* buildCompositeMeshFromGeometry(
        const std::shared_ptr<ZoneGeometry>& geometry,
        irr::video::ITexture* compositeTexture,
        const CompositeTextureData& compositeData);

    // Build an Irrlicht mesh with equipment texture overrides
    irr::scene::IMesh* buildMeshWithEquipment(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        const std::string& raceCode,
        const uint32_t* equipment);  // Array of 9 material IDs

    // Create cache key for race/gender combo
    static uint32_t makeCacheKey(uint16_t raceId, uint8_t gender) {
        return (static_cast<uint32_t>(raceId) << 8) | gender;
    }

    // Create cache key for race/gender/variant combo (includes head, body, and texture variant)
    static uint64_t makeVariantCacheKey(uint16_t raceId, uint8_t gender, uint8_t headVariant, uint8_t bodyVariant, uint8_t textureVariant = 0) {
        return (static_cast<uint64_t>(raceId) << 32) |
               (static_cast<uint64_t>(gender) << 24) |
               (static_cast<uint64_t>(headVariant) << 16) |
               (static_cast<uint64_t>(bodyVariant) << 8) |
               textureVariant;
    }

    // Load model from global_chr.s3d with specific variants
    bool loadModelFromGlobalChrWithVariants(uint16_t raceId, uint8_t gender,
                                            uint8_t headVariant, uint8_t bodyVariant);

    // Load model from global_chr.s3d with specific variants including raw geometry for animation
    bool loadModelFromGlobalChrWithVariantsForAnimation(uint16_t raceId, uint8_t gender,
                                                         uint8_t headVariant, uint8_t bodyVariant);

    // Build a merged texture map from all sources (global + numbered globals + zone)
    // Order: global_chr.s3d -> global2-7_chr.s3d (new only) -> zone_chr.s3d (overrides)
    std::map<std::string, std::shared_ptr<TextureInfo>> getMergedTextures();

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;
    std::unique_ptr<ZoneMeshBuilder> meshBuilder_;

    std::string clientPath_;

    // Graphics archive index for on-demand loading (deferred mode, non-owning)
    GraphicsArchiveIndex* graphicsArchiveIndex_ = nullptr;

    // Constrained texture cache for direct texture loading (non-owning)
    EQT::Graphics::ConstrainedTextureCache* constrainedTextureCache_ = nullptr;

    // Composite entity texture directory (from entity_composite_builder output)
    std::string compositeEntityPath_;

    // Staging cache for background-preloaded model data.
    // Written by EntityPrepWorker thread (preloadModelData).
    // Promoted to loadedModels_ on main thread via promotePreparedModels().
    mutable std::mutex preparedDataMutex_;
    std::map<uint32_t, std::shared_ptr<RaceModelData>> preparedModelData_;

    // T94: negative-result memo for preloadModelData()'s full search chain.
    // Key present => that race/gender has no model anywhere; value is the
    // number of entities that have hit the negative cache so far (for the
    // zone-load summary, so the once-per-race log line can report how many
    // entities were affected without re-searching). Guarded by
    // preparedDataMutex_, same as preparedModelData_/loadedModels_ staging
    // reads elsewhere in this class.
    std::unordered_map<uint32_t, size_t> knownMissingRaceGenders_;

    // P13b: per-part texture names DEFERRED (decode/upload skipped) because
    // preloadModelData() got a composite-sheet HIT for that (race, gender) —
    // see race_model_loader.cpp's T05 loop guard. Populated under
    // preparedDataMutex_ (same lock preloadModelData already holds when
    // touching preparedModelData_). Never cleared: an entry stays "deferred"
    // for the RaceModelLoader's whole lifetime (races persist across zone
    // transitions in loadedModels_/preparedModelData_, and a still-ineligible
    // entity of this race could appear in ANY future zone). Consulted by
    // releaseRawTextureData() (model_loading.cpp) to keep these specific raw
    // TextureInfo bytes alive across the releaseTextureDataAfterUpload
    // memory-reclaim pass, so the on-demand load (buildMeshFromGeometry /
    // createAnimatedNodeWithEquipment's texture-swap, mesh_building.cpp /
    // animated_mesh_creation.cpp) has real bytes to decode from whenever an
    // ineligible entity of this race first actually needs one.
    std::set<std::string> deferredPerPartTextureNames_;

    // Search character archive for race model, build RaceModelData without modifying caches.
    // Pure computation helper for preloadModelData() (thread-safe reads only).
    std::shared_ptr<RaceModelData> buildModelDataFromCharacters(
        const std::vector<std::shared_ptr<CharacterModel>>& characters,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint16_t raceId, uint8_t gender) const;

    // Cache of loaded race model data (main thread only — no mutex needed)
    std::map<uint32_t, std::shared_ptr<RaceModelData>> loadedModels_;

    // Cache of Irrlicht meshes (separate from model data for memory management)
    std::map<uint32_t, irr::scene::IMesh*> meshCache_;

    // Cache of animated meshes (non-composite / multi-buffer builds; also the fallback
    // destination when a composite-allowed build has no sheet available)
    std::map<uint32_t, EQAnimatedMesh*> animatedMeshCache_;

    // Cache of variant animated meshes (key includes head/body variant)
    std::map<uint64_t, EQAnimatedMesh*> variantAnimatedMeshCache_;

    // P08d: composite-mesh caches, kept SEPARATE from animatedMeshCache_/
    // variantAnimatedMeshCache_ so the same race/gender/variant can have both a composite
    // build (for eligible entities) and a multi-buffer build (for ineligible entities)
    // cached side by side without one clobbering the other. Same key schemes as their
    // non-composite counterparts (makeCacheKey / makeVariantCacheKey).
    std::map<uint32_t, EQAnimatedMesh*> compositeAnimatedMeshCache_;
    std::map<uint64_t, EQAnimatedMesh*> compositeVariantAnimatedMeshCache_;

    // P08d: composite sheet adoption counters (see getCompositeSheetHits/Misses above)
    size_t compositeSheetHits_ = 0;
    size_t compositeSheetMisses_ = 0;

    // Cache for variant-specific meshes (key includes head/body variant)
    // variantModels_ is written by worker thread (preloadVariantModel) and read by
    // render thread (getAnimatedMeshWithAppearance) — protected by variantModelsMutex_
    mutable std::mutex variantModelsMutex_;
    std::map<uint64_t, std::shared_ptr<RaceModelData>> variantModels_;
    std::map<uint64_t, irr::scene::IMesh*> variantMeshCache_;

    // Global character data loaded from global_chr.s3d
    std::vector<std::shared_ptr<CharacterModel>> globalCharacters_;
    std::map<std::string, std::shared_ptr<TextureInfo>> globalTextures_;
    bool globalModelsLoaded_ = false;

    // Numbered global character data (global2-7_chr.s3d)
    std::map<int, std::vector<std::shared_ptr<CharacterModel>>> numberedGlobalCharacters_;
    std::map<int, std::map<std::string, std::shared_ptr<TextureInfo>>> numberedGlobalTextures_;
    bool numberedGlobalsLoaded_ = false;

    // Armor texture lazy-loading index (lowercase name -> ref)
    std::map<std::string, ArmorTextureRef> armorTextureIndex_;
    std::map<std::string, std::shared_ptr<TextureInfo>> armorTextureCache_; // On-demand loaded
    bool armorTexturesLoaded_ = false;

    // Load a single armor texture on demand from its archive
    std::shared_ptr<TextureInfo> getArmorTexture(const std::string& lowerName);

    // Zone-specific character data
    std::string currentZoneName_;
    std::vector<std::shared_ptr<CharacterModel>> zoneCharacters_;
    std::map<std::string, std::shared_ptr<TextureInfo>> zoneTextures_;
    bool zoneModelsLoaded_ = false;

    // Cached merged textures (performance optimization)
    mutable std::map<std::string, std::shared_ptr<TextureInfo>> cachedMergedTextures_;
    mutable bool mergedTexturesCacheValid_ = false;

    // Old models mode (classic models from global_chr.s3d only)
    bool useOldModels_ = true;  // Default to old models

    // Cache for other _chr.s3d files loaded during searchZoneChrFilesForModel
    // Key is lowercase filename (e.g., "crushbone_chr.s3d")
    // Written by worker thread (preloadVariantModel) and render thread
    // (loadModelFromGlobalChrWithVariantsForAnimation) — protected by otherChrCacheMutex_
    struct OtherChrCache {
        std::vector<std::shared_ptr<CharacterModel>> characters;
        std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    };
    mutable std::mutex otherChrCacheMutex_;
    std::map<std::string, OtherChrCache> otherChrCaches_;
    size_t maxChrCacheEntries_ = 0;          // 0 = unlimited
    std::list<std::string> chrCacheLruOrder_; // Front = most recently used

    // T91: the ONLY correct way to insert into otherChrCaches_. Stores the
    // entry, registers it in chrCacheLruOrder_, and evicts the
    // least-recently-loaded entry if over maxChrCacheEntries_ -- exactly
    // what tryLoadFromArchive's lambda (preloadModelData, race_model_loader.cpp)
    // already did inline. Before T91, five insertion sites in
    // variant_loading.cpp (preloadVariantModel's JSON-specified-S3D and
    // animation-source-S3D loads, both in the primary and global-fallback
    // branches) wrote `otherChrCaches_[key] = ...` directly, bypassing this
    // accounting entirely: those entries grew otherChrCaches_.size() without
    // ever entering chrCacheLruOrder_, so they could never be evicted, while
    // silently pushing otherChrCaches_.size() over maxChrCacheEntries_ and
    // forcing the *tracked* entries (the global_chr.s3d/global2-7_chr.s3d
    // base-model fallback chain preloadModelData legitimately needs) to be
    // evicted and reloaded from disk far more often than the configured cap
    // should allow -- see refactor/T91_entity_prepare_gate.md Review for the
    // measured thrash this caused. Caller must already hold
    // otherChrCacheMutex_ (every existing call site already does).
    void storeOtherChrCacheLocked(const std::string& lowerFilename,
                                   std::vector<std::shared_ptr<CharacterModel>> characters,
                                   std::map<std::string, std::shared_ptr<TextureInfo>> textures);

    // Reference counts for cached meshes (cache key -> ref count)
    std::map<uint32_t, int> meshRefCounts_;

    // Temporary storage for vertex data during animated mesh building
    // (populated by buildMeshFromGeometry, consumed by getAnimatedMeshForRace)
    std::vector<irr::video::S3DVertex> originalVerticesForAnimation_;
    std::vector<VertexMappingList> vertexMappingForAnimation_;  // one-to-many, see P08D-RETAKE note
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RACE_MODEL_LOADER_H
