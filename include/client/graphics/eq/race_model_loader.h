#ifndef EQT_GRAPHICS_RACE_MODEL_LOADER_H
#define EQT_GRAPHICS_RACE_MODEL_LOADER_H

#include "s3d_loader.h"
#include "zone_geometry.h"
#include "animated_mesh_scene_node.h"
#include "race_codes.h"
#include "equipment_textures.h"
#include <irrlicht.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <list>

// Forward declarations
namespace EQT { namespace Graphics { struct EntityAppearance; class GraphicsArchiveIndex; } }

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
    EQAnimatedMesh* getAnimatedMeshForRace(uint16_t raceId, uint8_t gender = 0);

    // Get an animated mesh with appearance-based variant selection
    // headVariant: which head mesh to use (0 = default)
    // bodyVariant: which body mesh to use (0 = default)
    // textureVariant: equipment texture (0=naked, 1=leather, 2=chain, 3=plate, 10+=robes)
    EQAnimatedMesh* getAnimatedMeshWithAppearance(uint16_t raceId, uint8_t gender,
                                                   uint8_t headVariant, uint8_t bodyVariant,
                                                   uint8_t textureVariant = 0);

    // Create an animated mesh scene node for a race
    // The caller is responsible for adding the node to the scene
    EQAnimatedMeshSceneNode* createAnimatedNode(uint16_t raceId, uint8_t gender,
                                                  irr::scene::ISceneNode* parent = nullptr,
                                                  irr::s32 id = -1);

    // Create an animated mesh scene node with appearance-based variants
    EQAnimatedMeshSceneNode* createAnimatedNodeWithAppearance(uint16_t raceId, uint8_t gender,
                                                               uint8_t headVariant, uint8_t bodyVariant,
                                                               irr::scene::ISceneNode* parent = nullptr,
                                                               irr::s32 id = -1);

    // Create an animated mesh scene node with full equipment appearance
    EQAnimatedMeshSceneNode* createAnimatedNodeWithEquipment(uint16_t raceId, uint8_t gender,
                                                              const EntityAppearance& appearance,
                                                              irr::scene::ISceneNode* parent = nullptr,
                                                              irr::s32 id = -1);

    // Get race model data
    std::shared_ptr<RaceModelData> getRaceModelData(uint16_t raceId, uint8_t gender = 0);

    // Get mesh builder (for registering pre-uploaded textures in multi-frame pipeline)
    ZoneMeshBuilder* getMeshBuilder() { return meshBuilder_.get(); }

    // Background-safe: loads S3D model data + merges animations into staging cache.
    // Does NOT create textures, Irrlicht meshes, or scene nodes (no GL calls).
    // Reads from immutable archives (globalCharacters_, etc.) and writes to
    // preparedModelData_ (thread-safe staging map). Call promotePreparedModels()
    // on the main thread to move data into loadedModels_ before getMeshForRace().
    bool preloadModelData(uint16_t raceId, uint8_t gender);

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

    // Reference counting for mesh cache eviction
    // Call addMeshRef when creating an entity with this race/gender
    // Call removeMeshRef when removing that entity
    void addMeshRef(uint16_t raceId, uint8_t gender);
    void removeMeshRef(uint16_t raceId, uint8_t gender);

    // Accept pre-built global assets from background thread (avoids re-parsing archives)
    void adoptGlobalAssets(
        std::vector<std::shared_ptr<CharacterModel>>&& globalCharacters,
        std::map<std::string, std::shared_ptr<TextureInfo>>&& globalTextures,
        std::map<int, std::vector<std::shared_ptr<CharacterModel>>>&& numberedGlobalCharacters,
        std::map<int, std::map<std::string, std::shared_ptr<TextureInfo>>>&& numberedGlobalTextures,
        std::map<std::string, ArmorTextureRef>&& armorTextureIndex);

    // Thread-safe static loaders (no GL, no Irrlicht — for background thread)
    static bool loadGlobalModelsStatic(const std::string& clientPath,
        std::vector<std::shared_ptr<CharacterModel>>& outCharacters,
        std::map<std::string, std::shared_ptr<TextureInfo>>& outTextures);

    static bool loadNumberedGlobalModelsStatic(const std::string& clientPath,
        std::map<int, std::vector<std::shared_ptr<CharacterModel>>>& outCharacters,
        std::map<int, std::map<std::string, std::shared_ptr<TextureInfo>>>& outTextures);

    static bool loadArmorTextureIndexStatic(const std::string& clientPath,
        std::map<std::string, ArmorTextureRef>& outIndex);

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

    // Staging cache for background-preloaded model data.
    // Written by EntityPrepWorker thread (preloadModelData).
    // Promoted to loadedModels_ on main thread via promotePreparedModels().
    mutable std::mutex preparedDataMutex_;
    std::map<uint32_t, std::shared_ptr<RaceModelData>> preparedModelData_;

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

    // Cache of animated meshes
    std::map<uint32_t, EQAnimatedMesh*> animatedMeshCache_;

    // Cache of variant animated meshes (key includes head/body variant)
    std::map<uint64_t, EQAnimatedMesh*> variantAnimatedMeshCache_;

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

    // Reference counts for cached meshes (cache key -> ref count)
    std::map<uint32_t, int> meshRefCounts_;

    // Temporary storage for vertex data during animated mesh building
    // (populated by buildMeshFromGeometry, consumed by getAnimatedMeshForRace)
    std::vector<irr::video::S3DVertex> originalVerticesForAnimation_;
    std::vector<VertexMapping> vertexMappingForAnimation_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RACE_MODEL_LOADER_H
