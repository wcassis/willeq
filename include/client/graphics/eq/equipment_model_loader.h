#ifndef EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H
#define EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H

#include "s3d_loader.h"
#include "zone_geometry.h"
#include <irrlicht.h>
#include <map>
#include <memory>
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
class EquipmentModelLoader {
public:
    EquipmentModelLoader(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                         irr::io::IFileSystem* fileSystem);
    ~EquipmentModelLoader();

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

private:
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

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;
    std::unique_ptr<ZoneMeshBuilder> meshBuilder_;

    std::string clientPath_;

    // Database item ID -> IT model number mapping
    std::map<uint32_t, int> itemToModelMap_;

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

    bool archivesLoaded_ = false;
    bool mappingLoaded_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H
