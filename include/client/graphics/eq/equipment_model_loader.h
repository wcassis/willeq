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

    // Get number of loaded equipment models
    size_t getLoadedModelCount() const { return equipmentModels_.size(); }

    // Get number of item ID mappings
    size_t getMappingCount() const { return itemToModelMap_.size(); }

    // Get equipment model data by model ID (for debugging/inspection)
    const EquipmentModelData* getEquipmentModelData(int modelId) const {
        auto it = equipmentModels_.find(modelId);
        return (it != equipmentModels_.end()) ? it->second.get() : nullptr;
    }

private:
    // Load equipment models from a single S3D archive
    bool loadEquipmentArchive(const std::string& archivePath);

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

    // IT model ID -> equipment model data
    std::map<int, std::shared_ptr<EquipmentModelData>> equipmentModels_;

    // IT model ID -> cached Irrlicht mesh
    std::map<int, irr::scene::IMesh*> meshCache_;

    // Textures loaded from equipment archives
    std::map<std::string, std::shared_ptr<TextureInfo>> textures_;

    bool archivesLoaded_ = false;
    bool mappingLoaded_ = false;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_EQUIPMENT_MODEL_LOADER_H
